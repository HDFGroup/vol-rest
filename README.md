# HDF5 REST VOL plugin

HDF5 REST VOL plugin version 1.0.0 - currently under development


### Table of Contents:

    I. Introduction
    II. Installation
        A. Prerequisites
            i. External Libraries
            ii. HSDS Access
        B. Building the REST VOL plugin
            i. Obtaining the Source
            ii. One-Step Build
                a. Build Script Options
            iii. Manual Build
                a. Options for `configure`
            iv. Build Results
        C. Testing the REST VOL Installation
    III. Using the REST VOL plugin
        A. Writing REST VOL Applications
            i. Skeleton Example
        B. Building REST VOL Applications
        C. REST VOL Applications and HSDS
            i. HSDS Setup
            ii. Example Applications
    IV. Feature Support
        A. Unsupported HDF5 API calls
        B. Unsupported HDF5 Features
        C. Problematic HDF5 Features
    V. More Information



# I. Introduction

The REST VOL plugin is a plugin for HDF5 designed with the goal of allowing
HDF5 applications, both existing and future, to utilize web-based storage
systems by translating HDF5 API calls into HTTP-based REST calls, as defined
by the HDF5 REST API (See section V. for more information on RESTful HDF5).

Using a VOL plugin allows an existing HDF5 application to interface with
different storage systems with minimal changes necessary. The plugin accomplishes
this by utilizing the HDF5 Virtual Object Layer in order to re-route HDF5's
public API calls to specific callbacks in the plugin which handle all of the
usual HDF5 operations. The HDF5 Virtual Object Layer is an abstraction layer
that sits directly between HDF5's public API and the underlying storage system.
In this manner of operation, the mental data model of an HDF5 application can
be preserved and transparently mapped onto storage systems that differ from a
native filesystem, such as Amazon's S3.

The REST VOL plugin is under active development, and details given here may
change.

--------------------------------------------------------------------------------

# II. Installation

Notes and instructions related to obtaining, building and installing the REST VOL
plugin and accompanying HDF5 library.


## II.A. Prerequisites

TODO: short description

### II.A.i. External Libraries

To build the REST VOL plugin, the following libraries are required:

+ cURL - networking support
    + https://curl.haxx.se/

+ YAJL - JSON parsing and construction
    + https://lloyd.github.io/yajl/

Compiled libraries must either exist in the system's library paths or must be
supplied to the REST VOL's build scripts. Refer to section II.B.ii. below for
more information.



## II.B. Building the REST VOL plugin

### II.B.i. Obtaining the Source

The latest and most up-to-date REST VOL code can be viewed at:

`https://bitbucket.hdfgroup.org/users/jhenderson/repos/rest-vol/browse`

and can directly be obtained from:

`https://bitbucket.hdfgroup.org/scm/~jhenderson/rest-vol.git`

A source distribution of HDF5 has been included in the REST VOL plugin source
in the `/hdf5` directory. This version of HDF5 has been modified to support
the REST VOL plugin.

### II.B.ii. One-step Build

TODO: Short description

+ Autotools
  Run `build_vol_autotools.sh`. See section II.B.ii.a for configuration options.

+ CMake
  *** NOTE: CMake support is currently not functional and should not be used ***
  Run `build_vol_cmake.sh` (Linux or OS X) or `build_vol_cmake.bat` (Windows).

By default, all of these build scripts will compile and link with the provided
HDF5 source distribution. However, if you wish to use a manually built version of
the HDF5 library, include the flag `-H <dir>` where `dir` is the path to the HDF5
install prefix. Refer to the documentation in `hdf5/release_docs` (where `hdf5` is
the HDF5 distribution root directory) for more information on building HDF5 manually.

TODO: Only VOL-enabled HDF5 versions will work

### II.B.ii.a. Build Script Options

The following configuration options are available to all of the build scripts:

    -h      Prints out a help message indicating script usage and available options.

    -d      Enables debugging information printouts within the REST VOL plugin.

    -C      Enables debug information printouts from cURL within the REST VOL plugin.

    -m      Enables memory usage tracking within the REST VOL plugin. This options is
            mostly useful in helping to diagnose any possible memory leaks or other
            memory errors within the plugin.

    -H DIR  Prevents building of the provided HDF5 source. Instead, uses the compiled
            library found at directory `DIR`, where `DIR` is the path used as the
            installation prefix when building HDF5 manually.

    -p DIR  Specifies where the REST VOL plugin should be installed. The default
            installation prefix is `rest_vol_build` inside the REST VOL source root
            directory.

    -c DIR  Specifies the top-level directory where cURL is installed. Used if cURL is
            not installed to a system path or used to override 
            
    -y DIR  Specifies the top-level directory where YAJL is installed. Used if YAJL is
            not installed to a system path or used to override

    -t      Build the HDF5 tools with REST VOL support.
            WARNING: This option is experimental and should not currently be used.

### II.B.iii. Manual Build

TODO: how to build manually.

Autotools
---------

    $ autogen.sh
    $ configure [options]
    $ make
    $ make check (requires HSDS setup -- see section III.C.i.)
    $ make install

CMake
-----


### II.B.iii.a. Options for `configure`

When building the REST VOL plugin manually using Autotools, the following options are
available to `configure`.

The options in the supplied build script are mapped to the corresponding options here:

    -h, --help      Prints out a help message indicating script usage and available
                    options.

    --enable-build-mode=(production|debug)
                    Sets the build mode to be used.
                    Debug - enable debugging printouts within the REST VOL plugin.
                    Production - Focus more on optimization.

    --enable-curl-debug
                    Enables debug information printouts from cURL withing the REST VOL
                    plugin.

    --enable-mem-tracking
                    Enables memory tracking within the REST VOL plugin. This option is
                    mostly useful in helping to diagnose any possible memory leaks or
                    other memory errors within the plugin.

    --with-hdf5=DIR Used to specify the directory where an HDF5 distribution that uses
                    the VOL layer has already been built. This is to help the REST VOL
                    locate the HDF5 header files that it needs to include.

    --with-curl=DIR Used to specify the top-level directory where cURL is installed, if
                    cURL is not installed to a system path.

    --with-yajl=DIR Used to specify the top-level directory where YAJL is installed, if
                    YAJL is not installed to a system path.

    --prefix=DIR    Specifies the location for the resulting files. The default location
                    is `rest_vol_build` in the same directory as configure.

### II.B.iv. Build Results

If the build is successful, files are written into an installation directory. By default,
these files are placed in `rest_vol_build` in the REST VOL source root directory. This
default can be overridden with `build_vol_autotools.sh -p DIR` or `configure --prefix=<DIR>`
(for Autotools) or

TODO: CMake prefix option.

If the REST VOL was built using one of the included build scripts, all of the usual files
from an HDF5 source build should appear in the respective `bin`, `include`, `lib` and `share`
directories in the install directory. Notable among these is `bin/h5cc` (when built with
Autotools), a special-purpose compiler that streamlines the process of building HDF5
applications.

TODO: h5cc is not a compiler, it is a wrapper script.



## II.C. Testing the REST VOL plugin installation

The REST VOL plugin tests require HSDS setup and access -- see section III.C.i.

After building the REST VOL plugin, it is highly advised that you run `make check` to
verify that the HDF5 library and REST VOL plugin are functioning correctly.

TODO: incorporate section on 'test_rest_vol'

--------------------------------------------------------------------------------

# III. Using the REST VOL plugin

This section outlines the unique aspects of writing, building and running HDF5
applications with the REST VOL plugin.



## III.A. Writing HDF5 REST VOL applications

Any HDF5 application using the REST VOL must:

+ Include `rest_vol_public.h`, found in the `include` directory of the REST VOL
  plugin installation directory.

+ Link against `librestvol.a` (or similar), found in the `lib` directory of the
  REST VOL plugin installation directory.

An HDF5 REST VOL application requires three additional function calls in addition
to those for an equivalent HDF5 application:

+ RVinit() - Initializes the REST VOL plugin
  Called upon application startup, before any file is accessed.

  TODO: Set File Access Property List with what?
+ H5Pset_fapl_rest_vol() - Set File Access Property List.
  Called to prepare a FAPL to open a file through the REST VOL plugin. See
  `https://support.hdfgroup.org/HDF5/Tutor/property.html#fa` for more information
  about File Access Property Lists.

+ RVterm() - Cleanly shutdown the REST VOL plugin
  Called on application shutdown, after all files have been closed.

### III.A.i. Skeleton Example

Below is a no-op application that opens and closes a file using the REST VOL plugin.
For clarity, no error-checking is performed.

    #include "hdf5.h"
    #include "rest_vol_public.h"

    int main(void)
    {
        hid_t fapl_id;
        hid_t file_id;

        RVinit();

        fapl_id = H5Pcreate(H5P_FILE_ACCESS);
        H5Pset_fapl_rest_vol(fapl_id);
        file_id = H5Fopen("my/file.h5");

        /* operate on file */

        H5Pclose(fapl_id);
        H5Fclose(file_id);

        RVterm();

        return 0;
    }



## III.B. Building HDF5 REST VOL applications





