@0xbcc275d3d3385472;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("crimson::proto::osd::read");

struct Args {
	object @0 :Text;
	offset @1 :UInt64;
	length @2 :UInt64;
}

struct Res {
	errorCode @0 :UInt32;
	data @1 :Data;
}
