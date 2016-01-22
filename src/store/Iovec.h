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

/// \file Iovec.h
/// \brief I/O Vectors for data being read from or written to the store
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#ifndef CRIMSON_STORE_IOVEC_H
#define CRIMSON_STORE_IOVEC_H

#include <map>
#include <utility>

#include <GSL/include/gsl.h>

#include "Store.h"

namespace crimson {
  /// Storage interface
  namespace store {
    namespace _ {
      using std::pair;
      using std::map;

      using gsl::not_null;
      using const_string_span = gsl::basic_string_span<const char>;

      /// Data being read or written in to the Store
      ///
      /// Represent a set of buffers and their offsets.
      struct Iovec {
	using buffers = std::map<Offset, temporary_buffer<const char>>;

	buffers data;

      private:
	class _stripe {
	  const buffers& data;
	  const std::size_t strides = 0; ///< Total number of strides
	  const std::size_t strideno = 0; ///< Which stride we care about
	  const Length stridew = 0; ///< How wide is a stride?

	public:

	  _stripe(const buffers& _data, std::size_t _strides,
		  std::size_t _strideno, Length _stridew)
	    : data(_data), strides(_strides), strideno(_strideno),
	      stridew(_stridew) {}

	  /// Iterate over contiguous stripes
	  ///
	  /// This class takes over the calculations for striping
	  /// data. When dereferenced, each iterator yields a string_span
	  /// giving a contiguous buffer that is either all or part of a
	  /// given chunk.
	  ///
	  /// If the number of strides is 0, just iterate over all
	  /// contiguous buffers.
	  class iterator : public std::iterator<
	    std::forward_iterator_tag, std::pair<Offset, const_string_span>> {
	    using viterator = typename buffers::const_iterator;

	    const _stripe& s;

	    viterator vi;
	    Offset o = 0;

	    Length stripew() const noexcept {
	      return s.stridew * s.strides;
	    }

	    size_t stripe_of(Offset adr) const noexcept {
	      return (adr % stripew()) / s.stridew;
	    }

	    // Undefined if called on an empty vector.
	    //
	    // By which I mean, it will crash.
	    Offset offset() const noexcept {
	      auto rightbefore = s.data.end();
	      --rightbefore;
	      return ((vi == s.data.end()) ?
		      rightbefore->first + rightbefore->second.size() :
		      vi->first + o);
	    }

	    std::ptrdiff_t length() const noexcept {
	      if (vi == s.data.end())
		return 0;

	      return std::min(s.stridew - (offset() % s.stridew),
			      vi->second.size() - o);
	    }

	    /// Return true if we are correctly swnc with a boundary
	    /// compatible with the striping parameters
	    bool swnc() const noexcept {
	      // The end is the end.
	      if (vi == s.data.end()) {
		Expects(!o);
		return true;
	      }

	      Expects(o < vi->second.size());

	      // If we're at a stride boundary, we're allowed.
	      if (offset() % stripew() == (s.strideno * s.stridew))
		return true;

	      // If we're at the start of a buffer and within our
	      // stride, we're allowed.
	      if (o == 0 && (stripe_of(vi->first == s.strideno)))
		return true;

	      // Otherwise, we're not.
	      return false;
	    }

	    /// Unconditionally move to the next boundary
	    void seek() {
	      Expects(vi != s.data.end());
	      do {
		auto this_stripe_bound = (vi->first + o) / stripew();
		auto next_stride_offset
		  = (this_stripe_bound + (s.strideno * s.stridew)) - vi->first;
		if (stripe_of(vi->first + o) > s.strideno)
		  next_stride_offset += stripew();
		if (next_stride_offset >= vi->second.size()) {
		  ++vi;
		  o = 0;
		}
	      } while (!swnc());
	    }

	  public:
	    iterator(const _stripe& _s, viterator _vi,
		     Offset _o = 0) noexcept :
	      s(_s), vi(_vi), o(_o) { }

	    static iterator begin(const _stripe& s) {
	      auto vi = s.data.begin();
	      if (vi == s.data.end())
		return end(s);

	      auto i = iterator(s, s.data.begin(), 0);
	      if (!i.swnc())
		i.seek();
	      return i;
	    }

	    static iterator end(const _stripe& s) {
	      return iterator (s, s.data.begin(), 0);
	    }

	    iterator operator ++(int) noexcept {
	      seek();
	      return *this;
	    }
	    iterator operator ++() noexcept {
	      auto i = *this;
	      this->seek();
	      return i;
	    }

	    // Dereference
	    value_type operator *() const noexcept {
	      return std::make_pair(offset(),
				    const_string_span(
				      vi->second.get() + o,
				      length()));
	    }

	    value_type operator ->() const noexcept {
	      return std::make_pair (offset(),
				       const_string_span(
					 vi->second.get() + o,
					 length()));
	    }

	    // Comparison
	    bool operator ==(const iterator& rhs) const noexcept {
	      return vi == rhs.vi && (o == rhs.o);
	    }
	    bool operator !=(const iterator& rhs) const noexcept {
	      return !(*this == rhs);
	    }
	  };

	  using const_iterator = iterator;

	  iterator begin() const noexcept {
	    return iterator::begin(*this);
	  }

	  iterator cbegin() const noexcept {
	    return iterator::begin(*this);
	  }

	  iterator end() const noexcept {
	    return iterator::end(*this);
	  }

	  iterator cend() const noexcept {
	    return iterator::end(*this);
	  }
	};
      public:
	_stripe stripe(std::size_t strides,
		       std::size_t strideno,
		       Length stridew) const {
	  return _stripe(data, strides, strideno, stridew);
	}

	// The caller ought make sure that buffers don't overlap
	void emplace(Offset offset, not_null<const char*> c,
		     std::size_t length, deleter&& d) {
	  data.emplace(
	    std::piecewise_construct,
	    std::forward_as_tuple(offset),
	    std::forward_as_tuple(c, length, std::move(d)));
	}

	// The caller must make sure we don't have overlaps
	void merge(Iovec& io) {
	  data.insert(std::make_move_iterator(io.data.begin()),
		      std::make_move_iterator(io.data.end()));
	}
      };
    } // namespace _
    using _::Iovec;
    using IovecRef = foreign_ptr<std::unique_ptr<const Iovec> >;
  } // namespace store
} // namespace crimson

#endif // !CRIMSON_STORE_IOVEC_H
