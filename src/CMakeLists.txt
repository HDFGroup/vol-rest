cmake_minimum_required (VERSION 3.1.0)
PROJECT (REST_VOL_SRC C)

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

add_library (${REST_VOL_LIB_TARGET} STATIC ${REST_VOL_SRCS} ${REST_VOL_HDRS})
TARGET_C_PROPERTIES (${REST_VOL_LIB_TARGET} STATIC " " " ")
target_link_libraries (${REST_VOL_LIB_TARGET} ${HDF5_LIBRARIES_TO_EXPORT} ${LINK_LIBS})
if (NOT WIN32)
  target_link_libraries (${REST_VOL_LIB_TARGET} dl)
endif ()
set (REST_VOL_LIBRARIES_TO_EXPORT ${REST_VOL_LIB_TARGET} CACHE INTERNAL "Used to pass variables between directories" FORCE)
set_target_properties (${REST_VOL_LIB_TARGET} PROPERTIES
    FOLDER libraries
    INTERFACE_INCLUDE_DIRECTORIES "$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include>"
)

#add_dependencies(${REST_VOL_LIB_TARGET} ${HDF5_LIBRARIES_TO_EXPORT})

set (install_targets ${REST_VOL_LIB_TARGET})

if (BUILD_SHARED_LIBS)
  file (MAKE_DIRECTORY "${REST_VOL_BINARY_DIR}/shared")
  add_library (${REST_VOL_LIBSH_TARGET} SHARED ${REST_VOL_SRCS} ${REST_VOL_HDRS})
  TARGET_C_PROPERTIES (${REST_VOL_LIBSH_TARGET} SHARED " " " ")
  target_link_libraries (${REST_VOL_LIBSH_TARGET} ${HDF5_LIBRARIES_TO_EXPORT} ${LINK_SHARED_LIBS})
  if (NOT WIN32)
    target_link_libraries (${REST_VOL_LIBSH_TARGET} dl)
  endif ()
  set (REST_VOL_LIBRARIES_TO_EXPORT "${REST_VOL_LIBRARIES_TO_EXPORT};${REST_VOL_LIBSH_TARGET}" CACHE INTERNAL "Used to pass variables between directories" FORCE)
  set_target_properties (${REST_VOL_LIBSH_TARGET} PROPERTIES
      FOLDER libraries
      COMPILE_DEFINITIONS "REST_VOL_BUILT_AS_DYNAMIC_LIB"
      INTERFACE_INCLUDE_DIRECTORIES "$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include>"
      INTERFACE_COMPILE_DEFINITIONS REST_VOL_BUILT_AS_DYNAMIC_LIB=1
  )
  
  #add_dependencies(${REST_VOL_LIBSH_TARGET} ${HDF5_LIBRARIES_TO_EXPORT})

  set (install_targets ${install_targets} ${REST_VOL_LIBSH_TARGET})
endif ()

#-----------------------------------------------------------------------------
# Make the REST VOL depend on H5pubconf.h existing
#-----------------------------------------------------------------------------
add_custom_target(
    pubconf
    DEPENDS ${CMAKE_BINARY_DIR}/${HDF5_SOURCE_NAME}/H5pubconf.h
)

add_dependencies(${REST_VOL_LIB_TARGET} pubconf)

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
endif ()

#-----------------------------------------------------------------------------
# Add Target(s) to CMake Install for import into other projects
#-----------------------------------------------------------------------------
if (REST_VOL_EXPORTED_TARGETS)
  if (BUILD_SHARED_LIBS)
    INSTALL_TARGET_PDB (${REST_VOL_LIBSH_TARGET} ${REST_VOL_INSTALL_BIN_DIR} libraries)
  endif ()

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
endif ()