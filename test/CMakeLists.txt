cmake_minimum_required (VERSION 3.12)
PROJECT (HDF5_VOL_REST_TEST C)

#-----------------------------------------------------------------------------
# Apply definitions to compiler in this directory
#-----------------------------------------------------------------------------
add_definitions(${HDF5_VOL_REST_EXTRA_C_FLAGS})

if (BUILD_STATIC_LIBS)
  set (HDF5_VOL_REST_TEST_TARGET "test_rest_vol")
endif ()
if (BUILD_SHARED_LIBS)
  set (HDF5_VOL_REST_TESTSH_TARGET "test_rest_vol-shared")
endif ()

#-----------------------------------------------------------------------------
# Generic VOL tests
#-----------------------------------------------------------------------------
set(HDF5_VOL_TEST_EXTERNALLY_CONFIGURED TRUE)
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/vol-tests/CMakeLists.txt)
  message(STATUS "VOL test submodule not found, checking it out")
  # get newest submodule info
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" submodule update --init --recursive
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_SUBMODULE_UPDATE_OUTPUT
  )
  message(STATUS ${GIT_SUBMODULE_UPDATE_OUTPUT})
endif()
#add_subdirectory(vol-tests)

#-----------------------------------------------------------------------------
# REST VOL specific tests
#-----------------------------------------------------------------------------
set (TEST_LIB_SRCS
    ${HDF5_VOL_REST_TEST_SOURCE_DIR}/test_rest_vol.c
)

MACRO (ADD_HDF5_VOL_REST_EXE file)
  if (BUILD_STATIC_LIBS)
    add_executable (${file} ${HDF5_VOL_REST_TEST_SOURCE_DIR}/${file}.c)
    RV_TARGET_C_PROPERTIES (${file} STATIC " " " ")
    target_link_libraries (${file} PUBLIC ${HDF5_VOL_REST_LIB_TARGET})
    set_target_properties (${file} PROPERTIES FOLDER test)
    add_dependencies(${file} ${HDF5_VOL_REST_LIB_TARGET})
  endif ()
  if (BUILD_SHARED_LIBS)
    add_executable (${file}-shared ${HDF5_VOL_REST_TEST_SOURCE_DIR}/${file}.c)
    RV_TARGET_C_PROPERTIES (${file}-shared SHARED " " " ")
    target_link_libraries (${file}-shared PUBLIC ${HDF5_VOL_REST_LIBSH_TARGET})
    set_target_properties (${file}-shared PROPERTIES FOLDER test)
    add_dependencies(${file}-shared ${HDF5_VOL_REST_LIBSH_TARGET})
  endif (BUILD_SHARED_LIBS)
ENDMACRO (ADD_HDF5_VOL_REST_EXE file)

set (HDF5_VOL_REST_TESTS
    test_rest_vol
)

foreach (test ${HDF5_VOL_REST_TESTS})
  ADD_HDF5_VOL_REST_EXE(${test})
endforeach (test ${HDF5_VOL_REST_TESTS})

if (BUILD_STATIC_LIBS)
  foreach (test ${HDF5_VOL_REST_TESTS})
    if (${test} STREQUAL "big" AND CYGWIN)
      add_test (
          NAME RVTEST-${test}
          COMMAND ${CMAKE_COMMAND} -E echo "SKIP ${test}"
      )
    else (${test} STREQUAL "big" AND CYGWIN)
      add_test (NAME RVTEST-${test} COMMAND $<TARGET_FILE:${test}>)
    endif (${test} STREQUAL "big" AND CYGWIN)
    set_tests_properties (RVTEST-${test} PROPERTIES
        ENVIRONMENT "srcdir=${HDF5_VOL_REST_TEST_BINARY_DIR}"
        WORKING_DIRECTORY ${HDF5_VOL_REST_TEST_BINARY_DIR}
    )
  endforeach (test ${HDF5_VOL_REST_TESTS})
endif ()

if (BUILD_SHARED_LIBS)
  foreach (test ${HDF5_VOL_REST_TESTS})
    if (${test} STREQUAL "big" AND CYGWIN)
      add_test (
          NAME RVTEST-shared-${test}
          COMMAND ${CMAKE_COMMAND} -E echo "SKIP ${test}-shared"
      )
    else (${test} STREQUAL "big" AND CYGWIN)
      add_test (NAME RVTEST-shared-${test} COMMAND $<TARGET_FILE:${test}-shared>)
    endif (${test} STREQUAL "big" AND CYGWIN)
    set_tests_properties (RVTEST-shared-${test} PROPERTIES
        ENVIRONMENT "srcdir=${HDF5_VOL_REST_TEST_BINARY_DIR}"
        WORKING_DIRECTORY ${HDF5_VOL_REST_TEST_BINARY_DIR}
    )
  endforeach (test ${HDF5_VOL_REST_TESTS})
endif (BUILD_SHARED_LIBS)

#-----------------------------------------------------------------------------
# Add Target(s) to CMake Install for import into other projects
#-----------------------------------------------------------------------------
if (HDF5_VOL_REST_EXPORTED_TARGETS)
  if (BUILD_STATIC_LIBS)
    install (
        TARGETS
            ${HDF5_VOL_REST_TEST_TARGET}
        EXPORT
            ${HDF5_VOL_REST_EXPORTED_TARGETS}
        DESTINATION
            ${HDF5_VOL_REST_INSTALL_BIN_DIR} COMPONENT testing
    )
  endif()
  if (BUILD_SHARED_LIBS)
    install (
        TARGETS
            ${HDF5_VOL_REST_TESTSH_TARGET}
        EXPORT
            ${HDF5_VOL_REST_EXPORTED_TARGETS}
        DESTINATION
            ${HDF5_VOL_REST_INSTALL_BIN_DIR} COMPONENT testing
    )
  endif ()
endif ()
