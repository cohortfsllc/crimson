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

/// \file mem/Collection.h
/// \brief Fast, in-memory object collections
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#ifndef CRIMSON_STORE_MEM_COLLECTION_H
#define CRIMSON_STORE_MEM_COLLECTION_H

#include <utility>

#include "crimson.h"
#include "store/Collection.h"
#include "Object.h"

namespace crimson {
  /// Storage interface
  namespace store {
    namespace mem {
      class Collection;
      using MemColRef = boost::intrusive_ptr<Collection>;

      /// A collection is a grouping of objects.
      ///
      /// Collections have names and can be enumerated in order.  Like
      /// an individual object, a collection also has a set of xattrs.
      class Collection : public crimson::store::Collection {
	std::vector<foreign_ptr<unique_ptr<
	  std::map<string, MemObjRef>>>> maps;
	const unsigned cpu;
	size_t ref_cnt = 0;

	std::map<string, MemColRef>& slice;
	const std::map<string, MemColRef>::iterator iter;

	bool local() const {
	  return engine().cpu_id() == cpu;
	}

      public:
	Collection(StoreRef _store, string _cid,
		   std::map<string, MemColRef>& _slice)
	  : crimson::store::Collection(std::move(_store), std::move(_cid)),
	    cpu(xxHash()(cid) % smp::count), slice(_slice),
	    iter((slice.emplace(
		    std::piecewise_construct,
		    std::forward_as_tuple(cid),
		    std::forward_as_tuple(MemColRef(this)))).first) {
	  auto maker = [] {
	    return make_foreign(make_unique<std::map<string,MemObjRef>>());
	  };
	  maps.reserve(smp::count);
	  for (unsigned i = 0; i < smp::count; ++i) {
	    if (i == engine().cpu_id()) {
	      maps[i] = maker();
	    } else {
	      maps[i] = smp::submit_to(i, [&maker] { return maker(); }).get0();
	    }
	  }
	}
      public:

	virtual ~Collection() = default;

	Collection(const Collection&) = delete;
	Collection& operator =(const Collection&) = delete;
	Collection(Collection&&) = delete;
	Collection& operator =(Collection&&) = delete;


      public:

	unsigned on_cpu() const override;

	unsigned cpu_for(const string& oid) const override {
	  return xxHash()(oid) % smp::count;
	}

	void ref() override;
	void unref() override;

	/// Ensure the existance of an object in a collection
	future<ObjectRef> create(string oid, bool excl = false) override;
	/// Remove this collection
	future<> remove() override;
	/// Split this collection
	future<> split_collection(
	  store::Collection& dest,
	  function< bool(const string& oid) const> pred) override {
	  return make_exception_future<>(
	    std::system_error(errc::operation_not_supported));
	}
	/// Enumerate objects in a collection
	virtual future<std::vector<string>, OidCursorRef> enumerate_objects(
	  optional<OidCursorRef> cursor,
	  size_t to_return) const {
	  return make_exception_future<std::vector<string>, OidCursorRef>(
	    std::system_error(errc::operation_not_supported));
	}
	/// Get cursor for a given object
	///
	/// Not supported on MemStore (at least at the moment)
	future<OidCursorRef> obj_cursor(string oid) const override {
	  return make_exception_future<OidCursorRef>(
	    std::system_error(errc::operation_not_supported));
	}
      };
    } // namespace mem
  } // namespace store
} // namespace crimson

#endif // CRIMSON_STORE_MEM_COLLECTION_H
