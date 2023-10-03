# HDF5 REST VOL connector <!-- omit in toc -->

HDF5 REST VOL connector - currently under development

[![build status](https://img.shields.io/github/actions/workflow/status/HDFGroup/vol-rest/main.yml?branch=master&label=build%20and%20test)](https://github.com/HDFGroup/vol-rest/actions?query=branch%3Amaster)

### Table of Contents: <!-- omit in toc -->

- [I. Introduction](#i-introduction)
- [II. Installation](#ii-installation)
  - [II.A. Prerequisites](#iia-prerequisites)
    - [II.A.i. External Libraries](#iiai-external-libraries)
    - [II.A.ii. HDF5 REST API server access](#iiaii-hdf5-rest-api-server-access)
  - [II.B. Building the REST VOL connector](#iib-building-the-rest-vol-connector)
    - [II.B.i. Obtaining the Source](#iibi-obtaining-the-source)
    - [II.B.ii. One-step Build](#iibii-one-step-build)
      - [II.B.ii.a. Build Script Options](#iibiia-build-script-options)
    - [II.B.iii. Manual Build](#iibiii-manual-build)
      - [Autotools](#autotools)
      - [CMake](#cmake)
      - [II.B.iii.a. Options for `configure`](#iibiiia-options-for-configure)
      - [II.B.iii.b. Options for CMake](#iibiiib-options-for-cmake)
    - [II.B.iv. Building at HDF5 Build Time](#iibiv-building-at-hdf5-build-time)
    - [II.B.v. Build Results](#iibv-build-results)
- [III. Using/Testing the REST VOL connector](#iii-usingtesting-the-rest-vol-connector)
- [IV. More Information](#iv-more-information)

# I. Introduction

The HDF5 REST VOL connector is a plugin for HDF5 designed with the goal of
allowing HDF5 applications to utilize web-based storage systems by translating
HDF5 API calls into HTTP-based REST calls, as defined by the HDF5 REST API
(See section V. for more information on RESTful HDF5).

Using a VOL connector allows an existing HDF5 application to interface with
different storage systems with minimal changes necessary. The connector accomplishes
this by utilizing the HDF5 Virtual Object Layer in order to re-route HDF5's
public API calls to specific callbacks in the connector which handle all of the
usual HDF5 operations. The HDF5 Virtual Object Layer is an abstraction layer
that sits directly between HDF5's public API and the underlying storage system.
In this manner of operation, the mental data model of an HDF5 application can
be preserved and transparently mapped onto storage systems that differ from a
native filesystem, such as Amazon's S3.

The REST VOL connector is under development, and details given here may change.

--------------------------------------------------------------------------------

# II. Installation

Notes and instructions related to obtaining, building and installing the REST VOL
connector.

## II.A. Prerequisites

Before building and using the HDF5 REST VOL connector, a few requirements must be met.

### II.A.i. External Libraries

To build the REST VOL connector, the following libraries are required:

+ libhdf5 - The [HDF5](https://www.hdfgroup.org/downloads/hdf5/) library. The HDF5 library
            used must be at least version 1.12.0; for convenience, a source distribution of
            HDF5 has been included with the REST VOL connector and can be used during the
            build process. If a pre-built HDF5 distribution is being used instead, it should
            be built as a shared library only for maximal compatibility with the REST VOL
            connector. Using statically-built HDF5 libraries can cause issues with the REST
            VOL connector under certain circumstances.

+ libcurl (ver. 7.61.0 or greater) - networking support
    + https://curl.haxx.se/

+ libyajl (ver. 2.0.4 or greater) - JSON parsing and construction
    + https://lloyd.github.io/yajl/

Compiled libraries must either exist in the system's library paths or must be
pointed to during the REST VOL connector build process. Refer to section II.B.ii.
below for more information.


### II.A.ii. HDF5 REST API server access

Additionally, the HDF5 REST VOL connector requires access to a server which implements
the HDF5 REST API.

For more information on The HDF Group's officially supported service, please see
https://www.hdfgroup.org/hdf-kita.


## II.B. Building the REST VOL connector

### II.B.i. Obtaining the Source

The latest and most up-to-date REST VOL connector code can be viewed at:

https://github.com/HDFGroup/vol-rest

and can directly be obtained from:

`git clone https://github.com/HDFGroup/vol-rest`

For building with the 1.12 or later version of the HDF5 library, use the hdf5_1_12_update branch of this repository. 

A source distribution of the HDF5 library has been included in the REST VOL connector
source in the `/src/hdf5` directory.

### II.B.ii. One-step Build

Use one of the supplied Autotools or CMake build scripts, depending on preference or
system support.

+ Autotools

    Run `build_vol_autotools.sh`. See section II.B.ii.a for configuration options.


+ CMake

    Run `build_vol_cmake.sh` (Linux or OS X) or `build_vol_cmake.bat` (Windows).
    See section II.B.ii.a for configuration options.


By default, these build scripts will compile and link with the provided HDF5 source
distribution. However, if you wish to use a manually built version of the HDF5 library,
include the flag `-H <dir>` where `dir` is the path to the HDF5 install prefix.

NOTE: For those who are capable of using both build systems, the autotools build currently
does not support out-of-tree builds. If the REST VOL source directory is used for an autotools
build, it is important not to reuse the source directory for a later build using CMake.
This can cause build conflicts and result in strange and unexpected behavior.


#### II.B.ii.a. Build Script Options

The following configuration options are available to all of the build scripts:

    -h      Prints out a help message indicating script usage and available options.

    -d      Enables debugging information printouts within the REST VOL connector.

    -c      Enables debugging information printouts from cURL within the REST VOL connector.

    -m      Enables memory usage tracking within the REST VOL connector. This option is
            mostly useful in helping to diagnose any possible memory leaks or other
            memory errors within the connector.

    -g      Enables symbolic debugging of the REST VOL code. (Only available for
            `build_vol_autotools.sh`)

    -P DIR  Specifies where the REST VOL connector should be installed. The default
            installation prefix is `rest_vol_build` inside the REST VOL connector source
            root directory.

    -H DIR  Prevents building of the provided HDF5 source. Instead, uses the compiled
            library found at directory `DIR`, where `DIR` is the path used as the
            installation prefix when building HDF5 manually.

    -C DIR  Specifies the top-level directory where cURL is installed. Used if cURL is
            not installed to a system path or used to override

    -Y DIR  Specifies the top-level directory where YAJL is installed. Used if YAJL is
            not installed to a system path or used to override

Additionally, the CMake build scripts have the following configuration options:

    -B DIR  Specifies the directory that CMake should use as the build tree location.
            The default build tree location is `rest_vol_cmake_build_files` inside the
            REST VOL connector source root directory. Note that the REST VOL does not
            support in-source CMake builds.

    -G DIR  Specifies the CMake Generator to use when generating the build files
            for the project. On Unix systems, the default is "Unix Makefiles" and if
            this is not changed, the build script will automatically attempt to build
            the project after generating the Makefiles. If the generator is changed, the
            build script will only generate the build files and the build command to
            build the project will have to be run manually.

### II.B.iii. Manual Build

In general, the process for building the REST VOL connector involves either obtaining a VOL-enabled
HDF5 distribution or building one from source. Then, the REST VOL connector is built using that
HDF5 distribution by including the appropriate header files and linking against the HDF5 library.
If you wish to manually build HDF5 from the included source distribution, first run the following
commands from the root directory of the REST VOL connector source code in order to checkout the git
submodule and then proceed to build HDF5 as normal.

```bash
$ git submodule init
$ git submodule update
```

Once you have a VOL-enabled HDF5 distribution available, follow the instructions below for your
respective build system in order to build the REST VOL connector against the HDF5 distribution.

#### Autotools

```bash
$ cd rest-vol
$ ./autogen.sh
$ ./configure --prefix=INSTALL_DIR --with-hdf5=HDF5_DIR [options]
$ make
$ make check (requires HDF5 REST API server access -- see section II.A.ii.)
$ make install
```

#### CMake

First, create a build directory within the source tree:

```bash
$ cd rest-vol
$ mkdir build
$ cd build
```

Then, if all of the required components (HDF5, cURL and YAJL) are located within the system path,
building the connector should be as simple as running the following two commands to first have CMake
generate the build files to use and then to build the connector. If the required components are
located somewhere other than the system path, refer to section II.B.iii.b. for information on how to
point to their locations.

```bash
$ cmake -DPREBUILT_HDF5_DIR=HDF5_DIR [options] ..
$ make && make install (command may differ depending on platform and cmake generator used)
```

and, optionally, run the following to generate a system package for the REST VOL connector:

```bash
$ cpack
```

The options that can be specified to control the build process are covered in section II.B.iii.b.
Note that by default CMake will generate Unix Makefiles for the build, but other build files can
be generated by specifying the `-G` option for the `cmake` command; 
see [CMake Generators](https://cmake.org/cmake/help/v3.16/manual/cmake-generators.7.html) for more
information.

#### II.B.iii.a. Options for `configure`

When building the REST VOL connector manually using Autotools, the following options are
available to `configure`.

The options in the supplied Autotools build script are mapped to the corresponding options here:

    -h, --help      Prints out a help message indicating script usage and available
                    options.

    --prefix=DIR    Specifies the location for the resulting files. The default location
                    is `rest_vol_build` in the same directory as configure.

    --enable-build-mode=(production|debug)
                    Sets the build mode to be used.
                    Debug - enable debugging printouts within the REST VOL connector.
                    Production - Focus more on optimization.

    --enable-curl-debug
                    Enables debugging information printouts from cURL within the
                    REST VOL connector.

    --enable-mem-tracking
                    Enables memory tracking within the REST VOL connector. This option is
                    mostly useful in helping to diagnose any possible memory leaks or
                    other memory errors within the connector.

    --enable-tests
                    Enables/Disables building of the REST VOL connector tests.

    --enable-examples
                    Enables/Disables building of the REST VOL HDF5 examples.

    --with-hdf5=DIR Used to specify the directory where an HDF5 distribution that uses
                    the VOL layer has already been built. This is to help the REST VOL
                    connector locate the HDF5 header files that it needs to include.

    --with-curl=DIR Used to specify the top-level directory where cURL is installed, if
                    cURL is not installed to a system path.

    --with-yajl=DIR Used to specify the top-level directory where YAJL is installed, if
                    YAJL is not installed to a system path.


#### II.B.iii.b. Options for CMake

When building the REST VOL connector manually using CMake, the following CMake variables are
available for controlling the build process. These can be supplied to the `cmake` command by
prepending them with `-D`. Some of these options may be needed if, for example, the required
components mentioned previously cannot be found within the system path.

CMake-specific options:

  * `CMAKE_INSTALL_PREFIX` - This option controls the install directory that the resulting output files are written to. The default value is `/usr/local`.
  * `CMAKE_BUILD_TYPE` - This option controls the type of build used for the VOL connector. Valid values are Release, Debug, RelWithDebInfo and MinSizeRel; the default build type is RelWithDebInfo.

HDF5-specific options:

  * `HDF5_USE_STATIC_LIBRARIES` - Indicate if the static HDF5 libraries should be used for linking. The default value is `OFF`.

REST VOL Connector-specific options:

  * `PREBUILT_HDF5_DIR` - Specifies a directory which contains a pre-built HDF5 distribution which uses the VOL abstraction layer. By default, the REST VOL connector's CMake build will attempt to build the included HDF5 source distribution, then use that to build the connector itself. However, if a VOL-enabled HDF5 distribution is already available, this option can be set to point to the directory of the HDF5 distribution. In this case, CMake will use that HDF5 distribution to build the REST VOL connector and will not attempt to build HDF5 again.
  * `BUILD_TESTING` - This option is used to enable/disable building of the REST VOL connector's tests. The default value is `ON`.
  * `BUILD_EXAMPLES` - This option is used to enable/disable building of the REST VOL connector's HDF5 examples. The default value is `ON`.
  * `BUILD_SHARED_LIBS` - This option is used to enable/disable building the REST VOL connector's shared library. The default value is `ON`.
  * `BUILD_STATIC_LIBS` - This option is used to enable/disable building the REST VOL connector's static library. The default value is `OFF`.
  * `BUILD_STATIC_EXECS` - This option is used to enable/disable building the REST VOL connector's static executables. The default value is `OFF`.
  * `HDF5_VOL_REST_ENABLE_COVERAGE` - Enables/Disables code coverage for HDF5 REST VOL connector libraries and programs. The default value is `OFF`.
  * `HDF5_VOL_REST_ENABLE_DEBUG` - Enables/Disables debugging printouts within the REST VOL connector. The default value is `OFF`.
  * `HDF5_VOL_REST_ENABLE_EXAMPLES` - Indicate that building of the examples should be enabled. The default value is `ON`.
  * `HDF5_VOL_REST_ENABLE_CURL_DEBUG` - Enables/Disables debugging information printouts from cURL within the REST VOL connector. The default value is `OFF`.
  * `HDF5_VOL_REST_ENABLE_MEM_TRACKING` - Enables/Disables memory tracking within the REST VOL connector. This option is mostly useful in helping to diagnose any possible memory leaks or other memory errors within the connector. The default value is `OFF`.
  * `HDF5_VOL_REST_THREAD_SAFE` - Enables/Disables linking to HDF5 statically compiled with thread safe option. The default value is `OFF`.
  * `YAJL_USE_STATIC_LIBRARIES` - Indicate if the static YAJL libraries should be used for linking. The default value is `OFF`.
  
Note, when setting BUILD_SHARED_LIBS=ON and YAJL_USE_STATIC_LIBRARIES=ON, the static YAJL libraries have be build with the position independent code (PIC) option enabled. In the static YAJL build,
this PIC option has been turned off by default.

### II.B.iv. Building at HDF5 Build Time

It is also possible to build the REST VOL as part of the build process for the HDF5 library, using CMake's FetchContent module. This can be done using a local copy of the REST VOL's source code, or by providing the information for the repository to be automatically cloned from a branch of a Github repository. For full instructions on this process, see [Building and testing HDF5 VOL connectors with CMake FetchContent](https://github.com/HDFGroup/hdf5/blob/develop/doc/cmake-vols-fetchcontent.md).

### II.B.v. Build Results

If the build is successful, the following files will be written into the installation directory:

```
bin/

include/
     rest_vol_config.h - The header file containing the configuration options for the built REST VOL connector
     rest_vol_public.h - The REST VOL connector's public header file to include in HDF5 applications

lib/
    pkgconfig/
        hdf5_vol_rest-<version>.pc - The REST VOL connector pkgconfig file

    libhdf5_vol_rest.a - The REST VOL connector static library
    libhdf5_vol_rest.settings - The REST VOL connector build settings
    libhdf5_vol_rest.so - The REST VOL connector shared library

share/
    cmake/
        hdf5_vol_rest/
            hdf5_vol_rest-config.cmake
            hdf5_vol_rest-config-version.cmake
            hdf5_vol_rest-targets.cmake
            hdf5_vol_rest-targets-<build mode>.cmake
```

If the REST VOL connector was built using one of the included build scripts, all of the usual files
from an HDF5 source build should appear in the respective `bin`, `include`, `lib` and `share`
directories in the install directory. Notable among these is `bin/h5cc`, a special-purpose compiler wrapper script that streamlines the process of building HDF5 applications.


--------------------------------------------------------------------------------

# III. Using/Testing the REST VOL connector

For information on how to use the REST VOL connector with an HDF5 application,
as well as how to test that the connector is functioning properly, please refer
to the REST VOL User's Guide under `docs/users_guide.pdf`.


--------------------------------------------------------------------------------

# IV. More Information

+ HDF in the Cloud
    + https://www.hdfgroup.org/hdf-kita
    + https://www.hdfgroup.org/solutions/hdf-cloud
    + https://www.slideshare.net/HDFEOS/hdf-cloud-services

+ RESTful HDF5 - A description of the HDF5 REST API
    + https://support.hdfgroup.org/pubs/papers/RESTful_HDF5.pdf
    + http://hdf-rest-api.readthedocs.io/en/latest/

+ HDF5-JSON - A specification of and tools for representing HDF5 in JSON
    + http://hdf5-json.readthedocs.io/en/latest/

+ HDF Server (h5serv) - A python-based implementation of the HDF5 REST API which
  can send and receive HDF5 data through the use of HTTP requests
    + https://github.com/HDFGroup/h5serv
    + https://support.hdfgroup.org/projects/hdfserver/
    + https://s3.amazonaws.com/hdfgroup/docs/HDFServer_SciPy2015.pdf
