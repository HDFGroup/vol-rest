#
# Copyright by The HDF Group.
# Copyright by the Board of Trustees of the University of Illinois.
# All rights reserved.
#
# This file is part of HDF5.  The full HDF5 copyright notice, including
# terms governing use, modification, and redistribution, is contained in
# the files COPYING and Copyright.html.  COPYING can be found at the root
# of the source code distribution tree; Copyright.html can be found at the
# root level of an installed copy of the electronic HDF5 document set and
# is linked from the top-level documents page.  It can also be found at
# http://hdfgroup.org/HDF5/doc/Copyright.html.  If you do not have
# access to either file, you may request a copy from help@hdfgroup.org.
##
## Makefile.am
## Run automake to generate a Makefile.in from this file.
#
# REST VOL Makefile(.in)
#

HDF5_INSTALL_DIR=@RV_HDF5_INSTALL_DIR@
CURL_INSTALL_DIR=@RV_CURL_INSTALL_DIR@
YAJL_INSTALL_DIR=@RV_YAJL_INSTALL_DIR@

AM_CFLAGS=@AM_CFLAGS@ @RV_CFLAGS@ -I$(HDF5_INSTALL_DIR)/include
AM_CPPFLAGS=@AM_CPPFLAGS@ @RV_CPPFLAGS@
AM_LDFLAGS=@AM_LDFLAGS@ @RV_LDFLAGS@

ACLOCAL_AMFLAGS=-I m4

# Add in the linker flags for cURL
if !CURL_DEFAULT_DIR
    AM_CFLAGS += -I$(CURL_INSTALL_DIR)/include -L$(CURL_INSTALL_DIR)/lib$(LIB_SUFFIX)
endif

AM_CFLAGS += -lcurl

# Add in the linker flags for YAJL
if !YAJL_DEFAULT_DIR
    AM_CFLAGS += -I$(YAJL_INSTALL_DIR)/include -L$(YAJL_INSTALL_DIR)/lib$(LIB_SUFFIX)
endif

AM_CFLAGS += -lyajl

# Our main target, the REST VOL plugin library
lib_LTLIBRARIES=librestvol.la

# REST VOL's source file and private headers
librestvol_la_SOURCES=rest_vol.c rest_vol.h rest_vol_err.h

# Public headers
include_HEADERS=rest_vol_public.h

# Defines for building the test suite
test_PROGRAMS = test_rest_vol
test_rest_vol_SOURCES = test_rest_vol.c
test_rest_vol_LDADD = librestvol.la $(HDF5_INSTALL_DIR)/lib/libhdf5.la
test_rest_vol_CFLAGS = -I$(HDF5_INSTALL_DIR)/include

testdir=$(bindir)

# Automake needs to be taught how to build lib, progs, and tests targets.
# These will be filled in automatically for the most part (e.g.,
# lib_LIBRARIES are built for lib target), but EXTRA_LIB, EXTRA_PROG, and
# EXTRA_TEST variables are supplied to allow the user to force targets to
# be built at certain times.
LIB = $(lib_LTLIBRARIES)
PROGS = 
TESTS = $(test_PROGRAMS)

# lib/progs/tests targets recurse into subdirectories. build-* targets
# build files in this directory.
build-lib: $(LIB)
build-progs: $(LIB) $(PROGS)
build-tests: $(LIB) $(PROGS) $(TESTS)

# Tell Automake to build tests when the user types `make all' (this is
# not its default behavior).
all-local: $(TESTS)