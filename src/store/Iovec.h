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
      using pointer = typename std::allocator_traits<Allocator>::pointer;
      using const_pointer = typename std::allocator_traits<Allocator>
	::const_pointer;

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
	iterator(not_null<buffvec*> _v, typename buffvec::iterator _vi,
		 pointer _bi, size_type _o) noexcept
	  : v(_v), vi(_vi), bi(_bi), o(_o) {
	  Requres(vi != v->end() || bi == nullptr);
	};
	iterator operator++() noexcept {
	  iterator i = *this;
	  ++bi;
	  ++o;
	  if (bi == vi->end()) {
	    ++vi;
	    bi = (vi != v.end()) ? bi == vi->begin() : nullptr;
	  }
	  Ensures(vi != v->end() || bi == nullptr);
	  return i;
	}
	iterator operator++(int) noexcept {
	  ++bi;
	  ++o;
	  if (bi == vi->end()) {
	    ++vi;
	    bi = (vi != v.end()) ? bi == vi->begin() : nullptr;
	  }
	  Ensures(vi != v->end() || bi == nullptr);
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
	  Ensures(vi != v->end() || bi == nullptr);
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
	  Ensures(vi != v->end() || bi == nullptr);
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
	  Ensures(vi != v->end() || bi == nullptr);
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
	  Ensures(vi != v->end() || bi == nullptr);
	  return *this;
	}

	iterator operator -(difference_type n) const noexcept {
	  iterator t(*this);

	  return t += -n;
	}

	difference_type operator -(const iterator& i) const noexcept {
	  return i->o - o;
	}

	pointer operator ->() const noexcept {
	  return *bi;
	}
	reference operator *() const noexcept {
	  return *bi;
	}
	reference operator [](difference_type n) noexcept {
	  iterator t(*this);

	  return *(t + n);
	}

	bool operator ==(const iterator& rhs) const noexcept {
	  Requres((vi != v->end() || bi == nullptr) &&
		  (rhs.vi != v->end() || rhs.bi == nullptr) &&
		  v == rhs->v);

	  return (vi == rhs.vi && bi == rhs.vi);
	}
	bool operator !=(const iterator& rhs) const noexcept {
	  return !(*this == rhs);
	}
	bool operator <(const iterator& rhs) const noexcept {
	  Requres((vi != v->end() || bi == nullptr) &&
		  (rhs.vi != v->end() || rhs.bi == nullptr) &&
		  v == rhs->v);
	  return vi <= rhs.vi && bi < rhs.bi;
	}
	bool operator <=(const iterator& rhs) const noexcept {
	  Requres((vi != v->end() || bi == nullptr) &&
		  (rhs.vi != v->end() || rhs.bi == nullptr) &&
		  v == rhs->v);
	  return vi <= rhs.vi && bi <= rhs.bi;
	}
	bool operator >=(const iterator& rhs) const noexcept {
	  Requres((vi != v->end() || bi == nullptr) &&
		  (rhs.vi != v->end() || rhs.bi == nullptr) &&
		  v == rhs->v);
	  return vi >= rhs.vi && bi >= rhs.bi;
	}
	bool operator >(const iterator& rhs) const noexcept {
	  Requres((vi != v->end() || bi == nullptr) &&
		  (rhs.vi != v->end() || rhs.bi == nullptr) &&
		  v == rhs->v);
	  return vi > rhs.vi && bi >= rhs.bi;
	}

      private:
	buffvec const* v;
	typename buffvec::iterator vi;
	pointer bi;
	size_type o;
      };

      /// Character-by-character iterator, const version
      class const_iterator : public std::iterator<
	std::random_access_iterator_tag, char> {
      public:
	const_iterator() noexcept
	  : v(nullptr), bi(nullptr) { };
	const_iterator(not_null<const buffvec*> _v,
		       typename buffvec::const_iterator _vi,
		       const_pointer _bi, size_type _o) noexcept
	  : v(_v), vi(_vi), bi(_bi), o(_o) {
	  Requres(vi != v->end() || bi == nullptr);
	};
	const_iterator operator++() noexcept {
	  const_iterator i = *this;
	  ++bi;
	  ++o;
	  if (bi == vi->end()) {
	    ++vi;
	    bi = (vi != v.end()) ? bi == vi->begin() : nullptr;
	  }
	  Ensures(vi != v->end() || bi == nullptr);
	  return i;
	}
	const_iterator operator++(int) noexcept {
	  ++bi;
	  ++o;
	  if (bi == vi->end()) {
	    ++vi;
	    bi = (vi != v.end()) ? bi == vi->begin() : nullptr;
	  }
	  Ensures(vi != v->end() || bi == nullptr);
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
	  Ensures(vi != v->end() || bi == nullptr);
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
	  Ensures(vi != v->end() || bi == nullptr);
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
	  Ensures(vi != v->end() || bi == nullptr);
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
	  Ensures(vi != v->end() || bi == nullptr);
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
	inline const_pointer operator ->() const noexcept {
	  return *bi;
	}

	const_reference operator [](difference_type n) noexcept {
	  const_iterator t(*this);

	  return *(t + n);
	}

	bool operator ==(const const_iterator& rhs) const noexcept {
	  Requres((vi != v->end() || bi == nullptr) &&
		  (rhs.vi != v->end() || rhs.bi == nullptr) &&
		  v == rhs->v);

	  return (vi == rhs.vi && bi == rhs.vi);
	}
	bool operator !=(const const_iterator& rhs) const noexcept {
	  return !(*this == rhs);
	}
	bool operator <(const const_iterator& rhs) const noexcept {
	  Requres((vi != v->end() || bi == nullptr) &&
		  (rhs.vi != v->end() || rhs.bi == nullptr) &&
		  v == rhs->v);
	  return vi <= rhs.vi && bi < rhs.bi;
	}
	bool operator <=(const const_iterator& rhs) const noexcept {
	  Requres((vi != v->end() || bi == nullptr) &&
		  (rhs.vi != v->end() || rhs.bi == nullptr) &&
		  v == rhs->v);
	  return vi <= rhs.vi && bi <= rhs.bi;
	}
	bool operator >=(const const_iterator& rhs) const noexcept {
	  Requres((vi != v->end() || bi == nullptr) &&
		  (rhs.vi != v->end() || rhs.bi == nullptr) &&
		  v == rhs->v);
	  return vi >= rhs.vi && bi >= rhs.bi;
	}
	bool operator >(const const_iterator& rhs) const noexcept {
	  Requres((vi != v->end() || bi == nullptr) &&
		  (rhs.vi != v->end() || rhs.bi == nullptr) &&
		  v == rhs->v);
	  return vi > rhs.vi && bi >= rhs.bi;
	}

      private:
	const buffvec* const v;
	typename buffvec::const_iterator vi;
	const_pointer bi;
	size_type o;
      };

      using reverse_iterator = std::reverse_iterator<iterator>;
      using const_reverse_iterator = std::reverse_iterator<const_iterator>;

      /// Continguous buffer iterator
      ///
      /// Iterate over our buffers.
      class contig_iterator : public std::iterator<
	std::bidirectional_iterator_tag, GSL::string_span> {
      public:
	contig_iterator() noexcept
	  : v(nullptr) { };
	contig_iterator(not_null<buffvec*> _v, buffvec::iterator _vi) noexcept
	  : v(_v), vi(_vi) {};
	contig_iterator operator++(int) noexcept {
	  ++vi;
	  return *this;
	}

	iterator operator++() noexcept {
	  iterator i = *this;
	  ++vi;
	  return i;
	}
	iterator operator --(int) noexcept {
	  --vi;
	  return *this;
	}
	iterator operator--() noexcept {
	  iterator i = *this;
	  --vi;
	  return i;
	}

	pointer operator ->() const noexcept {
	  return GSL::string_span(bi->get_write(), bi->length());
	}
	reference operator *() const noexcept {
	  return GSL::string_span(bi->get_write(), bi->length());
	}

	bool operator ==(const iterator& rhs) const noexcept {
	  Requres(v == rhs->v);
	  return vi == rhs.vi;
	}
	bool operator !=(const iterator& rhs) const noexcept {
	  Requres(v == rhs->v);
	  return vi != rhs.vi;
	}
	bool operator <(const iterator& rhs) const noexcept {
	  Requres(v == rhs->v);
	  return vi < rhs.vi;
	}
	bool operator <=(const iterator& rhs) const noexcept {
	  Requres(v == rhs->v);
	  return vi <= rhs.vi;
	}
	bool operator >=(const iterator& rhs) const noexcept {
	  Requres(v == rhs->v);
	  return vi >= rhs.vi;
	}
	bool operator >(const iterator& rhs) const noexcept {
	  Requres(v == rhs->v);
	  return vi > rhs.vi;
	}

      private:
	buffvec const* v;
	typename buffvec::iterator vi;
      };
      /// Continguous buffer iterator
      ///
      /// Iterate over our buffers.
      class const_contig_iterator : public std::iterator<
	std::bidirectional_iterator_tag, const GSL::string_span> {
      public:
	contig_iterator() noexcept
	  : v(nullptr) { };
	contig_iterator(not_null<buffvec*> _v, buffvec::iterator _vi) noexcept
	  : v(_v), vi(_vi) {};
	contig_iterator operator++(int) noexcept {
	  ++vi;
	  return *this;
	}

	iterator operator++() noexcept {
	  iterator i = *this;
	  ++vi;
	  return i;
	}
	iterator operator --(int) noexcept {
	  --vi;
	  return *this;
	}
	iterator operator--() noexcept {
	  iterator i = *this;
	  --vi;
	  return i;
	}

	const pointer operator ->() const noexcept {
	  return GSL::string_span(bi->get_write(), bi->length());
	}
	value_type operator *() const noexcept {
	  return GSL::string_span(bi->get_write(), bi->length());
	}

	bool operator ==(const iterator& rhs) const noexcept {
	  Requres(v == rhs->v);
	  return vi == rhs.vi;
	}
	bool operator !=(const iterator& rhs) const noexcept {
	  Requres(v == rhs->v);
	  return vi != rhs.vi;
	}
	bool operator <(const iterator& rhs) const noexcept {
	  Requres(v == rhs->v);
	  return vi < rhs.vi;
	}
	bool operator <=(const iterator& rhs) const noexcept {
	  Requres(v == rhs->v);
	  return vi <= rhs.vi;
	}
	bool operator >=(const iterator& rhs) const noexcept {
	  Requres(v == rhs->v);
	  return vi >= rhs.vi;
	}
	bool operator >(const iterator& rhs) const noexcept {
	  Requres(v == rhs->v);
	  return vi > rhs.vi;
	}

      private:
	const buffvec const* v;
	typename buffvec::const_iterator vi;
      };

      using reverse_contig_iterator = std::reverse_iterator<contig_iterator>;
      using reverse_const_reverse_iterator = std::reverse_iterator<
	const_contig_iterator>;

      uint64_t offset;
      std::vector<temporary_buffer<char>, Allocator> data;
    };

    /// Data being read from the store
    using outvec = std::vector<temporary_buffer<char>>;
  } // namespace store
} // namespace crimson
