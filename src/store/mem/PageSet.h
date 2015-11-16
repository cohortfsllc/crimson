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

/// \file Mem.h
/// \brief Fast, in-memory object store
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#ifndef CRIMSON_STORE_MEM_H
#define CRIMSON_STORE_MEM_H

#include <utility>
#include <vector>

namespace crimson {
  /// Storage interface
  namespace store {
    namspace _mem {
      template<size_t PageSize>
	class Page {
	std::aligned_storage_t[PageSize];
	boost::intrusive::avl_set_member_hook<> hook;
	uint64_t offset;

	// key-value comparison functor
	struct Less {
	  bool operator()(uint64_t offset, const Page<PageSize> &page) const {
	    return offset < page.offset;
	  }
	  bool operator()(const Page<PageSize> &page, uint64_t offset) const {
	    return page.offset < offset;
	  }
	  bool operator()(const Page<PageSize> &lhs,
			  const Page<PageSize> &rhs) const {
	    return lhs.offset < rhs.offset;
	  }
	};
      public:
	Page(uint64_t offset = 0) : offset(offset) {}

	Page(const Page&) = delete;
	Page& operator=(const Page&) = delete;

	Page(Page&&) = delete;
	Page& operator=(Page&&) = delete;
      };

      template<size_t PageSize>
      class PageSet {
      public:
	using page_type = Page<PageSize>;

      private:
	// Does intrusive make sense in this context?
	// store pages in a boost intrusive avl_set
	using page_cmp = typename page_type::Less;
	using member_option = boost::intrusive::member_hook<
	  page_type, boost::intrusive::avl_set_member_hook<>,
	  &page_type::hook>;
	page_set = boost::intrusive::avl_set<
	  page_type, boost::intrusive::compare<page_cmp>, member_option>;

	using iterator = typename page_set::iterator;

	page_set pages;

	void free_pages(iterator cur, iterator end) {
	  while (cur != end) {
	    page_type *page = &*cur;
	    cur = pages.erase(cur);
	    // Better deletion
	    delete page;
	  }
	}

      public:
	PageSet() {}
	~PageSet() {
	  free_pages(pages.begin(), pages.end());
	}

	PageSet(const PageSet&) = delete;
	PageSet(PageSet&&) = delete;

	PageSet& PageSet(const PageSet&) = delete;
	PageSet& PageSet(PageSet&&) = delete;

	bool empty() const { return pages.empty(); }
	size_t size() const { return pages.size(); }

	// allocate all pages that intersect the range [offset,length)
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

	void free_pages_after(uint64_t offset) {
	  auto cur = pages.lower_bound(offset & ~(PageSize-1), page_cmp());
	  if (cur == pages.end())
	    return;
	  if (cur->offset < offset)
	    cur++;
	  free_pages(cur, pages.end());
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

	// return all allocated pages that intersect the range [offset,length)
	page_vector get_range(uint64_t offset, size_t length) {
	  const size_t stripe_unit = PageSize * pages_per_stripe;
	  auto part = partitions.begin() + (offset / stripe_unit)
	    % partitions.size();

	  while (length) {
	    // number of pages left in this stripe
	    size_t count = pages_per_stripe - (offset / PageSize)
	      % pages_per_stripe;
	    // ending offset of this stripe
	    uint64_t stripe_end = (offset & ~(PageSize-1)) + PageSize * count;
	    // bytes remaining in this stripe
	    uint64_t stripe_len = stripe_end - offset;

	    if (stripe_len > length)
	      stripe_len = length;

	    part->get_range(offset, stripe_len, range);

	    offset += stripe_len;
	    length -= stripe_len;

	    if (++part == partitions.end()) // next partition
	      part = partitions.begin();
	  }
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
