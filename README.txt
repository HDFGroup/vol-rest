    D. Building the REST VOL plugin against HDF5

        a) Building with autotools

        Included with the REST VOL source code is a script called
        called 'build_vol_autotools.sh', which is meant to do all of the work
        necessary in building HDF5 and then building the REST VOL against HDF5.
        However, if you wish to build the VOL manually, you should first proceed
        with building a version of HDF5 which utilizes the VOL layer, as per the
        instructions in Section C. above. The included HDF5 source distribution
        is one such version of HDF5, but may potentially be out of date. In
        order to ensure the latest source code is being used, please consult
        help@hdfgroup.org. With this available, the build process for the REST
        VOL should follow the familiar

        $ ./autogen.sh

        $ ./configure

        $ make

        method of building software with autotools.


        When building the REST VOL, there are a number of options that configure
        understands to help control the build process. If building using the
        'build_vol_autotools.sh' script, these options are as follows:


        -h        Prints out a help message indicating script usage and
                  available options.

        -d        Enables debugging information printouts within the REST VOL
                  plugin.

        -C        Enables debug information printouts from cURL within the REST
                  VOL plugin.

        -m        Enables memory usage tracking within the REST VOL plugin. This
                  option is mostly used to help diagnose any possible memory
                  leaks or other memory errors.

        -H DIR    Used to specify the directory where an HDF5 distribution that
                  uses the VOL layer has already been built. This is useful to
                  keep from having to rebuild HDF5 each time the
                  'build_vol_autotools.sh' script is invoked.

        -p DIR    Similar to 'configure --prefix', specifies where the REST VOL
                  should be installed to. Default is a directory named
                  'rest_vol_build' inside the source directory.

        -c DIR    Specifies the top-level directory where cURL is installed, if
                  cURL was not installed to a system path.

        -y DIR    Specifies the top-level directory where YAJL is installed, if
                  YAJL was not installed to a system path.

        -t        Build the tools with REST VOL support. Note that this option
                  is experimental and should not be used for the time being.

        After the build process has succeeded, an executable named
        'test_rest_vol' should have been created under the 'bin' directory
        inside the install directory for the REST VOL. This program tests a
        moderate amount of HDF5's public API functionality with the REST VOL and
        should be a good indicator of whether the REST VOL is working correctly
        in conjunction with a running HSDS instance.


III. Using the REST VOL plugin

    A. Building and running an application with the REST VOL plugin

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
    last step is to build the application as one would a normal HDF5
    application, with the additional task of linking against the built REST VOL
    library and its dependencies. In general, this simply involves adding
    '-lrestvol -lcurl -lyajl' to the build command for the application. However,
    this can vary depending on platform, the compiler used and where the
    REST VOL was installed to. 


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
    likely causes issues for the application if not avoided or at least taken
    into account:

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

