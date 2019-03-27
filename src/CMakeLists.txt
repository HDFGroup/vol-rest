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

#-----------------------------------------------------------------------------
# Add in source files from the "util" diectory
#-----------------------------------------------------------------------------
add_subdirectory(util ${PROJECT_BINARY_DIR}/src/util)

IDE_GENERATED_PROPERTIES("REST_VOL" "${REST_VOL_HDRS}" "${REST_VOL_SRCS}")

set (REST_VOL_PUBLIC_HEADERS
    ${REST_VOL_SRC_DIR}/rest_vol_public.h
)

add_library (${REST_VOL_LIB_TARGET} STATIC ${REST_VOL_SRCS} ${REST_VOL_HDRS})
TARGET_C_PROPERTIES (${REST_VOL_LIB_TARGET} STATIC " " " ")
target_link_libraries (${REST_VOL_LIB_TARGET} PUBLIC ${HDF5_LIBRARIES_TO_EXPORT} ${LINK_LIBS})
if (NOT WIN32)
  target_link_libraries (${REST_VOL_LIB_TARGET} PUBLIC dl)
endif ()
set (REST_VOL_LIBRARIES_TO_EXPORT ${REST_VOL_LIB_TARGET} CACHE INTERNAL "Used to pass variables between directories" FORCE)
set_target_properties (${REST_VOL_LIB_TARGET} PROPERTIES
    FOLDER libraries
    INTERFACE_INCLUDE_DIRECTORIES "$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include>"
)
if (WIN32)
  set_target_properties (${REST_VOL_LIB_TARGET} PROPERTIES
    OUTPUT_NAME lib${REST_VOL_LIB_CORENAME}
  )
else ()
  set_target_properties (${REST_VOL_LIB_TARGET} PROPERTIES
    OUTPUT_NAME ${REST_VOL_LIB_CORENAME}
  )
endif()

#add_dependencies(${REST_VOL_LIB_TARGET} ${HDF5_LIBRARIES_TO_EXPORT})

set (install_targets ${REST_VOL_LIB_TARGET})

if (BUILD_SHARED_LIBS)
  file (MAKE_DIRECTORY "${REST_VOL_BINARY_DIR}/shared")
  add_library (${REST_VOL_LIBSH_TARGET} SHARED ${REST_VOL_SRCS} ${REST_VOL_HDRS})
  TARGET_C_PROPERTIES (${REST_VOL_LIBSH_TARGET} SHARED " " " ")
  target_link_libraries (${REST_VOL_LIBSH_TARGET} PUBLIC ${HDF5_LIBRARIES_TO_EXPORT} ${LINK_SHARED_LIBS})
  if (NOT WIN32)
    target_link_libraries (${REST_VOL_LIBSH_TARGET} PUBLIC dl)
  endif ()
  set (REST_VOL_LIBRARIES_TO_EXPORT "${REST_VOL_LIBRARIES_TO_EXPORT};${REST_VOL_LIBSH_TARGET}" CACHE INTERNAL "Used to pass variables between directories" FORCE)
  set_target_properties (${REST_VOL_LIBSH_TARGET} PROPERTIES
      OUTPUT_NAME ${REST_VOL_LIB_CORENAME}
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
if (PREBUILT_HDF5_DIR)
  add_custom_target(
    rest_vol_pubconf_depend
    DEPENDS ${PREBUILT_HDF5_DIR}/include/H5pubconf.h
  )
else ()
  add_custom_target(
    rest_vol_pubconf_depend
    DEPENDS ${CMAKE_BINARY_DIR}/${HDF5_DIR_NAME}/H5pubconf.h
  )
endif ()

add_dependencies(${REST_VOL_LIB_TARGET} rest_vol_pubconf_depend)

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

#-----------------------------------------------------------------------------
# Create pkgconfig files
#-----------------------------------------------------------------------------
set (_PKG_CONFIG_PREFIX ${CMAKE_INSTALL_PREFIX})
set (_PKG_CONFIG_EXEC_PREFIX \${prefix})
set (_PKG_CONFIG_LIBDIR \${exec_prefix}/lib)
set (_PKG_CONFIG_INCLUDEDIR \${prefix}/include)
set (_PKG_CONFIG_LIBNAME "${REST_VOL_LIB_CORENAME}")
set (_PKG_CONFIG_VERSION "${REST_VOL_PACKAGE_VERSION}")

foreach (libs ${LINK_LIBS} ${LINK_COMP_LIBS})
  set (_PKG_CONFIG_LIBS_PRIVATE "${_PKG_CONFIG_LIBS_PRIVATE} -l${libs}")
endforeach ()

set (_PKG_CONFIG_LIBS "${_PKG_CONFIG_LIBS} -l${REST_VOL_LIB_CORENAME}")
if (BUILD_SHARED_LIBS)
  set (_PKG_CONFIG_SH_LIBS "${_PKG_CONFIG_SH_LIBS} -l${REST_VOL_LIB_CORENAME}")
endif ()

set (_PKG_CONFIG_REQUIRES "hdf5")
set (_PKG_CONFIG_REQUIRES_PRIVATE "hdf5")

configure_file (
    ${REST_VOL_RESOURCES_DIR}/librestvol.pc.in
    ${REST_VOL_BINARY_DIR}/CMakeFiles/${REST_VOL_LIB_CORENAME}-${REST_VOL_PACKAGE_VERSION}.pc
    @ONLY
)
install (
    FILES ${REST_VOL_BINARY_DIR}/CMakeFiles/${REST_VOL_LIB_CORENAME}-${REST_VOL_PACKAGE_VERSION}.pc
    DESTINATION ${REST_VOL_INSTALL_LIB_DIR}/pkgconfig
    COMPONENT libraries
)

if (NOT WIN32)
  set (_PKG_CONFIG_COMPILER ${CMAKE_C_COMPILER})
  configure_file (
      ${REST_VOL_RESOURCES_DIR}/rvcc.in
      ${REST_VOL_BINARY_DIR}/CMakeFiles/rvcc
      @ONLY
  )
  install (
      FILES ${REST_VOL_BINARY_DIR}/CMakeFiles/rvcc
      DESTINATION ${REST_VOL_INSTALL_BIN_DIR}
      PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
      COMPONENT libraries
  )
endif ()