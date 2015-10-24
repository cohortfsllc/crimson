@0xb88e5a3d5432dd8c;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("crimson::proto::osd::write");

using Flags = UInt32;

const onApply :Flags = 0x1;
const onCommit :Flags = 0x2;

struct Args {
	object @0 :Text;
	offset @1 :UInt64;
	length @2 :UInt64;
	data @3 :Data;
	flags @4 :Flags;
}

struct Res {
	union {
		errorCode @0 :UInt32;
		flags @1 :Flags;
	}
}
