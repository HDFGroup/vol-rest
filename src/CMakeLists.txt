cmake_minimum_required (VERSION 3.1.0)
PROJECT (REST_VOL_SRC C)

# Locate cURL and YAJL before building
find_package(CURL)

macro (IDE_GENERATED_PROPERTIES SOURCE_PATH HEADERS SOURCES)
  #set(source_group_path "Source/AIM/${NAME}")
  string (REPLACE "/" "\\\\" source_group_path ${SOURCE_PATH})
  source_group (${source_group_path} FILES ${HEADERS} ${SOURCES})

  #-- The following is needed if we ever start to use OS X Frameworks but only
  #--  works on CMake 2.6 and greater
  #set_property (SOURCE ${HEADERS}
  #       PROPERTY MACOSX_PACKAGE_LOCATION Headers/${NAME}
  #)
endmacro (IDE_GENERATED_PROPERTIES)

#-----------------------------------------------------------------------------
# Apply definitions to compiler in this directory
#-----------------------------------------------------------------------------
add_definitions(${REST_VOL_EXTRA_C_FLAGS})

#-----------------------------------------------------------------------------
# List source files
#-----------------------------------------------------------------------------
set (REST_VOL_SRCS
    ${REST_VOL_SRC_DIR}/rest_vol.c
)

set (REST_VOL_HDRS
    ${REST_VOL_SRC_DIR}/rest_vol.h
    ${REST_VOL_SRC_DIR}/rest_vol_public.h
    ${REST_VOL_SRC_DIR}/rest_vol_err.h
)
IDE_GENERATED_PROPERTIES("REST_VOL" "${REST_VOL_HDRS}" "${REST_VOL_SRCS}")

set (REST_VOL_PUBLIC_HEADERS
    ${REST_VOL_SRC_DIR}/rest_vol_public.h
)

#-----------------------------------------------------------------------------
# Add file(s) to CMake Install
#-----------------------------------------------------------------------------
if (NOT REST_VOL_INSTALL_NO_DEVELOPMENT)
  install (
      FILES
          ${REST_VOL_PUBLIC_HEADERS}
      DESTINATION
          ${REST_VOL_INSTALL_INCLUDE_DIR}
      COMPONENT
          headers
  )
endif (NOT REST_VOL_INSTALL_NO_DEVELOPMENT)

#-----------------------------------------------------------------------------
# Add Target(s) to CMake Install for import into other projects
#-----------------------------------------------------------------------------
if (REST_VOL_EXPORTED_TARGETS)
  if (BUILD_SHARED_LIBS)
    INSTALL_TARGET_PDB (${REST_VOL_LIBSH_TARGET} ${REST_VOL_INSTALL_BIN_DIR} libraries)
  endif (BUILD_SHARED_LIBS)

  install (
      TARGETS
          ${install_targets}
      EXPORT
          ${REST_VOL_EXPORTED_TARGETS}
      LIBRARY DESTINATION ${REST_VOL_INSTALL_LIB_DIR} COMPONENT libraries
      ARCHIVE DESTINATION ${REST_VOL_INSTALL_LIB_DIR} COMPONENT libraries
      RUNTIME DESTINATION ${REST_VOL_INSTALL_BIN_DIR} COMPONENT libraries
      FRAMEWORK DESTINATION ${REST_VOL_INSTALL_FWRK_DIR} COMPONENT libraries
  )
endif (REST_VOL_EXPORTED_TARGETS)