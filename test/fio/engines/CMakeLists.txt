add_library(fio_engine OBJECT engine.cc)
target_compile_options(fio_engine PRIVATE -fPIC)
target_include_directories(fio_engine PRIVATE ${SEASTAR_INCLUDE_DIRS} ${FIO_INCLUDE_DIR})

add_library(fio_crimson_osd SHARED fio_crimson_osd.cc $<TARGET_OBJECTS:fio_engine>)
target_include_directories(fio_crimson_osd PRIVATE ${FIO_INCLUDE_DIR})
target_link_libraries(fio_crimson_osd Seastar::Seastar proto)
install(TARGETS fio_crimson_osd DESTINATION lib)
