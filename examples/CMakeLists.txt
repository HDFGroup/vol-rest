cmake_minimum_required (VERSION 3.1.0)
PROJECT (REST_VOL_EXAMPLES)

#-----------------------------------------------------------------------------
# Apply Definitions to compiler in this directory and below
#-----------------------------------------------------------------------------
add_definitions (${REST_VOL_EXTRA_C_FLAGS})

#-----------------------------------------------------------------------------
# Define Sources
#-----------------------------------------------------------------------------
set (examples
    h5_crtdat
    h5_rdwt
    h5_crtatt
    h5_crtgrp
    h5_crtgrpar
    h5_crtgrpd
    h5_subset
    h5_write
    h5_read
    h5_chunk_read
    h5_compound
    h5_group
    h5_select
    h5_attribute
    h5_extlink
)

foreach (example ${examples})
  add_executable (${example} ${REST_VOL_EXAMPLES_SOURCE_DIR}/${example}.c)
  TARGET_NAMING (${example} STATIC)
  TARGET_C_PROPERTIES (${example} STATIC " " " ")
  target_link_libraries (${example} ${REST_VOL_LIB_TARGET})
  set_target_properties (${example} PROPERTIES FOLDER examples)
  if (BUILD_SHARED_LIBS)
    add_executable (${example}-shared ${REST_VOL_EXAMPLES_SOURCE_DIR}/${example}.c)
    TARGET_NAMING (${example}-shared SHARED)
    TARGET_C_PROPERTIES (${example}-shared SHARED " " " ")
    target_link_libraries (${example}-shared ${REST_VOL_LIBSH_TARGET})
    set_target_properties (${example}-shared PROPERTIES FOLDER examples)
  endif (BUILD_SHARED_LIBS)
endforeach (example ${examples})

if (BUILD_TESTING)
  include (CMakeTests.cmake)
endif (BUILD_TESTING)
