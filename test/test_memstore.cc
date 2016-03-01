// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

// Crimson: a prototype high performance OSD

// Copyright (C) 2016 Adam C. Emerson <aemerson@redhat.com>
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

#include <core/app-template.hh>
#include <core/reactor.hh>

#include "crimson.h"
#include "store/mem/Store.h"
#include "store/mem/Collection.h"
#include "store/mem/Object.h"

using namespace crimson;

namespace {
  future<> test_make_memstore() {
    return store::mem::Store::make().then(
      [](shared_ptr<store::mem::Store> s) {
	return make_ready_future<>();
      });
  }
}

int main(int argc, char** argv) {
  seastar::app_template app;
  try {
    app.run(argc, argv, [] {
	return now().then(
	  &test_make_memstore)
	  .then([] {
	      std::cout << "All tests succeeded" << std::endl; })
	  .handle_exception([] (auto eptr) {
	      std::cout << "Test failure" << std::endl;
	      return make_exception_future<>(eptr);
	    });
      });
  } catch (const std::exception& e) {
    std::cerr << "Tests failed: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
