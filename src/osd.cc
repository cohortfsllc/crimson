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

#include "osd.h"
#include "capn_connection.h"
#include <iostream>

using namespace crimson;
using osd::OSD;

future<> OSD::handle_message(lw_shared_ptr<net::CapnConnection> conn,
                             net::capn::SegmentMessageReader&& reader)
{
  auto message = reader.getRoot<proto::Message>();
  switch (message.which()) {
    case proto::Message::Which::OSD_READ:
      return handle_osd_read(conn, message);
    case proto::Message::Which::OSD_WRITE:
      return handle_osd_write(conn, message);
    default:
      return make_exception_future<>(std::runtime_error("unhandled message"));
  }
}

future<> OSD::handle_osd_read(lw_shared_ptr<net::CapnConnection> conn,
                              proto::Message::Reader message)
{
  auto request = message.getOsdRead();
  std::cout << "handle_osd_read seq=" << message.getHeader().getSequence()
      << " oid=" << request.getObject().cStr()
      << " offset=" << request.getOffset()
      << " length=" << request.getLength() << std::endl;

  auto builder = make_lw_shared<capnp::MallocMessageBuilder>();
  auto reply = builder->initRoot<proto::Message>();
  reply.initHeader().setSequence(message.getHeader().getSequence());
  reply.initOsdReadReply().setErrorCode(ENOENT);
  return conn->write_message(builder).then(
    [] { std::cout << "osd_read_reply sent" << std::endl; });
}

future<> OSD::handle_osd_write(lw_shared_ptr<net::CapnConnection> conn,
                               proto::Message::Reader message)
{
  auto request = message.getOsdWrite();
  std::cout << "handle_osd_write seq=" << message.getHeader().getSequence()
      << " oid=" << request.getObject().cStr()
      << " offset=" << request.getOffset()
      << " length=" << request.getLength()
      << " flags=" << std::hex << request.getFlags() << std::dec << std::endl;

  auto builder = make_lw_shared<capnp::MallocMessageBuilder>();
  auto reply = builder->initRoot<proto::Message>();
  reply.initHeader().setSequence(message.getHeader().getSequence());
  reply.initOsdWriteReply().setFlags(request.getFlags());
  return conn->write_message(builder).then(
    [] { std::cout << "osd_write_reply sent" << std::endl; });
}
