HDF5 REST VOL plugin version 1.0.0 currently under development

/********************************************\
|                                            |
|            HDF5 REST VOL plugin            |
|                                            |
\********************************************/

Table of Contents:

I.   Introduction
II.  Obtaining and Building the REST VOL plugin
    A. Prerequisites
    B. Obtaining the REST VOL plugin
    C. Building the included HDF5 distribution
    D. Building the REST VOL plugin against HDF5
        a) Building with autotools
        b) Building with CMake
III. Using the REST VOL plugin
    A. Building and running an application with the REST VOL plugin
IV. Currently unsupported features
V.  More information


I. Introduction

    The REST VOL plugin is a plugin for HDF5 designed with the goal of allowing
    HDF5 applications, both existing and future, to utilize web-based storage
    systems by translating HDF5 API calls into HTTP-based REST calls, as defined
    by the HDF5 REST API (see section IV for more information on RESTful HDF5).

    The plugin accomplishes this by utilizing the HDF5 Virtual Object Layer in
    order to re-route HDF5's public API calls to specific callbacks in the
    plugin which handle all of the usual HDF5 operations. The HDF5 Virtual
    Object Layer is an abstraction layer that sits directly between HDF5's
    public API calls and the underlying storage system. Using a VOL plugin
    allows an existing HDF5 application to interface with different storage
    systems with minimal changes necessary. In this manner, the mental data
    model of an HDF5 application can be preserved and mapped onto storage
    systems that differ from a native filesystem, such as Amazon's S3.


II. Obtaining and Building the REST VOL plugin

    A. Prerequisites

    The REST VOL plugin depends on two external libraries,
    cURL (https://curl.haxx.se/) and YAJL (https://lloyd.github.io/yajl/).

    If these libraries were installed to a system path on your machine through
    the use of a package manager or similar, the REST VOL's build scripts
    should be able to automatically pick up and correctly link in these
    libraries. However, if building one or both from source was necessary, the
    build scripts have options to specify the installed locations for each. See
    Section D. - 'Building the REST VOL plugin against HDF5' below for details.


    B. Obtaining the REST VOL plugin

    The latest and most up-to-date REST VOL code can be obtained from:

    https://bitbucket.hdfgroup.org/users/jhenderson/repos/rest-vol/browse


    C. Building the included HDF5 distribution

    Due to some specialized changes that had to be made in order for the REST
    VOL plugin to work correctly, a modified source distribution of HDF5 has
    been included in the folder 'hdf5' and needs to be built and then used to
    build the plugin itself.

    Building this included distribution should be as simple as running either
    the build_vol.sh script (if using autotools) or , as these should
    automatically handle the build step for HDF5. However, if you wish to
    build the HDF5 distribution manually, please refer to the documents under
    the "release_docs" directory inside the HDF5 source directory.


    D. Building the REST VOL plugin against HDF5

        a) Building with autotools

        Included with the REST VOL source code is a script called
        called 'build_vol.sh', which is meant to do all of the work necessary
        in building HDF5 and then building the REST VOL against HDF5. However,
        if you wish to build the VOL manually, you should first proceed with
        building a version of HDF5 which utilizes the VOL layer, as per the
        instructions in Section C. above, or obtaining . With this available,
        the build process for the REST VOL should follow the familiar

        $ ./autogen.sh

        $ ./configure

        $ make

        method of building software with autotools.


        When building the REST VOL, there are a number of options that configure
        understands to help control the build process. If building using the
        'build_vol.sh' script, these options are as follows:


        -h        Prints out a help message indicating script usage and
                  available options.

        -d        Enables debugging information printouts within the REST VOL
                  plugin.

        -C        Enables debug information printouts from cURL within the REST
                  REST VOL plugin.

        -m        Enables memory usage tracking within the REST VOL plugin. This
                  option is mostly used to help diagnose any possible memory
                  leaks or other memory errors.

        -H DIR    Used to specify the directory where an HDF5 distribution that
                  uses the VOL layer has already been built. This is useful to
                  keep from having to rebuild HDF5 each time the 'build_vol.sh'
                  script is invoked.

        -p DIR    Similar to 'configure --prefix', specifies where the REST VOL
                  should be installed to. Default is a directory named
                  'rest_vol_build' inside the source directory.

        -c DIR    Specifies the top-level directory where cURL is installed, if
                  cURL was not installed to a system path.

        -y DIR    Specifies the top-level directory where YAJL is installed, if
                  YAJL was not installed to a system path.

        -t        Build the tools with REST VOL support. Note that this option
                  is experimental and should not be used for the time being.


        These options are translated by the 'build_vol.sh' script into the
        equivalent options for the configure script and passed to it as follows
        (these are the options to pass to configure if building manually):


        -h, --help    Prints out a help message indicating script usage and
                      available options.

        --enable-build-mode=(production|debug)
                      Sets the build mode to be used. Debug will enable
                      debugging printouts within the REST VOL plugin. Production
                      will focus more on optimization.

        --enable-curl-debug
                      Enables debug information printouts from cURL within the
                      REST VOL plugin.

        --enable-mem-tracking
                      Enables memory usage tracking within the REST VOL plugin.
                      This option is mostly used to help diagnose any possible
                      memory leaks or other memory errors.

        --with-hdf5=DIR
                      Used to specify the directory where an HDF5 distribution
                      that uses the VOL layer has already been built. This is
                      to help the REST VOL locate the HDF5 header files that it
                      needs to include.

        --with-curl=DIR
                      Used to specify the top-level directory where cURL is
                      installed, if cURL was not installed to a system path.

        --with-yajl=DIR
                      Used to specify the top-level directory where YAJL is
                      installed, if YAJL was not installed to a system path.

        As usual with autotools, you can specify the '--prefix' option for
        configure to instruct the build on where to place the resulting files.

        After the build process has succeeded, an executable named
        'test_rest_vol' should have been created under the '/bin' directory
        inside the install directory for the REST VOL. This program tests a
        moderate amount of HDF5's public API functionality with the REST VOL and
        should be a good indicator of whether the REST VOL is working correctly
        in conjunction with a running HSDS instance.

        In the '/include' directory, you should find the 'rest_vol_public.h'
        header file. Any program which will use the REST VOL should include this
        header.

        In the '/lib' directory, you should find the REST VOL library file,
        'librestvol.a' or similar, depending on the build configuration. Any
        program which will use the REST VOL should link against this library at
        build time.


        b) Building with CMake


III. Using the REST VOL plugin

    A. Building and running an application with the REST VOL plugin

    In order for an HDF5 application to use the REST VOL, three function calls
    must be introduced into the application, RVinit(), RVterm() and
    H5Pset_fapl_rest_vol(). The first two functions are for ensuring that the
    REST VOL is properly initialized at the start of the application's execution
    and terminated at the end of the application's execution. The latter
    is used to set up a File Access Property List which will control how HDF5
    deals with the file that is created or opened. See
    https://support.hdfgroup.org/HDF5/Tutor/property.html#fa for more details
    on File Access Property Lists.

    Looking at the source code for the test program, 'test_rest_vol.c', should
    help to give a good idea of how to structure these function calls. In this
    particular case, each test should be thought of as a complete, separate
    program, with the RVinit() call coming before any HDF5 calls are made and
    with the RVinit() call coming after all HDF5 calls have been made. In
    addition, some of the example C programs included with HDF5 distributions
    have been adapted to work with the REST VOL and are included under the
    top-level 'examples' folder. Looking at these will also help guide the usage
    of these three functions.

    Once the application has been instrumented with these function calls, the
    last step is to link against the REST VOL library, as well as its
    dependencies, cURL and YAJL. Generally, this simply involves adding
    '-lrestvol -lcurl -lyajl' to the build command for the application, 

    If the REST VOL was built using autotools, the "bin" directory after
    building should contain the 'h5cc' script, which is useful in resolving
    the includes and linking dependencies when compiling HDF5 applications. 


IV. Currently unsupported features

    Due to a combination of lack of server support and the complexity in
    implementing them, or due to a particular call not making sense from the
    server's perspective, the following HDF5 API calls are currently
    unsupported:

        H5A interface:

        H5Aopen_by_idx
        H5Aget_info_by_idx
        H5Aget_name_by_idx
        H5Aget_storage_size
        H5Adelete_by_idx
        H5Arename
        H5Arename_by_name

        H5D interface:

        H5Dget_offset
        H5Dget_space_status
        H5Dget_storage_size
        H5Dset_extent

        H5F interface:

        H5Fget_obj_count
        H5Fget_obj_ids
        H5Fflush
        H5Fis_accessible
        H5Fmount
        H5Funmount
        H5Fclear_elink_file_cache
        H5Fget_file_image
        H5Fget_free_sections
        H5Fget_freespace
        H5Fget_mdc_config
        H5Fget_mdc_hit_rate
        H5Fget_mdc_size
        H5Fget_filesize
        H5Fget_vfd_handle
        H5Freset_mdc_hit_rate_stats
        H5Fset_mdc_config

        H5G interface:

        H5Gget_info_by_idx

        H5L interface:

        H5Lget_info_by_idx
        H5Lget_name_by_idx
        H5Lget_val_by_idx
        H5Ldelete_by_idx
        H5Lcopy
        H5Lmove

        H5O interface:

        H5Oopen_by_idx
        H5Oopen_by_addr
        H5Oget_info_by_idx
        H5Oincr_refcount
        H5Odecr_refcount
        H5Oexists_by_name
        H5Ovisit
        H5Ovisit_by_name
        H5Ocopy

        H5R interface:

        H5Rget_name
        H5Rget_region/Region references


    In addition to these API calls, the following other features are
    currently unsupported as well:

        Dataset Fill Values
        Dataset Filters
        Virtual Dataset layout
        External Storage for contiguous dataset layout

        Non-predefined integer and floating-point datatypes
        Variable-length, Opaque, Bitfield and Time datatypes
        Character sets other than H5T_CSET_ASCII for string datatypes
        String padding values other than H5T_STR_NULLPAD for
            fixed-length strings
        String padding values other than H5T_STR_NULLTERM for
            variable-length strings (note that variable-length string datatypes
            are currently unsupported by the REST VOL plugin, but a dataset
            can still be created with a variable-length string type)

        Non-regular hyperslab selections
        Non-contiguous hyperslab selections

        User-defined links

        H5Pset_create_intermediate_group property (the plugin will not
        currently create intermediate groups in a path if they do not exist)

    Finally, due to underlying implementation details, the following
    circumstances are known to be problematic for the REST VOL plugin and will
    likely causes issues for the application if not avoided:

        Cyclic links in the file (the plugin currently cannot detect cyclic
        links), which will generally end in infinite recursion and application
        stack issues

        Trying to open an object in the file by using a pathname where one or
        more components of the path on the way to the object in question are
        soft links. For example, trying to open a dataset by the pathname
        "/group/subgroup/soft_link_to_dataset" should work. However, trying to
        open a dataset using a pathname like
        "/group/soft_link_to_group/soft_link_to_dataset" will generally fail.

        Due to a simple 'basename' function implementation which follows the GNU
        behavior, using a trailing "/" on path names will likely confuse the
        plugin and cause incorrect behavior.

        The use of point selections for dataset writes will generally incur an
        additional memory overhead of approximately 4/3 the size of the original
        buffer used for the H5Dwrite() call. This is due to the fact that a
        temporary copy of the buffer must be made and then base64-encoded for
        the server transfer and base64-encoding generally introduces 33%
        overhead.

        Due to the HDF5 public API call H5Pset_external's use of the 'off_t'
        type, it is likely that compilation of the REST VOL on non-posix
        compliant systems will fail.


V. More information

    RESTful HDF5 - A description of the HDF5 REST API
    https://support.hdfgroup.org/pubs/papers/RESTful_HDF5.pdf

    HDF5-JSON - A specification of and tools for representing HDF5 in JSON
    http://hdf5-json.readthedocs.io/en/latest/

    HDF Server (h5serv) - A python-based implementation of the HDF5 REST API
    which can send and receive HDF5 data through the use of HTTP requests
    https://github.com/HDFGroup/h5serv
    https://support.hdfgroup.org/projects/hdfserver/
    https://s3.amazonaws.com/hdfgroup/docs/HDFServer_SciPy2015.pdf

    HSDS/HDF in the Cloud
    https://www.slideshare.net/HDFEOS/hdf-cloud-services

