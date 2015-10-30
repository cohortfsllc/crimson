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

#ifndef CRIMSON_STORE_COLLECTION_H
#define CRIMSON_STORE_COLLECTION_H

#include <core/sharded.hh>

namespace crimson {

  /// Storage interface
  namespace store {
    /// A collection is a grouping of objects.
    ///
    /// Collections have names and can be enumerated in order.  Like an
    /// individual object, a collection also has a set of xattrs.

    class Collection : public slab_item_base {
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
      friend inline void intrusive_ptr_add_ref(Collection* c) {
	++c->ref_count;
	if (c->ref_count == 2) {
	  collection_slab->lock_item(c);
	}
	assert(c->ref_count > 0);
      }
      friend inline void intrusive_ptr_release(Collection* c) {
	assert(c->ref_count > 0);
	--c->ref_count;
	if (c->ref_count == 1) {
	  collection_slab->unlock_item(c);
	} else if (c->ref_count == 0) {
	  collection_slab->free(c);
	}
      }
      ///@}
    private:
      Store& store;
      const sstring cid;

    public:

      explicit Collection(Store& _store, sstring _cid)
	: store(_store),  cid(std::move(_cid)) {}

      virtual ~Collection() = default;

      Collection(const Collection&) = delete;
      Collection& operator =(const Collection&) = delete;
      Collection(Collection&&) = delete;
      Collection& operator =(Collection&&) = delete;

      virtual unsigned get_cpu(const sstring& oid) const = 0;

      const sstring& get_cid() const {
	return cid;
      }
    };
  } // namespace store
} // namespace crimson

#endif // !CRIMSON_STORE_COLLECTION_H
