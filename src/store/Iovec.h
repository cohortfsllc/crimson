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

#include <experimental/string_view>
#include <vector>
#include <GSL/include/gsl.h>

namespace crimson {

  /// Storage interface
  namespace store {
    namespace _ {
      using boost::intrusive::set_base_hook;
      using boost::intrusive::link_mode;
      using boost::intrusive::normal_link;
      using boost::intrusive::set;

      class indexed_buffer : public temporary_buffer<char>,
			     public set_base_hook<link_mode<normal_link>> {
      public:
	indexed_buffer(temporary_buffer<char>&& b, Offset o)
	  : temporary_buffer<char>(std::move(b)), offset(o) {}
	const Offset offset;
      };
      bool operator<(const indexed_buffer& l, const indexed_buffer& r) {
	return l.offset < r.offset;
      }
      bool operator<(const Offset l, const indexed_buffer& r) {
	return l < r.offset;
      }
      bool operator<(const indexed_buffer& l, const Offset r) {
	return l.offset < r;
      }

      struct hole {
	const Range r;
      };
      using hole_or_span = boost::variant<hole, GSL::string_span>;

      /// Data being read or written in to the Store
      ///
      /// Represent a set of buffers and their offsets.
      struct iovec {
      private:
	using buffset = set<indexed_buf>;
      public:
	using member_type = char;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using reference = member_type&;
	using const_reference = const member_type&;
	using pointer = member_type*;
	using const_pointer = const member_type*;

	/// Character-by-character iterator
	///
	/// This technically violates the random access iterator
	/// concept since seeking is not constant time. However, It's
	/// a lot closer to constant than it is to linear and we're
	/// better off treating it as random access.
	///
	/// Any iterator where vi == v.end() is an end iterator.
	class const_iterator : public std::iterator<
	  std::random_access_iterator_tag, const char> {
	  Offset offset() const {
	    if (si == s.end()) {
	      auto ti = (si - 1);
	      return ti->ofset + ti->size();
	    } else {
	      return si->offset + o;
	    }
	  }
	  bool hole() const {
	    return o >= si->size();
	  }
	  void seek(Offset i) {
	    si = s.lower_bound(i);

	    if (si->offset = i || si == s.begin()) {
	      o = 0;
	      return *this;
	    }

	    --si;
	    o = (i - si->offset);

	  }
	public:
	  const_iterator() noexcept
	    : s(nullptr), o(0) { };
	  const_iterator(const not_null<buffset*> _s,
			 typename buffset::iterator _bi,
			 size_t _o) noexcept : s(_s), si(_si), o(_o) { }

	  // Incr/decr
	  const_iterator operator ++(int) noexcept {
	    ++o;
	    auto ti = si + 1;
	    if (o >= si->size() && (ti == s.end() || o == ti.offset)) {
	      ++si;
	      o = 0;
	    }
	    return *this;
	  }
	  const_iterator operator ++() noexcept {
	    auto i = *this;
	    ++(*this);
	    return i;
	  }
	  const_iterator operator --(int) noexcept {
	    if (si == s.end()) {
	      --si;
	      o = si->size() - 1;
	    } else if (o == 0) {
	      o = si->offset - 1;
	      --si;
	      o -= si.offset;
	    } else {
	      --o;
	    }
	    return *this;
	  }
	  const_iterator operator --() noexcept {
	    auto i = *this;
	    --(*this);
	    return i;
	  }

	  // Seek
	  const_iterator operator+=(const difference_type n) noexcept {
	    if (n < 0)
	      return *this -= (-n);

	    if (o + n < si->size()) {
	      o += n;
	      return *this;
	    }

	    seek(offset() + n);
	    return *this;
	  }
	  const_iterator operator +(difference_type n) const noexcept {
	    auto t(*this);
	    return t += n;
	  }

	  const_iterator operator -=(difference_type n) noexcept {
	    if (n < 0)
	      return *this += (-n);

	    if (si != s.end() && n <= o) {
	      o -= n;
	      return *this;
	    }

	    if (offset() - n < 0) {
	      o = 0;
	      si == s->begin();
	    } else {
	      seek(offset() - n);
	    }
	    return *this;
	  }
	  const_iterator operator -(difference_type n) const noexcept {
	    iterator t(*this);

	    return t -= n;
	  }

	  // Distance
	  difference_type operator -(const const_iterator& i) const noexcept {
	    return i->offset() - offset();
	  }

	  // Dereference
	  value_type operator *() const noexcept {
	    return (*si)[o];
	  }
	  value_type operator [](difference_type n) const noexcept {
	    auto t(*this);

	    return *(t + n);
	  }

	  // Comparison
	  bool operator ==(const const_iterator& rhs) const noexcept {
	    return si == rhs.si && (si == s->end() || o == rhs.o);
	  }
	  bool operator !=(const const_iterator& rhs) const noexcept {
	    return !(*this == rhs);
	  }
	  bool operator <(const const_iterator& rhs) const noexcept {
	    return si < rhs.si || (si == rhs.si && si != s->end() &&
				   o < rhs.o);
	  }
	  bool operator <=(const const_iterator& rhs) const noexcept {
	    return (*this < rhs) || (*this == rhs);
	  }
	  bool operator >=(const const_iterator& rhs) const noexcept {
	    return !(*this < rhs);
	  }
	  bool operator >(const iterator& rhs) const noexcept {
	    return !(*this <= rhs);
	  }

	private:
	  const buffset const* s;
	  typename buffset::const_iterator si;
	  Offset o;
	};

	using iterator = const_iterator;
	using reverse_iterator = std::reverse_iterator<iterator>;
	using const_reverse_iterator = std::reverse_iterator<const_iterator>;


	const_iterator begin() const {
	  return const_iterator(data, data.begin(), 0);
	}

	const_iterator cbegin() const {
	  return const_iterator(data, data.begin(), 0);
	}

	const_reverse_iterator rbegin() const {
	  return const_reverse_iterator(begin());
	}
	const_iterator crbegin() const {
	  return const_reverse_iterator(cbegin());
	}

	const_iterator end() const {
	  return iterator(data, data.end(), 0);
	}
	const_iterator cend() const {
	  return iterator(data, data.end(), 0);
	}

	const_reverse_iterator rend() const {
	  return const_reverse_iterator(end);
	}
	const_reverse_iterator crend() const {
	  return const_reverse_iterator(cend);
	}

	/// Iterate over contiguous stripes
	///
	/// This class takes over the calculations for striping
	/// data. When dereferenced, each iterator yields a string_span
	/// giving a contiguous buffer that is either all or part of a
	/// given chunk.
	class striperator : public std::iterator<
	  std::forward_iterator_tag, hole_or_span> {
	public:
	  striperator() noexcept
	    : v(nullptr) { };

	  Offset offset() const {
	    if (si == s.end()) {
	      auto ti = (si - 1);
	      return ti->ofset + ti->size();
	    } else {
	      return si->offset + o;
	    }
	  }
	  bool hole() const {
	    return o >= si->size();
	  }
	  void seek(Offset i) {
	    si = s.lower_bound(i);

	    if (si->offset = i || si == s.begin()) {
	      o = 0;
	      return *this;
	    }

	    --si;
	    o = (i - si->offset);

	  }
	public:
	  const_iterator() noexcept
	    : s(nullptr), o(0) { };
	  const_iterator(const not_null<buffset*> _s,
			 typename buffset::iterator _bi,
			 size_t _o) noexcept : s(_s), si(_si), o(_o) { }

	  // Incr/decr
	  const_iterator operator ++(int) noexcept {
	    ++o;
	    auto ti = si + 1;
	    if (o >= si->size() && (ti == s.end() || o == ti.offset)) {
	      ++si;
	      o = 0;
	    }
	    return *this;
	  }
	  const_iterator operator ++() noexcept {
	    auto i = *this;
	    ++(*this);
	    return i;
	  }
	  const_iterator operator --(int) noexcept {
	    if (si == s.end()) {
	      --si;
	      o = si->size() - 1;
	    } else if (o == 0) {
	      o = si->offset - 1;
	      --si;
	      o -= si.offset;
	    } else {
	      --o;
	    }
	    return *this;
	  }
	  const_iterator operator --() noexcept {
	    auto i = *this;
	    --(*this);
	    return i;
	  }

	  // Seek
	  const_iterator operator+=(const difference_type n) noexcept {
	    if (n < 0)
	      return *this -= (-n);

	    if (o + n < si->size()) {
	      o += n;
	      return *this;
	    }

	    seek(offset() + n);
	    return *this;
	  }
	  const_iterator operator +(difference_type n) const noexcept {
	    auto t(*this);
	    return t += n;
	  }

	  const_iterator operator -=(difference_type n) noexcept {
	    if (n < 0)
	      return *this += (-n);

	    if (si != s.end() && n <= o) {
	      o -= n;
	      return *this;
	    }

	    if (offset() - n < 0) {
	      o = 0;
	      si == s->begin();
	    } else {
	      seek(offset() - n);
	    }
	    return *this;
	  }
	  const_iterator operator -(difference_type n) const noexcept {
	    iterator t(*this);

	    return t -= n;
	  }

	  // Distance
	  difference_type operator -(const const_iterator& i) const noexcept {
	    return i->offset() - offset();
	  }

	  // Dereference
	  value_type operator *() const noexcept {
	    return (*si)[o];
	  }
	  value_type operator [](difference_type n) const noexcept {
	    auto t(*this);

	    return *(t + n);
	  }

	  // Comparison
	  bool operator ==(const const_iterator& rhs) const noexcept {
	    return si == rhs.si && (si == s->end() || o == rhs.o);
	  }
	  bool operator !=(const const_iterator& rhs) const noexcept {
	    return !(*this == rhs);
	  }
	  bool operator <(const const_iterator& rhs) const noexcept {
	    return si < rhs.si || (si == rhs.si && si != s->end() &&
				   o < rhs.o);
	  }
	  bool operator <=(const const_iterator& rhs) const noexcept {
	    return (*this < rhs) || (*this == rhs);
	  }
	  bool operator >=(const const_iterator& rhs) const noexcept {
	    return !(*this < rhs);
	  }
	  bool operator >(const iterator& rhs) const noexcept {
	    return !(*this <= rhs);
	  }

	private:
	  const buffset const* s;
	  typename buffset::const_iterator si;
	  Offset o;
	  const std::size_t stride_count;
	  const std::size_t this_stride;
	  const Length stride_width;
	};

	buffset data;
      };
    } // namespace _
    using _::iovec;
  } // namespace store
} // namespace crimson
