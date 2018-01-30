#!/bin/sh
#
# Copyright by The HDF Group.                                              
# All rights reserved.                                                     
#                                                                          
# This file is part of HDF5. The full HDF5 copyright notice, including     
# terms governing use, modification, and redistribution, is contained in   
# the files COPYING and Copyright.html.  COPYING can be found at the root  
# of the source code distribution tree; Copyright.html can be found at the 
# root level of an installed copy of the electronic document set and is    
# linked from the top-level documents page.  It can also be found at       
# http://hdfgroup.org/HDF5/doc/Copyright.html.  If you do not have access  
# to either file, you may request a copy from help@hdfgroup.org.           
#

# A script used to first configure and build the HDF5 source distribution
# included with the REST VOL plugin source code, and then use that built
# HDF5 to build the REST VOL plugin itself.

# Default name of the directory for the included HDF5 source distribution
HDF5_DIR="hdf5"

# Get the directory of the script itself
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Set the default install directory
INSTALL_DIR=${SCRIPT_DIR}/rest_vol_build

NPROCS=0

build_static=true
build_shared=false

# Default is to not build tools due to circular dependency on VOL being
# already built
build_tools=false

# Compiler flags for linking with cURL and YAJL
CURL_DIR=""
CURL_LINK="-lcurl"
YAJL_DIR=""
YAJL_LINK="-lyajl"

# Compiler flag for linking with the built REST VOL
REST_VOL_LINK="-lrestvol"

# Extra compiler options passed to the various steps, such as -Wall
COMP_OPTS="-Wall -pedantic -Wunused-macros"


echo
echo "******************************"
echo "* REST VOL build script      *"
echo "******************************"
echo

optspec=":hstc:y:p:-"
while getopts "$optspec" optchar; do
    case "${optchar}" in
    h)
        echo "usage: $0 [OPTIONS]"
        echo
        echo "      -h      Print this help message."
        echo
        echo "      -p DIR  Similar to 'configure --prefix', specifies where"
        echo "              the REST VOL should be installed to. Default is"
        echo "              'source directory/rest_vol_build'."
        echo
        echo "      -c DIR  To specify the directory to search for libcurl"
        echo "              within, if cURL was not installed to a system"
        echo "              directory."
        echo
        echo "      -y DIR  To specify the directory to search for libyajl"
        echo "              within, if YAJL was not installed to a system"
        echo "              directory."
        echo
        echo "      -s      Build the REST VOL plugin as a shared library."
        echo "              By default it is built statically."
        echo
        echo "      -t      Build the tools with REST VOL support. Note"
        echo "              that due to a circular build dependency, this"
        echo "              option should not be chosen until after the"
        echo "              included HDF5 source distribution and the"
        echo "              REST VOL plugin have been built once."
        echo
        exit 0
        ;;
    p)
        INSTALL_DIR="$OPTARG"
        echo "Prefix set to: ${INSTALL_DIR}"
        echo
        ;;
    c)
        CURL_DIR="$OPTARG"
        echo "Libcurl directory set to: ${CURL_DIR}"
        echo
        ;;
    y)
        YAJL_DIR="$OPTARG"
        echo "Libyajl directory set to: ${YAJL_DIR}"
        echo
        ;;
    s)
        echo "Building REST VOL as shared library"
        echo
        build_static=false
        build_shared=true
        ;;
    t)
        echo "Building tools with REST VOL support"
        echo
        build_tools=true
        ;;
    *)
        if [ "$OPTERR" != 1 ] || case $optspec in :*) ;; *) false; esac; then
            echo "ERROR: non-option argument: '-${OPTARG}'" >&2
            echo "Quitting"
            exit 1
        fi
        ;;
    esac
done


# If the libcurl and libyajl directories were specified on the command line,
# add the individual linker search flags into the flags for linking cURL and
# YAJL
if [ ! -z ${CURL_DIR} ]; then
    CURL_LINK="-L${CURL_DIR} ${CURL_LINK}"
fi

if [ ! -z ${YAJL_DIR} ]; then
    YAJL_LINK="-L${YAJL_DIR} ${YAJL_LINK}"
fi


# Try to determine a good number of cores to use for parallelizing both builds
if [ "$NPROCS" -eq "0" ]; then
    NPROCS=`getconf _NPROCESSORS_ONLN 2> /dev/null`
    
    # Handle FreeBSD
    if [ -z "$NPROCS" ]; then
        NPROCS=`getconf NPROCESSORS_ONLN 2> /dev/null`
    fi
fi


# First build HDF5
echo "*****************"
echo "* Building HDF5 *"
echo "*****************"
echo

cd ${SCRIPT_DIR}/${HDF5_DIR}

./autogen.sh

# If we are building the tools with REST VOL support, link in the already built
# REST VOL library, along with cURL and YAJL.
if [ "${build_tools}" = true ]; then
    ./configure --prefix=${INSTALL_DIR} CFLAGS="${COMP_OPTS} -L${INSTALL_DIR}/lib ${REST_VOL_LINK} ${CURL_LINK} ${YAJL_LINK}" || exit 1
else
    ./configure --prefix=${INSTALL_DIR} CFLAGS="${COMP_OPTS}" || exit 1
fi

make -j${NPROCS} && make install || exit 1


# Once HDF5 has been built, build the REST VOL plugin against HDF5.
echo "*******************************************"
echo "* Building REST VOL plugin and test suite *"
echo "*******************************************"
echo

cd ${SCRIPT_DIR}

./autogen.sh

./configure --prefix=${INSTALL_DIR} CFLAGS="-I${INSTALL_DIR}/include ${COMP_OPTS} ${CURL_LINK} ${YAJL_LINK}"

make -j${NPROCS} && make install || exit 1

exit 0