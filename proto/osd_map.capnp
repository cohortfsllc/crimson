@0x96ae8f35e7e591fb;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("crimson::proto::osd");

using Net = import "net.capnp";

struct OsdMap {
	epoch @0 :UInt32;
	entries @1 :List(Entry);

	struct Entry {
		id @0 :UInt32;
		addresses @1 :List(Net.Address);
	}
}
