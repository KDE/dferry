# - Try to find tinyxml2
# Once done this will define
#  LIBTINYXML2_FOUND - System has tinyxml2
#  LIBTINYXML2_INCLUDE_DIRS - The tinyxml2 include directories
#  LIBTINYXML2_LIBRARIES - The libraries needed to use tinyxml2
#  LIBTINYXML2_DEFINITIONS - Compiler switches required for using tinyml2

find_package(PkgConfig)
pkg_check_modules(PC_LIBTINYXML2 QUIET libtinyxml2)
set(LIBTINYXML2_DEFINITIONS ${PC_LIBTINYXML2_CFLAGS_OTHER})

find_path(LIBTINYXML2_INCLUDE_DIR tinyxml2.h
          HINTS ${PC_LIBTINYXML2_INCLUDEDIR} ${PC_LIBTINYXML2_INCLUDE_DIRS}
          PATH_SUFFIXES libtinyxml2)

find_library(LIBTINYXML2_LIBRARY NAMES tinyxml2 libtinyxml2
             HINTS ${PC_LIBTINYXML2_LIBDIR} ${PC_LIBTINYXML2_LIBRARY_DIRS})

set(LIBTINYXML2_LIBRARIES ${LIBTINYXML2_LIBRARY})
set(LIBTINYXML2_INCLUDE_DIRS ${LIBTINYXML2_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set LIBTINYXML2_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(LibTinyxml2  DEFAULT_MSG
                                  LIBTINYXML2_LIBRARY LIBTINYXML2_INCLUDE_DIR)

mark_as_advanced(LIBTINYXML2_INCLUDE_DIR LIBTINYXML2_LIBRARY)
