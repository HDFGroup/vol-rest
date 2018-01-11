/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the files COPYING and Copyright.html.  COPYING can be found at the root   *
 * of the source code distribution tree; Copyright.html can be found at the  *
 * root level of an installed copy of the electronic HDF5 document set and   *
 * is linked from the top-level documents page.  It can also be found at     *
 * http://hdfgroup.org/HDF5/doc/Copyright.html.  If you do not have          *
 * access to either file, you may request a copy from help@hdfgroup.org.     *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * Programmer:  Mohamad Chaarawi <chaarawi@hdfgroup.gov>
 *              January, 2011
 */
#ifndef _H5VLprivate_H
#define _H5VLprivate_H

#include "H5VLpublic.h"

/**************************/
/* Library Private Macros */
/**************************/

/****************************/
/* Library Private Typedefs */
/****************************/

#define H5_REQUEST_NULL NULL
#define H5_EVENT_STACK_NULL ((hid_t)-1)

/* Internal struct to track VOL information with objects */
typedef struct H5VL_t {
    const H5VL_class_t *vol_cls;        /* constant plugin class info */
    int                 nrefs;          /* number of references by objects using this struct */
    hid_t               vol_id;         /* identifier for the VOL class */
} H5VL_t;

/* The internal vol object structure returned to the API */
typedef struct H5VL_object_t {
    void               *vol_obj;        /* pointer to object created by plugin */
    H5VL_t             *vol_info;       /* pointer to VOL info struct */
} H5VL_object_t;

/* Define structure to hold plugin ID & info for FAPLs */
typedef struct {
    hid_t plugin_id;            /* VOL plugin's ID */
    const void *plugin_info;    /* VOL plugin info, for open callbacks */
} H5VL_plugin_prop_t;

/*****************************/
/* Library Private Variables */
/*****************************/

/******************************/
/* Library Private Prototypes */
/******************************/

/* Forward declarations for prototype arguments */
struct H5P_genplist_t;
struct H5F_t;

H5_DLL herr_t H5VL_init(void);
H5_DLL int H5VL_term_interface(void);
H5_DLL hid_t H5VL_register_id(H5I_type_t type, void *object, H5VL_t *vol_plugin, hbool_t app_ref);
H5_DLL herr_t H5VL_free_object(H5VL_object_t *obj);
H5_DLL hid_t  H5VL_register(const void *cls, size_t size, hbool_t app_ref);
H5_DLL hid_t H5VL_object_register(void *obj, H5I_type_t obj_type, hid_t plugin_id, hbool_t app_ref);
H5_DLL ssize_t H5VL_get_plugin_name(hid_t id, char *name/*out*/, size_t size);
H5_DLL H5VL_object_t *H5VL_get_object(hid_t id);
H5_DLL void *H5VL_object(hid_t id);
H5_DLL void *H5VL_object_verify(hid_t id, H5I_type_t obj_type);
H5_DLL void *H5VL_plugin_object(H5VL_object_t *obj);

H5_DLL void *H5VL_attr_create(void *obj, H5VL_loc_params_t loc_params, const H5VL_class_t *vol_cls, const char *attr_name, hid_t acpl_id, hid_t aapl_id, hid_t dxpl_id, void **req);
H5_DLL void *H5VL_attr_open(void *obj, H5VL_loc_params_t loc_params, const H5VL_class_t *vol_cls, const char *name, hid_t aapl_id, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_attr_read(void *attr, const H5VL_class_t *vol_cls, hid_t dtype_id, void *buf, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_attr_write(void *attr, const H5VL_class_t *vol_cls, hid_t dtype_id, const void *buf, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_attr_get(void *obj, const H5VL_class_t *vol_cls, H5VL_attr_get_t get_type, hid_t dxpl_id, void **req, ...);
H5_DLL herr_t H5VL_attr_specific(void *obj, H5VL_loc_params_t loc_params, const H5VL_class_t *vol_cls, H5VL_attr_specific_t specific_type, hid_t dxpl_id, void **req, ...);
H5_DLL herr_t H5VL_attr_optional(void *obj, const H5VL_class_t *vol_cls, hid_t dxpl_id, void **req, ...);
H5_DLL herr_t H5VL_attr_close(void *attr, const H5VL_class_t *vol_cls, hid_t dxpl_id, void **req);

H5_DLL void *H5VL_dataset_create(void *obj, H5VL_loc_params_t loc_params, const H5VL_class_t *vol_cls, const char *name, hid_t dcpl_id, hid_t dapl_id, hid_t dxpl_id, void **req);
H5_DLL void *H5VL_dataset_open(void *obj, H5VL_loc_params_t loc_params, const H5VL_class_t *vol_cls, const char *name, hid_t dapl_id, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_dataset_read(void *dset, const H5VL_class_t *vol_cls, hid_t mem_type_id, hid_t mem_space_id, hid_t file_space_id, hid_t plist_id, void *buf, void **req);
H5_DLL herr_t H5VL_dataset_write(void *dset, const H5VL_class_t *vol_cls, hid_t mem_type_id, hid_t mem_space_id, hid_t file_space_id, hid_t plist_id, const void *buf, void **req);
H5_DLL herr_t H5VL_dataset_get(void *dset, const H5VL_class_t *vol_cls, H5VL_dataset_get_t get_type, hid_t dxpl_id, void **req, ...);
H5_DLL herr_t H5VL_dataset_specific(void *obj, const H5VL_class_t *vol_cls, H5VL_dataset_specific_t specific_type, hid_t dxpl_id, void **req, ...);
H5_DLL herr_t H5VL_dataset_optional(void *obj, const H5VL_class_t *vol_cls, hid_t dxpl_id, void **req, ...);
H5_DLL herr_t H5VL_dataset_close(void *dset, const H5VL_class_t *vol_cls, hid_t dxpl_id, void **req);

H5_DLL void *H5VL_datatype_commit(void *obj, H5VL_loc_params_t loc_params, const H5VL_class_t *vol_cls, const char *name, hid_t type_id, hid_t lcpl_id, hid_t tcpl_id, hid_t tapl_id, hid_t dxpl_id, void **req);
H5_DLL void *H5VL_datatype_open(void *obj, H5VL_loc_params_t loc_params, const H5VL_class_t *vol_cls, const char *name, hid_t tapl_id, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_datatype_get(void *dt, const H5VL_class_t *vol_cls, H5VL_datatype_get_t get_type, hid_t dxpl_id, void **req, ...);
H5_DLL herr_t H5VL_datatype_specific(void *obj, const H5VL_class_t *vol_cls, H5VL_datatype_specific_t specific_type, hid_t dxpl_id, void **req, ...);
H5_DLL herr_t H5VL_datatype_optional(void *obj, const H5VL_class_t *vol_cls, hid_t dxpl_id, void **req, ...);
H5_DLL herr_t H5VL_datatype_close(void *dt, const H5VL_class_t *vol_cls, hid_t dxpl_id, void **req);

H5_DLL void *H5VL_file_create(const H5VL_class_t *vol_cls, const char *name, unsigned flags, hid_t fcpl_id, hid_t fapl_id, hid_t dxpl_id, void **req);
H5_DLL void *H5VL_file_open(const H5VL_class_t *vol_cls, const char *name, unsigned flags, hid_t fapl_id, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_file_get(void *file, const H5VL_class_t *vol_cls, H5VL_file_get_t get_type, hid_t dxpl_id, void **req, ...);
H5_DLL herr_t H5VL_file_specific(void *obj, const H5VL_class_t *vol_cls, H5VL_file_specific_t specific_type, hid_t dxpl_id, void **req, ...);
H5_DLL herr_t H5VL_file_optional(void *obj, const H5VL_class_t *vol_cls, hid_t dxpl_id, void **req, ...);
H5_DLL herr_t H5VL_file_close(void *file, const H5VL_class_t *vol_cls, hid_t dxpl_id, void **req);

H5_DLL void *H5VL_group_create(void *obj, H5VL_loc_params_t loc_params, const H5VL_class_t *vol_cls, const char *name, hid_t gcpl_id, hid_t gapl_id, hid_t dxpl_id, void **req);
H5_DLL void *H5VL_group_open(void *obj, H5VL_loc_params_t loc_params, const H5VL_class_t *vol_cls, const char *name, hid_t gapl_id, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_group_get(void *obj, const H5VL_class_t *vol_cls, H5VL_group_get_t get_type, hid_t dxpl_id, void **req, ...);
H5_DLL herr_t H5VL_group_specific(void *obj, const H5VL_class_t *vol_cls, H5VL_group_specific_t specific_type, hid_t dxpl_id, void **req, ...);
H5_DLL herr_t H5VL_group_optional(void *obj, const H5VL_class_t *vol_cls, hid_t dxpl_id, void **req, ...);
H5_DLL herr_t H5VL_group_close(void *grp, const H5VL_class_t *vol_cls, hid_t dxpl_id, void **req);

H5_DLL herr_t H5VL_link_create(H5VL_link_create_type_t create_type, void *obj, H5VL_loc_params_t loc_params, const H5VL_class_t *vol_cls, hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_link_copy(void *src_obj, H5VL_loc_params_t loc_params1,
                             void *dst_obj, H5VL_loc_params_t loc_params2, const H5VL_class_t *vol_cls,
                             hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_link_move(void *src_obj, H5VL_loc_params_t loc_params1,
                             void *dst_obj, H5VL_loc_params_t loc_params2, const H5VL_class_t *vol_cls,
                             hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_link_get(void *obj, H5VL_loc_params_t loc_params, const H5VL_class_t *vol_cls, H5VL_link_get_t get_type, hid_t dxpl_id, void **req, ...);
H5_DLL herr_t H5VL_link_specific(void *obj, H5VL_loc_params_t loc_params, const H5VL_class_t *vol_cls, H5VL_link_specific_t specific_type, hid_t dxpl_id, void **req, ...);
H5_DLL herr_t H5VL_link_optional(void *obj, const H5VL_class_t *vol_cls, hid_t dxpl_id, void **req, ...);

H5_DLL void *H5VL_object_open(void *obj, H5VL_loc_params_t loc_params, const H5VL_class_t *vol_cls, H5I_type_t *opened_type, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_object_copy(void *src_obj, H5VL_loc_params_t loc_params1, const H5VL_class_t *vol_cls1, const char *src_name, 
                               void *dst_obj, H5VL_loc_params_t loc_params2, const H5VL_class_t *vol_cls2, const char *dst_name, 
                               hid_t ocpypl_id, hid_t lcpl_id, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_object_get(void *obj, H5VL_loc_params_t loc_params, const H5VL_class_t *vol_cls, H5VL_object_get_t get_type, hid_t dxpl_id, void **req, ...);
H5_DLL herr_t H5VL_object_specific(void *obj, H5VL_loc_params_t loc_params, const H5VL_class_t *vol_cls, H5VL_object_specific_t specific_type, hid_t dxpl_id, void **req, ...);
H5_DLL herr_t H5VL_object_optional(void *obj, const H5VL_class_t *vol_cls, hid_t dxpl_id, void **req, ...);

H5_DLL herr_t H5VL_request_cancel(void **req, const H5VL_class_t *vol_cls, H5ES_status_t *status);
H5_DLL herr_t H5VL_request_test(void **req, const H5VL_class_t *vol_cls, H5ES_status_t *status);
H5_DLL herr_t H5VL_request_wait(void **req, const H5VL_class_t *vol_cls, H5ES_status_t *status);

H5_DLL herr_t H5F_close_file(void *file);
H5_DLL herr_t H5A_close_attr(void *attr);
H5_DLL herr_t H5D_close_dataset(void *dset);
H5_DLL herr_t H5G_close_group(void *grp);
H5_DLL herr_t H5T_close_datatype(void *dt);

H5_DLL hid_t H5VL_native_register(H5I_type_t type, void *obj, hbool_t app_ref);
H5_DLL herr_t H5VL_native_unregister(hid_t obj_id);
#endif /* _H5VLprivate_H */
