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

#include <memory>
#include <core/future.hh>
#include <core/shared_ptr.hh>

#include "crimson.h"

namespace capnp {
class MessageReader;
class MessageBuilder;
} // namespace capnp

namespace crimson {
namespace net {

class Connection {
 public:
  virtual ~Connection() = default;

  using MessageReaderPtr = std::unique_ptr<capnp::MessageReader>;
  using MessageBuilderPtr = std::unique_ptr<capnp::MessageBuilder>;

  // Read a message from the connection.
  virtual future<MessageReaderPtr> read_message() = 0;

  // Write a message to the connection.
  virtual future<> write_message(MessageBuilderPtr&& message) = 0;

  // Close the connection.
  virtual future<> close() = 0;
};

class Listener {
 public:
  virtual ~Listener() = default;

  /// Return a Connection object for the next client to connect
  virtual future<shared_ptr<Connection>> accept() = 0;

  /// Cancel outstanding accept()
  virtual future<> close() = 0;
};

} // namespace net
} // namespace crimson
