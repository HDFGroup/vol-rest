cmake_minimum_required (VERSION 3.1.0...3.29)

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
add_definitions(${HDF5_VOL_REST_EXTRA_C_FLAGS})

#-----------------------------------------------------------------------------
# List source files
#-----------------------------------------------------------------------------
set (HDF5_VOL_REST_SRCS
    ${HDF5_VOL_REST_SRC_DIR}/rest_vol.c
    ${HDF5_VOL_REST_SRC_DIR}/rest_vol_attr.c
    ${HDF5_VOL_REST_SRC_DIR}/rest_vol_dataset.c
    ${HDF5_VOL_REST_SRC_DIR}/rest_vol_datatype.c
    ${HDF5_VOL_REST_SRC_DIR}/rest_vol_file.c
    ${HDF5_VOL_REST_SRC_DIR}/rest_vol_group.c
    ${HDF5_VOL_REST_SRC_DIR}/rest_vol_link.c
    ${HDF5_VOL_REST_SRC_DIR}/rest_vol_object.c
    ${HDF5_VOL_REST_UTIL_DIR}/rest_vol_mem.c
    ${HDF5_VOL_REST_UTIL_DIR}/rest_vol_hash_table.c
)

if (HDF5_VOL_REST_ENABLE_DEBUG)
  set (HDF5_VOL_REST_SRCS ${HDF5_VOL_REST_SRCS} ${HDF5_VOL_REST_SRC_DIR}/rest_vol_debug.c)
endif ()

set (HDF5_VOL_REST_HDRS
    ${HDF5_VOL_REST_SRC_DIR}/rest_vol.h
    ${HDF5_VOL_REST_SRC_DIR}/rest_vol_attr.h
    ${HDF5_VOL_REST_SRC_DIR}/rest_vol_dataset.h
    ${HDF5_VOL_REST_SRC_DIR}/rest_vol_datatype.h
    ${HDF5_VOL_REST_SRC_DIR}/rest_vol_file.h
    ${HDF5_VOL_REST_SRC_DIR}/rest_vol_group.h
    ${HDF5_VOL_REST_SRC_DIR}/rest_vol_link.h
    ${HDF5_VOL_REST_SRC_DIR}/rest_vol_object.h
    ${HDF5_VOL_REST_SRC_DIR}/rest_vol_public.h
    ${HDF5_VOL_REST_UTIL_DIR}/rest_vol_mem.h
    ${HDF5_VOL_REST_UTIL_DIR}/rest_vol_err.h
    ${HDF5_VOL_REST_UTIL_DIR}/rest_vol_hash_table.h
    ${HDF5_VOL_REST_UTIL_DIR}/rest_vol_hash_string.h
)

if (HDF5_VOL_REST_ENABLE_DEBUG)
  set (HDF5_VOL_REST_HDRS ${HDF5_VOL_REST_HDRS} ${HDF5_VOL_REST_SRC_DIR}/rest_vol_debug.h)
endif ()

IDE_GENERATED_PROPERTIES("HDF5_VOL_REST" "${HDF5_VOL_REST_HDRS}" "${HDF5_VOL_REST_SRCS}")

set (HDF5_VOL_REST_PUBLIC_HEADERS
    ${HDF5_VOL_REST_SRC_DIR}/rest_vol_public.h
)

if (BUILD_STATIC_LIBS)
  add_library (${HDF5_VOL_REST_LIB_TARGET} STATIC ${HDF5_VOL_REST_SRCS} ${HDF5_VOL_REST_HDRS})
  RV_TARGET_C_PROPERTIES (${HDF5_VOL_REST_LIB_TARGET} STATIC " " " ")
  target_link_libraries (${HDF5_VOL_REST_LIB_TARGET} PUBLIC ${LINK_LIBS})

  if (NOT WIN32)
    target_link_libraries (${HDF5_VOL_REST_LIB_TARGET} PRIVATE dl)
  endif ()
  set_target_properties (${HDF5_VOL_REST_LIB_TARGET} PROPERTIES
    FOLDER libraries
    INTERFACE_INCLUDE_DIRECTORIES "$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include>"
  )
  if (WIN32)
    set_target_properties (${HDF5_VOL_REST_LIB_TARGET} PROPERTIES
      OUTPUT_NAME lib${HDF5_VOL_REST_LIB_CORENAME}
    )
  else ()
    set_target_properties (${HDF5_VOL_REST_LIB_TARGET} PROPERTIES
      OUTPUT_NAME ${HDF5_VOL_REST_LIB_CORENAME}
    )
  endif ()

  #add_dependencies(${HDF5_VOL_REST_LIB_TARGET} ${HDF5_LIBRARIES})
endif ()

if (BUILD_SHARED_LIBS)
  file (MAKE_DIRECTORY "${HDF5_VOL_REST_BINARY_DIR}/shared")
  add_library (${HDF5_VOL_REST_LIBSH_TARGET} SHARED ${HDF5_VOL_REST_SRCS} ${HDF5_VOL_REST_HDRS})
  RV_TARGET_C_PROPERTIES (${HDF5_VOL_REST_LIBSH_TARGET} SHARED " " " ")
  target_link_libraries (${HDF5_VOL_REST_LIBSH_TARGET} PUBLIC ${LINK_SHARED_LIBS})
  if (NOT WIN32)
    target_link_libraries (${HDF5_VOL_REST_LIBSH_TARGET} PRIVATE dl)
  endif ()
  set_target_properties (${HDF5_VOL_REST_LIBSH_TARGET} PROPERTIES
      OUTPUT_NAME ${HDF5_VOL_REST_LIB_CORENAME}
      FOLDER libraries
      COMPILE_DEFINITIONS "HDF5_VOL_REST_BUILT_AS_DYNAMIC_LIB"
      INTERFACE_INCLUDE_DIRECTORIES "$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include>"
      INTERFACE_COMPILE_DEFINITIONS HDF5_VOL_REST_BUILT_AS_DYNAMIC_LIB=1
  )

  #add_dependencies(${HDF5_VOL_REST_LIBSH_TARGET} ${HDF5_LIBRARIES})
endif ()

#-----------------------------------------------------------------------------
# Add file(s) to CMake Install
#-----------------------------------------------------------------------------
if (NOT HDF5_VOL_REST_INSTALL_NO_DEVELOPMENT)
  install (
      FILES ${HDF5_VOL_REST_PUBLIC_HEADERS}
      DESTINATION ${HDF5_VOL_REST_INSTALL_INCLUDE_DIR} COMPONENT headers
  )
endif ()

#-----------------------------------------------------------------------------
# Add Target(s) to CMake Install for import into other projects
#-----------------------------------------------------------------------------
if (HDF5_VOL_REST_EXPORTED_TARGETS)
  if (BUILD_SHARED_LIBS)
    INSTALL_TARGET_PDB (${HDF5_VOL_REST_LIBSH_TARGET} ${HDF5_VOL_REST_INSTALL_BIN_DIR} libraries)
  endif ()

  install (
      TARGETS ${HDF5_VOL_REST_LIBRARIES_TO_EXPORT}
      EXPORT ${HDF5_VOL_REST_EXPORTED_TARGETS}
      LIBRARY DESTINATION ${HDF5_VOL_REST_INSTALL_LIB_DIR} COMPONENT libraries
      ARCHIVE DESTINATION ${HDF5_VOL_REST_INSTALL_LIB_DIR} COMPONENT libraries
      RUNTIME DESTINATION ${HDF5_VOL_REST_INSTALL_BIN_DIR} COMPONENT libraries
      FRAMEWORK DESTINATION ${HDF5_VOL_REST_INSTALL_FWRK_DIR} COMPONENT libraries
  )
  install (
      EXPORT ${HDF5_VOL_REST_EXPORTED_TARGETS}
      FILE ${HDF5_VOL_REST_EXPORTED_TARGETS}.cmake
      DESTINATION ${HDF5_VOL_REST_INSTALL_CMAKE_DIR} COMPONENT configinstall
  )
endif ()

#-----------------------------------------------------------------------------
# Create pkgconfig files
#-----------------------------------------------------------------------------
set (_PKG_CONFIG_PREFIX ${CMAKE_INSTALL_PREFIX})
set (_PKG_CONFIG_EXEC_PREFIX \${prefix})
set (_PKG_CONFIG_LIBDIR \${exec_prefix}/lib)
set (_PKG_CONFIG_INCLUDEDIR \${prefix}/include)
set (_PKG_CONFIG_LIBNAME "${HDF5_VOL_REST_LIB_CORENAME}")
set (_PKG_CONFIG_VERSION "${HDF5_VOL_REST_PACKAGE_VERSION}")

foreach (libs ${LINK_LIBS} ${LINK_COMP_LIBS})
  set (_PKG_CONFIG_LIBS_PRIVATE "${_PKG_CONFIG_LIBS_PRIVATE} -l${libs}")
endforeach ()

set (_PKG_CONFIG_LIBS "${_PKG_CONFIG_LIBS} -l${HDF5_VOL_REST_LIB_CORENAME}")
if (BUILD_SHARED_LIBS)
  set (_PKG_CONFIG_SH_LIBS "${_PKG_CONFIG_SH_LIBS} -l${HDF5_VOL_REST_LIB_CORENAME}")
endif ()

set (_PKG_CONFIG_REQUIRES "hdf5")
set (_PKG_CONFIG_REQUIRES_PRIVATE "hdf5")

configure_file (
    ${HDF5_VOL_REST_RESOURCES_DIR}/libhdf5_vol_rest.pc.in
    ${HDF5_VOL_REST_BINARY_DIR}/CMakeFiles/${HDF5_VOL_REST_LIB_CORENAME}-${HDF5_VOL_REST_PACKAGE_VERSION}.pc
    @ONLY
)
install (
    FILES ${HDF5_VOL_REST_BINARY_DIR}/CMakeFiles/${HDF5_VOL_REST_LIB_CORENAME}-${HDF5_VOL_REST_PACKAGE_VERSION}.pc
    DESTINATION ${HDF5_VOL_REST_INSTALL_LIB_DIR}/pkgconfig
    COMPONENT libraries
)

if (NOT WIN32)
  set (_PKG_CONFIG_COMPILER ${CMAKE_C_COMPILER})
  configure_file (
      ${HDF5_VOL_REST_RESOURCES_DIR}/rvcc.in
      ${HDF5_VOL_REST_BINARY_DIR}/CMakeFiles/rvcc
      @ONLY
  )
  install (
      FILES ${HDF5_VOL_REST_BINARY_DIR}/CMakeFiles/rvcc
      DESTINATION ${HDF5_VOL_REST_INSTALL_BIN_DIR}
      PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
      COMPONENT libraries
  )
endif ()
