#----------------------------------------------------#
# CMake Build Script for the HDF5 REST VOL connector #
#----------------------------------------------------#

cmake_minimum_required (VERSION 3.10...3.29)
project (HDF5_VOL_REST C)

# Setup CMake policies.
foreach (policy
  CMP0074 # CMake 3.12
  )
  if (POLICY ${policy})
    cmake_policy (SET ${policy} NEW)
  endif ()
endforeach ()


#-----------------------------------------------------------------------------
# Instructions for use : Normal Build
#
# For standard build of the HDF5 REST VOL connector.
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

string (TIMESTAMP CONFIG_DATE "%Y-%m-%d")


#------------------------------------------------------------------------------
# Set CMake module path
#------------------------------------------------------------------------------
set(HDF5_VOL_REST_CMAKE_MODULE_PATH "${HDF5_VOL_REST_SOURCE_DIR}/CMake")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${HDF5_VOL_REST_CMAKE_MODULE_PATH})

#------------------------------------------------------------------------------
# Locate Git
#------------------------------------------------------------------------------
include(${HDF5_VOL_REST_CMAKE_MODULE_PATH}/Git/Git.cmake)

#-----------------------------------------------------------------------------
# Allow Visual Studio solution directories
#-----------------------------------------------------------------------------
# Provide a way for Visual Studio Express users to turn OFF the new FOLDER
# organization feature. Default to ON for non-Express users. Express users must
# explicitly turn off this option to build the HDF5 REST VOL connector in the
# Express IDE...
#
option (HDF5_VOL_REST_USE_FOLDERS "Enable folder grouping of projects in IDEs." ON)
mark_as_advanced (HDF5_VOL_REST_USE_FOLDERS)
if (HDF5_VOL_REST_USE_FOLDERS)
  set_property (GLOBAL PROPERTY USE_FOLDERS ON)
endif ()
option (HDF5_VOL_REST_NO_PACKAGES "CPACK - Disable packaging" ON)
mark_as_advanced (HDF5_VOL_REST_NO_PACKAGES)


#-----------------------------------------------------------------------------
# Set the core names of all the libraries
#-----------------------------------------------------------------------------
set (HDF5_VOL_REST_LIB_CORENAME        "hdf5_vol_rest")
set (HDF5_VOL_REST_TEST_LIB_CORENAME   "hdf5_vol_rest_test")

#-----------------------------------------------------------------------------
# Set the target names of all the libraries
#-----------------------------------------------------------------------------
set (HDF5_VOL_REST_LIB_TARGET          "${HDF5_VOL_REST_LIB_CORENAME}-static")
set (HDF5_VOL_REST_LIBSH_TARGET        "${HDF5_VOL_REST_LIB_CORENAME}-shared")
set (HDF5_VOL_REST_TEST_LIB_TARGET     "${HDF5_VOL_REST_TEST_LIB_CORENAME}-static")
set (HDF5_VOL_REST_TEST_LIBSH_TARGET   "${HDF5_VOL_REST_TEST_LIB_CORENAME}-shared")


#-----------------------------------------------------------------------------
# Define some CMake variables for use later in the project
#-----------------------------------------------------------------------------
set (HDF5_VOL_REST_RESOURCES_DIR       ${HDF5_VOL_REST_SOURCE_DIR}/config/cmake)
set (HDF5_VOL_REST_RESOURCES_EXT_DIR   ${HDF5_VOL_REST_SOURCE_DIR}/config/cmake_ext_mod)
set (HDF5_VOL_REST_RESOURCES_MOD_DIR   ${HDF5_VOL_REST_SOURCE_DIR}/config/cmake/modules)
set (HDF5_VOL_REST_SRC_DIR             ${HDF5_VOL_REST_SOURCE_DIR}/src)
set (HDF5_VOL_REST_UTIL_DIR            ${HDF5_VOL_REST_SOURCE_DIR}/src/util)
set (HDF5_VOL_REST_TEST_SRC_DIR        ${HDF5_VOL_REST_SOURCE_DIR}/test)
set (HDF5_VOL_REST_EXAMPLES_DIR        ${HDF5_VOL_REST_SOURCE_DIR}/examples)
set (HDF5_DIR_NAME                     "hdf5")
set (HDF5_HL_DIR_NAME                  "hl")

#-----------------------------------------------------------------------------
# Find HDF5, cURL and YAJL before building
#-----------------------------------------------------------------------------
if (HDF5_FOUND STREQUAL "")
  set (HDF5_FOUND FALSE)
endif()

find_package(HDF5 MODULE COMPONENTS C HL)

if (NOT HDF5_FOUND)
  message(SEND_ERROR "HDF5 not found!")
endif()

if (HDF5_ENABLE_THREADSAFE AND NOT Threads_FOUND)
  find_package(Threads)
endif ()

find_package (CURL 7.61 REQUIRED)
if (CURL_FOUND)
  include_directories(${CURL_INCLUDE_DIRS})
  set (LINK_LIBS ${LINK_LIBS} CURL::libcurl)
else ()
  message (FATAL_ERROR "cURL not found; please check CURL_INCLUDE_DIR")
endif ()

# Include custom module for finding YAJL
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${HDF5_VOL_REST_RESOURCES_MOD_DIR}")

find_package (YAJL 2.0.4 REQUIRED)
if (YAJL_FOUND)
  include_directories(${YAJL_INCLUDE_DIRS})
  set (LINK_LIBS ${LINK_LIBS} yajl)
else ()
  message (FATAL_ERROR "YAJL not found; please check YAJL_INCLUDE_DIR")
endif ()


#-----------------------------------------------------------------------------
# Setup the RPATH for the installed executables
#-----------------------------------------------------------------------------
set (CMAKE_SKIP_BUILD_RPATH  FALSE)
set (CMAKE_BUILD_WITH_INSTALL_RPATH FALSE) 
set (CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
set (CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
list (FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/lib" isSystemDir)
if ("${isSystemDir}" STREQUAL "-1")
   set (CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
endif ()


if (DEFINED ADDITIONAL_CMAKE_PREFIX_PATH AND EXISTS "${ADDITIONAL_CMAKE_PREFIX_PATH}")
  set (CMAKE_PREFIX_PATH ${ADDITIONAL_CMAKE_PREFIX_PATH} ${CMAKE_PREFIX_PATH})
endif()


#-----------------------------------------------------------------------------
# Setup the install directories
#-----------------------------------------------------------------------------
if (NOT HDF5_VOL_REST_INSTALL_BIN_DIR)
  set (HDF5_VOL_REST_INSTALL_BIN_DIR bin)
endif ()
if (NOT HDF5_VOL_REST_INSTALL_LIB_DIR)
  if (APPLE)
    set (HDF5_VOL_REST_INSTALL_FMWK_DIR ${CMAKE_INSTALL_FRAMEWORK_PREFIX})
  else (APPLE)
    set (HDF5_VOL_REST_INSTALL_JAR_DIR lib)
  endif ()
  set (HDF5_VOL_REST_INSTALL_LIB_DIR lib)
endif ()
if (NOT HDF5_VOL_REST_INSTALL_INCLUDE_DIR)
  set (HDF5_VOL_REST_INSTALL_INCLUDE_DIR include)
endif ()
if (NOT HDF5_VOL_REST_INSTALL_DATA_DIR)
  if (NOT WIN32)
    if (APPLE)
      if (HDF5_VOL_REST_BUILD_FRAMEWORKS)
        set (HDF5_VOL_REST_INSTALL_EXTRA_DIR ../SharedSupport)
      else (HDF5_VOL_REST_BUILD_FRAMEWORKS)
        set (HDF5_VOL_REST_INSTALL_EXTRA_DIR share)
      endif ()
      set (HDF5_VOL_REST_INSTALL_FWRK_DIR ${CMAKE_INSTALL_FRAMEWORK_PREFIX})
    endif ()
    set (HDF5_VOL_REST_INSTALL_DATA_DIR share)
    set (HDF5_VOL_REST_INSTALL_CMAKE_DIR share/cmake/${HDF5_VOL_REST_LIB_CORENAME})
  else (NOT WIN32)
    set (HDF5_VOL_REST_INSTALL_DATA_DIR ".")
    set (HDF5_VOL_REST_INSTALL_CMAKE_DIR cmake/${HDF5_VOL_REST_LIB_CORENAME})
  endif ()
endif ()

#-------------------------------------------------------------------------------------
# Parse the full version number from VERSION and include in HDF5_VOL_REST_VERS_INFO
#-------------------------------------------------------------------------------------
file (READ ${HDF5_VOL_REST_SOURCE_DIR}/VERSION _version_contents)
string (REGEX REPLACE "^.*([0-9][0-9]*).[0-9][0-9]*.[0-9][0-9]*.*$" "\\1" HDF5_VOL_REST_VERS_MAJOR ${_version_contents})
string (REGEX REPLACE "^.*[0-9][0-9]*.([0-9][0-9]*).[0-9][0-9]*.*$" "\\1" HDF5_VOL_REST_VERS_MINOR ${_version_contents})
string (REGEX REPLACE "^.*[0-9][0-9]*.[0-9][0-9]*.([0-9][0-9]*).*$" "\\1" HDF5_VOL_REST_VERS_RELEASE ${_version_contents})
message (STATUS "VERSION: ${HDF5_VOL_REST_VERS_MAJOR}.${HDF5_VOL_REST_VERS_MINOR}.${HDF5_VOL_REST_VERS_RELEASE}")


#-------------------------------------------------------------------------------
# Basic HDF5 REST VOL stuff here
#-------------------------------------------------------------------------------
set (HDF5_VOL_REST_PACKAGE "hdf5_vol_rest")
set (HDF5_VOL_REST_PACKAGE_NAME "HDF5_VOL_REST")
set (HDF5_VOL_REST_PACKAGE_VERSION "${HDF5_VOL_REST_VERS_MAJOR}.${HDF5_VOL_REST_VERS_MINOR}.${HDF5_VOL_REST_VERS_RELEASE}")
set (HDF5_VOL_REST_PACKAGE_VERSION_MAJOR "${HDF5_VOL_REST_VERS_MAJOR}.${HDF5_VOL_REST_VERS_MINOR}")
set (HDF5_VOL_REST_PACKAGE_VERSION_MINOR "${HDF5_VOL_REST_VERS_RELEASE}")
if (NOT "${HDF5_VOL_REST_VERS_SUBRELEASE}" STREQUAL "")
  set (HDF5_VOL_REST_PACKAGE_VERSION_STRING "${HDF5_VOL_REST_PACKAGE_VERSION}-${HDF5_VOL_REST_VERS_SUBRELEASE}")
else (NOT "${HDF5_VOL_REST_VERS_SUBRELEASE}" STREQUAL "")
  set (HDF5_VOL_REST_PACKAGE_VERSION_STRING "${HDF5_VOL_REST_PACKAGE_VERSION}")
endif ()

set (HDF5_VOL_REST_PACKAGE_STRING "${HDF5_VOL_REST_PACKAGE_NAME} ${HDF5_VOL_REST_PACKAGE_VERSION_STRING}")
set (HDF5_VOL_REST_PACKAGE_TARNAME "${HDF5_VOL_REST_PACKAGE}${HDF5_VOL_REST_PACKAGE_EXT}")
set (HDF5_VOL_REST_PACKAGE_URL "http://www.hdfgroup.org")
set (HDF5_VOL_REST_PACKAGE_BUGREPORT "help@hdfgroup.org")

#-----------------------------------------------------------------------------
# Targets built within this project are exported at Install time for use
# by other projects.
#-----------------------------------------------------------------------------
if (NOT HDF5_VOL_REST_EXPORTED_TARGETS)
  set (HDF5_VOL_REST_EXPORTED_TARGETS "${HDF5_VOL_REST_PACKAGE}-targets")
endif ()

#-----------------------------------------------------------------------------
# Include some macros for reusable code
#-----------------------------------------------------------------------------

#-------------------------------------------------------------------------------
macro (HDF5_VOL_REST_SET_LIB_OPTIONS libtarget libname libtype)
  set (LIB_OUT_NAME "${libname}")
  # SOVERSION passed in ARGN when shared
  if (${libtype} MATCHES "SHARED")
    if (ARGN)
      set (PACKAGE_SOVERSION ${ARGN})
    else (ARGN)
      set (PACKAGE_SOVERSION ${HDF5_VOL_REST_PACKAGE_SOVERSION})
    endif (ARGN)
    if (WIN32)
      set (LIBHDF5_VOL_REST_VERSION ${HDF5_VOL_REST_PACKAGE_VERSION_MAJOR})
    else (WIN32)
      set (LIBHDF5_VOL_REST_VERSION ${HDF5_VOL_REST_PACKAGE_VERSION})
    endif (WIN32)
    set_target_properties (${libtarget} PROPERTIES VERSION ${LIBHDF5_VOL_REST_VERSION})
    if (WIN32)
        set (${LIB_OUT_NAME} "${LIB_OUT_NAME}-${PACKAGE_SOVERSION}")
    else (WIN32)
        set_target_properties (${libtarget} PROPERTIES SOVERSION ${PACKAGE_SOVERSION})
    endif (WIN32)
  endif (${libtype} MATCHES "SHARED")
  HDF5_VOL_REST_SET_LIB_OPTIONS (${libtarget} ${LIB_OUT_NAME} ${libtype})

  #-- Apple Specific install_name for libraries
  if (APPLE)
    option (HDF5_VOL_REST_BUILD_WITH_INSTALL_NAME "Build with library install_name set to the installation path" OFF)
    if (HDF5_VOL_REST_BUILD_WITH_INSTALL_NAME)
      set_target_properties (${libtarget} PROPERTIES
          LINK_FLAGS "-current_version ${HDF5_VOL_REST_PACKAGE_VERSION} -compatibility_version ${HDF5_VOL_REST_PACKAGE_VERSION}"
          INSTALL_NAME_DIR "${CMAKE_INSTALL_PREFIX}/lib"
          BUILD_WITH_INSTALL_RPATH ${HDF5_VOL_REST_BUILD_WITH_INSTALL_NAME}
      )
    endif (HDF5_VOL_REST_BUILD_WITH_INSTALL_NAME)
    if (HDF5_VOL_REST_BUILD_FRAMEWORKS)
      if (${libtype} MATCHES "SHARED")
        # adapt target to build frameworks instead of dylibs
        set_target_properties(${libtarget} PROPERTIES
            XCODE_ATTRIBUTE_INSTALL_PATH "@rpath"
            FRAMEWORK TRUE
            FRAMEWORK_VERSION ${HDF5_VOL_REST_PACKAGE_VERSION_MAJOR}
            MACOSX_FRAMEWORK_IDENTIFIER org.hdfgroup.${libtarget}
            MACOSX_FRAMEWORK_SHORT_VERSION_STRING ${HDF5_VOL_REST_PACKAGE_VERSION_MAJOR}
            MACOSX_FRAMEWORK_BUNDLE_VERSION ${HDF5_VOL_REST_PACKAGE_VERSION_MAJOR})
      endif (${libtype} MATCHES "SHARED")
    endif (HDF5_VOL_REST_BUILD_FRAMEWORKS)
  endif (APPLE)
endmacro (HDF5_VOL_REST_SET_LIB_OPTIONS)

#-------------------------------------------------------------------------------
macro (RV_TARGET_C_PROPERTIES wintarget libtype addcompileflags addlinkflags)
  set_target_properties (${wintarget} PROPERTIES COMPILE_FLAGS "${addcompileflags}" LINK_FLAGS "${addlinkflags}")
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
    ${PROJECT_BINARY_DIR}/bin CACHE PATH "Single Directory for all Executables."
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
# which include the HDF5 REST VOL connector as a sub-project within their
# build tree
#-----------------------------------------------------------------------------
set (HDF5_VOL_REST_LIBRARIES_TO_EXPORT "")
set (EXTERNAL_HEADER_LIST "")
set (EXTERNAL_LIBRARY_LIST "")
set (EXTERNAL_LIBRARYDLL_LIST "")

#-----------------------------------------------------------------------------
# Run all the CMake configuration tests for our build environment
#-----------------------------------------------------------------------------
include (${HDF5_VOL_REST_RESOURCES_DIR}/ConfigureChecks.cmake)

set (CMAKE_INCLUDE_CURRENT_DIR_IN_INTERFACE ON)

#-----------------------------------------------------------------------------
# Mac OS X Options
#-----------------------------------------------------------------------------
if (HDF5_VOL_REST_BUILD_FRAMEWORKS AND NOT BUILD_SHARED_LIBS)
  set (BUILD_SHARED_LIBS ON CACHE BOOL "Build Shared Libraries" FORCE)
endif ()

#-----------------------------------------------------------------------------
# Option to Build Shared and Static libs, default is shared
#-----------------------------------------------------------------------------
option (BUILD_SHARED_LIBS "Build Shared Libraries" ON)
set (HDF5_VOL_REST_ENABLE_SHARED_LIB YES)
set (LINK_SHARED_LIBS ${LINK_LIBS})
if (NOT BUILD_SHARED_LIBS)
  set (HDF5_VOL_REST_ENABLE_SHARED_LIB NO)
  set (LINK_SHARED_LIBS )
endif ()
if (BUILD_SHARED_LIBS)
  set (HDF5_VOL_REST_LIBRARIES_TO_EXPORT ${HDF5_VOL_REST_LIBRARIES_TO_EXPORT} ${HDF5_VOL_REST_LIBSH_TARGET} CACHE INTERNAL "Store which libraries should be exported" FORCE)
endif ()

option (BUILD_STATIC_LIBS "Build Static Libraries" OFF)
set (HDF5_VOL_REST_ENABLE_STATIC_LIB NO)
if (BUILD_STATIC_LIBS)
  set (HDF5_VOL_REST_ENABLE_STATIC_LIB YES)
  set (LINK_STATIC_LIBS ${LINK_LIBS})
  set (HDF5_VOL_REST_LIBRARIES_TO_EXPORT ${HDF5_VOL_REST_LIBRARIES_TO_EXPORT} ${HDF5_VOL_REST_LIB_TARGET} CACHE INTERNAL "Store which libraries should be exported" FORCE)
endif ()

set (CMAKE_POSITION_INDEPENDENT_CODE ON)

#-----------------------------------------------------------------------------
# Option to Build Static executables
#-----------------------------------------------------------------------------
option (BUILD_STATIC_EXECS "Build Static Executables" OFF)
if (BUILD_STATIC_EXECS)
  if (NOT WIN32)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
  endif ()
endif ()

#-----------------------------------------------------------------------------
# Option to use code coverage
#-----------------------------------------------------------------------------
option (HDF5_VOL_REST_ENABLE_COVERAGE "Enable code coverage for HDF5 REST VOL connector Libraries and Programs" OFF)
if (HDF5_VOL_REST_ENABLE_COVERAGE)
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 --coverage -fprofile-arcs -ftest-coverage")
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g --coverage -O0 -fprofile-arcs -ftest-coverage")
  if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set (LDFLAGS "${LDFLAGS} -fprofile-arcs -ftest-coverage")
    link_libraries (gcov)
  else ()
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
  endif ()
endif ()

#-----------------------------------------------------------------------------
# Option to linking to HDF5 statically compiled with thread safe option
#-----------------------------------------------------------------------------
option (HDF5_VOL_REST_THREAD_SAFE "Enable linking to HDF5 statically compiled with thread safe option" OFF)
if (HDF5_VOL_REST_THREAD_SAFE)
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -pthread")
  SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
endif()

#-----------------------------------------------------------------------------
# Option to build the examples
#-----------------------------------------------------------------------------
option (HDF5_VOL_REST_ENABLE_EXAMPLES "Indicate that building examples should be enabled" ON)

#-----------------------------------------------------------------------------
# Option to indicate enabling connector debugging output
#-----------------------------------------------------------------------------
option (HDF5_VOL_REST_ENABLE_DEBUG "Indicate that connector debug output is used" OFF)
if (HDF5_VOL_REST_ENABLE_DEBUG)
  set (RV_CONNECTOR_DEBUG 1)
endif ()

#-----------------------------------------------------------------------------
# Option to indicate enabling cURL debugging output
#-----------------------------------------------------------------------------
option (HDF5_VOL_REST_ENABLE_CURL_DEBUG "Indicate that cURL debug output is used" OFF)
if (HDF5_VOL_REST_ENABLE_CURL_DEBUG)
  set (RV_CURL_DEBUG 1)
endif ()

#-----------------------------------------------------------------------------
# Option to indicate using a memory checker
#-----------------------------------------------------------------------------
option (HDF5_VOL_REST_ENABLE_MEM_TRACKING "Indicate that a memory checker is used" OFF)
if (HDF5_VOL_REST_ENABLE_MEM_TRACKING)
  set (RV_TRACK_MEM_USAGE 1)
endif ()

#-----------------------------------------------------------------------------
# Generate the rest_vol_config.h file containing user settings needed by
# compilation
#-----------------------------------------------------------------------------
configure_file (${HDF5_VOL_REST_RESOURCES_DIR}/rv_cmake_config.h.in ${HDF5_VOL_REST_BINARY_DIR}/rest_vol_config.h @ONLY)

#-----------------------------------------------------------------------------
# Include the main src directory and HDF5 distribution directory
#-----------------------------------------------------------------------------

set (HDF5_VOL_REST_INCLUDE_DIRECTORIES
    ${HDF5_VOL_REST_SRC_DIR}
    ${HDF5_VOL_REST_BINARY_DIR}
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

list(APPEND HDF5_VOL_REST_INCLUDE_DIRECTORIES
  ${HDF5_INCLUDE_DIRS}
)

set(HDF5_VOL_REST_INCLUDE_DIRECTORIES 
  ${HDF5_VOL_REST_INCLUDE_DIRECTORIES}
    CACHE 
    INTERNAL 
    "Include directories for HDF5 REST VOL connector"
)

INCLUDE_DIRECTORIES (${HDF5_VOL_REST_INCLUDE_DIRECTORIES})


#-----------------------------------------------------------------------------
# When building utility executables that generate other (source) files :
# we make use of the following variables defined in the root CMakeLists.
# Certain systems may add /Debug or /Release to output paths
# and we need to call the executable from inside the CMake configuration
#-----------------------------------------------------------------------------
set (EXE_EXT "")
if (WIN32 OR MINGW)
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
else ()
  add_definitions (-DNDEBUG)
endif ()

include (${HDF5_VOL_REST_RESOURCES_DIR}/HDFCompilerFlags.cmake)

#-----------------------------------------------------------------------------
# Option to Enable MPI Parallel
#-----------------------------------------------------------------------------
#option (HDF5_VOL_REST_ENABLE_PARALLEL "Enable parallel build (requires MPI)" OFF)
#if (HDF5_VOL_REST_ENABLE_PARALLEL)
#  find_package(MPI REQUIRED)
#  if (MPI_C_FOUND)
#    set (HDF5_VOL_REST_HAVE_PARALLEL 1)
#    # MPI checks, only do these if MPI_C_FOUND is true, otherwise they always fail
#    # and once set, they are cached as false and not regenerated
#    set (CMAKE_REQUIRED_LIBRARIES "${MPI_C_LIBRARIES}")
#    set (CMAKE_REQUIRED_INCLUDES "${MPI_C_INCLUDE_DIRS}")
#    # Used by Fortran + MPI
#    CHECK_SYMBOL_EXISTS (MPI_Comm_c2f "mpi.h" H5_HAVE_MPI_MULTI_LANG_Comm)
#    CHECK_SYMBOL_EXISTS (MPI_Info_c2f "mpi.h" H5_HAVE_MPI_MULTI_LANG_Info)

#    # Used by Parallel Compression feature
#    set (PARALLEL_FILTERED_WRITES ON)
#    CHECK_SYMBOL_EXISTS (MPI_Mprobe "mpi.h" H5_HAVE_MPI_Mprobe)
#    CHECK_SYMBOL_EXISTS (MPI_Imrecv "mpi.h" H5_HAVE_MPI_Imrecv)
#    if (NOT H5_HAVE_MPI_Mprobe OR NOT H5_HAVE_MPI_Imrecv)
#      message (WARNING "The MPI_Mprobe and/or MPI_Imrecv functions could not be located.
#               Parallel writes of filtered data will be disabled.")
#      set (PARALLEL_FILTERED_WRITES OFF)
#    endif ()

#    # Used by big I/O feature
#    set (LARGE_PARALLEL_IO ON)
#    CHECK_SYMBOL_EXISTS (MPI_Get_elements_x "mpi.h" H5_HAVE_MPI_Get_elements_x)
#    CHECK_SYMBOL_EXISTS (MPI_Type_size_x "mpi.h" H5_HAVE_MPI_Type_size_x)
#    if (NOT H5_HAVE_MPI_Get_elements_x OR NOT H5_HAVE_MPI_Type_size_x)
#      message (WARNING "The MPI_Get_elements_x and/or MPI_Type_size_x functions could not be located.
#               Reading/Writing >2GB of data in a single parallel I/O operation will be disabled.")
#      set (LARGE_PARALLEL_IO OFF)
#    endif ()
#  else ()
#    message (FATAL_ERROR "Parallel libraries not found")
#  endif ()
#endif ()

# Parallel IO usage requires MPI to be Linked and Included
#if (H5_HAVE_PARALLEL)
#  set (LINK_LIBS ${LINK_LIBS} ${MPI_C_LIBRARIES})
#  if (MPI_C_LINK_FLAGS)
#    set (CMAKE_EXE_LINKER_FLAGS "${MPI_C_LINK_FLAGS} ${CMAKE_EXE_LINKER_FLAGS}")
#  endif ()
#endif ()

#-----------------------------------------------------------------------------
# Include HDF5 Directories
#-----------------------------------------------------------------------------
include_directories(${HDF5_INCLUDE_DIRS})

if (BUILD_SHARED_LIBS)
  list (APPEND LINK_SHARED_LIBS 
    ${HDF5_C_LIBRARIES}
    ${HDF5_C_HL_LIBRARIES})
endif()

if (BUILD_STATIC_LIBS)
  list (APPEND LINK_LIBS 
    ${HDF5_C_LIBRARIES}
    ${HDF5_C_HL_LIBRARIES})
endif()


#-----------------------------------------------------------------------------
# Build the REST VOL
#-----------------------------------------------------------------------------
add_subdirectory(${HDF5_VOL_REST_SRC_DIR} ${PROJECT_BINARY_DIR}/src)

#-----------------------------------------------------------------------------
# Add the REST VOL examples to the build if they are enabled
#-----------------------------------------------------------------------------
if (HDF5_VOL_REST_ENABLE_EXAMPLES)
  add_subdirectory(${HDF5_VOL_REST_EXAMPLES_DIR} ${PROJECT_BINARY_DIR}/examples)
endif ()

#-----------------------------------------------------------------------------
# Add the REST VOL test Target to the build if testing is enabled
#-----------------------------------------------------------------------------
option (BUILD_TESTING "Build REST VOL Unit Testing" ON)
if (BUILD_TESTING)
  set (DART_TESTING_TIMEOUT 1200
      CACHE STRING
      "Timeout in seconds for each test (default 1200=20minutes)"
  )

  # Generate a list of timeouts based on DART_TESTING_TIMEOUT
  math (EXPR CTEST_SHORT_TIMEOUT "${DART_TESTING_TIMEOUT} / 2")
  math (EXPR CTEST_LONG_TIMEOUT "${DART_TESTING_TIMEOUT} * 2")
  math (EXPR CTEST_VERY_LONG_TIMEOUT "${DART_TESTING_TIMEOUT} * 3")

  enable_testing ()
  include (CTest)

  add_subdirectory(${HDF5_VOL_REST_TEST_SRC_DIR} ${PROJECT_BINARY_DIR}/test)

  include (${HDF5_VOL_REST_SOURCE_DIR}/CTestConfig.cmake)
endif ()

include (CMakeInstallation.cmake)
