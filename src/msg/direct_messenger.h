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
#include <core/circular_buffer.hh>
#include <core/shared_ptr.hh>

namespace crimson {
namespace net {

/// A Connection that reads and writes directly to another Connection pointer.
class DirectConnection : public Connection {
  shared_ptr<DirectConnection> other; //< other endpoint of the connection
  seastar::circular_buffer<promise<MessageReaderPtr>> reads_waiting_for_message;
  seastar::circular_buffer<promise<MessageReaderPtr>> messages_waiting_for_read;

  /// connect to another endpoint
  void connect(shared_ptr<DirectConnection> conn) { other = conn; }

  /// receive a message from the other endpoint
  void handle_message(MessageBuilderPtr&& message);

  // constructor is hidden for make_pair()
  DirectConnection() = default;

 public:
  /// Read a message from the \a other connection.
  future<MessageReaderPtr> read_message() override;

  /// Write a message to the \a other connection.
  future<> write_message(MessageBuilderPtr&& message) override;

  /// Close the connection.
  future<> close() override;

  /// Return a connected pair.
  static auto make_pair()
  {
    DirectConnection c;
    auto a = make_shared<DirectConnection>(std::move(c));
    auto b = make_shared<DirectConnection>(std::move(c));
    a->connect(b);
    b->connect(a);
    return std::make_pair(a, b);
  }
};

/// A Listener that enables clients within the process to initiate a
/// DirectConnection.
class DirectListener : public Listener {
  promise<shared_ptr<Connection>> accept_promise;
  bool accepting;
 public:
  DirectListener();

  /// Return a future that resolves on the next call to connect().
  future<shared_ptr<Connection>> accept() override;

  /// Fail the accept_promise and reset to initial state.
  future<> close() override;

  /// Create a DirectConnection pair and share it with accept().
  future<shared_ptr<Connection>> connect();
};

} // namespace net
} // namespace crimson
