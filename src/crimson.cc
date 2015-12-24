// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

// Crimson: a prototype high performance OSD

// Copyright (C) 2015 Adam C. Emerson <aemerson@redhat.com
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

/// \file crimson.cc
/// \brief Main driver for Project Crimson server executable
///
/// \author Adam C. Emerson <aemerson@redhat.com>

#include "common/buffer_array_message_reader.h"
#include "osd_map.capnp.h"
#include <gsl.h>
#include <core/app-template.hh>
#include <core/reactor.hh>
#include <boost/program_options.hpp>
#include <iostream>

using namespace crimson;

namespace bpo = boost::program_options;

namespace {

struct BlockReader {
  using tmp_buf = temporary_buffer<char>;

  file fd;
  std::vector<tmp_buf> segments;
  uint64_t offset{0};
  size_t size{0};
  size_t block_size{0};

  BlockReader(file fd, size_t size)
    : fd(fd),
      size(size),
      block_size(fd.disk_read_dma_alignment()) {
    segments.reserve(align_up(size, block_size) / block_size);
  }

  future<std::vector<tmp_buf>> read_blocks() && {
    std::cout << "reading offset " << offset << std::endl;

    auto buf = tmp_buf::aligned(fd.memory_dma_alignment(), block_size);
    auto read = fd.dma_read<char>(offset, buf.get_write(), buf.size());

    return read.then(
      [r = std::move(*this), buf = std::move(buf)] (auto count) mutable {
        if (!count) {
          std::cout << "read to eof at offset " << r.offset << std::endl;
          return make_ready_future<std::vector<tmp_buf>>(std::move(r.segments));
        }
        if (count < r.block_size) {
          r.segments.emplace_back(std::move(buf).prefix(count));
          std::cout << "short read at offset " << r.offset
              << " count " << count << std::endl;
          return make_ready_future<std::vector<tmp_buf>>(std::move(r.segments));
        }
        r.segments.emplace_back(std::move(buf));
        r.offset += r.block_size;
        return std::move(r).read_blocks();
      });
  }
};

future<BufferArrayMessageReader<>> read_osdmap(const std::string& filename)
{
  return open_file_dma(filename, open_flags::ro).then(
    [] (auto fd) {
      std::cout << "file opened, reading size.." << std::endl;
      // read the file size
      return fd.size().then(
        [fd] (auto size) {
          std::cout << "file size is " << size << std::endl;
          return BlockReader{fd, size}.read_blocks();
        }).then([] (auto segs) {
          std::cout << "read " << segs.size() << " blocks" << std::endl;
          return make_ready_future<BufferArrayMessageReader<>>(std::move(segs));
        }).finally([fd] { std::cout << "close" << std::endl; });
    });
}

} // anonymous namespace

int main(int argc, char** argv)
{
  app_template crimson;

  crimson.add_options()
      ("osd", bpo::value<std::vector<uint32_t>>(), "osd id")
      ("map", bpo::value<std::string>(), "osd map filename")
      ;

  try {
    return crimson.run(argc, argv,
      [&crimson] {
        auto cfg = crimson.configuration();
        // read the osdmap from disk
        return read_osdmap(cfg["map"].as<std::string>()).then(
          [] (BufferArrayMessageReader<>&& reader) {
            auto osdmap = reader.getRoot<proto::osd::OsdMap>();
            std::cout << "segment: " << reader.getSegment(0) << std::endl;
            std::cout << "read osdmap:\n";
            osdmap.toString().visit([] (auto s) { std::cout << s.begin(); });
            std::cout << std::endl;
          });
      });
  } catch (std::exception& e) {
    std::cerr << "Exiting with exception: " << e.what() << std::endl;
    return 1;
  }
}
