#-------------------------------------------------#
# CMake Build Script for the HDF5 REST VOL plugin #
#-------------------------------------------------#

cmake_minimum_required (VERSION 3.10)
PROJECT (REST_VOL C)

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
# Set the core names of all the libraries
#-----------------------------------------------------------------------------
set (REST_VOL_LIB_CORENAME        "restvol")
set (REST_VOL_TEST_LIB_CORENAME   "restvol_test")

#-----------------------------------------------------------------------------
# Set the target names of all the libraries
#-----------------------------------------------------------------------------
set (REST_VOL_LIB_TARGET          "${REST_VOL_LIB_CORENAME}-static")
set (REST_VOL_LIBSH_TARGET        "${REST_VOL_LIB_CORENAME}-shared")
set (REST_VOL_TEST_LIB_TARGET     "${REST_VOL_TEST_LIB_CORENAME}-static")
set (REST_VOL_TEST_LIBSH_TARGET   "${REST_VOL_TEST_LIB_CORENAME}-shared")

#-----------------------------------------------------------------------------
# Define some CMake variables for use later in the project
#-----------------------------------------------------------------------------
set (REST_VOL_RESOURCES_DIR       ${REST_VOL_SOURCE_DIR}/config/cmake)
set (REST_VOL_RESOURCES_EXT_DIR   ${REST_VOL_SOURCE_DIR}/config/cmake_ext_mod)
set (REST_VOL_RESOURCES_MOD_DIR   ${REST_VOL_SOURCE_DIR}/config/cmake/modules)
set (REST_VOL_SRC_DIR             ${REST_VOL_SOURCE_DIR}/src)
set (REST_VOL_TEST_SRC_DIR        ${REST_VOL_SOURCE_DIR}/test)
set (REST_VOL_EXAMPLES_DIR        ${REST_VOL_SOURCE_DIR}/examples)

#-----------------------------------------------------------------------------
# Make sure the REST VOL headers are available
#-----------------------------------------------------------------------------
include_directories(${REST_VOL_SRC_DIR})
include_directories(${REST_VOL_BINARY_DIR})


#-----------------------------------------------------------------------------
# Find cURL and YAJL before building
#-----------------------------------------------------------------------------
find_package (CURL REQUIRED)
if (CURL_FOUND)
  include_directories(${CURL_INCLUDE_DIRS})
  set (LINK_LIBS ${LINK_LIBS} ${CURL_LIBRARIES})
endif ()

# Include custom module for finding YAJL
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${REST_VOL_RESOURCES_MOD_DIR}")

find_package (YAJL REQUIRED)
if (YAJL_FOUND)
  include_directories(${YAJL_INCLUDE_DIRS})
  set (LINK_LIBS ${LINK_LIBS} ${YAJL_LIBRARIES})
endif ()


#-----------------------------------------------------------------------------
# Targets built within this project are exported at Install time for use
# by other projects using FindH4H5.
#-----------------------------------------------------------------------------
if (NOT REST_VOL_EXPORTED_TARGETS)
  set (REST_VOL_EXPORTED_TARGETS "restvol-targets")
endif ()


if(DEFINED ADDITIONAL_CMAKE_PREFIX_PATH AND EXISTS "${ADDITIONAL_CMAKE_PREFIX_PATH}")
  set (CMAKE_PREFIX_PATH ${ADDITIONAL_CMAKE_PREFIX_PATH} ${CMAKE_PREFIX_PATH})
endif()


#-----------------------------------------------------------------------------
# Setup the install directories
#-----------------------------------------------------------------------------
if (NOT REST_VOL_INSTALL_BIN_DIR)
  set (REST_VOL_INSTALL_BIN_DIR bin)
endif ()
if (NOT REST_VOL_INSTALL_LIB_DIR)
  if (APPLE)
    set (REST_VOL_INSTALL_FMWK_DIR ${CMAKE_INSTALL_FRAMEWORK_PREFIX})
  else (APPLE)
    set (REST_VOL_INSTALL_JAR_DIR lib)
  endif ()
  set (REST_VOL_INSTALL_LIB_DIR lib)
endif ()
if (NOT REST_VOL_INSTALL_INCLUDE_DIR)
  set (REST_VOL_INSTALL_INCLUDE_DIR include)
endif ()
if (NOT REST_VOL_INSTALL_DATA_DIR)
  if (NOT WIN32)
    if (APPLE)
      if (REST_VOL_BUILD_FRAMEWORKS)
        set (REST_VOL_INSTALL_EXTRA_DIR ../SharedSupport)
      else (REST_VOL_BUILD_FRAMEWORKS)
        set (REST_VOL_INSTALL_EXTRA_DIR share)
      endif ()
      set (REST_VOL_INSTALL_FWRK_DIR ${CMAKE_INSTALL_FRAMEWORK_PREFIX})
    endif ()
    set (REST_VOL_INSTALL_DATA_DIR share)
    set (REST_VOL_INSTALL_CMAKE_DIR share/cmake)
  else (NOT WIN32)
    set (REST_VOL_INSTALL_DATA_DIR ".")
    set (REST_VOL_INSTALL_CMAKE_DIR cmake)
  endif ()
endif ()

#-------------------------------------------------------------------------------
# Parse the full version number from README.md and include in REST_VOL_VERS_INFO
#-------------------------------------------------------------------------------
file (READ ${REST_VOL_SOURCE_DIR}/README.md _readme_contents)
string (REGEX REPLACE ".*([0-9][0-9]*).[0-9][0-9]*.[0-9][0-9]*.*$"
    "\\1" REST_VOL_VERS_MAJOR ${_readme_contents})
string (REGEX REPLACE ".*[0-9][0-9]*.([0-9][0-9]*).[0-9][0-9]*.*$"
    "\\1" REST_VOL_VERS_MINOR ${_readme_contents})
string (REGEX REPLACE ".*[0-9][0-9]*.[0-9][0-9]*.([0-9][0-9]*).*$"
    "\\1" REST_VOL_VERS_RELEASE ${_readme_contents})
message (STATUS "VERSION: ${REST_VOL_VERS_MAJOR}.${REST_VOL_VERS_MINOR}.${REST_VOL_VERS_RELEASE}")


#-------------------------------------------------------------------------------
# Basic REST VOL stuff here
#-------------------------------------------------------------------------------
set (REST_VOL_PACKAGE "rest-vol")
set (REST_VOL_PACKAGE_NAME "REST_VOL")
set (REST_VOL_PACKAGE_VERSION "${REST_VOL_VERS_MAJOR}.${REST_VOL_VERS_MINOR}.${REST_VOL_VERS_RELEASE}")
set (REST_VOL_PACKAGE_VERSION_MAJOR "${REST_VOL_VERS_MAJOR}.${REST_VOL_VERS_MINOR}")
set (REST_VOL_PACKAGE_VERSION_MINOR "${REST_VOL_VERS_RELEASE}")
if (NOT "${REST_VOL_VERS_SUBRELEASE}" STREQUAL "")
  set (REST_VOL_PACKAGE_VERSION_STRING "${REST_VOL_PACKAGE_VERSION}-${REST_VOL_VERS_SUBRELEASE}")
else (NOT "${REST_VOL_VERS_SUBRELEASE}" STREQUAL "")
  set (REST_VOL_PACKAGE_VERSION_STRING "${REST_VOL_PACKAGE_VERSION}")
endif ()

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

#-------------------------------------------------------------------------------
macro (TARGET_C_PROPERTIES wintarget libtype addcompileflags addlinkflags)
  if (MSVC)
    TARGET_MSVC_PROPERTIES (${wintarget} ${libtype} "${addcompileflags} ${WIN_COMPILE_FLAGS}" "${addlinkflags} ${WIN_LINK_FLAGS}")
  else ()
    set_target_properties (${wintarget} PROPERTIES COMPILE_FLAGS "${addcompileflags}" LINK_FLAGS "${addlinkflags}")
  endif ()
endmacro ()

#-------------------------------------------------------------------------------
macro (INSTALL_TARGET_PDB libtarget targetdestination targetcomponent)
  if (WIN32 AND MSVC)
    get_target_property (target_type ${libtarget} TYPE)
    if (${libtype} MATCHES "SHARED")
      set (targetfilename $<TARGET_PDB_FILE:${libtarget}>)
    else ()
      get_property (target_name TARGET ${libtarget} PROPERTY OUTPUT_NAME_RELWITHDEBINFO)
      set (targetfilename $<TARGET_FILE_DIR:${libtarget}>/${target_name}.pdb)
    endif ()
    install (
      FILES
          ${targetfilename}
      DESTINATION
          ${targetdestination}
      CONFIGURATIONS RelWithDebInfo
      COMPONENT ${targetcomponent}
  )
  endif ()
endmacro ()


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
endif ()

#-----------------------------------------------------------------------------
# To include a library in the list exported by the project AT BUILD TIME,
# add it to this variable. This is NOT used by Make Install, but for projects
# which include the REST VOL as a sub-project within their build tree
#-----------------------------------------------------------------------------
set (REST_VOL_LIBRARIES_TO_EXPORT ${REST_VOL_LIB_TARGET} CACHE INTERNAL "Used to pass variables between directories" FORCE)
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
endif ()

#-----------------------------------------------------------------------------
# Option to Build Shared and Static libs, default is static
#-----------------------------------------------------------------------------
option (BUILD_SHARED_LIBS "Build Shared Libraries" ON)
set (REST_VOL_ENABLE_SHARED_LIB NO)
if (BUILD_SHARED_LIBS)
  set (REST_VOL_ENABLE_SHARED_LIB YES)
  set (LINK_SHARED_LIBS ${LINK_LIBS})
endif ()
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
  endif ()
endif ()

#-----------------------------------------------------------------------------
# Option to use code coverage
#-----------------------------------------------------------------------------
option (REST_VOL_ENABLE_COVERAGE "Enable code coverage for Libraries and Programs" OFF)
if (REST_VOL_ENABLE_COVERAGE)
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 -fprofile-arcs -ftest-coverage")
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -fprofile-arcs -ftest-coverage")
  set (LDFLAGS "${LDFLAGS} -fprofile-arcs -ftest-coverage")
endif ()

#-----------------------------------------------------------------------------
# Option to build the examples
#-----------------------------------------------------------------------------
option (REST_VOL_ENABLE_EXAMPLES "Indicate that building examples should be enabled" ON)

#-----------------------------------------------------------------------------
# Option to indicate enabling plugin debugging output
#-----------------------------------------------------------------------------
option (REST_VOL_ENABLE_DEBUG "Indicate that plugin debug output is used" OFF)
if (REST_VOL_ENABLE_DEBUG)
  set (REST_VOL_PLUGIN_DEBUG 1)
endif ()

#-----------------------------------------------------------------------------
# Option to indicate enabling cURL debugging output
#-----------------------------------------------------------------------------
option (REST_VOL_ENABLE_CURL_DEBUG "Indicate that cURL debug output is used" OFF)
if (REST_VOL_ENABLE_CURL_DEBUG)
  set (REST_VOL_CURL_DEBUG 1)
endif ()

#-----------------------------------------------------------------------------
# Option to indicate using a memory checker
#-----------------------------------------------------------------------------
option (REST_VOL_ENABLE_MEM_TRACKING "Indicate that a memory checker is used" OFF)
if (REST_VOL_ENABLE_MEM_TRACKING)
  set (REST_VOL_TRACK_MEM_USAGE 1)
endif ()

#-----------------------------------------------------------------------------
# Allow Visual Studio solution directories
#-----------------------------------------------------------------------------
# Provide a way for Visual Studio Express users to turn OFF the new FOLDER
# organization feature. Default to ON for non-Express users. Express users must
# explicitly turn off this option to build HDF5 in the Express IDE...
#
option (REST_VOL_USE_FOLDERS "Enable folder grouping of projects in IDEs." ON)
mark_as_advanced (REST_VOL_USE_FOLDERS)
if (REST_VOL_USE_FOLDERS)
  set_property (GLOBAL PROPERTY USE_FOLDERS ON)
endif ()
option (REST_VOL_NO_PACKAGES "CPACK - Disable packaging" OFF)
mark_as_advanced (REST_VOL_NO_PACKAGES)


#-----------------------------------------------------------------------------
# Generate the rest_vol_config.h file containing user settings needed by
# compilation
#-----------------------------------------------------------------------------
configure_file (${REST_VOL_RESOURCES_DIR}/rv_cmake_config.h.in ${REST_VOL_BINARY_DIR}/rest_vol_config.h @ONLY)

#-----------------------------------------------------------------------------
# Include the main src directory
#-----------------------------------------------------------------------------
set (REST_VOL_INCLUDE_DIRECTORIES
    ${REST_VOL_SRC_DIR}
    ${REST_VOL_BINARY_DIR}
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)
INCLUDE_DIRECTORIES (${REST_VOL_INCLUDE_DIRECTORIES})


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
endif ()

if (MSVC)
  set (CMAKE_MFC_FLAG 0)
  set (WIN_COMPILE_FLAGS "")
  set (WIN_LINK_FLAGS "")
endif ()

set (MAKE_SYSTEM)
if (CMAKE_BUILD_TOOL MATCHES "make")
  set (MAKE_SYSTEM 1)
endif ()

set (CFG_INIT "/${CMAKE_CFG_INTDIR}")
if (MAKE_SYSTEM)
  set (CFG_INIT "")
endif ()

#-----------------------------------------------------------------------------
# Add some definitions for Debug Builds
#-----------------------------------------------------------------------------
if (CMAKE_BUILD_TYPE MATCHES Debug)
  add_definitions (-DDEBUG)
else (CMAKE_BUILD_TYPE MATCHES Debug)
  add_definitions (-DNDEBUG)
endif ()

include (${REST_VOL_RESOURCES_DIR}/HDFCompilerFlags.cmake)

#-----------------------------------------------------------------------------
# Build the REST VOL
#-----------------------------------------------------------------------------
add_subdirectory(${REST_VOL_SRC_DIR} ${PROJECT_BINARY_DIR}/src)

#-----------------------------------------------------------------------------
# Add the REST VOL test Target to the build if testing is enabled
#-----------------------------------------------------------------------------
option (BUILD_TESTING "Build REST VOL Unit Testing" ON)
if (BUILD_TESTING)
  set (DART_TESTING_TIMEOUT 1200
      CACHE INTEGER
      "Timeout in seconds for each test (default 1200=20minutes)"
  )

  enable_testing ()
  include (CTest)

  add_subdirectory(${REST_VOL_TEST_SRC_DIR} ${PROJECT_BINARY_DIR}/test)

  include (${REST_VOL_SOURCE_DIR}/CTestConfig.cmake)
endif ()

#-----------------------------------------------------------------------------
# Add the REST VOL examples Target to the build
#-----------------------------------------------------------------------------
if (REST_VOL_ENABLE_EXAMPLES)
  add_subdirectory(${REST_VOL_EXAMPLES_DIR} ${PROJECT_BINARY_DIR}/examples)
endif ()

include (CMakeInstallation.cmake)