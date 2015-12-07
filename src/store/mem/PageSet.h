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

#include <boost/range.hpp>

#include "../Store.h"

namespace crimson {
  namespace store {
    namspace _mem {
      constexpr bool is_power_of_2(const uintmax_t n) {
	return ((n != 0) && !(n & (n - 1)));
      }

      /// Size of a page (64 KiB)
      static constexpr size_t page_size = 64 << 10;

      /// Number of continuous pages in a slice
      static constexpr size_t page_slice_len = 16;

      /// A single page
      ///
      /// Support copy-on-write by being non-intrusive. We can use the
      /// use-count to decide whether we need to copy the page or
      /// whether we can just modify it.
      using Page = std::array<page_size, char>;

     /// A reference to a page
      ///
      /// At least, hwo we store them in trees and otherwise hang on
      /// to them when not operating on them immediately.
      using PageRef = lw_shared_ptr<Page>;

      /// A set of pages that knows it's part of a whole
      ///
      /// In order to minimize the amount of messaging between CPU
      /// cores, this class knows about striping and iterates over
      /// pages sparsely.
      class PageSetSlice {
	static_assert(is_power_of_2(page_size));
	static_assert(is_power_of_2(page_slice_len));
      private:
	/// Slice ID to determine which pages we store
	const uint32_t slice;
	/// Total number of slices in the page set
	const uint32_t total_slices;

	using PageSet = std::map<uint64_t, PageRef>;
	using iterator = typename PageSet::iterator;
	using const_iterator = typename PageSet::const_iterator;
	using range = typename boost::sub_range<PageSet>;

	/// Our container for holding pages
	///
	/// Use a std::map since we need to traverse ranges.
	PageSet pages;

	/// If a given offset even belongs to us
	bool in_this_slice(uint64_t offset) const noexcept {
	  return (offset / (page_size * page_slice_len) % total_slices)
	    == slice;
	}

	/// Offset of page containing denomianted byte
	static uint64_t page_offset(uint64_t offset) const noexcept {
	  return offset & ~(page_size - 1);
	}

	/// Copy a page, if in use
	///
	/// This function /must/ be called before mutating any
	/// page. It takes the iterator rather than the reference so
	/// we can update the map.
	void copy_on_write(iterator i) {
	  if (i->second.use_count > 1) {
	    auto p = make_lw_shared<Page>();
	    boost::copy(*(i->second), p->begin());
	    i->second = std::move(p);
	  }
	}
      public:
	/// Tell us who we are
	///
	/// @param[in] _slice        Which slice (within the PageSet) we are.
	/// @param[in] _total_slices How many there are
	PageSetSlice(uint32_t _slice, const uint32_t _total_slices) noexcept
	  : slice(_slice), total_slices(_total_slices) {}
	~PageSetSlice() = default;

	PageSetSlice(const PageSet&) = delete;
	PageSetSlice(PageSet&&) = delete;

	PageSetSlice& PageSet(const PageSet&) = delete;
	PageSetSlice& PageSet(PageSet&&) = delete;

	/// Trucate a pageset
	///
	/// Delete all pages after a given offset
	void truncate(uint64_t offset) noexcept {
	  auto cur = pages.lower_bound(page_offset(offset));
	  if (cur == pages.end())
	    return;
	  if (cur->offset < offset) {
	    // We could, potentially, zero-fill the point after offset
	    // on a page, but that would invoke COW to now
	    // avail. Since the length of our object is known we
	    // shouldn't read beyond the end.
	    ++cur;
	  }
	  erase(cur, pages.end);
	}

	//
	void alloc_range(uint64_t offset, size_t length) {
	  // loop in reverse so we can provide hints to
	  //	avl_set::insert_check() and get O(1) insertions after
	  //	the first
	  uint64_t position = offset + length - 1;

	  iterator cur = pages.end();
	  while (length) {
	    const uint64_t page_offset = position & ~(PageSize-1);

	    page_set::insert_commit_data commit;
	    auto insert = pages.insert_check(cur, page_offset, page_cmp(),
					     commit);
	    if (insert.second) {
	      page_type *page = new page_type(page_offset);
	      cur = pages.insert_commit(*page, commit);

	      /* XXX Dont zero-fill pages AOT, rather find holes and
	       * expand them when read.  Just avoiding the fills isn't
	       * enough, but it increased throughput by 100MB/s.  And
	       * it's enough for simple benchmarks that only read
	       * after write.  */

	      // zero end of page past offset + length
	      if (offset + length < page->offset + PageSize)
		std::fill(page->data + offset + length - page->offset,
			  page->data + PageSize, 0);
	      // zero front of page between page_offset and offset
	      if (offset > page->offset)
		std::fill(page->data, page->data + offset - page->offset, 0);

	    } else { // exists
	      cur = insert.first;
	    }
	    int c = std::min(length, (position & (PageSize-1)) + 1);
	    position -= c;
	    length -= c;
	  }
	}

	range page_range(uint64_t offset, uint64_t length) {
	  if (length == 0)
	    return range(pages.end(), pages.end());
	  auto i1 = pages.lower_bound(offset);
	  if (std::numeric_limits<uint64_t>::max() - length <= offset)
	    return range(i1, pages.end());
	  const uint64_t first_page_offset = offset & ~(PageSize - 1);
	  uint64_t last_page_offset = offset + length + 1 & ~(PageSize - 1);
	  if (last_page_offset == first_page_offset)
	    ++last_page_offset;
	  return range(i1, pages.upper_bound(last_past_offset));
	}

	const_range page_range(uint64_t offset, uint64_t length) const {
	  if (length == 0)
	    return const_range(pages.end(), pages.end());
	  auto i1 = pages.lower_bound(offset);
	  if (std::numeric_limits<uint64_t>::max() - length <= offset)
	    return const_range(i1, pages.end());
	  const uint64_t first_page_offset = offset & ~(PageSize - 1);
	  uint64_t last_page_offset = offset + length + 1 & ~(PageSize - 1);
	  if (last_page_offset == first_page_offset)
	    ++last_page_offset;
	  return const_range(i1, pages.upper_bound(last_past_offset));
	}
      };

      template<size_t PageSize>
      class PartitionedPageSet {
      private:
	std::vector<foreign_ptr<std::unique_ptr<PageSet<
	  PageSize>>>> partitions;
	size_t pages_per_stripe;

	static size_t count_pages(const uint64_t offset, const uint64_t len) {
	  // count the overlapping pages
	  size_t count = 0;
	  if (offset % PageSize) {
	    count++;
	    size_t rem = PageSize - offset % PageSize;
	    len = len <= rem ? 0 : len - rem;
	  }
	  count += len / PageSize;
	  if (len % PageSize)
	    count++;
	  return count;
	}

      public:
	PartitionedPageSet(size_t count, size_t pages_per_stripe)
	  : partitions(count), pages_per_stripe(pages_per_stripe)
	  {}

	// copy disabled
	PartitionedPageSet(const PartitionedPageSet&) = delete;
	PartitionedPageSet& operator=(const PartitionedPageSet&) = delete;

	// move disabled
	PartitionedPageSet(PartitionedPageSet&&) = delete;
	PartitionedPageSet& operator=(PartitionedPageSet&&) = delete;


	// allocate all pages that intersect the range [offset,length)
	void alloc_range(uint64_t offset, size_t length) {
	  const size_t page_count = count_pages(offset, length);

	  const size_t stripe_unit = PageSize * pages_per_stripe;
	  auto part = partitions.begin() + (offset / stripe_unit) %
	    partitions.size();

	  while (length) {
	    // number of pages left in this stripe
	    size_t count = pages_per_stripe
	      - (offset / PageSize) % pages_per_stripe;
	    // ending offset of this stripe
	    uint64_t stripe_end = (offset & ~(PageSize-1)) + PageSize * count;
	    // bytes remaining in this stripe
	    uint64_t stripe_len = stripe_end - offset;

	    if (stripe_len > length) {
	      count -= ((offset % PageSize) + (stripe_len - length))
		/ PageSize;
	      stripe_len = length;
	    }

	    part->alloc_range(offset, stripe_len, p, p + count);

	    offset += stripe_len;
	    length -= stripe_len;

	    if (++part == partitions.end()) // next partition
	      part = partitions.begin();
	    p += count; // advance position in output vector
	  }
	  Ensures(p == range.end());
	}



	void free_pages_after(uint64_t offset) {
	  for (auto &part : partitions)
	    part.free_pages_after(offset);
	}
      };
    } // namespace _mem
  } // namespace store
} // namespace crimson

#endif // CRIMSON_STORE_PAGESET_H
