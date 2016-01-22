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

/// \file PageSet.cc
/// \brief In memory page store, implementation
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#include "PageSet.h"

#include "PageSet.h"
#include "store/Iovec.h"

namespace crimson {
  namespace store {
    namespace mem {
      namespace _ {
	void PageSetSlice::hole_punch(const Range range) noexcept {
	  if (pages.empty())
	    return;

	  auto begin = pages.lower_bound(page_index(range.offset));
	  if (begin == pages.end())
	    return;
	  if (!page_aligned(range.offset) &&
	      begin->first == page_relative(range.offset)) {
	    auto& p = writable(begin);
	    std::fill(p.begin() + page_relative(range.offset), p.end(), 0);
	    ++begin;
	  }
	  auto end = pages.lower_bound(page_index(range.offset +
						  range.length));
	  if (!page_aligned(range.offset + range.length) &&
	      end != pages.end() &&
	      end->first == page_relative(range.offset + range.length)) {
	    auto& p = writable(end);
	    std::fill(p.begin(), p.begin() + page_relative(range.offset +
							   range.length),
		      0);
	  }
	  pages.erase(begin, end);
	}

	void PageSetSlice::write(const IovecRef& data) noexcept {
	  auto piter = pages.begin();

	  for (auto io : data->stripe(total_slices, slice,
				      page_size * page_slice_len)) {
	    auto biter = io.second.begin();
	    while (biter != io.second.end()) {
	      auto offset = io.first + (io.second.end() - biter);
	      // If we're not already there
	      if (piter == pages.end() || piter->first != page_index(offset)) {
		// An optimization for writing consecutive pages
		if (piter != pages.end() &&
		    piter->first + 1 == page_index(offset))
		  ++piter;
		else
		  // Search for it
		  piter = pages.lower_bound(page_index(offset));
		// If it doesn't exist
		if (piter == pages.end() ||
		    piter->first > page_index(offset)) {
		  // Make it
		  pages.emplace_hint(
		    piter, std::piecewise_construct,
		    std::forward_as_tuple(page_index(offset)),
		    std::forward_as_tuple(make_lw_shared<Page>()));
		}
	      }
	      /// Get a writable reference to the page.
	      auto& p = writable(piter);
	      auto o = page_relative(offset);
	      auto l = std::min(static_cast<size_t>(io.second.end() - biter),
				page_size - o);
	      std::copy_n(biter, l, p.begin() + o);
	      biter += l;
	    }
	  }
	}

	auto PageSetSlice::read(const Range range) const noexcept
	  -> foreign_ptr<std::unique_ptr<Iovec>> {
	  // Our caller will package this up in a future
	  auto iov = make_foreign(std::make_unique<Iovec>());
	  auto begin = pages.lower_bound(page_index(range.offset));
	  auto end = pages.lower_bound(page_index(range.offset +
						  range.length) - 1);
	  if (begin == pages.end())
	    return iov;

	  // Boundary at the start
	  if (!page_aligned(range.offset)) {
	    auto o = page_relative(range.offset);
	    if (end == begin) {
	      iov->emplace(
		range.offset, begin->second->data() + o, range.length,
		make_object_deleter(PageRef(begin->second)));
	      return iov;
	    } else {
	      iov->emplace(
		range.offset, begin->second->data() + o, page_size - o,
		make_object_deleter(PageRef(begin->second)));
	    }
	    ++begin;
	    if (begin == pages.end())
	      return iov;
	  }
	  auto unaligned_end =
	    !page_aligned(range.offset + range.length)
	 && end != pages.end()
	 && page_index(range.offset + range.length - 1) == end->first
	    ? page_relative(range.offset + range.length) : 0;
	  if (unaligned_end) {
	    if (begin == end) {
	      iov->emplace(
		begin->first, begin->second->data(), unaligned_end,
		make_object_deleter(PageRef(begin->second)));
	      return iov;
	    } else {
	      --end;
	    }
	  }
	  std::for_each(begin, end,
			[&iov](auto& kv) {
			  iov->emplace(
			    kv.first, kv.second->data(), page_size,
			    make_object_deleter(PageRef(kv.second)));
			});
	  if (unaligned_end) {
	    iov->emplace(end->first, end->second->data(),
			 unaligned_end,
			 make_object_deleter(PageRef(begin->second)));

	  }
	  return iov;
	}

	/// Write to every slice in the pageset
	///
	/// This returns a future that will be ready when all writes
	/// have completed.
	future<> PageSet::write(IovecRef&& iov) noexcept {
	  return do_with(std::move(iov), [this](const IovecRef& iov) {
	      return parallel_for_each(
		boost::make_counting_iterator(0ul),
		boost::make_counting_iterator(partitions.size()),
		[this, &iov](std::size_t slice) {
		  auto cpu = slice_cpu(slice);
		  if (local(cpu)) {
		    partitions[slice]->write(iov);
		    return make_ready_future<>();
		  } else {
		    return smp::submit_to(
		      cpu,
		      [&iov,
		       &slice = partitions[slice]]() {
			slice->write(iov);
		      });
		  }
		});
	    });
	}

	future<> PageSet::hole_pounch(Range range) noexcept {
	  return parallel_for_each(
	    boost::make_counting_iterator(0ul),
	    boost::make_counting_iterator(partitions.size()),
	    [this, range](std::size_t slice) {
	      auto cpu = slice_cpu(slice);
	      if (local(cpu)) {
		partitions[slice]->hole_punch(range);
		return make_ready_future<>();
	      } else {
		return smp::submit_to(
		  cpu,
		  [range,
		   &slice = partitions[slice]]() {
		    slice->hole_punch(range);
		  });
	      }
	    });
	}

	future<IovecRef> PageSet::read(Range range) const noexcept {
	  return map_reduce(
	    boost::make_counting_iterator(0ul),
	    boost::make_counting_iterator(partitions.size()),
	    [this, range](std::size_t slice) mutable {
	      auto cpu = slice_cpu(slice);
	      if (local(cpu)) {
		return make_ready_future<foreign_ptr<std::unique_ptr<Iovec>>> (
		  partitions[slice]->read(range));
	      } else {
		return smp::submit_to(
		  cpu,
		  [range, &slice = *(partitions[slice])]() {
		    return slice.read(range);
		  });
	      }
	    }, std::make_unique<Iovec>(),
	    [](std::unique_ptr<Iovec> i,
	       foreign_ptr<std::unique_ptr<Iovec>> v) {
	      i->merge(*v);
	      return i;
	    }).then([](std::unique_ptr<Iovec> p) {
		return make_foreign(std::unique_ptr<const Iovec>(
				      p.release())); });
	}
      } // namespace _
    } // namespace mem
  } // namespace store
} // namespace crimson
