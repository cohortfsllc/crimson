// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

// Crimson: a prototype high performance OSD

// Copyright (C) 2015 Adam C. Emerson <aemerson@redhat.com
//
// Based on MemStore work by Casey Bodley <cbodley@redhat.com>
// and the Ceph object store which is Copyright (C) 2004-2006
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

/// \file PageSet.h
/// \brief In memory page store
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#ifndef CRIMSON_STORE_MEM_PAGESET_H
#define CRIMSON_STORE_MEM_PAGESET_H

#include <limits>
#include <map>
#include <utility>

#include <boost/iterator/counting_iterator.hpp>
#include <boost/range.hpp>

#include "store/Iovec.h"

namespace crimson {
  namespace store {
    namespace mem {
      namespace _ {
	constexpr bool is_power_of_2(const uintmax_t n) {
	  return ((n != 0) && !(n & (n - 1)));
	}

	/// Size of a page (64 KiB)
	static constexpr std::size_t page_size_log2 = 16;
	static constexpr std::size_t page_size = 1 << page_size_log2;

	/// Number of continuous pages in a slice
	static constexpr std::size_t page_slice_len = 16;

	/// A single page
	///
	/// Support copy-on-write by being non-intrusive. We can use the
	/// use-count to decide whether we need to copy the page or
	/// whether we can just modify it.
	using Page = std::array<char, page_size>;

	/// A reference to a page
	///
	/// At least, hwo we store them in trees and otherwise hang on
	/// to them when not operating on them immediately.
	using PageRef = lw_shared_ptr<const Page>;

	/// A set of pages that knows it's part of a whole
	///
	/// In order to minimize the amount of messaging between CPU
	/// cores, this class knows about striping and iterates over
	/// pages sparsely.
	class PageSetSlice {
	  static_assert(is_power_of_2(page_size),
			"Page size must be a power of 2.");
	  static_assert(is_power_of_2(page_slice_len),
			"Slice length must be a power of 2.");
	private:
	  /// Slice ID to determine which pages we store
	  const std::size_t slice;
	  /// Total number of slices in the page set
	  const std::size_t total_slices;

	  using PageSet = std::map<std::size_t, PageRef>;
	  using iterator = typename PageSet::iterator;
	  using const_iterator = typename PageSet::const_iterator;
	  using range = typename boost::sub_range<PageSet>;

	  /// Our container for holding pages
	  ///
	  /// Use a std::map since we need to traverse ranges.
	  PageSet pages;

	  /// If a given offset even belongs to us
	  bool in_this_slice(Offset offset) const noexcept {
	    return (offset / (page_size * page_slice_len) % total_slices)
	      == slice;
	  }

	  /// Index of page containing denomianted byte
	  static std::size_t page_index(Offset offset) noexcept {
	    return offset >> page_size_log2;
	  }
	  /// Offset of first byte of given page
	  static Offset page_offset(std::size_t index) noexcept {
	    return index << page_size_log2;
	  }

	  /// Convert an object Offset into an offset relative to the
	  /// current page
	  static std::size_t page_relative(Offset offset) noexcept {
	    return offset & ~(page_size - 1);
	  }

	  /// Returns true if offset is page-aligned
	  static bool page_aligned(Offset offset) noexcept {
	    return page_relative(offset) == 0;
	  }

	  /// Return a non-const reference to a page
	  ///
	  /// This function *must* be used to get a non-const page
	  /// reference. It copies-on-write, when necessary.
	  Page& writable(iterator i) {
	    if (i->second.use_count() > 1) {
	      auto p = make_lw_shared<Page>(*i->second);
	      i->second = std::move(p);
	    }
	    return const_cast<Page&>(*i->second);
	  }

	public:
	  /// Tell us who we are
	  ///
	  /// @param[in] _slice        Which slice (within the PageSet) we are.
	  /// @param[in] _total_slices How many there are
	  PageSetSlice(uint32_t _slice, const uint32_t _total_slices) noexcept
	    : slice(_slice), total_slices(_total_slices) {}
	  ~PageSetSlice() = default;

	  PageSetSlice(const PageSetSlice&) = delete;
	  PageSetSlice(PageSetSlice&&) = delete;

	  PageSetSlice& operator =(const PageSetSlice&) = delete;
	  PageSetSlice& operator =(PageSetSlice&&) = delete;

	  /// Delete all pages within a given range
	  void hole_punch(const Range range) noexcept;

	  /// Write pages
	  ///
	  /// Write specified data to pages
	  void write(const IovecRef& data) noexcept;

	  /// Read all data in the given range
	  auto read(const Range range) const noexcept
	    -> foreign_ptr<std::unique_ptr<Iovec>>;
	};

	class PageSet {
	private:
	  std::vector<foreign_ptr<std::unique_ptr<PageSetSlice>>>  partitions;


	public:
	  /// Constructor
	  PageSet(std::size_t count = smp::count)
	    : partitions(count) {}

	  // copy disabled
	  PageSet(const PageSet&) = delete;
	  PageSet& operator =(const PageSet&) = delete;

	  // move disabled
	  PageSet(PageSet&&) = delete;
	  PageSet& operator =(PageSet&&) = delete;

	  /// Is this CPU the one I'm running on?
	  static bool local(unsigned cpu) {
	    return engine().cpu_id() == cpu;
	  }

	  // What CPU should a given slice run on?
	  unsigned slice_cpu(std::size_t slice) const {
	    return slice  % smp::count;
	  }

	  future<> write(IovecRef&& iov) noexcept;
	  future<> hole_punch(const Range& range) noexcept;
	  future<IovecRef> read(const Range& range) const noexcept;
	};
      } // namespace _
    } // namespace mem
  } // namespace store
} // namespace crimson

#endif // CRIMSON_STORE_PAGESET_H
