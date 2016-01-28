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
      future<IovecRef> Object::read(const Range& r) const {
	if (!local()) {
	  smp::submit_to(
	    cpu, [this, r]{ read(Range(r)); });
	}
	if (!in_range(r))
	  return make_exception_future<IovecRef>(
	    std::system_error(errc::out_of_range));
	else
	  return data.read(r);
      }

      future<> Object::write(IovecRef&& iov) {
	return data.write(std::move(iov));
      }

      future<> Object::zero(const Range& r) {
	if (data_len < r.offset + r.length)
	  data_len = r.offset + r.length;
	return data.hole_punch(r);
      }

      future<> Object::hole_punch(const Range& r) {
	if (!in_range(r))
	  return make_exception_future(std::system_error(
					 errc::out_of_range));
	else
	  return data.hole_punch(r);
      }
      future<> Object::truncate(const Length l) {
	return data.hole_punch(
	  Range(l, std::numeric_limits<uint64_t>::max() - l));
      }

      future<const_buffer> Object::getattr(
	attr_ns ns, string&& attr) const {
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
	  const_buffer(i->second->c_str(), i->second->size(),
				 make_object_deleter(i->second)));
      }

      future<std::vector<const_buffer>> Object::getattrs(
	attr_ns ns, std::vector<string>&& attrs) const {
	if (ns >= attr_ns::END)
	  return make_exception_future<std::vector<const_buffer>>(
	    std::system_error(errc::invalid_argument,
			      "Invalid attribute namespace"s));

	std::vector<const_buffer> out;

	out.reserve(attrs.size());
	auto& m = attarray[(unsigned)ns];

	try {
	  boost::range::transform(
	    attrs, out.begin(),
	    [&m](const string& attr) {
	      auto i = m.find(attr);
	      if (i == m.end())
		throw std::system_error(errc::no_such_attribute_key,
					"'"s + (std::string)attr +
					"' could not be found"s);
	      else
		return const_buffer(
		  i->second->c_str(), i->second->size(),
		  make_object_deleter(i->second));
	    });
	} catch (const std::exception& e) {
	  return make_exception_future<std::vector<const_buffer>>(e);
	}
	return make_ready_future<std::vector<const_buffer>>(
	  std::move(out));
      }

      future<> Object::setattr(attr_ns ns, string&& attr,
			       const_buffer&& val) {
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

      future<> Object::setattrs(
	attr_ns ns,
	std::vector<pair<string, const_buffer>>&& attrpairs) {
	if (ns >= attr_ns::END)
	  return make_exception_future<>(
	    std::system_error(errc::invalid_argument,
			      "Invalid attribute namespace"s));

	auto& m = attarray[(unsigned)ns];

	try {
	  boost::range::for_each(
	    attrpairs,
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

      future<> Object::rmattr(attr_ns ns, string&& attr) {
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
	m.erase(i);
	return make_ready_future<>();
      }

      future<> Object::rmattrs(
	attr_ns ns, std::vector<string>&& attrs) {
	if (ns >= attr_ns::END)
	  return make_exception_future<>(
	    std::system_error(errc::invalid_argument,
			      "Invalid attribute namespace"s));

	auto& m = attarray[(unsigned)ns];

	try {
	  boost::range::for_each(attrs, [&m](const string& attr) {
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

      /// TODO actually honor to_return parameter and implement cursor logic
      future<std::vector<string>, optional<AttrCursorRef>>
	Object::enumerate_attr_keys(attr_ns ns, optional<AttrCursorRef> cursor,
				    size_t to_return) const {
	if (ns >= attr_ns::END)
	  return make_exception_future<
	    std::vector<string>, optional<AttrCursorRef>>(
	      std::system_error(errc::invalid_argument,
				"Invalid attribute namespace"s));

	auto& m = attarray[(unsigned)ns];

	std::vector<string> out;
	out.reserve(m.size());

	boost::range::transform(m, out.begin(), [](const auto& kv) {
	    return kv.first;
	  });

	return make_ready_future<
	  std::vector<string>, optional<AttrCursorRef>>(
	    std::move(out), nullopt);
      }

      /// TODO actually honor to_return parameter and implement cursor logic
      future<std::vector<pair<string, const_buffer>>,
	     optional<AttrCursorRef>> Object::enumerate_attr_kvs(
	       attr_ns ns,
	       optional<AttrCursorRef> cursor,
	       size_t to_return) const {
	if (ns >= attr_ns::END)
	  return make_exception_future<
	    std::vector<pair<string, const_buffer>>, optional<AttrCursorRef>>(
	      std::system_error(errc::invalid_argument,
				"Invalid attribute namespace"s));

	auto& m = attarray[(unsigned)ns];

	std::vector<pair<string, const_buffer>> out;
	out.reserve(m.size());

	boost::range::transform(m, out.begin(), [](const auto& kv) {
	    return std::make_pair(kv.first,
				  const_buffer(
				    kv.second->c_str(), kv.second->size(),
				    make_object_deleter(kv.second)));
	  });

	return make_ready_future<
	  std::vector<pair<string, const_buffer>>, optional<AttrCursorRef>>(
	    std::move(out), nullopt);
      }
    } // namespace mem
  } // namespace store
} // namespace crimson
