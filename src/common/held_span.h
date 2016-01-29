// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

// Crimson: a prototype high performance OSD

// Copyright (C) 2015 Adam C. Emerson <aemerson@redhat.com

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

/// \file held_span.h
/// \brief A span packaged up with a deleter
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#ifndef CRIMSON_COMMON_HELD_SPAN_H
#define CRIMSON_COMMON_HELD_SPAN_H

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <new>
#include <numeric>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <GSL/include/gsl.h>

#include "crimson.h"

namespace crimson {
  /// A span that has ownership of some sort
  ///
  /// Inspired by temporary_buffer, pair a gsl::span with a deleter to
  /// allow passing contiguous blocks of objects to and from functions
  /// without having them go out of existence at inconvenient times.
  template<typename T,
	   std::ptrdiff_t FirstDimension = gsl::dynamic_range,
	   std::ptrdiff_t... RestDimensions>
  struct held_span {
    gsl::span<T, FirstDimension, RestDimensions...> s;
    using spantype = decltype(s);
    deleter d;
    using spanptr = std::add_pointer_t<spantype>;

    held_span() noexcept {};
    held_span(const spantype& _s, deleter _d) noexcept
      : s(_s), d(_d) {};
    held_span(spantype&& _s, deleter _d) noexcept
      : s(std::move(_s)), d(std::move(_d)) {};

    held_span(const held_span&) = delete;
    held_span& operator =(const held_span&) = delete;

    held_span(held_span&&) = default;
    held_span& operator =(held_span&&) = default;

    held_span(std::initializer_list<T> ts) {
      unique_ptr<T[]> ptr = new T[ts.size()];
      s = spantype(ptr.get(), ts.length());
      std::copy(ts.begin(), ts.end(), s.begin());
      d = seastar::make_object_deleter(make_foreign(std::move(ptr)));
    }

    held_span(unique_ptr<std::vector<T>>&& v) noexcept {
      s = spantype(*v);
      d = seastar::make_object_deleter(make_foreign(std::move(v)));
    }

    held_span(shared_ptr<std::vector<T>>& v) noexcept {
      s = spantype(*v);
      d = seastar::make_object_deleter(make_foreign(v));
    }

    held_span(lw_shared_ptr<std::vector<T>>& v) noexcept {
      s = spantype(*v);
      d = seastar::make_object_deleter(make_foreign(v));
    }

    template<size_t N>
    held_span(unique_ptr<std::array<T, N>>&& a) noexcept {
      s = spantype(*a);
      d = seastar::make_object_deleter(make_foreign(std::move(a)));
    }

    template<size_t N>
    held_span(shared_ptr<std::array<T, N>>& a) noexcept {
      s = spantype(*a);
      d = seastar::make_object_deleter(make_foreign(a));
    }

    template<size_t N>
    held_span(lw_shared_ptr<std::array<T, N>>& a) noexcept {
      s = spantype(*a);
      d = seastar::make_object_deleter(make_foreign(a));
    }

    held_span share() {
      return {s, d.share()};
    }

    spantype& operator *() {
      return s;
    }

    spanptr operator ->() {
      return &s;
    }
  };
} // namespace crimson

#endif // CRIMSON_COMMON_HELD_SPAN_H
