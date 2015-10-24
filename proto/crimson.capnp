@0x950b1ad72653a21b;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("crimson::proto");

using OsdRead = import "osd_read.capnp";
using OsdWrite = import "osd_write.capnp";

struct Header {
	sequence @0 :UInt32;
}

struct Message {
	header @0 :Header;
	union {
		osdRead @1 :OsdRead.Args;
		osdReadReply @2 :OsdRead.Res;

		osdWrite @3 :OsdWrite.Args;
		osdWriteReply @4 :OsdWrite.Res;
	}
}

