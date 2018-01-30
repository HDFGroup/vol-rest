cmake_minimum_required (VERSION 3.1.0)
PROJECT (REST VOL)

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

# HDF5_EXPORTED_TARGETS :
#   Set this to the name of the targets variable which controls exports
#   If unset (because parent project does not support/use the
#   install (EXPORT target...) syntax), then targets are not configured
#   for export during install.
set (HDF5_EXPORTED_TARGETS )

# HDF5_LIB_DEPENDENCIES :
#   If the build of HDF5 libs is being customized, then rules for the
#   dependencies of the HDF5 libs may be 'incomplete', add additional
#   dependencies to this variable so that external projects pick them up
set (HDF5_LIB_DEPENDENCIES )

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
set (HDF5_INSTALL_NO_DEVELOPMENT )

# Add the HDF5 source as a sub-project
add_subdirectory(hdf5)

string(TIMESTAMP CONFIG_DATE "%Y-%m-%d")

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
set (HDF_RESOURCES_DIR       ${HDF5_SOURCE_DIR}/config/cmake)
set (HDF_RESOURCES_EXT_DIR   ${HDF5_SOURCE_DIR}/config/cmake_ext_mod)
set (HDF5_SRC_DIR            ${HDF5_SOURCE_DIR}/src)
set (HDF5_TEST_SRC_DIR       ${HDF5_SOURCE_DIR}/test)
set (HDF5_CPP_SRC_DIR        ${HDF5_SOURCE_DIR}/c++)
set (HDF5_CPP_TST_DIR        ${HDF5_SOURCE_DIR}/c++/test)
set (HDF5_HL_SRC_DIR         ${HDF5_SOURCE_DIR}/hl)
set (HDF5_HL_CPP_SRC_DIR     ${HDF5_SOURCE_DIR}/hl/c++)
set (HDF5_TOOLS_SRC_DIR      ${HDF5_SOURCE_DIR}/tools)
set (HDF5_PERFORM_SRC_DIR    ${HDF5_SOURCE_DIR}/tools/perform)
set (HDF5_F90_SRC_DIR        ${HDF5_SOURCE_DIR}/fortran)
set (HDF5_JAVA_JNI_SRC_DIR         ${HDF5_SOURCE_DIR}/java/src/jni)
set (HDF5_JAVA_HDF5_SRC_DIR        ${HDF5_SOURCE_DIR}/java/src/hdf)
set (HDF5_JAVA_TEST_SRC_DIR        ${HDF5_SOURCE_DIR}/java/test)
set (HDF5_JAVA_LIB_DIR             ${HDF5_SOURCE_DIR}/java/lib)
set (HDF5_JAVA_LOGGING_JAR         ${HDF5_SOURCE_DIR}/java/lib/slf4j-api-1.7.5.jar)
set (HDF5_JAVA_LOGGING_NOP_JAR     ${HDF5_SOURCE_DIR}/java/lib/ext/slf4j-nop-1.7.5.jar)
set (HDF5_JAVA_LOGGING_SIMPLE_JAR  ${HDF5_SOURCE_DIR}/java/lib/ext/slf4j-simple-1.7.5.jar)

if (NOT HDF5_INSTALL_BIN_DIR)
  set (HDF5_INSTALL_BIN_DIR bin)
endif (NOT HDF5_INSTALL_BIN_DIR)
if (NOT HDF5_INSTALL_LIB_DIR)
  if (APPLE)
    if (HDF5_BUILD_FRAMEWORKS)
      set (HDF5_INSTALL_JAR_DIR ../Java)
    else (HDF5_BUILD_FRAMEWORKS)
      set (HDF5_INSTALL_JAR_DIR lib)
    endif (HDF5_BUILD_FRAMEWORKS)
    set (HDF5_INSTALL_FMWK_DIR ${CMAKE_INSTALL_FRAMEWORK_PREFIX})
  else (APPLE)
    set (HDF5_INSTALL_JAR_DIR lib)
  endif (APPLE)
  set (HDF5_INSTALL_LIB_DIR lib)
endif (NOT HDF5_INSTALL_LIB_DIR)
if (NOT HDF5_INSTALL_INCLUDE_DIR)
  set (HDF5_INSTALL_INCLUDE_DIR include)
endif (NOT HDF5_INSTALL_INCLUDE_DIR)
if (NOT HDF5_INSTALL_DATA_DIR)
  if (NOT WIN32)
    if (APPLE)
      if (HDF5_BUILD_FRAMEWORKS)
        set (HDF5_INSTALL_EXTRA_DIR ../SharedSupport)
      else (HDF5_BUILD_FRAMEWORKS)
        set (HDF5_INSTALL_EXTRA_DIR share)
      endif (HDF5_BUILD_FRAMEWORKS)
      set (HDF5_INSTALL_FWRK_DIR ${CMAKE_INSTALL_FRAMEWORK_PREFIX})
    endif (APPLE)
    set (HDF5_INSTALL_DATA_DIR share)
    set (HDF5_INSTALL_CMAKE_DIR share/cmake)
  else (NOT WIN32)
    set (HDF5_INSTALL_DATA_DIR ".")
    set (HDF5_INSTALL_CMAKE_DIR cmake)
  endif (NOT WIN32)
endif (NOT HDF5_INSTALL_DATA_DIR)

if(DEFINED ADDITIONAL_CMAKE_PREFIX_PATH AND EXISTS "${ADDITIONAL_CMAKE_PREFIX_PATH}")
  set (CMAKE_PREFIX_PATH ${ADDITIONAL_CMAKE_PREFIX_PATH} ${CMAKE_PREFIX_PATH})
endif(DEFINED ADDITIONAL_CMAKE_PREFIX_PATH AND EXISTS "${ADDITIONAL_CMAKE_PREFIX_PATH}")

#message (STATUS "VERSION: ${RV_RELEASE_VERSION}")

#-----------------------------------------------------------------------------
# parse the full soversion number from config/lt_vers.am and include in H5_SOVERS_INFO
#-----------------------------------------------------------------------------
file (READ ${HDF5_SOURCE_DIR}/config/lt_vers.am _lt_vers_am_contents)
string (REGEX REPLACE ".*LT_VERS_INTERFACE[ \t]+=[ \t]+([0-9]*).*$"
    "\\1" H5_SOVERS_INTERFACE ${_lt_vers_am_contents})
string (REGEX REPLACE ".*LT_VERS_REVISION[ \t]+=[ \t]+([0-9]*).*$"
    "\\1" H5_SOVERS_MINOR ${_lt_vers_am_contents})
string (REGEX REPLACE ".*LT_VERS_AGE[ \t]+=[ \t]+([0-9]*).*$"
    "\\1" H5_SOVERS_RELEASE ${_lt_vers_am_contents})
MATH (EXPR H5_SOVERS_MAJOR ${H5_SOVERS_INTERFACE}-${H5_SOVERS_RELEASE})
message (STATUS "SOVERSION: ${H5_SOVERS_MAJOR}.${H5_SOVERS_RELEASE}.${H5_SOVERS_MINOR}")
string (REGEX MATCH ".*LT_TOOLS_VERS_INTERFACE[ \t]+=[ \t]+([0-9]*).*$" H5_TOOLS_SOVERS_EXISTS ${_lt_vers_am_contents})
if(H5_TOOLS_SOVERS_EXISTS)
  string (REGEX REPLACE ".*LT_TOOLS_VERS_INTERFACE[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_TOOLS_SOVERS_INTERFACE ${_lt_vers_am_contents})
  string (REGEX REPLACE ".*LT_TOOLS_VERS_REVISION[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_TOOLS_SOVERS_MINOR ${_lt_vers_am_contents})
  string (REGEX REPLACE ".*LT_TOOLS_VERS_AGE[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_TOOLS_SOVERS_RELEASE ${_lt_vers_am_contents})
  MATH (EXPR H5_TOOLS_SOVERS_MAJOR ${H5_TOOLS_SOVERS_INTERFACE}-${H5_TOOLS_SOVERS_RELEASE})
  message (STATUS "SOVERSION_TOOLS: ${H5_TOOLS_SOVERS_MAJOR}.${H5_TOOLS_SOVERS_RELEASE}.${H5_TOOLS_SOVERS_MINOR}")
endif()
string (REGEX REPLACE ".*LT_CXX_VERS_INTERFACE[ \t]+=[ \t]+([0-9]*).*$" H5_CXX_SOVERS_EXISTS ${_lt_vers_am_contents})
if(H5_CXX_SOVERS_EXISTS)
  string (REGEX REPLACE ".*LT_CXX_VERS_INTERFACE[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_CXX_SOVERS_INTERFACE ${_lt_vers_am_contents})
  string (REGEX REPLACE ".*LT_CXX_VERS_REVISION[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_CXX_SOVERS_MINOR ${_lt_vers_am_contents})
  string (REGEX REPLACE ".*LT_CXX_VERS_AGE[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_CXX_SOVERS_RELEASE ${_lt_vers_am_contents})
  MATH (EXPR H5_CXX_SOVERS_MAJOR ${H5_CXX_SOVERS_INTERFACE}-${H5_CXX_SOVERS_RELEASE})
  message (STATUS "SOVERSION_CXX: ${H5_CXX_SOVERS_MAJOR}.${H5_CXX_SOVERS_RELEASE}.${H5_CXX_SOVERS_MINOR}")
endif()
string (REGEX REPLACE ".*LT_F_VERS_INTERFACE[ \t]+=[ \t]+([0-9]*).*$" H5_F_SOVERS_EXISTS ${_lt_vers_am_contents})
if(H5_F_SOVERS_EXISTS)
  string (REGEX REPLACE ".*LT_F_VERS_INTERFACE[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_F_SOVERS_INTERFACE ${_lt_vers_am_contents})
  string (REGEX REPLACE ".*LT_F_VERS_REVISION[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_F_SOVERS_MINOR ${_lt_vers_am_contents})
  string (REGEX REPLACE ".*LT_F_VERS_AGE[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_F_SOVERS_RELEASE ${_lt_vers_am_contents})
  MATH (EXPR H5_F_SOVERS_MAJOR ${H5_F_SOVERS_INTERFACE}-${H5_F_SOVERS_RELEASE})
  message (STATUS "SOVERSION_F: ${H5_F_SOVERS_MAJOR}.${H5_F_SOVERS_RELEASE}.${H5_F_SOVERS_MINOR}")
endif()
string (REGEX REPLACE ".*LT_HL_VERS_INTERFACE[ \t]+=[ \t]+([0-9]*).*$" H5_HL_SOVERS_EXISTS ${_lt_vers_am_contents})
if(H5_HL_SOVERS_EXISTS)
  string (REGEX REPLACE ".*LT_HL_VERS_INTERFACE[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_HL_SOVERS_INTERFACE ${_lt_vers_am_contents})
  string (REGEX REPLACE ".*LT_HL_VERS_REVISION[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_HL_SOVERS_MINOR ${_lt_vers_am_contents})
  string (REGEX REPLACE ".*LT_HL_VERS_AGE[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_HL_SOVERS_RELEASE ${_lt_vers_am_contents})
  MATH (EXPR H5_HL_SOVERS_MAJOR ${H5_HL_SOVERS_INTERFACE}-${H5_HL_SOVERS_RELEASE})
  message (STATUS "SOVERSION_HL: ${H5_HL_SOVERS_MAJOR}.${H5_HL_SOVERS_RELEASE}.${H5_HL_SOVERS_MINOR}")
endif()
string (REGEX REPLACE ".*LT_HL_CXX_VERS_INTERFACE[ \t]+=[ \t]+([0-9]*).*$" H5_HL_CXX_SOVERS_EXISTS ${_lt_vers_am_contents})
if(H5_HL_CXX_SOVERS_EXISTS)
  string (REGEX REPLACE ".*LT_HL_CXX_VERS_INTERFACE[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_HL_CXX_SOVERS_INTERFACE ${_lt_vers_am_contents})
  string (REGEX REPLACE ".*LT_HL_CXX_VERS_REVISION[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_HL_CXX_SOVERS_MINOR ${_lt_vers_am_contents})
  string (REGEX REPLACE ".*LT_HL_CXX_VERS_AGE[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_HL_CXX_SOVERS_RELEASE ${_lt_vers_am_contents})
  MATH (EXPR H5_HL_CXX_SOVERS_MAJOR ${H5_HL_CXX_SOVERS_INTERFACE}-${H5_HL_CXX_SOVERS_RELEASE})
  message (STATUS "SOVERSION_HL_CXX: ${H5_HL_CXX_SOVERS_MAJOR}.${H5_HL_CXX_SOVERS_RELEASE}.${H5_HL_CXX_SOVERS_MINOR}")
endif()
string (REGEX REPLACE ".*LT_HL_F_VERS_INTERFACE[ \t]+=[ \t]+([0-9]*).*$" H5_HL_F_SOVERS_EXISTS ${_lt_vers_am_contents})
if(H5_HL_F_SOVERS_EXISTS)
  string (REGEX REPLACE ".*LT_HL_F_VERS_INTERFACE[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_HL_F_SOVERS_INTERFACE ${_lt_vers_am_contents})
  string (REGEX REPLACE ".*LT_HL_F_VERS_REVISION[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_HL_F_SOVERS_MINOR ${_lt_vers_am_contents})
  string (REGEX REPLACE ".*LT_HL_F_VERS_AGE[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_HL_F_SOVERS_RELEASE ${_lt_vers_am_contents})
  MATH (EXPR H5_HL_F_SOVERS_MAJOR ${H5_HL_F_SOVERS_INTERFACE}-${H5_HL_F_SOVERS_RELEASE})
  message (STATUS "SOVERSION_HL_F: ${H5_HL_F_SOVERS_MAJOR}.${H5_HL_F_SOVERS_RELEASE}.${H5_HL_F_SOVERS_MINOR}")
endif()
string (REGEX REPLACE ".*LT_JAVA_VERS_INTERFACE[ \t]+=[ \t]+([0-9]*).*$" H5_JAVA_SOVERS_EXISTS ${_lt_vers_am_contents})
if(H5_JAVA_SOVERS_EXISTS)
  string (REGEX REPLACE ".*LT_JAVA_VERS_INTERFACE[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_JAVA_SOVERS_INTERFACE ${_lt_vers_am_contents})
  string (REGEX REPLACE ".*LT_JAVA_VERS_REVISION[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_JAVA_SOVERS_MINOR ${_lt_vers_am_contents})
  string (REGEX REPLACE ".*LT_JAVA_VERS_AGE[ \t]+=[ \t]+([0-9]*).*$"
      "\\1" H5_JAVA_SOVERS_RELEASE ${_lt_vers_am_contents})
  MATH (EXPR H5_JAVA_SOVERS_MAJOR ${H5_JAVA_SOVERS_INTERFACE}-${H5_JAVA_SOVERS_RELEASE})
  message (STATUS "SOVERSION_JAVA: ${H5_JAVA_SOVERS_MAJOR}.${H5_JAVA_SOVERS_RELEASE}.${H5_JAVA_SOVERS_MINOR}")
endif()

#-----------------------------------------------------------------------------
# Basic HDF5 stuff here
#-----------------------------------------------------------------------------
set (HDF5_PACKAGE "hdf5")
set (HDF5_PACKAGE_NAME "HDF5")
set (HDF5_PACKAGE_VERSION "${H5_VERS_MAJOR}.${H5_VERS_MINOR}.${H5_VERS_RELEASE}")
set (HDF5_PACKAGE_VERSION_MAJOR "${H5_VERS_MAJOR}.${H5_VERS_MINOR}")
set (HDF5_PACKAGE_VERSION_MINOR "${H5_VERS_RELEASE}")
if (NOT "${H5_VERS_SUBRELEASE}" STREQUAL "")
  set (HDF5_PACKAGE_VERSION_STRING "${HDF5_PACKAGE_VERSION}-${H5_VERS_SUBRELEASE}")
else (NOT "${H5_VERS_SUBRELEASE}" STREQUAL "")
  set (HDF5_PACKAGE_VERSION_STRING "${HDF5_PACKAGE_VERSION}")
endif (NOT "${H5_VERS_SUBRELEASE}" STREQUAL "")
set (HDF5_PACKAGE_SOVERSION "${H5_SOVERS_MAJOR}.${H5_SOVERS_RELEASE}.${H5_SOVERS_MINOR}")

set (HDF5_PACKAGE_STRING "${HDF5_PACKAGE_NAME} ${HDF5_PACKAGE_VERSION_STRING}")
set (HDF5_PACKAGE_TARNAME "${HDF5_PACKAGE}${HDF_PACKAGE_EXT}")
set (HDF5_PACKAGE_URL "http://www.hdfgroup.org")
set (HDF5_PACKAGE_BUGREPORT "help@hdfgroup.org")

#-----------------------------------------------------------------------------
# Include some macros for reusable code
#-----------------------------------------------------------------------------
include (${HDF_RESOURCES_EXT_DIR}/HDFMacros.cmake)
include (${HDF_RESOURCES_EXT_DIR}/HDFLibMacros.cmake)
include (${HDF_RESOURCES_DIR}/HDF5Macros.cmake)

#-----------------------------------------------------------------------------
# Setup output Directories
#-----------------------------------------------------------------------------
if (NOT HDF5_EXTERNALLY_CONFIGURED)
  set (CMAKE_RUNTIME_OUTPUT_DIRECTORY
      ${PROJECT_BINARY_DIR}/bin CACHE PATH "Single Directory for all Executables."
  )
  set (CMAKE_LIBRARY_OUTPUT_DIRECTORY
      ${PROJECT_BINARY_DIR}/bin CACHE PATH "Single Directory for all Libraries"
  )
  set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY
      ${PROJECT_BINARY_DIR}/bin CACHE PATH "Single Directory for all static libraries."
  )
  set (CMAKE_Fortran_MODULE_DIRECTORY
      ${PROJECT_BINARY_DIR}/bin CACHE PATH "Single Directory for all fortran modules."
  )
  if (WIN32)
    set (CMAKE_TEST_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE})
  else (WIN32)
    set (CMAKE_TEST_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
  endif (WIN32)
else (NOT HDF5_EXTERNALLY_CONFIGURED)
  # if we are externally configured, but the project uses old cmake scripts
  # this may not be set and utilities like H5detect will fail
  if (NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${EXECUTABLE_OUTPUT_PATH})
  endif (NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
endif (NOT HDF5_EXTERNALLY_CONFIGURED)

#-----------------------------------------------------------------------------
# Targets built within this project are exported at Install time for use
# by other projects using FindHDF5.
#-----------------------------------------------------------------------------
if (NOT HDF5_EXPORTED_TARGETS)
  set (HDF5_EXPORTED_TARGETS "hdf5-targets")
endif (NOT HDF5_EXPORTED_TARGETS)

#-----------------------------------------------------------------------------
# To include a library in the list exported by the project AT BUILD TIME,
# add it to this variable. This is NOT used by Make Install, but for projects
# which include hdf5 as a sub-project within their build tree
#-----------------------------------------------------------------------------
set_global_variable (HDF5_LIBRARIES_TO_EXPORT "")
set (EXTERNAL_HEADER_LIST "")
set (EXTERNAL_LIBRARY_LIST "")
set (EXTERNAL_LIBRARYDLL_LIST "")

#-----------------------------------------------------------------------------
# Run all the CMake configuration tests for our build environment
#-----------------------------------------------------------------------------
include (${HDF_RESOURCES_DIR}/ConfigureChecks.cmake)

set (CMAKE_INCLUDE_CURRENT_DIR_IN_INTERFACE ON)

#-----------------------------------------------------------------------------
# Mac OS X Options
#-----------------------------------------------------------------------------
if (HDF5_BUILD_FRAMEWORKS AND NOT BUILD_SHARED_LIBS)
  set (BUILD_SHARED_LIBS ON CACHE BOOL "Build Shared Libraries")
endif (HDF5_BUILD_FRAMEWORKS AND NOT BUILD_SHARED_LIBS)

#-----------------------------------------------------------------------------
# Option to Build Shared and Static libs, default is static
#-----------------------------------------------------------------------------
option (BUILD_SHARED_LIBS "Build Shared Libraries" ON)
set (H5_ENABLE_SHARED_LIB NO)
if (BUILD_SHARED_LIBS)
  set (H5_ENABLE_SHARED_LIB YES)
  set (LINK_SHARED_LIBS ${LINK_LIBS})
endif (BUILD_SHARED_LIBS)
set (H5_ENABLE_STATIC_LIB YES)
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
option (HDF5_ENABLE_COVERAGE "Enable code coverage for Libraries and Programs" OFF)
if (HDF5_ENABLE_COVERAGE)
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 -fprofile-arcs -ftest-coverage")
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -fprofile-arcs -ftest-coverage")
  set (LDFLAGS "${LDFLAGS} -fprofile-arcs -ftest-coverage")
endif (HDF5_ENABLE_COVERAGE)

#-----------------------------------------------------------------------------
# Option to indicate using a memory checker
#-----------------------------------------------------------------------------
option (HDF5_ENABLE_USING_MEMCHECKER "Indicate that a memory checker is used" OFF)
if (HDF5_ENABLE_USING_MEMCHECKER)
  set (H5_USING_MEMCHECKER 1)
endif (HDF5_ENABLE_USING_MEMCHECKER)

#-----------------------------------------------------------------------------
# Option to indicate internal memory allocation sanity checks are enabled
#-----------------------------------------------------------------------------
option (HDF5_MEMORY_ALLOC_SANITY_CHECK "Indicate that internal memory allocation sanity checks are enabled" OFF)
if (HDF5_MEMORY_ALLOC_SANITY_CHECK)
  set (H5_MEMORY_ALLOC_SANITY_CHECK 1)
endif (HDF5_MEMORY_ALLOC_SANITY_CHECK)

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
option (HDF5_ENABLE_TRACE "Enable API tracing capability" OFF)
mark_as_advanced (HDF5_ENABLE_TRACE)
if (CMAKE_BUILD_TYPE MATCHES Debug)
  add_definitions (-DDEBUG)
  # Enable tracing of the API
  if (HDF5_ENABLE_TRACE)
    add_definitions (-DH5_DEBUG_API )
  endif (HDF5_ENABLE_TRACE)
  # Enable instrumenting of the library's internal operations
  option (HDF5_ENABLE_INSTRUMENT "Instrument The library" OFF)
  if (HDF5_ENABLE_INSTRUMENT)
    set (H5_HAVE_INSTRUMENTED_LIBRARY 1)
  endif (HDF5_ENABLE_INSTRUMENT)
  mark_as_advanced (HDF5_ENABLE_INSTRUMENT)
else (CMAKE_BUILD_TYPE MATCHES Debug)
  add_definitions (-DNDEBUG)
  if (HDF5_ENABLE_TRACE)
    add_definitions (-DH5_DEBUG_API )
  endif (HDF5_ENABLE_TRACE)
endif (CMAKE_BUILD_TYPE MATCHES Debug)

include (${HDF_RESOURCES_DIR}/HDFCompilerFlags.cmake)

#-----------------------------------------------------------------------------
# All libs/tests/examples need the main include directories
#-----------------------------------------------------------------------------
INCLUDE_DIRECTORIES (${HDF5_BINARY_DIR} ${HDF5_SRC_DIR} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

#-----------------------------------------------------------------------------
# Include user macros
#-----------------------------------------------------------------------------
include (UserMacros.cmake)

# -----------------------------------------------------------------------
# wrapper script variables
#
#set (CFLAGS "${C_DEFINES}")
#set (CXXFLAGS "${CXX_DEFINES}")

#-----------------------------------------------------------------------------
# Add the HDF5 Library Target to the build
#-----------------------------------------------------------------------------
add_subdirectory (${HDF5_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/src)

if (HDF5_ALLOW_EXTERNAL_SUPPORT MATCHES "SVN" OR HDF5_ALLOW_EXTERNAL_SUPPORT MATCHES "TGZ")
  if (ZLIB_FOUND AND ZLIB_USE_EXTERNAL)
    ADD_DEPENDENCIES (${HDF5_LIB_TARGET} ZLIB)
    if (BUILD_SHARED_LIBS)
      add_dependencies (${HDF5_LIBSH_TARGET} ZLIB)
    endif (BUILD_SHARED_LIBS)
  endif (ZLIB_FOUND AND ZLIB_USE_EXTERNAL)
  if (SZIP_FOUND AND SZIP_USE_EXTERNAL)
    ADD_DEPENDENCIES (${HDF5_LIB_TARGET} SZIP)
    if (BUILD_SHARED_LIBS)
      add_dependencies (${HDF5_LIBSH_TARGET} SZIP)
    endif (BUILD_SHARED_LIBS)
  endif (SZIP_FOUND AND SZIP_USE_EXTERNAL)
endif (HDF5_ALLOW_EXTERNAL_SUPPORT MATCHES "SVN" OR HDF5_ALLOW_EXTERNAL_SUPPORT MATCHES "TGZ")

#-----------------------------------------------------------------------------
# Dashboard and Testing Settings
#-----------------------------------------------------------------------------
option (BUILD_TESTING "Build HDF5 Unit Testing" ON)
if (BUILD_TESTING)
  set (DART_TESTING_TIMEOUT 1200
      CACHE INTEGER
      "Timeout in seconds for each test (default 1200=20minutes)"
  )

  option (HDF5_TEST_VFD "Execute tests with different VFDs" OFF)
  mark_as_advanced (HDF5_TEST_VFD)
  if (HDF5_TEST_VFD)
    option (HDF5_TEST_FHEAP_VFD "Execute tests with different VFDs" ON)
    mark_as_advanced (HDF5_TEST_FHEAP_VFD)
  endif (HDF5_TEST_VFD)

  option (HDF_TEST_EXPRESS "Control testing framework (0-3)" "0")
  mark_as_advanced (HDF_TEST_EXPRESS)

  enable_testing ()
  include (CTest)

  include (${HDF5_SOURCE_DIR}/CTestConfig.cmake)
  configure_file (${HDF_RESOURCES_DIR}/CTestCustom.cmake ${HDF5_BINARY_DIR}/CTestCustom.ctest @ONLY)

  if (NOT HDF5_EXTERNALLY_CONFIGURED)
    if (EXISTS "${HDF5_SOURCE_DIR}/test" AND IS_DIRECTORY "${HDF5_SOURCE_DIR}/test")
      add_subdirectory (${HDF5_SOURCE_DIR}/tools/lib ${PROJECT_BINARY_DIR}/tools/lib)
      add_subdirectory (${HDF5_SOURCE_DIR}/test      ${PROJECT_BINARY_DIR}/test)
    endif (EXISTS "${HDF5_SOURCE_DIR}/test" AND IS_DIRECTORY "${HDF5_SOURCE_DIR}/test")
    if (H5_HAVE_PARALLEL)
      if (EXISTS "${HDF5_SOURCE_DIR}/testpar" AND IS_DIRECTORY "${HDF5_SOURCE_DIR}/testpar")
        add_subdirectory (${HDF5_SOURCE_DIR}/testpar ${PROJECT_BINARY_DIR}/testpar)
      endif (EXISTS "${HDF5_SOURCE_DIR}/testpar" AND IS_DIRECTORY "${HDF5_SOURCE_DIR}/testpar")
    endif (H5_HAVE_PARALLEL)
  endif (NOT HDF5_EXTERNALLY_CONFIGURED)
endif (BUILD_TESTING)

#-----------------------------------------------------------------------------
# Option to build HDF5 Tools
#-----------------------------------------------------------------------------
if (EXISTS "${HDF5_SOURCE_DIR}/tools" AND IS_DIRECTORY "${HDF5_SOURCE_DIR}/tools")
  option (HDF5_BUILD_TOOLS  "Build HDF5 Tools" ON)
  if (HDF5_BUILD_TOOLS)
    add_subdirectory (${HDF5_SOURCE_DIR}/tools ${PROJECT_BINARY_DIR}/tools)
  endif (HDF5_BUILD_TOOLS)
endif (EXISTS "${HDF5_SOURCE_DIR}/tools" AND IS_DIRECTORY "${HDF5_SOURCE_DIR}/tools")

#-----------------------------------------------------------------------------
# Option to build examples
#-----------------------------------------------------------------------------
if (EXISTS "${HDF5_SOURCE_DIR}/examples" AND IS_DIRECTORY "${HDF5_SOURCE_DIR}/examples")
  option (HDF5_BUILD_EXAMPLES  "Build HDF5 Library Examples" ON)
  if (HDF5_BUILD_EXAMPLES)
    add_subdirectory (${HDF5_SOURCE_DIR}/examples ${PROJECT_BINARY_DIR}/examples)
  endif (HDF5_BUILD_EXAMPLES)
endif (EXISTS "${HDF5_SOURCE_DIR}/examples" AND IS_DIRECTORY "${HDF5_SOURCE_DIR}/examples")

#-----------------------------------------------------------------------------
# Generate the rest_vol_config.h file containing user settings needed by compilation
#-----------------------------------------------------------------------------
configure_file (${}/rest_vol_config.h.in ${}/rest_vol_config.h @ONLY)

include (CMakeInstallation.cmake)
