// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

// Crimson: a prototype high performance OSD

// Copyright (C) 2015 Adam C. Emerson <aemerson@redhat.com
//
// Based on MemStore work by Casey Bodley <cbodley@redhat.com>
// and the Ceph object store which is Copyright (C) 2004-2006
// Sage Weil <sage@newdream.net>
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

/// \file mem/Object.cc
/// \brief Fast, in-memory objects and the functions that implement them
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#include "Object.h"

namespace crimson {
  /// Storage interface
  namespace store {
    /// Memory based object store
    namespace mem {
      future<IovecRef> read(const Range& r) const {
	check_range(r);
	return data.read(r);
      }

      future<> write(IovecRef iov) {
	return data.write(iov);
      }

      future<> zero(const Range& r) {
	if (data_len < range.offset + range.length)
	  data_len = range.offset + range.length;
	return data.hole_punch(r);
      }

      future<> hole_punch(const Range& r) {
	if (data_len < range.offset + range.length)
	  data_len = range.offset + range.length;
	return data.hole_punch(r);
      }
    } // namespace mem
  } // namespace store
} // namespace crimson
