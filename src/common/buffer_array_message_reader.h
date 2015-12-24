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

#include <core/temporary_buffer.hh>
#include <capnp/message.h>
#include <gsl.h>
#include <iomanip>
#include <vector>

namespace crimson {

using capnp::byte;
/// 64-bit words are the unit of capnp buffer segments
using capnp::word;

/// Construct a kj::ArrayPtr that points to an owned temporary_buffer
template <typename T, typename C>
inline kj::ArrayPtr<const T> kj_buffer_cast(const temporary_buffer<C>& s)
{
  Expects(s.size() % sizeof(T) == 0); // would truncate the buffer
  return {reinterpret_cast<const T*>(s.get()), s.size() / sizeof(T)};
}

/// Write a hex dump of a temporary_buffer
template <typename C>
inline std::ostream& operator<<(std::ostream& out,
                                const temporary_buffer<C>& rhs)
{
  // cast to an ArrayPtr of bytes
  return out << kj_buffer_cast<byte>(rhs);
}

/// Write a hex dump of a generic ArrayPtr
template <typename C>
inline std::ostream& operator<<(std::ostream& out, const kj::ArrayPtr<C>& rhs)
{
  // cast to an ArrayPtr of bytes
  return out << rhs.asBytes();
}

/// specialization for an ArrayPtr of bytes
template <>
inline std::ostream& operator<< <const byte>(std::ostream& out,
                                             const kj::ArrayPtr<const byte>& rhs)
{
  out << std::hex;
  auto fill = out.fill('0');
  for (auto c : rhs) out << static_cast<uint32_t>(c);
  out.fill(fill);
  return out << std::dec;
}


/// A MessageReader similar to capnp::SegmentArrayMessageReader, except that it
/// takes ownership of the given segments. That means it must not be destructed
/// while there are outstanding references to its segments.
template <typename C = char,
          typename Segment = temporary_buffer<C>,
          typename Array = std::vector<Segment>>
class BufferArrayMessageReader final : public capnp::MessageReader {
  using Opts = capnp::ReaderOptions;
  Array segments;
 public:
  BufferArrayMessageReader(Array&& segments, Opts options = Opts())
    : MessageReader(options), segments(std::move(segments)) {}

  /// Returns an ArrayPtr to the given buffer segment
  kj::ArrayPtr<const word> getSegment(uint id) override {
    if (id >= segments.size())
      return nullptr;
    return kj_buffer_cast<word>(segments[id]);
  }
};

} // namespace crimson
