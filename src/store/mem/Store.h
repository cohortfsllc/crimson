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

#include "common/xxHash.h"

#include "Store.h"

namespace crimson {
  /// Storage interface
  namespace store {
    namspace _mem {
      /// A collection is a grouping of objects.
      ///
      /// Collections have names and can be enumerated in order.  Like
      /// an individual object, a collection also has a set of xattrs.

      class MemCollection : public Collection, public slab_item_base {
      private:
	MemStore& mstore() {
	  return static_cast<MemStore&>(store);
	}

	std::vector<foreign_ptr<std::unique_ptr<MemObject::cache>> trees;

	MemCollection(uint32_t _slab_page_index, Store& _store, sstring _cid)
	  : Collection(_slab_page_index, _store,  _cid) {
	  trees.resize(smp::count);
	}
      public:
	static future<CollectionRef> make(Store& _store, sstring _cid) {
	  auto cpu = get_cpu(_cid);
	  Expects(cpu == engine().cpu_id());
	  auto slab = _store.collection_slab(cpu);
	  std::unique_ptr<NihilCollection> new_item(
	  slab->create(sizeof(NihilCollection), _store, _cid),
	  [slab](NihilCollection* c) {
	    slab->free(c);
	  });
	  return parallel_for_each(
	    boost::irange<unsigned>(0, smp::count),
	    [new_item] (unsigned c) mutable {
	      return smp::submit_to(c, [this, args] () mutable {
		  trees[engine().cpu_id()] =
		    make_foreign(std::make_unique<NihilObject::cache>());
		});
	    }).then([new_item] {
		return make_ready_future<CollectionRef>(new_item.release());
	      });
	}

	virtual ~MemCollection() = default;

	MemCollection(const MemCollection&) = delete;
	MemCollection& operator =(const MemCollection&) = delete;
	MemCollection(MemCollection&&) = delete;
	MemCollection& operator =(MemCollection&&) = delete;

      unsigned get_cpu(const sstring& oid) const overrides {
	xxHash(oid) % smp::count;
      }

      private:
	static ObjectRef get_objectref_local(const sstring& oid,
					     Object::cache& tree,
					     CollectionRef&& me) const {}
      public:
	future<ObjectRef> get_objectref(const sstring& oid) const overrides {
	  auto cpu = get_cpu(oid);
	  if (cpu == engine.cpu_id()) {
	    return make_ready_future<ObjectRef>(
	      get_objectref_local(oid, trees[cpu], CollectionRef(this)));
	  } else {
	    return smp::submit_to(cpu, [c = CollectionRef(this)] mutable {
		trees[engine().cpu_id()] =
		  make_foreign(std::make_unique<NihilObject::cache>());
	      });
	  }
	}
      };

      class Mem : public Store {
      private:
	boost::uuids::uuid id;
      public:
	std::pair<future<CompoundRes> future<>> exec_compound(
	  Sequencer& osr, Compound& t) override;

	MemStore() noexcept : id(boost::uuids::random_generator()()) {}
	virtual ~MemStore() = default;
	MemStore(const Store& o) = delete;
	const MemStore& operator=(const MemStore& o) = delete;
	MemStore(MemStore&& o) = delete;
	const memStore& operator=(MemStore&& o) = delete;

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
      unsigned get_cpu(const sstring& cid) const noexcept override {
	xxHash(cid) % smp::count;
      }

	/**
	 * Set and get internal fsid for this instance. No external data
	 * is modified
	 */
	future<> set_fsid(boost::uuids::uuid u) override {
	  id = u;
	  return make_ready_future<>();
	}
	future<boost::uuids::uuid> get_fsid() override {
	  return make_ready_future<boost::uuids::uuid>(id);
	}
      };
    } // namespace _
    using _::Mem;
  } // namespace store
} // namespace crimson

#endif // CRIMSON_STORE_NIHIL_H
