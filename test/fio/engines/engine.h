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
#pragma once

#include <core/reactor.hh>
#include <experimental/optional>

struct io_u; //< fio io unit

namespace boost { namespace program_options {
class variables_map;
class options_description;
} } // namespace boost::program_options

namespace bpo = boost::program_options;

namespace crimson {
namespace fio {

/// The backend interface for Engine
class Backend {
 public:
  virtual ~Backend() = default;

  /// Perform initialization within the seastar context.
  virtual future<> start() = 0;

  /// Handle an io request.
  virtual future<> handle_request(io_u* unit) = 0;
};

/**
 * Engine provides the queue() and getevents() part of the fio engine interface,
 * using the given \a Backend to satisfy io requests. It provides a safe bridge
 * between the fio and Seastar threading models by using the \a get_events()
 * call to run Seastar's event loop.
 *
 * The class operates on state that is global to Seastar, so only one instance
 * can exist at a time.
 */
class Engine final {
  /// backend interface to satisfy io requests
  Backend* backend{nullptr};
  /// queue of io completions for get_events() from the backend
  circular_buffer<io_u*> completions;
  /// completed ios from get_events()
  std::vector<io_u*> events;

 public:
  using Clock = lowres_clock; // 10ms resolution, atomic read to access
  using TimePoint = Clock::time_point;
  using OptionalTimePoint = std::experimental::optional<TimePoint>;

  Engine() = default;
  Engine(const bpo::variables_map& cfg, Backend* backend, uint32_t iodepth);
  ~Engine();

  Engine(Engine&&) = default;
  Engine& operator=(Engine&&) = default;

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  /// Enqueue an io request to be started on the next call to get_events().
  void queue(io_u* unit);

  /// Poll for completion events until an optional timeout is hit or at
  /// least min events are avialable. Return no more than max events.
  ///
  /// @return The number of completed events available via get_event()
  int get_events(uint32_t min, uint32_t max, OptionalTimePoint timeout);

  /// Return the given completed event from get_events().
  io_u* get_event(int event) { return events[event]; }


  /// Return the supported option descriptions.
  static bpo::options_description get_options_description();
};

inline void Engine::queue(io_u* unit)
{
  // use the backend to satisfy each request
  backend->handle_request(unit).then([this, unit] {
      completions.push_back(unit);
      engine().force_poll(); // return control to get_events()
    });
}

inline int Engine::get_events(uint32_t min, uint32_t max,
                              OptionalTimePoint timeout)
{
  timer<Clock> timer;
  if (timeout) {
    timer.set_callback([&min] {
        min = 0; // break the poll() loop
        engine().force_poll(); // return control to get_events()
      });
    timer.arm(timeout.value());
  }

  // poll until the minimum number of events available
  size_t count;
  do {
    count = completions.size();
  } while (count < min && engine().poll());

  // consume up to max completions
  if (count > max)
    count = max;

  events.clear();

  while (count--) {
    auto unit = completions.front();
    completions.pop_front();
    events.push_back(unit);
  }
  return events.size();
}

} // namespace fio
} // namespace crimson
