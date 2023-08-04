#!/bin/sh
#
# Copyright by The HDF Group.
# All rights reserved.
#
# This file is part of the HDF5 REST VOL connector. The full copyright
# notice, including terms governing use, modification, and redistribution,
# is contained in the COPYING file, which can be found at the root of the
# source code distribution tree.
#
# A script used to first configure and build the HDF5 source distribution
# included with the REST VOL connector source code, and then use that built
# HDF5 to build the REST VOL connector itself.

# Get the directory of the script itself
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Set the default install directory
INSTALL_DIR="${SCRIPT_DIR}/rest_vol_build"

# Set the default build directory
BUILD_DIR="${SCRIPT_DIR}/rest_vol_cmake_build_files"

# Default name of the directory for the included HDF5 source distribution,
# as well as the default directory where it gets installed
HDF5_DIR="src/hdf5"

# By default, tell CMake to generate Unix Makefiles
CMAKE_GENERATOR="Unix Makefiles"

# Determine the number of processors to use when
# building in parallel with Autotools make
NPROCS=0

# Extra compiler options passed to the various steps, such as -Wall
COMP_OPTS="-Wall -pedantic -Wunused-macros"

# Extra options passed to the REST VOLs CMake script
CONNECTOR_DEBUG_OPT=
CURL_DEBUG_OPT=
MEM_TRACK_OPT=
THREAD_SAFE_OPT=
PREBUILT_HDF5_OPT=
PREBUILT_HDF5_DIR=

echo
echo "*************************"
echo "* REST VOL build script *"
echo "*************************"
echo

usage()
{
    echo "usage: $0 [OPTIONS]"
    echo
    echo "      -h      Print this help message."
    echo
    echo "      -d      Enable debugging output in the REST VOL."
    echo
    echo "      -c      Enable cURL debugging output in the REST VOL."
    echo
    echo "      -m      Enable memory tracking in the REST VOL."
    echo
    echo "      -s      Enable linking to thread safe static hdf5 library."
    echo
    echo "      -G      Specify the CMake Generator to use for the build"
    echo "              files created. Default is 'Unix Makefiles'."
    echo
    echo "      -P DIR  Similar to '-DCMAKE_INSTALL_PREFIX=DIR', specifies"
    echo "              where the REST VOL should be installed to. Default"
    echo "              is 'source directory/rest_vol_build'."
    echo
    echo "      -H DIR  To specify a directory where HDF5 has already"
    echo "              been built."
    echo
    echo "      -B DIR  Specifies the directory that CMake should use as"
    echo "              the build tree location. Default is"
    echo "              'source directory/rest_vol_cmake_build_files'."
    echo "              Note that the REST VOL does not support in-source"
    echo "              CMake builds."
    echo
    echo "      -C DIR  To specify the top-level directory where cURL is"
    echo "              installed, if cURL was not installed to a system"
    echo "              directory."
    echo
    echo "      -Y DIR  To specify the top-level directory where YAJL is"
    echo "              installed, if YAJL was not installed to a system"
    echo "              directory."
    echo
}

optspec=":hctdmsG:H:C:Y:B:P:-"
while getopts "$optspec" optchar; do
    case "${optchar}" in
    h)
        usage
        exit 0
        ;;
    d)
        CONNECTOR_DEBUG_OPT="-DHDF5_VOL_REST_ENABLE_DEBUG=ON"
        echo "Enabled connector debugging"
        echo
        ;;
    c)
        CURL_DEBUG_OPT="-DHDF5_VOL_REST_ENABLE_CURL_DEBUG=ON"
        echo "Enabled cURL debugging"
        echo
        ;;
    m)
        MEM_TRACK_OPT="-DHDF5_VOL_REST_ENABLE_MEM_TRACKING=ON"
        echo "Enabled connector memory tracking"
        echo
        ;;
    s)
        THREAD_SAFE_OPT="-DHDF5_VOL_REST_THREAD_SAFE=ON"
        echo "Enabled linking to static thread safe hdf5 library"
        echo
        ;;
    G)
        CMAKE_GENERATOR="$OPTARG"
        echo "CMake Generator set to: ${CMAKE_GENERATOR}"
        echo
        ;;
    B)
        BUILD_DIR="$OPTARG"
        echo "Build directory set to: ${BUILD_DIR}"
        echo
        ;;
    P)
        INSTALL_DIR="$OPTARG"
        echo "Prefix set to: ${INSTALL_DIR}"
        echo
        ;;
    H)
        PREBUILT_HDF5_OPT="-DPREBUILT_HDF5_DIR=$OPTARG"
        PREBUILT_HDF5_DIR="$OPTARG"
        echo "Set HDF5 install directory to: $OPTARG"
        echo
        ;;
    C)
        CURL_DIR="$OPTARG"
        CURL_LINK="-L${CURL_DIR}/lib ${CURL_LINK}"
        CMAKE_OPTS="--with-curl=${CURL_DIR} ${CMAKE_OPTS}"
        echo "Libcurl directory set to: ${CURL_DIR}"
        echo
        ;;
    Y)
        YAJL_DIR="$OPTARG"
        YAJL_LINK="-L${YAJL_DIR}/lib ${YAJL_LINK}"
        CMAKE_OPTS="--with-yajl=${YAJL_DIR} ${CMAKE_OPTS}"
        echo "Libyajl directory set to: ${YAJL_DIR}"
        echo
        ;;
    *)
        if [ "$OPTERR" != 1 ] || case $optspec in :*) ;; *) false; esac; then
            echo "ERROR: non-option argument: '-${OPTARG}'" >&2
            echo
            usage
            echo
            exit 1
        fi
        ;;
    esac
done


# Try to determine a good number of cores to use for parallelizing both builds
if [ "$NPROCS" -eq "0" ]; then
    NPROCS=`getconf _NPROCESSORS_ONLN 2> /dev/null`

    # Handle FreeBSD
    if [ -z "$NPROCS" ]; then
        NPROCS=`getconf NPROCESSORS_ONLN 2> /dev/null`
    fi
fi

# Ensure that the HDF5 submodule gets checked out
if [ -z "$(ls -A ${SCRIPT_DIR}/${HDF5_DIR})" ]; then
    git submodule init
    git submodule update
fi

# Once HDF5 has been built, build the REST VOL connector against HDF5.
echo "*******************************************"
echo "* Building REST VOL connector and test suite *"
echo "*******************************************"
echo

mkdir -p "${BUILD_DIR}"
mkdir -p "${INSTALL_DIR}"

# Clean out the old CMake cache
rm -f "${BUILD_DIR}/CMakeCache.txt"

cd "${BUILD_DIR}"

CFLAGS="-D_POSIX_C_SOURCE=200809L" cmake -G "${CMAKE_GENERATOR}" -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" "${PREBUILT_HDF5_OPT}" "${CONNECTOR_DEBUG_OPT}" "${CURL_DEBUG_OPT}" "${MEM_TRACK_OPT}" "${THREAD_SAFE_OPT}" "${SCRIPT_DIR}"

echo "Build files have been generated for CMake generator '${CMAKE_GENERATOR}'"

# Build with autotools make by default
if [ "${CMAKE_GENERATOR}" = "Unix Makefiles" ]; then
  make -j${NPROCS} && make install || exit 1
fi

echo "REST VOL (and HDF5) built"

# Clean out the old CMake cache
rm -f "${BUILD_DIR}/CMakeCache.txt"

# Configure vol-tests

mkdir -p "${BUILD_DIR}/tests/vol-tests"
cd "${BUILD_DIR}/tests/vol-tests"

if [ -z "$PREBUILT_HDF5_DIR" ]; then
    HDF5_INSTALL_DIR=$HDF5_DIR
else
    HDF5_INSTALL_DIR=$PREBUILT_HDF5_DIR
fi

CFLAGS="-D_POSIX_C_SOURCE=200809L" cmake -G "${CMAKE_GENERATOR}" -DHDF5_DIR=${HDF5_INSTALL_DIR} -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" "${CONNECTOR_DEBUG_OPT}" "${CURL_DEBUG_OPT}" "${MEM_TRACK_OPT}" "${THREAD_SAFE_OPT}" "${SCRIPT_DIR}/test/vol-tests"

echo "Build files generated for vol-tests"

make || exit 1
 
echo "VOL tests built"

exit 0
