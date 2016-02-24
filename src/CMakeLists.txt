add_subdirectory(common)
add_subdirectory(msg)
add_subdirectory(store)

add_executable(crimson crimson.cc)
target_link_libraries(crimson Seastar::Seastar)
install(TARGETS crimson DESTINATION bin)
