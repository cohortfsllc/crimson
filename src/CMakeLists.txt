add_subdirectory(msg)

add_executable(crimson crimson.cc $<TARGET_OBJECTS:messenger>)
target_link_libraries(crimson Seastar::Seastar proto)
install(TARGETS crimson DESTINATION bin)
