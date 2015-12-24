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

#include "socket_messenger.h"
#include "common/buffer_array_message_reader.h"
#include "kj/debug.h"

using namespace crimson;
using namespace crimson::net;

namespace {

using tmp_buf = temporary_buffer<char>;

// The following functions implement the segment framing procol recommended
// here: https://capnproto.org/encoding.html#serialization-over-a-stream
//
// """
// When transmitting over a stream, the following should be sent. All integers
// are unsigned and little-endian.
//
// (4 bytes) The number of segments, minus one (since there is always at
//     least one segment).
// (N * 4 bytes) The size of each segment, in words.
// (0 or 4 bytes) Padding up to the next word boundary.
// The content of each segment, in order.
// """

future<uint32_t> read_segment_count(input_stream<char>& in)
{
  return in.read_exactly(sizeof(uint32_t)).then(
    [] (auto data) {
      KJ_REQUIRE(data.size() == sizeof(uint32_t), "eof reading segment count");
      return *unaligned_cast<uint32_t>(data.get()) + 1;
    });
}

future<tmp_buf> read_segment_sizes(input_stream<char>& in, uint32_t count)
{
  // read the sizes, including padding for word alignment
  auto aligned_size = align_up(count * sizeof(uint32_t), sizeof(word));

  return in.read_exactly(aligned_size).then(
    [aligned_size] (auto data) {
      KJ_REQUIRE(data.size() == aligned_size, "eof reading segment sizes");
      return data;
    });
}

future<std::vector<tmp_buf>> read_segments(input_stream<char>& in,
                                           uint32_t count, tmp_buf&& sizes) {
  std::vector<tmp_buf> segments;
  segments.reserve(count);

  auto begin = unaligned_cast<uint32_t>(sizes.begin());
  auto end = begin + count; // use count, as sizes.size() may include padding

  // read the segment for each size
  auto f = do_for_each(begin, end,
    [&in, &segments] (auto size) {
      KJ_REQUIRE(size > 0, "requires non-zero segment size");
      return in.read_exactly(size).then(
        [&segments] (auto data) {
          segments.emplace_back(std::move(data));
        });
    });
  // return the accumulated segments
  return f.then([segments = std::move(segments)] () mutable {
      return std::move(segments);
    });
}

future<std::vector<tmp_buf>> read_frame(input_stream<char>& in)
{
  return read_segment_count(in).then(
    [&in] (auto count) {
      return read_segment_sizes(in, count).then(
        [&in, count] (auto sizes) {
          return read_segments(in, count, std::move(sizes));
        });
    });
}

future<> write_segment_count(uint32_t count, output_stream<char>& out)
{
  auto data = count - 1;
  return out.write(reinterpret_cast<const char*>(&data), sizeof(uint32_t));
}

future<> write_segment_sizes(kj::ArrayPtr<const kj::ArrayPtr<const word>> segments,
                             output_stream<char>& out)
{
  return do_for_each(segments.begin(), segments.end(),
    [&out] (auto segment) {
      uint32_t data = segment.asBytes().size(); // size in bytes
      return out.write(reinterpret_cast<const char*>(&data), sizeof(uint32_t));
    }).then([&out, &segments] {
      if (segments.size() % 2 == 0)
        return now();
      // pad for word alignment
      uint32_t data = 0;
      return out.write(reinterpret_cast<const char*>(&data), sizeof(uint32_t));
    });
}

future<> write_frame(kj::ArrayPtr<const kj::ArrayPtr<const word>> segments,
                     output_stream<char>& out)
{
  return write_segment_count(segments.size(), out).then(
    [&out, segments] {
      return write_segment_sizes(segments, out);
    }).then([&out, segments] {
      return do_for_each(segments.begin(), segments.end(),
        [&out] (auto segment) {
          auto s = segment.asBytes();
          return out.write(reinterpret_cast<const char*>(s.begin()), s.size());
        });
    });
}

auto make_listener(socket_address address)
{
  listen_options lo;
  lo.reuse_address = true;
  return engine().listen(address, lo);
}

} // anonymous namespace

future<Connection::MessageReaderPtr> SocketConnection::read_message()
{
  return read_frame(in).then([] (auto segments) -> MessageReaderPtr {
      return std::make_unique<BufferArrayMessageReader<>>(std::move(segments));
    });
}

future<> SocketConnection::write_message(MessageBuilderPtr&& message)
{
  // write the segment count, sizes, and data
  auto segments = message->getSegmentsForOutput();
  return write_frame(std::move(segments), out).then(
    [this] { return out.flush(); }
  ).finally([message = std::move(message)] {});
}

future<> SocketConnection::close()
{
  return out.close();
}


SocketListener::SocketListener(socket_address address)
  : listener(make_listener(address))
{
}

future<shared_ptr<Connection>> SocketListener::accept()
{
  return listener.accept().then(
    [] (auto socket, auto addr) -> shared_ptr<Connection> {
      return make_shared<SocketConnection>(std::move(socket), addr);
    });
}

future<> SocketListener::close()
{
  listener.abort_accept();
  return now();
}
