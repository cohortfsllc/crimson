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

/// \file Store.cc
/// \brief Implementation of object storage, as well as shared functionality
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#include <system_error>
#include "Store.h"

namespace crimson {
  namespace store {
    using namespace std::literals;

    class _category : public std::error_category {
      const char* name() const noexcept override {
	return "store";
      }
      std::string message(int ev) const override {
	switch (static_cast<errc>(ev)) {
	case errc::no_such_collection:
	  return "No such collection"s;
	case errc::no_such_object:
	  return "No such object"s;
	case errc::no_such_attribute_key:
	  return "No such attribute key"s;
	case errc::collection_exists:
	  return "Collection exists"s;
	case errc::object_exists:
	  return "Object exists"s;
	case errc::operation_not_supported:
	  return "Operation not supported"s;
	case errc::invalid_handle:
	  return "Invalid handle"s;
	case errc::invalid_cursor:
	  return "invalid cursor"s;
	case errc::out_of_range:
	  return "Out of range"s;
	case errc::invalid_argument:
	  return "Invalid argument"s;
	case errc::collection_not_empty:
	  return "Collection not empty"s;
	}
	return "Unknown error code"s;
      }

      std::error_condition default_error_condition(int ev)
	const noexcept override {
	switch (static_cast<errc>(ev)) {
	case errc::no_such_collection:
	  return std::errc::no_such_file_or_directory;
	case errc::no_such_object:
	  return std::errc::no_such_file_or_directory;
	case errc::no_such_attribute_key:
	  return std::errc::no_such_file_or_directory;
	case errc::collection_exists:
	  return std::errc::file_exists;
	case errc::object_exists:
	  return std::errc::file_exists;
	case errc::operation_not_supported:
	  return std::errc::operation_not_supported;
	case errc::out_of_range:
	  return std::errc::invalid_argument;
	case errc::invalid_argument:
	  return std::errc::invalid_argument;
	default:
	  return std::error_condition(ev, *this);
	}
      }
    };

    const std::error_category& category() {
      static _category instance;
      return instance;
    }
  }
}
