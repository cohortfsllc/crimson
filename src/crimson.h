// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

// Crimson: a prototype high performance OSD

// Copyright (C) 2015 Adam C. Emerson <aemerson@redhat.com
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

/// \file crimson.h
/// \brief Basic definitions and data types
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#include <experimental/optional>

#include <core/future.hh>
#include <core/sharded.hh>
#include <core/shared_ptr.hh>
#include <core/sstring.hh>
#include <core/temporary_buffer.hh>

namespace crimson {
  using seastar::future;
  using seastar::promise;
  using seastar::make_ready_future;
  using seastar::make_exception_future;
  using seastar::do_for_each;
  using seastar::parallel_for_each;
  using seastar::map_reduce;

  using seastar::shared_ptr;
  using seastar::make_shared;
  using seastar::lw_shared_ptr;
  using seastar::make_lw_shared;
  using seastar::foreign_ptr;
  using seastar::make_foreign;
  using seastar::deleter;

  using string = seastar::sstring;
  using const_buffer = seastar::temporary_buffer<const char>;
  using temporary_buffer = seastar::temporary_buffer<char>;

  using seastar::now;
  using seastar::input_stream;
  using seastar::output_stream;

  using seastar::unaligned_cast;

  using seastar::engine;
  using seastar::smp;

  using std::experimental::optional;
  using std::experimental::nullopt;
  using std::pair;
  using std::tuple;
}
