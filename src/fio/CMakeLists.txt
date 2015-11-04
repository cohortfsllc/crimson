add_library(crimson-fio STATIC fio-engine.cc)
target_link_libraries(crimson-fio Seastar::Seastar)
install(TARGETS crimson-fio DESTINATION lib)
install(FILES fio-engine.h DESTINATION include/crimson)
