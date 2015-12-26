// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

// Crimson: a prototype high performance OSD

// Copyright (C) 2015 Adam C. Emerson <aemerson@redhat.com
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

/// \file crimson.cc
/// \brief Main driver for Project Crimson server executable
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#include "message_helpers.h"
#include "osd_map.capnp.h"
#include <gsl.h>
#include <core/app-template.hh>
#include <core/fstream.hh>
#include <core/reactor.hh>
#include <boost/program_options.hpp>
#include <iostream>

using namespace crimson;

namespace bpo = boost::program_options;

int main(int argc, char** argv)
{
  app_template crimson;

  crimson.add_options()
      ("osd", bpo::value<std::vector<uint32_t>>(), "osd id")
      ("map", bpo::value<std::string>(), "osd map filename")
      ;

  try {
    return crimson.run(argc, argv,
      [&crimson] {
        auto cfg = crimson.configuration();
        // read the osdmap from disk
        return open_file_dma(cfg["map"].as<std::string>(), open_flags::ro).then(
          [] (auto fd) {
            return do_with(make_file_input_stream(fd), [] (auto& in) {
                return readMessage(in);
              }).finally([fd] () mutable {
                return fd.close().finally([fd] {});
              });
          }).then(
          [] (std::unique_ptr<capnp::MessageReader>&& reader) {
            auto osdmap = reader->getRoot<proto::osd::OsdMap>();
            std::cout << "segment: " << reader->getSegment(0) << std::endl;
            std::cout << "read osdmap:\n";
            osdmap.toString().visit([] (auto s) { std::cout << s.begin(); });
            std::cout << std::endl;
          });
      });
  } catch (std::exception& e) {
    std::cerr << "Exiting with exception: " << e.what() << std::endl;
    return 1;
  }
}
