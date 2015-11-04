enable_testing()
add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND})

# unit tests
add_executable(test_messenger EXCLUDE_FROM_ALL test_messenger.cc $<TARGET_OBJECTS:messenger>)
target_link_libraries(test_messenger Seastar::Seastar proto)
add_test(Messenger test_messenger)
add_dependencies(check test_messenger)
