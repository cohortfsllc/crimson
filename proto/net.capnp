@0xef3b9c9014c8c3a8;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("crimson::proto::net");

struct Address {
	type @0 :Type;
	name @1 :Text;

	enum Type {
		ip @0;
		rdma @1;
	}
}
