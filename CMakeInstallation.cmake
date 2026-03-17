include (CMakePackageConfigHelpers)

#-----------------------------------------------------------------------------
# Check for Installation Utilities
#-----------------------------------------------------------------------------
if (WIN32)
  set (PF_ENV_EXT "(x86)")
  find_program (NSIS_EXECUTABLE NSIS.exe PATHS "$ENV{ProgramFiles}\\NSIS" "$ENV{ProgramFiles${PF_ENV_EXT}}\\NSIS")
  if(NOT CPACK_WIX_ROOT)
    file(TO_CMAKE_PATH "$ENV{WIX}" CPACK_WIX_ROOT)
  endif()
  find_program (WIX_EXECUTABLE candle  PATHS "${CPACK_WIX_ROOT}/bin")
endif ()

#-----------------------------------------------------------------------------
# Add file(s) to CMake Install
#-----------------------------------------------------------------------------
if (NOT HDF5_VOL_REST_INSTALL_NO_DEVELOPMENT)
  install (
      FILES ${PROJECT_BINARY_DIR}/rest_vol_config.h
      DESTINATION ${HDF5_VOL_REST_INSTALL_INCLUDE_DIR}
      COMPONENT headers
  )
endif ()

#-----------------------------------------------------------------------------
# Add Target(s) to CMake Install for import into other projects
#-----------------------------------------------------------------------------
#install (
#    EXPORT ${HDF5_VOL_REST_INSTALL_TARGET}
#    DESTINATION ${HDF5_VOL_REST_INSTALL_CMAKE_DIR}
#    FILE ${HDF5_VOL_REST_PACKAGE}${HDF5_VOL_REST_PACKAGE_EXT}-targets.cmake
#    COMPONENT configinstall
#)

#-----------------------------------------------------------------------------
# If cross-compiling, export all exported targets to the build tree
#-----------------------------------------------------------------------------
if (CMAKE_CROSSCOMPILING)
  export (
      TARGETS ${HDF5_VOL_REST_LIBRARIES_TO_EXPORT} ${HDF5_VOL_REST_LIB_DEPENDENCIES}
      FILE ${HDF5_VOL_REST_PACKAGE}${HDF5_VOL_REST_PACKAGE_EXT}-targets.cmake
  )
endif ()

#-----------------------------------------------------------------------------
# Set includes needed for build
#-----------------------------------------------------------------------------
set (HDF5_VOL_REST_INCLUDES_BUILD_TIME
    ${HDF5_VOL_REST_SRC_DIR}
)

#-----------------------------------------------------------------------------
# Set variables needed for installation
#-----------------------------------------------------------------------------
set (HDF5_VOL_REST_VERSION_STRING ${HDF5_VOL_REST_PACKAGE_VERSION})
set (HDF5_VOL_REST_VERSION_MAJOR  ${HDF5_VOL_REST_PACKAGE_VERSION_MAJOR})
set (HDF5_VOL_REST_VERSION_MINOR  ${HDF5_VOL_REST_PACKAGE_VERSION_MINOR})

#-----------------------------------------------------------------------------
# Configure the rv-config.cmake file for the build directory
#-----------------------------------------------------------------------------
set (INCLUDE_INSTALL_DIR ${HDF5_VOL_REST_INSTALL_INCLUDE_DIR})
set (SHARE_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/${HDF5_VOL_REST_INSTALL_CMAKE_DIR}" )
set (CURRENT_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}" )
configure_package_config_file (
    ${HDF5_VOL_REST_RESOURCES_DIR}/rv-config.cmake.in
    "${HDF5_VOL_REST_BINARY_DIR}/${HDF5_VOL_REST_PACKAGE}${HDF5_VOL_REST_PACKAGE_EXT}-config.cmake"
    INSTALL_DESTINATION "${HDF5_VOL_REST_INSTALL_CMAKE_DIR}"
    PATH_VARS INCLUDE_INSTALL_DIR SHARE_INSTALL_DIR CURRENT_BUILD_DIR
    INSTALL_PREFIX "${CMAKE_CURRENT_BINARY_DIR}"
)

#-----------------------------------------------------------------------------
# Configure the FindRESTVOL.cmake file for the install directory
#-----------------------------------------------------------------------------
#configure_file (
#    ${HDF5_VOL_REST_RESOURCES_DIR}/FindRESTVOL.cmake.in
#    ${HDF5_VOL_REST_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/FindRESTVOL${HDF5_VOL_REST_PACKAGE_EXT}.cmake @ONLY
#)
#install (
#    FILES ${HDF5_VOL_REST_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/FindRESTVOL${HDF5_VOL_REST_PACKAGE_EXT}.cmake
#    DESTINATION ${HDF5_VOL_REST_INSTALL_CMAKE_DIR}
#    COMPONENT configinstall
#)

#-----------------------------------------------------------------------------
# Configure the rv-config.cmake file for the install directory
#-----------------------------------------------------------------------------
set (INCLUDE_INSTALL_DIR ${HDF5_VOL_REST_INSTALL_INCLUDE_DIR})
set (SHARE_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/${HDF5_VOL_REST_INSTALL_CMAKE_DIR}" )
set (CURRENT_BUILD_DIR "${CMAKE_INSTALL_PREFIX}" )
configure_package_config_file (
    ${HDF5_VOL_REST_RESOURCES_DIR}/rv-config.cmake.in
    "${HDF5_VOL_REST_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${HDF5_VOL_REST_PACKAGE}${HDF5_VOL_REST_PACKAGE_EXT}-config.cmake"
    INSTALL_DESTINATION "${HDF5_VOL_REST_INSTALL_CMAKE_DIR}"
    PATH_VARS INCLUDE_INSTALL_DIR SHARE_INSTALL_DIR CURRENT_BUILD_DIR
)

install (
    FILES ${HDF5_VOL_REST_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${HDF5_VOL_REST_PACKAGE}${HDF5_VOL_REST_PACKAGE_EXT}-config.cmake
    DESTINATION ${HDF5_VOL_REST_INSTALL_CMAKE_DIR}
    COMPONENT configinstall
)

#-----------------------------------------------------------------------------
# Configure the rv-config-version.cmake file for the install directory
#-----------------------------------------------------------------------------
configure_file (
    ${HDF5_VOL_REST_RESOURCES_DIR}/rv-config-version.cmake.in
    ${HDF5_VOL_REST_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${HDF5_VOL_REST_PACKAGE}${HDF5_VOL_REST_PACKAGE_EXT}-config-version.cmake @ONLY
)
install (
    FILES ${HDF5_VOL_REST_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${HDF5_VOL_REST_PACKAGE}${HDF5_VOL_REST_PACKAGE_EXT}-config-version.cmake
    DESTINATION ${HDF5_VOL_REST_INSTALL_CMAKE_DIR}
    COMPONENT configinstall
)

#-----------------------------------------------------------------------------
# Install FindYAJL.cmake
#-----------------------------------------------------------------------------
install (
    FILES ${HDF5_VOL_REST_SOURCE_DIR}/CMake/FindYAJL.cmake
    DESTINATION ${HDF5_VOL_REST_INSTALL_DATA_DIR}/cmake
    COMPONENT configinstall
)

#-----------------------------------------------------------------------------
# Configure the libhdf5_vol_rest.settings file for the lib info
#-----------------------------------------------------------------------------
if (H5_WORDS_BIGENDIAN)
  set (BYTESEX big-endian)
else ()
  set (BYTESEX little-endian)
endif ()
configure_file (
    ${HDF5_VOL_REST_RESOURCES_DIR}/libhdf5_vol_rest.settings.cmake.in
    ${HDF5_VOL_REST_BINARY_DIR}/libhdf5_vol_rest.settings @ONLY
)
install (
    FILES ${HDF5_VOL_REST_BINARY_DIR}/libhdf5_vol_rest.settings
    DESTINATION ${HDF5_VOL_REST_INSTALL_LIB_DIR}
    COMPONENT libraries
)

#-----------------------------------------------------------------------------
# Create pkgconfig files
#-----------------------------------------------------------------------------
#foreach (libs ${LINK_LIBS})
#  set (LIBS "${LIBS} -l${libs}")
#endforeach (libs ${LINK_LIBS})
#foreach (libs ${HDF5_VOL_REST_LIBRARIES_TO_EXPORT})
#  set (HDF5_VOL_RESTLIBS "${HDF5_VOL_RESTLIBS} -l${libs}")
#endforeach (libs ${HDF5_VOL_REST_LIBRARIES_TO_EXPORT})
#configure_file (
#    ${HDF5_VOL_REST_RESOURCES_DIR}/libhdf5.pc.in
#    ${HDF5_VOL_REST_BINARY_DIR}/CMakeFiles/libhdf5.pc @ONLY
#)
#install (
#    FILES ${HDF5_VOL_REST_BINARY_DIR}/CMakeFiles/libhdf5.pc
#    DESTINATION ${HDF5_VOL_REST_INSTALL_LIB_DIR}/pkgconfig
#)

#-----------------------------------------------------------------------------
# Add Document File(s) to CMake Install
#-----------------------------------------------------------------------------
install (
    FILES
        ${HDF5_VOL_REST_SOURCE_DIR}/COPYING
    DESTINATION ${HDF5_VOL_REST_INSTALL_DATA_DIR}
    COMPONENT rvdocuments
)
install (
    FILES
        ${HDF5_VOL_REST_SOURCE_DIR}/README.md
    DESTINATION ${HDF5_VOL_REST_INSTALL_DATA_DIR}
    COMPONENT rvdocuments
)
if (EXISTS "${HDF5_VOL_REST_SOURCE_DIR}/release_docs" AND IS_DIRECTORY "${HDF5_VOL_REST_SOURCE_DIR}/release_docs")
  set (release_files
      ${HDF5_VOL_REST_SOURCE_DIR}/release_docs/USING_HDF5_VOL_REST_CMake.txt
      ${HDF5_VOL_REST_SOURCE_DIR}/release_docs/COPYING
      ${HDF5_VOL_REST_SOURCE_DIR}/release_docs/RELEASE.txt
  )
  if (HDF5_VOL_REST_PACK_INSTALL_DOCS)
    set (release_files
        ${release_files}
        ${HDF5_VOL_REST_SOURCE_DIR}/release_docs/INSTALL_CMake.txt
        ${HDF5_VOL_REST_SOURCE_DIR}/release_docs/HISTORY-1_8.txt
        ${HDF5_VOL_REST_SOURCE_DIR}/release_docs/INSTALL
    )
    if (WIN32)
      set (release_files
          ${release_files}
          ${HDF5_VOL_REST_SOURCE_DIR}/release_docs/INSTALL_Windows.txt
      )
    endif ()
    if (CYGWIN)
      set (release_files
          ${release_files}
          ${HDF5_VOL_REST_SOURCE_DIR}/release_docs/INSTALL_Cygwin.txt
      )
    endif ()
    if (HDF5_VOL_REST_ENABLE_PARALLEL)
      set (release_files
          ${release_files}
          ${HDF5_VOL_REST_SOURCE_DIR}/release_docs/INSTALL_parallel
      )
    endif ()
  endif ()
  install (
      FILES ${release_files}
      DESTINATION ${HDF5_VOL_REST_INSTALL_DATA_DIR}
      COMPONENT hdfdocuments
  )
endif ()

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  if (CMAKE_HOST_UNIX)
    set (CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}/HDF_Group/${HDF5_VOL_REST_PACKAGE_NAME}/${HDF5_VOL_REST_PACKAGE_VERSION}"
      CACHE PATH "Install path prefix, prepended onto install directories." FORCE)
  else ()
    GetDefaultWindowsPrefixBase(CMAKE_GENERIC_PROGRAM_FILES)
    set (CMAKE_INSTALL_PREFIX
      "${CMAKE_GENERIC_PROGRAM_FILES}/HDF_Group/${HDF5_VOL_REST_PACKAGE_NAME}/${HDF5_VOL_REST_PACKAGE_VERSION}"
      CACHE PATH "Install path prefix, prepended onto install directories." FORCE)
    set (CMAKE_GENERIC_PROGRAM_FILES)
  endif ()
endif ()

#-----------------------------------------------------------------------------
# Set the cpack variables
#-----------------------------------------------------------------------------
if (NOT HDF5_VOL_REST_NO_PACKAGES)
  set (CPACK_PACKAGE_NAME "${HDF5_VOL_REST_PACKAGE_NAME}")
  set (CPACK_PACKAGE_DESCRIPTION_FILE ${HDF5_VOL_REST_SOURCE_DIR}/README.md)
  set (CPACK_RESOURCE_FILE_LICENSE ${HDF5_VOL_REST_SOURCE_DIR}/COPYING)
  set (CPACK_PACKAGE_DESCRIPTION_SUMMARY "HDF5 VOL connector for the HDF REST API")
  set (CPACK_PACKAGE_VENDOR "The HDF Group")
  set (CPACK_PACKAGE_VERSION_MAJOR "${HDF5_VOL_REST_PACKAGE_VERSION_MAJOR}")
  set (CPACK_PACKAGE_VERSION_MINOR "${HDF5_VOL_REST_PACKAGE_VERSION_MINOR}")
  set (CPACK_PACKAGE_VERSION_PATCH "")
  set (CPACK_SOURCE_PACKAGE_FILE_NAME ${HDF5_VOL_REST_PACKAGE}-${HDF5_VOL_REST_PACKAGE_VERSION_STRING})
  set (CPACK_SOURCE_IGNORE_FILES
    # Files specific to version control
    "/\\\\.git/"
    "/\\\\.git$"
    "/\\\\.gitattributes$"
    "/\\\\.github/"
    "/\\\\.gitignore$"
    "/\\\\.gitmodules$"

    # IDE files
    "/\\\\.vscode/"
    "/\\\\.settings/"
    "/\\\\.autotools$"
    "/\\\\.autotools$"
    "/\\\\.project$"
    "/\\\\.cproject$"

    # Build
    "/build/"

    # Temporary files
    "\\\\.swp$"
    "\\\\.#"
    "/#"
    "~$"
  )

  include (CPack)
endif ()
