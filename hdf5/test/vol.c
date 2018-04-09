/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the root of the source code       *
 * distribution tree, or in https://support.hdfgroup.org/ftp/HDF5/releases.  *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * Purpose:	Tests the virtual object layer (H5VL)
 *
 *          This is a minimal test to ensure VOL usage (setting a VOL, etc.)
 *          works as expected. Actual VOL functionality is tested using
 *          other mechanisms.
 */

#include "h5test.h"
#include "H5VLnative.h"


#define NATIVE_VOL_TEST_FILENAME        "native_vol_test"
#define NATIVE_VOL_TEST_GROUP_NAME      "test_group"
#define NATIVE_VOL_TEST_DATASET_NAME    "test_dataset"
#define NATIVE_VOL_TEST_ATTRIBUTE_NAME  "test_dataset"
#define NATIVE_VOL_TEST_HARD_LINK_NAME  "test_hard_link"
#define NATIVE_VOL_TEST_SOFT_LINK_NAME  "test_soft_link"
#define NATIVE_VOL_TEST_MOVE_LINK_NAME  "test_move_link"
#define NATIVE_VOL_TEST_COPY_LINK_NAME  "test_copy_link"
#define NATIVE_VOL_TEST_DATATYPE_NAME   "test_datatype"

#define N_ELEMENTS  10

#define FAKE_VOL_NAME   "fake"

/* A VOL class struct that describes a VOL class with no
 * functionality.
 */
static const H5VL_class_t fake_vol_g = {
    0,                                              /* version      */
    (H5VL_class_value_t)999,                        /* value        */
    FAKE_VOL_NAME,                                  /* name         */
    NULL,                                           /* initialize   */
    NULL,                                           /* terminate    */
    (size_t)0,                                      /* fapl size    */
    NULL,                                           /* fapl copy    */
    NULL,                                           /* fapl free    */
    {   /* attribute_cls */
        NULL,                                       /* create       */
        NULL,                                       /* open         */
        NULL,                                       /* read         */
        NULL,                                       /* write        */
        NULL,                                       /* get          */
        NULL,                                       /* specific     */
        NULL,                                       /* optional     */
        NULL                                        /* close        */
    },
    {   /* dataset_cls */
        NULL,                                       /* create       */
        NULL,                                       /* open         */
        NULL,                                       /* read         */
        NULL,                                       /* write        */
        NULL,                                       /* get          */
        NULL,                                       /* specific     */
        NULL,                                       /* optional     */
        NULL                                        /* close        */
    },
    {   /* datatype_cls */
        NULL,                                       /* commit       */
        NULL,                                       /* open         */
        NULL,                                       /* get_size     */
        NULL,                                       /* specific     */
        NULL,                                       /* optional     */
        NULL                                        /* close        */
    },
    {   /* file_cls */
        NULL,                                       /* create       */
        NULL,                                       /* open         */
        NULL,                                       /* get          */
        NULL,                                       /* specific     */
        NULL,                                       /* optional     */
        NULL                                        /* close        */
    },
    {   /* group_cls */
        NULL,                                       /* create       */
        NULL,                                       /* open         */
        NULL,                                       /* get          */
        NULL,                                       /* specific     */
        NULL,                                       /* optional     */
        NULL                                        /* close        */
    },
    {   /* link_cls */
        NULL,                                       /* create       */
        NULL,                                       /* copy         */
        NULL,                                       /* move         */
        NULL,                                       /* get          */
        NULL,                                       /* specific     */
        NULL                                        /* optional     */
    },
    {   /* object_cls */
        NULL,                                       /* open         */
        NULL,                                       /* copy         */
        NULL,                                       /* get          */
        NULL,                                       /* specific     */
        NULL                                        /* optional     */
    },
    {   /* async_cls */
        NULL,                                       /* cancel       */
        NULL,                                       /* test         */
        NULL                                        /* wait         */
    },
    NULL                                            /* optional     */
};


/*-------------------------------------------------------------------------
 * Function:    test_vol_registration()
 *
 * Purpose:     Tests if we can load, register, and close a simple
 *              VOL driver.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
test_vol_registration(void)
{
    htri_t is_registered;
    hid_t vol_id = -1;

    TESTING("VOL registration");

    /* The test/fake VOL driver should not be registered at the start of the test */
    if ((is_registered = H5VLis_registered(FAKE_VOL_NAME)) < 0)
        FAIL_STACK_ERROR;
    if (is_registered > 0)
        FAIL_PUTS_ERROR("native VOL driver is inappropriately registered");

    /* Load a VOL interface */
    if ((vol_id = H5VLregister(&fake_vol_g)) < 0)
        FAIL_STACK_ERROR;

    /* The test/fake VOL driver should be registered now */
    if ((is_registered = H5VLis_registered(FAKE_VOL_NAME)) < 0)
        FAIL_STACK_ERROR;
    if (0 == is_registered)
        FAIL_PUTS_ERROR("native VOL driver is un-registered");

    /* Close the VOL interface */
    if (H5VLclose(vol_id) < 0)
        FAIL_STACK_ERROR;

    PASSED();
    return SUCCEED;

error:
    H5E_BEGIN_TRY {
        H5VLclose(vol_id);
    } H5E_END_TRY;
    return FAIL;

} /* end test_vol_registration() */


/*-------------------------------------------------------------------------
 * Function:    test_native_vol_init()
 *
 * Purpose:     Tests if the native VOL driver gets initialized.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
test_native_vol_init(void)
{
    htri_t is_registered;

    TESTING("Native VOL driver initialization");

    /* The native VOL driver should always be registered */
    if ((is_registered = H5VLis_registered(H5VL_NATIVE_NAME)) < 0)
        FAIL_STACK_ERROR;
    if (0 == is_registered)
        FAIL_PUTS_ERROR("native VOL driver is un-registered");

    PASSED();
    return SUCCEED;

error:
    return FAIL;

} /* end test_native_vol_init() */


/*-------------------------------------------------------------------------
 * Function:    test_basic_file_operation()
 *
 * Purpose:     Uses the native VOL driver to test basic VOL file operations
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
test_basic_file_operation(void)
{
    hid_t fid           = H5I_INVALID_HID;
    hid_t fid_reopen    = H5I_INVALID_HID;
    hid_t fapl_id       = H5I_INVALID_HID;
    hid_t fcpl_id       = H5I_INVALID_HID;

    ssize_t         obj_count;
    hid_t           obj_id_list[1];
    hsize_t         file_size;
    unsigned        intent;
    void           *os_file_handle = NULL;
    H5F_info2_t     finfo;
    char            name[32];

    TESTING("Basic VOL file operations");

    /* H5Fcreate */
    if ((fid = H5Fcreate(NATIVE_VOL_TEST_FILENAME, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT)) < 0)
        TEST_ERROR;

    /* H5Fget_obj_count */
    if ((obj_count = H5Fget_obj_count(fid, H5F_OBJ_FILE)) < 0)
        TEST_ERROR;
    if ((obj_count = H5Fget_obj_count(fid, H5F_OBJ_ALL)) < 0)
        TEST_ERROR;
    if ((obj_count = H5Fget_obj_count((hid_t)H5F_OBJ_ALL, H5F_OBJ_DATASET)) < 0)
        TEST_ERROR;

    /* H5Fget_obj_ids */
    if ((obj_count = H5Fget_obj_ids(fid, H5F_OBJ_ALL, 2, obj_id_list)) < 0)
        TEST_ERROR;
    if ((obj_count = H5Fget_obj_ids((hid_t)H5F_OBJ_ALL, H5F_OBJ_DATASET, 2, obj_id_list)) < 0)
        TEST_ERROR;

    /* H5Fget_access_plist */
    if ((fapl_id = H5Fget_access_plist(fid)) < 0)
        TEST_ERROR;
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR;

    /* H5Fget_create_plist */
    if ((fcpl_id = H5Fget_create_plist(fid)) < 0)
        TEST_ERROR;
    if (H5Pclose(fcpl_id) < 0)
        TEST_ERROR;

    /* H5Fget_filesize */
    if (H5Fget_filesize(fid, &file_size) < 0)
        TEST_ERROR;

    /* H5Fget_vfd_handle */
    if (H5Fget_vfd_handle(fid, H5P_DEFAULT, &os_file_handle) < 0)
        TEST_ERROR;

    /* H5Fget_intent */
    if (H5Fget_intent(fid, &intent) < 0)
        TEST_ERROR;

    /* H5Fget_info2 */
    if (H5Fget_info2(fid, &finfo) < 0)
        TEST_ERROR;

    /* H5Fget_name */
    if (H5Fget_name(fid, name, 32) < 0)
        TEST_ERROR;

    /* H5Fclear_elink_file_cache */
    if (H5Fclear_elink_file_cache(fid) < 0)
        TEST_ERROR;

    /* H5Fflush */
    if (H5Fflush(fid, H5F_SCOPE_GLOBAL) < 0)
        TEST_ERROR;

    /* H5Fclose */
    if (H5Fclose(fid) < 0)
        TEST_ERROR;

    /* H5Fis_hdf5 */
    if (H5Fis_hdf5(NATIVE_VOL_TEST_FILENAME) < 0)
        TEST_ERROR;

    /* H5Fis_accessible */
    if (H5Fis_accessible(NATIVE_VOL_TEST_FILENAME, H5P_DEFAULT) < 0)
        TEST_ERROR;

    /* H5Fopen */
    if ((fid = H5Fopen(NATIVE_VOL_TEST_FILENAME, H5F_ACC_RDWR, H5P_DEFAULT)) < 0)
        TEST_ERROR;
    if ((fid_reopen = H5Freopen(fid)) < 0)
        TEST_ERROR;
    if (H5Fclose(fid) < 0)
        TEST_ERROR;
    if (H5Fclose(fid_reopen) < 0)
        TEST_ERROR;

    HDremove(NATIVE_VOL_TEST_FILENAME);

    PASSED();
    return SUCCEED;

error:
    H5E_BEGIN_TRY {
        H5Fclose(fid);
        H5Fclose(fid_reopen);
        H5Pclose(fapl_id);
        H5Pclose(fcpl_id);
    } H5E_END_TRY;

    return FAIL;

} /* end test_basic_file_operation() */


/*-------------------------------------------------------------------------
 * Function:    test_basic_group_operation()
 *
 * Purpose:     Uses the native VOL driver to test basic VOL group operations
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
test_basic_group_operation(void)
{
    hid_t fid = H5I_INVALID_HID;
    hid_t gid = H5I_INVALID_HID;
    hid_t gid_a = H5I_INVALID_HID;
    hid_t gcpl_id = H5I_INVALID_HID;
    H5G_info_t info;

    TESTING("Basic VOL group operations");

    if ((fid = H5Fcreate(NATIVE_VOL_TEST_FILENAME, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT)) < 0)
        TEST_ERROR;

    /* H5Gcreate */
    if ((gid = H5Gcreate2(fid, NATIVE_VOL_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0)
        TEST_ERROR;

    /* H5Gget_create_plist */
    if ((gcpl_id = H5Gget_create_plist(gid)) < 0)
        TEST_ERROR;
    if (H5Pclose(gcpl_id) < 0)
        TEST_ERROR;

    /* H5Gget_info */
    if (H5Gget_info(gid, &info) < 0)
        TEST_ERROR;
    if (H5Gget_info(fid, &info) < 0)
        TEST_ERROR;

    /* H5Gget_info_by_name */
    if (H5Gget_info_by_name(fid, NATIVE_VOL_TEST_GROUP_NAME, &info, H5P_DEFAULT) < 0)
        TEST_ERROR;

    /* H5Gget_info_by_idx */
    if (H5Gget_info_by_idx(fid, "/", H5_INDEX_NAME, H5_ITER_NATIVE, 0, &info, H5P_DEFAULT) < 0)
        TEST_ERROR;

    /* H5Gflush */
    if (H5Gflush(gid) < 0)
        TEST_ERROR;

    /* H5Gclose */
    if (H5Gclose(gid) < 0)
        TEST_ERROR;

    /* H5Gopen */
    if ((gid = H5Gopen2(fid, NATIVE_VOL_TEST_GROUP_NAME, H5P_DEFAULT)) < 0)
        TEST_ERROR;

    /* H5Gcreate_anon */
    if ((gid_a = H5Gcreate_anon(fid, H5P_DEFAULT, H5P_DEFAULT)) < 0)
        TEST_ERROR;

    /* H5Grefresh */
    if (H5Grefresh(gid) < 0)
        TEST_ERROR;

    if (H5Gclose(gid) < 0)
        TEST_ERROR;
    if (H5Gclose(gid_a) < 0)
        TEST_ERROR;
    if (H5Fclose(fid) < 0)
        TEST_ERROR;

    HDremove(NATIVE_VOL_TEST_FILENAME);

    PASSED();
    return SUCCEED;

error:
    H5E_BEGIN_TRY {
        H5Fclose(fid);
        H5Gclose(gid);
        H5Pclose(gcpl_id);
    } H5E_END_TRY;

    return FAIL;

} /* end test_basic_group_operation() */


/*-------------------------------------------------------------------------
 * Function:    test_basic_dataset_operation()
 *
 * Purpose:     Uses the native VOL driver to test basic VOL dataset operations
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
test_basic_dataset_operation(void)
{
    hid_t fid       = H5I_INVALID_HID;
    hid_t dcpl_id   = H5I_INVALID_HID;
    hid_t dapl_id   = H5I_INVALID_HID;
    hid_t did       = H5I_INVALID_HID;
    hid_t did_a     = H5I_INVALID_HID;
    hid_t sid       = H5I_INVALID_HID;
    hid_t tid       = H5I_INVALID_HID;

    hsize_t curr_dims   = 0;
    hsize_t max_dims    = H5S_UNLIMITED;

    hsize_t storage_size;
    haddr_t offset;
    H5D_space_status_t status;

    int in_buf[N_ELEMENTS];
    int out_buf[N_ELEMENTS];

    int i;

    TESTING("Basic VOL dataset operations");

    if ((fid = H5Fcreate(NATIVE_VOL_TEST_FILENAME, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT)) < 0)
        TEST_ERROR;
    for (i = 0; i < N_ELEMENTS; i++) {
        in_buf[i] = i;
        out_buf[i] = 0;
    }

    /* H5Dcreate */
    curr_dims = 0;
    if ((sid = H5Screate_simple(1, &curr_dims, &max_dims)) < 0)
        TEST_ERROR;
    curr_dims = N_ELEMENTS;
    if ((dcpl_id = H5Pcreate(H5P_DATASET_CREATE)) < 0)
        TEST_ERROR;
    if (H5Pset_chunk(dcpl_id, 1, &curr_dims) < 0)
        TEST_ERROR;
    if ((did = H5Dcreate2(fid, NATIVE_VOL_TEST_DATASET_NAME, H5T_NATIVE_INT, sid, H5P_DEFAULT, dcpl_id, H5P_DEFAULT)) < 0)
        TEST_ERROR;

    /* H5Dcreate_anon */
    if ((did_a = H5Dcreate_anon(fid, H5T_NATIVE_INT, sid, dcpl_id, H5P_DEFAULT)) < 0)
        TEST_ERROR;

    if (H5Sclose(sid) < 0)
        TEST_ERROR;
    if (H5Pclose(dcpl_id) < 0)
        TEST_ERROR;

    /* H5Dset_extent */
    curr_dims = N_ELEMENTS;
    if (H5Dset_extent(did, &curr_dims) < 0)
        TEST_ERROR;

    /* H5Dflush */
    if (H5Dflush(did) < 0)
        TEST_ERROR;

    /* H5Dwrite */
    if (H5Dwrite(did, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, in_buf) < 0)
        TEST_ERROR;

    /* H5Drefresh */
    if (H5Drefresh(did) < 0)
        TEST_ERROR;

    /* H5Dclose */
    if (H5Dclose(did) < 0)
        TEST_ERROR;
    if (H5Dclose(did_a) < 0)
        TEST_ERROR;

    /* H5Dopen */
    if ((did = H5Dopen2(fid, NATIVE_VOL_TEST_DATASET_NAME, H5P_DEFAULT)) < 0)
        TEST_ERROR;

    /* H5Dget_space */
    if ((sid = H5Dget_space(did)) < 0)
        TEST_ERROR;
    if (H5Sclose(sid) < 0)
        TEST_ERROR;

    /* H5Dget_space_status */
    if (H5Dget_space_status(did, &status) < 0)
        TEST_ERROR;

    /* H5Dget_type */
    if ((tid = H5Dget_type(did)) < 0)
        TEST_ERROR;
    if (H5Tclose(tid) < 0)
        TEST_ERROR;

    /* H5Tcopy (when used w/ a dataset, it gets an H5VL struct */
    if ((tid = H5Tcopy(did)) < 0)
        TEST_ERROR;
    if (H5Tclose(tid) < 0)
        TEST_ERROR;

    /* H5Dget_create_plist */
    if ((dcpl_id = H5Dget_create_plist(did)) < 0)
        TEST_ERROR;
    if (H5Pclose(dcpl_id) < 0)
        TEST_ERROR;

    /* H5Dget_access_plist */
    if ((dapl_id = H5Dget_access_plist(did)) < 0)
        TEST_ERROR;
    if (H5Pclose(dapl_id) < 0)
        TEST_ERROR;

    /* H5Dget_storage_size */
    /* XXX: This is a terrible API call that can't truly indicate failure */
    if (0 == (storage_size = H5Dget_storage_size(did)))
        TEST_ERROR;

    /* H5Dget_offset */
    /* XXX: Another bad API call that can't flag error values. Also, this
     *      returns HADDR_UNDEF for chunked datasets, which is bizarre.
     */
    if (HADDR_UNDEF != (offset = H5Dget_offset(did)))
        TEST_ERROR;

    /* H5Dread */
    if (H5Dread(did, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, out_buf) < 0)
        TEST_ERROR;

    for (i = 0; i < N_ELEMENTS; i++)
        if (in_buf[i] != out_buf[i])
            TEST_ERROR;

    if (H5Dclose(did) < 0)
        TEST_ERROR;
    if (H5Fclose(fid) < 0)
        TEST_ERROR;

    HDremove(NATIVE_VOL_TEST_FILENAME);

    PASSED();
    return SUCCEED;

error:
    H5E_BEGIN_TRY {
        H5Fclose(fid);
        H5Dclose(did);
        H5Dclose(did_a);
        H5Sclose(sid);
        H5Tclose(tid);
        H5Pclose(dapl_id);
        H5Pclose(dcpl_id);
    } H5E_END_TRY;

    return FAIL;

} /* end test_basic_dataset_operation() */


/*-------------------------------------------------------------------------
 * Function:    test_basic_attribute_operation()
 *
 * Purpose:     Uses the native VOL driver to test basic VOL attribute operations
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
test_basic_attribute_operation(void)
{
    hid_t fid       = H5I_INVALID_HID;
    hid_t gid       = H5I_INVALID_HID;
    hid_t aid       = H5I_INVALID_HID;
    hid_t aid_name  = H5I_INVALID_HID;
    hid_t sid       = H5I_INVALID_HID;

    hsize_t dims    = 1;

    int     data_in     = 42;
    int     data_out    = 0;

    TESTING("Basic VOL attribute operations");

    if ((fid = H5Fcreate(NATIVE_VOL_TEST_FILENAME, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT)) < 0)
        TEST_ERROR;
    if ((gid = H5Gcreate2(fid, NATIVE_VOL_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0)
        TEST_ERROR;
    dims = 1;
    if ((sid = H5Screate_simple(1, &dims, &dims)) < 0)
        TEST_ERROR;

    /* H5Acreate */
    if ((aid = H5Acreate2(fid, NATIVE_VOL_TEST_ATTRIBUTE_NAME, H5T_NATIVE_INT, sid, H5P_DEFAULT, H5P_DEFAULT)) < 0)
        TEST_ERROR;

    /* H5Awrite */
    if (H5Awrite(aid, H5T_NATIVE_INT, &data_in) < 0)
        TEST_ERROR;

    /* H5Aread */
    if (H5Aread(aid, H5T_NATIVE_INT, &data_out) < 0)
        TEST_ERROR;
    if (data_in != data_out)
        TEST_ERROR;

    /* H5Aclose */
    if (H5Aclose(aid) < 0)
        TEST_ERROR;

    /* H5Aopen */
    if ((aid = H5Aopen(fid, NATIVE_VOL_TEST_ATTRIBUTE_NAME, H5P_DEFAULT)) < 0)
        TEST_ERROR;
    if (H5Aclose(aid) < 0)
        TEST_ERROR;

    /* H5Adelete */
    if (H5Adelete(fid, NATIVE_VOL_TEST_ATTRIBUTE_NAME) < 0)
        TEST_ERROR;

    /* H5Acreate_by_name */
    if ((aid_name = H5Acreate_by_name(fid, NATIVE_VOL_TEST_GROUP_NAME, NATIVE_VOL_TEST_ATTRIBUTE_NAME, H5T_NATIVE_INT, sid, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0)
        TEST_ERROR;
    /* H5Aclose */
    if (H5Aclose(aid_name) < 0)
        TEST_ERROR;

    /* H5Adelete_by_name */
    if (H5Adelete_by_name(fid, NATIVE_VOL_TEST_GROUP_NAME, NATIVE_VOL_TEST_ATTRIBUTE_NAME, H5P_DEFAULT) < 0)
        TEST_ERROR;

    if (H5Sclose(sid) < 0)
        TEST_ERROR;
    if (H5Gclose(gid) < 0)
        TEST_ERROR;
    if (H5Fclose(fid) < 0)
        TEST_ERROR;

    HDremove(NATIVE_VOL_TEST_FILENAME);

    PASSED();
    return SUCCEED;

error:
    H5E_BEGIN_TRY {
        H5Fclose(fid);
        H5Gclose(gid);
        H5Sclose(sid);
        H5Aclose(aid);
        H5Aclose(aid_name);
    } H5E_END_TRY;

    return FAIL;

} /* end test_basic_attribute_operation() */


/*-------------------------------------------------------------------------
 * Function:    test_basic_object_operation()
 *
 * Purpose:     Uses the native VOL driver to test basic VOL object operations
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
test_basic_object_operation(void)
{
    hid_t fid       = H5I_INVALID_HID;
    hid_t gid       = H5I_INVALID_HID;
    hid_t oid       = H5I_INVALID_HID;

    H5O_info_t object_info;

    TESTING("Basic VOL object operations");

    if ((fid = H5Fcreate(NATIVE_VOL_TEST_FILENAME, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT)) < 0)
        TEST_ERROR;
    if ((gid = H5Gcreate2(fid, NATIVE_VOL_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0)
        TEST_ERROR;

    /* H5Oget_info */
    if (H5Oget_info(fid, &object_info) < 0)
        TEST_ERROR;

    /* H5Oget_info_by_name */
    if (H5Oget_info_by_name(fid, NATIVE_VOL_TEST_GROUP_NAME, &object_info, H5P_DEFAULT) < 0)
        TEST_ERROR;

    /* H5Oexists_by_name */
    if (H5Oexists_by_name(fid, NATIVE_VOL_TEST_GROUP_NAME, H5P_DEFAULT) != TRUE) 
        TEST_ERROR;

    /* H5Oopen/close */
    if ((oid = H5Oopen(fid, NATIVE_VOL_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) 
        TEST_ERROR;
    if (H5Oclose(oid) < 0)
        TEST_ERROR;

    if (H5Fclose(fid) < 0)
        TEST_ERROR;
    if (H5Gclose(gid) < 0)
        TEST_ERROR;

    HDremove(NATIVE_VOL_TEST_FILENAME);

    PASSED();
    return SUCCEED;

error:
    H5E_BEGIN_TRY {
        H5Fclose(fid);
        H5Gclose(gid);
    } H5E_END_TRY;

    return FAIL;

} /* end test_basic_object_operation() */


/*-------------------------------------------------------------------------
 * Function:    test_basic_link_operation()
 *
 * Purpose:     Uses the native VOL driver to test basic VOL link operations
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
test_basic_link_operation(void)
{
    hid_t fid       = H5I_INVALID_HID;
    hid_t gid       = H5I_INVALID_HID;

    TESTING("Basic VOL link operations");

    if ((fid = H5Fcreate(NATIVE_VOL_TEST_FILENAME, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT)) < 0)
        TEST_ERROR;
    if ((gid = H5Gcreate2(fid, NATIVE_VOL_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0)
        TEST_ERROR;

    /* H5Lcreate_hard */
    if (H5Lcreate_hard(fid, "/", gid, NATIVE_VOL_TEST_HARD_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0)
        TEST_ERROR;

    /* H5Lcreate_soft (to itself) */
    if (H5Lcreate_soft("/", fid, NATIVE_VOL_TEST_SOFT_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0)
        TEST_ERROR;

    /* H5Lexists */
    if (H5Lexists(gid, NATIVE_VOL_TEST_HARD_LINK_NAME, H5P_DEFAULT) < 0)
        TEST_ERROR;
    if (H5Lexists(fid, NATIVE_VOL_TEST_SOFT_LINK_NAME, H5P_DEFAULT) < 0)
        TEST_ERROR;

    /* H5Lcopy */
    if (H5Lcopy(gid, NATIVE_VOL_TEST_HARD_LINK_NAME, fid, NATIVE_VOL_TEST_COPY_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0)
        TEST_ERROR;

    /* H5Lmove */
    if (H5Lmove(fid, NATIVE_VOL_TEST_COPY_LINK_NAME, gid, NATIVE_VOL_TEST_MOVE_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0)
        TEST_ERROR;

    if (H5Fclose(fid) < 0)
        TEST_ERROR;
    if (H5Gclose(gid) < 0)
        TEST_ERROR;

    HDremove(NATIVE_VOL_TEST_FILENAME);

    PASSED();
    return SUCCEED;

error:
    H5E_BEGIN_TRY {
        H5Fclose(fid);
        H5Fclose(gid);
    } H5E_END_TRY;

    return FAIL;

} /* end test_basic_link_operation() */


/*-------------------------------------------------------------------------
 * Function:    test_basic_datatype_operation()
 *
 * Purpose:     Uses the native VOL driver to test basic VOL datatype operations
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
test_basic_datatype_operation(void)
{
    hid_t fid       = H5I_INVALID_HID;
    hid_t tid       = H5I_INVALID_HID;
    hid_t tid_anon  = H5I_INVALID_HID;
    hid_t tcpl_id   = H5I_INVALID_HID;

    TESTING("Basic VOL datatype operations");

    if ((fid = H5Fcreate(NATIVE_VOL_TEST_FILENAME, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT)) < 0)
        TEST_ERROR;
    if ((tid = H5Tcopy(H5T_NATIVE_INT)) < 0)
        TEST_ERROR;

    /* H5Tcommit */
    if (H5Tcommit2(fid, NATIVE_VOL_TEST_DATATYPE_NAME, tid, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT) < 0)
        TEST_ERROR;

    /* H5Tflush */
    if (H5Tflush(tid) < 0)
        TEST_ERROR;

    /* H5Trefresh */
    if (H5Trefresh(tid) < 0)
        TEST_ERROR;

    /* H5Tclose */
    if (H5Tclose(tid) < 0)
        TEST_ERROR;

    /* H5Topen */
    if ((tid = H5Topen2(fid, NATIVE_VOL_TEST_DATATYPE_NAME, H5P_DEFAULT)) < 0)
        TEST_ERROR;

    /* H5Tget_create_plist */
    if ((tcpl_id = H5Tget_create_plist(tid)) < 0)
        TEST_ERROR;

    /* H5Tcommit_anon */
    if ((tid_anon = H5Tcopy(H5T_NATIVE_INT)) < 0)
        TEST_ERROR;
    if (H5Tcommit_anon(fid, tid_anon, H5P_DEFAULT, H5P_DEFAULT) < 0)
        TEST_ERROR;

    if (H5Pclose(tcpl_id) < 0)
        TEST_ERROR;
    if (H5Tclose(tid) < 0)
        TEST_ERROR;
    if (H5Tclose(tid_anon) < 0)
        TEST_ERROR;
    if (H5Fclose(fid) < 0)
        TEST_ERROR;

    HDremove(NATIVE_VOL_TEST_FILENAME);

    PASSED();
    return SUCCEED;

error:
    H5E_BEGIN_TRY {
        H5Pclose(tcpl_id);
        H5Fclose(fid);
        H5Tclose(tid);
        H5Tclose(tid_anon);
    } H5E_END_TRY;

    return FAIL;

} /* end test_basic_datatype_operation() */

#if 0

/*-------------------------------------------------------------------------
 * Function:    test_echo_vol_operation()
 *
 * Purpose:     Uses the echo VOL driver to test basic VOL operations
 *              via the H5VL public API.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
test_echo_vol_operation(void)
{
    char name[25];

    TESTING("Echo VOL operations");

    PASSED();
    return SUCCEED;

error:
    return FAIL;

} /* end test_basic_vol_operation() */
#endif


/*-------------------------------------------------------------------------
 * Function:    main
 *
 * Purpose:     Tests the virtual object layer interface (H5VL)
 *
 * Return:      EXIT_SUCCESS/EXIT_FAILURE
 *
 *-------------------------------------------------------------------------
 */
int
main(void)
{
    int nerrors = 0;

    h5_reset();

    HDputs("Testing basic Virtual Object Layer (VOL) functionality.");

    nerrors += test_vol_registration() < 0          ? 1 : 0;
    nerrors += test_native_vol_init() < 0           ? 1 : 0;
    nerrors += test_basic_file_operation() < 0      ? 1 : 0;
    nerrors += test_basic_group_operation() < 0     ? 1 : 0;
    nerrors += test_basic_dataset_operation() < 0   ? 1 : 0;
    nerrors += test_basic_attribute_operation() < 0 ? 1 : 0;
    nerrors += test_basic_object_operation() < 0    ? 1 : 0;
    nerrors += test_basic_link_operation() < 0      ? 1 : 0;
    nerrors += test_basic_datatype_operation() < 0  ? 1 : 0;

    if (nerrors) {
        HDprintf("***** %d Virtual Object Layer TEST%s FAILED! *****\n",
            nerrors, nerrors > 1 ? "S" : "");
        HDexit(EXIT_FAILURE);
    }

    HDputs("All Virtual Object Layer (VOL) tests passed.");

    HDexit(EXIT_SUCCESS);

} /* end main() */

