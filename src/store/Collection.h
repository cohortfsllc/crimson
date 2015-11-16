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

    class Collection {
    private:
      Store& store;
      const sstring cid;

    public:
      virtual void ref()  = 0;
      virtual void unref()  = 0;
      friend inline void intrusive_ptr_add_ref(Collection* c) {
	c->ref();
      }
      friend inline void intrusive_ptr_release(Collection* c) {
	c->unref();
      }

      explicit Collection(uint32_t _slab_page_index, Store& _store,
			  sstring _cid)
	: slab_page_index(_slab_page_index), store(_store),
	  cid(std::move(_cid)) {}

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
