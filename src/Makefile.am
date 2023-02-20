#
# Copyright by The HDF Group.
# All rights reserved.
#
# This file is part of the HDF5 REST VOL connector. The full copyright
# notice, including terms governing use, modification, and redistribution,
# is contained in the COPYING file, which can be found at the root of the
# source code distribution tree.
#
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

# Our main target, the REST VOL connector library
lib_LTLIBRARIES=libhdf5_vol_rest.la

# REST VOL's source file and private headers
libhdf5_vol_rest_la_SOURCES=rest_vol.c rest_vol.h rest_vol_attr.c rest_vol_attr.h rest_vol_dataset.c rest_vol_dataset.h \
                            rest_vol_datatype.c rest_vol_datatype.h rest_vol_file.c rest_vol_file.h rest_vol_group.c rest_vol_group.h \
                            rest_vol_link.c rest_vol_link.h rest_vol_object.c rest_vol_object.h \
                            util/rest_vol_err.h util/rest_vol_mem.c util/rest_vol_mem.h \
                            util/rest_vol_hash_table.c util/rest_vol_hash_table.h util/rest_vol_hash_string.h

# Add in debugging sources
if DEBUG_ENABLED
    libhdf5_vol_rest_la_SOURCES += rest_vol_debug.c rest_vol_debug.h
endif

# Public headers
include_HEADERS=rest_vol_public.h rest_vol_config.h

# install libhdf5_vol_rest.settings in lib directory
settingsdir=$(libdir)
settings_DATA=libhdf5_vol_rest.settings

# Automake needs to be taught how to build lib, progs, and tests targets.
# These will be filled in automatically for the most part (e.g.,
# lib_LIBRARIES are built for lib target), but EXTRA_LIB, EXTRA_PROG, and
# EXTRA_TEST variables are supplied to allow the user to force targets to
# be built at certain times.
LIB = $(lib_LTLIBRARIES)
PROGS = 

# lib/progs/tests targets recurse into subdirectories. build-* targets
# build files in this directory.
build-lib: $(LIB)
build-progs: $(LIB) $(PROGS)