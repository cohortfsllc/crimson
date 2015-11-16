// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

// Crimson: a prototype high performance OSD

// Copyright (C) 2015 Adam C. Emerson <aemerson@redhat.com
//
// Based on the Ceph object store which is Copyright (C) 2004-2006
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

/// \file Nihil.h
/// \brief Object store that stores nothing.
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#ifndef CRIMSON_STORE_NIHIL_H
#define CRIMSON_STORE_NIHIL_H

#include <utility>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/random_generator.hpp>

#include "common/xxHash.h"

#include "Store.h"

namespace crimson {

  /// Storage interface
  namespace store {
    class NihilObject : public Object, public slab_page_index {
    private:
      NihilCollection& ncoll() {
	return static_cast<NihilCollection&>(*coll);
      }
      /// \name lifecycle
      ///
      /// Members related to lifecycle and interaction with the slab allocator.
      ///@{
    private:
      uint32_t slab_page_index;
      uint32_t ref_count;
    public:
      uint32_t get_slab_page_index() const {
	return slab_page_index;
      }
      bool is_unlocked() const {
	return ref_count == 1;
      }
    private:
      void ref() override {
	++ref_count;
	if (ref_count == 2) {
	  ncoll().object_slab().lock_item(this);
	}
	Ensures(ref_count > 0);
      }
      void unref() override {
	Requires(ref_count > 0);
	--ref_count;
	if (ref_count == 1) {
	  ncoll().object_slab().unlock_item(this);
	} else if (o->ref_count == 0) {
	  ncoll().object_slab().free(this);
	}
      }
      ///@}
      /// \name cache
      ///
      /// Members related to cache lookup and storage
      ///@{
      // For debugging
      using cache_link_mode = boost::intrusive::link_mode<
      boost::intrusive::safe_link>;
    public:
      boost::intrusive::set_member_hook<cache_link_mode> cache_hook;
    private:
      using cache_option =
	boost::intrusive::member_hook<Object, decltype(cache_hook),
				      &Object::cache_hook>;
      struct Compare {
	bool operator()(const Object& l, const Object& r) const {
	  return std::less<sstring>()(l.oid, r.oid);
	}
	bool operator()(const Object& l, const sstring& r) const {
	  return std::less<sstring>()(l.oid, r);
	}
	bool operator()(const sstring& l, const Object& r) const {
	  return std::less<sstring>()(l, r.oid);
	}
      };
      friend Compare;
    public:
      using cache = boost::intrusive::set<
	Object, cache_option,
	boost::intrusive::constant_time_size<false>,
	boost::intrusive::compare<Compare>>;
      ///@}
    public:
      NihilObject(uint32_t _slab_page_index, CollectionRef _coll,
		  sstring _oid)
	: Object(_slab_page_index, std::move(_coll), std::move(_oid)) {
	trees.resize(smp::count);
      }
      virtual ~NihilObject() = default;
    };

    /// A collection is a grouping of objects.
    ///
    /// Collections have names and can be enumerated in order.  Like an
    /// individual object, a collection also has a set of xattrs.

    class NihilCollection : public Collection, public slab_item_base {
    private:
      NihilStore& nstore() {
	return static_cast<NihilStore&>(store);
      }
      /// \name lifecycle
      ///
      /// Members related to lifecycle and interaction with the slab allocator.
      ///@{
    private:
      uint32_t slab_page_index;
      uint32_t ref_count;
    public:
      uint32_t get_slab_page_index() const {
	return slab_page_index;
      }
      bool is_unlocked() const {
	return ref_count == 1;
      }
      void ref() override {
	++ref_count;
	if (ref_count == 2) {
	  store->collection_slab(engine->cpu_id())->lock_item(this);
	}
	Ensures(ref_count > 0);
      }
      void unref() override {
	Requires(ref_count > 0);
	--ref_count;
	if (ref_count == 1) {
	  store->collection_slab().unlock_item(c);
	} else if (ref_count == 0) {
	  store->collection_slab()->free(c);
	}
      }
      ///@}

      mutable std::vector<foreign_ptr<std::unique_ptr<
	NihilObject::cache>> trees;

      NihilCollection(uint32_t _slab_page_index, Store& _store, sstring _cid)
	: Collection(_slab_page_index, _store,  _cid) {
	trees.resize(smp::count);
      }
      friend class slab_class<Collection>;
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

      virtual ~NihilCollection() = default;

      NihilCollection(const Collection&) = delete;
      NihilCollection& operator =(const Collection&) = delete;
      NihilCollection(Collection&&) = delete;
      NihilCollection& operator =(Collection&&) = delete;

      unsigned get_cpu(const sstring& oid) const overrides {
	xxHash(oid) % smp::count;
      }

    private:
      static ObjectRef get_objectref_local(const sstring& oid,
					   Object::cache& tree,
					   CollectionRef&& me) const {
	Expects(get_cpu(oid) == engine().cpu_id());
	auto it = tree.find(oid);
	if (it == tree.end()) {
	  auto new_item = Object::make(me, oid);
	  tree.insert(new_item);
	  return make_ready_future<ObjectRef>(new_item);
	}
	return make_ready_future<ObjectRef>(*it);
      }
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

    class NihilStore : public Store {
    private:
      std::vector<slab_allocator<NihilCollection>*> collection_slabs;
      std::vector<slab_allocator<NihilObject>*> object_slabs;
      boost::uuids::uuid id;
      slab_allocator<NihilCollection>& collection_slab() const {
	return *collection_slabs[engine().cpu_id()];
      }
      friend class NihilCollection;
      slab_allocator<NihilObject>& object_slab() const {
	return *object_slabs[engine().cpu_id()];
      }
      friend class NihilCollection;
    public:
      std::pair<future<CompoundRes> future<>> exec_compound(
	Sequencer& osr, Compound& t) override;

      Nihil() noexcept : id(boost::uuids::random_generator()()) {}
      virtual ~Nihil() = default;
      Nihil(const Store& o) = delete;
      const Nihil& operator=(const Nihil& o) = delete;
      Nihil(Nihil&& o) = delete;
      const Nihil& operator=(Nihil&& o) = delete;

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
  } // namespace store
} // namespace crimson

#endif // CRIMSON_STORE_NIHIL_H
