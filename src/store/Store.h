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
#include <core/sstring.hh>
#include <core/temporary_buffer.hh>

#include "GSL/include/gsl.h"
#include "cxx_function/cxx_function.hpp"

namespace crimson {

  /// Storage interface
  namespace store {
    class Collection;
    using CollectionRef = foreign_ptr<boost::intrusive_ptr<Collection>>;

    using Offset = uint64_t;
    using Length = uint64_t;
    struct Range {
      Offset offset;
      Length length;

      Range(Offset _offset, Length _length) :
	offset(_offset), length(_length) {
	Expects(std::numeric_limits<Length>::max() - offset >= length);
      }
    };

    class Sequencer {
    public:
      virtual ~Sequencer() = default;
      /// wait for any queued transactions on this sequencer to apply
      virtual future<> flush() = 0;
    };

    class Store {
    public:
      Store() = default;
      virtual ~Store() = default;
      Store(const Store& o) = delete;
      const Store& operator=(const Store& o) = delete;

      // mgmt
      virtual size_t get_max_object_name_length() const noexcept = 0;
      virtual size_t get_max_attr_name_length() const noexcept = 0;
      virtual future<> mkfs() = 0;  // wipe

      /// Get the CPU to look up a collection
      ///
      /// Collections exist on multiple CPUs, but one CPU has the tree
      /// where the collection name can be looked up and which drives
      /// initialization.
      virtual unsigned get_cpu(const sstring& cid) const noexcept = 0;

      /**
       * Set and get internal fsid for this instance. No external data
       * is modified
       */
      virtual future<> set_fsid(boost::uuids::uuid u) = 0;
      virtual future<boost::uuids::uuid> get_fsid() const = 0;

      /// Make a collection
      ///
      /// Create a new collection. The collection must not exist prior
      /// to the call.
      ///
      /// \param[in] cid Collection ID
      virtual future<CollectionRef>create_collection(sstring cid) = 0;
      /// Enumerate all collections in this store
      ///
      /// \note Ceph ObjectStore just returns them all at once. Do we
      /// think we'll need cursor-like logic the way we do for
      /// attribute and object enumeration?
      virtual future<sstring> enumerate_collections() const = 0;
      /// Commit the entire Store
      ///
      /// All of it. No questions asked. This function acts as a
      /// barrier on all operations. No operations may begin until all
      /// outstanding ones are completed and stored stably.
      virtual future<> commit() = 0;
    };
  } // namespace store
} // namespace crimson

#endif // CRIMSON_STORE_STORE_H
