cmake_minimum_required (VERSION 3.2.2)
project (HDF5App_Java Java)

#-----------------------------------------------------------------------------
# Build the Java Application
#-----------------------------------------------------------------------------

#-----------------------------------------------------------------------------
# Define Sources, one application for multiple files
#-----------------------------------------------------------------------------
include (J_sourcefiles.cmake)

# --------------------------------------------------------------------
# Notes: When creating apps they should be prefixed
# with "j_". This allows for easier filtering of the apps.
# --------------------------------------------------------------------

set (CMAKE_VERBOSE_MAKEFILE 1)

#-----------------------------------------------------------------------------
# Setup include Directories
#-----------------------------------------------------------------------------

INCLUDE_DIRECTORIES (
    ${HDFJAVA_LIB_DIR}
    ${JAVA_INCLUDE_PATH}
    ${JAVA_INCLUDE_PATH2}
)

if (WIN32 AND NOT CYGWIN)
  set (CMAKE_JAVA_INCLUDE_FLAG_SEP ";")
else ()
  set (CMAKE_JAVA_INCLUDE_FLAG_SEP ":")
endif ()

set (CMAKE_JAVA_CLASSPATH ".")
foreach (CMAKE_INCLUDE_PATH ${CMAKE_JAVA_INCLUDE_PATH})
  set (CMAKE_JAVA_CLASSPATH "${CMAKE_JAVA_CLASSPATH}${CMAKE_JAVA_INCLUDE_FLAG_SEP}${CMAKE_INCLUDE_PATH}")
endforeach ()

foreach (HCP_JAR ${CMAKE_JAVA_INCLUDE_PATH})
  get_filename_component (_HCP_FILE ${HCP_JAR} NAME)
  set (HDFJAVA_CLASSJARS "${_HCP_FILE} ${HDFJAVA_CLASSJARS}")
endforeach ()

file (WRITE ${PROJECT_BINARY_DIR}/Manifest.txt
"Main-Class: ${hdf5app_name}
Class-Path: ${HDFJAVA_CLASSJARS}
"
)
ADD_JAR (j_${hdf5app_name} SOURCES ${hdf5app} MANIFEST ${PROJECT_BINARY_DIR}/Manifest.txt)
get_target_property (${hdf5app_name}_JAR_FILE j_${hdf5app_name} JAR_FILE)

if (BUILD_TESTING)
  macro (ADD_H5_TEST testname)
    add_test (
        NAME J_${testname}-clearall
        COMMAND    ${CMAKE_COMMAND}
            -E remove
            ${testname}.h5
    )
    if (COMPARE_TESTING)
      add_test (
          NAME J_${testname}
          COMMAND "${CMAKE_COMMAND}"
              -D "TEST_TESTER=${CMAKE_Java_RUNTIME}"
              -D "TEST_PROGRAM=${testname}"
              -D "TEST_ARGS:STRING=${ARGN}"
              -D "TEST_CLASSPATH:STRING=${CMAKE_JAVA_CLASSPATH}${CMAKE_JAVA_INCLUDE_FLAG_SEP}${${testname}_JAR_FILE}"
              -D "TEST_LIBRARY_DIRECTORY=${HDF5_JAVA_LIBRARIES}"
              -D "TEST_FOLDER=${PROJECT_BINARY_DIR}"
              -D "TEST_OUTPUT=${PROJECT_BINARY_DIR}/${testname}.out"
              -D "TEST_EXPECT=0"
              -D "TEST_REFERENCE=${testname}.tst"
              -P "${HDF5_RESOURCES_DIR}/jrunTest.cmake"
      )
      set_tests_properties (J_${testname} PROPERTIES DEPENDS J_${testname}-clearall)
      if (HDF5_BUILD_TOOLS)
        add_test (
            NAME H5DUMP-J_${testname}
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
        set_tests_properties (H5DUMP-J_${testname} PROPERTIES DEPENDS J_${testname})
      endif ()
    else ()
      add_test (
          NAME J_${testname}
          COMMAND "${CMAKE_COMMAND}"
              -D "TEST_TESTER=${CMAKE_Java_RUNTIME}"
              -D "TEST_PROGRAM=${testname}"
              -D "TEST_ARGS:STRING=${ARGN}"
              -D "TEST_CLASSPATH:STRING=${CMAKE_JAVA_CLASSPATH}${CMAKE_JAVA_INCLUDE_FLAG_SEP}${${testname}_JAR_FILE}"
              -D "TEST_LIBRARY_DIRECTORY=${HDF5_JAVA_LIBRARIES}"
              -D "TEST_FOLDER=${PROJECT_BINARY_DIR}"
              -D "TEST_OUTPUT=${PROJECT_BINARY_DIR}/${testname}.out"
              -D "TEST_EXPECT=0"
              -D "TEST_SKIP_COMPARE=TRUE"
              -P "${HDF5_RESOURCES_DIR}/jrunTest.cmake"
      )
      set_tests_properties (J_${testname} PROPERTIES DEPENDS J_${testname}-clearall)
    endif ()
  endmacro ()

#-----------------------------------------------------------------------------
# Copy the reference files, one output file (*.tst) and one dumper file (*.ddl) per application
#-----------------------------------------------------------------------------
  H5_COPY_REFERENCE_FILES (${hdf5app_name})

  ADD_H5_TEST (${hdf5app_name})
endif ()
