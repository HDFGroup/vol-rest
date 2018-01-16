/********************************************\
|                                            |
|            HDF5 REST VOL plugin            |
|                                            |
\********************************************/

Table of Contents:

I.   Introduction
II.  Obtaining and Building the REST VOL plugin
    a. Obtaining HDF5 and the REST VOL plugin
    b. Building the REST VOL plugin against HDF5
III. Using the REST VOL plugin
    a. Building and running an application with the REST VOL plugin
    b. Currently unsupported features
IV.  More information


I. Introduction

    The REST VOL plugin is a plugin for HDF5 designed with the goal of allowing
    HDF5 applications, both existing and future, to utilize web-based storage
    systems by translating HDF5 API calls into HTTP-based REST calls, as defined
    by the HDF5 REST API (see section IV for more information on RESTful HDF5).

    The HDF5 Virtual Object Layer is an abstraction layer that sits directly
    between HDF5's public API calls and the underlying storage system. Using
    a VOL plugin allows an existing HDF5 application to interface with
    different storage systems with minimal changes necessary. In this manner,
    the mental data model of an HDF5 application can be preserved and mapped
    onto , such as Amazon S3 in this particular case.


II. Obtaining and Building the REST VOL plugin

    a. Obtaining the REST VOL plugin

    b. Building the REST VOL plugin against HDF5

III. Using the REST VOL plugin

    a. Building and running an application with the REST VOL plugin

    

    b. Currently unsupported features

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

        Due to the algorithm used to process the JSON fields of a compound
        datatype and turn them each into an HDF5 hid_t, including a '{' or '}'
        symbol inside the name of a compound datatype member will confuse the
        plugin and lead to incorrect behavior.

        Due to the HDF5 public API call H5Pset_external's use of the 'off_t'
        type, it is likely that compilation of the REST VOL on non-posix
        compliant systems will fail.


IV. More information

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