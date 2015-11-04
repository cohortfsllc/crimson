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

#include "direct_messenger.h"
#include <capnp/message.h>
#include <core/reactor.hh>

using namespace crimson::net;

namespace {

/// MessageBuilderReader adapts the given MessageBuilder into a MessageReader by
/// sharing its segments.
class MessageBuilderReader : public capnp::SegmentArrayMessageReader {
  /// must hold a reference on the builder as long as its segments are in use
  Connection::MessageBuilderPtr message;
 public:
  MessageBuilderReader(Connection::MessageBuilderPtr&& builder)
    : SegmentArrayMessageReader(builder->getSegmentsForOutput()),
      message(std::move(builder)) {}
};

} // anonymous namespace

void DirectConnection::handle_message(MessageBuilderPtr&& message)
{
  auto adapter = std::make_unique<MessageBuilderReader>(std::move(message));
  if (!reads_waiting_for_message.empty()) {
    // use this message to fulfil the first promise from read_message()
    reads_waiting_for_message.front().set_value(std::move(adapter));
    reads_waiting_for_message.pop_front();
  } else {
    // enqueue a promise for read_message()
    messages_waiting_for_read.emplace_back();
    messages_waiting_for_read.back().set_value(std::move(adapter));
  }
}

future<Connection::MessageReaderPtr> DirectConnection::read_message()
{
  if (!messages_waiting_for_read.empty()) {
    // return an already-fulfilled promise from handle_message()
    auto fut = messages_waiting_for_read.front().get_future();
    messages_waiting_for_read.pop_front();
    return fut;
  } else {
    // enqueue a promise for handle_message()
    reads_waiting_for_message.emplace_back();
    return reads_waiting_for_message.back().get_future();
  }
}

future<> DirectConnection::write_message(MessageBuilderPtr&& message)
{
  other->handle_message(std::move(message));
  return now();
}

future<> DirectConnection::close()
{
  auto p = std::move(other);
  if (!p)
    return now();

  auto e = std::runtime_error{"connection closed"};
  reads_waiting_for_message.for_each([&e] (auto& p) { p.set_exception(e); });
  auto release_read = std::move(reads_waiting_for_message);

  auto destroy_unread = std::move(messages_waiting_for_read);
  return p->close();
}


DirectListener::DirectListener()
  : accepting(false)
{}

future<shared_ptr<Connection>> DirectListener::accept()
{
  if (accepting)
    return make_exception_future<shared_ptr<Connection>>(
        std::runtime_error("address in use"));
  accepting = true;
  return accept_promise.get_future();
}

/// Fail the accept_promise and reset to initial state.
future<> DirectListener::close()
{
  accept_promise.set_exception(std::runtime_error("listener closed"));
  auto destroy_previous = std::move(accept_promise);
  accepting = false;
  return now();
}

/// Create a DirectConnection pair and share it with accept().
future<shared_ptr<Connection>> DirectListener::connect()
{
  if (!accepting)
    return make_exception_future<shared_ptr<Connection>>(
        std::runtime_error("connection refused"));
  accepting = false;
  auto c = DirectConnection::make_pair();
  accept_promise.set_value(c.second);
  auto destroy_previous = std::move(accept_promise);
  return make_ready_future<shared_ptr<Connection>>(c.first);
}
