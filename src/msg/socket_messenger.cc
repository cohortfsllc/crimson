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

#include "socket_messenger.h"
#include "message_helpers.h"

using namespace crimson;
using namespace crimson::net;

namespace {

auto make_listener(socket_address address)
{
  listen_options lo;
  lo.reuse_address = true;
  return engine().listen(address, lo);
}

} // anonymous namespace

future<Connection::MessageReaderPtr> SocketConnection::read_message()
{
  return readMessage(in);
}

future<> SocketConnection::write_message(MessageBuilderPtr&& message)
{
  auto f = writeMessage(out, *message);
  return f.then([this] {
        return out.flush();
      }).finally([m = std::move(message)] {});
}

future<> SocketConnection::close()
{
  return out.close();
}


SocketListener::SocketListener(socket_address address)
  : listener(make_listener(address))
{
}

future<shared_ptr<Connection>> SocketListener::accept()
{
  return listener.accept().then(
    [] (auto socket, auto addr) -> shared_ptr<Connection> {
      return make_shared<SocketConnection>(std::move(socket), addr);
    });
}

future<> SocketListener::close()
{
  listener.abort_accept();
  return now();
}
