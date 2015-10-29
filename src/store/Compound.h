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

/// \file Compound.h
/// \brief Object store compound operations
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#ifndef CRIMSON_STORE_COMPOUND_H
#define CRIMSON_STORE_COMPOUND_H

#include <algorithm>
#include <functional>
#include <vector>

#include <core/sharded.hh>
#include <core/sstring.hh>
#include <core/temporary_buffer.hh>

namespace crimson {

/// Storage interface
namespace store {

class Collection;
using CollectionRef = foreign_ptr<boost::intrusive_ptr<Collection>>;

class Object;
using ObjectRef = foreign_ptr<boost::intrusive_ptr<Object>>;

class AttrCursor;
using AttrCursorRef = foreign_ptr<boost::intrusive_ptr<AttrCursor>>;

class OidCursor;
using OidCursorRef = foreign_ptr<boost::intrusive_ptr<OidCursor>>;

/// A sequence of operations to be executed by a Store
///
/// We encapsulate both read and write operations in a single
/// compound. This is not an NFSv4 compound, but it is conceptually
/// similar. Execution of the compound results in a future, more
/// details of which will be forthcoming.
///
/// Registers
/// ---------
///
/// Objects, collections, cursors, and opaques (opaques are OIDs,
/// Collection IDs, and attribute keys as well as anything that can be
/// read or written from object data or an attribute value) are stored
/// in registers and referenced by index in the operation. This leads
/// to two advantages:
///
///   1. If we refer to the same thing several times, the Compound
///      need only own one instance of it.
///   2. A result of a previous operation can serve as the input to a
///      future operation.
///
/// In order to make the system more tractable, there are a few Rules
/// that must be enforced.
///
///   - Registers are write-once. Once we specify something as an
///     input, after that point, it can't be an output. The Store must
///     update a register that becomes invalid as a result of
///     executing a compound (\see move_coll_rename) but that is the
///     Store acting on its own, not the compound changing its mind
///     about what object object register 1 refers to.
///   - Registers are filled from zero onward, with no gaps. If the
///     compound references three objects when it starts, those
///     objects must have the indices 0, 1, and 2. If it gains a new
///     object reference while executing, that will have index 3.
///   - No register may be used before it is defined. If a Compound
///     is executed with object registers 0, 1, and 2 filled, no
///     Operation may input from register 3 unless a previous
///     operation specifies it as an output register.
///
/// As such, consumers of the Store system will not manipulate
/// registers directly, a register scheduler will help construct
/// compounds for them.
///
/// Ownership and Execution
/// -----------------------
///
/// The Compound is owned by whatever thread constructs it. When it
/// has finished execution, that thread decides what to do about the
/// results and executes it.
///
/// Execution is in sequence. Each operation runs after the previous
/// one has completed. While only one thread owns the Compound (and
/// thus may deallocate it) other threads may write to it. Only one
/// thread may write to it at a time, the one owning the object
/// referenced by the currently executing operation.
///
/// In the case of a single operation spanning multiple objects or
/// collections, only one thread may mutate the Compound while that
/// operation executes. It is up to the store to decide which thread
/// that is.
///
/// \todo This may not be the best design. It may be better to have a
/// single thread traverse the object and evaluate futures on other
/// threads and store the results in registers, depending. We could
/// optimize for the common case by making sure that the Compound is
/// owned by the first object referenced, which will often be the only
/// object referenced.
///
/// Error Handling
/// --------------
///
/// In the event of an error, the future representing the execution of
/// the compound will be come exceptional and execution shall
/// cease. No guarantees are made about the progress of the individual
/// operation (a write may be only partially applied).
///
/// \todo is this sufficient?
///
/// Consistency
/// -----------
///
/// Compounds are not isolated. Compounds are not atomic. Using a
/// Sequencer with the store to force compounds (on an object or
/// collection granularity, say) into a sequence is possible.
///
/// The use of zero-copy does require that stores not deallocate
/// memory while outstanding buffers exist referencing it. They may
/// defer operations until said buffers are freed, or implement RCU,
/// or do anything else they wish that fits this requrement.
class Compound {
  /// Registers holding object handles
  std::vector<ObjectRef> obj_registers;
  using obj_inreg = decltype(obj_registers)::size_type;
  using obj_outreg = boost::optional<decltype(obj_registers)::size_type>;

  /// Registers holding collection references
  std::vector<CollectionRef> coll_registers;
  using coll_inreg = decltype(coll_registers)::size_type;
  using coll_outreg = boost::optional<decltype(coll_registers)::size_type>;

  /// Registers holding attribute cursors
  std::vector<AttrCursorRef> attcur_registers;
  using attcur_inreg = decltype(attcur_registers)::size_type;
  using attcur_outreg = boost::optional<decltype(attcur_registers)::size_type>;

  /// Registers holding oid cursors
  std::vector<OidCursorRef> oidcur_registers;
  using oidcur_inreg = decltype(oidcur_registers)::size_type;
  using oidcur_outreg = boost::optional<decltype(oidcur_registers)::size_type>;

  struct opaque {
    union {
      sstring s;
      temporary_buffer<char> t;
    };
    bool is_temporary_buffer;
  };

  /// Registers holding opaques
  std::vector<opaque> opaque_registers;
  using opaque_inreg = decltype(opaque_registers)::size_type;
  using opaque_outreg = boost::optional<decltype(opaque_registers)::size_type>;

  /// A single operation
  struct Op {
    /// Operation codes
    enum class code {
      // Object operations
      nop, touch, read, write, zero, hole_punch, truncate, remove,
      getattr, getattrs, setattr, setattrs, rmattr, rmattrs, rmattr_range,
      enumerate_attr_keys, enumerate_attr_keyvals, attr_cursor,
      clone, clone_range, set_alloc_hint, get_header, set_header,
      get_extents,

      // Collection operations
      make_coll, remove_coll, split_coll, move_coll_rename,
      enumerate_objects, object_cursor,

      // Store-wide operations
      enumerate_collections, sync
    };
    /// The opcode
    code op;
    /// Namespace of extended attributes
    ///
    /// The Ceph object store has two sets of operations on key-value
    /// pairs, one for xattrs and one for omaps. While they have
    /// different /expectations/ which may apply in Store
    /// implementations (xattrs are expected to be few and short)
    /// there's no reason not to unify the operations managing them,
    /// so long as you can have an xattr and an omap share the same
    /// key.
    enum class attr_ns {
      xattr, omap
    };
    /// Check that an opcode doesn't modify anything
    static constexpr bool read_only_opcode(const code c) {
      switch (c) {
      case code::nop:
      case code::read:
      case code::getattr:
      case code::getattrs:
      case code::enumerate_attr_keys:
      case code::enumerate_attr_keyvals:
      case code::attr_cursor:
      case code::get_header:
      case code::get_extents:
      case code::enumerate_objects:
      case code::object_cursor:
      case code::enumerate_collections:
	return true;

      case code::touch:
      case code::write:
      case code::zero:
      case code::hole_punch:
      case code::truncate:
      case code::remove:
      case code::setattr:
      case code::setattrs:
      case code::rmattr:
      case code::rmattrs:
      case code::rmattr_range:
      case code::clone:
      case code::clone_range:
      case code::set_alloc_hint:
      case code::set_header:
      case code::make_coll:
      case code::remove_coll:
      case code::split_coll:
      case code::move_coll_rename:
      case code::sync:
	break;
      }
      return false;
    };
    /// This operation doesn't modify anything
    bool is_read_only() const {
      return read_only_opcode(op);
    }
    union u {
      /// noop. 'nuf said
      struct {} nop;
      /// Ensure the existance of an object in a collection
      ///
      /// Create an empty object if necessary
      struct {
	coll_inreg coll; ///< Index of collection holding/to hold object
	opaque_inreg oid; ///< OID of object to touch
	obj_outreg obj; ///< Index in which (optionally) to store object handle
      } touch;
      /// Read data from offset within an object
      ///
      /// \todo Ceph ObjectStore specifies that reads past the end of
      /// an object return 0 rather than error. Do we want to retain
      /// that functionality?
      struct {
	obj_inreg obj; ///< Object to write
	uint64_t offset; ///< Offset from which to read
	uint64_t length; ///< Length to read
	opaque_outreg data; ///< Optionally, store the read value in a register
      } read;
      /// Write data to an offset within an object
      ///
      /// If the object is too small, it is expanded as needed.  It is
      /// possible to specify an offset beyond the current end of an
      /// object and it will be expanded as needed. Simple
      /// implementations of Store will just zero the data between the
      /// old end of the object and the newly provided data. More
      /// sophisticated implementations of Store will omit the
      /// untouched data and store it as a "hole" in the file.
      struct {
	obj_inreg obj; ///< Object to write
	uint64_t offset; ///< Offset at which to write
	opaque_inreg buff; ///< Data to write
      } write;
      /// Zero out the indicated byte range within an object.
      ///
      /// Some Store instances may optimize this to release the
      /// underlying storage space.
      ///
      /// \see hole_punch
      struct {
	obj_inreg obj; ///< Object to zero
	uint64_t offset; ///< Start of region
	uint64_t length; ///< Length of region
      } zero;
      /// Punch a hole in the object of the given dimension
      ///
      /// If the store cannot punch holes or cannot punch holes of the
      /// specified dimension, this opreation fails.
      ///
      /// \see zero, get_extents
      struct {
	obj_inreg obj; ///< Object in which to punch a hole
	uint64_t offset; ///< Start of region
	uint64_t length; ///< Length of region
      } hole_punch;
      /// Truncate an object.
      ///
      /// \note This will only make objects shorter, you cannot
      /// `truncate` to make a sparse, large object.
      ///
      /// \see hole_punch
      struct {
	obj_inreg obj; ///< Object to truncate
	uint64_t length; ///< Length to which to truncate
      } truncate;
      /// Remove an object
      ///
      /// All portions of the object are removed. This should almost
      /// certainly not be done 'by name' since we have to acquire the
      /// object in some sense anyway in case temporary buffers are
      /// outstanding with data in the process of being sent.
      struct {
	obj_inreg obj; ///< Object to remove
      } remove;
      /// Get a single attribute value
      ///
      /// In the case where we want only one, don't pay for the whole
      /// vector overhead business.
      struct {
	obj_inreg obj; ///< Object to query
	attr_ns ns; ///< Attribute namespace
	opaque_inreg attr; ///< Attribute
	opaque_outreg val; ///< Optionally store the value in a register
      } getattr;
      /// Get some attribute values
      struct {
	obj_inreg obj; ///< Object to query
	attr_ns ns; ///< Attribute namespace
	std::vector<
	  std::pair<opaque_inreg,
		    opaque_outreg>> attrs; ///< Attributes to get, optionally
					   ///< registers in which to
					   ///< store the values
      } getattrs;
      /// Set a single attribute
      struct {
	obj_inreg obj; ///< Object to query
	attr_ns ns; ///< Attribute namespace
	opaque_inreg attr; ///< Attribute to set
	opaque_inreg val; ///< Value to assign
      } setattr;
      /// Set multiple attributes
      struct {
	obj_inreg obj; ///< Object to query
	attr_ns ns; ///< Attribute namespace
	std::vector<
	  std::pair<opaque_inreg,
		    opaque_inreg> > attrvals; ///< Attribute/value
					      ///< pairs to set
      } setattrs;
      /// Remove an attribute
      struct {
	obj_inreg obj; ///< Object to query
	attr_ns ns; ///< Attribute namespace
	opaque_inreg attr; ///< Attribute to remove
      } rmattr;
      /// Remove attributes
      struct {
	obj_inreg obj; ///< Object to query
	attr_ns ns; ///< Attribute namespace
	std::vector<opaque_inreg> attr; ///< Attributes to remove
      } rmattrs;
      /// Remove attributes in an overcomplicated way
      ///
      /// When given two valid cursors, remove attributes that would
      /// have been enumerated starting with `lb_cursor` and stopping
      /// before attributes that would have been enumerated starting
      /// with `ub_cursor`.
      ///
      /// \note Not supported by stores without a well-defined
      /// enumeration order for attributes.
      struct {
	obj_inreg obj; ///< Object to query
	attr_ns ns; ///< Attribute namespace
	attcur_inreg lb_cursor; ///< Lower bound to remove
	attcur_inreg ub_cursor; ///< Upper bound to remove (exclusive)
      } rmattr_range;
      /// Enumerate attributes (just the names)
      struct {
	obj_inreg obj; ///< Object to query
	attr_ns ns; ///< Attribute namespace
	boost::optional<attcur_inreg> cursor; ///< Cursor to continue
					      ///< enumeration (none
					      ///< if beginning)
	size_t to_return; ///< Maximum number of items to return. The
			  ///< Store may return fewer but must not
			  ///< return more.
	attcur_outreg next_cursor; ///< Index to store cursor to continue
				   ///< this enumeration.
      } enumerate_attr_keys;
      /// Enumerate attributes (the names and values)
      struct {
	obj_inreg obj; ///< Object to query
	attr_ns ns; ///< Attribute namespace
	boost::optional<attcur_inreg> cursor; ///< Cursor to continue
					      ///< enumeration (none
					      ///< if beginning)
	size_t to_return; ///< Maximum number of items to return. The
			  ///< Store may return fewer but must not
			  ///< return more.
	attcur_outreg next_cursor; ///< Index to store cursor to continue
				   ///< this enumeration.
      } enumerate_attr_keyvals;
      /// Get cursor for attribute key
      ///
      /// This function gives a cursor that will continue an
      /// enumeration as if a previous enumeration had ended
      /// just before returning `attr`.
      ///
      /// \note Not supported on stores without a well-defined
      /// enumeration order for attributes.
      struct {
	obj_inreg obj; ///< Object to query
	attr_ns ns; ///< Attribute namespace
	attcur_inreg attr; ///< Attribute to bound cursor
	attcur_outreg cursor; ///< Cursor lower bounded by `attr`
      } attr_cursor;
      /// Clone an object into another object
      ///
      /// Low-cost (e.g., O(1)) cloning (if supported) is best, but
      /// fallback to an O(n) copy is allowed.  All object data are
      /// cloned.
      ///
      /// Previous object contents are discarded.
      ///
      /// \note Objects must be in the same collection.
      ///
      //// \see clone_range
      struct {
	obj_inreg src_obj; ///< Source object
	obj_inreg dest_obj; ///< Destination object
      } clone;
      /// Clone a byte range from one object to another
      ///
      /// This only affects the data portion of the destination object.
      ///
      /// \see clone
      struct {
	obj_inreg src_obj; ///< Source object
	uint64_t src_off; ///< Source offset
	uint64_t src_len; ///< Source length
	obj_inreg dest_obj; ///< Destination object
	uint64_t dest_off; ///< Destination offset
      } clone_range;
      /// Inform store of future allocation plans
      struct {
	obj_inreg obj; ///< The object to hint
	uint64_t obj_size; ///< Expected object size
	uint64_t write_size; ///< Expected size of future writes
      } set_alloc_hint;
      /// Get the object "header"
      ///
      /// Ceph object stores have an additional piece of data, an
      /// 'OMAP Header' that is read or written in its entirety in a
      /// single operation.
      ///
      /// \see set_header
      struct {
	obj_inreg obj; ///< The object to query
	opaque_outreg header; ///< Optionally, store the header in a register
      } get_header;
      /// Set the object "header"
      ///
      /// \see get_header
      struct {
	obj_inreg obj; ///< The object to modify
	opaque_inreg header; ///< Header to set
      } set_header;
      /// Get allocated extents within a range
      ///
      /// Return a list of extents that contain actual data within a
      /// range.
      ///
      /// \note Is there a better interface for this?
      ///
      /// \see hole_punch
      struct {
	obj_inreg obj;
	uint64_t off;
	uint64_t len;
      } get_extents;
      /// Make a collection
      ///
      /// Create a new collection. The collection must not exist prior
      /// to the call.
      struct {
	opaque_inreg cid; ///< Collection name
	coll_outreg coll; ///< Optionally, store the newly created handle
			  ///< in a register
      } make_collection;
      /// Remove a collection
      ///
      /// The collection must be empty
      struct {
	coll_inreg coll; ///< The collection to remove
      } remove_collection;
      /// Split a collection
      ///
      /// Move objects matching a predicate into another
      /// collection.
      ///
      /// \warning Similar concerns apply as to move_coll_rename.
      ///
      /// \see move_coll_rename
      struct {
	coll_inreg src; ///< Source collection
	coll_inreg dest; ///< Destination collection
	cxx_function::unique_function<bool(const sstring& oid)> pred;
      } split_collection;
      /// Move an object from one collection to another
      ///
      /// \note This is used by Ceph's recovery logic, to move objects
      /// out of Temporary Collections.
      ///
      /// \warning The same concerns apply as for
      /// split_collection.
      ///
      /// \warning It's very likely that this call might invalidate
      /// outstanding handles to the object it moves. In some
      /// implementations, this call might function as a barrier,
      /// preventing the acquisition of new handles on the object in
      /// question and waiting for outstanding handles to be
      /// released. If this is the case, the Store should make sure
      /// the compound does not deadlock on itself. Such an
      /// implementation should:
      ///
      /// 1. Disallow new references on the object
      /// 2. Release any references in the registers, recording their indices
      /// 3. Wait for the object to become free
      /// 4. Move it
      /// 5. Update the saved indices with new references to the object
      /// 6. Allow new references to be acquired
      ///
      /// \see split_collection
      struct {
	obj_inreg src; ///< Source object
	coll_inreg dest_coll; ///< Destination collection
	opaque_inreg dest_oid; ///< New name for the object
      } move_coll_rename;
      /// Enumerate objects in a collection
      ///
      /// \see obj_cursor
      struct {
	coll_inreg coll; ///< Collection to enumerate
	boost::optional<oidcur_inreg> cursor; ///< Cursor to continue
					      ///< enumeration (none
					      ///< if beginning)
	size_t to_return; ///< Maximum number of oids to return. The
			  ///< Store may return fewer but must not
			  ///< return more.
	oidcur_outreg next_cursor; ///< Index to store cursor to continue
				   ///< this enumeration.
      } enumerate_objects;
      /// Get cursor for a given object
      ///
      /// This function gives a cursor that will continue an
      /// enumeration as if a previous enumeration had ended just
      /// before returning `oid`.
      ///
      /// \note Not supported on stores without a well-defined
      /// enumeration order for oids.
      struct {
	coll_inreg coll; ///< Collection to query
	opaque_inreg oid; ///< OID to bound cursor
	oidcur_outreg cursor; ///< Cursor lower bounded by `oid`
      } obj_cursor;
      /// Enumerate all collections in this store
      ///
      /// \note Ceph ObjectStore just returns them all at once. Do we
      /// think we'll need cursor-like logic the way we do for
      /// attribute and object enumeration?
      struct {
      } enumerate_collections;
      /// Sync the entire Store
      ///
      /// All of it. No questions asked.
      struct {
      } sync;
    };
  };
  std::vector<Op> ops;
  bool is_read_only() const {
    return std::all_of(ops.cbegin(), ops.cend(),
		       std::mem_fn(&Op::is_read_only));
  }
};

} // namespace store
} // namespace crimson

#endif // CRIMSON_STORE_COMPOUND_H
