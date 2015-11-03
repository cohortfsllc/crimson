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

/// \file Nihil.h
/// \brief Object store that stores nothing.
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#ifndef CRIMSON_STORE_NIHIL_H
#define CRIMSON_STORE_NIHIL_H

#include <utility>

#include <boost/uuid/uuid.hpp>

#include "Store.h"

namespace crimson {

  /// Storage interface
  namespace store {
    class Nihil : public Store {
      boost::uuids::uuid id;
    public:
      std::pair<future<CompoundRes> future<>> exec_compound(
	Sequencer& osr, Compound& t) override;

      virtual ~Nihil() = default;
      Nihil(const Store& o) = delete;
      const Nihil& operator=(const Nihil& o) = delete;
      Nihil(Nihil&& o) = delete;
      const Nihil& operator=(Nihil&& o) = delete;

      // mgmt
      size_t get_max_object_name_length() override;
      size_t get_max_attr_name_length() override;
      future<> mkfs() override; // wipe

      /// Get the CPU to look up a collection
      unsigned get_cpu(const sstring& cid) const override;

      /**
       * Set and get internal fsid for this instance. No external data
       * is modified
       */
      future<> set_fsid(boost::uuids::uuid u) override;
      future<boost::uuids::uuid> get_fsid() override;
    };
  } // namespace store
} // namespace crimson

#endif // CRIMSON_STORE_NIHIL_H
