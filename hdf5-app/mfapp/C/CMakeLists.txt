cmake_minimum_required (VERSION 3.2.2)
project (HDF5App_C)

#-----------------------------------------------------------------------------
# Build the C Application
#-----------------------------------------------------------------------------

#-----------------------------------------------------------------------------
# Define Sources, one application for multiple files
#-----------------------------------------------------------------------------
include (C_sourcefiles.cmake)

# --------------------------------------------------------------------
# Notes: When creating apps they should be prefixed
# with "c_". This allows for easier filtering of the apps.
# --------------------------------------------------------------------

add_executable (c_${hdf5app_name} ${hdf5app})
TARGET_NAMING (c_${hdf5app_name} ${LIB_TYPE})
target_link_libraries (c_${hdf5app_name} ${LINK_LIBS})

if (BUILD_TESTING)
  macro (ADD_H5_TEST testname)
    add_test (
        NAME C_${testname}-clearall
        COMMAND    ${CMAKE_COMMAND}
            -E remove
            ${testname}.h5
    )
    if (COMPARE_TESTING)
      add_test (
          NAME C_${testname}
          COMMAND "${CMAKE_COMMAND}"
              -D "TEST_PROGRAM=$<TARGET_FILE:c_${testname}>"
              -D "TEST_ARGS:STRING=${ARGN}"
              -D "TEST_FOLDER=${PROJECT_BINARY_DIR}"
              -D "TEST_OUTPUT=${testname}.out"
              -D "TEST_EXPECT=0"
              -D "TEST_REFERENCE=${testname}.tst"
              -D "TEST_LIBRARY_DIRECTORY=${CMAKE_TEST_LIB_DIRECTORY}"
              -P "${HDF5_RESOURCES_DIR}/runTest.cmake"
      )
      set_tests_properties (C_${testname} PROPERTIES DEPENDS C_${testname}-clearall)
      if (HDF5_BUILD_TOOLS)
        add_test (
            NAME H5DUMP-C_${testname}
            COMMAND "${CMAKE_COMMAND}"
                -D "TEST_PROGRAM=$<TARGET_FILE:hdf5::h5dump>"
                -D "TEST_ARGS:STRING=-p;${testname}.h5"
                -D "TEST_FOLDER=${PROJECT_BINARY_DIR}"
                -D "TEST_OUTPUT=${testname}.ddl.out"
                -D "TEST_EXPECT=0"
                -D "TEST_REFERENCE=${testname}.ddl"
                -D "TEST_LIBRARY_DIRECTORY=${CMAKE_TEST_LIB_DIRECTORY}"
                -P "${HDF5_RESOURCES_DIR}/runTest.cmake"
        )
        set_tests_properties (H5DUMP-C_${testname} PROPERTIES DEPENDS C_${testname})
      endif ()
    else ()
      add_test (
          NAME C_${testname}
          COMMAND "${CMAKE_COMMAND}"
              -D "TEST_PROGRAM=$<TARGET_FILE:c_${testname}>"
              -D "TEST_ARGS:STRING=${ARGN}"
              -D "TEST_FOLDER=${PROJECT_BINARY_DIR}"
              -D "TEST_OUTPUT=${testname}.out"
              -D "TEST_EXPECT=0"
              -D "TEST_SKIP_COMPARE=TRUE"
              -D "TEST_LIBRARY_DIRECTORY=${CMAKE_TEST_LIB_DIRECTORY}"
              -P "${HDF5_RESOURCES_DIR}/runTest.cmake"
      )
      set_tests_properties (C_${testname} PROPERTIES DEPENDS C_${testname}-clearall)
    endif ()
  endmacro ()

#-----------------------------------------------------------------------------
# Copy the reference files, one output file (*.tst) and one dumper file (*.ddl) for the application
#-----------------------------------------------------------------------------
  H5_COPY_REFERENCE_FILES (${hdf5app_name})

  ADD_H5_TEST (${hdf5app_name})
endif ()
