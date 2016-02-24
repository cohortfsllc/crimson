add_subdirectory(mem)

set(store_srcs Store.cc)
add_library(store OBJECT ${store_srcs})
target_compile_options(store PUBLIC ${SEASTAR_COMPILE_OPTIONS})
target_include_directories(store PUBLIC ${SEASTAR_INCLUDE_DIRS})
