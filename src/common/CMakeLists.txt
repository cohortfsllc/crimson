add_library(xxhash OBJECT ${CMAKE_SOURCE_DIR}/src/xxHash/xxhash.c)
target_compile_options(xxhash PUBLIC)
