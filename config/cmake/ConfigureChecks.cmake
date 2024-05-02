#-----------------------------------------------------------------------------
# Include all the necessary files for macros
#-----------------------------------------------------------------------------
set (HDF_PREFIX "H5")
include(CheckFunctionExists)
include(CheckLibraryExists)

if (NOT DEFINED "H5_DEFAULT_PLUGINDIR")
  if (WINDOWS)
    set (H5_DEFAULT_PLUGINDIR "%ALLUSERSPROFILE%\\\\hdf5\\\\lib\\\\plugin")
  else (WINDOWS)
    set (H5_DEFAULT_PLUGINDIR "/usr/local/hdf5/lib/plugin")
  endif (WINDOWS)
endif (NOT DEFINED "H5_DEFAULT_PLUGINDIR")

if (WINDOWS)
  set (H5_HAVE_WINDOWS 1)
  # ----------------------------------------------------------------------
  # Set the flag to indicate that the machine has window style pathname,
  # that is, "drive-letter:\" (e.g. "C:") or "drive-letter:/" (e.g. "C:/").
  # (This flag should be _unset_ for all machines, except for Windows)
  set (H5_HAVE_WINDOW_PATH 1)
endif (WINDOWS)
SET (H5_DEFAULT_VOL H5VL_NATIVE)

# ----------------------------------------------------------------------
# END of WINDOWS Hard code Values
# ----------------------------------------------------------------------

# Find the library containing clock_gettime()
CHECK_FUNCTION_EXISTS (clock_gettime CLOCK_GETTIME_IN_LIBC)
CHECK_FUNCTION_EXISTS (gettimeofday RV_HAVE_GETTIMEOFDAY)
CHECK_FUNCTION_EXISTS(strtok_r RV_HAVE_STRTOK_R)
CHECK_FUNCTION_EXISTS(strtok_s RV_HAVE_STRTOK_S)
CHECK_LIBRARY_EXISTS (rt clock_gettime "" CLOCK_GETTIME_IN_LIBRT)
CHECK_LIBRARY_EXISTS (posix4 clock_gettime "" CLOCK_GETTIME_IN_LIBPOSIX4)
if (CLOCK_GETTIME_IN_LIBC)
  set (RV_HAVE_CLOCK_GETTIME 1)
elseif (CLOCK_GETTIME_IN_LIBRT)
  set (RV_HAVE_CLOCK_GETTIME 1)
  list (APPEND LINK_LIBS rt)
elseif (CLOCK_GETTIME_IN_LIBPOSIX4)
  set (RV_HAVE_CLOCK_GETTIME 1)
  list (APPEND LINK_LIBS posix4)
endif ()


# -----------------------------------------------------------------------
# wrapper script variables
# 
set (prefix ${CMAKE_INSTALL_PREFIX})
set (exec_prefix "\${prefix}")
set (libdir "${exec_prefix}/lib")
set (includedir "\${prefix}/include")
set (host_os ${CMAKE_HOST_SYSTEM_NAME})
set (CC ${CMAKE_C_COMPILER})
set (CXX ${CMAKE_CXX_COMPILER})
set (FC ${CMAKE_Fortran_COMPILER})
foreach (LINK_LIB ${LINK_LIBS})
  set (LIBS "${LIBS} -l${LINK_LIB}")
endforeach (LINK_LIB ${LINK_LIBS})
