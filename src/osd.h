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

#include "crimson.capnp.h"
#include "messenger.h"

namespace crimson {
namespace osd {

class OSD {
  future<> handle_osd_read(shared_ptr<net::Connection> conn,
                           proto::Message::Reader message);
  future<> handle_osd_write(shared_ptr<net::Connection> conn,
                            proto::Message::Reader message);
 public:
  future<> handle_message(shared_ptr<net::Connection> conn,
                          net::Connection::MessageReaderPtr&& message);
};

} // namespace osd
} // namespace crimson
