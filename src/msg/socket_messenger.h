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

#include "messenger.h"
#include <core/reactor.hh>

namespace crimson {
namespace net {

  using seastar::connected_socket;
  using seastar::server_socket;
  using seastar::socket_address;

/// A Connection that reads and writes over a connected_socket.
class SocketConnection : public Connection {
  connected_socket socket;
  socket_address address;
  input_stream<char> in;
  output_stream<char> out;

 public:
  SocketConnection(connected_socket&& fd, socket_address address)
    : socket(std::move(fd)),
      address(address),
      in(socket.input()),
      out(socket.output())
  {}

  /// Read a message from the Connection's input stream
  future<MessageReaderPtr> read_message() override;

  /// Write a message to the Connection's output stream
  future<> write_message(MessageBuilderPtr&& message) override;

  /// Close the output stream
  future<> close() override;
};

/// A Listener that listens on a server_socket.
class SocketListener : public Listener {
  server_socket listener;

 public:
  SocketListener(socket_address address);

  /// Accept the next incoming connection on the server_socket
  future<shared_ptr<Connection>> accept() override;

  /// Cancel outstanding accept()
  future<> close() override;
};

} // namespace net
} // namespace crimson
