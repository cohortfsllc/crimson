set(messenger_srcs
	direct_messenger.cc
	socket_messenger.cc
	)
add_library(messenger OBJECT ${messenger_srcs})
target_compile_options(messenger PUBLIC ${SEASTAR_COMPILE_OPTIONS})
target_include_directories(messenger PUBLIC ${SEASTAR_INCLUDE_DIRS} ${CAPNP_INCLUDE_DIRS})

set(messenger_public_headers
	direct_messenger.h
	messenger.h
	socket_messenger.h
	)
install(FILES ${messenger_public_headers} DESTINATION include/crimson)
