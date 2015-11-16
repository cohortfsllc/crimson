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

#include "msg/direct_messenger.h"
#include "msg/socket_messenger.h"
#include "crimson.capnp.h"
#include <capnp/message.h>
#include <kj/debug.h>
#include <core/app-template.hh>
#include <iostream>

using namespace crimson;
using namespace crimson::net;
using namespace crimson::proto::osd;

namespace {
std::ostream& operator<<(std::ostream& out, const kj::StringPtr& rhs) {
  return out << rhs.cStr();
}

future<> run_mock_server(shared_ptr<Connection> conn)
{
  // read a single osd_read message from the connection
  std::cout << "waiting for osd_read" << std::endl;
  return conn->read_message().then(
    [conn] (Connection::MessageReaderPtr&& reader) {
      auto request = reader->getRoot<proto::Message>();
      auto read_request = request.getOsdRead();
      std::cout << "got osd_read oid=" << read_request.getObject()
          << " offset=" << read_request.getOffset()
          << " length=" << read_request.getLength()<< std::endl;
      // reply with ENOENT
      auto message = std::make_unique<capnp::MallocMessageBuilder>();
      auto reply = message->initRoot<proto::Message>().initOsdReadReply();
      reply.setErrorCode(ENOENT);
      std::cout << "sending osd_read_reply" << std::endl;
      return conn->write_message(std::move(message));
    }).finally([conn] {
      return conn->close().finally([conn] {});
    });
}

future<uint32_t> run_mock_client(shared_ptr<Connection> conn)
{
  // send an osd_read message over the connection
  auto message = std::make_unique<capnp::MallocMessageBuilder>();
  auto request = message->initRoot<proto::Message>().initOsdRead();
  request.setOffset(65536);
  request.setLength(1024);
  std::cout << "sending osd_read" << std::endl;
  return conn->write_message(std::move(message)).then(
    [conn] {
      std::cout << "waiting for osd_read_reply" << std::endl;
      return conn->read_message().then(
        [] (Connection::MessageReaderPtr&& reader) {
          std::cout << "got osd_read_reply" << std::endl;
          // copy the reply
          auto reply = reader->getRoot<proto::Message>().getOsdReadReply();
          return make_ready_future<uint32_t>(reply.getErrorCode());
        });
    }).finally([conn] {
      return conn->close().finally([conn] {});
    });
}

future<> test_direct_connection()
{
  // start a listener
  auto listener = make_shared<DirectListener>();
  listener->accept().then(&run_mock_server);

  // connect to the listener
  return listener->connect().then(
      &run_mock_client
    ).then([] (auto result) {
      KJ_REQUIRE(result == ENOENT);
    }).finally([listener] {});
}

future<> test_socket_connection()
{
  auto addr = make_ipv4_address({"127.0.0.1", 3678});

  // start a listener
  auto listener = make_shared<SocketListener>(addr);
  listener->accept().then(&run_mock_server);

  // connect to the listener
  return engine().connect(addr).then(
    [addr] (connected_socket fd) {
      auto conn = make_shared<SocketConnection>(std::move(fd), addr);
      return run_mock_client(conn);
    }).then([] (auto result) {
      KJ_REQUIRE(result == ENOENT);
    }).finally([listener] {});
}

} // anonymous namespace

int main(int argc, char** argv)
{
  app_template app;
  return app.run(argc, argv, [] {
      return now().then(
          &test_direct_connection
        ).then(
          &test_socket_connection
        ).then([] {
          std::cout << "All tests succeeded" << std::endl;
        }).handle_exception([] (auto eptr) {
          std::cout << "Test failure" << std::endl;
          return make_exception_future<>(eptr);
        });
    });
}
