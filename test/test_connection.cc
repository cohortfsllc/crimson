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

#include "connection.h"
#include <core/app-template.hh>
#include <gtest/gtest.h>

namespace {
using crimson::net::Connection;

using tmp_buf = temporary_buffer<char>;
using optional_tmp_buf = std::experimental::optional<tmp_buf>;

class EchoConnection : public Connection {
  struct consumer : public tmp_buf, input_stream<char>::ConsumerConcept {
    // consume every buffer by moving it into my base tmp_buf
    future<optional_tmp_buf> operator()(tmp_buf input) {
      tmp_buf::operator=(std::move(input));
      return make_ready_future<optional_tmp_buf>(tmp_buf{});
    }
  } data;

 public:
  EchoConnection(connected_socket&& socket, socket_address address)
    : Connection(std::move(socket), address) {}

  future<> read_to_eof() {
    return do_until(
      [this] { return in.eof(); },
      [this] {
        // echo every consumed buffer to the output stream
        return in.consume(data).then(
          [this] {
            if (data.empty())
              return make_ready_future<>();
            return out.write(data.get(), data.size()).then(
              [this] { out.flush(); });
          });
      }).finally([this] { return out.close(); });
  }
};
} // namespace

TEST(Connection, Echo) {
  lw_shared_ptr<server_socket> listener;
  tmp_buf reply;

  app_template app;
  char arg[] = "test_messenger";
  char *args = arg;
  app.run(1, &args,
    [&listener, &reply] {
      auto addr = make_ipv4_address({"127.0.0.1", 3678});

      listen_options lo;
      lo.reuse_address = true;
      listener = engine().listen(addr, lo);
      keep_doing([&listener] {
        return listener->accept().then(
          [] (connected_socket fd, socket_address address) {
            auto conn = make_lw_shared<EchoConnection>(std::move(fd), address);
            return conn->read_to_eof().finally([conn] {});
          });
      }).or_terminate();

      return engine().connect(addr).then(
        [&reply, addr] (connected_socket fd) {
          auto conn = make_lw_shared<Connection>(std::move(fd), addr);
          return conn->out.write("hello", 6).then(
            [&reply, conn] {
              conn->out.flush();
              return conn->in.read_exactly(6).then(
                [&reply] (auto data) {
                  reply = std::move(data);
                  return make_ready_future<>();
                });
            }).finally([conn] {
              return conn->out.close().finally([conn] {});
            });
        });
    });
  ASSERT_STREQ("hello", reply.begin());
}
