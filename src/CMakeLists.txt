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
add_definitions(${RV_EXTRA_C_FLAGS})

#-----------------------------------------------------------------------------
# List source files
#-----------------------------------------------------------------------------
set (RV_SRCS
    ${RV_SRC_DIR}/rest_vol.c
)

set (RV_HDRS
    ${RV_SRC_DIR}/rest_vol.h
    ${RV_SRC_DIR}/rest_vol_public.h
    ${RV_SRC_DIR}/rest_vol_err.h
)
IDE_GENERATED_PROPERTIES("RV" "${RV_HDRS}" "${RV_SRCS}")

set (RV_PUBLIC_HEADERS
    ${RV_SRC_DIR}/rest_vol_public.h
)

#-----------------------------------------------------------------------------
# Add file(s) to CMake Install
#-----------------------------------------------------------------------------
if (NOT RV_INSTALL_NO_DEVELOPMENT)
  install (
      FILES
          ${RV_PUBLIC_HEADERS}
      DESTINATION
          ${RV_INSTALL_INCLUDE_DIR}
      COMPONENT
          headers
  )
endif (NOT RV_INSTALL_NO_DEVELOPMENT)

#-----------------------------------------------------------------------------
# Add Target(s) to CMake Install for import into other projects
#-----------------------------------------------------------------------------
if (RV_EXPORTED_TARGETS)
  if (BUILD_SHARED_LIBS)
    INSTALL_TARGET_PDB (${RV_LIBSH_TARGET} ${RV_INSTALL_BIN_DIR} libraries)
  endif (BUILD_SHARED_LIBS)

  install (
      TARGETS
          ${install_targets}
      EXPORT
          ${RV_EXPORTED_TARGETS}
      LIBRARY DESTINATION ${RV_INSTALL_LIB_DIR} COMPONENT libraries
      ARCHIVE DESTINATION ${RV_INSTALL_LIB_DIR} COMPONENT libraries
      RUNTIME DESTINATION ${RV_INSTALL_BIN_DIR} COMPONENT libraries
      FRAMEWORK DESTINATION ${RV_INSTALL_FWRK_DIR} COMPONENT libraries
  )
endif (RV_EXPORTED_TARGETS)