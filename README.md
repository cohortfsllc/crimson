Crimson
=======

Project Crimson is a prototype to demonstrate available performance in
a design that can be extended to support the requirements of a Ceph OSD.

Introduction
------------

Our goals are to optimize the 'top half' of the Ceph OSD, providing a
single fast path from network to memory. Using this, we can then
reason effectively about the performance of other design decisions,
such as Newstore, DM-Clock, and replication and consistency tradeoffs.

Development
-----------

For our first, basic prototype, we shall:

- Base our design on the Seastar framework
- Include a high performance memory backend
- Use Cap'n Proto/Flatbuffers style messaging (we aren't committing to
  any library, this refers to a family of serialziation formats that
  are low overhead and work well with RDMA.)
- Support both Placement Group and Flexible Placement systems
- We shall *not* implement snapshots or replication in this pass. Instead
  we shall include the placement group total ordering, and add null
  functions for both. (For reasoning about performance we could
  configure these to have specific time costs to simulate them, and
  help us figure out where best and how to insert them.)

Building
--------

You will need

- cmake
- Seastar  
  it may be fetched with

        git clone git://github.com/scylladb/seastar.git

  and built with

        ./configure.py
        ninja-build

  For more info read Seastar's install instructions.

This project uses git submodules. Before building, make sure to
execute

    git submodule update --init --recursive

Call CMake and build make. If you have seastar installed in a
nonstandard location, use a line like

    PKG_CONFIG_PATH=/path/to/seastar/build/release cmake /path/to/crimson

(Substitute `debug` for release if you wish.)

Future
------

Currently this is a prototype and tool to reason about performance. if
it proves useful it may have compatibility with Ceph wire protocol
added and grow into a high performance 'new OSD'.
