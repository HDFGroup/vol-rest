# - Try to find libyajl
#
# The variable YAJL_USE_STATIC_LIBRARIES can be used to select
# the static YAJL library.
#
# Once done this will define the variables
#   YAJL_FOUND        - System has libyajl
#   YAJL_INCLUDE_DIR  - The libyajl include directory
#   YAJL_INCLUDE_DIRS - The libyajl include directories
#   YAJL_LIBRARIES    - The libraries needed to use libyajl
# and the targets
#   yajl              - shared or static YAJL library depending on the value of YAJL_USE_STATIC_LIBRARIES variable.
#   yajl-shared       - Dedicated shared YAJL library target
#   yajl-static       - Dedicated static YAJL library target

find_path(YAJL_INCLUDE_DIR yajl/yajl_version.h DOC "YAJL include directory")
find_library(YAJL_SHARED_LIBRARY NAMES yajl libyajl DOC "Shared YAJL library")
find_library(YAJL_STATIC_LIBRARY NAMES yajl_s libyajl_s DOC "Static YAJL library")

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set LIBYAJL_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(YAJL DEFAULT_MSG
                                  YAJL_SHARED_LIBRARY YAJL_STATIC_LIBRARY YAJL_INCLUDE_DIR)

if (YAJL_FOUND)
  add_library(yajl-shared SHARED IMPORTED)
  add_library(yajl-static STATIC IMPORTED)
  if (YAJL_USE_STATIC_LIBRARIES)
    add_library(yajl ALIAS yajl-static)
  else ()
    add_library(yajl ALIAS yajl-shared)
  endif ()
  set_target_properties(yajl-shared PROPERTIES
    IMPORTED_LOCATION "${YAJL_SHARED_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${YAJL_INCLUDE_DIR}"
  )
  set_target_properties(yajl-static PROPERTIES
    IMPORTED_LOCATION "${YAJL_STATIC_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${YAJL_INCLUDE_DIR}"
  )

  set(YAJL_INCLUDE_DIRS ${YAJL_INCLUDE_DIR})
  if (YAJL_USE_STATIC_LIBRARIES)
    set(YAJL_LIBRARIES ${YAJL_STATIC_LIBRARY})
  else ()
    set(YAJL_LIBRARIES ${YAJL_SHARED_LIBRARY})
  endif ()
endif ()

mark_as_advanced(YAJL_INCLUDE_DIR YAJL_SHARED_LIBRARY YAJL_STATIC_LIBRARY)
