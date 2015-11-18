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

#define KJ_STD_COMPAT // for std algorithms with capnp::List iterators

#include "osd_map.capnp.h"
#include <capnp/serialize-packed.h>
#include <kj/io.h>
#include <core/posix.hh>
#include <gsl.h>
#include <cxx_function.hpp>
#include <boost/program_options.hpp>

namespace bpo = boost::program_options;

using namespace crimson;
using cxx_function::function;

namespace {

int usage(const bpo::options_description& options)
{
  std::cerr << "Usage: osdmaptool <command> <filename> [options]\n"
      << options << std::endl;
  return EXIT_FAILURE;
}

struct Command {
  using Func = function<int(int fd, std::vector<std::string>&& args,
                            bpo::variables_map& cfg)>;

  Func func; //< command function
  int o_flags; //< O_ flags for open
};

/// stream output operator for Capn Proto objects
std::ostream& operator<<(std::ostream& out, const kj::StringTree& str) {
  str.visit([&out] (auto str) { out << str.begin(); });
  return out;
}

/// copy addresses, starting with the given destination iterator
template <class Iter>
Iter copy_addrs(Iter first,
                const std::vector<std::string>& rdma_addrs,
                const std::vector<std::string>& ip_addrs)
{
  auto addr = first;
  for (auto& name : rdma_addrs) {
    addr->setType(proto::net::Address::Type::RDMA);
    addr->setName(name);
    ++addr;
  }
  for (auto& name : ip_addrs) {
    addr->setType(proto::net::Address::Type::IP);
    addr->setName(name);
    ++addr;
  }
  return addr;
}

/// create an empty OsdMap and write it to the file
int osdmap_create(int fd, std::vector<std::string>&& args,
                  bpo::variables_map& cfg)
{
  capnp::MallocMessageBuilder builder;
  auto osdmap = builder.initRoot<proto::osd::OsdMap>();
  writePackedMessageToFd(fd, builder);

  std::cout << "Successfully created:\n\n" << osdmap.toString() << std::endl;
  return EXIT_SUCCESS;
}

/// decode the OsdMap and print its contents
int osdmap_show(int fd, std::vector<std::string>&& args,
                bpo::variables_map& cfg)
{
  capnp::PackedFdMessageReader reader{fd};
  auto osdmap = reader.getRoot<proto::osd::OsdMap>();
  std::cout << osdmap.toString() << std::endl;
  return EXIT_SUCCESS;
}

/// add an osd entry to an existing OsdMap
int osdmap_add_osd(int fd, std::vector<std::string>&& args,
                   bpo::variables_map& cfg)
{
  // command line options
  uint32_t id;
  std::vector<std::string> rdma_addrs;
  std::vector<std::string> ip_addrs;

  bpo::options_description options("Options for add-osd");
  options.add_options()
      ("osd", bpo::value(&id), "Osd id")
      ("rdma-address", bpo::value(&rdma_addrs), "RDMA address")
      ("ip-address", bpo::value(&ip_addrs), "IP address")
  ;
  if (cfg.count("help")) {
    return usage(options);
  }

  bpo::store(bpo::command_line_parser(args).options(options).run(), cfg);
  bpo::notify(cfg);

  if (!cfg.count("osd")) {
    std::cerr << "add-osd command missing --osd argument." << std::endl;
    return EXIT_FAILURE;
  }
  if (rdma_addrs.empty() && ip_addrs.empty()) {
    std::cerr << "add-osd command missing an address argument." << std::endl;
    return EXIT_FAILURE;
  }

  // read in the original osdmap
  capnp::PackedFdMessageReader reader{fd};
  auto orig = reader.getRoot<proto::osd::OsdMap>();

  auto entries = orig.getEntries();
  auto osd_cmp = [id] (auto osd, auto id) { return osd.getId() < id; };

  auto existing = std::lower_bound(entries.begin(), entries.end(), id, osd_cmp);
  if (existing != entries.end() && id == existing->getId()) {
    std::cerr << "add-osd command found existing osd " << id << std::endl;
    return EXIT_FAILURE;
  }

  // copy it into a builder
  capnp::MallocMessageBuilder builder;
  auto osdmap = builder.initRoot<proto::osd::OsdMap>();
  osdmap.setEpoch(orig.getEpoch() + 1);

  auto new_entries = osdmap.initEntries(entries.size() + 1);
  uint new_index = 0;

  // copy entries before the insert
  auto e = entries.begin();
  for (; e != existing; ++e) {
    new_entries.setWithCaveats(new_index++, *e);
  }

  // initialize the new osd entry in its sorted position
  auto osd = new_entries[new_index++];
  osd.setId(id);

  // copy in the addresses
  auto addrs = osd.initAddresses(rdma_addrs.size() + ip_addrs.size());
  auto last = copy_addrs(addrs.begin(), rdma_addrs, ip_addrs);
  Ensures(last == addrs.end());

  // copy entries after the insert
  for (; e != entries.end(); ++e) {
    new_entries.setWithCaveats(new_index++, *e);
  }
  Ensures(new_index == new_entries.size());

  // rewind the file pointer and overwrite the file
  auto offset = ::lseek(fd, 0, SEEK_SET);
  throw_system_error_on(offset == (off_t)-1, "lseek");

  writePackedMessageToFd(fd, builder);

  std::cout << "Added osd " << id << ".\n\n"
      << osdmap.toString() << std::endl;
  return EXIT_SUCCESS;
}

/// remove an osd entry from an existing OsdMap
int osdmap_remove_osd(int fd, std::vector<std::string>&& args,
                      bpo::variables_map& cfg)
{
  // command line options
  uint32_t id;

  bpo::options_description options("Options for remove-osd");
  options.add_options()
      ("osd", bpo::value(&id), "Osd id")
  ;
  if (cfg.count("help")) {
    return usage(options);
  }

  bpo::store(bpo::command_line_parser(args).options(options).run(), cfg);
  bpo::notify(cfg);

  if (!cfg.count("osd")) {
    std::cerr << "remove-osd command missing --osd argument." << std::endl;
    return EXIT_FAILURE;
  }

  // read in the original osdmap
  capnp::PackedFdMessageReader reader{fd};
  auto orig = reader.getRoot<proto::osd::OsdMap>();

  // make sure the entry exists
  auto entries = orig.getEntries();
  auto osd_cmp = [id] (auto osd, auto id) { return osd.getId() < id; };

  auto existing = std::lower_bound(entries.begin(), entries.end(), id, osd_cmp);
  if (existing == entries.end() || id != existing->getId()) {
    std::cerr << "remove-osd command found no osd " << id << std::endl;
    return EXIT_FAILURE;
  }

  // copy it into a builder
  capnp::MallocMessageBuilder builder;
  auto osdmap = builder.initRoot<proto::osd::OsdMap>();
  osdmap.setEpoch(orig.getEpoch() + 1);

  // copy all but the removed osd
  auto new_entries = osdmap.initEntries(entries.size() - 1);
  uint new_index = 0;
  for (auto e = entries.begin(); e != entries.end(); ++e) {
    if (e != existing)
      new_entries.setWithCaveats(new_index++, *e);
  }
  Ensures(new_index == new_entries.size());

  // rewind the file pointer and overwrite the file
  auto offset = ::lseek(fd, 0, SEEK_SET);
  throw_system_error_on(offset == (off_t)-1, "lseek");

  writePackedMessageToFd(fd, builder);

  std::cout << "Removed osd " << id << ".\n\n"
      << osdmap.toString() << std::endl;
  return EXIT_SUCCESS;
}

/// add addresses to an existing osd entry
int osdmap_add_addrs(int fd, std::vector<std::string>&& args,
                     bpo::variables_map& cfg)
{
  // command line options
  uint32_t id;
  std::vector<std::string> rdma_addrs;
  std::vector<std::string> ip_addrs;

  bpo::options_description options("Options for add-addrs");
  options.add_options()
      ("osd", bpo::value(&id), "Osd id")
      ("rdma-address", bpo::value(&rdma_addrs), "RDMA address")
      ("ip-address", bpo::value(&ip_addrs), "IP address")
  ;
  if (cfg.count("help")) {
    return usage(options);
  }

  bpo::store(bpo::command_line_parser(args).options(options).run(), cfg);
  bpo::notify(cfg);

  if (!cfg.count("osd")) {
    std::cerr << "add-addrs command missing --osd argument." << std::endl;
    return EXIT_FAILURE;
  }
  if (rdma_addrs.empty() && ip_addrs.empty()) {
    std::cerr << "add-addrs command missing an address argument." << std::endl;
    return EXIT_FAILURE;
  }

  // read in the original osdmap
  capnp::PackedFdMessageReader reader{fd};
  auto orig = reader.getRoot<proto::osd::OsdMap>();

  // make sure the entry exists
  auto entries = orig.getEntries();
  auto osd_cmp = [id] (auto osd, auto id) { return osd.getId() < id; };

  auto existing = std::lower_bound(entries.begin(), entries.end(), id, osd_cmp);
  if (existing == entries.end() || id != existing->getId()) {
    std::cerr << "add-addrs command found no osd " << id << std::endl;
    return EXIT_FAILURE;
  }
  auto existing_index = existing - entries.begin();

  // check for duplicates
  {
    auto addrs = existing->getAddresses();
    for (auto& a : rdma_addrs) {
      auto cmp = [&a] (auto addr) { return addr.getName() == a; };
      if (std::find_if(addrs.begin(), addrs.end(), cmp) != addrs.end()) {
        std::cerr << "add-addrs command found existing address "
            << a << " in osd " << id << std::endl;
        return EXIT_FAILURE;
      }
    }
    for (auto& a : ip_addrs) {
      auto cmp = [&a] (auto addr) { return addr.getName() == a; };
      if (std::find_if(addrs.begin(), addrs.end(), cmp) != addrs.end()) {
        std::cerr << "add-addrs command found existing address "
            << a << " in osd " << id << std::endl;
        return EXIT_FAILURE;
      }
    }
  }

  // copy it into a builder
  capnp::MallocMessageBuilder builder;
  builder.setRoot(orig);
  auto osdmap = builder.getRoot<proto::osd::OsdMap>();
  osdmap.setEpoch(orig.getEpoch() + 1);

  auto osd = osdmap.getEntries()[existing_index];
  {
    using AddrList = capnp::List<proto::net::Address>;
    auto count = rdma_addrs.size() + ip_addrs.size();
    auto list = builder.getOrphanage().newOrphan<AddrList>(count);

    auto addrs = list.get();
    auto last = copy_addrs(addrs.begin(), rdma_addrs, ip_addrs);
    Ensures(last == addrs.end());

    // append the entry to the existing list
    AddrList::Reader lists[] = { existing->getAddresses(), list.getReader() };
    auto array = kj::ArrayPtr<AddrList::Reader>{lists};
    osd.adoptAddresses(builder.getOrphanage().newOrphanConcat(array));
  }

  // rewind the file pointer and overwrite the file
  auto offset = ::lseek(fd, 0, SEEK_SET);
  throw_system_error_on(offset == (off_t)-1, "lseek");

  writePackedMessageToFd(fd, builder);

  std::cout << "Added addresses to osd " << id << ".\n\n"
      << osd.toString() << std::endl;
  return EXIT_SUCCESS;
}

/// remove addresses from an existing osd entry
int osdmap_remove_addrs(int fd, std::vector<std::string>&& args,
                        bpo::variables_map& cfg)
{
  // command line options
  uint32_t id;
  std::vector<std::string> rdma_addrs;
  std::vector<std::string> ip_addrs;

  bpo::options_description options("Options for remove-addrs");
  options.add_options()
      ("osd", bpo::value(&id), "Osd id")
      ("rdma-address", bpo::value(&rdma_addrs), "RDMA address")
      ("ip-address", bpo::value(&ip_addrs), "IP address")
  ;
  if (cfg.count("help")) {
    return usage(options);
  }

  bpo::store(bpo::command_line_parser(args).options(options).run(), cfg);
  bpo::notify(cfg);

  if (!cfg.count("osd")) {
    std::cerr << "remove-addrs missing --osd argument." << std::endl;
    return EXIT_FAILURE;
  }
  if (rdma_addrs.empty() && ip_addrs.empty()) {
    std::cerr << "remove-addrs missing an address argument." << std::endl;
    return EXIT_FAILURE;
  }

  // read in the original osdmap
  capnp::PackedFdMessageReader reader{fd};
  auto orig = reader.getRoot<proto::osd::OsdMap>();

  // make sure the entry exists
  auto entries = orig.getEntries();
  auto osd_cmp = [id] (auto osd, auto id) { return osd.getId() < id; };

  auto existing = std::lower_bound(entries.begin(), entries.end(), id, osd_cmp);
  if (existing == entries.end() || id != existing->getId()) {
    std::cerr << "remove-addrs found no osd " << id << std::endl;
    return EXIT_FAILURE;
  }
  auto existing_index = existing - entries.begin();

  auto addrs = existing->getAddresses();
  using AddrType = proto::net::Address::Type;

  std::vector<uint> matches;
  matches.reserve(rdma_addrs.size() + ip_addrs.size());

  for (auto& a : rdma_addrs) {
    auto cmp = [&a] (auto addr) {
      return addr.getType() == AddrType::RDMA && addr.getName() == a;
    };
    auto addr = std::find_if(addrs.begin(), addrs.end(), cmp);
    if (addr == addrs.end()) {
      std::cerr << "remove-addrs found no rdma address "
          << a << " in osd " << id << std::endl;
      return EXIT_FAILURE;
    }
    matches.push_back(addr - addrs.begin());
  }

  for (auto& a : ip_addrs) {
    auto cmp = [&a] (auto addr) {
      return addr.getType() == AddrType::IP && addr.getName() == a;
    };
    auto addr = std::find_if(addrs.begin(), addrs.end(), cmp);
    if (addr == addrs.end()) {
      std::cerr << "remove-addrs found no ip address "
          << a << " in osd " << id << std::endl;
      return EXIT_FAILURE;
    }
    matches.push_back(addr - addrs.begin());
  }

  // copy it into a builder
  capnp::MallocMessageBuilder builder;
  builder.setRoot(orig);
  auto osdmap = builder.getRoot<proto::osd::OsdMap>();
  osdmap.setEpoch(orig.getEpoch() + 1);

  auto osd = osdmap.getEntries()[existing_index];

  // copy all but the removed addrs
  auto new_addrs = osd.initAddresses(addrs.size() - matches.size());
  uint new_index = 0;
  for (uint i = 0; i < addrs.size(); i++) {
    if (std::find(matches.begin(), matches.end(), i) == matches.end())
      new_addrs.setWithCaveats(new_index++, addrs[i]);
  }
  Ensures(new_index == new_addrs.size());

  // rewind the file pointer and overwrite the file
  auto offset = ::lseek(fd, 0, SEEK_SET);
  throw_system_error_on(offset == (off_t)-1, "lseek");

  writePackedMessageToFd(fd, builder);

  std::cout << "Removed addresses from osd " << id << ".\n\n"
      << osd.toString() << std::endl;
  return EXIT_SUCCESS;
}

} // anonymous namespace

int main(int argc, char** argv)
{
  // command line options
  std::string command;
  std::string filename;

  bpo::options_description options("Options");
  options.add_options()
      ("command", bpo::value(&command), "command")
      ("filename", bpo::value(&filename), "osdmap filename")
      ("help,h", "Show this help message")
  ;

  bpo::positional_options_description positional;
  positional.add("command", 1);
  positional.add("filename", 1);

  bpo::variables_map cfg;
  std::vector<std::string> extra_args; // unparsed args for command
  try {
    auto parsed = bpo::command_line_parser(argc, argv)
        .options(options)
        .positional(positional)
        .allow_unregistered()
        .run();
    bpo::store(parsed, cfg);

    // collect unrecognized arguments for the command
    extra_args = bpo::collect_unrecognized(parsed.options,
                                           bpo::exclude_positional);
  } catch (bpo::error& e) {
    std::cerr << "Error: " << e.what() << "\n\n";
    return usage(options);
  }
  bpo::notify(cfg);

  // commands
  auto commands = std::map<std::string, Command>{
    {"show", {osdmap_show, O_RDONLY}},
    {"create", {osdmap_create, O_RDWR | O_CREAT}},
    {"add-osd", {osdmap_add_osd, O_RDWR}},
    {"remove-osd", {osdmap_remove_osd, O_RDWR}},
    {"add-addrs", {osdmap_add_addrs, O_RDWR}},
    {"remove-addrs", {osdmap_remove_addrs, O_RDWR}},
  };

  if (command.empty()) {
    usage(options);
    std::cerr << "Valid commands are: ";
    for (auto cmd = commands.begin(); cmd != commands.end(); ++cmd) {
      if (cmd != commands.begin())
        std::cerr << ", ";
      std::cerr << cmd->first;
    }
    std::cerr << std::endl;
    return EXIT_FAILURE;
  }
  // pass --help through to the command
  if (filename.empty() && !cfg.count("help")) {
    return usage(options);
  }

  auto cmd = commands.find(command);
  if (cmd == commands.end()) {
    std::cerr << "Unrecognized command: " << command << std::endl;
    return EXIT_FAILURE;
  }

  if (cfg.count("help")) {
    return cmd->second.func(0, std::move(extra_args), cfg);
  }

  // open the file
  auto fd = ::open(filename.c_str(), cmd->second.o_flags, 0644);
  try {
    throw_system_error_on(fd < 0, "open");
  } catch (std::exception& e) {
    std::cerr << "Failed to open " << filename.c_str()
        << ": " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  kj::AutoCloseFd close(fd);

  try {
    // run the command
    return cmd->second.func(fd, std::move(extra_args), cfg);
  } catch (bpo::error& e) {
    std::cerr << "Error: " << e.what()
        << "\n\nTry --help " << cmd->first << std::endl;
  } catch (std::exception& e) {
    std::cerr << "Failed with exception: " << e.what() << std::endl;
  }
  return EXIT_FAILURE;
}
