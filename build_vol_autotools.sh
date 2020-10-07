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

# Default name of the directory for the included HDF5 source distribution,
# as well as the default directory where it gets installed
HDF5_DIR="src/hdf5"
HDF5_INSTALL_DIR="${INSTALL_DIR}"
build_hdf5=true

# Determine the number of processors to use when
# building in parallel with Autotools make
NPROCS=0

# Compiler flags for linking with cURL and YAJL
CURL_DIR=""
CURL_LINK="-lcurl"
YAJL_DIR=""
YAJL_LINK="-lyajl"

# Compiler flag for linking with the built REST VOL
REST_VOL_LINK="-lrestvol"

# Extra compiler options passed to the various steps, such as -Wall
COMP_OPTS="-Wall -pedantic -Wunused-macros"

# Extra options passed to the REST VOLs configure script
RV_OPTS=""


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
    echo "      -g      Enable symbolic debugging of the REST VOL code."
    echo
    echo "      -P DIR  Similar to 'configure --prefix=DIR', specifies"
    echo "              where the REST VOL should be installed to. Default"
    echo "              is 'source directory/rest_vol_build'."
    echo
    echo "      -H DIR  To specify a directory where HDF5 has already"
    echo "              been built."
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

optspec=":hcgtdmH:C:Y:P:-"
while getopts "$optspec" optchar; do
    case "${optchar}" in
    h)
        usage
        exit 0
        ;;
    d)
        RV_OPTS="${RV_OPTS} --enable-build-mode=debug"
        echo "Enabled connector debugging"
        echo
        ;;
    c)
        RV_OPTS="${RV_OPTS} --enable-curl-debug"
        echo "Enabled cURL debugging"
        echo
        ;;
    m)
        RV_OPTS="${RV_OPTS} --enable-mem-tracking"
        echo "Enabled connector memory tracking"
        echo
        ;;
    g)
        COMP_OPTS="-g ${COMP_OPTS}"
        echo "Enabled symbolic debugging"
        echo
        ;;
    P)
        if [ "$HDF5_INSTALL_DIR" = "$INSTALL_DIR" ]; then
            HDF5_INSTALL_DIR="$OPTARG"
            echo "Set HDF5 install directory to: ${HDF5_INSTALL_DIR}"
        fi
        INSTALL_DIR="$OPTARG"
        echo "Prefix set to: ${INSTALL_DIR}"
        echo
        ;;
    H)
        build_hdf5=false
        HDF5_INSTALL_DIR="$OPTARG"
        RV_OPTS="${RV_OPTS} --with-hdf5=${HDF5_INSTALL_DIR}"
        echo "Set HDF5 install directory to: ${HDF5_INSTALL_DIR}"
        echo
        ;;
    C)
        CURL_DIR="$OPTARG"
        CURL_LINK="-L${CURL_DIR}/lib ${CURL_LINK}"
        RV_OPTS="${RV_OPTS} --with-curl=${CURL_DIR}"
        COMP_OPTS="${COMP_OPTS} ${CURL_LINK}"
        echo "Libcurl directory set to: ${CURL_DIR}"
        echo
        ;;
    Y)
        YAJL_DIR="$OPTARG"
        YAJL_LINK="-L${YAJL_DIR}/lib ${YAJL_LINK}"
        RV_OPTS="${RV_OPTS} --with-yajl=${YAJL_DIR}"
        COMP_OPTS="${COMP_OPTS} ${YAJL_LINK}"
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

# Ensure that the HDF5 and VOL tests submodules get checked out
if [ -z "$(ls -A ${SCRIPT_DIR}/${HDF5_DIR})" ]; then
    git submodule init
    git submodule update
fi

# If the user hasn't already, first build HDF5
if [ "$build_hdf5" = true ]; then
    echo "*****************"
    echo "* Building HDF5 *"
    echo "*****************"
    echo

    cd "${SCRIPT_DIR}/${HDF5_DIR}"

    ./autogen.sh || exit 1

    ./configure --prefix="${HDF5_INSTALL_DIR}" CFLAGS="${COMP_OPTS}" || exit 1

    make -j${NPROCS} && make install || exit 1
fi


# Once HDF5 has been built, build the REST VOL connector against HDF5.
echo "*******************************************"
echo "* Building REST VOL connector and test suite *"
echo "*******************************************"
echo

mkdir -p "${INSTALL_DIR}"

cd "${SCRIPT_DIR}"

./autogen.sh || exit 1

./configure --prefix="${INSTALL_DIR}" ${RV_OPTS} CFLAGS="${COMP_OPTS}" || exit 1

make -j${NPROCS} && make install || exit 1

exit 0
