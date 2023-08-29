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
    - [II.B.i. Building with CMake](#iibi-building-with-cmake)
    - [II.B.ii. Options for CMake](#iibii-options-for-cmake)
    - [II.B.iii. Build Results](#iibiii-build-results)
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
            used must be at least version 1.14.0. The REST VOL connector must be built
            at the same time as the HDF5 library itself, as described in section II.B.
            The HDF5 library must also be built as a shared library, as statically-built 
            HDF5 libraries can cause issues with the REST VOL connector under certain circumstances.

+ libcurl - networking support
    + https://curl.haxx.se/

+ libyajl (ver. 2.0.4 or greater) - JSON parsing and construction
    + https://lloyd.github.io/yajl/

Compiled libraries must either exist in the system's library paths or must be
pointed to during the REST VOL connector build process. Refer to section II.B.ii.
below for more information.

### II.A.ii. HDF5 REST API server access

The HDF5 REST VOL connector requires access to a server which implements
the HDF5 REST API.

For more information on The HDF Group's officially supported service, please see
https://github.com/HDFGroup/hsds and https://www.hdfgroup.org/hdf-kita.

## II.B. Building the REST VOL connector

### II.B.i. Building with CMake 

The latest and most up-to-date REST VOL connector code can be viewed at:

https://github.com/HDFGroup/vol-rest

In order to clone and build the REST VOL connector at the same time as the HDF5 library, follow the steps listed here:

https://github.com/HDFGroup/hdf5/blob/develop/doc/cmake-vols-fetchcontent.md

while providing the following CMake variables to HDF5 at configuration time:

```HDF5_VOL_URL01=https://github.com/HDFGroup/vol-rest
HDF5_VOL_VOL-REST_BRANCH="master"
HDF5_VOL_VOL-REST_NAME="REST"

HDF5_ALLOW_EXTERNAL_SUPPORT="GIT"
HDF5_VOL_ALLOW_EXTERNAL=ON
HDF5_TEST_API=ON 

HDF5_ENABLE_SZIP_SUPPORT=OFF 
HDF5_ENABLE_Z_LIB_SUPPORT=OFF
HDF5_VOL_VOL-REST_TEST_PARALLEL=OFF
```

Note that CMake requires variables to be prefixed with `-D`, such that the command would look like `cmake -DHDF5_VOL_URL01=https://github.com/HDFGroup/vol-rest -DHDF5_VOL_VOL-REST_BRANCH="master" ....`

If the required components (cURL, YAJL) are located somewhere other than the system path, refer to section II.B.ii. for information on how to
point to their locations.

The options that can be specified to control the build process are covered in section II.B.ii.
Note that by default CMake will generate Unix Makefiles for the build, but other build files can
be generated by specifying the `-G` option for the `cmake` command; 
see [CMake Generators](https://cmake.org/cmake/help/v3.16/manual/cmake-generators.7.html) for more
information.

#### II.B.ii. Options for CMake

When building HDF5 and the REST VOL connector with CMake, the following CMake variables are
available for controlling the build process. These can be supplied to the `cmake` command by
prepending them with `-D`. Some of these options may be needed if, for example, the required
components mentioned previously cannot be found within the system path.

CMake-specific options:

  * `CMAKE_INSTALL_PREFIX` - This option controls the install directory that the resulting output files are written to. The default value is `/usr/local`.
  * `CMAKE_BUILD_TYPE` - This option controls the type of build used for HDF5 and the VOL connector. Valid values are Release, Debug, RelWithDebInfo and MinSizeRel; the default build type is RelWithDebInfo.

HDF5 + REST VOL options:

  * `BUILD_TESTING` - This option is used to enable/disable building of HDF5 and the REST VOL connector's tests. The default value is `ON`.
  * `BUILD_SHARED_LIBS` - This option is used to enable/disable building HDF5 and the REST VOL connector's shared library. The default value is `ON`.
  * `BUILD_STATIC_LIBS` - This option is used to enable/disable building HDF5 and the REST VOL connector's static library. The default value is `OFF`.
  * `BUILD_STATIC_EXECS` - This option is used to enable/disable building HDF5 and the REST VOL connector's static executables. The default value is `OFF`.

HDF5-specific options:

  * `HDF5_USE_STATIC_LIBRARIES` - Indicate if the static HDF5 libraries should be used for linking. The default value is `OFF`.

REST VOL Connector-specific options:

  * `HDF5_VOL_REST_ENABLE_COVERAGE` - Enables/Disables code coverage for HDF5 REST VOL connector libraries and programs. The default value is `OFF`.
  * `HDF5_VOL_REST_ENABLE_DEBUG` - Enables/Disables debugging printouts within the REST VOL connector. The default value is `OFF`.
  * `HDF5_VOL_REST_ENABLE_EXAMPLES` - Indicate that building of the examples should be enabled. The default value is `ON`.
  * `HDF5_VOL_REST_ENABLE_CURL_DEBUG` - Enables/Disables debugging information printouts from cURL within the REST VOL connector. The default value is `OFF`.
  * `HDF5_VOL_REST_ENABLE_MEM_TRACKING` - Enables/Disables memory tracking within the REST VOL connector. This option is mostly useful in helping to diagnose any possible memory leaks or other memory errors within the connector. The default value is `OFF`.
  * `HDF5_VOL_REST_THREAD_SAFE` - Enables/Disables linking to HDF5 statically compiled with thread safe option. The default value is `OFF`.
  * `YAJL_USE_STATIC_LIBRARIES` - Indicate if the static YAJL libraries should be used for linking. The default value is `OFF`.
  
Note, when setting BUILD_SHARED_LIBS=ON and YAJL_USE_STATIC_LIBRARIES=ON, the static YAJL libraries have be build with the position independent code (PIC) option enabled. In the static YAJL build, this PIC option has been turned off by default.

### II.B.iii. Build Results

If the build is successful, the following files will be written into the installation directory, in addition to the usual files for an HDF5 source build:

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
        FindYAJL.cmake
        hdf5_vol_rest/
            hdf5_vol_rest-config.cmake
            hdf5_vol_rest-config-version.cmake
            hdf5_vol_rest-targets.cmake
            hdf5_vol_rest-targets-<build mode>.cmake
```

Notable among the files produced by the HDF library build is `bin/h5cc`, a special-purpose compiler wrapper script that streamlines the process of building HDF5 applications.


--------------------------------------------------------------------------------

# III. Using/Testing the REST VOL connector

For information on how to use the REST VOL connector with an HDF5 application,
as well as how to test that the connector is functioning properly, please refer
to the REST VOL User's Guide under `docs/users_guide.pdf`.


--------------------------------------------------------------------------------

# IV. More Information

+ Highly Scalable Data Service (HSDS) - A python-based implementation of the HDF5 REST API which
  can send and receive HDF5 data through the use of HTTP requests
    + https://github.com/HDFGroup/hsds
    + https://portal.hdfgroup.org/display/KITA/Highly+Scalable+Data+Service
    + http://s3.amazonaws.com/hdfgroup/docs/hdf_data_services_scipy2017.pdf

+ HDF in the Cloud
    + https://www.hdfgroup.org/hdf-kita
    + https://www.hdfgroup.org/solutions/hdf-cloud
    + https://www.slideshare.net/HDFEOS/hdf-cloud-services

+ RESTful HDF5 - A description of the HDF5 REST API
    + https://support.hdfgroup.org/pubs/papers/RESTful_HDF5.pdf
    + http://hdf-rest-api.readthedocs.io/en/latest/

+ HDF5-JSON - A specification of and tools for representing HDF5 in JSON
    + http://hdf5-json.readthedocs.io/en/latest/
