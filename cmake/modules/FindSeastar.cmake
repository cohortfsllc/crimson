# - Try to find Seastar
#
# Seastar has a lot of required cflags and ldflags, so a seastar.pc file
# is required for pkg-config.
#
# The Seastar build process generates a seastar.pc in two directories:
#   ${SEASTAR_ROOT}/build/release
#   ${SEASTAR_ROOT}/build/debug
#
# To select one, use the PKG_CONFIG_PATH environment variable:
#   PKG_CONFIG_PATH=~/seastar/build/release cmake ..
#
# Once done this will define
#  SEASTAR_FOUND - System has Seastar
#  SEASTAR_COMPILE_OPTIONS - Compiler options required for using Seastar
#  SEASTAR_INCLUDE_DIRS - The Seastar include directories
#  SEASTAR_LIBRARIES - The libraries needed to use Seastar
#
# A Seastar::Seastar interface library target
#

find_package(PkgConfig)
pkg_check_modules(PC_SEASTAR QUIET seastar)

if(PC_SEASTAR_STATIC_FOUND)
	set(SEASTAR_COMPILE_OPTIONS "${PC_SEASTAR_STATIC_CFLAGS_OTHER}")
	set(SEASTAR_INCLUDE_DIRS "${PC_SEASTAR_STATIC_INCLUDE_DIRS}")
	set(SEASTAR_LIBRARIES "${PC_SEASTAR_STATIC_LDFLAGS}")
elseif(PC_SEASTAR_FOUND)
	set(SEASTAR_COMPILE_OPTIONS "${PC_SEASTAR_CFLAGS_OTHER}")
	set(SEASTAR_INCLUDE_DIRS "${PC_SEASTAR_STATIC_INCLUDE_DIRS}")
	set(SEASTAR_LIBRARIES "${PC_SEASTAR_LDFLAGS}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Seastar DEFAULT_MSG
	SEASTAR_INCLUDE_DIRS SEASTAR_COMPILE_OPTIONS SEASTAR_LIBRARIES)

mark_as_advanced(SEASTAR_COMPILE_OPTIONS SEASTAR_INCLUDE_DIRS SEASTAR_LIBRARIES)

if(SEASTAR_FOUND AND NOT TARGET Seastar::Seastar)
	# filter out some cflags
	list(FIND SEASTAR_COMPILE_OPTIONS "-fvisibility=hidden" hidden_index)
	if(NOT hidden_index EQUAL -1)
		list(REMOVE_AT SEASTAR_COMPILE_OPTIONS ${hidden_index})
	endif()

	add_library(Seastar::Seastar INTERFACE IMPORTED)
	set_target_properties(Seastar::Seastar PROPERTIES
		INTERFACE_COMPILE_OPTIONS "${SEASTAR_COMPILE_OPTIONS}"
		INTERFACE_INCLUDE_DIRECTORIES "${SEASTAR_INCLUDE_DIRS}"
		INTERFACE_LINK_LIBRARIES "${SEASTAR_LIBRARIES}")
endif()
