add_library(fio_engine OBJECT engine.cc)
target_compile_options(fio_engine PRIVATE -fPIC)
target_include_directories(fio_engine PRIVATE ${SEASTAR_INCLUDE_DIRS} ${FIO_INCLUDE_DIR})
