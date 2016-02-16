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

/// \file mem/Object.cc
/// \brief Fast, in-memory objects and the functions that implement them
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#include <boost/range/algorithm.hpp>

#include "Object.h"

using namespace std::literals;

namespace crimson {
  /// Storage interface
  namespace store {
    /// Memory based object store
    namespace mem {
      namespace _ {
	AsyncMutation::AsyncMutation(Object* o)
	  : object(o) {
	  object->mutations.push_back(*this);
	}

	AsyncMutation::~AsyncMutation() {
	  auto i = object->mutations.iterator_to(*this);
	  auto j = i;
	  ++j;

	  if (j != object->mutations.end() && j->commit &&
	      i == object->mutations.begin())
	    j->p.set_value();

	  object->mutations.erase(i);
	}
      } // namespace _
      void Object::ref() const {
	++ref_cnt;
      }
      void Object::unref() const {
	if (--ref_cnt == 0)
	  delete this;
      }

      future<IovecRef> Object::read(const Range r) const {
	if (!local()) {
	  return smp::submit_to(
	    cpu, [this, r]{ return read(r); });
	}
	if (!in_range(r))
	  return make_exception_future<IovecRef>(
	    std::system_error(errc::out_of_range));
	else
	  return seastar::do_with(
	    boost::intrusive_ptr<const Object>(this),
	    [this, r](boost::intrusive_ptr<const Object>&) mutable {
	      return data.read(r);
	    });
      }

      future<> Object::write(IovecRef iov) {
	if (!local()) {
	  return smp::submit_to(
	    cpu, [this,
		  iov = std::move(iov)] () mutable {
	      return write(std::move(iov));
	    });
	}

	auto i = iov->data.end();
	--i;
	auto writelen = (i->first + i->second.size());
	if (data_len < writelen)
	  data_len = writelen;
	return seastar::do_with(
	  make_unique<_::AsyncMutation>(this),
	  [this, iov = std::move(iov)](unique_ptr<_::AsyncMutation>&) mutable {
	    return data.write(std::move(iov));
	  });
      }

      future<> Object::zero(const Range r) {
	if (!local()) {
	  return smp::submit_to(
	    cpu, [this, r]{ return zero(r); });
	}

	if (data_len < r.offset + r.length)
	  data_len = r.offset + r.length;

	return seastar::do_with(
	  make_unique<_::AsyncMutation>(this),
	  [this, r](unique_ptr<_::AsyncMutation>&) {
	    return data.hole_punch(r);
	  });
      }

      future<> Object::hole_punch(const Range r) {
	if (!local()) {
	  return smp::submit_to(
	    cpu, [this, r]{ return hole_punch(r); });
	}

	if (!in_range(r))
	  return make_exception_future(std::system_error(
					 errc::out_of_range));
	else
	  return seastar::do_with(
	    make_unique<_::AsyncMutation>(this),
	    [this, r](unique_ptr<_::AsyncMutation>&) {
	      return data.hole_punch(r);
	    });
      }

      future<> Object::truncate(const Length l) {
	if (!local()) {
	  return smp::submit_to(
	    cpu, [this, l]{ return truncate(l); });
	}
	if (data_len > l) {
	  data_len = l;
	  return seastar::do_with(
	    make_unique<_::AsyncMutation>(this),
	    [this,
	     r = Range(l, std::numeric_limits<uint64_t>::max() - 1)](
	       unique_ptr<_::AsyncMutation>&) {
	      return data.hole_punch(r);
	    });
	} else {
	  return make_ready_future<>();
	}
      }

      future<> Object::remove() {
	slice.erase(iter);
	return make_ready_future<>();
      }

      future<const_buffer> Object::getattr(attr_ns ns, string attr) const {
	if (!local()) {
	  return smp::submit_to(
	    cpu, [this, ns, attr = std::move(attr)]{
	      return getattr(ns, attr); });
	}

	if (ns >= attr_ns::END)
	  return make_exception_future<const_buffer>(
	    std::system_error(errc::invalid_argument,
			      "Invalid attribute namespace"s));

	auto& m = attarray[(unsigned)ns];
	auto i = m.find(attr);
	if (i == m.end())
	  return make_exception_future<const_buffer>(
	    std::system_error(errc::no_such_attribute_key,
			      "'"s + (std::string)attr +
			      "' could not be found"s));
	return make_ready_future<const_buffer>(
	  make_const_buffer(i->second));
      }

      future<held_span<const_buffer>> Object::getattrs(
	attr_ns ns, held_span<string> attrs) const {
	if (!local()) {
	  smp::submit_to(
	    cpu, [this, ns, attrs = std::move(attrs)] () mutable {
	      return getattrs(ns, std::move(attrs));
	    });
	}
	if (ns >= attr_ns::END)
	  return make_exception_future<held_span<const_buffer>>(
	    std::system_error(errc::invalid_argument,
			      "Invalid attribute namespace"s));

	auto out = std::make_unique<std::vector<const_buffer>>();

	out->reserve(attrs->size());
	auto& m = attarray[(unsigned)ns];

	try {
	  std::transform(
	    attrs->begin(), attrs->end(), out->begin(),
	    [&m](const string& attr) {
	      auto i = m.find(attr);
	      if (i == m.end())
		throw std::system_error(errc::no_such_attribute_key,
					"'"s + (std::string)attr +
					"' could not be found"s);
	      else
		// We're not modifying anything visible to the caller,
		// and we're the only one accessing this.
		return make_const_buffer(
		  i->second);
	    });
	} catch (const std::exception& e) {
	  return make_exception_future<held_span<const_buffer>>(e);
	}
	return make_ready_future<held_span<const_buffer>>(
	  std::move(out));
      }

      future<> Object::setattr(attr_ns ns, string attr,
			       const_buffer val) {
	if (!local()) {
	  return smp::submit_to(
	    cpu, [this, ns,
		  attr = std::move(attr),
		  val = std::move(val)] () mutable {
	      return setattr(ns, attr, std::move(val));
	    });
	}
	if (ns >= attr_ns::END)
	  return make_exception_future<>(
	    std::system_error(errc::invalid_argument,
			      "Invalid attribute namespace"s));

	auto& m = attarray[(unsigned)ns];
	auto i = m.lower_bound(attr);
	if (i->first == attr) {
	  if (i->second.use_count() > 1) {
	    i->second = make_lw_shared<string>(val.get(), val.size());
	  } else {
	    *(i->second) = to_sstring(std::move(val));
	  }
	} else {
	  m.emplace_hint(i, std::piecewise_construct,
			 std::forward_as_tuple(std::move(attr)),
			 std::forward_as_tuple(make_lw_shared<string>(
						 val.get(), val.size())));
	}
	return make_ready_future<>();
      }

      future<> Object::setattrs(attr_ns ns,
				held_span<pair<string,
					       const_buffer>> attrpairs) {
	if (ns >= attr_ns::END)
	  return make_exception_future<>(
	    std::system_error(errc::invalid_argument,
			      "Invalid attribute namespace"s));

	auto& m = attarray[(unsigned)ns];

	try {
	  std::for_each(
	    attrpairs->begin(), attrpairs->end(),
	    [&m](pair<string, const_buffer>& attrpair) {
	      auto& attr = attrpair.first;
	      auto& val = attrpair.second;
	      auto i = m.lower_bound(attr);
	      if (i->first == attr) {
		if (i->second.use_count() > 1) {
		  i->second = make_lw_shared<string>(val.get(), val.size());
		} else {
		  *(i->second) = to_sstring(std::move(val));
		}
	      } else {
		m.emplace_hint(i, std::piecewise_construct,
			       std::forward_as_tuple(std::move(attr)),
			       std::forward_as_tuple(
				 make_lw_shared<string>(
				   val.get(), val.size())));
	      }
	    });
	} catch (const std::exception& e) {
	  return make_exception_future<>(e);
	}
	return make_ready_future<>();
      }

      future<> Object::rmattr(attr_ns ns, string attr) {
	if (!local()) {
	  return smp::submit_to(
	    cpu, [this, ns, attr = std::move(attr)] () mutable {
	      return rmattr(ns, attr);
	    });
	}
	if (ns >= attr_ns::END)
	  return make_exception_future<>(
	    std::system_error(errc::invalid_argument,
			      "Invalid attribute namespace"s));

	auto& m = attarray[(unsigned)ns];
	auto i = m.find(attr);
	if (i == m.end())
	  return make_exception_future<>(
	    std::system_error(errc::no_such_attribute_key,
			      "'"s + (std::string)attr +
			      "' could not be found"s));
	auto j = attrcursors.begin();
	while (j != attrcursors.end()) {
	  if (j->i == i) {
	    auto k = j;
	    ++j;
	    k->valid = false;
	    attrcursors.erase(k);
	  } else {
	    ++j;
	  }
	}
	m.erase(i);
	return make_ready_future<>();
      }

      future<> Object::rmattrs(attr_ns ns, held_span<string> attrs) {
	if (!local()) {
	  return smp::submit_to(
	    cpu, [this, ns, attrs = std::move(attrs)] () mutable {
	      return rmattrs(ns, std::move(attrs));
	    });
	}
	if (ns >= attr_ns::END)
	  return make_exception_future<>(
	    std::system_error(errc::invalid_argument,
			      "Invalid attribute namespace"s));

	auto& m = attarray[(unsigned)ns];

	try {
	  std::for_each(
	    attrs->begin(), attrs->end(),
	    [&m](const string& attr) {
	      auto i = m.find(attr);
	      if (i == m.end())
		throw std::system_error(errc::no_such_attribute_key,
					"'"s + (std::string)attr +
					"' could not be found"s);
	      m.erase(i);
	    });
	} catch (const std::exception& e) {
	  return make_exception_future<>(e);
	}
	return make_ready_future<>();
      }

      AttrCursorRef Object::cursor_ref(attrmap::const_iterator i) const {
	auto j = attrcursors.begin();
	while (j != attrcursors.end()) {
	  if (j->i == i) {
	    return { &(*j) };
	  } else {
	    ++j;
	  }
	}
	auto c = make_unique<AttrCursor>(i);
	attrcursors.push_front(*c);
	return AttrCursorRef(c.release());
      }

      future<held_span<string>, optional<AttrCursorRef>>
	Object::enumerate_attr_keys(attr_ns ns, optional<AttrCursorRef> cursor,
				    size_t to_return) const {
	if (!local()) {
	  return smp::submit_to(
	    cpu, [this, ns, to_return, cursor = std::move(cursor)] () mutable {
	      return enumerate_attr_keys(ns, std::move(cursor), to_return);
	    });
	}
	if (ns >= attr_ns::END)
	  return make_exception_future<
	    held_span<string>, optional<AttrCursorRef>>(
	      std::system_error(errc::invalid_argument,
				"Invalid attribute namespace"s));

	auto& m = attarray[(unsigned)ns];

	auto out = make_unique<std::vector<string>>();
	out->reserve(std::min(to_return, m.size()));

	std::remove_reference_t<decltype(m)>::const_iterator k;
	if (cursor) {
	  auto& c = static_cast<AttrCursor&>(*(*cursor));
	  if (c.valid)
	    k = c.i;
	  else
	    return make_exception_future<
	      held_span<string>, optional<AttrCursorRef>>(
		std::system_error(errc::invalid_cursor));
	} else {
	  k = m.begin();
	}

	optional<AttrCursorRef> rc;

	while (k != m.end()) {
	  if (out->size() == to_return) {
	    rc = cursor_ref(k);
	    break;
	  }
	  out->push_back(k->first);
	}

	return make_ready_future<
	  held_span<string>, optional<AttrCursorRef>>(
	    std::move(out), std::move(rc));
      }

      future<held_span<pair<string, const_buffer>>,
	     optional<AttrCursorRef>> Object::enumerate_attr_kvs(
	       attr_ns ns, optional<AttrCursorRef> cursor,
	       size_t to_return) const {
	if (!local()) {
	  return smp::submit_to(
	    cpu, [this, ns, to_return, cursor = std::move(cursor)] () mutable {
	      return enumerate_attr_kvs(ns, std::move(cursor), to_return);
	    });
	}
	if (ns >= attr_ns::END)
	  return make_exception_future<
	    held_span<pair<string, const_buffer>>, optional<AttrCursorRef>>(
	      std::system_error(errc::invalid_argument,
				"Invalid attribute namespace"s));

	auto& m = attarray[(unsigned)ns];

	auto out = std::make_unique<std::vector<pair<string, const_buffer>>>();
	out->reserve(std::min(to_return, m.size()));

	std::remove_reference_t<decltype(m)>::const_iterator k;
	if (cursor) {
	  auto& c = static_cast<AttrCursor&>(*(*cursor));
	  if (c.valid)
	    k = c.i;
	  else
	    return make_exception_future<
	      held_span<pair<string, const_buffer>>, optional<AttrCursorRef>>(
		std::system_error(errc::invalid_cursor));
	} else {
	  k = m.begin();
	}

	optional<store::AttrCursorRef> rc;

	while (k != m.end()) {
	  if (out->size() == to_return) {
	    rc = cursor_ref(k);
	    break;
	  }
	  out->emplace_back(std::piecewise_construct,
			    std::forward_as_tuple(k->first),
			    std::forward_as_tuple(
			      make_const_buffer(k->second)));
	}


	return make_ready_future<
	  held_span<pair<string, const_buffer>>, optional<AttrCursorRef>>(
	    std::move(out), std::move(rc));
      }

      future<const_buffer> Object::get_header() const {
	if (!local()) {
	  return smp::submit_to(cpu, [this] { return get_header(); });
	}

	return make_ready_future<const_buffer>
	  (make_const_buffer(omap_header));
      }

      future<> Object::set_header(const_buffer header) {
	if (!local()) {
	  return smp::submit_to(
	    cpu, [this, header = std::move(header)] () mutable {
	      return set_header(std::move(header)); });
	}

	omap_header = make_lw_shared<string>(header.get(),
					     header.size());
	return make_ready_future<>();
      }

      future<> Object::commit() {
	return seastar::do_with(
	  make_unique<_::AsyncMutation>(this),
	  [](unique_ptr<_::AsyncMutation>& a) {
	    a->commit = true;
	    return a->p.get_future();
	  });
      }
   } // namespace mem
  } // namespace store
} // namespace crimson
