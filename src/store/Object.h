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

/// \file Object.h
/// \brief Base class of Store objects
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#ifndef CRIMSON_STORE_OBJECT_H
#define CRIMSON_STORE_OBJECT_H

#include <boost/intrusive_ptr.hpp>

#include <core/sharded.hh>
#include <core/slab.hh>

#include "Store.h"
#include "Collection.h"

namespace crimson {

  /// Storage interface
  namespace store {
    /// A handle for object storage operations
    ///
    /// Used to represent existing objects in the Store.
    class Object : public slab_item_base {
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
      friend inline void intrusive_ptr_add_ref(Object* o) {
	++o->ref_count;
	if (o->ref_count == 2) {
	  coll->get_store().manager.object_slab->lock_item(o);
	}
	assert(o->ref_count > 0);
      }
      friend inline void intrusive_ptr_release(Object* o) {
	assert(o->ref_count > 0);
	--o->ref_count;
	if (o->ref_count == 1) {
	  coll->store.manager.object_slab->unlock_item(o);
	} else if (o->ref_count == 0) {
	  coll->get_store().manager.object_slab->free(o);
	}
      }
      ///@}

    protected:
      CollectionRef coll;
      const sstring oid;

    public:
      explicit Object(uint32_t _slab_page_index, CollectionRef _coll,
		      sstring _oid)
	: slab_page_index(_slab_page_index), coll(std::move(_c)),
	  oid(std::move(_oid)) {}
      virtual ~Object() = default;

      const sstring& get_oid() const {
	return oid;
      }

      operator sstring() const {
	return oid;
      }

      const CollectionRef& get_collection() const {
	return coll;
      }

      friend class Collection;
    }; /* Object */

    using ObjectRef = foreign_ptr<boost::intrusive_ptr<Object>>;
  } // namespace store
} // namespace crimson

#endif // !CRIMSON_STORE_OBJECT_H
