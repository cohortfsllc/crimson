set(osd_srcs
	capn_connection.cc
	)
add_library(osd ${osd_srcs})
target_include_directories(osd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(osd Seastar::Seastar proto)

add_executable(crimson crimson.cc)
target_link_libraries(crimson osd)
install(TARGETS crimson DESTINATION bin)
