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
                b. Options for CMake
            iv. Build Results
        C. Testing the REST VOL plugin installation
    III. Using the REST VOL plugin
        A. Writing HDF5 REST VOL plugin applications
            i. Skeleton Example
        B. Building HDF5 REST VOL plugin applications
        C. HDF5 REST VOL plugin applications and HSDS
            i. HSDS Setup
            ii. Example applications
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

Before building and using the HDF5 REST VOL plugin, a few requirements must be met.

### II.A.i. External Libraries

To build the REST VOL plugin, the following libraries are required:

+ cURL - networking support
    + https://curl.haxx.se/

+ YAJL (ver. 2.0.4 or greater) - JSON parsing and construction
    + https://lloyd.github.io/yajl/

Compiled libraries must either exist in the system's library paths or must be
supplied to the REST VOL plugin's build scripts. Refer to section II.B.ii. below
for more information.


### II.A.ii. HSDS Access

TODO: HSDS description


## II.B. Building the REST VOL plugin

### II.B.i. Obtaining the Source

The latest and most up-to-date REST VOL plugin code can be viewed at:

https://bitbucket.hdfgroup.org/users/jhenderson/repos/rest-vol/browse

and can directly be obtained from:

`https://bitbucket.hdfgroup.org/scm/~jhenderson/rest-vol.git`

A source distribution of HDF5 has been included in the REST VOL plugin source
in the `/hdf5` directory. This version of HDF5 has been modified to support
the REST VOL plugin.

### II.B.ii. One-step Build

Use one of the supplied Autotools or CMake build scripts, depending on preference or
system support.

+ Autotools

    Run `build_vol_autotools.sh`. See section II.B.ii.a for configuration options.


+ CMake

    Run `build_vol_cmake.sh` (Linux or OS X) or `build_vol_cmake.bat` (Windows).
    See section II.B.ii.a for configuration options.


By default, all of these build scripts will compile and link with the provided
HDF5 source distribution. However, if you wish to use a manually built version of
the HDF5 library, include the flag `-H <dir>` where `dir` is the path to the HDF5
install prefix. Refer to the documentation in `hdf5/release_docs` (where `hdf5` is
the HDF5 distribution root directory) for more information on building HDF5 manually.
Note that if you wish to use a manually built version of the HDF5 library, it must be
a version which contains the VOL abstraction layer; otherwise, the REST VOL plugin will
not function correctly.


### II.B.ii.a. Build Script Options

The following configuration options are available to all of the build scripts:

    -h      Prints out a help message indicating script usage and available options.

    -d      Enables debugging information printouts within the REST VOL plugin.

    -c      Enables debugging information printouts from cURL within the REST VOL plugin.

    -m      Enables memory usage tracking within the REST VOL plugin. This option is
            mostly useful in helping to diagnose any possible memory leaks or other
            memory errors within the plugin.

    -t      Build the HDF5 tools with REST VOL plugin support.
            WARNING: This option is experimental and should not currently be used.

    -P DIR  Specifies where the REST VOL plugin should be installed. The default
            installation prefix is `rest_vol_build` inside the REST VOL plugin source
            root directory.

    -H DIR  Prevents building of the provided HDF5 source. Instead, uses the compiled
            library found at directory `DIR`, where `DIR` is the path used as the
            installation prefix when building HDF5 manually.

    -C DIR  Specifies the top-level directory where cURL is installed. Used if cURL is
            not installed to a system path or used to override 

    -Y DIR  Specifies the top-level directory where YAJL is installed. Used if YAJL is
            not installed to a system path or used to override

Additionally, the CMake build scripts have the following configuration option:

    -B DIR  Specifies the directory that CMake should use as the build tree location.
            The default build tree location is `rest_vol_cmake_build_files` inside the
            REST VOL plugin source root directory. Note that the REST VOL does not
            support in-source CMake builds.

### II.B.iii. Manual Build

In general, the process for building the REST VOL plugin involves either obtaining a VOL-enabled
HDF5 distribution or building one from source. Then, the REST VOL plugin is built using that
HDF5 distribution by including the appropriate header files and linking against the HDF5 library.

Once you have a VOL-enabled HDF5 distribution available, follow the instructions below for your
respective build system in order to build the REST VOL plugin against the HDF5 distribution.


Autotools
---------

    $ autogen.sh
    $ configure --with-hdf5[=DIR] [options]
    $ make
    $ make check (requires HSDS setup -- see section III.C.i.)
    $ make install

CMake
-----

    $ mkdir builddir
    $ cd builddir
    $ cmake -G "CMake Generator (Unix Makefiles, etc.)" -DHDF5_DIR=built_hdf5_dir [options] rest_vol_src_dir
    $ build command (e.g. `make && make install` for CMake Generator "Unix Makefiles")

and optionally:

    $ cpack

### II.B.iii.a. Options for `configure`

When building the REST VOL plugin manually using Autotools, the following options are
available to `configure`.

The options in the supplied Autotools build script are mapped to the corresponding options here:

    -h, --help      Prints out a help message indicating script usage and available
                    options.

    --enable-build-mode=(production|debug)
                    Sets the build mode to be used.
                    Debug - enable debugging printouts within the REST VOL plugin.
                    Production - Focus more on optimization.

    --enable-curl-debug
                    Enables debugging information printouts from cURL within the
                    REST VOL plugin.

    --enable-mem-tracking
                    Enables memory tracking within the REST VOL plugin. This option is
                    mostly useful in helping to diagnose any possible memory leaks or
                    other memory errors within the plugin.

    --enable-tests
                    Enables/Disables building of the REST VOL plugin tests.

    --enable-examples
                    Enables/Disables building of the REST VOL HDF5 examples.

    --enable-tools
                    Enables/Disables building of the HDF5 tools with REST VOL plugin
                    support. (Currently experimental and should not be used)

    --with-hdf5=DIR Used to specify the directory where an HDF5 distribution that uses
                    the VOL layer has already been built. This is to help the REST VOL
                    plugin locate the HDF5 header files that it needs to include.

    --with-curl=DIR Used to specify the top-level directory where cURL is installed, if
                    cURL is not installed to a system path.

    --with-yajl=DIR Used to specify the top-level directory where YAJL is installed, if
                    YAJL is not installed to a system path.

    --prefix=DIR    Specifies the location for the resulting files. The default location
                    is `rest_vol_build` in the same directory as configure.

### II.B.iii.b. Options for CMake

When building the REST VOL plugin manually using CMake, the following options are available.

Some of the options in the supplied CMake build script are mapped to the corresponding options here:

    CMAKE_INSTALL_PREFIX (Default: `rest_vol_build` in REST VOL plugin source root
                          directory)
                    Specifies the directory where CMake will install the resulting
                    files to.

    BUILD_SHARED_LIBS (Default: ON)
                    Enables/Disables building of the REST VOL as a shared library.

    BUILD_STATIC_EXECS (Default: OFF)
                    Enables/Disables building of REST VOL executables as static
                    executables.

    REST_VOL_ENABLE_DEBUG (Default: OFF)
                    Enables/Disables debugging printouts within the REST VOL plugin.

    REST_VOL_ENABLE_CURL_DEBUG (Default: OFF)
                    Enables/Disables debugging information printouts from cURL within
                    the REST VOL plugin.

    REST_VOL_ENABLE_MEM_TRACKING (Default: OFF)
                    Enables/Disables memory tracking withing the REST VOL plugin. This
                    options is mostly useful in helping to diagnose any possible memory
                    leaks or other memory errors within the plugin.

    REST_VOL_ENABLE_EXAMPLES (Default: ON)
                    Enables/Disables building of the REST VOL HDF5 examples.

    BUILD_TESTING (Default: ON)
                    Enables/Disables building of the REST VOL tests.

### II.B.iv. Build Results

If the build is successful, files are written into an installation directory. By default,
these files are placed in `rest_vol_build` in the REST VOL plugin source root directory.
For Autotools, this default can be overridden with `build_vol_autotools.sh -P DIR`
(when using the build script) or `configure --prefix=<DIR>` (when building manually).
For CMake, the equivalent for overriding this default is `build_vol_cmake.sh/.bat -P DIR`
(when using the build script) or `-DCMAKE_INSTALL_PREFIX=DIR` (when building manually).

If the REST VOL plugin was built using one of the included build scripts, all of the usual files
from an HDF5 source build should appear in the respective `bin`, `include`, `lib` and `share`
directories in the install directory. Notable among these is `bin/h5cc` (when built with
Autotools), a special-purpose compiler wrapper script that streamlines the process of building
HDF5 applications.



## II.C. Testing the REST VOL plugin installation

The REST VOL plugin tests require HSDS setup and access -- see section III.C.i.

After building the REST VOL plugin and setting up HSDS access according to the above
reference, it is highly advised that you run `make check` (for Autotools builds) or
`ctest .` (for CMake builds) to verify that the HDF5 library and REST VOL plugin
are functioning correctly.

Each of these commands will run the `test_rest_vol` executable, which is built by
each of the REST VOL plugin's build systems and contains a set of tests to cover a
moderate amount of the HDF5 public API. Alternatively, this executable can simply
be run directly.

--------------------------------------------------------------------------------

# III. Using the REST VOL plugin

This section outlines the unique aspects of writing, building and running HDF5
applications with the REST VOL plugin.



## III.A. Writing HDF5 REST VOL plugin applications

Any HDF5 application using the REST VOL plugin must:

+ Include `rest_vol_public.h`, found in the `include` directory of the REST VOL
  plugin installation directory.

+ Link against `librestvol.a` (or similar), found in the `lib` directory of the
  REST VOL plugin installation directory.

An HDF5 REST VOL plugin application requires three new function calls in addition
to those for an equivalent HDF5 application:

+ RVinit() - Initializes the REST VOL plugin

    Called upon application startup, before any file is accessed.


+ H5Pset_fapl_rest_vol() - Set REST VOL plugin access on File Access Property List.
  
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



## III.B. Building HDF5 REST VOL plugin applications

Assuming an application has been written following the above instructions, the application
must be built prior to running. In general, the application should be built as normal for
any other HDF5 application.

To link in the required libraries, the compiler will likely require the additional linker
flags:

`-lrestvol -lcurl -lyajl`

However, these may vary depending on platform, compiler and installation location of the
REST VOL plugin.

If the REST VOL plugin was built using Autotools, it is highly recommended that compilation
of HDF5 REST VOL plugin applications be done using the supplied `h5cc` script, as it will
manage linking with the HDF5 library. `h5cc` may be found in the `/bin` directory of the
installation. The above notice about additional libraries applies to usage of `h5cc`.
For example:

`h5cc -lrestvol -curl -lyajl my_restvol_application.c -o my_executable`



## III.C. HDF5 REST VOL plugin applications and HSDS

Running applications that use the REST VOL plugin requires... 

TODO: mention HSDS

### III.C.i. HSDS Setup

TODO: HSDS setup details


For the REST VOL plugin to correctly interact with an HSDS server instance, there are three
environment variables the plugin uses which must be first set. These are:

    + HSDS_USERNAME - 

    + HSDS_PASSWORD - 

    + HSDS_ENDPOINT - 


### III.C.ii. Example applications

The file `test/test_rest_vol.c`, in addition to being the source for the REST VOL plugin
test suite, serves double purpose with each test function being an example application
in miniature, focused on a particular behavior. This application tests a moderate amount
of HDF5's public API functionality with the REST VOL plugin and should be a good indicator
of whether the REST VOL plugin is working correctly in conjunction with a running HSDS
instance.

In addition to this file, some of the example C applications included with HDF5
distributions have been adapted to work with the REST VOL plugin and are included
under the top-level `examples` directory in the REST VOL plugin source root directory.

Before running any of the examples, an HSDS server must be set up, with the relevant
environment variables set (see section III.C.i. for more information).

--------------------------------------------------------------------------------

# IV. Feature Support

Not all aspects of HDF5 are implemented by or are applicable to the REST VOL plugin.



## IV.A. Unsupported HDF5 API calls

Due to a combination of lack of server support and the complexity in implementing them,
or due to a particular call not making sense from the server's perspective, the following
HDF5 API calls are currently unsupported:

+ H5A interface

    + H5Aopen_by_idx
    + H5Aget_info_by_idx
    + H5Aget_name_by_idx
    + H5Aget_storage_size
    + H5Adelete_by_idx
    + H5Arename
    + H5Arename_by_name

+ H5D interface

    + H5Dget_offset
    + H5Dget_space_status
    + H5Dget_storage_size
    + H5Dset_extent

+ H5F interface

    + H5Fget_obj_count
    + H5Fget_obj_ids
    + H5Fflush
    + H5Fis_accessible
    + H5Fmount
    + H5Funmount
    + H5Fclear_elink_file_cache
    + H5Fget_file_image
    + H5Fget_free_sections
    + H5Fget_freespace
    + H5Fget_mdc_config
    + H5Fget_mdc_hit_rate
    + H5Fget_mdc_size
    + H5Fget_filesize
    + H5Fget_vfd_handle
    + H5Freset_mdc_hit_rate_stats
    + H5Fset_mdc_config

+ H5G interface

    + H5Gget_info_by_idx

+ H5L interface

    + H5Lget_info_by_idx
    + H5Lget_name_by_idx
    + H5Lget_val_by_idx
    + H5Ldelete_by_idx
    + H5Lcopy
    + H5Lmove

+ H5O interface

    + H5Oopen_by_idx
    + H5Oopen_by_addr
    + H5Oget_info_by_idx
    + H5Oincr_refcount
    + H5Odecr_refcount
    + H5Oexists_by_name
    + H5Ovisit
    + H5Ovisit_by_name
    + H5Ocopy

+ H5R interface

    + H5Rget_name
    + H5Rget_region/Region references



## IV.B. Unsupported HDF5 Features

The following other features are currently unsupported:

+ Dataset Fill Values
+ Virtual Dataset layouts
+ External Storage for contiguous dataset layouts

+ Non-predefined integer and floating-point datatypes
+ Variable-length, Opaque, Bitfield and Time datatypes
+ Character sets other than H5T_CSET_ASCII for string datatypes
+ String padding values other than H5T_STR_NULLPAD for fixed-length strings
+ String padding values other than H5T_STR_NULLTERM for variable-length strings
  (Note that variable-length string datatypes are currently unsupported by the
  REST VOL plugin, but a dataset can still be created with a variable-length
  string type)

+ Non-regular hyperslab selections
+ Non-contiguous hyperslab selections

+ User-defined links
+ External links

+ H5Pset_create_intermediate_group property (the plugin will not currently
  create intermediate groups in a path if they do not exist)



## IV.C. Problematic HDF5 Features

Due to underlying implementation details, the following circumstances are
known to be problematic for the REST VOL plugin and will likely cause issues
for the application if not avoided or taken into account:

+ Cyclic links in the file. The plugin currently cannot detect cyclic links,
  which will generally end in infinite recursion and application stack issues.

+ Trying to open an object in the file by using a pathname where one or more
  components of the path on the way to the object in question are soft links.
  For example, trying to open a dataset by the pathname
  `/group/subgroup/soft_link_to_dataset`
  should work. However, trying to open a dataset using a pathname like
  `/group/soft_link_to_group/soft_link_to_dataset` will generally fail.

+ Due to a simple `basename` function implementation which follows the GNU
  behavior, using a trailing `/` on path names will likely confuse the plugin
  and cause incorrect behavior.

+ The use of point selections for dataset writes will generally incur an additional
  memory overhead of approximately 4/3 the size of the original buffer used for
  the `H5Dwrite` call. This is due to the fact that a temporary copy of the buffer
  must be made and then base64-encoded for the server transfer and base64-encoding
  generally imposes a 33% overhead.

+ Due to the HDF5 public API call `H5Pset_external` using the `off_t` type, it is
  possible that compilation of the REST VOL plugin on non-POSIX-compliant systems
  may fail.

--------------------------------------------------------------------------------

# V. More Information

+ RESTful HDF5 - A description of the HDF5 REST API
    + https://support.hdfgroup.org/pubs/papers/RESTful_HDF5.pdf

+ HDF5-JSON - A specification of and tools for representing HDF5 in JSON
    + http://hdf5-json.readthedocs.io/en/latest/

+ HDF Server (h5serv) - A python-based implementation of the HDF5 REST API which
  can send and receive HDF5 data through the use of HTTP requests
    + https://github.com/HDFGroup/h5serv
    + https://support.hdfgroup.org/projects/hdfserver/
    + https://s3.amazonaws.com/hdfgroup/docs/HDFServer_SciPy2015.pdf

+ HSDS/HDF in the Cloud
    + https://www.hdfgroup.org/solutions/hdf-cloud
    + https://www.slideshare.net/HDFEOS/hdf-cloud-services

