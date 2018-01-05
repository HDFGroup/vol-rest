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
 * Programmer:  Jordan Henderson
 *              February, 2017
 *
 * Purpose: The private header file for the REST VOL plugin.
 */

#ifndef rest_vol_H
#define rest_vol_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rest_vol_public.h"

#define HDF5_VOL_REST_VERSION 1 /* Version number of the REST VOL plugin */

#define H5_VOL_REST_CLS_VAL (H5VL_class_value_t) H5_VOL_MAX_LIB_VALUE + 1 /* Class value of the REST VOL plugin as defined in H5VLpublic.h */

/* Defines for the use of HTTP status codes */
#define HTTP_INFORMATIONAL_MIN 100 /* Minimum and maximum values for the 100 class of */
#define HTTP_INFORMATIONAL_MAX 199 /* HTTP information responses */

#define HTTP_SUCCESS_MIN 200       /* Minimum and maximum values for the 200 class of */
#define HTTP_SUCCESS_MAX 299       /* HTTP success responses */

#define HTTP_REDIRECT_MIN 300      /* Minimum and maximum values for the 300 class of */
#define HTTP_REDIRECT_MAX 399      /* HTTP redirect responses */

#define HTTP_CLIENT_ERROR_MIN 400  /* Minimum and maximum values for the 400 class of */
#define HTTP_CLIENT_ERROR_MAX 499  /* HTTP client error responses */

#define HTTP_SERVER_ERROR_MIN 500  /* Minimum and maximum values for the 500 class of */
#define HTTP_SERVER_ERROR_MAX 599  /* HTTP server error responses */

/* Macros to check for various classes of HTTP response */
#define HTTP_INFORMATIONAL(status_code) (status_code >= HTTP_INFORMATIONAL_MIN && status_code <= HTTP_INFORMATIONAL_MAX)
#define HTTP_SUCCESS(status_code)       (status_code >= HTTP_SUCCESS_MIN && status_code <= HTTP_SUCCESS_MAX)
#define HTTP_REDIRECT(status_code)      (status_code >= HTTP_REDIRECT_MIN && status_code <= HTTP_REDIRECT_MAX)
#define HTTP_CLIENT_ERROR(status_code)  (status_code >= HTTP_CLIENT_ERROR_MIN && status_code <= HTTP_CLIENT_ERROR_MAX)
#define HTTP_SERVER_ERROR(status_code)  (status_code >= HTTP_SERVER_ERROR_MIN && status_code <= HTTP_SERVER_ERROR_MAX)


typedef struct RV_object_t RV_object_t;
typedef struct link_table_entry link_table_entry;

typedef struct RV_file_t {
    unsigned  intent;
    char     *filepath_name;
    hid_t     fcpl_id;
    hid_t     fapl_id;
} RV_file_t;

typedef struct RV_group_t {
    hid_t gcpl_id;
} RV_group_t;

typedef struct RV_dataset_t {
    hid_t space_id;
    hid_t dtype_id;
    hid_t dcpl_id;
    hid_t dapl_id;
} RV_dataset_t;

typedef struct RV_attr_t {
    H5I_type_t  parent_obj_type;
    char        parent_obj_URI[URI_MAX_LENGTH];
    hid_t       space_id;
    hid_t       dtype_id;
    hid_t       acpl_id;
    char       *attr_name;
} RV_attr_t;

typedef struct RV_datatype_t {
    hid_t dtype_id;
    hid_t tcpl_id;
} RV_datatype_t;

struct RV_object_t {
    RV_object_t *domain;
    H5I_type_t   obj_type;
    char         URI[URI_MAX_LENGTH];

    union {
        RV_datatype_t datatype;
        RV_dataset_t  dataset;
        RV_group_t    group;
        RV_attr_t     attribute;
        RV_file_t     file;
    } u;
};


/* XXX: The following two definitions are only here until they are
 * moved out of their respective H5Xpkg.h header files and into a
 * more public scope. They are still needed for the REST VOL to handle
 * these API calls being made.
 */

typedef enum H5VL_file_optional_t {
    H5VL_FILE_CLEAR_ELINK_CACHE,       /* Clear external link cache             */
    H5VL_FILE_GET_FILE_IMAGE,          /* file image                            */
    H5VL_FILE_GET_FREE_SECTIONS,       /* file free selections                  */
    H5VL_FILE_GET_FREE_SPACE,          /* file freespace                        */
    H5VL_FILE_GET_INFO,                /* file info                             */
    H5VL_FILE_GET_MDC_CONF,            /* file metadata cache configuration     */
    H5VL_FILE_GET_MDC_HR,              /* file metadata cache hit rate          */
    H5VL_FILE_GET_MDC_SIZE,            /* file metadata cache size              */
    H5VL_FILE_GET_SIZE,                /* file size                             */
    H5VL_FILE_GET_VFD_HANDLE,          /* file VFD handle                       */
    H5VL_FILE_REOPEN,                  /* reopen the file                       */
    H5VL_FILE_RESET_MDC_HIT_RATE,      /* get metadata cache hit rate           */
    H5VL_FILE_SET_MDC_CONFIG           /* set metadata cache configuration      */
} H5VL_file_optional_t;

typedef enum H5VL_object_optional_t {
    H5VL_OBJECT_GET_COMMENT,           /* get object comment                    */
    H5VL_OBJECT_GET_INFO,              /* get object info                       */
    H5VL_OBJECT_SET_COMMENT            /* set object comment                    */
} H5VL_object_optional_t;


#ifdef __cplusplus
}
#endif

#endif /* rest_vol_H */
