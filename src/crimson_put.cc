// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

// Crimson: a prototype high performance OSD

// Copyright (C) 2015 Casey Bodley <cbodley@redhat.com>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2.1 of
// the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
// 02110-1301 USA

#include <iostream>

#include "capn_connection.h"
#include "crimson.capnp.h"
#include <core/app-template.hh>

using namespace crimson;
using namespace crimson::net;
using crimson::net::capn::SegmentMessageReader;
using crimson::proto::Message;

namespace {

using buffer_t = temporary_buffer<capnp::byte>;

// Reads blocks from the given fd with dma_read()
class BlockReader {
  file fd;
  uint64_t pos;
  size_t size;
  size_t block_size;

 public:
  BlockReader() : pos(0), size(0), block_size(0) {}

  BlockReader(file fd, uint64_t pos, size_t size, size_t block_size);

  // Read blocks starting at offset 0 and ending at size, calling the given
  // func() for each block with the parameters (uint64_t pos, buffer_t&&)
  template <class Func>
  future<> read(Func func);
};

BlockReader::BlockReader(file fd, uint64_t pos, size_t size, size_t block_size)
  : fd(fd), pos(pos), size(size), block_size(block_size)
{
  auto align = fd.disk_read_dma_alignment();
  KJ_REQUIRE(pos % align == 0, "dma_read() requires offsets with " +
             std::to_string(align) + "-byte alignment");
  KJ_REQUIRE(block_size % align == 0, "dma_read() requires block size in "
             "multiples of " + std::to_string(align));
}

template <class Func>
future<> BlockReader::read(Func func)
{
  if (pos >= size)
    return now();

  std::cout << "reading " << pos << " / " << size << std::endl;

  // read the block
  return fd.dma_read<capnp::byte>(pos, block_size).then(
    [this, func] (auto data) {
      if (data.empty())
        return now();
      // process the buffer with func()
      return func(pos, std::move(data)).then(
        [this, func] {
          // read the next block
          pos += block_size;
          return this->read(func);
        });
    });
}

// Tracks replies for BlockSender
class ReplyTracker {
  lw_shared_ptr<CapnConnection> conn;
  future<> reader; // result of read()
  future<> replies; // chain of unresolved replies
  std::exception_ptr err;
  semaphore& sem;

  struct Entry {
    uint32_t flags; // write flags we still expect to see
    promise<> prom;
  };
  using entry_map_t = std::unordered_map<uint32_t, Entry>;
  using entry_pair_t = entry_map_t::value_type;

  // maintain an entry for each sequence id for which we still expect a reply
  entry_map_t entries;

  // reads replies from the connection, and signals the semaphore for each
  future<> read();

  void on_reply(proto::Message::Reader message);

 public:
  ReplyTracker(lw_shared_ptr<CapnConnection> conn, semaphore& sem)
    : conn(conn), reader(read()), replies(now()), sem(sem) {}

  // register a sequence id and the kind of replies that we expect
  void expect(uint32_t sequence, uint32_t flags);

  // return a future that resolves when once all expected replies are received
  future<> wait_for_all() { return std::move(replies); }

  // if an exception was thrown in read(), throw it to the caller
  void throw_on_error() noexcept(false) {
    if (err)
      std::rethrow_exception(err);
  }
};

future<> ReplyTracker::read()
{
  return do_until(
    [this] { return conn->in.eof(); },
    [this] {
      return conn->read_message().then(
        [this] (SegmentMessageReader&& reader) {
          on_reply(reader.getRoot<Message>());
        }).handle_exception([this] (auto eptr) {
          // save the exception to be thrown in send()
          err = eptr;
          sem.signal();
        });
    });
}

void ReplyTracker::on_reply(proto::Message::Reader message)
{
  auto sequence = message.getHeader().getSequence();
  auto reply = message.getOsdWriteReply();
  if (reply.isErrorCode()) {
    std::cerr << "osd_write_reply seq " << sequence
        << " failed with " << reply.getErrorCode() << std::endl;
    throw std::runtime_error("osd_read_reply failed with " +
                             std::to_string(reply.getErrorCode()));
  }
  auto i = entries.find(sequence);
  if (i == entries.end()) {
    std::cerr << "osd_write_reply dropping unexpected seq "
        << sequence << std::endl;
    return;
  }
  auto& entry = i->second;
  std::cerr << "osd_write_reply seq " << sequence
      << " flags " << std::hex << reply.getFlags() << std::dec << std::endl;

  uint32_t flags = entry.flags & reply.getFlags();
  if (flags & proto::osd::write::ON_APPLY)
    sem.signal(); // allow another request through

  entry.flags &= ~flags;
  if (entry.flags == 0) {
    entry.prom.set_value(); // complete the promise
    entries.erase(i);
  }
}

void ReplyTracker::expect(uint32_t sequence, uint32_t flags)
{
  auto result = entries.emplace(sequence, Entry{flags});
  KJ_REQUIRE(result.second, "duplicate entry found for sequence " +
             std::to_string(sequence));
  auto& entry = result.first->second;
  auto f = entry.prom.get_future();
  replies = replies.then([f = std::move(f)] () mutable { return std::move(f); });
}

// Sends a write request for every buffer given to send()
class BlockSender {
  lw_shared_ptr<CapnConnection> conn;
  std::string object; // object name to be written
  semaphore sem; // throttle the number of outstanding messages
  uint32_t next_sequence = 0;
  ReplyTracker replies;

  future<> read_replies();

 public:
  BlockSender() = default;

  BlockSender(lw_shared_ptr<CapnConnection> conn, std::string object,
              size_t max_requests)
    : conn(conn), object(object), sem(max_requests), replies(conn, sem) {}

  // Send an osd write request with the given buffer
  future<> send(uint64_t pos, buffer_t&& data);

  // Clean up the reply reader
  future<> close();
};

future<> BlockSender::send(uint64_t pos, buffer_t&& data)
{
  // alert the caller of any exceptions thrown by read_replies()
  replies.throw_on_error();

  return sem.wait().then(
    [this, pos, data = std::move(data)] () mutable {
      replies.throw_on_error(); // check again after the wait

      // set up a promise for the next sequence number
      auto sequence = next_sequence++;
      auto flags = proto::osd::write::ON_APPLY | proto::osd::write::ON_COMMIT;
      replies.expect(sequence, flags);

      std::cout << "osd_write seq " << sequence << " offset " << pos
          << " length " << data.size() << std::endl;
      auto builder = make_lw_shared<capnp::MallocMessageBuilder>();
      auto message = builder->initRoot<Message>();
      message.initHeader().setSequence(sequence);
      auto request = message.initOsdWrite();
      request.setObject(capnp::Text::Reader(object.c_str(), object.size()));
      request.setOffset(pos);
      request.setLength(data.size());
      request.setData(capnp::Data::Reader(data.get_write(), data.size()));
      request.setFlags(proto::osd::write::ON_APPLY | proto::osd::write::ON_COMMIT);
      return conn->write_message(builder);
    });
}

future<> BlockSender::close()
{
  // alert the caller of any exceptions thrown by read_replies()
  replies.throw_on_error();
  // wait for outstanding replies
  return replies.wait_for_all().finally(
    [this] { return conn->out.close(); });
}

} // anonymous namespace

int main(int argc, char** argv) {
  app_template crimson;

  namespace bpo = boost::program_options;
  crimson.add_options()
      ("address", bpo::value<std::string>()->default_value("127.0.0.1"),
       "Specify the osd address")
      ("port", bpo::value<uint16_t>()->default_value(6800),
       "Specify the osd port")
      ("filename", bpo::value<std::string>(),
       "Specify the source object filename")
      ("block-size", bpo::value<size_t>()->default_value(4096),
       "Specify the write block size (must be a multiple of 4096)")
      ("max-requests", bpo::value<size_t>()->default_value(32),
       "Specify the maximum number of outstanding write requests")
      ("object", bpo::value<std::string>(),
       "Specify the target object name")
      ;

  try {
    return crimson.run(argc, argv,
      [&crimson] {
        auto cfg = crimson.configuration();

        const auto filename = cfg["filename"].as<std::string>();
        auto bs = cfg["block-size"].as<size_t>();
        auto oid = cfg["object"].as<std::string>();
        auto maxreq = cfg["max-requests"].as<size_t>();

        // connect to the osd
        auto addr = make_ipv4_address({cfg["address"].as<std::string>(),
                                       cfg["port"].as<uint16_t>()});
        std::cout << "connecting to " << addr << ".." << std::endl;
        auto connect = engine().connect(addr).then(
          [oid = std::move(oid), maxreq, addr] (connected_socket fd) {
            std::cout << "connection established" << std::endl;
            auto conn = make_lw_shared<CapnConnection>(std::move(fd), addr);

            // initialize the block sender
            return make_lw_shared<BlockSender>(conn, std::move(oid), maxreq);
          });

        // open the input file
        std::cout << "opening " << filename << ".." << std::endl;
        auto open = open_file_dma(filename, open_flags::ro).then(
          [bs] (auto file) {
            std::cout << "file opened, reading size.." << std::endl;
            // read the file size
            return file.size().then(
              [bs, file] (auto size) {
                std::cout << "file size is " << size << std::endl;
                // initialize the block reader
                return make_lw_shared<BlockReader>(file, 0, size, bs);
              });
          });

        return connect.then(
          [open = std::move(open)] (auto sender) mutable {
            return open.then(
              [sender] (auto reader) {
                return reader->read(
                  [sender] (auto pos, auto data) {
                    return sender->send(pos, std::move(data));
                  }).finally([reader] {});
              }).then([] {
                std::cout << "completed" << std::endl;
              }).then([sender] {
                return sender->close();
              }).finally([sender] {});
          });
      });
  } catch (std::exception& e) {
    std::cerr << "Exiting with exception: " << e.what() << std::endl;
    return 1;
  }
}
