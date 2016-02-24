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

/// \file mem/Store.h
/// \brief Fast, in-memory object store
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#ifndef CRIMSON_STORE_MEM_H
#define CRIMSON_STORE_MEM_H

#include <utility>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/random_generator.hpp>

#include <core/slab.hh>

#include "common/xxHash.h"

#include "store/Store.h"

#include "Collection.h"


namespace crimson {
  /// Storage interface
  namespace store {
  /// Memory-backed storage
    namespace mem {
      class Store : public crimson::store::Store {
      private:
	boost::uuids::uuid id;
	std::vector<foreign_ptr<unique_ptr<
	  std::map<string, MemColRef>>>> maps;
	static const unsigned cpu = 0;
	size_t ref_cnt = 0;

	bool local() const {
	  return engine().cpu_id() == cpu;
	}

      public:
	Store() noexcept : id(boost::uuids::random_generator()()) {
	  Expects(engine().cpu_id() == cpu);

	  auto maker = [] {
	    return make_foreign(make_unique<std::map<string,MemColRef>>());
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

	virtual ~Store() = default;
	Store(const Store& o) = delete;
	const Store& operator=(const Store& o) = delete;
	Store(Store&& o) = delete;
	const Store& operator=(Store&& o) = delete;

	unsigned cpu_for(const string& cid) const override {
	  return xxHash()(cid) % smp::count;
	}

	void ref() override;
	void unref() override;

	// mgmt
	size_t get_max_object_name_length() const noexcept override {
	  return 1<<10;
	}
	size_t get_max_attr_name_length() const noexcept override {
	  return 1<<10;
	}
	future<> mkfs() override {
	  return make_ready_future<>();
	}

	/// Get the CPU to look up a collection
	unsigned get_cpu(const string& cid) const noexcept override {
	  return xxHash()(cid) % smp::count;
	}

	/**
	 * Set and get internal fsid for this instance. No external data
	 * is modified
	 */
	future<> set_fsid(boost::uuids::uuid u) override {
	  id = u;
	  return make_ready_future<>();
	}
	future<boost::uuids::uuid> get_fsid() const override {
	  return make_ready_future<boost::uuids::uuid>(id);
	}

	/// Look up a collection
	future<CollectionRef> lookup_collection(string cid) const override;

	/// Make a collection
	future<CollectionRef> create_collection(string cid) override;
	/// Enumerate all collections in this store
	///
	/// \note Ceph ObjectStore just returns them all at once. Do we
	/// think we'll need cursor-like logic the way we do for
	/// attribute and object enumeration?
	future<held_span<string>> enumerate_collections() const override {
	  return make_exception_future<held_span<string>>(
	    std::system_error(errc::operation_not_supported));
	}
	/// Commit the entire Store
	///
	/// All of it. No questions asked. This function acts as a
	/// barrier on all operations. No operations may begin until all
	/// outstanding ones are completed and stored stably.
	future<> commit() override {
	  return make_exception_future<>(
	    std::system_error(errc::operation_not_supported));
	}
      };
    } // namespace mem
  } // namespace store
} // namespace crimson

#endif // CRIMSON_STORE_NIHIL_H
