add_executable(osdmaptool osdmaptool.cc)
target_link_libraries(osdmaptool Seastar::Seastar proto)
target_include_directories(osdmaptool PRIVATE
	${CMAKE_SOURCE_DIR}/src/GSL/include
	${CMAKE_SOURCE_DIR}/src/cxx_function)
install(TARGETS osdmaptool DESTINATION bin)
