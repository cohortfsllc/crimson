set(messenger_srcs
	direct_messenger.cc
	socket_messenger.cc
	)

# build as an object library
add_library(messenger_objs OBJECT ${messenger_srcs})
target_compile_options(messenger_objs PRIVATE ${SEASTAR_COMPILE_OPTIONS})
target_include_directories(messenger_objs PRIVATE ${SEASTAR_INCLUDE_DIRS} ${CAPNP_INCLUDE_DIRS})

# wrap in an interface library to attach dependencies
add_library(messenger INTERFACE)
set_target_properties(messenger PROPERTIES
	INTERFACE_SOURCES $<TARGET_OBJECTS:messenger_objs>)
target_include_directories(messenger INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(messenger INTERFACE Seastar::Seastar Capnp::Lite)

set(messenger_public_headers
	direct_messenger.h
	messenger.h
	socket_messenger.h
	)
install(FILES ${messenger_public_headers} DESTINATION include/crimson)
