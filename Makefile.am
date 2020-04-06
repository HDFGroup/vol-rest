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

AM_CFLAGS = @AM_CFLAGS@ @H5_CFLAGS@
AM_FCFLAGS = @AM_FCFLAGS@ @H5_FCFLAGS@
AM_CXXFLAGS = @AM_CXXFLAGS@ @H5_CXXFLAGS@
AM_CPPFLAGS = @AM_CPPFLAGS@ @H5_CPPFLAGS@
AM_LDFLAGS = @AM_LDFLAGS@ @H5_LDFLAGS@

ACLOCAL_AMFLAGS = -I m4

# List all build rules defined by HDF5 Makefiles as "PHONY" targets here.
# This tells the Makefiles that these targets are not files to be built but
# commands that should be executed even if a file with the same name already
# exists.
.PHONY: build-lib lib tests

SUBDIRS = src

if BUILD_TESTS
  SUBDIRS += test
endif

if BUILD_EXAMPLES
  SUBDIRS += examples
endif

# Some files generated during configure that should be cleaned
DISTCLEANFILES = config/stamp1 config/stamp2

lib:
	for d in $(SUBDIRS); do                               \
	  if test $$d != .; then                              \
	    (cd $$d && $(MAKE) $(AM_MAKEFLAGS) $@) || exit 1; \
	  fi;                                                 \
	done

install: install-recursive
uninstall: uninstall-recursive

install-all:
	@$(MAKE) $(AM_MAKEFLAGS) install
uninstall-all:
	@$(MAKE) $(AM_MAKEFLAGS) uninstall