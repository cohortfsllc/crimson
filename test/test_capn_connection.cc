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

#include <iostream>
#include "capn_connection.h"
#include "crimson.capnp.h"
#include <core/app-template.hh>
#include <gtest/gtest.h>

using crimson::net::CapnConnection;
using crimson::net::capn::SegmentMessageReader;
using crimson::proto::Message;
using namespace crimson::proto::osd;

namespace {
std::ostream& operator<<(std::ostream& out, const kj::StringPtr& rhs) {
  return out << rhs.cStr();
}
} // anonymous namespace

TEST(CapnConnection, OsdRead) {
  lw_shared_ptr<server_socket> listener;

  capnp::MallocMessageBuilder builder;
  capnp::Orphan<read::Res> read_reply;
  auto reply = [&builder, &read_reply] (read::Res::Reader res) {
    read_reply = builder.getOrphanage().newOrphanCopy(res);
  };

  app_template app;
  char arg[] = "test_capn_proto";
  char *args = arg;
  app.run(1, &args,
    [&listener, &reply] {
      auto addr = make_ipv4_address({"127.0.0.1", 3678});

      // start a listener
      listen_options lo;
      lo.reuse_address = true;
      listener = engine().listen(addr, lo);
      keep_doing([&listener] {
        return listener->accept().then(
          [] (connected_socket fd, socket_address address) {
            // read a single osd_read message from the connection
            auto conn = make_lw_shared<CapnConnection>(std::move(fd), address);
            std::cout << "waiting for osd_read" << std::endl;
            return conn->read_message().then(
              [conn] (SegmentMessageReader&& reader) {
                auto request = reader.getRoot<Message>();
                auto read_request = request.getOsdRead();
                std::cout << "got osd_read oid=" << read_request.getObject()
                    << " offset=" << read_request.getOffset()
                    << " length=" << read_request.getLength()<< std::endl;
                // reply with ENOENT
                auto message = make_lw_shared<capnp::MallocMessageBuilder>();
                auto reply = message->initRoot<Message>().initOsdReadReply();
                reply.setErrorCode(ENOENT);
                std::cout << "sending osd_read_reply" << std::endl;
                return conn->write_message(message);
              }).finally([conn] {
                return conn->out.close().finally([conn] {});
              });
          });
      }).or_terminate();

      // connect to the listener
      return engine().connect(addr).then(
        [&reply, addr] (connected_socket fd) {
          // send an osd_read message
          auto conn = make_lw_shared<CapnConnection>(std::move(fd), addr);
          auto message = make_lw_shared<capnp::MallocMessageBuilder>();
          auto request = message->initRoot<Message>().initOsdRead();
          request.setOffset(65536);
          request.setLength(1024);
          std::cout << "sending osd_read" << std::endl;
          return conn->write_message(message).then(
            [&reply, conn] {
              std::cout << "waiting for osd_read_reply" << std::endl;
              return conn->read_message().then(
                [&reply] (SegmentMessageReader&& reader) {
                  std::cout << "got osd_read_reply" << std::endl;
                  // copy the reply
                  reply(reader.getRoot<Message>().getOsdReadReply());
                  return make_ready_future<>();
                });
            }).finally([conn] {
              return conn->out.close().finally([conn] {});
            });
        });
    });
  ASSERT_EQ(read_reply.get().getErrorCode(), ENOENT);
}
