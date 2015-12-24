add_executable(osdmaptool osdmaptool.cc)
target_link_libraries(osdmaptool Seastar::Seastar proto)
install(TARGETS osdmaptool DESTINATION bin)
