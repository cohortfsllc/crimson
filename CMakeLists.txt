project(crimson)
cmake_minimum_required(VERSION 3.1)
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules)

set(CMAKE_CXX_STANDARD 14) # though Seastar adds -std=gnu++1y

find_package(Seastar REQUIRED)
find_package(CapnProto REQUIRED)

# protocol definitions
add_subdirectory(proto)

# crimson source
add_subdirectory(src)

# crimson tools
add_subdirectory(tools)

# tests
option(WITH_TESTS "Create a 'check' target to build and run tests" ON)
if(WITH_TESTS)
	include_directories(src)
	add_subdirectory(test)
endif(WITH_TESTS)
