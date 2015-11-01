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

#include <iostream>

#include "osd.h"
#include "capn_connection.h"
#include <core/app-template.hh>

using namespace crimson;
using namespace crimson::net;
using crimson::net::capn::SegmentMessageReader;
using crimson::net::capn::ProtocolError;

namespace bpo = boost::program_options;

/// Return the bind address from configuration
socket_address get_bind_address(const bpo::variables_map& cfg)
{
  auto addr = cfg["address"].as<std::string>();
  auto port = cfg["port"].as<uint16_t>();
  if (addr.empty())
    return make_ipv4_address({port});
  return make_ipv4_address({addr, port});
}

int main(int argc, char** argv) {
  crimson::osd::OSD osd;
  lw_shared_ptr<server_socket> listener;
  app_template crimson;

  crimson.add_options()
      ("address", bpo::value<std::string>()->default_value(""),
       "Specify the bind address")
      ("port", bpo::value<uint16_t>()->default_value(6800),
       "Specify the port to bind")
      ;

  try {
    return crimson.run(argc, argv,
      [&osd, &listener, &crimson] {
        auto cfg = crimson.configuration();
        auto addr = get_bind_address(cfg);

        listen_options lo;
        lo.reuse_address = true;
        listener = engine().listen(addr, lo);

        return keep_doing(
          [&osd, &listener] {
            return listener->accept().then(
              [&osd] (connected_socket fd, socket_address addr) {
                std::cout << "client " << addr << " connected" << std::endl;
                auto conn = make_lw_shared<CapnConnection>(std::move(fd), addr);
                // read messages until eof
                return do_until(
                  [conn] { return conn->in.eof(); },
                  [&osd, conn] {
                    return conn->read_message().then(
                      [&osd, conn] (SegmentMessageReader&& reader) {
                        return osd.handle_message(conn, std::move(reader));
                      });
                  }).handle_exception([conn] (auto eptr) {
                    // just print an error and close the connection on errors
                    try {
                      if (eptr)
                        std::rethrow_exception(eptr);
                    } catch (const std::exception& e) {
                      std::cout << "client " << conn->address
                          << " disconnected: " << e.what() << std::endl;
                    }
                  }).finally([conn] {
                    return conn->out.close().then([conn] {});
                  });
              });
          }).or_terminate();
      });
  } catch (std::exception& e) {
    std::cerr << "Exiting with exception: " << e.what() << std::endl;
    return 1;
  }
}
