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

/// \file xxHash.h
/// \brief C++ wrapper for xxHash
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#ifndef CRIMSON_COMMON_XXHASH_H
#define CRIMSON_COMMON_XXHASH_H

#include <experimental/string_view>

#include "GSL/include/gsl.h"

#include "xxHash/xxhash.h"

namespace crimson {
  class xxHash {
  private:
    XXH64_state_t state;
  public:
    xxHash(uint64_t seed = 0) {
      XXH64_reset(&state, seed);
    }

    void reset(uint64_t seed = 0) {
      XXH64_reset(&state, seed);
    }

    template<typename C>
    void update(const std::experimental::basic_string_view<C> v) noexcept {
      Expects(v.data() != nullptr);
      auto ret = XXH64_update(&state,
			      static_cast<void*>(v.data()),
			      v.length() * sizeof(C));
      Ensures(ret == XXH_OK);
    }
    template<typename C>
    void update(const gsl::not_null<gsl::basic_string_view<C>> v) noexcept {
      auto ret = XXH64_update(&state,
			      static_cast<void*>(v.data()),
			      v.length() * sizeof(C));
      Ensures(ret == XXH_OK);
    }
    void digest() const noexcept {
      return XXH64_digest(&state);
    }

    template<typename C>
    static uint64_t operator()(const std::experimental::basic_string_view<C> v,
			       uint64_t seed = 0) noexcept {
      Expects(v.data() != nullptr);
      return XXH64(static_cast<void*>(v.data()), v.length() * sizeof(C), seed);
    }
    template<typename C>
    static uint64_t operator()(const gsl::not_null<
				 gsl::basic_string_view<C>> v,
			       uint64_t seed = 0) noexcept {
      return XXH64(static_cast<void>v.data(), v.length() * sizeof(C), seed);
    }
  };
} // namespace crimson
#endif // !CRIMSON_COMMON_XXHASH_H
