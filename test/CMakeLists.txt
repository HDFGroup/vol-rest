cmake_minimum_required (VERSION 3.1.0)
PROJECT (REST_VOL_TEST C)

#-----------------------------------------------------------------------------
# Apply definitions to compiler in this directory
#-----------------------------------------------------------------------------
add_definitions(${RV_EXTRA_C_FLAGS})

#-----------------------------------------------------------------------------
# Define test sources
#-----------------------------------------------------------------------------
set (TEST_LIB_SRCS
    ${REST_VOL_TEST_SOURCE_DIR}/test_rest_vol.c
)

MACRO (ADD_RV_EXE file)
  add_executable (${file} ${REST_VOL_TEST_SOURCE_DIR}/${file}.c)
  TARGET_NAMING (${file} STATIC)
  TARGET_C_PROPERTIES (${file} STATIC " " " ")
  target_link_libraries (${file} ${REST_VOL_LIB_TARGET} ${HDF5_LIBRARIES_TO_EXPORT})
  set_target_properties (${file} PROPERTIES FOLDER test)
  add_dependencies(${file} ${REST_VOL_LIB_TARGET} ${HDF5_LIBRARIES_TO_EXPORT})
  if (BUILD_SHARED_LIBS)
    add_executable (${file}-shared ${REST_VOL_TEST_SOURCE_DIR}/${file}.c)
    TARGET_NAMING (${file}-shared SHARED)
    TARGET_C_PROPERTIES (${file}-shared SHARED " " " ")
    target_link_libraries (${file}-shared ${RV_LIBSH_TARGET} ${HDF5_LIBRARIES_TO_EXPORT})
    set_target_properties (${file}-shared PROPERTIES FOLDER test)
    add_dependencies(${file}-shared ${REST_VOL_LIBSH_TARGET} ${HDF5_LIBRARIES_TO_EXPORT})
  endif (BUILD_SHARED_LIBS)
ENDMACRO (ADD_RV_EXE file)

set (REST_VOL_TESTS
    test_rest_vol
)

foreach (test ${REST_VOL_TESTS})
    ADD_RV_EXE(${test})
endforeach (test ${REST_VOL_TESTS})

foreach (test ${REST_VOL_TESTS})
  if (${test} STREQUAL "big" AND CYGWIN)
    add_test (
        NAME RVTEST-${test}
        COMMAND ${CMAKE_COMMAND} -E echo "SKIP ${test}"
    )
  else (${test} STREQUAL "big" AND CYGWIN)
    add_test (NAME RVTEST-${test} COMMAND $<TARGET_FILE:${test}>)
  endif (${test} STREQUAL "big" AND CYGWIN)
  set_tests_properties (RVTEST-${test} PROPERTIES
      ENVIRONMENT "srcdir=${REST_VOL_TEST_BINARY_DIR}"
      WORKING_DIRECTORY ${REST_VOL_TEST_BINARY_DIR}
  )
endforeach (test ${REST_VOL_TESTS})

if (BUILD_SHARED_LIBS)
  foreach (test ${REST_VOL_TESTS})
    if (${test} STREQUAL "big" AND CYGWIN)
      add_test (
          NAME RVTEST-shared-${test}
          COMMAND ${CMAKE_COMMAND} -E echo "SKIP ${test}-shared"
      )
    else (${test} STREQUAL "big" AND CYGWIN)
      add_test (NAME RVTEST-shared-${test} COMMAND $<TARGET_FILE:${test}-shared>)
    endif (${test} STREQUAL "big" AND CYGWIN)
    set_tests_properties (RVTEST-shared-${test} PROPERTIES
        ENVIRONMENT "srcdir=${REST_VOL_TEST_BINARY_DIR}"
        WORKING_DIRECTORY ${REST_VOL_TEST_BINARY_DIR}
    )
  endforeach (test ${REST_VOL_TESTS})
endif (BUILD_SHARED_LIBS)