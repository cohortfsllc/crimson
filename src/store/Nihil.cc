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

/// \file Nihil.cc
/// \brief Object store that stores nothing. Implementation.
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#include <boost/iterator/counting_iterator.hpp>

namespace crimson {
  namespace store {
    class NihilObject : public Object {
    private:
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

    class NihilCollection : public Collection {
    private:
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
      ObjectRef get_objectref_local(const sstring& oid,
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
	auto cpu = 
      }
    };

  } // namespace store
} // namespace crimson
