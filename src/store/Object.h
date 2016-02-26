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

#include "common/held_span.h"

#include "Iovec.h"
#include "Store.h"
#include "Collection.h"

namespace crimson {
  /// Storage interface
  namespace store {
    /// Attribute namespace specifier
    enum class attr_ns { xattr, omap, END };

    class AttrCursor {
      friend void intrusive_ptr_add_ref(AttrCursor* a);
      friend void intrusive_ptr_release(AttrCursor* a);
      virtual void get() = 0;
      virtual void put() = 0;

    public:
      virtual ~AttrCursor() {} ;
      friend void intrusive_ptr_add_ref(AttrCursor* a) {
	a->get();
      }
      friend void intrusive_ptr_release(AttrCursor* a) {
	a->put();
      }
    };
    using AttrCursorRef = foreign_ptr<boost::intrusive_ptr<AttrCursor>>;

    /// A handle for object storage operations
    ///
    /// Used to represent existing objects in the Store.
    class Object {
    protected:
      CollectionRef coll;
      const string oid;

      virtual void ref() const = 0;
      virtual void unref() const = 0;
      friend inline void intrusive_ptr_add_ref(const Object* o) {
	o->ref();
      }
      friend inline void intrusive_ptr_release(const Object* o) {
	o->unref();
      }
    public:
      explicit Object(CollectionRef _coll,
		      string _oid)
	: coll(std::move(_coll)), oid(std::move(_oid)) {}
      virtual ~Object() {}

      const string& get_oid() const {
	return oid;
      }

      operator string() const {
	return oid;
      }

      const CollectionRef& get_collection() const {
	return coll;
      }

      friend class Collection;

      /// CPU owning this object
      ///
      /// Return the CPU no which all methods for this object will be executed.
      virtual unsigned on_cpu() const = 0;
      /// Read data from offset within an object
      ///
      /// \param[in] range Range to read
      ///
      /// \return Data read from object
      ///
      /// \throws store::errc::out_of_range if attempt is made to read after
      ///         the end of an object.
      virtual future<IovecRef> read(const Range r) const = 0;
      /// Write data to an offset within an object
      ///
      /// If the object is too small, it is expanded as needed.  It is
      /// possible to specify an offset beyond the current end of an
      /// object and it will be expanded as needed. Simple
      /// implementations of Store will just zero the data between the
      /// old end of the object and the newly provided data. More
      /// sophisticated implementations of Store will omit the
      /// untouched data and store it as a "hole" in the file.
      virtual future<> write(IovecRef data) = 0;
      /// Zero out the indicated byte range within an object.
      ///
      /// Some Store instances may optimize this to release the
      /// underlying storage space.
      ///
      /// @param[in] range Range to zero
      ///
      /// \see hole_punch
      virtual future<> zero(const Range range) = 0;
      /// Punch a hole in the object of the given dimension
      ///
      /// If the store cannot punch holes or cannot punch holes of the
      /// specified dimension, this operation throws an exception.
      ///
      /// @param[in] range Range in which to punch the hole
      ///
      /// \see zero, get_extents
      ///
      /// \throws store::errc::out_of_range if attempt is made to punch hole
      ///         after the end of an object.
      virtual future<> hole_punch(const Range range) = 0;
      /// Truncate an object.
      ///
      /// \note This will only make objects shorter, you cannot
      /// `truncate` to make a sparse, large object.
      ///
      /// \param[in] length To which to truncate the object
      ///
      /// \see hole_punch
      virtual future<> truncate(const Length length) = 0;
      /// Remove an object
      ///
      /// All portions of the object are removed. This should almost
      /// certainly not be done 'by name' since we have to acquire the
      /// object in some sense anyway in case temporary buffers are
      /// outstanding with data in the process of being sent.
      virtual future<> remove() = 0;
      /// Get a single attribute value
      ///
      /// \param[in] ns   Attribute namespace
      /// \param[in] attr Attribute key
      virtual future<const_buffer> getattr(
	attr_ns ns, string attr) const = 0;
      /// Get some attribute values
      ///
      /// \param[in] ns    Attribute namespace
      /// \param[in] attrs Attribute keys
      ///
      /// \return A held_span of attribute values, in the same order
      /// as the keys supplied.
      virtual future<held_span<const_buffer>> getattrs(
	attr_ns ns, held_span<string> attrs) const = 0;

      /// Set a single attribute
      ///
      /// \param[in] ns   Attribute namespace
      /// \param[in] attr Attribute key
      /// \param[in] val  Attribute value
      virtual future<> setattr(attr_ns ns, string attr,
			       const_buffer val) = 0;
      /// Sets attributes
      ///
      /// \param[in] ns       Attribute namespace
      /// \param[in] attrvals Attribute key/value pairs
      virtual future<> setattrs(
	attr_ns ns, held_span<pair<string, const_buffer>> attrpairs) = 0;
      /// Remove an attribute
      ///
      /// \param[in] ns   Attribute namespace
      /// \param[in] attr Attribute key
      virtual future<> rmattr(attr_ns ns, string attr) = 0;
      /// Remove several attributes
      ///
      /// \param[in] ns    Attribute namespace
      /// \param[in] attrs Attribute keys
      virtual future<> rmattrs(attr_ns ns,
			       held_span<string> attr) = 0;
      /// Remove attributes in an overcomplicated way
      ///
      /// When given two valid cursors, remove attributes that would
      /// have been enumerated starting with `lb_cursor` and stopping
      /// before attributes that would have been enumerated starting
      /// with `ub_cursor`.
      ///
      /// \param[in] ns Attribute namespace
      /// \param[in] lb Lower bound of attributes to remove
      /// \param[in] ub Upper bound of attributes to remove (exclusive)
      ///
      /// \note Not supported by stores without a well-defined
      /// enumeration order for attributes.
      virtual future<> rmattr_range(attr_ns ns,
				    AttrCursorRef lb,
				    AttrCursorRef ub) = 0;
      /// Enumerate attributes (just the names)
      virtual future<held_span<string>, optional<AttrCursorRef>>
	enumerate_attr_keys(attr_ns ns,
			    optional<AttrCursorRef> cursor,
			    size_t to_return) const = 0;
      /// Enumerate attributes (key/value)
      virtual future<held_span<pair<string, const_buffer>>,
		     optional<AttrCursorRef>> enumerate_attr_kvs(
		       attr_ns ns,
		       optional<AttrCursorRef> cursor,
		       size_t to_return) const = 0;

      /// Get cursor for attribute key
      ///
      /// This function gives a cursor that will continue an
      /// enumeration as if a previous enumeration had ended
      /// just before returning `attr`.
      ///
      /// \note Not supported on stores without a well-defined
      /// enumeration order for attributes.
      virtual future<AttrCursorRef> attr_cursor(attr_ns ns, string attr) const;

      /// Get the object "header"
      ///
      /// Ceph object stores have an additional piece of data, an
      /// 'OMAP Header' that is read or written in its entirety in a
      /// single operation.
      ///
      /// \see set_header
      virtual future<const_buffer> get_header() const = 0;
      /// Set the object "header"
      ///
      /// param[in] header Header to set
      /// \see get_header
      virtual future<> set_header(const_buffer header) = 0;

      /// Clone this object into another object
      ///
      /// Low-cost (e.g., O(1)) cloning (if supported) is best, but
      /// fallback to an O(n) copy is allowed.  All object data are
      /// cloned.
      ///
      /// This clones everything, attributes, omap header, etc.
      ///
      /// \param[in] dest Destination object
      ///
      /// \note Objects must be in the same collection.
      ///
      /// \see clone_range
      virtual future<> clone(Object& dest_obj) const = 0;
      /// Clone a byte range from one object to another
      ///
      /// This only affects the data portion of the destination object.
      ///
      /// \see clone
      virtual future<> clone_range(const Range src_range,
				   Object& dest,
				   const Offset dest_offset) const = 0;

      /// Inform store of future allocation plans
      ///
      /// @param[in] obj_size   Expected total size of object
      /// @param[in] write_size Expected size of write operations
      virtual future<>set_alloc_hint(const Length obj_size,
				     const Length write_size) = 0;

      /// Get allocated extents within a range
      ///
      /// Return a list of extents that contain actual data within a
      /// range.
      ///
      /// \note Is there a better interface for this?
      ///
      /// \param[in] range Range of object to query
      ///
      /// \see hole_punch
      virtual future<held_span<Range>> get_extents(
	const Range range) const = 0;
      /// Move object from one collection to another
      ///
      /// \note This is used by Ceph's recovery logic, to move objects
      /// out of Temporary Collections.
      ///
      /// \warning The same concerns apply as for
      /// split_collection.
      ///
      /// \warning It's very likely that this call invalidates
      /// outstanding handles to the object it moves. In some
      /// implementations, this call might function as a barrier,
      /// preventing the acquisition of new handles on the object in
      /// question and waiting for outstanding handles to be
      /// released. If this is the case, the Store should make sure
      /// not to deadlock itself.
      ///
      /// \param[in] dest_coll Collection to which to move the object
      /// \param[in] dest_oid  OID it should have there
      ///
      /// \see split_collection
      virtual future<>move_to_collection(Collection& dest_coll,
					 string dest_oid) = 0;
      /// Commit all outstanding modifications on this object
      ///
      /// This function acts as a barrier. It will complete when all
      /// outstanding operations are complete and written to stable storage.
      virtual future<> commit() = 0;
    }; /* Object */
  } // namespace store
} // namespace crimson

#endif // !CRIMSON_STORE_OBJECT_H
