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

#include "message_helpers.h"
#include <core/unaligned.hh>

using namespace crimson;

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
      Ensures(data.size() == sizeof(uint32_t));
      return *unaligned_cast<uint32_t>(data.get()) + 1;
    });
}

future<tmp_buf> read_segment_sizes(input_stream<char>& in, uint32_t count)
{
  // read the sizes, including padding for word alignment
  auto aligned_size = align_up(count * sizeof(uint32_t), sizeof(word));
  aligned_size -= sizeof(uint32_t); // adjust for the count we already read

  return in.read_exactly(aligned_size).then(
    [aligned_size] (auto data) {
      Ensures(data.size() == aligned_size);
      return data;
    });
}

template <typename SizeIter, typename BufferIter>
future<> read_next_segment(input_stream<char>& in, SizeIter size, SizeIter last,
                           BufferIter buffer)
{
  if (size == last) {
    return now();
  }
  Expects(*size > 0);
  auto expected_size = *size * sizeof(word);
  return in.read_exactly(expected_size).then(
    [buffer, expected_size] (auto data) {
      Ensures(data.size() == expected_size);
      *buffer = std::move(data);
    }).then([&in, size, last, buffer] {
      return read_next_segment(in, size + 1, last, buffer + 1);
    });
}

future<std::vector<tmp_buf>> read_segments(input_stream<char>& in,
                                           uint32_t count, tmp_buf&& sizes)
{
  auto begin = unaligned_cast<uint32_t>(sizes.begin());
  auto end = begin + count; // use count, as sizes.size() may include padding

  std::vector<tmp_buf> segments(count);
  auto buffer = segments.data();

  return read_next_segment(in, begin, end, buffer).then(
    [z = std::move(sizes), s = std::move(segments)] () mutable {
      return std::move(s);
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

future<> write_segment_count(output_stream<char>& out, uint32_t count)
{
  auto data = count - 1;
  return out.write(reinterpret_cast<const char*>(&data), sizeof(uint32_t));
}

future<> write_segment_sizes(output_stream<char>& out,
                             kj::ArrayPtr<const kj::ArrayPtr<const word>> segments)
{
  return do_for_each(segments.begin(), segments.end(),
    [&out] (auto segment) {
      uint32_t data = segment.size(); // size in words
      return out.write(reinterpret_cast<const char*>(&data), sizeof(uint32_t));
    }).then([&out, &segments] {
      if (segments.size() % 2 == 1)
        return now();
      // pad for word alignment
      uint32_t data = 0;
      return out.write(reinterpret_cast<const char*>(&data), sizeof(uint32_t));
    });
}

future<> write_frame(output_stream<char>& out,
                     kj::ArrayPtr<const kj::ArrayPtr<const word>> segments)
{
  return write_segment_count(out, segments.size()).then(
    [&out, segments] {
      return write_segment_sizes(out, segments);
    }).then([&out, segments] {
      return do_for_each(segments.begin(), segments.end(),
        [&out] (auto segment) {
          auto s = segment.asChars();
          return out.write(s.begin(), s.size());
        });
    });
}

} // anonymous namespace

namespace crimson {

future<std::unique_ptr<capnp::MessageReader>> readMessage(input_stream<char>& in)
{
  return read_frame(in).then(
    [] (auto segments) -> std::unique_ptr<capnp::MessageReader> {
      return std::make_unique<BufferArrayMessageReader>(std::move(segments));
    });
}

future<> writeMessage(output_stream<char>& out, capnp::MessageBuilder& message)
{
  // write the segment count, sizes, and data
  return write_frame(out, message.getSegmentsForOutput());
}

} // namespace crimson
