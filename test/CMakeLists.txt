enable_testing()
add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND})

# use gtest
find_package(GTest REQUIRED)
add_library(GTest::GTest INTERFACE IMPORTED)
set_target_properties(GTest::GTest PROPERTIES
	INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE_DIRS}"
	INTERFACE_LINK_LIBRARIES "${GTEST_LIBRARIES};${GTEST_MAIN_LIBRARIES}")

# unit tests
