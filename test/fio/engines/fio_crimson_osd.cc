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

using namespace crimson;

namespace {

class OsdBackend : public fio::Backend {
 public:
  future<> start() override {
    return now();
  }
  future<> handle_request(io_u* unit) override {
    return now();
  }
};

struct OsdEngine {
  OsdBackend backend;
  fio::Engine engine;

  OsdEngine(const bpo::variables_map& cfg, uint32_t iodepth)
    : engine(cfg, &backend, iodepth) {}
};

template <typename T, size_t N>
constexpr size_t arraysize(const T(&)[N]) { return N; }

int fio_crimson_init(thread_data* td)
{
  const char* argv[] = {
    "crimson-osd",
    "-c", "1", // don't spawn any extra seastar threads
    "-m", "1M", // don't hog all of the memory
    // seastar segfaults on SIGINT because it comes from a non-seastar thread
    "--no-handle-interrupt",
  };
  auto argc = arraysize(argv);

  auto options = fio::Engine::get_options_description();
  bpo::variables_map cfg;
  cfg.emplace("argv0", bpo::variable_value(argv[0], false));

  try {
    bpo::store(bpo::command_line_parser(argc, const_cast<char**>(argv))
               .options(options)
               .run(),
               cfg);
  } catch (bpo::error& e) {
    std::cerr << "configuration failed. " << e.what() << std::endl;
    return -1;
  }
  bpo::notify(cfg);

  try {
    td->io_ops->data = new OsdEngine(cfg, td->o.iodepth);
  } catch (std::exception& e) {
    std::cerr << "initialization failed. " << e.what() << std::endl;
    return -1;
  }
  return 0;
}

int fio_crimson_queue(thread_data* td, io_u* unit)
{
  auto c = static_cast<OsdEngine*>(td->io_ops->data);
  c->engine.queue(unit);
  return FIO_Q_QUEUED;
}

io_u* fio_crimson_event(thread_data* td, int event)
{
  auto c = static_cast<OsdEngine*>(td->io_ops->data);
  return c->engine.get_event(event);
}

int fio_crimson_getevents(thread_data* td, unsigned int min,
                          unsigned int max, const timespec* t)
{
  auto c = static_cast<OsdEngine*>(td->io_ops->data);

  auto timeout = fio::Engine::OptionalTimePoint{};
  if (t) {
    using namespace std::chrono;
    auto duration = seconds{t->tv_sec} + nanoseconds{t->tv_nsec};

    using Clock = fio::Engine::Clock;
    timeout = Clock::now() + duration_cast<Clock::duration>(duration);
  }

  return c->engine.get_events(min, max, timeout);
}

void fio_crimson_cleanup(thread_data* td)
{
  auto c = static_cast<OsdEngine*>(td->io_ops->data);
  td->io_ops->data = nullptr;
  delete c;
}

int fio_crimson_open_file(thread_data* td, fio_file* f)
{
  return 0;
}

int fio_crimson_close_file(thread_data fio_unused* td, fio_file* f)
{
  return 0;
}

} // anonymous namespace

extern "C" {

// seastar adds -fvisibility=hidden to the ldflags, so we need to manually
// export this to prevent it from being stripped from the shared library
__attribute__((__visibility__("default")))
void get_ioengine(struct ioengine_ops **ioengine_ptr)
{
  struct ioengine_ops *e;
  e = (struct ioengine_ops *) calloc(sizeof(struct ioengine_ops), 1);

  strncpy(e->name, "crimson-osd", sizeof(e->name));
  e->version    = FIO_IOOPS_VERSION;
  e->init       = fio_crimson_init;
  e->queue      = fio_crimson_queue;
  e->getevents  = fio_crimson_getevents;
  e->event      = fio_crimson_event;
  e->cleanup    = fio_crimson_cleanup;
  e->open_file  = fio_crimson_open_file;
  e->close_file = fio_crimson_close_file;

  *ioengine_ptr = e;
}

} // extern "C"
