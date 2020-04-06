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
# REST VOL test suite Makefile(.in)
#

HDF5_INSTALL_DIR=@RV_HDF5_INSTALL_DIR@

AM_CFLAGS=@AM_CFLAGS@ @RV_CFLAGS@
AM_CPPFLAGS=@AM_CPPFLAGS@ @RV_CPPFLAGS@
AM_LDFLAGS=@AM_LDFLAGS@ @RV_LDFLAGS@

ACLOCAL_AMFLAGS=-I m4

LIBRESTVOL = $(top_builddir)/src/libhdf5_vol_rest.la

# Defines for building the test suite
test_PROGRAMS = test_rest_vol
test_rest_vol_SOURCES = test_rest_vol.c
test_rest_vol_LDADD = $(LIBRESTVOL) $(HDF5_INSTALL_DIR)/lib/libhdf5.la
test_rest_vol_CFLAGS = -I$(top_srcdir)/src -I$(HDF5_INSTALL_DIR)/include

testdir=$(bindir)

TESTS = $(test_PROGRAMS)

build-tests: $(TESTS)

# Tell Automake to build tests when the user types `make all' (this is
# not its default behavior).
all-local: $(TESTS)