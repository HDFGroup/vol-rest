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
endif (WIN32)

#-----------------------------------------------------------------------------
# Add file(s) to CMake Install
#-----------------------------------------------------------------------------
if (NOT REST_VOL_INSTALL_NO_DEVELOPMENT)
  install (
      FILES ${PROJECT_BINARY_DIR}/rest_vol_config.h
      DESTINATION ${REST_VOL_INSTALL_INCLUDE_DIR}
      COMPONENT headers
  )
endif (NOT REST_VOL_INSTALL_NO_DEVELOPMENT)

#-----------------------------------------------------------------------------
# Add Target(s) to CMake Install for import into other projects
#-----------------------------------------------------------------------------
install (
    EXPORT ${REST_VOL_INSTALL_TARGET}
    DESTINATION ${REST_VOL_INSTALL_CMAKE_DIR}
    FILE ${REST_VOL_PACKAGE}${REST_VOL_PACKAGE_EXT}-targets.cmake
    COMPONENT configinstall
)

#-----------------------------------------------------------------------------
# Export all exported targets to the build tree for use by parent project
#-----------------------------------------------------------------------------
export (
    TARGETS ${REST_VOL_LIBRARIES_TO_EXPORT} ${REST_VOL_LIB_DEPENDENCIES}
    FILE ${REST_VOL_PACKAGE}${REST_VOL_PACKAGE_EXT}-targets.cmake
)

#-----------------------------------------------------------------------------
# Set includes needed for build
#-----------------------------------------------------------------------------
set (REST_VOL_INCLUDES_BUILD_TIME
    ${REST_VOL_SRC_DIR}
)

#-----------------------------------------------------------------------------
# Set variables needed for installation
#-----------------------------------------------------------------------------
set (REST_VOL_VERSION_STRING ${REST_VOL_PACKAGE_VERSION})
set (REST_VOL_VERSION_MAJOR  ${REST_VOL_PACKAGE_VERSION_MAJOR})
set (REST_VOL_VERSION_MINOR  ${REST_VOL_PACKAGE_VERSION_MINOR})

#-----------------------------------------------------------------------------
# Configure the rv-config.cmake file for the build directory
#-----------------------------------------------------------------------------
set (INCLUDE_INSTALL_DIR ${REST_VOL_INSTALL_INCLUDE_DIR})
set (SHARE_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/${REST_VOL_INSTALL_CMAKE_DIR}" )
set (CURRENT_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}" )
configure_package_config_file (
    ${REST_VOL_RESOURCES_DIR}/rv-config.cmake.in
    "${REST_VOL_BINARY_DIR}/${REST_VOL_PACKAGE}${REST_VOL_PACKAGE_EXT}-config.cmake"
    INSTALL_DESTINATION "${REST_VOL_INSTALL_CMAKE_DIR}"
    PATH_VARS INCLUDE_INSTALL_DIR SHARE_INSTALL_DIR CURRENT_BUILD_DIR
    INSTALL_PREFIX "${CMAKE_CURRENT_BINARY_DIR}"
)

#-----------------------------------------------------------------------------
# Configure the FindRESTVOL.cmake file for the install directory
#-----------------------------------------------------------------------------
#configure_file (
#    ${REST_VOL_RESOURCES_DIR}/FindRESTVOL.cmake.in
#    ${REST_VOL_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/FindRESTVOL${REST_VOL_PACKAGE_EXT}.cmake @ONLY
#)
#install (
#    FILES ${REST_VOL_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/FindRESTVOL${REST_VOL_PACKAGE_EXT}.cmake
#    DESTINATION ${REST_VOL_INSTALL_CMAKE_DIR}
#    COMPONENT configinstall
#)

#-----------------------------------------------------------------------------
# Configure the rv-config.cmake file for the install directory
#-----------------------------------------------------------------------------
set (INCLUDE_INSTALL_DIR ${REST_VOL_INSTALL_INCLUDE_DIR})
set (SHARE_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/${REST_VOL_INSTALL_CMAKE_DIR}" )
set (CURRENT_BUILD_DIR "${CMAKE_INSTALL_PREFIX}" )
configure_package_config_file (
    ${REST_VOL_RESOURCES_DIR}/rv-config.cmake.in
    "${REST_VOL_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${REST_VOL_PACKAGE}${REST_VOL_PACKAGE_EXT}-config.cmake"
    INSTALL_DESTINATION "${REST_VOL_INSTALL_CMAKE_DIR}"
    PATH_VARS INCLUDE_INSTALL_DIR SHARE_INSTALL_DIR CURRENT_BUILD_DIR
)

install (
    FILES ${REST_VOL_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${REST_VOL_PACKAGE}${REST_VOL_PACKAGE_EXT}-config.cmake
    DESTINATION ${REST_VOL_INSTALL_CMAKE_DIR}
    COMPONENT configinstall
)

#-----------------------------------------------------------------------------
# Configure the rv-config-version.cmake file for the install directory
#-----------------------------------------------------------------------------
configure_file (
    ${REST_VOL_RESOURCES_DIR}/rv-config-version.cmake.in
    ${REST_VOL_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${REST_VOL_PACKAGE}${REST_VOL_PACKAGE_EXT}-config-version.cmake @ONLY
)
install (
    FILES ${REST_VOL_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${REST_VOL_PACKAGE}${REST_VOL_PACKAGE_EXT}-config-version.cmake
    DESTINATION ${REST_VOL_INSTALL_CMAKE_DIR}
    COMPONENT configinstall
)

#-----------------------------------------------------------------------------
# Configure the libhdf5.settings file for the lib info
#-----------------------------------------------------------------------------
if (H5_WORDS_BIGENDIAN)
  set (BYTESEX big-endian)
else (H5_WORDS_BIGENDIAN)
  set (BYTESEX little-endian)
endif (H5_WORDS_BIGENDIAN)
configure_file (
    ${REST_VOL_RESOURCES_DIR}/librestvol.settings.cmake.in
    ${REST_VOL_BINARY_DIR}/librestvol.settings @ONLY
)
install (
    FILES ${REST_VOL_BINARY_DIR}/librestvol.settings
    DESTINATION ${REST_VOL_INSTALL_LIB_DIR}
    COMPONENT libraries
)

#-----------------------------------------------------------------------------
# Create pkgconfig files
#-----------------------------------------------------------------------------
#foreach (libs ${LINK_LIBS})
#  set (LIBS "${LIBS} -l${libs}")
#endforeach (libs ${LINK_LIBS})
#foreach (libs ${REST_VOL_LIBRARIES_TO_EXPORT})
#  set (REST_VOLLIBS "${REST_VOLLIBS} -l${libs}")
#endforeach (libs ${REST_VOL_LIBRARIES_TO_EXPORT})
#configure_file (
#    ${REST_VOL_RESOURCES_DIR}/libhdf5.pc.in
#    ${REST_VOL_BINARY_DIR}/CMakeFiles/libhdf5.pc @ONLY
#)
#install (
#    FILES ${REST_VOL_BINARY_DIR}/CMakeFiles/libhdf5.pc
#    DESTINATION ${REST_VOL_INSTALL_LIB_DIR}/pkgconfig
#)

#-----------------------------------------------------------------------------
# Add Document File(s) to CMake Install
#-----------------------------------------------------------------------------
install (
    FILES
        ${REST_VOL_SOURCE_DIR}/COPYING
    DESTINATION ${REST_VOL_INSTALL_DATA_DIR}
    COMPONENT rvdocuments
)
if (EXISTS "${REST_VOL_SOURCE_DIR}/release_docs" AND IS_DIRECTORY "${REST_VOL_SOURCE_DIR}/release_docs")
  set (release_files
      ${REST_VOL_SOURCE_DIR}/release_docs/USING_REST_VOL_CMake.txt
      ${REST_VOL_SOURCE_DIR}/release_docs/COPYING
      ${REST_VOL_SOURCE_DIR}/release_docs/RELEASE.txt
  )
  if (REST_VOL_PACK_INSTALL_DOCS)
    set (release_files
        ${release_files}
        ${REST_VOL_SOURCE_DIR}/release_docs/INSTALL_CMake.txt
        ${REST_VOL_SOURCE_DIR}/release_docs/HISTORY-1_8.txt
        ${REST_VOL_SOURCE_DIR}/release_docs/INSTALL
    )
    if (WIN32)
      set (release_files
          ${release_files}
          ${REST_VOL_SOURCE_DIR}/release_docs/INSTALL_Windows.txt
      )
    endif (WIN32)
    if (CYGWIN)
      set (release_files
          ${release_files}
          ${REST_VOL_SOURCE_DIR}/release_docs/INSTALL_Cygwin.txt
      )
    endif (CYGWIN)
    if (REST_VOL_ENABLE_PARALLEL)
      set (release_files
          ${release_files}
          ${REST_VOL_SOURCE_DIR}/release_docs/INSTALL_parallel
      )
    endif (REST_VOL_ENABLE_PARALLEL)
  endif (REST_VOL_PACK_INSTALL_DOCS)
  install (
      FILES ${release_files}
      DESTINATION ${REST_VOL_INSTALL_DATA_DIR}
      COMPONENT hdfdocuments
  )
endif (EXISTS "${REST_VOL_SOURCE_DIR}/release_docs" AND IS_DIRECTORY "${REST_VOL_SOURCE_DIR}/release_docs")

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  if (CMAKE_HOST_UNIX)
    set (CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}/HDF_Group/${REST_VOL_PACKAGE_NAME}/${REST_VOL_PACKAGE_VERSION}"
      CACHE PATH "Install path prefix, prepended onto install directories." FORCE)
  else (CMAKE_HOST_UNIX)
    GetDefaultWindowsPrefixBase(CMAKE_GENERIC_PROGRAM_FILES)
    set (CMAKE_INSTALL_PREFIX
      "${CMAKE_GENERIC_PROGRAM_FILES}/HDF_Group/${REST_VOL_PACKAGE_NAME}/${REST_VOL_PACKAGE_VERSION}"
      CACHE PATH "Install path prefix, prepended onto install directories." FORCE)
    set (CMAKE_GENERIC_PROGRAM_FILES)
  endif (CMAKE_HOST_UNIX)
endif (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)

#-----------------------------------------------------------------------------
# Set the cpack variables
#-----------------------------------------------------------------------------
if (NOT REST_VOL_NO_PACKAGES)
  set (CPACK_PACKAGE_VENDOR "HDF_Group")
  set (CPACK_PACKAGE_NAME "${REST_VOL_PACKAGE_NAME}")
  if (CDASH_LOCAL)
    set (CPACK_PACKAGE_VERSION "${REST_VOL_PACKAGE_VERSION}")
  else (CDASH_LOCAL)
    set (CPACK_PACKAGE_VERSION "${REST_VOL_PACKAGE_VERSION_STRING}")
  endif (CDASH_LOCAL)
  set (CPACK_PACKAGE_VERSION_MAJOR "${REST_VOL_PACKAGE_VERSION_MAJOR}")
  set (CPACK_PACKAGE_VERSION_MINOR "${REST_VOL_PACKAGE_VERSION_MINOR}")
  set (CPACK_PACKAGE_VERSION_PATCH "")
  if (EXISTS "${REST_VOL_SOURCE_DIR}/release_docs")
    set (CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/release_docs/RELEASE.txt")
    set (CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/release_docs/COPYING")
    set (CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/release_docs/RELEASE.txt")
  endif (EXISTS "${REST_VOL_SOURCE_DIR}/release_docs")
  set (CPACK_PACKAGE_RELOCATABLE TRUE)
  if (OVERRIDE_INSTALL_VERSION)
    set (CPACK_PACKAGE_INSTALL_DIRECTORY "${CPACK_PACKAGE_VENDOR}/${CPACK_PACKAGE_NAME}/${OVERRIDE_INSTALL_VERSION}")
  else (OVERRIDE_INSTALL_VERSION)
    set (CPACK_PACKAGE_INSTALL_DIRECTORY "${CPACK_PACKAGE_VENDOR}/${CPACK_PACKAGE_NAME}/${CPACK_PACKAGE_VERSION}")
  endif (OVERRIDE_INSTALL_VERSION)
  set (CPACK_PACKAGE_ICON "${REST_VOL_RESOURCES_EXT_DIR}/hdf.bmp")

  set (CPACK_GENERATOR "TGZ")
  if (WIN32)
    set (CPACK_GENERATOR "ZIP")

    if (NSIS_EXECUTABLE)
      list (APPEND CPACK_GENERATOR "NSIS")
    endif (NSIS_EXECUTABLE)
    # Installers for 32- vs. 64-bit CMake:
    #  - Root install directory (displayed to end user at installer-run time)
    #  - "NSIS package/display name" (text used in the installer GUI)
    #  - Registry key used to store info about the installation
    set (CPACK_NSIS_PACKAGE_NAME "${REST_VOL_PACKAGE_STRING}")
    if (CMAKE_CL_64)
      set (CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
      set (CPACK_PACKAGE_INSTALL_REGISTRY_KEY "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION} (Win64)")
    else (CMAKE_CL_64)
      set (CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES")
      set (CPACK_PACKAGE_INSTALL_REGISTRY_KEY "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}")
    endif (CMAKE_CL_64)
    # set the install/unistall icon used for the installer itself
    # There is a bug in NSI that does not handle full unix paths properly.
    set (CPACK_NSIS_MUI_ICON "${REST_VOL_RESOURCES_EXT_DIR}\\\\hdf.ico")
    set (CPACK_NSIS_MUI_UNIICON "${REST_VOL_RESOURCES_EXT_DIR}\\\\hdf.ico")
    # set the package header icon for MUI
    set (CPACK_PACKAGE_ICON "${REST_VOL_RESOURCES_EXT_DIR}\\\\hdf.bmp")
    set (CPACK_NSIS_DISPLAY_NAME "${CPACK_NSIS_PACKAGE_NAME}")
    if (OVERRIDE_INSTALL_VERSION)
      set (CPACK_PACKAGE_INSTALL_DIRECTORY "${CPACK_PACKAGE_VENDOR}\\\\${CPACK_PACKAGE_NAME}\\\\${OVERRIDE_INSTALL_VERSION}")
    else (OVERRIDE_INSTALL_VERSION)
      set (CPACK_PACKAGE_INSTALL_DIRECTORY "${CPACK_PACKAGE_VENDOR}\\\\${CPACK_PACKAGE_NAME}\\\\${CPACK_PACKAGE_VERSION}")
    endif (OVERRIDE_INSTALL_VERSION)
    set (CPACK_NSIS_CONTACT "${REST_VOL_PACKAGE_BUGREPORT}")
    set (CPACK_NSIS_MODIFY_PATH ON)

    if (WIX_EXECUTABLE)
      list (APPEND CPACK_GENERATOR "WIX")
    endif (WIX_EXECUTABLE)
#WiX variables
    set (CPACK_WIX_UNINSTALL "1")
# .. variable:: CPACK_WIX_LICENSE_RTF
#  RTF License File
#
#  If CPACK_RESOURCE_FILE_LICENSE has an .rtf extension it is used as-is.
#
#  If CPACK_RESOURCE_FILE_LICENSE has an .txt extension it is implicitly
#  converted to RTF by the WiX Generator.
#  The expected encoding of the .txt file is UTF-8.
#
#  With CPACK_WIX_LICENSE_RTF you can override the license file used by the
#  WiX Generator in case CPACK_RESOURCE_FILE_LICENSE is in an unsupported
#  format or the .txt -> .rtf conversion does not work as expected.
    set (CPACK_RESOURCE_FILE_LICENSE "${REST_VOL_BINARY_DIR}/COPYING.txt")
# .. variable:: CPACK_WIX_PRODUCT_ICON
#  The Icon shown next to the program name in Add/Remove programs.
    set(CPACK_WIX_PRODUCT_ICON "${REST_VOL_RESOURCES_EXT_DIR}\\\\hdf.ico")
#
# .. variable:: CPACK_WIX_UI_BANNER
#
#  The bitmap will appear at the top of all installer pages other than the
#  welcome and completion dialogs.
#
#  If set, this image will replace the default banner image.
#
#  This image must be 493 by 58 pixels.
#
# .. variable:: CPACK_WIX_UI_DIALOG
#
#  Background bitmap used on the welcome and completion dialogs.
#
#  If this variable is set, the installer will replace the default dialog
#  image.
#
#  This image must be 493 by 312 pixels.
#
    set(CPACK_WIX_PROPERTY_ARPCOMMENTS "HDF5 (Hierarchical Data Format 5) REST VOL plugin")
    set(CPACK_WIX_PROPERTY_ARPURLINFOABOUT "${REST_VOL_PACKAGE_URL}")
    set(CPACK_WIX_PROPERTY_ARPHELPLINK "${REST_VOL_PACKAGE_BUGREPORT}")
  elseif (APPLE)
    list (APPEND CPACK_GENERATOR "DragNDrop")
    set (CPACK_COMPONENTS_ALL_IN_ONE_PACKAGE ON)
    set (CPACK_PACKAGING_INSTALL_PREFIX "/${CPACK_PACKAGE_INSTALL_DIRECTORY}")
    set (CPACK_PACKAGE_ICON "${REST_VOL_RESOURCES_EXT_DIR}/hdf.icns")

    option (REST_VOL_PACK_MACOSX_FRAMEWORK  "Package the REST VOL Library in a Frameworks" OFF)
    if (REST_VOL_PACK_MACOSX_FRAMEWORK AND REST_VOL_BUILD_FRAMEWORKS)
      set (CPACK_BUNDLE_NAME "${REST_VOL_PACKAGE_STRING}")
      set (CPACK_BUNDLE_LOCATION "/")    # make sure CMAKE_INSTALL_PREFIX ends in /
      set (CMAKE_INSTALL_PREFIX "/${CPACK_BUNDLE_NAME}.framework/Versions/${CPACK_PACKAGE_VERSION}/${CPACK_PACKAGE_NAME}/")
      set (CPACK_BUNDLE_ICON "${REST_VOL_RESOURCES_EXT_DIR}/hdf.icns")
      set (CPACK_BUNDLE_PLIST "${REST_VOL_BINARY_DIR}/CMakeFiles/Info.plist")
      set (CPACK_SHORT_VERSION_STRING "${CPACK_PACKAGE_VERSION}")
      #-----------------------------------------------------------------------------
      # Configure the Info.plist file for the install bundle
      #-----------------------------------------------------------------------------
      configure_file (
          ${REST_VOL_RESOURCES_DIR}/CPack.Info.plist.in
          ${REST_VOL_BINARY_DIR}/CMakeFiles/Info.plist @ONLY
      )
      configure_file (
          ${REST_VOL_RESOURCES_DIR}/PkgInfo.in
          ${REST_VOL_BINARY_DIR}/CMakeFiles/PkgInfo @ONLY
      )
      configure_file (
          ${REST_VOL_RESOURCES_DIR}/version.plist.in
          ${REST_VOL_BINARY_DIR}/CMakeFiles/version.plist @ONLY
      )
      install (
          FILES ${REST_VOL_BINARY_DIR}/CMakeFiles/PkgInfo
          DESTINATION ..
      )
    endif (REST_VOL_PACK_MACOSX_FRAMEWORK AND REST_VOL_BUILD_FRAMEWORKS)
  else (WIN32)
    list (APPEND CPACK_GENERATOR "STGZ")
    set (CPACK_PACKAGING_INSTALL_PREFIX "/${CPACK_PACKAGE_INSTALL_DIRECTORY}")
    set (CPACK_COMPONENTS_ALL_IN_ONE_PACKAGE ON)

    set (CPACK_DEBIAN_PACKAGE_SECTION "Libraries")
    set (CPACK_DEBIAN_PACKAGE_MAINTAINER "${REST_VOL_PACKAGE_BUGREPORT}")

    set (CPACK_RPM_PACKAGE_RELEASE "1")
    set (CPACK_RPM_COMPONENT_INSTALL ON)
    set (CPACK_RPM_PACKAGE_RELOCATABLE ON)
    set (CPACK_RPM_PACKAGE_LICENSE "BSD-style")
    set (CPACK_RPM_PACKAGE_GROUP "Development/Libraries")
    set (CPACK_RPM_PACKAGE_URL "${REST_VOL_PACKAGE_URL}")
    set (CPACK_RPM_PACKAGE_SUMMARY "The HDF5 REST VOL plugin is a plugin designed to allow access to ...")
    set (CPACK_RPM_PACKAGE_DESCRIPTION
        "The HDF5 REST VOL plugin..."
    )

    #-----------------------------------------------------------------------------
    # Configure the spec file for the install RPM
    #-----------------------------------------------------------------------------
#    configure_file ("${REST_VOL_RESOURCES_DIR}/hdf5.spec.in" "${CMAKE_CURRENT_BINARY_DIR}/${REST_VOL_PACKAGE_NAME}.spec" @ONLY IMMEDIATE)
#    set (CPACK_RPM_USER_BINARY_SPECFILE "${CMAKE_CURRENT_BINARY_DIR}/${REST_VOL_PACKAGE_NAME}.spec")
  endif (WIN32)

  # By default, do not warn when built on machines using only VS Express:
  if (NOT DEFINED CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_NO_WARNINGS)
    set (CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_NO_WARNINGS ON)
  endif (NOT DEFINED CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_NO_WARNINGS)
  include (InstallRequiredSystemLibraries)

  set (CPACK_INSTALL_CMAKE_PROJECTS "${REST_VOL_BINARY_DIR};REST VOL;ALL;/")

  include (CPack)

  cpack_add_install_type(Full DISPLAY_NAME "Everything")
  cpack_add_install_type(Developer)

  cpack_add_component_group(Runtime)

  cpack_add_component_group(Documents
      EXPANDED
      DESCRIPTION "Release notes for developing HDF5 applications"
  )

  cpack_add_component_group(Development
      EXPANDED
      DESCRIPTION "All of the tools you'll need to develop HDF5 applications"
  )

  cpack_add_component_group(Applications
      EXPANDED
      DESCRIPTION "Tools for HDF5 files"
  )

  #---------------------------------------------------------------------------
  # Now list the cpack commands
  #---------------------------------------------------------------------------
  cpack_add_component (libraries
      DISPLAY_NAME "HDF5 REST VOL Libraries"
      GROUP Runtime
      INSTALL_TYPES Full Developer User
  )
  cpack_add_component (headers
      DISPLAY_NAME "HDF5 REST VOL Headers"
      DEPENDS libraries
      GROUP Development
      INSTALL_TYPES Full Developer
  )
  cpack_add_component (hdfdocuments
      DISPLAY_NAME "HDF5 REST VOL Documents"
      GROUP Documents
      INSTALL_TYPES Full Developer
  )
  cpack_add_component (configinstall
      DISPLAY_NAME "HDF5 REST VOL CMake files"
      HIDDEN
      DEPENDS libraries
      GROUP Development
      INSTALL_TYPES Full Developer User
  )

  if (REST_VOL_BUILD_TOOLS)
    cpack_add_component (toolsapplications
        DISPLAY_NAME "HDF5 Tools Applications (with REST VOL support)"
        DEPENDS toolslibraries
        GROUP Applications
        INSTALL_TYPES Full Developer User
    )
    cpack_add_component (toolslibraries
        DISPLAY_NAME "HDF5 Tools Libraries (with REST VOL support)"
        DEPENDS libraries
        GROUP Runtime
        INSTALL_TYPES Full Developer User
    )
    cpack_add_component (toolsheaders
        DISPLAY_NAME "HDF5 Tools Headers (with REST VOL support)"
        DEPENDS toolslibraries
        GROUP Development
        INSTALL_TYPES Full Developer
    )
  endif (REST_VOL_BUILD_TOOLS)

endif (NOT REST_VOL_NO_PACKAGES)
