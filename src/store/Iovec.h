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
    /// Data being written in to the Store
    ///
    /// Represent a set of buffers and the offset to which they should
    /// be written. The Store will never modify a value of this type.
    template<typename Allocator = std::allocator<char>>
    struct invec {
    private:
      using buffvec = std::vector<temporary_buffer<char>, Allocator>;
    public:
      using member_type = char;
      using size_type = std::size_t;
      using difference_type = std::ptrdiff_t;
      using allocator_type = Allocator;
      using reference = member_type&;
      using const_reference = const member_type&;
      using pointer = std::allocator_traits<Allocator>::pointer;
      using const_pointer = std::allocator_traits<Allocator>::const_pointer;

      /// Character-by-character iterator
      ///
      /// This technically violates the random access iterator concept
      /// since seeking is not constant time. But in the usual case
      /// it's a lot closer to constant than it is to linear and we're
      /// better off treating it as random access.
      class iterator : public std::iterator<
	std::random_access_iterator_tag, char> {
      public:
	iterator() noexcept
	  : v(nullptr), bi(nullptr) { };
	iterator(buffvec* _v, buffvec::iterator _vi, pointer _bi,
		 size_type _o) noexcept
	  : v(_v), vi(_vi), bi(_bi), o(_o) {
	  Requres(vi != vector->end() || bi == nullptr);
	};
	iterator operator++() noexcept {
	  iterator i = *this;
	  ++bi;
	  ++o;
	  if (bi == vi->end()) {
	    ++vi;
	    bi = (vi != v.end()) ? bi == vi->begin() : nullptr;
	  }
	  Ensures(vi != vector->end() || bi == nullptr);
	  return i;
	}
	iterator operator++(int) noexcept {
	  ++bi;
	  ++o;
	  if (bi == vi->end()) {
	    ++vi;
	    bi = (vi != v.end()) ? bi == vi->begin() : nullptr;
	  }
	  Ensures(vi != vector->end() || bi == nullptr);
	  return *this;
	}

	iterator operator+=(difference_type n) noexcept {
	  if (n < 0)
	    return *this -= (-n);
	  o += n;
	  while (n > 0 && vi != v.end()) {
	    if (vi->end() - bi < n) {
	      bi += n;
	      n = 0;
	    } else {
	      n -= (vi->end() - bi);
	      ++vi;
	      bi = (vi == v.end() ? nullptr : vi->begin());
	    }
	  }
	  Ensures(vi != vector->end() || bi == nullptr);
	  return *this;
	}

	iterator operator--() noexcept {
	  iterator i = *this;
	  if (vi == v.end() ||
	      bi == vi->begin()) {
	    --vi;
	    bi = --(vi->end());
	  } else {
	    --bi;
	  }
	  --o;
	  Ensures(vi != vector->end() || bi == nullptr);
	  return i;
	}
	iterator operator +(difference_type n) const noexcept {
	  iterator t(*this);
	  return t += n;
	}

	iterator operator --(int) noexcept {
	  if (vi == v.end() ||
	      bi == vi->begin()) {
	    --vi;
	    bi = --(vi->end());
	  } else {
	    --bi;
	  }
	  --o;
	  Ensures(vi != vector->end() || bi == nullptr);
	  return *this;
	}
	iterator operator -=(difference_type n) noexcept {
	  if (n < 0)
	    return *this += (-n);
	  o -= n;
	  while (n > 0 && vi != v.begin()) {
	    if (vi == v->end) {
	      --vi;
	      bi = vi->end();
	    }
	    if (bi - vi->begin() < n) {
	      bi -= n;
	      n = 0;
	    } else {
	      n -= (bi - vi->begin());
	      --vi;
	      bi = vi->end();
	    }
	  }
	  Ensures(vi != vector->end() || bi == nullptr);
	  return *this;
	}

	iterator operator -(difference_type n) const noexcept {
	  iterator t(*this);

	  return t += -n;
	}

	difference_type operator -(const iterator& i) const noexcept {
	  return i->o - o;
	}

	reference operator *() const noexcept {
	  return *bi;
	}
	pointer operator ->() const noxcept {
	  return *bi;
	}
	reference operator [](difference_type n) noexcept {
	  iterator t(*this);

	  return *(t + n);
	}

	bool operator ==(const iterator& rhs) const noexcept {
	  Requres((vi != vector->end() || bi == nullptr) &&
		  (rhs.vi != vector->end() || rhs.bi == nullptr) &&
		  v == rhs->v);

	  return (vi == rhs.vi && bi == rhs.vi);
	}
	bool operator !=(const iterator& rhs) const noexcept {
	  return !(*this == rhs);
	}
	bool operator <(const iterator& rhs) const noexcept {
	  Requres((vi != vector->end() || bi == nullptr) &&
		  (rhs.vi != vector->end() || rhs.bi == nullptr) &&
		  v == rhs->v);
	  return vi <= rhs.vi && bi < rhs.bi;
	}
	bool operator <=(const iterator& rhs) const noexcept {
	  Requres((vi != vector->end() || bi == nullptr) &&
		  (rhs.vi != vector->end() || rhs.bi == nullptr) &&
		  v == rhs->v);
	  return vi <= rhs.vi && bi <= rhs.bi;
	}
	bool operator >=(const iterator& rhs) const noexcept {
	  Requres((vi != vector->end() || bi == nullptr) &&
		  (rhs.vi != vector->end() || rhs.bi == nullptr) &&
		  v == rhs->v);
	  return vi >= rhs.vi && bi >= rhs.bi;
	}
	bool operator >(const iterator& rhs) const noexcept {
	  Requres((vi != vector->end() || bi == nullptr) &&
		  (rhs.vi != vector->end() || rhs.bi == nullptr) &&
		  v == rhs->v);
	  return vi > rhs.vi && bi >= rhs.bi;
	}

      private:
	buffvec const* v;
	buffvec::iterator vi;
	pointer bi;
	size_type o;
      };

      /// Character-by-character iterator, const version
      class const_iterator : public std::iterator<
	std::random_access_iterator_tag, char> {
      public:
	const_iterator() noexcept
	  : v(nullptr), bi(nullptr) { };
	const_iterator(buffvec* _v, buffvec::const_iterator _vi,
		       const_pointer _bi, size_type _o) noexcept
	  : v(_v), vi(_vi), bi(_bi), o(_o) {
	  Requres(vi != vector->end() || bi == nullptr);
	};
	const_iterator operator++() noexcept {
	  const_iterator i = *this;
	  ++bi;
	  ++o;
	  if (bi == vi->end()) {
	    ++vi;
	    bi = (vi != v.end()) ? bi == vi->begin() : nullptr;
	  }
	  Ensures(vi != vector->end() || bi == nullptr);
	  return i;
	}
	const_iterator operator++(int) noexcept {
	  ++bi;
	  ++o;
	  if (bi == vi->end()) {
	    ++vi;
	    bi = (vi != v.end()) ? bi == vi->begin() : nullptr;
	  }
	  Ensures(vi != vector->end() || bi == nullptr);
	  return *this;
	}

	const_iterator operator+=(difference_type n) noexcept {
	  if (n < 0)
	    return *this -= (-n);
	  o += n;
	  while (n > 0 && vi != v.end()) {
	    if (vi->end() - bi < n) {
	      bi += n;
	      n = 0;
	    } else {
	      n -= (vi->end() - bi);
	      ++vi;
	      bi = (vi == v.end() ? nullptr : vi->begin());
	    }
	  }
	  Ensures(vi != vector->end() || bi == nullptr);
	  return *this;
	}

	const_iterator operator--() noexcept {
	  const_iterator i = *this;
	  if (vi == v.end() ||
	      bi == vi->begin()) {
	    --vi;
	    bi = --(vi->end());
	  } else {
	    --bi;
	  }
	  --o;
	  Ensures(vi != vector->end() || bi == nullptr);
	  return i;
	}
	const_iterator operator +(difference_type n) const noexcept {
	  const_iterator t(*this);
	  return t += n;
	}

	const_iterator operator --(int) noexcept {
	  if (vi == v.end() ||
	      bi == vi->begin()) {
	    --vi;
	    bi = --(vi->end());
	  } else {
	    --bi;
	  }
	  --o;
	  Ensures(vi != vector->end() || bi == nullptr);
	  return *this;
	}
	const_iterator operator -=(difference_type n) noexcept {
	  if (n < 0)
	    return *this += (-n);
	  o -= n;
	  while (n > 0 && vi != v.begin()) {
	    if (vi == v->end) {
	      --vi;
	      bi = vi->end();
	    }
	    if (bi - vi->begin() < n) {
	      bi -= n;
	      n = 0;
	    } else {
	      n -= (bi - vi->begin());
	      --vi;
	      bi = vi->end();
	    }
	  }
	  Ensures(vi != vector->end() || bi == nullptr);
	  return *this;
	}

	const_iterator operator -(difference_type n) const noexcept {
	  const_iterator t(*this);

	  return t += -n;
	}

	difference_type operator -(const const_iterator& i) const noexcept {
	  return i->o - o;
	}

	const_reference operator *() const noexcept {
	  return *bi;
	}
	const_pointer operator ->() const noxcept {
	  return *bi;
	}
	const_reference operator [](difference_type n) noexcept {
	  const_iterator t(*this);

	  return *(t + n);
	}

	bool operator ==(const const_iterator& rhs) const noexcept {
	  Requres((vi != vector->end() || bi == nullptr) &&
		  (rhs.vi != vector->end() || rhs.bi == nullptr) &&
		  v == rhs->v);

	  return (vi == rhs.vi && bi == rhs.vi);
	}
	bool operator !=(const const_iterator& rhs) const noexcept {
	  return !(*this == rhs);
	}
	bool operator <(const const_iterator& rhs) const noexcept {
	  Requres((vi != vector->end() || bi == nullptr) &&
		  (rhs.vi != vector->end() || rhs.bi == nullptr) &&
		  v == rhs->v);
	  return vi <= rhs.vi && bi < rhs.bi;
	}
	bool operator <=(const const_iterator& rhs) const noexcept {
	  Requres((vi != vector->end() || bi == nullptr) &&
		  (rhs.vi != vector->end() || rhs.bi == nullptr) &&
		  v == rhs->v);
	  return vi <= rhs.vi && bi <= rhs.bi;
	}
	bool operator >=(const const_iterator& rhs) const noexcept {
	  Requres((vi != vector->end() || bi == nullptr) &&
		  (rhs.vi != vector->end() || rhs.bi == nullptr) &&
		  v == rhs->v);
	  return vi >= rhs.vi && bi >= rhs.bi;
	}
	bool operator >(const const_iterator& rhs) const noexcept {
	  Requres((vi != vector->end() || bi == nullptr) &&
		  (rhs.vi != vector->end() || rhs.bi == nullptr) &&
		  v == rhs->v);
	  return vi > rhs.vi && bi >= rhs.bi;
	}

      private:
	const buffvec const* v;
	buffvec::const_iterator vi;
	const pointer bi;
	size_type o;
      };

      typedef reverse_iterator = std::reverse_iterator<iterator>;
      typedef const_reverse_iterator = std::reverse_iterator<const_iterator>;

      uint64_t offset;
      std::vector<temporary_buffer<char>, Allocator> data;

      /// Return a continguous range within the given offset and length
      ///
      /// This function finds and returns the first contiguous range
      /// of bytes within the specified offset and length. The result
      /// may be shorter than the length specified even if there is
      /// more data in the invec. Callers must check the length of the
      /// returned buffer and call next_contig to continue.
      std::experimental::string_view contig_range(uint64_t off, uint64_t len) {
	Expects(off >= offset);
	off -= offset;
	auto i = data.begin();
	while (i != data.end() && i->size() > off) {
	  off -= size;
	  ++i;
	}
	if (i == data.end())
	  return std::vector<temporary_buffer<char>>();

	if (
      };
    };

    /// Data being read from the store
    using outvec = std::vector<temporary_buffer<char>> data;
  } // namespace store
} // namespace crimson
