#-------------------------------------------------------------------------------
macro (H5_SET_LIB_OPTIONS libtarget libname libtype)
  set (LIB_OUT_NAME "${libname}")
  if (${libtype} MATCHES "SHARED")
    if (WIN32)
      set (LIBHDF_VERSION ${HDF5_PACKAGE_VERSION_MAJOR})
    else ()
      set (LIBHDF_VERSION ${HDF5_PACKAGE_VERSION})
    endif ()
    set_target_properties (${libtarget} PROPERTIES VERSION ${LIBHDF_VERSION})
    if (WIN32)
        set (${LIB_OUT_NAME} "${LIB_OUT_NAME}-${LIBHDF_VERSION}")
    else ()
        set_target_properties (${libtarget} PROPERTIES SOVERSION ${LIBHDF_VERSION})
    endif ()
  endif ()
  HDF_SET_LIB_OPTIONS (${libtarget} ${LIB_OUT_NAME} ${libtype})

  #-- Apple Specific install_name for libraries
  if (APPLE)
    option (HDF5_BUILD_WITH_INSTALL_NAME "Build with library install_name set to the installation path" OFF)
    if (HDF5_BUILD_WITH_INSTALL_NAME)
      set_target_properties (${libtarget} PROPERTIES
          LINK_FLAGS "-current_version ${HDF5_PACKAGE_VERSION} -compatibility_version ${HDF5_PACKAGE_VERSION}"
          INSTALL_NAME_DIR "${CMAKE_INSTALL_PREFIX}/lib"
          BUILD_WITH_INSTALL_RPATH ${HDF5_BUILD_WITH_INSTALL_NAME}
      )
    endif ()
  endif ()
endmacro ()

macro (H5_COPY_REFERENCE_FILES refname)
  if (COMPARE_TESTING)
    set (testdest "${PROJECT_BINARY_DIR}/${hdf5app}")
    #message (STATUS " Copying ${hdf5app}.tst")
    add_custom_command (
        TARGET     jh5_${hdf5app}
        POST_BUILD
        COMMAND    ${CMAKE_COMMAND}
        ARGS       -E copy_if_different ${PROJECT_SOURCE_DIR}/testfiles/${hdf5app}.tst ${testdest}.tst
    )
    if (HDF5_BUILD_TOOLS AND COMPARE_TESTING)
      add_custom_command (
          TARGET     jh5_${hdf5app}
          POST_BUILD
          COMMAND    ${CMAKE_COMMAND}
          ARGS       -E copy_if_different ${PROJECT_SOURCE_DIR}/testfiles/${hdf5app}.ddl ${testdest}.ddl
      )
    endif ()
  endif ()
endmacro ()
