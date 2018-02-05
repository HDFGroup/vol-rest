cmake_minimum_required (VERSION 3.1.0)
PROJECT (REST_VOL C)

# Locate cURL and YAJL before building
find_package(CURL)

#-----------------------------------------------------------------------------
# Instructions for use : Normal Build
#
# For standard build of the HDF5 REST VOL plugin.
# Run cmake using the REST VOL source tree to generate a build tree.
# Enable/Disable options according to requirements and
# set CMAKE_INSTALL_PREFIX to the required install path.
# Make install can be used to install all components for system-wide use.
#
if ("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_BINARY_DIR}")
    MESSAGE(FATAL_ERROR "\nERROR! ${PROJECT_NAME} DOES NOT SUPPORT IN SOURCE BUILDS!\n"
      "CMAKE_CURRENT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
      " == CMAKE_CURRENT_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}\n"
      "NEXT STEPS:\n"
      "(1) Delete the CMakeCache.txt file and the CMakeFiles/ directory\n"
      "     under the source directory for ${PROJECT_NAME}, otherwise you\n"
      "     will not be able to configure ${PROJECT_NAME} correctly!\n"
      "      * For example, on linux machines do:\n"
      "        $ rm -r CMakeCache.txt CMakeFiles/\n"
      "(2) Create a different directory and configure ${PROJECT_NAME} in that directory.\n"
      "      * For example, on linux machines do:\n"
      "        $ mkdir MY_BUILD\n"
      "        $ cd MY_BUILD\n"
      "        $ cmake [OPTIONS] ..\n"
      )
endif ()

#-----------------------------------------------------------------------------
# Instructions for use : Sub-Project Build
#
# To include HDF5 as a sub-project within another project.
# Set HDF5_EXTERNALLY_CONFIGURED to 1 in the parent project and
# supply values for the following variables...

#   # Export configuration to this export variable
#   set (HDF5_EXPORTED_TARGETS "paraview-targets")
#
#   # Setup all necessary overrides for zlib so that HDF5 uses our
#   # internally compiled zlib rather than any other version
#   if (HDF5_ENABLE_Z_LIB_SUPPORT)
#     # We must tell the main HDF5 library that it depends on our zlib
#     set (HDF5_LIB_DEPENDENCIES vtkzlib)
#     # Override the zlib header file
#     if (VTK_USE_SYSTEM_ZLIB)
#       set (H5_ZLIB_HEADER "zlib.h")
#     else (VTK_USE_SYSTEM_ZLIB)
#       set (H5_ZLIB_HEADER "vtk_zlib.h")
#       # Set vars that FindZlib would have set if used in sub project
#       set (ZLIB_INCLUDE_DIRS "${VTK_ZLIB_INCLUDE_DIRS}")
#       set (ZLIB_LIBRARIES vtkzlib)
#     endif (VTK_USE_SYSTEM_ZLIB)
#   endif (HDF5_ENABLE_Z_LIB_SUPPORT)
#
#   # Add the sub project
#   add_subdirectory (Utilities/hdf5-1.8)
#-----------------------------------------------------------------------------

#-----------------------------------------------------------------------------
# Setup HDF5 as a sub-project
#-----------------------------------------------------------------------------
set (HDF5_EXTERNALLY_CONFIGURED 1)

set (HDF5_SOURCE_DIR "hdf5")

# HDF5_EXPORTED_TARGETS :
#   Set this to the name of the targets variable which controls exports
#   If unset (because parent project does not support/use the
#   install (EXPORT target...) syntax), then targets are not configured
#   for export during install.
set (HDF5_EXPORTED_TARGETS "rest_vol-targets")

# HDF5_LIB_DEPENDENCIES :
#   If the build of HDF5 libs is being customized, then rules for the
#   dependencies of the HDF5 libs may be 'incomplete', add additional
#   dependencies to this variable so that external projects pick them up
#set (HDF5_LIB_DEPENDENCIES )

# HDF5_EXTERNAL_LIB_PREFIX :
#   If the parent project needs to install hdf libraries, but avoid
#   name conflicts with system versions, then a prefix may be added
#   to ensure that the correct versions configured are used.
set (HDF5_EXTERNAL_LIB_PREFIX "rv")

# HDF5_INSTALL_BIN_DIR, HDF5_INSTALL_LIB_DIR, HDF5_INSTALL_INCLUDE_DIR, HDF5_INSTALL_DATA_DIR :
#   Customize the 'bin', 'lib', 'include', and 'share' installation directories.
set (HDF5_INSTALL_BIN_DIR )
set (HDF5_INSTALL_LIB_DIR )
set (HDF5_INSTALL_INCLUDE_DIR )
set (HDF5_INSTALL_DATA_DIR )

# HDF5_INSTALL_NO_DEVELOPMENT :
#   Set to true to skip installation of headers and CMake package files.
set (HDF5_INSTALL_NO_DEVELOPMENT TRUE)

# Add the HDF5 source as a sub-project
add_subdirectory(${HDF5_SOURCE_DIR})

if (NOT REST_VOL_EXPORTED_TARGETS)
  set (REST_VOL_EXPORTED_TARGETS "rest_vol-targets")
endif (NOT REST_VOL_EXPORTED_TARGETS)

#-----------------------------------------------------------------------------
# Set the core names of all the libraries
#-----------------------------------------------------------------------------
set (REST_VOL_LIB_CORENAME        "restvol")

#-----------------------------------------------------------------------------
# Set the target names of all the libraries
#-----------------------------------------------------------------------------
set (REST_VOL_LIB_TARGET          "${REST_VOL_LIB_CORENAME}-static")
set (REST_VOL_LIBSH_TARGET        "${REST_VOL_LIB_CORENAME}-shared")

#-----------------------------------------------------------------------------
# Define some CMake variables for use later in the project
#-----------------------------------------------------------------------------
set (REST_VOL_RESOURCES_DIR       ${REST_VOL_SOURCE_DIR}/config/cmake)
set (REST_VOL_RESOURCES_EXT_DIR   ${REST_VOL_SOURCE_DIR}/config/cmake_ext_mod)
set (REST_VOL_SRC_DIR             ${REST_VOL_SOURCE_DIR}/src)

if (NOT REST_VOL_INSTALL_BIN_DIR)
  set (REST_VOL_INSTALL_BIN_DIR bin)
endif (NOT REST_VOL_INSTALL_BIN_DIR)
if (NOT REST_VOL_INSTALL_LIB_DIR)
  if (APPLE)
    set (REST_VOL_INSTALL_FMWK_DIR ${CMAKE_INSTALL_FRAMEWORK_PREFIX})
  else (APPLE)
    set (REST_VOL_INSTALL_JAR_DIR lib)
  endif (APPLE)
  set (REST_VOL_INSTALL_LIB_DIR lib)
endif (NOT REST_VOL_INSTALL_LIB_DIR)
if (NOT REST_VOL_INSTALL_INCLUDE_DIR)
  set (REST_VOL_INSTALL_INCLUDE_DIR include)
endif (NOT REST_VOL_INSTALL_INCLUDE_DIR)
if (NOT REST_VOL_INSTALL_DATA_DIR)
  if (NOT WIN32)
    if (APPLE)
      if (REST_VOL_BUILD_FRAMEWORKS)
        set (REST_VOL_INSTALL_EXTRA_DIR ../SharedSupport)
      else (REST_VOL_BUILD_FRAMEWORKS)
        set (REST_VOL_INSTALL_EXTRA_DIR share)
      endif (REST_VOL_BUILD_FRAMEWORKS)
      set (REST_VOL_INSTALL_FWRK_DIR ${CMAKE_INSTALL_FRAMEWORK_PREFIX})
    endif (APPLE)
    set (REST_VOL_INSTALL_DATA_DIR share)
    set (REST_VOL_INSTALL_CMAKE_DIR share/cmake)
  else (NOT WIN32)
    set (REST_VOL_INSTALL_DATA_DIR ".")
    set (REST_VOL_INSTALL_CMAKE_DIR cmake)
  endif (NOT WIN32)
endif (NOT REST_VOL_INSTALL_DATA_DIR)

if(DEFINED ADDITIONAL_CMAKE_PREFIX_PATH AND EXISTS "${ADDITIONAL_CMAKE_PREFIX_PATH}")
  set (CMAKE_PREFIX_PATH ${ADDITIONAL_CMAKE_PREFIX_PATH} ${CMAKE_PREFIX_PATH})
endif(DEFINED ADDITIONAL_CMAKE_PREFIX_PATH AND EXISTS "${ADDITIONAL_CMAKE_PREFIX_PATH}")

#message (STATUS "VERSION: ${REST_VOL_RELEASE_VERSION}")

#-----------------------------------------------------------------------------
# parse the full soversion number from config/lt_vers.am and include in REST_VOL_SOVERS_INFO
#-----------------------------------------------------------------------------
file (READ ${REST_VOL_SOURCE_DIR}/config/lt_vers.am _lt_vers_am_contents)
string (REGEX REPLACE ".*LT_VERS_INTERFACE[ \t]+=[ \t]+([0-9]*).*$"
    "\\1" REST_VOL_SOVERS_INTERFACE ${_lt_vers_am_contents})
string (REGEX REPLACE ".*LT_VERS_REVISION[ \t]+=[ \t]+([0-9]*).*$"
    "\\1" REST_VOL_SOVERS_MINOR ${_lt_vers_am_contents})
string (REGEX REPLACE ".*LT_VERS_AGE[ \t]+=[ \t]+([0-9]*).*$"
    "\\1" REST_VOL_SOVERS_RELEASE ${_lt_vers_am_contents})
MATH (EXPR REST_VOL_SOVERS_MAJOR ${REST_VOL_SOVERS_INTERFACE}-${REST_VOL_SOVERS_RELEASE})
message (STATUS "SOVERSION: ${REST_VOL_SOVERS_MAJOR}.${REST_VOL_SOVERS_RELEASE}.${REST_VOL_SOVERS_MINOR}")

#-----------------------------------------------------------------------------
# Basic REST VOL stuff here
#-----------------------------------------------------------------------------
set (REST_VOL_PACKAGE "rest-vol")
set (REST_VOL_PACKAGE_NAME "REST VOL")
set (REST_VOL_PACKAGE_VERSION "${REST_VOL_VERS_MAJOR}.${REST_VOL_VERS_MINOR}.${REST_VOL_VERS_RELEASE}")
set (REST_VOL_PACKAGE_VERSION_MAJOR "${REST_VOL_VERS_MAJOR}.${REST_VOL_VERS_MINOR}")
set (REST_VOL_PACKAGE_VERSION_MINOR "${REST_VOL_VERS_RELEASE}")
if (NOT "${REST_VOL_VERS_SUBRELEASE}" STREQUAL "")
  set (REST_VOL_PACKAGE_VERSION_STRING "${REST_VOL_PACKAGE_VERSION}-${REST_VOL_VERS_SUBRELEASE}")
else (NOT "${REST_VOL_VERS_SUBRELEASE}" STREQUAL "")
  set (REST_VOL_PACKAGE_VERSION_STRING "${REST_VOL_PACKAGE_VERSION}")
endif (NOT "${REST_VOL_VERS_SUBRELEASE}" STREQUAL "")
set (REST_VOL_PACKAGE_SOVERSION "${REST_VOL_SOVERS_MAJOR}.${REST_VOL_SOVERS_RELEASE}.${REST_VOL_SOVERS_MINOR}")

set (REST_VOL_PACKAGE_STRING "${REST_VOL_PACKAGE_NAME} ${REST_VOL_PACKAGE_VERSION_STRING}")
set (REST_VOL_PACKAGE_TARNAME "${REST_VOL_PACKAGE}${REST_VOL_PACKAGE_EXT}")
set (REST_VOL_PACKAGE_URL "http://www.hdfgroup.org")
set (REST_VOL_PACKAGE_BUGREPORT "help@hdfgroup.org")

#-----------------------------------------------------------------------------
# Include some macros for reusable code
#-----------------------------------------------------------------------------

#-------------------------------------------------------------------------------
macro (REST_VOL_SET_LIB_OPTIONS libtarget libname libtype)
  set (LIB_OUT_NAME "${libname}")
  # SOVERSION passed in ARGN when shared
  if (${libtype} MATCHES "SHARED")
    if (ARGN)
      set (PACKAGE_SOVERSION ${ARGN})
    else (ARGN)
      set (PACKAGE_SOVERSION ${REST_VOL_PACKAGE_SOVERSION})
    endif (ARGN)
    if (WIN32)
      set (LIBREST_VOL_VERSION ${REST_VOL_PACKAGE_VERSION_MAJOR})
    else (WIN32)
      set (LIBREST_VOL_VERSION ${REST_VOL_PACKAGE_VERSION})
    endif (WIN32)
    set_target_properties (${libtarget} PROPERTIES VERSION ${LIBREST_VOL_VERSION})
    if (WIN32)
        set (${LIB_OUT_NAME} "${LIB_OUT_NAME}-${PACKAGE_SOVERSION}")
    else (WIN32)
        set_target_properties (${libtarget} PROPERTIES SOVERSION ${PACKAGE_SOVERSION})
    endif (WIN32)
  endif (${libtype} MATCHES "SHARED")
  REST_VOL_SET_LIB_OPTIONS (${libtarget} ${LIB_OUT_NAME} ${libtype})

  #-- Apple Specific install_name for libraries
  if (APPLE)
    option (REST_VOL_BUILD_WITH_INSTALL_NAME "Build with library install_name set to the installation path" OFF)
    if (REST_VOL_BUILD_WITH_INSTALL_NAME)
      set_target_properties (${libtarget} PROPERTIES
          LINK_FLAGS "-current_version ${REST_VOL_PACKAGE_VERSION} -compatibility_version ${REST_VOL_PACKAGE_VERSION}"
          INSTALL_NAME_DIR "${CMAKE_INSTALL_PREFIX}/lib"
          BUILD_WITH_INSTALL_RPATH ${REST_VOL_BUILD_WITH_INSTALL_NAME}
      )
    endif (REST_VOL_BUILD_WITH_INSTALL_NAME)
    if (REST_VOL_BUILD_FRAMEWORKS)
      if (${libtype} MATCHES "SHARED")
        # adapt target to build frameworks instead of dylibs
        set_target_properties(${libtarget} PROPERTIES
            XCODE_ATTRIBUTE_INSTALL_PATH "@rpath"
            FRAMEWORK TRUE
            FRAMEWORK_VERSION ${REST_VOL_PACKAGE_VERSION_MAJOR}
            MACOSX_FRAMEWORK_IDENTIFIER org.hdfgroup.${libtarget}
            MACOSX_FRAMEWORK_SHORT_VERSION_STRING ${REST_VOL_PACKAGE_VERSION_MAJOR}
            MACOSX_FRAMEWORK_BUNDLE_VERSION ${REST_VOL_PACKAGE_VERSION_MAJOR})
      endif (${libtype} MATCHES "SHARED")
    endif (REST_VOL_BUILD_FRAMEWORKS)
  endif (APPLE)

endmacro (REST_VOL_SET_LIB_OPTIONS)

#-----------------------------------------------------------------------------
# Setup output Directories
#-----------------------------------------------------------------------------
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY
    ${PROJECT_BINARY_DIR}/bin CACHE PATH "Single Directory for all Exectuables."
)
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY
    ${PROJECT_BINARY_DIR}/bin CACHE PATH "Single Directory for all Libraries"
)
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY
    ${PROJECT_BINARY_DIR}/bin CACHE PATH "Single Directory for all static libraries."
)

if (WIN32)
  set (CMAKE_TEST_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE})
else (WIN32)
  set (CMAKE_TEST_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
endif (WIN32)

#-----------------------------------------------------------------------------
# To include a library in the list exported by the project AT BUILD TIME,
# add it to this variable. This is NOT used by Make Install, but for projects
# which include the REST VOL as a sub-project within their build tree
#-----------------------------------------------------------------------------
set_global_variable (REST_VOL_LIBRARIES_TO_EXPORT "")
set (EXTERNAL_HEADER_LIST "")
set (EXTERNAL_LIBRARY_LIST "")
set (EXTERNAL_LIBRARYDLL_LIST "")

#-----------------------------------------------------------------------------
# Run all the CMake configuration tests for our build environment
#-----------------------------------------------------------------------------
#include (${REST_VOL_RESOURCES_DIR}/ConfigureChecks.cmake)

set (CMAKE_INCLUDE_CURRENT_DIR_IN_INTERFACE ON)

#-----------------------------------------------------------------------------
# Mac OS X Options
#-----------------------------------------------------------------------------
if (REST_VOL_BUILD_FRAMEWORKS AND NOT BUILD_SHARED_LIBS)
  set (BUILD_SHARED_LIBS ON CACHE BOOL "Build Shared Libraries")
endif (REST_VOL_BUILD_FRAMEWORKS AND NOT BUILD_SHARED_LIBS)

#-----------------------------------------------------------------------------
# Option to Build Shared and Static libs, default is static
#-----------------------------------------------------------------------------
option (BUILD_SHARED_LIBS "Build Shared Libraries" ON)
set (REST_VOL_ENABLE_SHARED_LIB NO)
if (BUILD_SHARED_LIBS)
  set (REST_VOL_ENABLE_SHARED_LIB YES)
  set (LINK_SHARED_LIBS ${LINK_LIBS})
endif (BUILD_SHARED_LIBS)
set (REST_VOL_ENABLE_STATIC_LIB YES)
set (CMAKE_POSITION_INDEPENDENT_CODE ON)

#-----------------------------------------------------------------------------
# Option to Build Static executables
#-----------------------------------------------------------------------------
option (BUILD_STATIC_EXECS "Build Static Executabless" OFF)
if (BUILD_STATIC_EXECS)
  if (NOT WIN32)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
  endif (NOT WIN32)
endif (BUILD_STATIC_EXECS)

#-----------------------------------------------------------------------------
# Option to use code coverage
#-----------------------------------------------------------------------------
option (REST_VOL_ENABLE_COVERAGE "Enable code coverage for Libraries and Programs" OFF)
if (REST_VOL_ENABLE_COVERAGE)
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 -fprofile-arcs -ftest-coverage")
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -fprofile-arcs -ftest-coverage")
  set (LDFLAGS "${LDFLAGS} -fprofile-arcs -ftest-coverage")
endif (REST_VOL_ENABLE_COVERAGE)

#-----------------------------------------------------------------------------
# Option to indicate enabling plugin debugging output
#-----------------------------------------------------------------------------
option (REST_VOL_ENABLE_DEBUG "Indicate that plugin debug output is used" OFF)
if (REST_VOL_ENABLE_DEBUG)
  set (REST_VOL_PLUGIN_DEBUG 1)
endif (REST_VOL_ENABLE_DEBUG)

#-----------------------------------------------------------------------------
# Option to indicate enabling cURL debugging output
#-----------------------------------------------------------------------------
option (REST_VOL_ENABLE_CURL_DEBUG "Indicate that cURL debug output is used" OFF)
if (REST_VOL_ENABLE_CURL_DEBUG)
  set (REST_VOL_CURL_DEBUG 1)
endif (REST_VOL_ENABLE_CURL_DEBUG)

#-----------------------------------------------------------------------------
# Option to indicate using a memory checker
#-----------------------------------------------------------------------------
option (REST_VOL_ENABLE_MEM_TRACKING "Indicate that a memory checker is used" OFF)
if (REST_VOL_ENABLE_MEM_TRACKING)
  set (REST_VOL_TRACK_MEM_USAGE 1)
endif (REST_VOL_ENABLE_MEM_TRACKING)

#-----------------------------------------------------------------------------
# When building utility executables that generate other (source) files :
# we make use of the following variables defined in the root CMakeLists.
# Certain systems may add /Debug or /Release to output paths
# and we need to call the executable from inside the CMake configuration
#-----------------------------------------------------------------------------
set (EXE_EXT "")
if (WIN32)
  set (EXE_EXT ".exe")
  if (NOT CYGWIN)
    add_definitions (-D_BIND_TO_CURRENT_VCLIBS_VERSION=1)
    add_definitions (-D_CRT_SECURE_NO_WARNINGS)
    add_definitions (-D_CONSOLE)
  endif (NOT CYGWIN)
endif (WIN32)

if (MSVC)
  set (CMAKE_MFC_FLAG 0)
  set (WIN_COMPILE_FLAGS "")
  set (WIN_LINK_FLAGS "")
endif (MSVC)

set (MAKE_SYSTEM)
if (CMAKE_BUILD_TOOL MATCHES "make")
  set (MAKE_SYSTEM 1)
endif (CMAKE_BUILD_TOOL MATCHES "make")

set (CFG_INIT "/${CMAKE_CFG_INTDIR}")
if (MAKE_SYSTEM)
  set (CFG_INIT "")
endif (MAKE_SYSTEM)

#-----------------------------------------------------------------------------
# Add some definitions for Debug Builds
#-----------------------------------------------------------------------------
if (CMAKE_BUILD_TYPE MATCHES Debug)
  add_definitions (-DDEBUG)
else (CMAKE_BUILD_TYPE MATCHES Debug)
  add_definitions (-DNDEBUG)
endif (CMAKE_BUILD_TYPE MATCHES Debug)

include (${REST_VOL_RESOURCES_DIR}/HDFCompilerFlags.cmake)

#-----------------------------------------------------------------------------
# All libs/tests/examples need the main include directories
#-----------------------------------------------------------------------------
INCLUDE_DIRECTORIES (${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

#-----------------------------------------------------------------------------
# Dashboard and Testing Settings
#-----------------------------------------------------------------------------
option (BUILD_TESTING "Build REST VOL Unit Testing" ON)
if (BUILD_TESTING)
  set (DART_TESTING_TIMEOUT 1200
      CACHE INTEGER
      "Timeout in seconds for each test (default 1200=20minutes)"
  )

  option (REST_VOL_TEST_EXPRESS "Control testing framework (0-3)" "0")
  mark_as_advanced (REST_VOL_TEST_EXPRESS)

  enable_testing ()
  include (CTest)

  include (${REST_VOL_SOURCE_DIR}/CTestConfig.cmake)
endif (BUILD_TESTING)

#-----------------------------------------------------------------------------
# Generate the rest_vol_config.h file containing user settings needed by compilation
#-----------------------------------------------------------------------------
configure_file (${REST_VOL_RESOURCES_DIR}/rv_cmake_config.h.in ${REST_VOL_SRC_DIR}/rest_vol_config.h @ONLY)

include (CMakeInstallation.cmake)
