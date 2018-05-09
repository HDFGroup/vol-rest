cmake_minimum_required (VERSION 3.1.0)
PROJECT (REST_VOL_HL_EXAMPLES)

#-----------------------------------------------------------------------------
# Apply Definitions to compiler in this directory and below
#-----------------------------------------------------------------------------
add_definitions (${REST_VOL_EXTRA_C_FLAGS})

#-----------------------------------------------------------------------------
# Define Sources
#-----------------------------------------------------------------------------
set (examples
    rv_ds1
)

foreach (example ${examples})
  add_executable (${example} ${REST_VOL_HL_EXAMPLES_SOURCE_DIR}/${example}.c)
  TARGET_C_PROPERTIES (${example} STATIC " " " ")
  target_link_libraries (${example} ${REST_VOL_LIB_TARGET} ${HDF5_LIBRARIES_TO_EXPORT})
  set_target_properties (${example} PROPERTIES FOLDER examples)
  add_dependencies(${example} ${REST_VOL_LIB_TARGET} ${HDF5_LIBRARIES_TO_EXPORT})
  if (BUILD_SHARED_LIBS)
    add_executable (${example}-shared ${REST_VOL_HL_EXAMPLES_SOURCE_DIR}/${example}.c)
    TARGET_C_PROPERTIES (${example}-shared SHARED " " " ")
    target_link_libraries (${example}-shared ${REST_VOL_LIBSH_TARGET} ${HDF5_LIBRARIES_TO_EXPORT})
    set_target_properties (${example}-shared PROPERTIES FOLDER examples)
    add_dependencies(${example}-shared ${REST_VOL_LIBSH_TARGET} ${HDF5_LIBRARIES_TO_EXPORT})
  endif (BUILD_SHARED_LIBS)

  #-----------------------------------------------------------------------------
  # Add Target(s) to CMake Install for import into other projects
  #-----------------------------------------------------------------------------
  if (REST_VOL_EXPORTED_TARGETS)
    install (
        TARGETS
            ${example}
        EXPORT
            ${REST_VOL_EXPORTED_TARGETS}
        DESTINATION
            ${REST_VOL_INSTALL_DATA_DIR}/rest_vol_examples/hl COMPONENT examples
    )
  endif ()
endforeach (example ${examples})

if (BUILD_TESTING)
  include (${REST_VOL_EXAMPLES_SOURCE_DIR}/CMakeTests.cmake)
endif (BUILD_TESTING)
