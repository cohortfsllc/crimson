# - Find Fio
#
# FIO_INCLUDE_DIR - where to find fio.h
# FIO_FOUND - True if found.

find_path(FIO_INCLUDE_DIR fio.h NO_DEFAULT_PATH PATHS
	/usr/include
	/opt/local/include
	/usr/local/include
	)

# handle the QUIETLY and REQUIRED arguments and set FIO_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Fio DEFAULT_MSG FIO_INCLUDE_DIR)

mark_as_advanced(FIO_INCLUDE_DIR)
