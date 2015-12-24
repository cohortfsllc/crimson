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
#include "common/segment_message_reader.h"
#include <algorithm>

using namespace crimson;
using namespace crimson::net;

namespace {

class ProtocolError : public std::runtime_error {
 public:
  ProtocolError(const std::string& msg) : std::runtime_error(msg) {}
};

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
  return in.read_exactly(4).then(
    [] (auto data) {
      if (data.size() != 4)
        throw ProtocolError("failed to read segment count");
      return *unaligned_cast<uint32_t>(data.get()) + 1;
    });
}

future<segment_t> read_segment_sizes(input_stream<char>& in, uint32_t count)
{
  return in.read_exactly(4*count).then(
    [count] (auto data) {
      if (data.size() < 4*count)
        throw ProtocolError("failed to read segment sizes");
      return data;
    });
}

/// An input_stream consumer that collects buffer segments for the message.
/// We're given the array of segment sizes, but we only care about their sum
/// because we know input_stream::consume() will give us zero-copy buffers.
template <class CharType>
class SegmentConsumer {
  segment_array_t segments;
  uint32_t bytes_remaining;

  /// return the total message size
  static uint32_t total_bytes(segment_t& sizes) {
    return std::accumulate(unaligned_cast<uint32_t>(sizes.begin()),
                           unaligned_cast<uint32_t>(sizes.end()), 0);
  }

 public:
  SegmentConsumer(segment_t&& sizes) : bytes_remaining(total_bytes(sizes)) {
    segments.reserve(sizes.size());
  }

  segment_array_t take_segments() { return std::move(segments); }

  using unconsumed_remainder = typename input_stream<CharType>::unconsumed_remainder;
  using tmp_buf = temporary_buffer<CharType>;

  future<unconsumed_remainder> operator()(tmp_buf data) {
    // return an empty buffer to declare that we're done
    if (bytes_remaining == 0)
      return make_ready_future<unconsumed_remainder>(tmp_buf{});

    // return an undefined remainder to ask for another buffer
    if (data.empty())
      return make_ready_future<unconsumed_remainder>(std::experimental::nullopt);

    if (bytes_remaining > data.size()) {
      // take the whole buffer and ask for another
      bytes_remaining -= data.size();
      segments.emplace_back(std::move(data));
      return make_ready_future<unconsumed_remainder>(std::experimental::nullopt);
    }

    // chop off the bytes we need and return the remainder
    auto len = data.size() - bytes_remaining;
    auto remainder = data.share(bytes_remaining, len);

    data.trim(bytes_remaining);
    segments.emplace_back(std::move(data));

    bytes_remaining = 0;
    return make_ready_future<unconsumed_remainder>(std::move(remainder));
  }
};

future<> write_segment_count(uint32_t count, output_stream<char>& out)
{
  auto data = count - 1;
  return out.write(reinterpret_cast<const char*>(&data), 4);
}

template <class Iter>
future<> write_segment_sizes(Iter begin, Iter end, output_stream<char>& out)
{
  return do_for_each(begin, end,
    [&out] (auto segment) {
      uint32_t data = segment.asBytes().size(); // size in bytes
      return out.write(reinterpret_cast<const char*>(&data), 4);
    });
}

future<> write_frame(kj_segment_array_t segments, output_stream<char>& out)
{
  return write_segment_count(segments.size(), out).then(
    [&out, segments] {
      return write_segment_sizes(segments.begin(), segments.end(), out);
    }).then([&out, segments] {
      return do_for_each(segments.begin(), segments.end(),
        [&out] (auto segment) {
          auto s = segment.asBytes();
          return out.write(reinterpret_cast<const char*>(s.begin()), s.size());
        });
    });
}

} // anonymous namespace

future<Connection::MessageReaderPtr> SocketConnection::read_message()
{
  return read_segment_count(in).then(
    [this] (auto count) {
      return read_segment_sizes(in, count).then(
        [this, count] (auto sizes) {
          SegmentConsumer<char> c(std::move(sizes));
          auto consume = in.consume(c);
          return consume.then([c = std::move(c)] () mutable -> MessageReaderPtr {
              return std::make_unique<SegmentMessageReader>(c.take_segments());
            });
        });
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

static auto make_listen(socket_address address)
{
  listen_options lo;
  lo.reuse_address = true;
  return engine().listen(address, lo);
}

SocketListener::SocketListener(socket_address address)
  : listener(make_listen(address))
{
}

future<shared_ptr<Connection>> SocketListener::accept()
{
  return listener.accept().then(
    [] (auto socket, auto addr) {
      auto conn = make_shared<SocketConnection>(std::move(socket), addr);
      return make_ready_future<shared_ptr<Connection>>(conn);
    });
}

future<> SocketListener::close()
{
  listener.abort_accept();
  return now();
}
