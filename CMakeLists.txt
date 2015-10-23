project(crimson)
cmake_minimum_required(VERSION 3.1)
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules)

set(CMAKE_CXX_STANDARD 14) # though Seastar adds -std=gnu++1y

find_package(Seastar REQUIRED)

# crimson source
add_subdirectory(src)
