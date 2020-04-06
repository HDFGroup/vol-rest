#
# Copyright by The HDF Group.
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
##
#
# REST VOL HDF5 Library Examples Makefile(.in)
#

# Libraries to link to while building
LIBHDF5=$(top_builddir)/src/libhdf5.la

# Scripts used to build examples
# If only shared libraries have been installed, have h5cc build examples with
# shared libraries instead of static libraries
H5CC=${DESTDIR}$(bindir)/h5cc


# H5_CFLAGS holds flags that should be used when building hdf5,
# but which should not be exported to h5cc for building other programs.
# AM_CFLAGS is an automake construct which should be used by Makefiles 
# instead of CFLAGS, as CFLAGS is reserved solely for the user to define.
# This applies to FCFLAGS, CXXFLAGS, CPPFLAGS, and LDFLAGS as well.

AM_CFLAGS=@AM_CFLAGS@ @RV_CFLAGS@
AM_FCFLAGS=@AM_FCFLAGS@ @RV_FCFLAGS@
AM_CXXFLAGS=@AM_CXXFLAGS@ @RV_CXXFLAGS@
AM_CPPFLAGS=@AM_CPPFLAGS@ @RV_CPPFLAGS@
AM_LDFLAGS=@AM_LDFLAGS@ @RV_LDFLAGS@

ACLOCAL_AMFLAGS=-I m4

# .chkexe files are used to mark tests that have run successfully.
# .chklog files are output from those tests.
CHECK_CLEANFILES=*.chkexe *.chklog

INSTALL_SCRIPT_FILES = run-c-ex.sh
INSTALL_TOP_SCRIPT_FILES = run-all-ex.sh
INSTALL_TOP_FILES = README

# Example programs.
# Don't tell automake about them, because if it knew they were programs,
# it would try to compile them instead of using the h5cc script.
# Use the boilerplate in config/examples.am instead.
EXAMPLE_PROG = rv_write rv_read rv_chunk_read rv_compound \
           rv_crtgrpd rv_subset rv_rdwt rv_crtgrpar \
           rv_crtatt rv_crtgrp rv_crtdat \
           rv_group rv_select rv_attribute \
           rv_extlink hl/rv_ds1
TEST_SCRIPT=testh5cc.sh 
TEST_EXAMPLES_SCRIPT=$(INSTALL_SCRIPT_FILES)

# Install files
# List all file that should be installed in examples directory
INSTALL_FILES = rv_write.c rv_read.c rv_chunk_read.c   \
          rv_crtgrpd.c rv_subset.c rv_rdwt.c rv_crtgrpar.c \
          rv_crtatt.c rv_crtgrp.c rv_crtdat.c \
          rv_compound.c rv_group.c rv_select.c rv_attribute.c \
          rv_extlink.c hl/rv_ds1.c



# How to build examples, using installed version of h5cc
$(EXTRA_PROG): $(H5CC)
	$(H5CC) $(H5CCFLAGS) $(CFLAGS) -o $@ $(srcdir)/$@.c;

# Some examples depend on files created by other examples.
rv_read.chkexe_: rv_write.chkexe_
rv_chunk_read.chkexe_: rv_extend_write.chkexe_
rv_crtgrpd.chkexe_: rv_crtgrpar.chkexe_
# rv_rdwt and rv_crtatt both modify the same file created by
# rv_crtdat. Serialize them.
rv_rdwt.chkexe_: rv_crtdat.chkexe_
rv_crtatt.chkexe_: rv_rdwt.chkexe_

# The external link examples demonstrate how to use paths; they need
# directories to be created to do this.
EXTLINK_DIRS=red blue u2w

$(EXTLINK_DIRS):
	echo $(mkdir_p) $@
	$(mkdir_p) $@

CHECK_CLEANFILES+=$(EXTLINK_DIRS)

# Example directory
# Note: no '/' after DESTDIR.  Explanation in commence.am
EXAMPLEHLDIR=${DESTDIR}$(exec_prefix)/share/rest_vol_examples/c/hl
EXAMPLEDIR=${DESTDIR}$(exec_prefix)/share/rest_vol_examples/c
EXAMPLETOPDIR=${DESTDIR}$(exec_prefix)/share/rest_vol_examples
EXAMPLEBASEDIR=${DESTDIR}$(exec_prefix)/share

# List dependencies for each program.  Normally, automake would take
# care of this for us, but if we tell automake about the programs it
# will try to build them with the normal C compiler, not h5cc.  This is
# an inelegant way of solving the problem.
# All programs share the same build rule and a dependency on the main hdf5
# library above.
rv_chunk_read: $(srcdir)/rv_chunk_read.c
rv_compound: $(srcdir)/rv_compound.c
rv_crtgrpd: $(srcdir)/rv_crtgrpd.c
rv_subset: $(srcdir)/rv_subset.c
rv_rdwt: $(srcdir)/rv_rdwt.c
rv_crtgrpar: $(srcdir)/rv_crtgrpar.c
rv_crtatt: $(srcdir)/rv_crtatt.c
rv_crtgrp: $(srcdir)/rv_crtgrp.c
rv_crtdat: $(srcdir)/rv_crtdat.c
rv_group: $(srcdir)/rv_group.c
rv_write: $(srcdir)/rv_write.c
rv_read: $(srcdir)/rv_read.c
rv_select: $(srcdir)/rv_select.c
rv_attribute: $(srcdir)/rv_attribute.c
rv_extlink: $(srcdir)/rv_extlink.c $(EXTLINK_DIRS)
hl/rv_ds1: $(srcdir)/hl/rv_ds1.c

# Assume that all tests in this directory are examples, and tell
# conclude.am when to build them.
EXTRA_PROG = $(EXAMPLE_PROG) $(EXAMPLE_PROG_PARA)

# We need to tell automake what to clean
MOSTLYCLEANFILES=*.raw *.meta *.o
CHECK_CLEANFILES+=*.h5
CLEANFILES=$(EXAMPLE_PROG) $(EXAMPLE_PROG_PARA)

# How to create EXAMPLEDIR if it doesn't already exist
$(EXAMPLEBASEDIR):
	mkdir -p $(EXAMPLEBASEDIR)
$(EXAMPLETOPDIR):
	mkdir -p $(EXAMPLEBASEDIR)
	mkdir -p $(EXAMPLETOPDIR)
$(EXAMPLEDIR):
	mkdir -p $(EXAMPLEBASEDIR)
	mkdir -p $(EXAMPLETOPDIR)
	mkdir -p $(EXAMPLEDIR)
	mkdir -p $(EXAMPLEHLDIR)

# Install and uninstall rules.  We install the source files, not the
# example programs themselves.
install-data-local:
	@$(MAKE) $(AM_MAKEFLAGS) install-examples
uninstall-local:
	@$(MAKE) $(AM_MAKEFLAGS) uninstall-examples

install-examples: $(EXAMPLEDIR) $(INSTALL_FILES) 
	@for f in X $(INSTALL_FILES); do                                     \
	  if test $$f != X; then                                             \
	    (set -x; $(INSTALL) $(srcdir)/$$f $(EXAMPLEDIR)/$$f || exit 1);    \
            chmod a-x $(EXAMPLEDIR)/$$f;                                     \
	  fi;                                                                \
	done
	@for f in X $(INSTALL_SCRIPT_FILES); do                                   \
	  if test $$f != X; then                                             \
	    (set -x; $(INSTALL) $$f $(EXAMPLEDIR)/. || exit 1);\
	  fi;                                                                \
	done
	@for f in X $(INSTALL_TOP_FILES); do                       \
	  if test $$f != X; then                                             \
	    (set -x; $(INSTALL) $(srcdir)/$$f $(EXAMPLETOPDIR)/. || exit 1); \
            chmod a-x $(EXAMPLETOPDIR)/$$f;\
	  fi;                                                                \
	done
	@for f in X $(INSTALL_TOP_SCRIPT_FILES); do                       \
	  if test $$f != X; then                                             \
	    (set -x; $(INSTALL) $(srcdir)/$$f $(EXAMPLETOPDIR)/. || exit 1); \
	  fi;                                                                \
	done

uninstall-examples:
	@if test -n "$(INSTALL_FILES)" -a -d $(EXAMPLEDIR); then             \
	  set -x; cd $(EXAMPLEDIR) && $(RM) $(INSTALL_FILES);                \
	fi
	@if test -n "$(INSTALL_SCRIPT_FILES)" -a -d $(EXAMPLEDIR); then           \
	  set -x; cd $(EXAMPLEDIR) && $(RM) $(INSTALL_SCRIPT_FILES);              \
	fi
	@if test -n "$(INSTALL_TOP_FILES)" -a -d $(EXAMPLETOPDIR); then    \
	  set -x; cd $(EXAMPLETOPDIR) && $(RM) $(INSTALL_TOP_FILES);       \
	fi
	@if test -n "$(INSTALL_TOP_SCRIPT_FILES)" -a -d $(EXAMPLETOPDIR); then    \
	  set -x; cd $(EXAMPLETOPDIR) && $(RM) $(INSTALL_TOP_SCRIPT_FILES);       \
	fi

installcheck-local:
	@if test "$(STATIC_SHARED)" = "static, shared"; then               \
	  H5CCFLAGS="-shlib" $(MAKE) $(AM_MAKEFLAGS) check;                \
	  $(MAKE) $(AM_MAKEFLAGS) clean;                                   \
	  H5CCFLAGS="" $(MAKE) $(AM_MAKEFLAGS) check;                      \
	elif test "$(STATIC_SHARED)" = "shared"; then                     \
	  H5CCFLAGS="-shlib" $(MAKE) $(AM_MAKEFLAGS) check;                \
	else                                                              \
	  $(MAKE) $(AM_MAKEFLAGS) check;                                   \
	fi 
	@if test "$(INSTALL_FILES)" -a $(TEST_EXAMPLES_SCRIPT) -a -d $(EXAMPLEDIR); then \
	    echo "============================";                                         \
	    echo "Testing $(TEST_EXAMPLES_SCRIPT)";                                      \
	    echo "============================";                                         \
	    (cd $(EXAMPLEDIR);                                                           \
	     /bin/sh ./$(TEST_EXAMPLES_SCRIPT);)                                                   \
	fi
