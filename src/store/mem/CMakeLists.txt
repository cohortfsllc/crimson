set(store_mem_srcs
        Store.cc)
add_library(store_mem OBJECT ${store_mem_srcs})
target_compile_options(store_mem PUBLIC ${SEASTAR_COMPILE_OPTIONS})
target_include_directories(store_mem PUBLIC ${SEASTAR_INCLUDE_DIRS})
