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
#include <system_error>

#include <boost/intrusive_ptr.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/optional.hpp>
#include <boost/uuid/uuid.hpp>

#include <core/future.hh>
#include <core/sharded.hh>
#include <core/temporary_buffer.hh>

#include "GSL/include/gsl.h"
#include "cxx_function/cxx_function.hpp"

#include "crimson.h"

namespace crimson {


  /// Storage interface
  namespace store {
    /// Error codes for the Store interface
    enum class errc {
      no_such_collection,
      no_such_object,
      no_such_attribute_key,
      collection_exists,
      object_exists,
      operation_not_supported,
      invalid_handle,
      out_of_range
    };

    class error_category : public std::error_category {
      virtual const char* name() const noexcept;
      virtual std::string message(int ev) const;
      virtual std::error_condition default_error_condition(
	int ev) const noexcept {
	switch (static_cast<errc>(ev)) {
	case errc::no_such_collection:
	  return std::errc::no_such_file_or_directory;
	case errc::no_such_object:
	  return std::errc::no_such_file_or_directory;
	case errc::collection_exists:
	  return std::errc::file_exists;
	case errc::object_exists:
	  return std::errc::file_exists;
	case errc::operation_not_supported:
	  return std::errc::operation_not_supported;
	case errc::out_of_range:
	  return std::errc::invalid_argument;
	default:
	  return std::error_condition(ev, *this);
	}
      }
    };

    const std::error_category& error_category();

    static inline std::error_condition make_error_condition(errc e) {
      return std::error_condition(static_cast<int>(e), error_category());
    }

    static inline std::error_code make_error_code(errc e) {
      return std::error_code(static_cast<int>(e), error_category());
    }
  } // namespace store
} // namespace crimson

namespace std {
  template<>
  struct is_error_code_enum<crimson::store::errc> : public std::true_type {};
};

namespace crimson {
  namespace store {
    class Collection;
    using CollectionRef = seastar::foreign_ptr<
      boost::intrusive_ptr<Collection>>;

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
      virtual unsigned get_cpu(const string& cid) const noexcept = 0;

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
      virtual future<CollectionRef>create_collection(string cid) = 0;
      /// Enumerate all collections in this store
      ///
      /// \note Ceph ObjectStore just returns them all at once. Do we
      /// think we'll need cursor-like logic the way we do for
      /// attribute and object enumeration?
      virtual future<std::vector<string>> enumerate_collections() const = 0;
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
