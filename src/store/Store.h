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

/// \file Store.h
/// \brief Interface for object storage, as well as shared functionality
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#ifndef CRIMSON_STORE_STORE_H
#define CRIMSON_STORE_STORE_H

#include <cassert>
#include <memory>
#include <utility>

#include <boost/intrusive_ptr.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/optional.hpp>
#include <boost/uuid/uuid.hpp>

#include <core/future.hh>
#include <core/sharded.hh>
#include <core/slab.hh>
#include <core/sstring.hh>
#include <core/temporary_buffer.hh>

#include "cxx_function/cxx_function.hpp"

#include "Compound.h"

namespace crimson {

  /// Storage interface
  namespace store {

    class Store;
    class Object;
    /// slab allocator for Objects
    ///
    /// \todo Temporary, stuff in StoreManager
    thread_local slab_allocator<Object>* object_slab;

    class Collection;
    using CollectionRef = foreign_ptr<boost::intrusive_ptr<Collection>>;

    /// slab allocator for Collections
    ///
    /// \todo Temporary, stuff in StoreManager
    thread_local slab_allocator<Collection>* collection_slab;

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
	  object_slab->lock_item(o);
	}
	assert(o->ref_count > 0);
      }
      friend inline void intrusive_ptr_release(Object* o) {
	assert(o->ref_count > 0);
	--o->ref_count;
	if (o->ref_count == 1) {
	  object_slab->unlock_item(o);
	} else if (o->ref_count == 0) {
	  object_slab->free(o);
	}
      }
      ///@}
      // For debugging
    private:
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
	  return (std::less<const Collection*>()(std::addressof(*(l.c)),
						 std::addressof(*(r.c))) &&
		  std::less<sstring>()(l.oid, r.oid));
	}
	bool operator()(
	  const Object& l,
	  const std::pair<const CollectionRef&, const sstring&>& r) const {
	  return (std::less<const Collection*>()(std::addressof(*(l.c)),
						 std::addressof(*(r.first))) &&
		  std::less<const sstring>()(l.oid, r.second));
	}
	bool operator()(
	  const std::pair<const CollectionRef&, const sstring&>& l,
	  const Object& r) const {
	  return (std::less<const Collection*>()(std::addressof(*(l.first)),
						 std::addressof(*(r.c))) &&
		  std::less<sstring>()(l.second, r.oid));
	}
      };
      friend Compare;
    public:
      using cache = boost::intrusive::set<
      Object, cache_option,
      boost::intrusive::constant_time_size<false>,
      boost::intrusive::compare<Compare>>;

    protected:
      CollectionRef c;
      const sstring oid;

      mutable bool ready; // double-check var

    public:
      explicit Object(uint32_t _slab_page_index, CollectionRef _c,
		      sstring _oid)
	: slab_page_index(_slab_page_index), c(std::move(_c)),
	  oid(std::move(_oid)) {}
      virtual ~Object() = default;

      const sstring& get_oid() const {
	return oid;
      }

      operator sstring() const {
	return oid;
      }

      const CollectionRef& get_collection() const {
	return c;
      }

      friend class Collection;
    }; /* Object */

    using ObjectRef = foreign_ptr<boost::intrusive_ptr<Object>>;

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
      using cache_link_mode = boost::intrusive::link_mode<
      boost::intrusive::safe_link>;
    public:
      boost::intrusive::set_member_hook<cache_link_mode> cache_hook;
    private:
      using cache_option =
	boost::intrusive::member_hook<Collection, decltype(cache_hook),
				      &Collection::cache_hook>;
      struct Compare {
	bool operator()(const Collection& l, const Collection& r) const {
	  return (std::less<const Store*>()(std::addressof(l.s),
					    std::addressof(r.s)) &&
		  std::less<sstring>()(l.cid, r.cid));
	}
	bool operator()(
	  const Collection& l,
	  const std::pair<const Store&, const sstring&>& r) const {
	  return (std::less<const Store*>()(std::addressof(l.s),
					    std::addressof(r.first)) &&
		  std::less<sstring>()(l.cid, r.second));
	}
	bool operator()(
	  const std::pair<const Store&, const sstring&>& l,
	  const Collection& r) const {
	  return (std::less<const Store*>()(std::addressof(l.first),
					    std::addressof(r.s)) &&
		  std::less<sstring>()(l.second, r.cid));
	}
      };
      friend Compare;
    public:
      using cache = boost::intrusive::set<
      Collection, cache_option,
      boost::intrusive::constant_time_size<false>,
      boost::intrusive::compare<Compare>>;

    private:
      Store& s;
      const sstring cid;
      const cxx_function::function<unsigned(const Collection&,
					    const sstring&) const> cpu_fn;

    public:

      template<typename... Args>
      explicit Collection(Store& _s, sstring _cid,
			  Args&& ...cpu_fn_args)
	: s(_s),  cid(std::move(_cid)),
	  cpu_fn(std::forward<Args>(cpu_fn_args)...) {}

      virtual ~Collection() = default;

      Collection(const Collection&) = delete;
      Collection& operator =(const Collection&) = delete;
      Collection(Collection&&) = delete;
      Collection& operator =(Collection&&) = delete;

      Store& get_store() const {
	return s;
      }
      unsigned get_cpu(const sstring& oid) const {
	return cpu_fn(*this, oid);
      }

      const sstring& get_cid() const {
	return cid;
      }
    };



/**
 * a sequencer orders transactions
 *
 * Any transactions queued under a given sequencer will be applied in
 * sequence.  Transactions queued under different sequencers may run
 * in parallel.
 *
 * Clients of Store create and maintain their own Sequencer objects.
 * When a list of transactions is queued the caller specifies a
 * Sequencer to be used.
 *
 */

    class Sequencer {
    public:
      virtual ~Sequencer() = default;
      /// wait for any queued transactions on this sequencer to apply
      virtual future<> flush() = 0;
    };


    class Store {

    public:
      /*********************************
       * All objects are identified as a named object in a named
       * collection.  Operations support the creation, mutation, deletion
       * and enumeration of objects within a collection.
       *
       * Each object has four distinct parts: byte data, xattrs,
       * omap_header and omap entries.
       *
       * The data portion of an object is conceptually equivalent to a
       * file in a file system. Random and Partial access for both read
       * and write operations is required.
       *
       * Xattrs are equivalent to the extended attributes of file
       * systems. Xattrs are a set of key/value pairs.  Sub-value access
       * is not required. It is possible to enumerate the set of xattrs in
       * key order.  At the implementation level, xattrs are used
       * exclusively internal to Ceph and the implementer can expect the
       * total size of all of the xattrs on an object to be relatively
       * small, i.e., less than 64KB. Much of Ceph assumes that accessing
       * xattrs on temporally adjacent object accesses (recent past or
       * near future) is inexpensive.
       *
       * omap_header is a single blob of data. It can be read or written
       * in total.
       *
       * Omap entries are conceptually the same as xattrs
       * but in a different address space. In other words, you can have
       * the same key as an xattr and an omap entry and they have distinct
       * values. Enumeration of xattrs doesn't include omap entries and
       * vice versa. The size and access characteristics of omap entries
       * are very different from xattrs. In particular, the value portion
       * of an omap entry can be quite large (MBs).  More importantly, the
       * interface must support efficient range queries on omap entries even
       * when there are a large numbers of entries.
       *
       *********************************/

      /*******************************
       *
       * Collections
       *
       * A collection is simply a grouping of objects that can be
       * enumerated in order.  Like an individual object, a collection
       * also has a set of xattrs.
       */


    public:
      virtual future<> exec_compound(Sequencer* osr, Compound& t) = 0;

      Store() {}
      virtual ~Store() = default;
      Store(const Store& o) = delete;
      const Store& operator=(const Store& o) = delete;

      // mgmt
      virtual size_t get_max_object_name_length() = 0;
      virtual size_t get_max_attr_name_length() = 0;
      virtual future<> mkfs() = 0;  // wipe
      virtual future<> mkjournal() = 0; // journal only
      virtual bool needs_journal() = 0;  //< requires a journal
      virtual bool wants_journal() = 0;  //< prefers a journal
      virtual bool allows_journal() = 0; //< allows a journal

      /**
       * check the journal uuid/fsid, without opening
       */
      virtual future<boost::uuids::uuid> peek_journal_fsid() = 0;

      /**
       * get ideal max value for collection_list()
       *
       * default to some arbitrary values; the implementation will override.
       */
      virtual int get_ideal_list_max() { return 64; }

      /**
       * Synchronous read operations
       */


      /**
       * exists -- Test for existance of object
       */
      virtual future<bool> exists(const sstring& cid, const sstring& oid) = 0;

      // collections

      /**
       * does a collection exist?
       *
       * @param c collection
       * @returns true if it exists, false otherwise
       */
      virtual future<bool> collection_exists(const sstring& c) = 0;

      /**
       * is a collection empty?
       *
       * @param c collection
       * @returns true if empty, false otherwise
       */
      virtual future<bool> collection_empty(const sstring& c) = 0;


      /**
       * Set and get internal fsid for this instance. No external data
       * is modified
       */
      virtual future<> set_fsid(boost::uuids::uuid u) = 0;
      virtual future<boost::uuids::uuid> get_fsid() = 0;
    };
  } // namespace store
} // namespace crimson

#endif // CRIMSON_STORE_STORE_H
