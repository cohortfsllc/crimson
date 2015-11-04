FIO Engines
===========

Project Crimson provides a crimson-fio library that adapts the fio engine
interface into one with a Seastar/futures-based view of the system.

This allows the fio workload generator to measure the performance of single
components in isolation or reduced configurations of the io pipeline. We use
the results to focus our optimization efforts and reason about the available
performance of the system as a whole.

Note that some rather invasive surgery to Seastar's reactor class was
needed to allow it to run safely in the fio thread. For this reason,
a special branch of our Seastar repository is required.

The fio engines themselves are built as part of the fio tree.

Building
--------

You will need

- Seastar

  Clone the 'poll' branch with

      git clone git://github.com/cohortfsllc/seastar.git -b poll

- crimson-fio

  This library is built with the rest of project crimson. Note the install
  location so it can be passed to fio.

- fio

  clone the 'crimson' branch with

      git clone git://github.com/cohortfsllc/fio.git -b crimson

  Configure with

      PKG_CONFIG_PATH=/path/to/seastar/build/release ./configure --extra-cflags="-I/path/to/crimson/install/include" --extra-ldflags="-L/path/to/crimson/install/lib"

  If configure prints 'Crimson    no', check config.log for errors and
  modify the cflags/ldflags as needed.

  Build with

      make

Running
-------

The crimson fio job files are located in the fio/examples directory. Select
one and run it with

    ./fio examples/crimson-noop.fio
