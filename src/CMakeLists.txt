add_executable(crimson crimson.cc)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(crimson Seastar::Seastar)
install(TARGETS crimson DESTINATION bin)
