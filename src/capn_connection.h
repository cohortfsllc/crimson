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
#pragma once

#include "connection.h"
#include <kj/debug.h>
#include <capnp/message.h>
#include <vector>

namespace crimson {
namespace net {

namespace capn {

/// Buffer segments are represented by seastar's temporary_buffer, which
/// provides ownership semantics that we use to control the buffer lifecycle
using segment_t = temporary_buffer<char>;
using segment_array_t = std::vector<segment_t>;

/// 64-bit words are the unit of capnp buffer segments
using capnp::word;
/// capnp::MessageReader/Builder deal with buffer segments as kj::ArrayPtrs,
/// which have no ownership semantics
using kj_segment_t = kj::ArrayPtr<const word>;
using kj_segment_array_t = kj::ArrayPtr<const kj_segment_t>;

/// Construct a kj_segment_t that points to the buffer owned by a segment_t
inline kj_segment_t kj_segment_cast(segment_t& s) {
  KJ_REQUIRE(s.size() % sizeof(word) == 0, "kj_segment_cast would truncate");
  return {reinterpret_cast<const word*>(s.begin()), s.size() / sizeof(word)};
}

/// Write a hex dump of a segment_t
inline std::ostream& operator<<(std::ostream& out, segment_t& rhs) {
  out << std::hex;
  auto fill = out.fill('0');
  for (auto c : rhs) out << static_cast<uint32_t>(c);
  out.fill(fill);
  return out << std::dec;
}

/// Write a hex dump of a kj_segment_t
inline std::ostream& operator<<(std::ostream& out, kj_segment_t& rhs) {
  out << std::hex;
  auto fill = out.fill('0');
  for (auto c : rhs.asBytes()) out << static_cast<uint32_t>(c);
  out.fill(fill);
  return out << std::dec;
}

/// A MessageReader similar to capnp::SegmentArrayMessageReader, except that it
/// takes ownership of the given segments. That means it must not be destructed
/// while there are outstanding references to its segments.
class SegmentMessageReader final : public capnp::MessageReader {
  segment_array_t segments; //< buffers from the input stream
 public:
  SegmentMessageReader(segment_array_t&& segments,
                       capnp::ReaderOptions options = capnp::ReaderOptions())
    : MessageReader(options), segments(std::move(segments)) {}

  /// Returns an ArrayPtr to the given buffer segment
  kj::ArrayPtr<const capnp::word> getSegment(uint id) override {
    if (id >= segments.size())
      return nullptr;
    return kj_segment_cast(segments[id]);
  }
};

class ProtocolError : public std::runtime_error {
 public:
  ProtocolError(const std::string& msg) : std::runtime_error(msg) {}
};

} // namespace capn

/// A Connection that reads and writes Cap'n Proto messages.
class CapnConnection : public Connection {
  future<> write_frame(capn::kj_segment_array_t segments);

 public:
  CapnConnection(connected_socket&& socket, socket_address address)
    : Connection(std::move(socket), address) {}

  /// Read a message from the Connection's input stream
  future<capn::SegmentMessageReader> read_message();

  /// Write a message to the Connection's output stream
  template <class MessageBuilder>
  future<> write_message(MessageBuilder&& message);
};

template <class MessageBuilder>
future<> CapnConnection::write_message(MessageBuilder&& message)
{
  // write the segment count, sizes, and data
  return write_frame(message->getSegmentsForOutput()).then(
    [this] { return out.flush(); }
  ).finally([message] {});
}

} // namespace net
} // namespace crimson
