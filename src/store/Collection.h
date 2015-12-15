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

#include <boost/intrusive_ptr.hpp>
#include <core/sharded.hh>

#include "cxx_function/cxx_function.hpp"

namespace crimson {

  /// Storage interface
  namespace store {
    class Store;
    class Object;
    using ObjectRef = foreign_ptr<boost::intrusive_ptr<Object>>;


    class OidCursor;
    using OidCursorRef = foreign_ptr<boost::intrusive_ptr<OidCursor>>;
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

      explicit Collection(Store& _store, sstring _cid)
	: store(_store), cid(std::move(_cid)) {}

      virtual ~Collection() = default;

      Collection(const Collection&) = delete;
      Collection& operator =(const Collection&) = delete;
      Collection(Collection&&) = delete;
      Collection& operator =(Collection&&) = delete;

      virtual unsigned get_cpu(const sstring& oid) const = 0;

      const sstring& get_cid() const {
	return cid;
      }

      /// Ensure the existance of an object in a collection
      ///
      /// Create an empty object if necessary
      ///
      /// @param[in] oid  Name of the object that should exist
      /// @param[in] excl True if the object must not already exist
      /// @return A reference to the object
      virtual future<ObjectRef> create(sstring oid, bool excl = false) = 0;
      /// Remove this collection
      ///
      /// The collection must be empty
      virtual future<>remove() = 0;
      /// Split this collection
      ///
      /// Move objects matching a predicate into another
      /// collection.
      ///
      /// \warning Similar concerns apply as to move_coll_rename.
      ///
      /// \note We use a unique_function for the predicate rather than
      /// a template, since this is virtual.
      ///
      /// \param[in] dest Destination
      ///
      /// \see move_coll_rename
      virtual future<> split_collection(
	Collection& dest,
	cxx_function::unique_function<bool(const sstring& oid)> pred) = 0;
      /// Enumerate objects in a collection
      ///
      ///
      /// \param[in] cursor    If present, a cursor returned by a
      ///                      previous enumerate_objects call.
      /// \param[in] to_return Maximum number of OIDs to return. The
      ///                      store may return fewer (even if more
      ///                      remain) but not more.
      ///
      /// \see obj_cursor
      virtual future<std::vector<sstring>, OidCursorRef> enumerate_objects(
	boost::optional<OidCursorRef> cursor,
	size_t to_return) const = 0;
      /// Get cursor for a given object
      ///
      /// This function gives a cursor that will continue an
      /// enumeration as if a previous enumeration had ended just
      /// before returning `oid`.
      ///
      /// \param[in] oid OID to (exclusively) lower-bound enumeration
      ///
      /// \note Not supported on stores without a well-defined
      /// enumeration order for oids.
      virtual future<OidCursorRef> obj_cursor(sstring oid) const = 0;
    };
  } // namespace store
} // namespace crimson

#endif // !CRIMSON_STORE_COLLECTION_H
