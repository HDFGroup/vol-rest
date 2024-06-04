rem
rem Copyright by The HDF Group.
rem All rights reserved.
rem
rem This file is part of the HDF5 REST VOL connector. The full copyright
rem notice, including terms governing use, modification, and redistribution,
rem is contained in the COPYING file, which can be found at the root of the
rem source code distribution tree.           
rem
rem A script used to first configure and build the HDF5 source distribution
rem included with the REST VOL connector source code, and then use that built
rem HDF5 to build the REST VOL connector itself.

rem Disable showing executed commands to console
@echo off

rem Do not export variable set in this script to the environment
setlocal

rem Get the directory of the script itself
set SCRIPT_DIR=%~dp0

rem Remove trailing backslash
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%

rem Set default install directory
set INSTALL_DIR=%SCRIPT_DIR%\rest_vol_build

rem Set default build directory
set BUILD_DIR=%SCRIPT_DIR%\rest_vol_cmake_build_files

rem Determine the number of processors to use when building in parallel
set /A NPROCS=0

rem Extra compiler options passed to the various steps, such as -Wall
set COMP_OPTS="-Wall -pedantic -Wunused-macros"

rem Extra options passed to the REST VOLs CMake script
set CONNECTOR_DEBUG_OPT=
set CURL_DEBUG_OPT=
set MEM_TRACK_OPT=
set THREAD_SAFE_OPT=
set CURL_OPT=
set YAJL_OPT=
set YAJL_LIB_OPT=
set CMAKE_C_FLAGS=
rem On Windows, build type will default to Debug if not specified
set CMAKE_BUILD_TYPE=Release

echo.
echo *************************
echo * REST VOL build script *
echo *************************
echo.


call :parse_opts %*

rem Determine the number of cores to use when parallelizing builds
if %NPROCS% == 0 (
    set NPROCS=%NUMBER_OF_PROCESSORS%
)

rem Check out vol-tests submodule if subdirectory does not exist
if not exist %SCRIPT_DIR%\test\vol-tests (
    git submodule init
    git submodule update
)

rem Build the REST VOL connector against HDF5
echo "*******************************************"
echo "* Building REST VOL connector and test suite *"
echo "*******************************************"
echo.

mkdir %BUILD_DIR%
mkdir %INSTALL_DIR%

rem Clean out the old CMake cache
del /q %BUILD_DIR%\CMakeCache.txt > nul 2>&1

cd %BUILD_DIR%

echo Configuring build...
cmake -G %CMAKE_GENERATOR% -DBUILD_SHARED_LIBS=ON -DBUILD_STATIC_LIBS=ON -DCMAKE_BUILD_TYPE=%CMAKE_BUILD_TYPE% -DCMAKE_C_FLAGS=%CMAKE_C_FLAGS% -DHDF5_ROOT=%HDF5_INSTALL_DIR% -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% %CURL_OPT% %CURL_LIB_OPT% %YAJL_OPT% %YAJL_LIB_OPT% %CONNECTOR_DEBUG_OPT% %CURL_DEBUG_OPT% %MEM_TRACK_OPT% %THREAD_SAFE_OPT% %SCRIPT_DIR%

echo Build files generated for CMake generator %CMAKE_GENERATOR%

cmake --build . -j %NPROCS% --config %CMAKE_BUILD_TYPE% && cmake --install . --config %CMAKE_BUILD_TYPE%

if %errorlevel% equ 0 (
    echo REST VOL built
) else (
    echo An error occurred during build and installation of REST VOL
    EXIT /B 1
)

echo Configuring vol-tests

mkdir %BUILD_DIR%\tests\vol-tests\
cd %BUILD_DIR%\tests\vol-tests\

cmake -G %CMAKE_GENERATOR% -DCMAKE_BUILD_TYPE=%CMAKE_BUILD_TYPE% -DCMAKE_C_FLAGS=%CMAKE_C_FLAGS% -DHDF5_DIR=%HDF5_INSTALL_DIR% -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% %CONNECTOR_DEBUG_OPT% %CURL_DEBUG_OPT% %MEM_TRACK_OPT% %THREAD_SAFE_OPT% %SCRIPT_DIR%/test/vol-tests

echo Build files generated for vol-tests

cmake --build . -j

echo VOL tests built

endlocal
EXIT /B 0

:parse_opts
set "optchar=%1"
if "%optchar%"=="" (
    echo Ending arg parse
    EXIT /B 0
)
shift

if "%optchar%"=="-h" (
    call :usage
    EXIT /B 0
) else if "%optchar%"=="-d" (
    set CONNECTOR_DEBUG_OPT=-DHDF5_VOL_REST_ENABLE_DEBUG=ON
    set CMAKE_BUILD_TYPE=Debug
    echo Enabled connector debugging
    echo.
) else if "%optchar%"=="-c" (
    set CURL_DEBUG_OPT=-DHDF5_VOL_REST_ENABLE_CURL_DEBUG=ON
    echo Enabled cURL debugging
    echo.
) else if "%optchar%"=="-m" (
    set MEM_TRACK_OPT=-DHDF5_VOL_REST_ENABLE_MEM_TRACKING=ON
    echo Enabled connector memory tracking
    echo.
) else if "%optchar%"=="-s" (
    set THREAD_SAFE_OPT=-DHDF5_VOL_REST_THREAD_SAFE=ON
    echo Enabled connector memory tracking
    echo.
) else if "%optchar%"=="-t" (
    set YAJL_LIB_OPT=-DYAJL_USE_STATIC_LIBRARIES=ON
    echo Using the static YAJL library
    echo.
) else if "%optchar%"=="-u" (
    set CMAKE_C_FLAGS=/DCURL_STATICLIB
    set CURL_LIB_OPT=-DCURL_USE_STATIC_LIBRARIES=ON
    shift
    echo Using the static cURL library
    echo.
) else if "%optchar%"=="-G" (
    set CMAKE_GENERATOR=%1
    shift
    echo Set CMake generator
    echo.
) else if "%optchar%"=="-B" (
    set BUILD_DIR=%1
    shift
    echo Set build directory
    echo.
) else if "%optchar%"=="-P" (
    set INSTALL_DIR=%1
    shift
    echo Set installation prefix
    echo.
) else if "%optchar%"=="-H" (
    set HDF5_INSTALL_DIR=%1
    shift
    echo Set HDF5 install directory
    echo.
) else if "%optchar%"=="-C" (
    set CURL_OPT=-DCURL_ROOT=%1
    shift
    echo Set CURL_ROOT
    echo.
) else if "%optchar%"=="-Y" (
    set YAJL_OPT=-DYAJL_ROOT=%1
    shift
    echo Set YAJL_ROOT
    echo.
) else (
    echo ERROR: non-option argument: '%optchar%' >&2
    echo.
    call :usage
    echo.
    EXIT /B 1
)
goto :parse_opts

:usage
echo usage: %~dp0 [OPTIONS]
echo.
echo       -h      Print this help message.
echo.
echo       -d      Enable debugging output in the REST VOL.
echo       -c      Enable cURL debugging output in the REST VOL.
echo.
echo       -m      Enable memory tracking in the REST VOL.
echo.
echo       -s      Enable linking to thread safe static hdf5 library.
echo.
echo       -t      Make use of the static YAJL library. Be aware the
echo               library should be built with position independent
echo               code option enabled.
echo.
echo       -u      Make use of the static cURL library.
echo.
echo       -G      Specify the CMake Generator to use for the build
echo               files created.
echo.
echo       -P DIR  Similar to '-DCMAKE_INSTALL_PREFIX=DIR', specifies
echo               where the REST VOL should be installed to. Default
echo               is 'source directory/rest_vol_build'.
echo.
echo       -H DIR  To specify a directory where HDF5 has already
echo               been installed. Similar to '-DHDF5_ROOT=DIR'.
echo.
echo       -B DIR  Specifies the directory that CMake should use as
echo               the build tree location. Default is
echo               'source directory/rest_vol_cmake_build_files'.
echo               Note that the REST VOL does not support in-source
echo               CMake builds.
echo.
echo       -C DIR  To specify the top-level directory where cURL is
echo               installed, if cURL was not installed to a system
echo               directory. Similar to '-DCURL_ROOT=DIR'.
echo.
echo       -Y DIR  To specify the top-level directory where YAJL is
echo               installed, if YAJL was not installed to a system
echo               directory. Similar to '-DYAJL_ROOT=DIR'.
echo.
EXIT /B 0