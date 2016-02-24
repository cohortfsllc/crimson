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

/// \file mem/Object.h
/// \brief Fast, in-memory objects
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#ifndef CRIMSON_STORE_MEM_OBJECT_H
#define CRIMSON_STORE_MEM_OBJECT_H

#include <array>
#include <utility>

#include "store/Object.h"

#include "common/xxHash.h"
#include "PageSet.h"


namespace crimson {
  /// Storage interface
  namespace store {
    /// Memory based object store
    namespace mem {
      class Object;
      using MemObjRef = boost::intrusive_ptr<Object>;
      namespace _ {
	using boost::intrusive::slist_base_hook;
	using boost::intrusive::link_mode;
	using boost::intrusive::normal_link;
	using boost::intrusive::auto_unlink;
	using boost::intrusive::cache_last;
	using boost::intrusive::constant_time_size;
	struct AsyncMutation : public slist_base_hook<link_mode<normal_link>> {
	  MemObjRef object;
	  bool commit;
	  promise<> p;

	  AsyncMutation(Object* o);
	  ~AsyncMutation();
	};
	using mutslist = boost::intrusive::slist<AsyncMutation,
						 cache_last<true>>;
	struct AttrCursor : public store::AttrCursor,
			    public slist_base_hook<link_mode<auto_unlink>> {
	  std::size_t ref_cnt = 0;
	  std::map<string, lw_shared_ptr<string>>::const_iterator i;
	  bool valid = true;
	  AttrCursor(std::map<string, lw_shared_ptr<string>>
		     ::const_iterator _i)
	    : i(_i) {}
	  ~AttrCursor() {}
	  void get() override {
	    ++ref_cnt;
	  }
	  void put() override {
	    if (--ref_cnt == 0)
	      delete this;
	  }
	};
	// I would really rather use a set, but std::map iterators can
	// only be checked for equality, not compared.
	using curslist = boost::intrusive::slist<AttrCursor,
						 cache_last<false>,
						 constant_time_size<false>>;
      } // namespace _
      using _::AttrCursor;

      class Collection;
      class Object : public crimson::store::Object {
	friend Collection;
	friend _::AsyncMutation;
	mutable std::size_t ref_cnt = 0;
	const unsigned cpu;
	_::PageSet data;
	Length data_len;
	using attrmap = std::map<string, lw_shared_ptr<string>>;
	std::array<attrmap, (unsigned)attr_ns::END> attarray;
	lw_shared_ptr<string> omap_header;
	std::map<string, MemObjRef> slice;
	const std::map<string, MemObjRef>::iterator iter;
	_::mutslist mutations;
	mutable _::curslist attrcursors;

	AttrCursorRef cursor_ref(attrmap::const_iterator i) const;

	bool in_range(const Range range) const {
	  return (range.offset + range.length <= data_len);
	}

	bool local() const {
	  return engine().cpu_id() == cpu;
	}

	Object(CollectionRef _coll, string _oid,
	       std::map<string, MemObjRef>& _slice)
	  : crimson::store::Object(std::move(_coll), std::move(_oid)),
	    cpu(xxHash()(oid) % smp::count), slice(_slice),
	    iter((slice.emplace(
		    std::piecewise_construct,
		    std::forward_as_tuple(oid),
		    std::forward_as_tuple(MemObjRef(this)))).first) {}

      public:

	~Object() = default;

	void ref() const override;
	void unref() const override;

	/// CPU owning this object
	unsigned on_cpu() const override {
	  return cpu;
	}
	/// Read data from an object
	future<IovecRef> read(const Range r) const override;
	/// Write data to an offset within an object
	future<> write(IovecRef iov) override;
	/// Zero out the indicated byte range within an object.
	///
	/// \note For the memstore, zero is equivalent to hole_punch,
	/// except that zero will not throw store::errc::out_of_range
	future<> zero(const Range range) override;
	/// Punch a hole in the object of the given dimension
	future<> hole_punch(const Range range) override;
	/// Truncate an object.
	future<> truncate(const Length length) override;
	/// Removes the object from the collection map
	future<> remove() override;
	/// Get a single attribute value
	future<const_buffer> getattr(attr_ns ns, string attr) const override;
	/// Get some attribute values
	future<held_span<const_buffer>> getattrs(
	  attr_ns ns, held_span<string> attrs) const override;
	/// Set a single attribute
	future<> setattr(attr_ns ns, string attr,
			 const_buffer val) override;
	/// Sets attributes
	future<> setattrs(attr_ns ns,
			  held_span<pair<string,
			  const_buffer>> attrpairs) override;
	/// Remove an attribute
	future<> rmattr(attr_ns ns, string attr) override;
	/// Remove several attributes
	future<> rmattrs(attr_ns ns, held_span<string> attr) override;
	/// Remove attributes in an overcomplicated way
	///
	/// Not supported for memstore right now
	future<> rmattr_range(attr_ns ns,
			      AttrCursorRef lb,
			      AttrCursorRef ub) override {
	  return make_exception_future<>(
	    std::system_error(errc::operation_not_supported));
	}
	/// Enumerate attributes (just the names)
	future<held_span<string>, optional<AttrCursorRef>>
	  enumerate_attr_keys(attr_ns ns, optional<AttrCursorRef> cursor,
			      size_t to_return) const override;
	/// Enumerate attributes (key/value)
	future<held_span<pair<string, const_buffer>>,
	       optional<AttrCursorRef>> enumerate_attr_kvs(
		 attr_ns ns, optional<AttrCursorRef> cursor,
		 size_t to_return) const override;

	/// Get cursor for attribute key
	///
	/// Not supported on this store.
	virtual future<AttrCursorRef> attr_cursor(attr_ns ns,
						  string attr) const {
	  return make_exception_future<AttrCursorRef>(
	    std::system_error(errc::operation_not_supported));
	}

	/// Get the object "header"
	future<const_buffer> get_header() const override;
	/// Set the object "header"
	future<> set_header(const_buffer header) override;

	/// Clone this object into another object
	future<> clone(store::Object& dest_obj) const override {
	  return make_exception_future<>(
	    std::system_error(errc::operation_not_supported));
	}

	/// Clone a byte range from one object to another
	///
	/// This only affects the data portion of the destination object.
	///
	/// \see clone
	future<> clone_range(const Range src_range,
			     store::Object& dest,
			     const Offset dest_offset) const override {
	  return make_exception_future<>(
	    std::system_error(errc::operation_not_supported));
	 }

	/// Inform store of future allocation plans
	///
	/// A no-op for mem::Store
	future<>set_alloc_hint(Length obj_size,
			       Length write_size) {
	  return make_ready_future<>();
	}

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
	future<held_span<Range>> get_extents(
	  const Range range) const override {
	  return make_exception_future<held_span<Range>>(
	    std::system_error(errc::operation_not_supported));
	}
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
	future<>move_to_collection(store::Collection& dest_coll,
				   string dest_oid) override {
	  return make_exception_future<>(
	    std::system_error(errc::operation_not_supported));
	}
	/// Commit all outstanding modifications on this object
	future<> commit() override;
      };
    } // namespace mem
  } // namespace store
} // namespace crimson

#endif // CRIMSON_STORE_MEM_OBJECT_H
