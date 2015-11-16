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

/// \file Manager.h
/// \brief Class that manages object Stores and holds common resoruces
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#ifndef CRIMSON_STORE_MANAGER_H
#define CRIMSON_STORE_MANAGER_H

namespace crimson {
  /// Storage interface
  namespace store {
    class Collection;
    class Object;

    /// Manager of Stores
    ///
    /// Manager owns the slab allocators used by the various object
    /// stores and which stores are available.
    class Manager {
      seastar::sharded<Manager>& peers;
    };
  } // namespace store
} // namespace crimson

#endif //! CRIMSON_STORE_MANAGER_H
