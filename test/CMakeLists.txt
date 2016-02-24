enable_testing()

# unit tests
add_executable(test_messenger test_messenger.cc $<TARGET_OBJECTS:messenger>)
target_link_libraries(test_messenger Seastar::Seastar proto)
add_test(Messenger test_messenger)

add_executable(test_memstore test_memstore.cc $<TARGET_OBJECTS:store> $<TARGET_OBJECTS:store_mem>
  $<TARGET_OBJECTS:xxhash>)
target_link_libraries(test_memstore Seastar::Seastar)
add_test(MemStore test_memstore)
