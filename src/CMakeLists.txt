add_subdirectory(msg)

set(osd_srcs
	osd.cc
	)
add_library(crimson-osd ${osd_srcs})
target_include_directories(crimson-osd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(crimson-osd proto messenger)
install(TARGETS crimson-osd DESTINATION lib)
install(FILES osd.h DESTINATION include/crimson)

add_executable(crimson crimson.cc)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(crimson Seastar::Seastar)
install(TARGETS crimson DESTINATION bin)

if(WITH_FIO)
	add_subdirectory(fio)
endif(WITH_FIO)
