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

#include "capn_connection.h"

using namespace crimson;
using namespace crimson::net;
using namespace capn;

namespace {

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
      auto p = unaligned_cast<uint32_t>(data.get());
      return ::net::ntoh(*p) + 1;
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

future<segment_t> read_segment(input_stream<char>& in, uint32_t size)
{
  return in.read_exactly(size).then(
    [size] (auto data) {
      if (data.size() < size)
        throw ProtocolError("failed to read segment: expected size " +
                             std::to_string(size) + ", got " +
                             std::to_string(data.size()));
      return data;
    });
}

template <class Iter>
future<segment_array_t> read_segments(Iter first, Iter last,
                                      segment_array_t&& segments,
                                      input_stream<char>& in)
{
  if (first == last)
    return make_ready_future<segment_array_t>(std::move(segments));

  auto size = ::net::ntoh(*first++);
  return read_segment(in, size).then(
    [first, last, segments = std::move(segments), &in] (auto&& s) mutable {
      segments.push_back(std::move(s));
      return read_segments(first, last, std::move(segments), in);
    });
}


future<> write_segment_count(uint32_t count, output_stream<char>& out)
{
  auto data = ::net::hton(count - 1);
  return out.write(reinterpret_cast<const char*>(&data), 4);
}

template <class Iter>
future<> write_segment_sizes(Iter begin, Iter end, output_stream<char>& out)
{
  return do_for_each(begin, end,
    [&out] (auto segment) {
      uint32_t size = segment.asBytes().size(); // size in bytes
      uint32_t data = ::net::hton(size);
      return out.write(reinterpret_cast<const char*>(&data), 4);
    });
}

} // anonymous namespace

future<SegmentMessageReader> CapnConnection::read_message()
{
  return read_segment_count(in).then(
    [this] (auto count) {
      return read_segment_sizes(in, count).then(
        [this, count] (auto sizes) {
          // allocate a segment array
          segment_array_t segments;
          segments.reserve(count);
          return read_segments(unaligned_cast<uint32_t>(sizes.begin()),
                               unaligned_cast<uint32_t>(sizes.end()),
                               std::move(segments), in).then(
            [] (segment_array_t&& segments) {
              return SegmentMessageReader(std::move(segments));
            });
        });
    });
}

future<> CapnConnection::write_frame(kj_segment_array_t segments)
{
  return write_segment_count(segments.size(), out).then(
    [this, segments] {
      return write_segment_sizes(segments.begin(), segments.end(), out);
    }).then([this, segments] {
      return do_for_each(segments.begin(), segments.end(),
        [this] (auto segment) {
          auto s = segment.asBytes();
          return out.write(reinterpret_cast<const char*>(s.begin()), s.size());
        });
    });
}
