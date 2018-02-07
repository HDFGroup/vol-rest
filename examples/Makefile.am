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

INSTALL_SCRIPT_FILES = run-c-ex.sh
INSTALL_TOP_SCRIPT_FILES = run-all-ex.sh
INSTALL_TOP_FILES = README

# Example programs.
# Don't tell automake about them, because if it knew they were programs,
# it would try to compile them instead of using the h5cc script.
# Use the boilerplate in config/examples.am instead.
EXAMPLE_PROG = h5_write h5_read h5_chunk_read h5_compound \
           h5_crtgrpd h5_subset h5_rdwt h5_crtgrpar \
           h5_crtatt h5_crtgrp h5_crtdat \
           h5_group h5_select h5_attribute \
           h5_extlink
TEST_SCRIPT=testh5cc.sh 
TEST_EXAMPLES_SCRIPT=$(INSTALL_SCRIPT_FILES)

# Install files
# List all file that should be installed in examples directory
INSTALL_FILES = h5_write.c h5_read.c h5_chunk_read.c   \
          h5_crtgrpd.c h5_subset.c h5_rdwt.c h5_crtgrpar.c \
          h5_crtatt.c h5_crtgrp.c h5_crtdat.c \
          h5_compound.c h5_group.c h5_select.c h5_attribute.c \
          h5_extlink.c



# How to build examples, using installed version of h5cc
$(EXTRA_PROG): $(H5CC)
	$(H5CC) $(H5CCFLAGS) $(CFLAGS) -o $@ $(srcdir)/$@.c;

# Some examples depend on files created by other examples.
h5_read.chkexe_: h5_write.chkexe_
h5_chunk_read.chkexe_: h5_extend_write.chkexe_
h5_crtgrpd.chkexe_: h5_crtgrpar.chkexe_
# h5_rdwt and h5_crtatt both modify the same file created by
# h5_crtdat. Serialize them.
h5_rdwt.chkexe_: h5_crtdat.chkexe_
h5_crtatt.chkexe_: h5_rdwt.chkexe_

# The external link examples demonstrate how to use paths; they need
# directories to be created to do this.
EXTLINK_DIRS=red blue u2w

$(EXTLINK_DIRS):
	echo $(mkdir_p) $@
	$(mkdir_p) $@

CHECK_CLEANFILES+=$(EXTLINK_DIRS)

# Example directory
# Note: no '/' after DESTDIR.  Explanation in commence.am
EXAMPLEDIR=${DESTDIR}$(exec_prefix)/share/rest_vol_examples/c
EXAMPLETOPDIR=${DESTDIR}$(exec_prefix)/share/rest_vol_examples

# List dependencies for each program.  Normally, automake would take
# care of this for us, but if we tell automake about the programs it
# will try to build them with the normal C compiler, not h5cc.  This is
# an inelegant way of solving the problem.
# All programs share the same build rule and a dependency on the main hdf5
# library above.
h5_chunk_read: $(srcdir)/h5_chunk_read.c
h5_compound: $(srcdir)/h5_compound.c
h5_crtgrpd: $(srcdir)/h5_crtgrpd.c
h5_subset: $(srcdir)/h5_subset.c
h5_rdwt: $(srcdir)/h5_rdwt.c
h5_crtgrpar: $(srcdir)/h5_crtgrpar.c
h5_crtatt: $(srcdir)/h5_crtatt.c
h5_crtgrp: $(srcdir)/h5_crtgrp.c
h5_crtdat: $(srcdir)/h5_crtdat.c
h5_group: $(srcdir)/h5_group.c
h5_write: $(srcdir)/h5_write.c
h5_read: $(srcdir)/h5_read.c
h5_select: $(srcdir)/h5_select.c
h5_attribute: $(srcdir)/h5_attribute.c
h5_extlink: $(srcdir)/h5_extlink.c $(EXTLINK_DIRS)

if BUILD_SHARED_SZIP_CONDITIONAL
LD_LIBRARY_PATH=$(LL_PATH)
endif

# Assume that all tests in this directory are examples, and tell
# conclude.am when to build them.
EXTRA_PROG = $(EXAMPLE_PROG) $(EXAMPLE_PROG_PARA)

# We need to tell automake what to clean
MOSTLYCLEANFILES=*.raw *.meta *.o
CHECK_CLEANFILES+=*.h5
CLEANFILES=$(EXAMPLE_PROG) $(EXAMPLE_PROG_PARA)

# How to create EXAMPLEDIR if it doesn't already exist
$(EXAMPLEDIR):
	-$(top_srcdir)/bin/mkdirs $@
$(EXAMPLETOPDIR):
	-$(top_srcdir)/bin/mkdirs $@

# Install and uninstall rules.  We install the source files, not the
# example programs themselves.
install-data-local:
	@$(MAKE) $(AM_MAKEFLAGS) install-examples
uninstall-local:
	@$(MAKE) $(AM_MAKEFLAGS) uninstall-examples

install-examples: $(EXAMPLEDIR) $(INSTALL_FILES) 
	@for f in X $(INSTALL_FILES); do                                     \
	  if test $$f != X; then                                             \
	    (set -x; $(INSTALL) $(srcdir)/$$f $(EXAMPLEDIR)/. || exit 1);    \
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
