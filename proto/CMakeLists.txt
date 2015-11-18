# compile .capnp files into a library
set(proto_files
	crimson.capnp
	net.capnp
	osd_read.capnp
	osd_map.capnp
	osd_write.capnp
	)

set(CAPNPC_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
capnp_generate_cpp(proto_srcs proto_headers ${proto_files})

add_library(proto ${proto_srcs})
target_link_libraries(proto Capnp::Lite)
target_include_directories(proto PUBLIC ${CAPNPC_OUTPUT_DIR})
