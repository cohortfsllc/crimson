// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

// Crimson: a prototype high performance OSD

// Copyright (C) 2015 Casey Bodley <cbodley@redhat.com>
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

#include <fio.h>

#include "engine.h"

#include <core/scollectd.hh>

using namespace crimson;
using namespace fio;

Engine::Engine(const bpo::variables_map& cfg, Backend* backend,
               uint32_t iodepth)
  : backend(backend)
{
  if (&engine())
    throw std::runtime_error("only a single instance of Engine is allowed");

  events.reserve(iodepth);

  // spawn and configure seastar reactor threads
  smp::configure(cfg);

  bool ready{false};
  engine().when_started().then([backend] {
      return backend->start();
    }).then([&ready] {
      ready = true;
      engine().force_poll(); // return control to ctor
    });

  auto r = engine().polling_mode_init();
  if (r != 0)
    throw std::runtime_error("reactor::polling_mode_init failed");

  // poll until ready
  do {} while (!ready && engine().poll());
}

Engine::~Engine()
{
  if (!backend) // the backend moved away
    return;

  // start shutdown on all threads
  engine().exit(0);

  // poll until done
  do {} while (engine().poll());

  engine().polling_mode_cleanup();
}

bpo::options_description Engine::get_options_description()
{
  bpo::options_description options("Options");
  // include all of the seastar options
  options.add(reactor::get_options_description());
  options.add(smp::get_options_description());
  options.add(scollectd::get_options_description());
  return options;
}
