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

/// \file mem/Collection.cc
/// \brief Fast, in-memory object collections, implementation
///
/// \author Adam C. Emerson <aemerson@redhat.com>


#include <functional>

#include "Collection.h"


namespace crimson {
  namespace store {
    namespace mem {
      void Collection::ref() {
	++ref_cnt;
      }
      void Collection::unref() {
	if (--ref_cnt == 0)
	  delete this;
      }

      unsigned Collection::on_cpu() const {
	return cpu;
      }

      future<ObjectRef> Collection::create(string oid, bool excl) {
	if (!local())
	  return smp::submit_to(
	    cpu, [this, oid = std::move(oid), excl]() {
	      return create(oid);
	    });

	auto maker = [this, oid = std::move(oid), excl](
	  std::map<string,MemObjRef>& slice) {
	  auto i = slice.find(oid);
	  if (i != slice.end()) {
	    if (excl)
	      return make_exception_future<ObjectRef>(
		std::system_error(errc::object_exists));
	    else
	      return make_ready_future<ObjectRef>(ObjectRef(i->second.get()));
	  }

	  return make_ready_future<ObjectRef>(
	    ObjectRef(new Object(CollectionRef(this), oid, slice)));
	};

	auto oid_cpu = cpu_for(oid);
	if (oid_cpu == engine().cpu_id())
	  return maker(*maps[oid_cpu]);
	else
	  return smp::submit_to(
	    oid_cpu, [&slice = *maps[oid_cpu], &maker] {
	      return maker(slice); });
      }

      future<> Collection::remove() {
	if (!local())
	  return smp::submit_to(
	    cpu, [this] {
	      return remove();
	    });

	// It's kind of crap that foreign_ptr doesn't let you see what
	// CPU it belongs to.
	return seastar::map_reduce(
	  boost::make_counting_iterator(0ul),
	  boost::make_counting_iterator(maps.size()),
	  [this](std::size_t i) {
	    return smp::submit_to(
	      i, [m = *maps[i]] {
		return m.empty();
	      });
	  }, true, std::logical_and<bool>()).then(
	    [this](bool empty) {
	      if (empty) {
		slice.erase(iter);
		return make_ready_future<>();
	      } else {
		return make_exception_future<>(
		  std::system_error(errc::collection_not_empty));
	      }
	    });
      }
    } // namespace mem
  } // namespace store
} // crimson
