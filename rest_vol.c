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
 * Purpose: An implementation of a VOL plugin to access HDF5 data in a
 *          REST-oriented manner.
 *
 *          Due to specialized improvements needed for performance reasons
 *          and otherwise, this VOL plugin is currently only supported for
 *          use with the HSDS server
 *          XXX: Add link to HSDS
 */
/* XXX: Implement _iterate functions */
/* XXX: Add support for the _by_idx and _by_name functions. In particular to not segfault when certain parameters to main functions are NULL */
/* XXX: Eventually replace CURL PUT calls with CURLOPT_UPLOAD calls */
/* XXX: Attempt to eliminate all use of globals/static variables */
/* XXX: Create a table of all the hard-coded JSON keys used so these can be modified in the future if desired */

#include "H5private.h"       /* XXX: Temporarily needed; Generic Functions */
#include "H5Ppublic.h"       /* Property Lists    */
#include "H5Spublic.h"       /* Dataspaces        */
#include "H5VLpublic.h"      /* VOL plugins       */
#include "H5VLprivate.h"     /* XXX: Temporarily needed */
#include "rest_vol.h"        /* REST VOL plugin   */
#include "rest_vol_public.h"
#include "rest_vol_err.h"    /* REST VOL error reporting macros */

/* Macro to handle various HTTP response codes */
#define HANDLE_RESPONSE(response_code, ERR_MAJOR, ERR_MINOR, ret_value)                                         \
do {                                                                                                            \
    switch(response_code) {                                                                                     \
        case 200:                                                                                               \
        case 201:                                                                                               \
            break;                                                                                              \
        case 400:                                                                                               \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "Malformed/Bad request for resource\n");           \
            break;                                                                                              \
        case 401:                                                                                               \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "Username/Password needed to access resource\n");  \
            break;                                                                                              \
        case 403:                                                                                               \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "Unauthorized access to resource\n");              \
            break;                                                                                              \
        case 404:                                                                                               \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "Resource not found\n");                           \
            break;                                                                                              \
        case 405:                                                                                               \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "Method not allowed\n");                           \
            break;                                                                                              \
        case 409:                                                                                               \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "Resource already exists\n");                      \
            break;                                                                                              \
        case 410:                                                                                               \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "Resource has been deleted\n");                    \
            break;                                                                                              \
        case 413:                                                                                               \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "Selection too large\n");                          \
            break;                                                                                              \
        case 500:                                                                                               \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "An internal server error occurred\n");            \
            break;                                                                                              \
        case 501:                                                                                               \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "Functionality not implemented\n");                \
            break;                                                                                              \
        case 503:                                                                                               \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "Service unavailable\n");                          \
            break;                                                                                              \
        case 504:                                                                                               \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "Gateway timeout\n");                              \
            break;                                                                                              \
        default:                                                                                                \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "Unknown error occurred\n");                       \
            break;                                                                                              \
    } /* end switch */                                                                                          \
} while(0)

/* Macro to perform cURL operation and handle errors. Note that
 * this macro should not generally be called directly. Use one
 * of the below macros to call this with the appropriate arguments. */
#define CURL_PERFORM_INTERNAL(curl_ptr, handle_HTTP_response, ERR_MAJOR, ERR_MINOR, ret_value)                  \
do {                                                                                                            \
    CURLcode result = curl_easy_perform(curl_ptr);                                                              \
                                                                                                                \
    /* Reset the cURL response buffer write position pointer */                                                 \
    response_buffer.curr_buf_ptr = response_buffer.buffer;                                                      \
                                                                                                                \
    if (CURLE_OK != result)                                                                                     \
        FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "%s", curl_easy_strerror(result))                      \
                                                                                                                \
    if (handle_HTTP_response) {                                                                                 \
        long response_code;                                                                                     \
                                                                                                                \
        if (CURLE_OK != curl_easy_getinfo(curl_ptr, CURLINFO_RESPONSE_CODE, &response_code))                    \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "can't get HTTP response code")                    \
                                                                                                                \
        HANDLE_RESPONSE(response_code, ERR_MAJOR, ERR_MINOR, ret_value);                                        \
    } /* end if */                                                                                              \
} while(0)

/* Calls the CURL_PERFORM_INTERNAL macro in such a way that any
 * HTTP error responses will cause an HDF5-like error which
 * usually calls goto and causes the function to fail. This is
 * the default behavior for most of the server requests that
 * this VOL plugin makes.
 */
#define CURL_PERFORM(curl_ptr, ERR_MAJOR, ERR_MINOR, ret_value)                                                 \
CURL_PERFORM_INTERNAL(curl_ptr, TRUE, ERR_MAJOR, ERR_MINOR, ret_value)

/* Calls the CURL_PERFORM_INTERNAL macro in such a way that any
 * HTTP error responses will not cause a function failure. This
 * is generally useful in cases where a request is sent to the
 * server to test for the existence of an object, such as in the
 * behavior for H5Fcreate()'s H5F_ACC_TRUNC flag.
 */
#define CURL_PERFORM_NO_ERR(curl_ptr, ret_value)                                                                \
CURL_PERFORM_INTERNAL(curl_ptr, FALSE, H5E_NONE_MAJOR, H5E_NONE_MINOR, ret_value)

/* Macro to check whether the size of a buffer matches the given target size
 * and reallocate the buffer if it is too small, keeping track of a given
 * pointer into the buffer. This is used when doing multiple formatted
 * prints to the same buffer. A pointer into the buffer is kept and
 * incremented so that the next print operation can continue where the
 * last one left off, and not overwrite the current contents of the buffer.
 */
#define CHECKED_REALLOC(buffer, buffer_len, target_size, ptr_to_buffer, ERR_MAJOR, ret_value)                   \
while (target_size > buffer_len) {                                                                              \
    char *tmp_realloc;                                                                                          \
                                                                                                                \
    if (NULL == (tmp_realloc = (char *) RV_realloc(buffer, 2 * buffer_len))) {                                  \
        RV_free(buffer); buffer = NULL;                                                                         \
        FUNC_GOTO_ERROR(ERR_MAJOR, H5E_CANTALLOC, ret_value, "can't allocate space")                            \
    } /* end if */                                                                                              \
                                                                                                                \
    /* Place the "current position" pointer at the correct spot in the new buffer */                            \
    if (ptr_to_buffer) ptr_to_buffer = tmp_realloc + ((char *) ptr_to_buffer - buffer);                         \
    buffer = tmp_realloc;                                                                                       \
    buffer_len *= 2;                                                                                            \
}

/* Helper macro to call the above with a temporary useless variable, since directly passing
 * NULL to the macro generates invalid code
 */
#define CHECKED_REALLOC_NO_PTR(buffer, buffer_len, target_size, ERR_MAJOR, ret_value)                           \
do {                                                                                                            \
    char *tmp = NULL;                                                                                           \
    CHECKED_REALLOC(buffer, buffer_len, target_size, tmp, ERR_MAJOR, ret_value);                                \
} while (0)


/* Defines for Dataset operations */
#define DATASET_CREATION_PROPERTIES_BODY_DEFAULT_SIZE 512
#define DATASET_CREATE_MAX_COMPACT_ATTRIBUTES_DEFAULT 8
#define DATASET_CREATE_MIN_DENSE_ATTRIBUTES_DEFAULT   6

/* Defines for Datatype operations */
#define DATATYPE_BODY_DEFAULT_SIZE                    2048
#define ENUM_MAPPING_DEFAULT_SIZE                     4096
#define OBJECT_REF_STRING_LEN                         48

/* Default sizes for various strings formed when dealing with turning a
 * representation of an HDF5 dataspace and a selection within one into JSON
 */
#define DATASPACE_SELECTION_STRING_DEFAULT_SIZE       512
#define DATASPACE_SHAPE_BUFFER_DEFAULT_SIZE           256
#define DATASPACE_MAX_RANK                            32

/* Default initial size for the response buffer allocated which cURL writes
 * its responses into
 */
#define CURL_RESPONSE_BUFFER_DEFAULT_SIZE             1024

/* Maximum length (in charactes) of the string representation of an HDF5
 * predefined integer or floating-point type, such as H5T_STD_I8LE or
 * H5T_IEEE_F32BE
 */
#define PREDEFINED_DATATYPE_NAME_MAX_LENGTH           20

/* Defines for the user of filters */
#define FILTER_NAME_MAX_LENGTH                        256
#define FILTER_MAX_CD_VALUES                          32
#define LZF_FILTER_ID                                 32000 /* The HDF5 Library could potentially add 'H5Z_FILTER_LZF' in the future */

/*
 * The vol identification number.
 */
static hid_t REST_g = -1;

hid_t h5_err_class_g = -1;

/*
 * The CURL pointer used for all cURL operations.
 */
static CURL *curl = NULL;

/*
 * cURL error message buffer.
 */
static char curl_err_buf[CURL_ERROR_SIZE];

/*
 * cURL header list
 */
struct curl_slist *curl_headers = NULL;

/*
 * Saved copy of the base URL for operating on
 */
static char *base_URL = NULL;

#ifdef TRACK_MEM_USAGE
/*
 * Counter to keep track of the currently allocated amount of bytes
 */
static size_t rest_curr_alloc_bytes;
#endif

/* XXX: Eventually pass these around instead of using a global one */
static struct {
    char   *buffer;
    char   *curr_buf_ptr;
    size_t  buffer_size;
} response_buffer;

/* Host header string for specifying the host (Domain) for requests */
const char * const host_string = "Host: ";

/* Internal initialization/termination functions which are called by
 * the public functions RVinit() and RVterm() */
static herr_t RV_init(void);
static herr_t RV_term(hid_t vtpl_id);

/* Internal malloc/free functions to track memory usage for debugging purposes */
static void *RV_malloc(size_t size);
static void *RV_calloc(size_t size);
static void *RV_realloc(void *mem, size_t size);
static void *RV_free(void *mem);

/* REST VOL Attribute callbacks */
static void  *RV_attr_create(void *obj, H5VL_loc_params_t loc_params, const char *attr_name, hid_t acpl_id, hid_t aapl_id, hid_t dxpl_id, void **req);
static void  *RV_attr_open(void *obj, H5VL_loc_params_t loc_params, const char *attr_name, hid_t aapl_id, hid_t dxpl_id, void **req);
static herr_t RV_attr_read(void *attr, hid_t dtype_id, void *buf, hid_t dxpl_id, void **req);
static herr_t RV_attr_write(void *attr, hid_t dtype_id, const void *buf, hid_t dxpl_id, void **req);
static herr_t RV_attr_get(void *obj, H5VL_attr_get_t get_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t RV_attr_specific(void *obj, H5VL_loc_params_t loc_params, H5VL_attr_specific_t specific_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t RV_attr_close(void *attr, hid_t dxpl_id, void **req);

/* REST VOL Dataset callbacks */
static void  *RV_dataset_create(void *obj, H5VL_loc_params_t loc_params, const char *name, hid_t dcpl_id, hid_t dapl_id, hid_t dxpl_id, void **req);
static void  *RV_dataset_open(void *obj, H5VL_loc_params_t loc_params, const char *name, hid_t dapl_id, hid_t dxpl_id, void **req);
static herr_t RV_dataset_read(void *dset, hid_t mem_type_id, hid_t mem_space_id,
                              hid_t file_space_id, hid_t dxpl_id, void *buf, void **req);
static herr_t RV_dataset_write(void *dset, hid_t mem_type_id, hid_t mem_space_id,
                               hid_t file_space_id, hid_t dxpl_id, const void *buf, void **req);
static herr_t RV_dataset_get(void *dset, H5VL_dataset_get_t get_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t RV_dataset_specific(void *dset, H5VL_dataset_specific_t specific_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t RV_dataset_close(void *dset, hid_t dxpl_id, void **req);

/* REST VOL Datatype callbacks */
static void  *RV_datatype_commit(void *obj, H5VL_loc_params_t loc_params, const char *name, hid_t type_id, hid_t lcpl_id, hid_t tcpl_id, hid_t tapl_id, hid_t dxpl_id, void **req);
static void  *RV_datatype_open(void *obj, H5VL_loc_params_t loc_params, const char *name, hid_t tapl_id, hid_t dxpl_id, void **req);
static herr_t RV_datatype_get(void *dt, H5VL_datatype_get_t get_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t RV_datatype_close(void *dt, hid_t dxpl_id, void **req);

/* REST VOL File callbacks */
static void  *RV_file_create(const char *name, unsigned flags, hid_t fcpl_id, hid_t fapl_id, hid_t dxpl_id, void **req);
static void  *RV_file_open(const char *name, unsigned flags, hid_t fapl_id, hid_t dxpl_id, void **req);
static herr_t RV_file_get(void *file, H5VL_file_get_t get_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t RV_file_specific(void *file, H5VL_file_specific_t specific_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t RV_file_optional(void *file, hid_t dxpl_id, void **req, va_list arguments);
static herr_t RV_file_close(void *file, hid_t dxpl_id, void **req);

/* REST VOL Group callbacks */
static void  *RV_group_create(void *obj, H5VL_loc_params_t loc_params, const char *name, hid_t gcpl_id, hid_t gapl_id, hid_t dxpl_id, void **req);
static void  *RV_group_open(void *obj, H5VL_loc_params_t loc_params, const char *name, hid_t gapl_id, hid_t dxpl_id, void **req);
static herr_t RV_group_get(void *obj, H5VL_group_get_t get_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t RV_group_close(void *grp, hid_t dxpl_id, void **req);

/* REST VOL Link callbacks */
static herr_t RV_link_create(H5VL_link_create_type_t create_type, void *obj,
                             H5VL_loc_params_t loc_params, hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req);
static herr_t RV_link_copy(void *src_obj, H5VL_loc_params_t loc_params1,
                           void *dst_obj, H5VL_loc_params_t loc_params2,
                           hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req);
static herr_t RV_link_move(void *src_obj, H5VL_loc_params_t loc_params1,
                           void *dst_obj, H5VL_loc_params_t loc_params2,
                           hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req);
static herr_t RV_link_get(void *obj, H5VL_loc_params_t loc_params, H5VL_link_get_t get_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t RV_link_specific(void *obj, H5VL_loc_params_t loc_params, H5VL_link_specific_t specific_type, hid_t dxpl_id, void **req, va_list arguments);

/* REST VOL Object callbacks */
static void  *RV_object_open(void *obj, H5VL_loc_params_t loc_params, H5I_type_t *opened_type, hid_t dxpl_id, void **req);
static herr_t RV_object_copy(void *src_obj, H5VL_loc_params_t loc_params1, const char *src_name,
                             void *dst_obj, H5VL_loc_params_t loc_params2, const char *dst_name,
                             hid_t ocpypl_id, hid_t lcpl_id, hid_t dxpl_id, void **req);
static herr_t RV_object_get(void *obj, H5VL_loc_params_t loc_params, H5VL_object_get_t get_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t RV_object_specific(void *obj, H5VL_loc_params_t loc_params, H5VL_object_specific_t specific_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t RV_object_optional(void *obj, hid_t dxpl_id, void **req, va_list arguments);

/* cURL write function callback */
static size_t write_data_callback(void *buffer, size_t size, size_t nmemb, void *userp);

/* Alternate, more portable version of the basename function which doesn't modify its argument */
static const char *RV_basename(const char *path);

/* Alternate, more portable version of the dirname function which doesn't modify its argument */
static char *RV_dirname(const char *path);

/* H5Dscatter() callback for dataset reads */
static herr_t dataset_read_scatter_op(const void **src_buf, size_t *src_buf_bytes_used, void *op_data);

/* Helper function to parse an HTTP response according to the parse callback function */
static herr_t RV_parse_response(char *HTTP_response, void *callback_data_in, void *callback_data_out, herr_t (*parse_callback)(char *, void *, void *));

/* Set of callbacks for H5VL_rest_parse_response() */
static herr_t RV_copy_object_URI_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out);
static herr_t RV_get_link_type_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out);
static herr_t RV_retrieve_attribute_count_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out);
static herr_t RV_get_group_info_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out);
static herr_t RV_parse_dataset_creation_properties_callback(char *HTTP_response, void H5_ATTR_UNUSED *callback_data_in, void *callback_data_out);

/* Helper function to find an object given a starting object to search from and a path */
static htri_t RV_find_object_by_path(RV_object_t *parent_obj, const char *obj_path, H5I_type_t *target_object_type,
       herr_t (*obj_found_callback)(char *, void *, void *), void *callback_data_in, void *callback_data_out);

/* Conversion functions to convert a JSON-format string to an HDF5 Datatype or vice versa */
static const char *RV_convert_predefined_datatype_to_string(hid_t type_id);
static herr_t      RV_convert_datatype_to_string(hid_t type_id, char **type_body, size_t *type_body_len, hbool_t nested);
static hid_t       RV_convert_string_to_datatype(const char *type);

/* Conversion function to convert one or more rest_obj_ref_t objects into a binary buffer for data transfer */
static herr_t RV_convert_obj_refs_to_buffer(const rv_obj_ref_t *ref_array, size_t ref_array_len, char **buf_out, size_t *buf_out_len);
static herr_t RV_convert_buffer_to_obj_refs(char *ref_buf, size_t ref_buf_len, rv_obj_ref_t **buf_out, size_t *buf_out_len);

/* Helper function to parse a JSON string representing an HDF5 Datatype and
 * setup an hid_t for the Datatype
 */
static hid_t RV_parse_datatype(char *type, hbool_t need_truncate);

/* Helper function to parse a JSON string representing an HDF5 Dataspace and
 * setup an hid_t for the Dataspace */
static hid_t RV_parse_dataspace(char *space);

/* Helper function to interpret a dataspace's shape and convert it into JSON */
static herr_t RV_convert_dataspace_shape_to_string(hid_t space_id, char **shape_body, char **maxdims_body);

/* Helper function to convert a selection within an HDF5 Dataspace into a JSON-format string */
static herr_t RV_convert_dataspace_selection_to_string(hid_t space_id, char **selection_string, size_t *selection_string_len, hbool_t req_param);

/* Helper functions for creating a Dataset */
static herr_t RV_setup_dataset_create_request_body(void *parent_obj, const char *name, hid_t dcpl, char **create_request_body, size_t *create_request_body_len);
static herr_t RV_parse_dataset_creation_properties(hid_t dcpl_id, char **creation_properties_body, size_t *creation_properties_body_len);

static H5VL_class_t H5VL_rest_g = {
    HDF5_VOL_REST_VERSION,     /* Version number                 */
    H5_VOL_REST_CLS_VAL,       /* Plugin value                   */
    "REST",                    /* Plugin name                    */
    NULL,                      /* Plugin initialization function */
    RV_term,                   /* Plugin termination function    */
    0,                         /* Plugin info FAPL size          */
    NULL,                      /* Plugin FAPL copy function      */
    NULL,                      /* Plugin FAPL free function      */
    {
        RV_attr_create,        /* Attribute create function      */
        RV_attr_open,          /* Attribute open function        */
        RV_attr_read,          /* Attribute read function        */
        RV_attr_write,         /* Attribute write function       */
        RV_attr_get,           /* Attribute get function         */
        RV_attr_specific,      /* Attribute specific function    */
        NULL,                  /* Attribute optional function    */
        RV_attr_close          /* Attribute close function       */
    },
    {
        RV_dataset_create,     /* Dataset create function        */
        RV_dataset_open,       /* Dataset open function          */
        RV_dataset_read,       /* Dataset read function          */
        RV_dataset_write,      /* Dataset write function         */
        RV_dataset_get,        /* Dataset get function           */
        RV_dataset_specific,   /* Dataset specific function      */
        NULL,                  /* Dataset optional function      */
        RV_dataset_close       /* Dataset close function         */
    },
    {
        RV_datatype_commit,    /* Datatype commit function       */
        RV_datatype_open,      /* Datatype open function         */
        RV_datatype_get,       /* Datatype get function          */
        NULL,                  /* Datatype specific function     */
        NULL,                  /* Datatype optional function     */
        RV_datatype_close      /* Datatype close function        */
    },
    {
        RV_file_create,        /* File create function           */
        RV_file_open,          /* File open function             */
        RV_file_get,           /* File get function              */
        RV_file_specific,      /* File specific function         */
        RV_file_optional,      /* File optional function         */
        RV_file_close          /* File close function            */
    },
    {
        RV_group_create,       /* Group create function          */
        RV_group_open,         /* Group open function            */
        RV_group_get,          /* Group get function             */
        NULL,                  /* Group specific function        */
        NULL,                  /* Group optional function        */
        RV_group_close         /* Group close function           */
    },
    {
        RV_link_create,        /* Link create function           */
        RV_link_copy,          /* Link copy function             */
        RV_link_move,          /* Link move function             */
        RV_link_get,           /* Link get function              */
        RV_link_specific,      /* Link specific function         */
        NULL                   /* Link optional function         */
    },
    {
        RV_object_open,        /* Object open function           */
        RV_object_copy,        /* Object copy function           */
        RV_object_get,         /* Object get function            */
        RV_object_specific,    /* Object specific function       */
        RV_object_optional     /* Object optional function       */
    },
    {
        NULL,
        NULL,
        NULL
    },
    NULL
};


/*-------------------------------------------------------------------------
 * Function:    RVinit
 *
 * Purpose:     Initialize the REST VOL plugin by initializing cURL and
 *              then registering the plugin with the library
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
herr_t
RVinit(void)
{
    herr_t ret_value = SUCCEED;

    /* Check if already initialized */
    if (REST_g >= 0)
        FUNC_GOTO_DONE(SUCCEED)

#ifdef TRACK_MEM_USAGE
    /* Initialize allocated memory counter */
    rest_curr_alloc_bytes = 0;
#endif

    /* Initialize cURL */
    if (NULL == (curl = curl_easy_init()))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't initialize cURL")

    /* Instruct cURL to use the buffer for error messages */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_err_buf))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't set cURL error buffer")

    /* Allocate buffer for cURL to write responses to */
    if (NULL == (response_buffer.buffer = (char *) RV_malloc(CURL_RESPONSE_BUFFER_DEFAULT_SIZE)))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTALLOC, FAIL, "can't allocate cURL response buffer")
    response_buffer.buffer_size = CURL_RESPONSE_BUFFER_DEFAULT_SIZE;
    response_buffer.curr_buf_ptr = response_buffer.buffer;

    /* Redirect cURL output to response buffer */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data_callback))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set cURL write function: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
    /* curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); */
#endif

    /* Register the plugin with HDF5's error reporting API */
    if ((h5_err_class_g = H5Eregister_class("REST VOL", "REST VOL", "1.0")) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't register with HDF5 error API")

    /* Register the plugin with the library */
    if (RV_init() < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't initialize REST VOL plugin")

done:
    /* Cleanup if REST VOL plugin initialization failed */
    if (ret_value < 0) {
        if (response_buffer.buffer)
            RV_free(response_buffer.buffer);

        curl_easy_cleanup(curl);
    } /* end if */

    return ret_value;
} /* end RVinit() */


/*-------------------------------------------------------------------------
 * Function:    RV_init
 *
 * Purpose:     Register the REST VOL plugin with the library
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
RV_init(void)
{
    herr_t ret_value = SUCCEED;

    /* Register the REST VOL plugin, if it isn't already registered */
    if (H5I_VOL != H5Iget_type(REST_g)) {
        if ((REST_g = H5VL_register((const H5VL_class_t *) &H5VL_rest_g, sizeof(H5VL_class_t), TRUE)) < 0)
            FUNC_GOTO_ERROR(H5E_ATOM, H5E_CANTINSERT, FAIL, "can't create ID for REST VOL plugin")
    } /* end if */

done:
    return ret_value;
} /* end RV_init() */


/*-------------------------------------------------------------------------
 * Function:    RVterm
 *
 * Purpose:     Shut down the REST VOL plugin
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
herr_t
RVterm(void)
{
    herr_t ret_value = SUCCEED;

    if (RV_term(-1) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CLOSEERROR, FAIL, "can't close REST VOL plugin")

done:
    return ret_value;
} /* end RVterm() */


/*-------------------------------------------------------------------------
 * Function:    RV_term
 *
 * Purpose:     Shut down the REST VOL plugin
 *
 * Return:      SUCCEED (can't fail)
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
RV_term(hid_t H5_ATTR_UNUSED vtpl_id)
{
    herr_t ret_value = SUCCEED;

    /* Free base URL */
    if (base_URL)
        base_URL = (char *) RV_free(base_URL);

    /* Free memory for cURL response buffer */
    if (response_buffer.buffer)
        response_buffer.buffer = (char *) RV_free(response_buffer.buffer);

    /* Allow cURL to clean up */
    if (curl) {
        curl_easy_cleanup(curl);
        curl = NULL;
    } /* end if */

#ifdef TRACK_MEM_USAGE
    /* Check for allocated memory */
    if (0 != rest_curr_alloc_bytes)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CLOSEERROR, FAIL, "%zu bytes were still left allocated", rest_curr_alloc_bytes)

done:
    rest_curr_alloc_bytes = 0;
#endif

    /* Unregister from the HDF5 error API */
    if (h5_err_class_g >= 0) {
        if (H5Eunregister_class(h5_err_class_g) < 0)
            FUNC_DONE_ERROR(H5E_VOL, H5E_CLOSEERROR, FAIL, "can't unregister from HDF5 error API")
        h5_err_class_g = -1;
    } /* end if */

    /* Reset ID */
    REST_g = -1;

    return ret_value;
} /* end RV_term() */


/*-------------------------------------------------------------------------
 * Function:    H5Pset_fapl_rest_vol
 *
 * Purpose:     Modify the file access property list to use the REST VOL
 *              plugin
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
herr_t
H5Pset_fapl_rest_vol(hid_t fapl_id, const char *URL, const char *username, const char *password)
{
    size_t URL_len = 0;
    herr_t ret_value;

    assert(URL && "must specify a base URL");

    if (REST_g < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_UNINITIALIZED, FAIL, "REST VOL plugin not initialized")

    if (H5P_DEFAULT == fapl_id)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_BADVALUE, FAIL, "can't set REST VOL plugin for default property list")

    if ((ret_value = H5Pset_vol(fapl_id, REST_g, NULL)) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't set REST VOL plugin in FAPL")

    /* Save a copy of the base URL being worked on so that operations like
     * creating a Group can be redirected to "base URL"/groups by building
     * off of the base URL supplied.
     */
    URL_len = strlen(URL);
    if (NULL == (base_URL = (char *) RV_malloc(URL_len + 1)))
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_CANTALLOC, FAIL, "can't allocate space for necessary base URL")

    strncpy(base_URL, URL, URL_len);
    base_URL[URL_len] = '\0';

    if (username) {
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_USERNAME, username))
            FUNC_GOTO_ERROR(H5E_ARGS, H5E_CANTINIT, FAIL, "can't set username: %s", curl_err_buf)
    } /* end if */

    if (password) {
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_PASSWORD, password))
            FUNC_GOTO_ERROR(H5E_ARGS, H5E_CANTINIT, FAIL, "can't set password: %s", curl_err_buf)
    } /* end if */

done:
    return ret_value;
} /* end H5Pset_fapl_rest_vol() */


const char *
RVget_uri(hid_t obj_id)
{
    RV_object_t *VOL_obj;
    char               *ret_value = NULL;

    if (NULL == (VOL_obj = (RV_object_t *) H5VL_object(obj_id)))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTGET, NULL, "invalid identifier")
    ret_value = VOL_obj->URI;

done:
    return (const char *) ret_value;
} /* end RVget_uri() */


/*-------------------------------------------------------------------------
 * Function:    RV_malloc
 *
 * Purpose:     Similar to the C89 version of malloc().
 *
 *              On size of 0, we return a NULL pointer instead of the
 *              standard-allowed 'special' pointer since that's more
 *              difficult to check as a return value. This is still
 *              considered an error condition since allocations of zero
 *              bytes usually indicate problems.
 *
 * Return:      Success:    Pointer to new memory
 *              Failure:    NULL
 *
 * Programmer:  Jordan Henderson
 *              Nov 8, 2017
 */
static void *
RV_malloc(size_t size)
{
    void *ret_value = NULL;

    if (size) {
#ifdef TRACK_MEM_USAGE
        size_t block_size = size;

        /* Keep track of the allocated size */
        if (NULL != (ret_value = malloc(size + sizeof(block_size)))) {
            memcpy(ret_value, &block_size, sizeof(block_size));
            ret_value = (char *) ret_value + sizeof(block_size);

            rest_curr_alloc_bytes += size;
        } /* end if */
#else
        ret_value = malloc(size);
#endif
    } /* end if */
    else
        ret_value = NULL;

    return ret_value;
} /* end RV_malloc() */


/*-------------------------------------------------------------------------
 * Function:    RV_calloc
 *
 * Purpose:     Similar to the C89 version of calloc(), except this
 *              routine just takes a 'size' parameter.
 *
 *              On size of 0, we return a NULL pointer instead of the
 *              standard-allowed 'special' pointer since that's more
 *              difficult to check as a return value. This is still
 *              considered an error condition since allocations of zero
 *              bytes usually indicate problems.
 *
 *
 * Return:      Success:    Pointer to new memory
 *              Failure:    NULL
 *
 * Programmer:  Jordan Henderson
 *              Nov 8, 2017
 */
static void *
RV_calloc(size_t size)
{
    void *ret_value = NULL;

    if (size) {
#ifdef TRACK_MEM_USAGE
        if (NULL != (ret_value = rest_malloc(size)))
            memset(ret_value, 0, size);
#else
        ret_value = calloc(1, size);
#endif
    } /* end if */
    else
        ret_value = NULL;

    return ret_value;
} /* end RV_calloc() */


/*-------------------------------------------------------------------------
 * Function:    RV_realloc
 *
 * Purpose:     Similar semantics as C89's realloc(). Specifically, the
 *              following calls are equivalent:
 *
 *              rest_realloc(NULL, size)    <==> rest_malloc(size)
 *              rest_realloc(ptr, 0)        <==> rest_free(ptr)
 *              rest_realloc(NULL, 0)       <==> NULL
 *
 *              Note that the (NULL, 0) combination is undefined behavior
 *              in the C standard.
 *
 * Return:      Success:    Ptr to new memory if size > 0
 *                          NULL if size is zero
 *              Failure:    NULL (input buffer is unchanged on failure)
 *
 * Programmer:  Jordan Henderson
 *              Nov 8, 2017
 */
static void *
RV_realloc(void *mem, size_t size)
{
    void *ret_value = NULL;

    if (!(NULL == mem && 0 == size)) {
#ifdef TRACK_MEM_USAGE
        if (size > 0) {
            if (mem) {
                size_t block_size;

                memcpy(&block_size, (char *) mem - sizeof(block_size), sizeof(block_size));

                ret_value = RV_malloc(size);
                memcpy(ret_value, mem, MIN(size, block_size));
                rest_free(mem);
            } /* end if */
            else
                ret_value = RV_malloc(size);
        } /* end if */
        else
            ret_value = RV_free(mem);
#else
        ret_value = realloc(mem, size);

        if (0 == size)
            ret_value = NULL;
#endif
    } /* end if */

    return ret_value;
} /* end RV_realloc() */


/*-------------------------------------------------------------------------
 * Function:    RV_free
 *
 * Purpose:     Just like free(3) except null pointers are allowed as
 *              arguments, and the return value (always NULL) can be
 *              assigned to the pointer whose memory was just freed:
 *
 *              thing = rest_free (thing);
 *
 * Return:      Success:    NULL
 *              Failure:    never fails
 *
 * Programmer:  Jordan Henderson
 *              Nov 8, 2017
 */
static void *
RV_free(void *mem)
{
    if (mem) {
#ifdef TRACK_MEM_USAGE
        size_t block_size;

        memcpy(&block_size, (char *) mem - sizeof(block_size), sizeof(block_size));
        rest_curr_alloc_bytes -= block_size;

        free((char *) mem - sizeof(block_size));
#else
        free(mem);
#endif
    } /* end if */

    return NULL;
} /* end RV_free() */

/****************************************
 *         VOL plugin callbacks         *
 ****************************************/


/*-------------------------------------------------------------------------
 * Function:    RV_attr_create
 *
 * Purpose:     Creates an HDF5 attribute by making the appropriate REST
 *              API call to the server and allocating an internal memory
 *              struct object for the attribute.
 *
 * Return:      Pointer to an RV_object_t struct corresponding to the
 *              created attribute on success/NULL on failure
 *
 * Programmer:  Frank Willmore, Jordan Henderson
 *              September, 2017
 */
static void *
RV_attr_create(void *obj, H5VL_loc_params_t loc_params, const char *attr_name, hid_t acpl_id,
               hid_t H5_ATTR_UNUSED aapl_id, hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req)
{
    RV_object_t *parent = (RV_object_t *) obj;
    RV_object_t *new_attribute = NULL;
    curl_off_t   create_request_body_len = 0;
    size_t       create_request_nalloc = 0;
    size_t       host_header_len = 0;
    size_t       datatype_body_len = 0;
    size_t       attr_name_len = 0;
    hid_t        type_id, space_id;
    char        *host_header = NULL;
    char        *create_request_body = NULL;
    char        *datatype_body = NULL;
    char        *shape_body = NULL;
    char         request_url[URL_MAX_LENGTH];
    char        *url_encoded_attr_name = NULL;
    void        *ret_value = NULL;

#ifdef PLUGIN_DEBUG
    printf("Received Attribute create call with following parameters:\n");
    printf("  - Attribute Name: %s\n", attr_name);
    printf("  - ACPL: %ld\n", acpl_id);
    printf("  - Parent Object URI: %s\n", parent->URI);
    printf("  - Parent Object Type: %d\n", parent->obj_type);
#endif

    assert((H5I_FILE == parent->obj_type
          || H5I_GROUP == parent->obj_type
          || H5I_DATATYPE == parent->obj_type
          || H5I_DATASET == parent->obj_type)
          && "parent object not a group, datatype or dataset");

    /* Check for write access */
    if (!(parent->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, NULL, "no write intent on file")

    /* Allocate and setup internal Attribute struct */
    if (NULL == (new_attribute = (RV_object_t *) RV_malloc(sizeof(*new_attribute))))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL, "can't allocate attribute object")

    new_attribute->obj_type = H5I_ATTR;
    new_attribute->domain = parent->domain; /* Store pointer to file that the newly-created attribute is in */
    new_attribute->u.attribute.parent_obj = parent;
    new_attribute->u.attribute.dtype_id = FAIL;
    new_attribute->u.attribute.space_id = FAIL;
    new_attribute->u.attribute.acpl_id = FAIL;
    new_attribute->u.attribute.attr_name = NULL;

    /* Copy the ACPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * H5Aget_create_plist() will function correctly
     */
    if (H5P_ATTRIBUTE_CREATE_DEFAULT != acpl_id) {
        if ((new_attribute->u.attribute.acpl_id = H5Pcopy(acpl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy ACPL")
    } /* end if */
    else
        new_attribute->u.attribute.acpl_id = H5P_ATTRIBUTE_CREATE_DEFAULT;

    /* Get the Datatype and Dataspace IDs */
    if (H5Pget(acpl_id, H5VL_PROP_ATTR_TYPE_ID, &type_id) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, NULL, "can't get property list value for attribute's datatype ID")
    if (H5Pget(acpl_id, H5VL_PROP_ATTR_SPACE_ID, &space_id) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, NULL, "can't get property list value for attribute's dataspace ID")

    /* Copy the IDs into the internal struct for the Attribute */
    if ((new_attribute->u.attribute.dtype_id = H5Tcopy(type_id)) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTCOPY, NULL, "failed to copy datatype")
    if ((new_attribute->u.attribute.space_id = H5Scopy(space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTCOPY, NULL, "failed to copy dataspace")

    /* Copy the attribute's name */
    attr_name_len = strlen(attr_name);
    if (NULL == (new_attribute->u.attribute.attr_name = (char *) RV_malloc(attr_name_len + 1)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL, "can't allocate space for attribute name")
    memcpy(new_attribute->u.attribute.attr_name, attr_name, attr_name_len);
    new_attribute->u.attribute.attr_name[attr_name_len] = '\0';

    /* Form the request body to give the new Attribute its properties */

    /* Form the Datatype portion of the Attribute create request */
    if (RV_convert_datatype_to_string(type_id, &datatype_body, &datatype_body_len, FALSE) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTCONVERT, NULL, "can't convert datatype to string representation")

    /* If the Dataspace of the Attribute was specified, convert it to JSON. Otherwise, use defaults */
    if (H5P_DEFAULT != space_id)
        if (RV_convert_dataspace_shape_to_string(space_id, &shape_body, NULL) < 0)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCREATE, NULL, "can't parse Attribute shape parameters")

    create_request_nalloc = strlen(datatype_body) + (shape_body ? strlen(shape_body) : 0) + 4;
    if (NULL == (create_request_body = (char *) RV_malloc(create_request_nalloc)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL, "can't allocate space for attribute create request body")

    if ((create_request_body_len = snprintf(create_request_body, create_request_nalloc,
            "{%s%s%s}",
            datatype_body,
            shape_body ? "," : "",
            shape_body ? shape_body : "")
        ) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL, "snprintf error")

    /* Setup the "Host: " header */
    host_header_len = strlen(parent->domain->u.file.filepath_name) + strlen(host_string) + 1;
    if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL, "can't allocate space for request Host header")

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, parent->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    /* Instruct cURL that we are sending JSON */
    curl_headers = curl_slist_append(curl_headers, "Content-Type: application/json");

    /* URL-encode the attribute name to ensure that the resulting URL for the creation
     * operation contains no illegal characters
     */
    if (NULL == (url_encoded_attr_name = curl_easy_escape(curl, attr_name, (int) attr_name_len)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTENCODE, NULL, "can't URL-encode attribute name")

    /* Redirect cURL from the base URL to
     * "/groups/<id>/attributes/<attr name>",
     * "/datatypes/<id>/attributes/<attr name>"
     * or
     * "/datasets/<id>/attributes/<attr name>",
     * depending on the type of the object the attribute is being attached to. */
    switch (parent->obj_type) {
        case H5I_FILE:
        case H5I_GROUP:
            snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/attributes/%s",
                     base_URL, parent->URI, url_encoded_attr_name);
            break;

        case H5I_DATATYPE:
            snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s/attributes/%s",
                     base_URL, parent->URI, url_encoded_attr_name);
            break;

        case H5I_DATASET:
            snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s/attributes/%s",
                     base_URL, parent->URI, url_encoded_attr_name);
            break;

        case H5I_ATTR:
        case H5I_UNINIT:
        case H5I_BADID:
        case H5I_DATASPACE:
        case H5I_REFERENCE:
        case H5I_VFL:
        case H5I_VOL:
        case H5I_GENPROP_CLS:
        case H5I_GENPROP_LST:
        case H5I_ERROR_CLASS:
        case H5I_ERROR_MSG:
        case H5I_ERROR_STACK:
        case H5I_NTYPES:
        default:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, NULL, "parent object not a group, datatype or dataset")
    } /* end switch */

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, NULL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT"))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, NULL, "can't set up cURL to make HTTP PUT request: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDS, create_request_body))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, NULL, "can't set cURL PUT data: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, create_request_body_len))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, NULL, "can't set cURL PUT data size: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, NULL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
    printf("  - Creating Attribute\n\n");

    printf("   /********************************\\\n");
    printf("-> | Making a request to the server |\n");
    printf("   \\********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_ATTR, H5E_CANTCREATE, NULL);

    ret_value = (void *) new_attribute;

done:
#ifdef PLUGIN_DEBUG
    printf("Attribute create URL: %s\n\n", request_url);
    printf("Attribute create body: %s\n\n", create_request_body);
    printf("Attribute create response buffer: %s\n\n", response_buffer.buffer);

    if (new_attribute) {
        printf("Attribute H5VL_rest_object_t fields:\n");
        printf("  - Attribute Object type: %d\n", new_attribute->obj_type);
        printf("  - Attribute Parent Domain path: %s\n\n", new_attribute->domain->u.file.filepath_name);
    }
#endif

    if (create_request_body)
        RV_free(create_request_body);
    if (datatype_body)
        RV_free(datatype_body);
    if (shape_body)
        RV_free(shape_body);
    if (host_header)
        RV_free(host_header);
    if (url_encoded_attr_name)
        curl_free(url_encoded_attr_name);

    /* Clean up allocated attribute object if there was an issue */
    if (new_attribute && !ret_value)
        if (RV_attr_close(new_attribute, FAIL, NULL) < 0)
            FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTCLOSEOBJ, NULL, "can't close attribute")

    /* Reset cURL custom request to prevent issues with future requests */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL))
        FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTSET, NULL, "can't reset cURL custom request: %s", curl_err_buf)

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    return ret_value;
} /* end RV_attr_create() */


/*-------------------------------------------------------------------------
 * Function:    RV_attr_open
 *
 * Purpose:     Opens an existing HDF5 attribute object by retrieving its
 *              URI, dataspace and datatype info from the server and
 *              allocating an internal memory struct object for the
 *              attribute.
 *
 * Return:      Pointer to an RV_object_t struct corresponding to the
 *              opened attribute on success/NULL on failure
 *
 * Programmer:  Frank Willmore, Jordan Henderson
 *              September, 2017
 */
static void *
RV_attr_open(void *obj, H5VL_loc_params_t loc_params, const char *attr_name,
             hid_t H5_ATTR_UNUSED aapl_id, hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req)
{
    RV_object_t *parent = (RV_object_t *) obj;
    RV_object_t *attribute = NULL;
    size_t       attr_name_len = 0;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    char         request_url[URL_MAX_LENGTH];
    char        *url_encoded_attr_name = NULL;
    void        *ret_value = NULL;

#ifdef PLUGIN_DEBUG
    printf("Received Attribute open call with following parameters:\n");
    printf("  - Attribute Name: %s\n", attr_name);
    printf("  - AAPL: %ld\n", aapl_id);
    printf("  - Parent Object Type: %d\n", parent->obj_type);
#endif

    assert((H5I_FILE == parent->obj_type
          || H5I_GROUP == parent->obj_type
          || H5I_DATATYPE == parent->obj_type
          || H5I_DATASET == parent->obj_type)
          && "parent object not a group, datatype or dataset");

    /* XXX: Eventually implement H5Aopen_by_idx() */
    if (loc_params.type == H5VL_OBJECT_BY_IDX)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_UNSUPPORTED, NULL, "opening an attribute by index is currently unsupported")

    /* Allocate and setup internal Attribute struct */
    if (NULL == (attribute = (RV_object_t *) RV_malloc(sizeof(*attribute))))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL, "can't allocate attribute object")

    attribute->obj_type = H5I_ATTR;
    attribute->domain = parent->domain; /* Store pointer to file that the opened Dataset is within */
    attribute->u.attribute.parent_obj = parent;
    attribute->u.attribute.dtype_id = FAIL;
    attribute->u.attribute.space_id = FAIL;
    attribute->u.attribute.acpl_id = FAIL;
    attribute->u.attribute.attr_name = NULL;

    /* Make a GET request to the server to retrieve information about the attribute */

    /* Setup the "Host: " header */
    host_header_len = strlen(attribute->domain->u.file.filepath_name) + strlen(host_string) + 1;
    if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL, "can't allocate space for request Host header")

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, attribute->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    /* URL-encode the attribute name to ensure that the resulting URL for the open
     * operation contains no illegal characters
     */
    attr_name_len = strlen(attr_name);
    if (NULL == (url_encoded_attr_name = curl_easy_escape(curl, attr_name, (int) attr_name_len)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTENCODE, NULL, "can't URL-encode attribute name")

    /* Redirect cURL from the base URL to
     * "/groups/<id>/attributes/<attr name>",
     * "/datatypes/<id>/attributes/<attr name>"
     * or
     * "/datasets/<id>/attributes/<attr name>",
     * depending on the type of the object the attribute is attached to. */
    switch (parent->obj_type) {
        case H5I_FILE:
        case H5I_GROUP:
            snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/attributes/%s",
                     base_URL, parent->URI, url_encoded_attr_name);
            break;

        case H5I_DATATYPE:
            snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s/attributes/%s",
                     base_URL, parent->URI, url_encoded_attr_name);
            break;

        case H5I_DATASET:
            snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s/attributes/%s",
                     base_URL, parent->URI, url_encoded_attr_name);
            break;

        case H5I_ATTR:
        case H5I_UNINIT:
        case H5I_BADID:
        case H5I_DATASPACE:
        case H5I_REFERENCE:
        case H5I_VFL:
        case H5I_VOL:
        case H5I_GENPROP_CLS:
        case H5I_GENPROP_LST:
        case H5I_ERROR_CLASS:
        case H5I_ERROR_MSG:
        case H5I_ERROR_STACK:
        case H5I_NTYPES:
        default:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, NULL, "parent object not a group, datatype or dataset")
    } /* end switch */

#ifdef PLUGIN_DEBUG
    printf("Accessing link: %s\n\n", request_url);
#endif

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, NULL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, NULL, "can't set up cURL to make HTTP GET request: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, NULL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
    printf("   /********************************\\\n");
    printf("-> | Making a request to the server |\n");
    printf("   \\********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_ATTR, H5E_CANTGET, NULL);

    /* Set up a Dataspace for the opened Attribute */
    if ((attribute->u.attribute.space_id = RV_parse_dataspace(response_buffer.buffer)) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, NULL, "can't parse attribute dataspace")

    /* Set up a Datatype for the opened Attribute */
    if ((attribute->u.attribute.dtype_id = RV_parse_datatype(response_buffer.buffer, TRUE)) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, NULL, "can't parse attribute datatype")

    /* Copy the attribute's name */
    if (NULL == (attribute->u.attribute.attr_name = (char *) RV_malloc(attr_name_len + 1)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL, "can't allocate space for attribute name")
    memcpy(attribute->u.attribute.attr_name, attr_name, attr_name_len);
    attribute->u.attribute.attr_name[attr_name_len] = '\0';

    /* Set up an ACPL for the attribute so that H5Aget_create_plist() will function correctly */
    /* XXX: Set any properties necessary */
    if ((attribute->u.attribute.acpl_id = H5Pcreate(H5P_ATTRIBUTE_CREATE)) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "can't create ACPL for attribute")

    ret_value = (void *) attribute;

done:
#ifdef PLUGIN_DEBUG
    printf("Link access response buffer: %s\n\n", response_buffer.buffer);

    if (attribute) {
        printf("Attribute H5VL_rest_object_t fields:\n");
        printf("  - Attribute Object type: %d\n", attribute->obj_type);
        printf("  - Attribute Parent Domain path: %s\n\n", attribute->domain->u.file.filepath_name);
    }
#endif

    if (host_header)
        RV_free(host_header);
    if (url_encoded_attr_name)
        curl_free(url_encoded_attr_name);

    /* Clean up allocated attribute object if there was an issue */
    if (attribute && !ret_value)
        if (RV_attr_close(attribute, FAIL, NULL) < 0)
            FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTCLOSEOBJ, NULL, "can't close attribute")

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    return ret_value;
} /* end RV_attr_open() */


/*-------------------------------------------------------------------------
 * Function:    RV_attr_read
 *
 * Purpose:     Reads an entire HDF5 attribute from the server.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              November, 2017
 */
static herr_t
RV_attr_read(void *attr, hid_t dtype_id, void *buf, hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req)
{
    RV_object_t *attribute = (RV_object_t *) attr;
    H5T_class_t  dtype_class;
    hssize_t     file_select_npoints;
    hbool_t      is_transfer_binary = FALSE;
    htri_t       is_variable_str;
    size_t       dtype_size;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    char        *url_encoded_attr_name = NULL;
    char         request_url[URL_MAX_LENGTH];
    herr_t       ret_value = SUCCEED;

    assert(buf);
    assert(H5I_ATTR == attribute->obj_type && "not an attribute");

#ifdef PLUGIN_DEBUG
    printf("Recieved Attribute read call with following parameters:\n");
    if (attribute->u.attribute.attr_name) printf("  - Attribute name: %s\n", attribute->u.attribute.attr_name);
#endif

    /* Determine whether it's possible to receive the data as a binary blob instead of as JSON */
    if (H5T_NO_CLASS == (dtype_class = H5Tget_class(dtype_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid attribute datatype")

    if ((is_variable_str = H5Tis_variable_str(dtype_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid attribute datatype")

    is_transfer_binary = (H5T_VLEN != dtype_class) && !is_variable_str;

    if ((file_select_npoints = H5Sget_select_npoints(attribute->u.attribute.space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid attribute dataspace")

    if (0 == (dtype_size = H5Tget_size(dtype_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid attribute datatype")

    /* Setup the "Host: " header */
    host_header_len = strlen(attribute->domain->u.file.filepath_name) + strlen(host_string) + 1;
    if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, attribute->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    /* Instruct cURL on which type of transfer to perform, binary or JSON */
    curl_headers = curl_slist_append(curl_headers, is_transfer_binary ? "Accept: application/octet-stream" : "Accept: application/json");

    /* URL-encode the attribute name to ensure that the resulting URL for the read
     * operation contains no illegal characters
     */
    if (NULL == (url_encoded_attr_name = curl_easy_escape(curl, attribute->u.attribute.attr_name, 0)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTENCODE, FAIL, "can't URL-encode attribute name")

    /* Redirect cURL from the base URL to
     * "/groups/<id>/attributes/<attr name>/value",
     * "/datatypes/<id>/attributes/<attr name>/value"
     * or
     * "/datasets/<id>/attributes/<attr name>/value",
     * depending on the type of the object the attribute is attached to. */
    switch (attribute->u.attribute.parent_obj->obj_type) {
        case H5I_FILE:
        case H5I_GROUP:
            snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/attributes/%s/value",
                     base_URL, attribute->u.attribute.parent_obj->URI, url_encoded_attr_name);
            break;

        case H5I_DATATYPE:
            snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s/attributes/%s/value",
                     base_URL, attribute->u.attribute.parent_obj->URI, url_encoded_attr_name);
            break;

        case H5I_DATASET:
            snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s/attributes/%s/value",
                     base_URL, attribute->u.attribute.parent_obj->URI, url_encoded_attr_name);
            break;

        case H5I_ATTR:
        case H5I_UNINIT:
        case H5I_BADID:
        case H5I_DATASPACE:
        case H5I_REFERENCE:
        case H5I_VFL:
        case H5I_VOL:
        case H5I_GENPROP_CLS:
        case H5I_GENPROP_LST:
        case H5I_ERROR_CLASS:
        case H5I_ERROR_MSG:
        case H5I_ERROR_STACK:
        case H5I_NTYPES:
        default:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "parent object not a group, datatype or dataset")
    } /* end switch */

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP GET request: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
    printf("  - Reading attribute\n\n");

    printf("   /********************************\\\n");
    printf("-> | Making a request to the server |\n");
    printf("   \\********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_ATTR, H5E_READERROR, FAIL);

    memcpy(buf, response_buffer.buffer, (size_t) file_select_npoints * dtype_size);

done:
#ifdef PLUGIN_DEBUG
    printf("Attribute read URL: %s\n\n", request_url);
    printf("Attribute read response buffer: %s\n\n", response_buffer.buffer);
#endif

    if (host_header)
        RV_free(host_header);
    if (url_encoded_attr_name)
        curl_free(url_encoded_attr_name);

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    return ret_value;
} /* end RV_attr_read() */


/*-------------------------------------------------------------------------
 * Function:    RV_attr_write
 *
 * Purpose:     Writes an entire HDF5 attribute on the server.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              November, 2017
 */
static herr_t
RV_attr_write(void *attr, hid_t dtype_id, const void *buf, hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req)
{
    RV_object_t *attribute = (RV_object_t *) attr;
    H5T_class_t  dtype_class;
    hssize_t     file_select_npoints;
    hbool_t      is_transfer_binary = FALSE;
    htri_t       is_variable_str;
    size_t       dtype_size;
    size_t       write_body_len = 0;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    char        *write_body = NULL;
    char        *url_encoded_attr_name = NULL;
    char         request_url[URL_MAX_LENGTH];
    herr_t       ret_value = SUCCEED;

    assert(buf);
    assert(H5I_ATTR == attribute->obj_type && "not an attribute");

    /* Check for write access */
    if (!(attribute->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "no write intent on file")

#ifdef PLUGIN_DEBUG
    printf("Received Attribute write call with following parameters:\n");
    if (attribute->u.attribute.attr_name) printf("  - Attribute name: %s\n", attribute->u.attribute.attr_name);
#endif

    /* Determine whether it's possible to send the data as a binary blob instead of as JSON */
    if (H5T_NO_CLASS == (dtype_class = H5Tget_class(dtype_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid attribute datatype")

    if ((is_variable_str = H5Tis_variable_str(dtype_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid attribute datatype")

    is_transfer_binary = (H5T_VLEN != dtype_class) && !is_variable_str;

    if ((file_select_npoints = H5Sget_select_npoints(attribute->u.attribute.space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid attribute dataspace")

    if (0 == (dtype_size = H5Tget_size(dtype_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid attribute datatype")

    if (!is_transfer_binary) {

    } /* end if */
    else
        write_body_len = (size_t) file_select_npoints * dtype_size;

    /* Setup the "Host: " header */
    host_header_len = strlen(attribute->domain->u.file.filepath_name) + strlen(host_string) + 1;
    if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, attribute->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    /* Instruct cURL on which type of transfer to perform, binary or JSON */
    curl_headers = curl_slist_append(curl_headers, is_transfer_binary ? "Content-Type: application/octet-stream" : "Content-Type: application/json");

    /* URL-encode the attribute name to ensure that the resulting URL for the write
     * operation contains no illegal characters
     */
    if (NULL == (url_encoded_attr_name = curl_easy_escape(curl, attribute->u.attribute.attr_name, 0)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTENCODE, FAIL, "can't URL-encode attribute name")

    /* Redirect cURL from the base URL to
     * "/groups/<id>/attributes/<attr name>/value",
     * "/datatypes/<id>/attributes/<attr name>/value"
     * or
     * "/datasets/<id>/attributes/<attr name>/value",
     * depending on the type of the object the attribute is attached to. */
    switch (attribute->u.attribute.parent_obj->obj_type) {
        case H5I_FILE:
        case H5I_GROUP:
            snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/attributes/%s/value",
                     base_URL, attribute->u.attribute.parent_obj->URI, url_encoded_attr_name);
            break;

        case H5I_DATATYPE:
            snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s/attributes/%s/value",
                     base_URL, attribute->u.attribute.parent_obj->URI, url_encoded_attr_name);
            break;

        case H5I_DATASET:
            snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s/attributes/%s/value",
                     base_URL, attribute->u.attribute.parent_obj->URI, url_encoded_attr_name);
            break;

        case H5I_ATTR:
        case H5I_UNINIT:
        case H5I_BADID:
        case H5I_DATASPACE:
        case H5I_REFERENCE:
        case H5I_VFL:
        case H5I_VOL:
        case H5I_GENPROP_CLS:
        case H5I_GENPROP_LST:
        case H5I_ERROR_CLASS:
        case H5I_ERROR_MSG:
        case H5I_ERROR_STACK:
        case H5I_NTYPES:
        default:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "parent object not a group, datatype or dataset")
    } /* end switch */

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT"))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP PUT request: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDS, is_transfer_binary ? (const char *) buf : write_body))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set cURL PUT data: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t) write_body_len)) /* XXX: unsafe cast */
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set cURL PUT data size: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
    printf("  - Writing attribute\n\n");

    printf("   /********************************\\\n");
    printf("-> | Making a request to the server |\n");
    printf("   \\********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_ATTR, H5E_WRITEERROR, FAIL);

done:
#ifdef PLUGIN_DEBUG
    printf("Attribute write URL: %s\n\n", request_url);
    printf("Attribute write response buffer: %s\n\n", response_buffer.buffer);
#endif

    if (write_body)
        RV_free(write_body);
    if (host_header)
        RV_free(host_header);
    if (url_encoded_attr_name)
        curl_free(url_encoded_attr_name);

    /* Reset cURL custom request to prevent issues with future requests */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL))
        FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't reset cURL custom request: %s", curl_err_buf)

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    return ret_value;
} /* end RV_attr_write() */


/*-------------------------------------------------------------------------
 * Function:    RV_attr_get
 *
 * Purpose:     Performs a "GET" operation on an HDF5 attribute, such as
 *              calling the H5Aget_info routine.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              November, 2017
 */
static herr_t
RV_attr_get(void *obj, H5VL_attr_get_t get_type, hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req, va_list arguments)
{
    RV_object_t *_obj = (RV_object_t *) obj;
    herr_t       ret_value = SUCCEED;

#ifdef PLUGIN_DEBUG
    printf("Received Attribute get call with following parameters:\n");
    printf("  - Get Type: %d\n", get_type);
    if (H5I_ATTR == _obj->obj_type) printf("  - Attribute URI: %s\n", _obj->URI);
    printf("  - Attribute File: %s\n\n", _obj->domain->u.file.filepath_name);
#endif

    switch (get_type) {
        /* H5Aget_create_plist */
        case H5VL_ATTR_GET_ACPL:
        {
            hid_t *ret_id = va_arg(arguments, hid_t *);

            if ((*ret_id = H5Pcopy(_obj->u.attribute.acpl_id)) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, FAIL, "can't copy attribute ACPL")

            break;
        } /* H5VL_ATTR_GET_ACPL */

        /* H5Aget_info (_by_name/_by_idx) */
        case H5VL_ATTR_GET_INFO:
        {
            H5VL_loc_params_t  loc_params = va_arg(arguments, H5VL_loc_params_t);
            H5A_info_t        *attr_info = va_arg(arguments, H5A_info_t *);
            const char        *attr_name = NULL;

            /* Initialize struct to 0 */
            memset(attr_info, 0, sizeof(*attr_info));

            switch (loc_params.type) {
                /* H5Aget_info */
                case H5VL_OBJECT_BY_SELF:
                {
                    /* XXX: */
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_UNSUPPORTED, FAIL, "H5Aget_info is unsupported")
                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Aget_info_by_name */
                case H5VL_OBJECT_BY_NAME:
                {
                    /* XXX: */
                    attr_name = va_arg(arguments, const char *);

                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_UNSUPPORTED, FAIL, "H5Aget_info_by_name is unsupported")
                    break;
                } /* H5VL_OBJECT_BY_NAME */

                /* H5Aget_info_by_idx */
                case H5VL_OBJECT_BY_IDX:
                {
                    /* XXX: */
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_UNSUPPORTED, FAIL, "H5Aget_info_by_idx is unsupported")
                    break;
                } /* H5VL_OBJECT_BY_IDX */

                case H5VL_OBJECT_BY_ADDR:
                case H5VL_OBJECT_BY_REF:
                default:
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid loc_params type")
            } /* end switch */

            break;
        } /* H5VL_ATTR_GET_INFO */

        /* H5Aget_name (_by_idx) */
        case H5VL_ATTR_GET_NAME:
        {
            H5VL_loc_params_t  loc_params = va_arg(arguments, H5VL_loc_params_t);
            size_t             name_buf_size = va_arg(arguments, size_t);
            char              *name_buf = va_arg(arguments, char *);
            ssize_t           *ret_size = va_arg(arguments, ssize_t *);

            switch (loc_params.type) {
                /* H5Aget_name */
                case H5VL_OBJECT_BY_SELF:
                {
                    *ret_size = (ssize_t) strlen(_obj->u.attribute.attr_name);

                    if (name_buf) {
                        strncpy(name_buf, _obj->u.attribute.attr_name, name_buf_size - 1);
                        name_buf[name_buf_size - 1] = '\0';
                    } /* end if */

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Aget_name_by_idx */
                case H5VL_OBJECT_BY_IDX:
                {
                    /* XXX: Handle _by_idx case */
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_UNSUPPORTED, FAIL, "H5Aget_name_by_idx is unsupported")
                    break;
                } /* H5VL_OBJECT_BY_IDX */

                case H5VL_OBJECT_BY_REF:
                case H5VL_OBJECT_BY_ADDR:
                case H5VL_OBJECT_BY_NAME:
                default:
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid loc_params type")
            }

            break;
        } /* H5VL_ATTR_GET_NAME */

        /* H5Aget_space */
        case H5VL_ATTR_GET_SPACE:
        {
            hid_t *ret_id = va_arg(arguments, hid_t *);

            if ((*ret_id = H5Scopy(_obj->u.attribute.space_id)) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTCOPY, FAIL, "can't copy attribute's dataspace")

            break;
        } /* H5VL_ATTR_GET_SPACE */

        /* H5Aget_storage_size */
        case H5VL_ATTR_GET_STORAGE_SIZE:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_UNSUPPORTED, FAIL, "H5Aget_storage_size is unsupported")

        /* H5Aget_type */
        case H5VL_ATTR_GET_TYPE:
        {
            hid_t *ret_id = va_arg(arguments, hid_t *);

            if ((*ret_id = H5Tcopy(_obj->u.attribute.dtype_id)) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTCOPY, FAIL, "can't copy attribute's datatype")

            break;
        } /* H5VL_ATTR_GET_TYPE */

        default:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "can't get this type of information from attribute")
    } /* end switch */

done:
    return ret_value;
} /* end RV_attr_get() */


/*-------------------------------------------------------------------------
 * Function:    RV_attr_specific
 *
 * Purpose:     Performs a plugin-specific operation on an HDF5 attribute,
 *              such as calling H5Adelete
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              November, 2017
 */
static herr_t
RV_attr_specific(void *obj, H5VL_loc_params_t loc_params, H5VL_attr_specific_t specific_type,
                 hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req, va_list arguments)
{
    RV_object_t *loc_obj = (RV_object_t *) obj;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    char         request_url[URL_MAX_LENGTH];
    char        *url_encoded_attr_name = NULL;
    herr_t       ret_value = SUCCEED;

#ifdef PLUGIN_DEBUG
    printf("Received Attribute-specific call with following parameters:\n");
    printf("  - Specific type: %d\n", specific_type);
#endif

    switch (specific_type) {
        /* H5Adelete (_by_name/_by_idx) */
        case H5VL_ATTR_DELETE:
        {
            char *attr_name = NULL;
            char *obj_URI = NULL;
            char  temp_URI[URI_MAX_LENGTH];

            /* Check for write access */
            if (!(loc_obj->domain->u.file.intent & H5F_ACC_RDWR))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "no write intent on file")

            switch (loc_params.type) {
                /* H5Adelete and H5Adelete_by_name */
                case H5VL_OBJECT_BY_SELF:
                {
                    attr_name = va_arg(arguments, char *);
                    obj_URI = loc_obj->URI;

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                case H5VL_OBJECT_BY_NAME:
                {
                    H5I_type_t obj_type = H5I_UNINIT;
                    htri_t     search_ret;

                    attr_name = va_arg(arguments, char *);

                    search_ret = RV_find_object_by_path(loc_obj, loc_params.loc_data.loc_by_name.name, &obj_type,
                            RV_copy_object_URI_callback, NULL, temp_URI);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PATH, FAIL, "can't locate object that attribute is attached to")

                    obj_URI = temp_URI;

                    break;
                } /* H5VL_OBJECT_BY_NAME */

                /* H5Adelete_by_idx */
                case H5VL_OBJECT_BY_IDX:
                {
                    /* XXX: */
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_UNSUPPORTED, FAIL, "H5Adelete_by_idx is unsupported")
                    break;
                } /* H5VL_OBJECT_BY_IDX */

                case H5VL_OBJECT_BY_ADDR:
                case H5VL_OBJECT_BY_REF:
                default:
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid loc_params type")
            } /* end switch */

            /* URL-encode the attribute name so that the resulting URL for the
             * attribute delete operation doesn't contain any illegal characters
             */
            if (NULL == (url_encoded_attr_name = curl_easy_escape(curl, attr_name, 0)))
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTENCODE, FAIL, "can't URL-encode attribute name")

            /* Redirect cURL from the base URL to
             * "/groups/<id>/attributes/<attr name>",
             * "/datatypes/<id>/attributes/<attr name>"
             * or
             * "/datasets/<id>/attributes/<attr name>,
             * depending on the type of the object the attribute is attached to. */
            switch (loc_params.obj_type) {
                case H5I_FILE:
                case H5I_GROUP:
                    snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/attributes/%s",
                            base_URL, obj_URI, url_encoded_attr_name);
                    break;

                case H5I_DATATYPE:
                    snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s/attributes/%s",
                            base_URL, obj_URI, url_encoded_attr_name);
                    break;

                case H5I_DATASET:
                    snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s/attributes/%s",
                            base_URL, obj_URI, url_encoded_attr_name);
                    break;

                case H5I_ATTR:
                case H5I_UNINIT:
                case H5I_BADID:
                case H5I_DATASPACE:
                case H5I_REFERENCE:
                case H5I_VFL:
                case H5I_VOL:
                case H5I_GENPROP_CLS:
                case H5I_GENPROP_LST:
                case H5I_ERROR_CLASS:
                case H5I_ERROR_MSG:
                case H5I_ERROR_STACK:
                case H5I_NTYPES:
                default:
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "parent object not a group, datatype or dataset")
            } /* end switch */

#ifdef PLUGIN_DEBUG
            printf("  - Attribute Delete URL: %s\n\n", request_url);
#endif

            /* Setup the "Host: " header */
            host_header_len = strlen(loc_obj->domain->u.file.filepath_name) + strlen(host_string) + 1;
            if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

            strcpy(host_header, host_string);

            curl_headers = curl_slist_append(curl_headers, strncat(host_header, loc_obj->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

            /* Disable use of Expect: 100 Continue HTTP response */
            curl_headers = curl_slist_append(curl_headers, "Expect:");

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE"))
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP DELETE request: %s", curl_err_buf)
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
            printf("   /********************************\\\n");
            printf("-> | Making a request to the server |\n");
            printf("   \\********************************/\n\n");
#endif

            CURL_PERFORM(curl, H5E_ATTR, H5E_CANTREMOVE, FAIL);

            break;
        } /* H5VL_ATTR_DELETE */

        /* H5Aexists (_by_name) */
        case H5VL_ATTR_EXISTS:
        {
            const char *attr_name = va_arg(arguments, const char *);
            htri_t     *ret = va_arg(arguments, htri_t *);
            long        http_response;
            char       *obj_URI;
            char        temp_URI[URI_MAX_LENGTH];

            /* XXX: Handle _by_name case */
            switch (loc_params.type) {
                /* H5Aexists */
                case H5VL_OBJECT_BY_SELF:
                {
                    obj_URI = loc_obj->URI;
                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Aexists_by_name */
                case H5VL_OBJECT_BY_NAME:
                {
                    H5I_type_t obj_type = H5I_UNINIT;
                    htri_t     search_ret;

                    search_ret = RV_find_object_by_path(loc_obj, loc_params.loc_data.loc_by_name.name, &obj_type,
                            RV_copy_object_URI_callback, NULL, temp_URI);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PATH, FAIL, "can't locate object that attribute is attached to")

                    obj_URI = temp_URI;

                    break;
                } /* H5VL_OBJECT_BY_NAME */

                case H5VL_OBJECT_BY_IDX:
                case H5VL_OBJECT_BY_ADDR:
                case H5VL_OBJECT_BY_REF:
                default:
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid loc_params types")
            } /* end switch */

            /* URL-encode the attribute name so that the resulting URL for the
             * attribute delete operation doesn't contain any illegal characters
             */
            if (NULL == (url_encoded_attr_name = curl_easy_escape(curl, attr_name, 0)))
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTENCODE, FAIL, "can't URL-encode attribute name")

            /* Redirect cURL from the base URL to
             * "/groups/<id>/attributes/<attr name>",
             * "/datatypes/<id>/attributes/<attr name>"
             * or
             * "/datasets/<id>/attributes/<attr name>,
             * depending on the type of the object the attribute is attached to. */
            switch (loc_params.obj_type) {
                case H5I_FILE:
                case H5I_GROUP:
                    snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/attributes/%s",
                            base_URL, obj_URI, url_encoded_attr_name);
                    break;

                case H5I_DATATYPE:
                    snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s/attributes/%s",
                            base_URL, obj_URI, url_encoded_attr_name);
                    break;

                case H5I_DATASET:
                    snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s/attributes/%s",
                            base_URL, obj_URI, url_encoded_attr_name);
                    break;

                case H5I_ATTR:
                case H5I_UNINIT:
                case H5I_BADID:
                case H5I_DATASPACE:
                case H5I_REFERENCE:
                case H5I_VFL:
                case H5I_VOL:
                case H5I_GENPROP_CLS:
                case H5I_GENPROP_LST:
                case H5I_ERROR_CLASS:
                case H5I_ERROR_MSG:
                case H5I_ERROR_STACK:
                case H5I_NTYPES:
                default:
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "parent object not a group, datatype or dataset")
            } /* end switch */

#ifdef PLUGIN_DEBUG
            printf("  - Attribute existence check URL: %s\n\n", request_url);
#endif

            /* Setup the "Host: " header */
            host_header_len = strlen(loc_obj->domain->u.file.filepath_name) + strlen(host_string) + 1;
            if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

            strcpy(host_header, host_string);

            curl_headers = curl_slist_append(curl_headers, strncat(host_header, loc_obj->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

            /* Disable use of Expect: 100 Continue HTTP response */
            curl_headers = curl_slist_append(curl_headers, "Expect:");

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP GET request: %s", curl_err_buf)
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
            printf("   /********************************\\\n");
            printf("-> | Making a request to the server |\n");
            printf("   \\********************************/\n\n");
#endif

            CURL_PERFORM_NO_ERR(curl, FAIL);

            if (CURLE_OK != curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_response))
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "can't get HTTP response code")

            if (HTTP_SUCCESS(http_response))
                *ret = TRUE;
            else if (HTTP_CLIENT_ERROR(http_response))
                *ret = FALSE;
            else
                HANDLE_RESPONSE(http_response, H5E_ATTR, H5E_CANTGET, FAIL);

            break;
        } /* H5VL_ATTR_EXISTS */

        /* H5Aiterate (_by_name) */
        case H5VL_ATTR_ITER:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_UNSUPPORTED, FAIL, "H5Aiterate and H5Aiterate_by_name are unsupported")

        /* H5Arename (_by_name) */
        case H5VL_ATTR_RENAME:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_UNSUPPORTED, FAIL, "H5Arename and H5Arename_by_name are unsupported")

        default:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "unknown attribute operation")
    } /* end switch */

done:
    if (host_header)
        RV_free(host_header);

    /* In case a custom DELETE request was made, reset the request to NULL
     * to prevent any possible future issues with requests
     */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL))
        FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't reset cURL custom request: %s", curl_err_buf)

    if (url_encoded_attr_name)
        curl_free(url_encoded_attr_name);

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    return ret_value;
} /* end RV_attr_specific() */


/*-------------------------------------------------------------------------
 * Function:    RV_attr_close
 *
 * Purpose:     Closes an HDF5 attribute by freeing the memory allocated
 *              for its internal memory struct object. There is no
 *              interaction with the server, whose state is unchanged.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Frank Willmore, Jordan Henderson
 *              September, 2017
 */
static herr_t
RV_attr_close(void *attr, hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req)
{
    RV_object_t *_attr = (RV_object_t *) attr;
    herr_t       ret_value = SUCCEED;

#ifdef PLUGIN_DEBUG
    printf("Received Attribute close call with following parameters:\n");
    if (_attr->u.attribute.attr_name) printf("  - Attribute name: %s\n", _attr->u.attribute.attr_name);
    printf("  - Attribute Domain path: %s\n", _attr->domain->u.file.filepath_name);
#endif

    assert(H5I_ATTR == _attr->obj_type && "not an attribute");

    if (_attr->u.attribute.attr_name)
        RV_free(_attr->u.attribute.attr_name);

    if (_attr->u.attribute.dtype_id >= 0 && H5Tclose(_attr->u.attribute.dtype_id) < 0)
        FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTCLOSEOBJ, FAIL, "can't close datatype")
    if (_attr->u.attribute.space_id >= 0 && H5Sclose(_attr->u.attribute.space_id) < 0)
        FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTCLOSEOBJ, FAIL, "can't close dataspace")

    if (_attr->u.attribute.acpl_id >= 0) {
        if (_attr->u.attribute.acpl_id != H5P_ATTRIBUTE_CREATE_DEFAULT && H5Pclose(_attr->u.attribute.acpl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close ACPL")
    } /* end if */

    RV_free(_attr);

    return ret_value;
} /* end RV_attr_close() */


/*-------------------------------------------------------------------------
 * Function:    RV_datatype_commit
 *
 * Purpose:     Commits the given HDF5 datatype into the file structure of
 *              the given HDF5 file object and allocates an internal memory
 *              struct object for the datatype.
 *
 * Return:      Pointer to RV_object_t struct corresponding to the
 *              newly-committed datatype on success/NULL on failure
 *
 * Programmer:  Jordan Henderson
 *              August, 2017
 */
static void *
RV_datatype_commit(void *obj, H5VL_loc_params_t H5_ATTR_UNUSED loc_params, const char *name, hid_t type_id,
                   hid_t lcpl_id, hid_t tcpl_id, hid_t tapl_id, hid_t dxpl_id, void H5_ATTR_UNUSED **req)
{
    RV_object_t *parent = (RV_object_t *) obj;
    RV_object_t *new_datatype = NULL;
    curl_off_t   commit_request_len = 0;
    size_t       commit_request_nalloc = 0;
    size_t       host_header_len = 0;
    size_t       datatype_body_len = 0;
    size_t       link_body_len = 0;
    char        *host_header = NULL;
    char        *commit_request_body = NULL;
    char        *datatype_body = NULL;
    char        *link_body = NULL;
    char        *path_dirname = NULL;
    char         request_url[URL_MAX_LENGTH];
    void        *ret_value = NULL;

#ifdef PLUGIN_DEBUG
    printf("Received Datatype commit call with following parameters:\n");
    printf("  - Name: %s\n", name);
    printf("  - Type ID: %ld\n", type_id);
    printf("  - LCPL: %ld\n", lcpl_id);
    printf("  - TCPL: %ld\n", tcpl_id);
    printf("  - TAPL: %ld\n", tapl_id);
    printf("  - DXPL: %ld\n", dxpl_id);
    printf("  - Parent Object URI: %s\n", parent->URI);
    printf("  - Parent Object type: %d\n\n", parent->obj_type);
#endif

    assert((H5I_FILE == parent->obj_type || H5I_GROUP == parent->obj_type)
          && "parent object not a file or group");

    /* Check for write access */
    if (!(parent->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, NULL, "no write intent on file")

    /* Allocate and setup internal Datatype struct */
    if (NULL == (new_datatype = (RV_object_t *) RV_malloc(sizeof(*new_datatype))))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, NULL, "can't allocate datatype object")

    new_datatype->obj_type = H5I_DATATYPE;
    new_datatype->domain = parent->domain; /* Store pointer to file that the newly-committed datatype is in */
    new_datatype->u.datatype.dtype_id = FAIL;
    new_datatype->u.datatype.tcpl_id = FAIL;

    /* Copy the TCPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * H5Tget_create_plist() will function correctly
     */
    if (H5P_DATATYPE_CREATE_DEFAULT != tcpl_id) {
        if ((new_datatype->u.datatype.tcpl_id = H5Pcopy(tcpl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy TCPL")
    } /* end if */
    else
        new_datatype->u.datatype.tcpl_id = H5P_DATATYPE_CREATE_DEFAULT;

    /* Convert the datatype into JSON to be used in the request body */
    if (RV_convert_datatype_to_string(type_id, &datatype_body, &datatype_body_len, FALSE) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, NULL, "can't convert datatype to string representation")

    /* If this is not a H5Tcommit_anon call, create a link for the Datatype
     * to link it into the file structure */
    if (name) {
        hbool_t            empty_dirname;
        char               target_URI[URI_MAX_LENGTH];
        const char * const link_basename = RV_basename(name);
        const char * const link_body_format = "\"link\": {"
                                                  "\"id\": \"%s\", "
                                                  "\"name\": \"%s\""
                                              "}";

        /* In case the user specified a path which contains multiple groups on the way to the
         * one which the datatype will ultimately be linked under, extract out the path to the
         * final group in the chain */
        if (NULL == (path_dirname = RV_dirname(name)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, NULL, "invalid pathname for datatype link")
        empty_dirname = !strcmp(path_dirname, "");

#ifdef PLUGIN_DEBUG
        printf("  - Datatype path dirname is: %s\n\n", path_dirname);
#endif

        /* If the path to the final group in the chain wasn't empty, get the URI of the final
         * group in order to correctly link the datatype into the file structure. Otherwise,
         * the supplied parent group is the one housing the datatype, so just use its URI.
         */
        if (!empty_dirname) {
            H5I_type_t obj_type = H5I_GROUP;
            htri_t     search_ret;

            search_ret = RV_find_object_by_path(parent, path_dirname, &obj_type, RV_copy_object_URI_callback, NULL, target_URI);
            if (!search_ret || search_ret < 0)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_PATH, NULL, "can't locate target for dataset link")
        } /* end if */

        link_body_len = strlen(link_body_format) + strlen(link_basename) + (empty_dirname ? strlen(parent->URI) : strlen(target_URI)) + 1;
        if (NULL == (link_body = (char *) RV_malloc(link_body_len)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, NULL, "can't allocate space for datatype link body")

        /* Form the Datatype Commit Link portion of the commit request using the above format
         * specifier and the corresponding arguments */
        snprintf(link_body, link_body_len, link_body_format, empty_dirname ? parent->URI : target_URI, link_basename);
    } /* end if */

    /* Form the request body to commit the Datatype */
    commit_request_nalloc = datatype_body_len + (link_body ? link_body_len + 2 : 0) + 3;
    if (NULL == (commit_request_body = (char *) RV_malloc(commit_request_nalloc)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, NULL, "can't allocate space for datatype commit request body")

    if ((commit_request_len = snprintf(commit_request_body, commit_request_nalloc,
             "{%s%s%s}",
             datatype_body,
             link_body ? ", " : "",
             link_body ? link_body : "")
        ) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, NULL, "snprintf error")

    /* Setup the "Host: " header */
    host_header_len = strlen(parent->domain->u.file.filepath_name) + strlen(host_string) + 1;
    if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, NULL, "can't allocate space for request Host header")

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, parent->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    /* Instruct cURL that we are sending JSON */
    curl_headers = curl_slist_append(curl_headers, "Content-Type: application/json");

    /* Redirect cURL from the base URL to "/datatypes" to commit the datatype */
    snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes", base_URL);

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTSET, NULL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POST, 1))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTSET, NULL, "can't set up cURL to make HTTP POST request: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDS, commit_request_body))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTSET, NULL, "can't set cURL POST data: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, commit_request_len))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTSET, NULL, "can't set cURL POST data size: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTSET, NULL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
    printf("  - Committing datatype\n\n");

    printf("   /********************************\\\n");
    printf("-> | Making a request to the server |\n");
    printf("   \\********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_DATATYPE, H5E_BADVALUE, NULL);

    /* Store the newly-committed Datatype's URI */
    if (RV_parse_response(response_buffer.buffer, NULL, new_datatype->URI, RV_copy_object_URI_callback) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, NULL, "can't parse committed datatype's URI")

    ret_value = (void *) new_datatype;

done:
#ifdef PLUGIN_DEBUG
    printf("Datatype commit URL: %s\n\n", request_url);
    printf("Datatype commit request body: %s\n\n", commit_request_body);
    printf("Datatype commit response buffer: %s\n\n", response_buffer.buffer);

    if (new_datatype) {
        printf("Datatype H5VL_rest_object_t fields:\n");
        printf("  - Datatype URI: %s\n", new_datatype->URI);
        printf("  - Datatype Object type: %d\n", new_datatype->obj_type);
        printf("  - Datatype Parent Domain path: %s\n\n", new_datatype->domain->u.file.filepath_name);
    }
#endif

    if (path_dirname)
        RV_free(path_dirname);
    if (commit_request_body)
        RV_free(commit_request_body);
    if (host_header)
        RV_free(host_header);
    if (datatype_body)
        RV_free(datatype_body);
    if (link_body)
        RV_free(link_body);

    /* Clean up allocated datatype object if there was an issue */
    if (new_datatype && !ret_value)
        if (RV_datatype_close(new_datatype, FAIL, NULL) < 0)
            FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, NULL, "can't close datatype")

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    return ret_value;
} /* end RV_datatype_commit() */


/*-------------------------------------------------------------------------
 * Function:    RV_datatype_open
 *
 * Purpose:     Opens an existing HDF5 committed datatype by retrieving its
 *              URI and datatype info from the server and setting up an
 *              internal memory struct object for the datatype.
 *
 * Return:      Pointer to RV_object_t struct corresponding to the opened
 *              committed datatype on success/NULL on failure
 *
 * Programmer:  Jordan Henderson
 *              August, 2017
 */
static void *
RV_datatype_open(void *obj, H5VL_loc_params_t H5_ATTR_UNUSED loc_params, const char *name,
                 hid_t tapl_id, hid_t dxpl_id, void H5_ATTR_UNUSED **req)
{
    RV_object_t *parent = (RV_object_t *) obj;
    RV_object_t *datatype = NULL;
    H5I_type_t   obj_type = H5I_DATATYPE;
    htri_t       search_ret;
    void        *ret_value = NULL;

#ifdef PLUGIN_DEBUG
    printf("Received Datatype open call with following parameters:\n");
    printf("  - Name: %s\n", name);
    printf("  - TAPL: %ld\n", tapl_id);
    printf("  - DXPL: %ld\n", dxpl_id);
    printf("  - Parent object type: %d\n", parent->obj_type);
#endif

    assert((H5I_FILE == parent->obj_type || H5I_GROUP == parent->obj_type)
          && "parent object not a file or group");

    /* Allocate and setup internal Datatype struct */
    if (NULL == (datatype = (RV_object_t *) RV_malloc(sizeof(*datatype))))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, NULL, "can't allocate datatype object")

    datatype->obj_type = H5I_DATATYPE;
    datatype->domain = parent->domain; /* Store pointer to file that the opened Dataset is within */
    datatype->u.datatype.dtype_id = FAIL;
    datatype->u.datatype.tcpl_id = FAIL;

    /* Locate the named Datatype */
    search_ret = RV_find_object_by_path(parent, name, &obj_type, RV_copy_object_URI_callback, NULL, datatype->URI);
    if (!search_ret || search_ret < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PATH, NULL, "can't locate datatype by path")

    /* Set up the actual datatype by converting the string representation into an hid_t */
    if ((datatype->u.datatype.dtype_id = RV_parse_datatype(response_buffer.buffer, TRUE)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, NULL, "can't parse dataset's datatype")

    /* Set up a TCPL for the datatype so that H5Tget_create_plist() will function correctly.
       Note that currently there aren't any properties that can be set for a TCPL, however
       we still use one here specifically for H5Tget_create_plist(). */
    if ((datatype->u.datatype.tcpl_id = H5Pcreate(H5P_DATATYPE_CREATE)) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "can't create TCPL for datatype")

    ret_value = (void *) datatype;

done:
#ifdef PLUGIN_DEBUG
    printf("Link access response buffer: %s\n\n", response_buffer.buffer);

    if (datatype) {
        printf("Datatype H5VL_rest_object_t fields:\n");
        printf("  - Datatype URI: %s\n", datatype->URI);
        printf("  - Datatype object type: %d\n", datatype->obj_type);
        printf("  - Datatype Parent Domain path: %s\n\n", datatype->domain->u.file.filepath_name);
    }
#endif

    /* Clean up allocated datatype object if there was an issue */
    if (datatype && !ret_value)
        if (RV_datatype_close(datatype, FAIL, NULL) < 0)
            FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, NULL, "can't close datatype")

    return ret_value;
} /* end RV_datatype_open() */


/*-------------------------------------------------------------------------
 * Function:    RV_datatype_get
 *
 * Purpose:     Performs a "GET" operation on an HDF5 committed datatype,
 *              such as calling the H5Tget_create_plist routine.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              August, 2017
 */
static herr_t
RV_datatype_get(void *obj, H5VL_datatype_get_t get_type, hid_t H5_ATTR_UNUSED dxpl_id,
                void H5_ATTR_UNUSED **req, va_list arguments)
{
    RV_object_t *dtype = (RV_object_t *) obj;
    herr_t       ret_value = SUCCEED;

#ifdef PLUGIN_DEBUG
    printf("Received Datatype get call with following parameters:\n");
    printf("  - Get Type: %d\n", get_type);
    printf("  - Datatype URI: %s\n", dtype->URI);
    printf("  - Datatype File: %s\n\n", dtype->domain->u.file.filepath_name);
#endif

    assert(H5I_DATATYPE == dtype->obj_type && "not a datatype");

    switch (get_type) {
        case H5VL_DATATYPE_GET_BINARY:
        {
            ssize_t *nalloc = va_arg(arguments, ssize_t *);
            void    *buf = va_arg(arguments, void *);
            size_t   size = va_arg(arguments, size_t);

            if (H5Tencode(dtype->u.datatype.dtype_id, buf, &size) < 0)
                FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "can't determine serialized length of datatype")

            *nalloc = (ssize_t) size;

            break;
        } /* H5VL_DATATYPE_GET_BINARY */

        /* H5Tget_create_plist */
        case H5VL_DATATYPE_GET_TCPL:
        {
            hid_t *plist_id = va_arg(arguments, hid_t *);

            /* Retrieve the datatype's creation property list */
            if((*plist_id = H5Pcopy(dtype->u.datatype.tcpl_id)) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get datatype creation property list")

            break;
        } /* H5VL_DATATYPE_GET_TCPL */

        default:
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get this type of information from datatype")
    } /* end switch */

done:
    return ret_value;
} /* end RV_datatype_get() */


/*-------------------------------------------------------------------------
 * Function:    RV_datatype_close
 *
 * Purpose:     Closes an HDF5 committed datatype by freeing the memory
 *              allocated for its associated internal memory struct object.
 *              There is no interaction with the server, whose state is
 *              unchanged.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              August, 2017
 */
static herr_t
RV_datatype_close(void *dt, hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req)
{
    RV_object_t *_dtype = (RV_object_t *) dt;
    herr_t       ret_value = SUCCEED;

#ifdef PLUGIN_DEBUG
    printf("Received Datatype close call with following parameters:\n");
    printf("  - URI: %s\n\n", _dtype->URI);
#endif

    assert(H5I_DATATYPE == _dtype->obj_type && "not a datatype");

    if (_dtype->u.datatype.dtype_id >= 0 && H5Tclose(_dtype->u.datatype.dtype_id) < 0)
        FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, FAIL, "can't close datatype")

    if (_dtype->u.datatype.tcpl_id >= 0) {
        if (_dtype->u.datatype.tcpl_id != H5P_DATATYPE_CREATE_DEFAULT && H5Pclose(_dtype->u.datatype.tcpl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close TCPL")
    } /* end if */

    RV_free(_dtype);

    return ret_value;
} /* end RV_datatype_close() */


/*-------------------------------------------------------------------------
 * Function:    RV_dataset_create
 *
 * Purpose:     Creates an HDF5 dataset by making the appropriate REST API
 *              call to the server and allocating an internal memory struct
 *              object for the dataset.
 *
 * Return:      Pointer to an RV_object_t struct corresponding to the
 *              newly-created dataset on success/NULL on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static void *
RV_dataset_create(void *obj, H5VL_loc_params_t H5_ATTR_UNUSED loc_params, const char *name, hid_t dcpl_id,
                  hid_t dapl_id, hid_t dxpl_id, void H5_ATTR_UNUSED **req)
{
    RV_object_t *parent = (RV_object_t *) obj;
    RV_object_t *new_dataset = NULL;
    curl_off_t   create_request_body_len = 0;
    size_t       host_header_len = 0;
    hid_t        space_id, type_id;
    char        *host_header = NULL;
    char        *create_request_body = NULL;
    char         request_url[URL_MAX_LENGTH];
    void        *ret_value = NULL;

#ifdef PLUGIN_DEBUG
    printf("Received Dataset create call with following parameters:\n");
    printf("  - Name: %s\n", name);
    printf("  - DCPL: %ld\n", dcpl_id);
    printf("  - DAPL: %ld\n", dapl_id);
    printf("  - DXPL: %ld\n", dxpl_id);
    printf("  - Parent Object URI: %s\n", parent->URI);
    printf("  - Parent Object Type: %d\n\n", parent->obj_type);
#endif

    assert((H5I_FILE == parent->obj_type || H5I_GROUP == parent->obj_type)
          && "parent object not a file or group");

    /* Check for write access */
    if (!(parent->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, NULL, "no write intent on file")

    /* Allocate and setup internal Dataset struct */
    if (NULL == (new_dataset = (RV_object_t *) RV_malloc(sizeof(*new_dataset))))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, NULL, "can't allocate dataset object")

    new_dataset->obj_type = H5I_DATASET;
    new_dataset->domain = parent->domain; /* Store pointer to file that the newly-created dataset is in */
    new_dataset->u.dataset.dtype_id = FAIL;
    new_dataset->u.dataset.space_id = FAIL;
    new_dataset->u.dataset.dapl_id = FAIL;
    new_dataset->u.dataset.dcpl_id = FAIL;

    /* Copy the DAPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * H5Dget_access_plist() will function correctly
     */
    if (H5P_DATASET_ACCESS_DEFAULT != dapl_id) {
        if ((new_dataset->u.dataset.dapl_id = H5Pcopy(dapl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy DAPL")
    } /* end if */
    else
        new_dataset->u.dataset.dapl_id = H5P_DATASET_ACCESS_DEFAULT;

    /* Copy the DCPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * H5Dget_create_plist() will function correctly
     */
    if (H5P_DATASET_CREATE_DEFAULT != dcpl_id) {
        if ((new_dataset->u.dataset.dcpl_id = H5Pcopy(dcpl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy DCPL")
    } /* end if */
    else
        new_dataset->u.dataset.dcpl_id = H5P_DATASET_CREATE_DEFAULT;

    /* Form the request body to give the new Dataset its properties */
    {
        size_t tmp_len = 0;

        if (RV_setup_dataset_create_request_body(obj, name, dcpl_id, &create_request_body, &tmp_len) < 0)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCREATE, NULL, "can't parse dataset creation parameters")

        /* XXX: unsafe cast */
        create_request_body_len = (curl_off_t) tmp_len;
    }

    /* Setup the "Host: " header */
    host_header_len = strlen(parent->domain->u.file.filepath_name) + strlen(host_string) + 1;
    if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, NULL, "can't allocate space for request Host header")

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, parent->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    /* Instruct cURL that we are sending JSON */
    curl_headers = curl_slist_append(curl_headers, "Content-Type: application/json");

    /* Redirect cURL from the base URL to "/datasets" to create the dataset */
    snprintf(request_url, URL_MAX_LENGTH, "%s/datasets", base_URL);

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, NULL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POST, 1))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, NULL, "can't set up cURL to make HTTP POST request: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDS, create_request_body))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, NULL, "can't set cURL POST data: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, create_request_body_len))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, NULL, "can't set cURL POST data size: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, NULL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
    printf("  - Creating dataset\n\n");

    printf("   /********************************\\\n");
    printf("-> | Making a request to the server |\n");
    printf("   \\********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_DATASET, H5E_CANTCREATE, NULL);

    /* Store the newly-created dataset's URI */
    if (RV_parse_response(response_buffer.buffer, NULL, new_dataset->URI, RV_copy_object_URI_callback) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCREATE, NULL, "can't parse new dataset's URI")

    if (H5Pget(dcpl_id, H5VL_PROP_DSET_TYPE_ID, &type_id) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, NULL, "can't get property list value for dataset's datatype ID")
    if (H5Pget(dcpl_id, H5VL_PROP_DSET_SPACE_ID, &space_id) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, NULL, "can't get property list value for dataset's dataspace ID")

    if ((new_dataset->u.dataset.dtype_id = H5Tcopy(type_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCOPY, NULL, "failed to copy datatype")
    if ((new_dataset->u.dataset.space_id = H5Scopy(space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCOPY, NULL, "failed to copy dataspace")

    ret_value = (void *) new_dataset;

done:
#ifdef PLUGIN_DEBUG
    printf("Dataset create URL: %s\n\n", request_url);
    printf("Dataset create body: %s\n\n", create_request_body);
    printf("Dataset create body len: %ld\n\n", create_request_body_len);
    printf("Dataset Create response buffer: %s\n\n", response_buffer.buffer);

    if (new_dataset) {
        printf("Dataset H5VL_rest_object_t fields:\n");
        printf("  - Dataset URI: %s\n", new_dataset->URI);
        printf("  - Dataset Object type: %d\n", new_dataset->obj_type);
        printf("  - Dataset Parent Domain path: %s\n\n", new_dataset->domain->u.file.filepath_name);
    }
#endif

    if (create_request_body)
        RV_free(create_request_body);
    if (host_header)
        RV_free(host_header);

    /* Clean up allocated dataset object if there was an issue */
    if (new_dataset && !ret_value)
        if (RV_dataset_close(new_dataset, FAIL, NULL) < 0)
            FUNC_DONE_ERROR(H5E_DATASET, H5E_CANTCLOSEOBJ, NULL, "can't close dataset")

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    return ret_value;
} /* end RV_dataset_create() */


/*-------------------------------------------------------------------------
 * Function:    RV_dataset_open
 *
 * Purpose:     Opens an existing HDF5 dataset by retrieving its URI,
 *              dataspace and datatype info from the server and allocating
 *              an internal memory struct object for the dataset.
 *
 * Return:      Pointer to an RV_object_t struct corresponding to
 *              the opened dataset on success/NULL on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static void *
RV_dataset_open(void *obj, H5VL_loc_params_t H5_ATTR_UNUSED loc_params, const char *name,
                hid_t dapl_id, hid_t dxpl_id, void H5_ATTR_UNUSED **req)
{
    RV_object_t *parent = (RV_object_t *) obj;
    RV_object_t *dataset = NULL;
    H5I_type_t   obj_type = H5I_DATASET;
    htri_t       search_ret;
    void        *ret_value = NULL;

#ifdef PLUGIN_DEBUG
    printf("Received Dataset open call with following parameters:\n");
    printf("  - Name: %s\n", name);
    printf("  - DAPL: %ld\n", dapl_id);
    printf("  - DXPL: %ld\n", dxpl_id);
    printf("  - Parent Object Type: %d\n", parent->obj_type);
#endif

    assert((H5I_FILE == parent->obj_type || H5I_GROUP == parent->obj_type)
          && "parent object not a file or group");

    /* Allocate and setup internal Dataset struct */
    if (NULL == (dataset = (RV_object_t *) RV_malloc(sizeof(*dataset))))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, NULL, "can't allocate dataset object")

    dataset->obj_type = H5I_DATASET;
    dataset->domain = parent->domain; /* Store pointer to file that the opened Dataset is within */
    dataset->u.dataset.dtype_id = FAIL;
    dataset->u.dataset.space_id = FAIL;
    dataset->u.dataset.dapl_id = FAIL;
    dataset->u.dataset.dcpl_id = FAIL;

    /* Locate the Dataset */
    search_ret = RV_find_object_by_path(parent, name, &obj_type, RV_copy_object_URI_callback, NULL, dataset->URI);
    if (!search_ret || search_ret < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_PATH, NULL, "can't locate dataset by path")

    /* Set up a Dataspace for the opened Dataset */
    if ((dataset->u.dataset.space_id = RV_parse_dataspace(response_buffer.buffer)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, NULL, "can't parse dataset dataspace")

    /* Set up a Datatype for the opened Dataset */
    if ((dataset->u.dataset.dtype_id = RV_parse_datatype(response_buffer.buffer, TRUE)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, NULL, "can't parse dataset datatype")

    /* Set up a DAPL for the dataset so that H5Dget_access_plist() will function correctly */
    if ((dataset->u.dataset.dapl_id = H5Pcreate(H5P_DATASET_ACCESS)) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "can't create DAPL for dataset")

    /* Set up a DCPL for the dataset so that H5Dget_create_plist() will function correctly */
    if ((dataset->u.dataset.dcpl_id = H5Pcreate(H5P_DATASET_CREATE)) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "can't create DCPL for dataset")

    /* Set any necessary creation properties on the DCPL setup for the dataset */
    if (RV_parse_response(response_buffer.buffer, NULL, &dataset->u.dataset.dcpl_id, RV_parse_dataset_creation_properties_callback) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "can't parse dataset's creation properties")

    ret_value = (void *) dataset;

done:
#ifdef PLUGIN_DEBUG
    printf("Link access response buffer: %s\n\n", response_buffer.buffer);

    if (dataset) {
        printf("Dataset H5VL_rest_object_t fields:\n");
        printf("  - Dataset URI: %s\n", dataset->URI);
        printf("  - Dataset Object type: %d\n", dataset->obj_type);
        printf("  - Dataset Parent Domain path: %s\n\n", dataset->domain->u.file.filepath_name);
    }
#endif

    /* Clean up allocated dataset object if there was an issue */
    if (dataset && !ret_value)
        if (RV_dataset_close(dataset, FAIL, NULL) < 0)
            FUNC_DONE_ERROR(H5E_DATASET, H5E_CANTCLOSEOBJ, NULL, "can't close dataset")

    return ret_value;
} /* end RV_dataset_open() */


/*-------------------------------------------------------------------------
 * Function:    RV_dataset_read
 *
 * Purpose:     Reads data from an HDF5 dataset according to the given
 *              memory dataspace by making the appropriate REST API call to
 *              the server.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
RV_dataset_read(void *obj, hid_t mem_type_id, hid_t mem_space_id,
                hid_t file_space_id, hid_t dxpl_id, void *buf, void H5_ATTR_UNUSED **req)
{
    H5S_sel_type  sel_type = H5S_SEL_ALL;
    RV_object_t  *dataset = (RV_object_t *) obj;
    H5T_class_t   dtype_class;
    hssize_t      mem_select_npoints, file_select_npoints;
    hbool_t       is_transfer_binary = FALSE;
    htri_t        is_variable_str;
    size_t        read_data_size;
    size_t        selection_body_len = 0;
    size_t        host_header_len = 0;
    char         *host_header = NULL;
    char         *selection_body = NULL;
    void         *obj_ref_buf = NULL;
    char          request_url[URL_MAX_LENGTH];
    herr_t        ret_value = SUCCEED;

    assert(buf);
    assert(H5I_DATASET == dataset->obj_type && "not a dataset");

#ifdef PLUGIN_DEBUG
    printf("Received Dataset read call with following parameters:\n");
    printf("  - Dataset URI: %s\n", dataset->URI);
    printf("  - mem_type_id: %ld\n", mem_space_id);
    printf("  - is all mem: %s\n", (mem_space_id == H5S_ALL) ? "yes" : "no");
    printf("  - file_space_id: %ld\n", file_space_id);
    printf("  - is all file: %s\n", (file_space_id == H5S_ALL) ? "yes" : "no");
    printf("  - DXPL ID: %ld\n", dxpl_id);
    printf("  - is default dxpl: %s\n", (dxpl_id == H5P_DATASET_XFER_DEFAULT) ? "true" : "false");
#endif

    /* Follow the semantics for the use of H5S_ALL */
    if (H5S_ALL == mem_space_id && H5S_ALL == file_space_id) {
        /* The file dataset's dataspace is used for the memory dataspace
         * and the selection within the memory dataspace is set to the
         * "all" selection. The selection within the file dataset's
         * dataspace is set to the "all" selection.
         */
        mem_space_id = file_space_id = dataset->u.dataset.space_id;
        H5Sselect_all(file_space_id);
    } /* end if */
    else if (H5S_ALL == file_space_id) {
        /* mem_space_id specifies the memory dataspace and the selection
         * within it. The selection within the file dataset's dataspace
         * is set to the "all" selection.
         */
        file_space_id = dataset->u.dataset.space_id;
        H5Sselect_all(file_space_id);
    } /* end if */
    else {
        /* The file dataset's dataspace is used for the memory dataspace
         * and the selection specified with file_space_id specifies the
         * selection within it. The combination of the file dataset's
         * dataspace and the selection from file_space_id is used for
         * memory also.
         */
        if (H5S_ALL == mem_space_id) {
            mem_space_id = dataset->u.dataset.space_id;

            /* Copy the selection from file_space_id into the mem_space_id. */
            if (H5Sselect_copy(mem_space_id, file_space_id, FALSE) < 0)
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCOPY, FAIL, "can't copy selection from file space to memory space")
        } /* end if */

        /* Since the selection in the dataset's file dataspace is not set
         * to "all", convert the selection into JSON */

        /* Retrieve the selection type to choose how to format the dataspace selection */
        if (H5S_SEL_ERROR == (sel_type = H5Sget_select_type(file_space_id)))
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't get dataspace selection type")

        if (RV_convert_dataspace_selection_to_string(file_space_id, &selection_body, &selection_body_len,
                (H5S_SEL_POINTS == sel_type) ? FALSE : TRUE) < 0)
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "can't convert dataspace selection to string representation")
    } /* end else */

    /* Verify that the number of selected points matches */
    if ((mem_select_npoints = H5Sget_select_npoints(mem_space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "invalid dataspace")
    if ((file_select_npoints = H5Sget_select_npoints(file_space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "invalid dataspace")
    assert((mem_select_npoints == file_select_npoints) && "memory selection num points != file selection num points");


    /* Determine whether it's possible to send the data as a binary blob instead of a JSON array */
    if (H5T_NO_CLASS == (dtype_class = H5Tget_class(mem_type_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid datatype")

    if ((is_variable_str = H5Tis_variable_str(mem_type_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid datatype")

    /* Only perform a binary transfer for fixed-length datatype datasets with an
     * All or Hyperslab selection. Point selections are dealt with by POSTing the
     * point list as JSON in the request body.
     */
    is_transfer_binary = (H5T_VLEN != dtype_class) && !is_variable_str;

    /* Setup the "Host: " header */
    host_header_len = strlen(dataset->domain->u.file.filepath_name) + strlen(host_string) + 1;
    if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, dataset->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    /* Instruct cURL on which type of transfer to perform, binary or JSON */
    curl_headers = curl_slist_append(curl_headers, is_transfer_binary ? "Accept: application/octet-stream" : "Accept: application/json");

    /* Redirect cURL from the base URL to "/datasets/<id>/value" to get the dataset data values */
    snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s/value%s%s",
                                          base_URL,
                                          dataset->URI,
                                          is_transfer_binary && selection_body && (H5S_SEL_POINTS != sel_type) ? "?select=" : "",
                                          is_transfer_binary && selection_body && (H5S_SEL_POINTS != sel_type) ? selection_body : "");

    /* If using a point selection, instruct cURL to perform a POST request
     * in order to post the point list. Otherwise, a simple GET request
     * can be made, where the selection body should have already been
     * added as a request parameter to the GET URL.
     */
    if (H5S_SEL_POINTS == sel_type) {
        curl_off_t write_len;

        /* As the dataspace-selection-to-string function is not designed to include the enclosing '{' and '}',
         * since returning just the selection string to the user makes more sense if they are including more
         * elements in their JSON, we have to wrap the selection body here before sending it off to cURL
         */

        /* Ensure we have enough space to add the enclosing '{' and '}' */
        if (NULL == (selection_body = (char *) RV_realloc(selection_body, selection_body_len + 3)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL, "can't reallocate space for point selection body")

        /* Shift the whole string down by a byte */
        memmove(selection_body + 1, selection_body, selection_body_len + 1);

        /* Add in the braces */
        selection_body[0] = '{'; selection_body[selection_body_len + 1] = '}';
        selection_body[selection_body_len + 2] = '\0';

        write_len = (curl_off_t) (selection_body_len + 2); /* XXX: unsafe cast */

#ifdef PLUGIN_DEBUG
        printf("Point sel list after shifting: %s\n\n", selection_body);
#endif

        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POST, 1))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP POST request: %s", curl_err_buf)
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDS, selection_body))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL POST data: %s", curl_err_buf)
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, write_len))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL POST data size: %s", curl_err_buf)

        curl_headers = curl_slist_append(curl_headers, "Content-Type: application/json");
    } /* end if */
    else {
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP GET request: %s", curl_err_buf)
    } /* end else */

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
    printf("  - Reading dataset\n\n");

    printf("   /********************************\\\n");
    printf("-> | Making a request to the server |\n");
    printf("   \\********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_DATASET, H5E_READERROR, FAIL);

    if ((H5T_REFERENCE != dtype_class) && (H5T_VLEN != dtype_class) && !is_variable_str) {
        size_t dtype_size;

        if (0 == (dtype_size = H5Tget_size(mem_type_id)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid datatype")

        /* Scatter the read data out to the supplied read buffer according to the mem_type_id
         * and mem_space_id given */
        read_data_size = (size_t) file_select_npoints * dtype_size;
        if (H5Dscatter(dataset_read_scatter_op, &read_data_size, mem_type_id, mem_space_id, buf) < 0)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_READERROR, FAIL, "can't scatter data to read buffer")
    } /* end if */
    else {
        if (H5T_STD_REF_OBJ == mem_type_id) {
            /* Convert the received binary buffer into a buffer of rest_obj_ref_t's */
            if (RV_convert_buffer_to_obj_refs(response_buffer.buffer, (size_t) file_select_npoints,
                    (rv_obj_ref_t **) &obj_ref_buf, &read_data_size) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL, "can't convert ref string/s to object ref array")

            memcpy(buf, obj_ref_buf, read_data_size);
        } /* end if */
    } /* end else */

done:
#ifdef PLUGIN_DEBUG
    printf("Dataset read URL: %s\n\n", request_url);
    printf("Dataset read selection body: %s\n\n", selection_body);
    printf("Dataset read response buffer: %s\n\n", response_buffer.buffer);
#endif

    if (obj_ref_buf)
        RV_free(obj_ref_buf);
    if (host_header)
        RV_free(host_header);
    if (selection_body)
        RV_free(selection_body);

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    return ret_value;
} /* end RV_dataset_read() */


/*-------------------------------------------------------------------------
 * Function:    RV_dataset_write
 *
 * Purpose:     Writes data to an HDF5 dataset according to the given
 *              memory dataspace by making the appropriate REST API call to
 *              the server.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
RV_dataset_write(void *obj, hid_t mem_type_id, hid_t mem_space_id,
                 hid_t file_space_id, hid_t dxpl_id, const void *buf, void H5_ATTR_UNUSED **req)
{
    H5S_sel_type  sel_type = H5S_SEL_ALL;
    RV_object_t  *dataset = (RV_object_t *) obj;
    H5T_class_t   dtype_class;
    hssize_t      mem_select_npoints, file_select_npoints;
    hbool_t       is_transfer_binary = FALSE;
    htri_t        is_variable_str;
    size_t        host_header_len = 0;
    size_t        write_body_len = 0;
    char         *selection_body = NULL;
    char         *host_header = NULL;
    char         *write_body = NULL;
    char          request_url[URL_MAX_LENGTH];
    herr_t        ret_value = SUCCEED;

    assert(buf);
    assert(H5I_DATASET == dataset->obj_type && "not a dataset");

    /* Check for write access */
    if (!(dataset->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "no write intent on file")

#ifdef PLUGIN_DEBUG
    printf("Received Dataset write call with following parameters:\n");
    printf("  - Dataset URI: %s\n", dataset->URI);
    printf("  - mem_space_id: %ld\n", mem_space_id);
    printf("  - is all mem: %s\n", (mem_space_id == H5S_ALL) ? "true" : "false");
    printf("  - file_space_id: %ld\n", file_space_id);
    printf("  - is all file: %s\n", (file_space_id == H5S_ALL) ? "true" : "false");
    printf("  - DXPL ID: %ld\n", dxpl_id);
    printf("  - is default dxpl: %s\n", (dxpl_id == H5P_DATASET_XFER_DEFAULT) ? "true" : "false");
#endif

    /* Determine whether it's possible to send the data as a binary blob instead of as JSON */
    if (H5T_NO_CLASS == (dtype_class = H5Tget_class(mem_type_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid datatype")

    if ((is_variable_str = H5Tis_variable_str(mem_type_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid datatype")

    is_transfer_binary = (H5T_VLEN != dtype_class) && !is_variable_str;

    /* Follow the semantics for the use of H5S_ALL */
    if (H5S_ALL == mem_space_id && H5S_ALL == file_space_id) {
        /* The file dataset's dataspace is used for the memory dataspace
         * and the selection within the memory dataspace is set to the
         * "all" selection. The selection within the file dataset's
         * dataspace is set to the "all" selection.
         */
        mem_space_id = file_space_id = dataset->u.dataset.space_id;
        H5Sselect_all(file_space_id);
    } /* end if */
    else if (H5S_ALL == file_space_id) {
        /* mem_space_id specifies the memory dataspace and the selection
         * within it. The selection within the file dataset's dataspace
         * is set to the "all" selection.
         */
        file_space_id = dataset->u.dataset.space_id;
        H5Sselect_all(file_space_id);
    } /* end if */
    else {
        /* The file dataset's dataspace is used for the memory dataspace
         * and the selection specified with file_space_id specifies the
         * selection within it. The combination of the file dataset's
         * dataspace and the selection from file_space_id is used for
         * memory also.
         */
        if (H5S_ALL == mem_space_id) {
            mem_space_id = dataset->u.dataset.space_id;

            /* Copy the selection from file_space_id into the mem_space_id */
            if (H5Sselect_copy(mem_space_id, file_space_id, FALSE) < 0)
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCOPY, FAIL, "can't copy selection from file space to memory space")
        } /* end if */

        /* Since the selection in the dataset's file dataspace is not set
         * to "all", convert the selection into JSON */

        /* Retrieve the selection type here for later use */
        if (H5S_SEL_ERROR == (sel_type = H5Sget_select_type(file_space_id)))
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't get dataspace selection type")

        if (RV_convert_dataspace_selection_to_string(file_space_id, &selection_body, NULL, is_transfer_binary) < 0)
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "can't convert dataspace to string representation")
    } /* end else */

    /* Verify that the number of selected points matches */
    if ((mem_select_npoints = H5Sget_select_npoints(mem_space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "invalid dataspace")
    if ((file_select_npoints = H5Sget_select_npoints(file_space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "invalid dataspace")
    assert((mem_select_npoints == file_select_npoints) && "memory selection num points != file selection num points");

    /* Setup the size of the data being transferred and the data buffer itself (for non-simple
     * types like object references or variable length types)
     */
    if ((H5T_REFERENCE != dtype_class) && (H5T_VLEN != dtype_class) && !is_variable_str) {
        size_t dtype_size;

        if (0 == (dtype_size = H5Tget_size(mem_type_id)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid datatype")

        write_body_len = (size_t) file_select_npoints * dtype_size;
    } /* end if */
    else {
        if (H5T_STD_REF_OBJ == mem_type_id) {
            /* Convert the buffer of rest_obj_ref_t's to a binary buffer */
            if (RV_convert_obj_refs_to_buffer((const rv_obj_ref_t *) buf, (size_t) file_select_npoints, &write_body, &write_body_len) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL, "can't convert object ref/s to ref string/s")
            buf = write_body;
        } /* end if */
    } /* end else */


    /* Setup the "Host: " header */
    host_header_len = strlen(dataset->domain->u.file.filepath_name) + strlen(host_string) + 1;
    if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, dataset->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    /* Instruct cURL on which type of transfer to perform, binary or JSON */
    curl_headers = curl_slist_append(curl_headers, is_transfer_binary ? "Content-Type: application/octet-stream" : "Content-Type: application/json");

    /* Redirect cURL from the base URL to "/datasets/<id>/value" to write the value out */
    snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s/value%s%s",
                                          base_URL,
                                          dataset->URI,
                                          is_transfer_binary && selection_body && (H5S_SEL_POINTS != sel_type) ? "?select=" : "",
                                          is_transfer_binary && selection_body && (H5S_SEL_POINTS != sel_type) ? selection_body : "");

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT"))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP PUT request: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDS, is_transfer_binary ? (const char *) buf : write_body))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL PUT data: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t) write_body_len)) /* XXX: unsafe cast */
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL PUT data size: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
    printf("  - Writing dataset\n\n");

    printf("   /********************************\\\n");
    printf("-> | Making a request to the server |\n");
    printf("   \\********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_DATASET, H5E_WRITEERROR, FAIL);

done:
#ifdef PLUGIN_DEBUG
    printf("Dataset write URL: %s\n\n", request_url);
    printf("Dataset write response buffer: %s\n\n", response_buffer.buffer);
#endif

    if (selection_body)
        RV_free(selection_body);
    if (write_body)
        RV_free(write_body);
    if (host_header)
        RV_free(host_header);

    /* Reset cURL custom request to prevent issues with future requests */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL))
        FUNC_DONE_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't reset cURL custom request: %s", curl_err_buf)

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    return ret_value;
} /* end RV_dataset_write() */


/*-------------------------------------------------------------------------
 * Function:    RV_dataset_get
 *
 * Purpose:     Performs a "GET" operation on an HDF5 dataset, such as
 *              calling the H5Dget_type routine.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
RV_dataset_get(void *obj, H5VL_dataset_get_t get_type, hid_t H5_ATTR_UNUSED dxpl_id,
               void H5_ATTR_UNUSED **req, va_list arguments)
{
    RV_object_t *dset = (RV_object_t *) obj;
    herr_t       ret_value = SUCCEED;

#ifdef PLUGIN_DEBUG
    printf("Received Dataset get call with following parameters:\n");
    printf("  - Get Type: %d\n", get_type);
    printf("  - Dataset URI: %s\n", dset->URI);
    printf("  - Dataset File: %s\n\n", dset->domain->u.file.filepath_name);
#endif

    assert(H5I_DATASET == dset->obj_type && "not a dataset");

    switch (get_type) {
        /* H5Dget_access_plist */
        case H5VL_DATASET_GET_DAPL:
        {
            hid_t *ret_id = va_arg(arguments, hid_t *);

            if ((*ret_id = H5Pcopy(dset->u.dataset.dapl_id)) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, FAIL, "can't copy Dataset DAPL")

            break;
        } /* H5VL_DATASET_GET_DAPL */

        /* H5Dget_create_plist */
        case H5VL_DATASET_GET_DCPL:
        {
            hid_t *ret_id = va_arg(arguments, hid_t *);

            if ((*ret_id = H5Pcopy(dset->u.dataset.dcpl_id)) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, FAIL, "can't copy Dataset DCPL")

            break;
        } /* H5VL_DATASET_GET_DCPL */

        /* H5Dget_offset */
        case H5VL_DATASET_GET_OFFSET:
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_UNSUPPORTED, FAIL, "get dataset offset unsupported")

        /* H5Dget_space */
        case H5VL_DATASET_GET_SPACE:
        {
            hid_t *ret_id = va_arg(arguments, hid_t *);

            if ((*ret_id = H5Scopy(dset->u.dataset.space_id)) < 0)
                FUNC_GOTO_ERROR(H5E_ARGS, H5E_CANTGET, FAIL, "can't get dataspace of dataset")

            break;
        } /* H5VL_DATASET_GET_SPACE */

        /* H5Dget_space_status */
        case H5VL_DATASET_GET_SPACE_STATUS:
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_UNSUPPORTED, FAIL, "get dataset space status unsupported")

        /* H5Dget_storage_size */
        case H5VL_DATASET_GET_STORAGE_SIZE:
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_UNSUPPORTED, FAIL, "get dataset storage size unsupported")

        /* H5Dget_type */
        case H5VL_DATASET_GET_TYPE:
        {
            hid_t *ret_id = va_arg(arguments, hid_t *);

            if ((*ret_id = H5Tcopy(dset->u.dataset.dtype_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCOPY, FAIL, "can't copy dataset's datatype")

            break;
        } /* H5VL_DATASET_GET_TYPE */

        default:
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "can't get this type of information from dataset")
    } /* end switch */

done:
    return ret_value;
} /* end RV_dataset_get() */


/*-------------------------------------------------------------------------
 * Function:    RV_dataset_specific
 *
 * Purpose:     Performs a plugin-specific operation on an HDF5 dataset,
 *              such as calling the H5Dset_extent routine.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
RV_dataset_specific(void *obj, H5VL_dataset_specific_t specific_type,
                    hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req, va_list arguments)
{
    RV_object_t *dset = (RV_object_t *) obj;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    char         request_url[URL_MAX_LENGTH];
    herr_t       ret_value = SUCCEED;

#ifdef PLUGIN_DEBUG
    printf("Received Dataset-specific call with following parameters:\n");
    printf("  - Specific type: %d\n", specific_type);
    printf("  - Dataset URI: %s\n", dset->URI);
    printf("  - Dataset File: %s\n\n", dset->domain->u.file.filepath_name);
#endif

    assert(H5I_DATASET == dset->obj_type && "not a dataset");

    switch (specific_type) {
        /* H5Dset_extent */
        case H5VL_DATASET_SET_EXTENT:
            /* Check for write access */
            if (!(dset->domain->u.file.intent & H5F_ACC_RDWR))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "no write intent on file")

            FUNC_GOTO_ERROR(H5E_DATASET, H5E_UNSUPPORTED, FAIL, "set dataset extent unsupported")
            break;

        default:
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "unknown dataset operation")
    } /* end switch */

done:
    if (host_header)
        RV_free(host_header);

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    return ret_value;
} /* end RV_dataset_specific() */


/*-------------------------------------------------------------------------
 * Function:    RV_dataset_close
 *
 * Purpose:     Closes an HDF5 dataset by freeing the memory allocated for
 *              its internal memory struct object. There is no interation
 *              with the server, whose state is unchanged.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
RV_dataset_close(void *dset, hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req)
{
    RV_object_t *_dset = (RV_object_t *) dset;
    herr_t       ret_value = SUCCEED;

#ifdef PLUGIN_DEBUG
    printf("Received Dataset close call with following parameters:\n");
    printf("  - URI: %s\n\n", _dset->URI);
#endif

    assert(H5I_DATASET == _dset->obj_type && "not a dataset");

    if (_dset->u.dataset.dtype_id >= 0 && H5Tclose(_dset->u.dataset.dtype_id) < 0)
        FUNC_DONE_ERROR(H5E_DATASET, H5E_CANTCLOSEOBJ, FAIL, "can't close datatype")
    if (_dset->u.dataset.space_id >= 0 && H5Sclose(_dset->u.dataset.space_id) < 0)
        FUNC_DONE_ERROR(H5E_DATASET, H5E_CANTCLOSEOBJ, FAIL, "can't close dataspace")

    if (_dset->u.dataset.dapl_id >= 0) {
        if (_dset->u.dataset.dapl_id != H5P_DATASET_ACCESS_DEFAULT && H5Pclose(_dset->u.dataset.dapl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close DAPL")
    } /* end if */

    if (_dset->u.dataset.dcpl_id >= 0) {
        if (_dset->u.dataset.dcpl_id != H5P_DATASET_CREATE_DEFAULT && H5Pclose(_dset->u.dataset.dcpl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close DCPL")
    } /* end if */

    RV_free(_dset);

    return ret_value;
} /* end RV_dataset_close() */


/*-------------------------------------------------------------------------
 * Function:    RV_file_create
 *
 * Purpose:     Creates an HDF5 file by making the appropriate REST API
 *              call to the server and allocating an internal memory struct
 *              object for the file.
 *
 * Return:      Pointer to an RV_object_t struct corresponding to the
 *              newly-created file on success/NULL on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static void *
RV_file_create(const char *name, unsigned flags, hid_t fcpl_id, hid_t fapl_id,
               hid_t dxpl_id, void H5_ATTR_UNUSED **req)
{
    RV_object_t *new_file = NULL;
    size_t       name_length;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    void        *ret_value = NULL;

#ifdef PLUGIN_DEBUG
    printf("Received File create call with following parameters:\n");
    printf("  - Name: %s\n", name);
    printf("  - Flags: %u\n", flags);
    printf("  - FCPL: %ld\n", fcpl_id);
    printf("  - FAPL: %ld\n", fapl_id);
    printf("  - DXPL: %ld\n\n", dxpl_id);
#endif

    /* Allocate and setup internal File struct */
    if (NULL == (new_file = (RV_object_t *) RV_malloc(sizeof(*new_file))))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate file object")

    new_file->obj_type = H5I_FILE;
    new_file->u.file.intent = H5F_ACC_RDWR;
    new_file->u.file.filepath_name = NULL;
    new_file->u.file.fapl_id = FAIL;
    new_file->u.file.fcpl_id = FAIL;

    /* Copy the FAPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * H5Fget_access_plist() will function correctly. Note that due to the nature
     * of VOLs and needing to supply a FAPL to work correctly, the default case
     * should theoretically never be touched. However, it is included here for
     * the sake of completeness.
     */
    if (H5P_FILE_ACCESS_DEFAULT != fapl_id) {
        if ((new_file->u.file.fapl_id = H5Pcopy(fapl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy FAPL")
    } /* end if */
    else
        new_file->u.file.fapl_id = H5P_FILE_ACCESS_DEFAULT;

    /* Copy the FCPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * H5Fget_create_plist() will function correctly
     */
    if (H5P_FILE_CREATE_DEFAULT != fcpl_id) {
        if ((new_file->u.file.fcpl_id = H5Pcopy(fcpl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy FCPL")
    } /* end if */
    else
        new_file->u.file.fcpl_id = H5P_FILE_CREATE_DEFAULT;

    /* Store self-referential pointer in the domain field for this object
     * to simplify code for other types of objects
     */
    new_file->domain = new_file;

    /* Copy the path name into the new file object */
    name_length = strlen(name);
    if (NULL == (new_file->u.file.filepath_name = (char *) RV_malloc(name_length + 1)))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate space for filepath name")

    strncpy(new_file->u.file.filepath_name, name, name_length);
    new_file->u.file.filepath_name[name_length] = '\0';

    /* Setup the "Host: " header */
    host_header_len = name_length + strlen(host_string) + 1;
    if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate space for request Host header")

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, name, name_length));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, base_URL))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL request URL: %s", curl_err_buf)

    /* Before making the actual request, check the file creation flags for
     * the use of H5F_ACC_TRUNC. In this case, we want to check with the
     * server before trying to create a file which already exists.
     */
    if (flags & H5F_ACC_TRUNC) {
        long http_response;

        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
            FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set up cURL to make HTTP GET request: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
        printf("  - H5F_ACC_TRUNC specified; checking if file exists\n\n");

        printf("   /********************************\\\n");
        printf("-> | Making a request to the server |\n");
        printf("   \\********************************/\n\n");
#endif

        /* Note that we use the special version of CURL_PERFORM because if
         * the file doesn't exist, and the check for this throws a 404 response,
         * the standard CURL_PERFORM would fail this entire function. We don't
         * want this, we just want to get an idea of whether the file exists
         * or not.
         */
        CURL_PERFORM_NO_ERR(curl, NULL);

        if (CURLE_OK != curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_response))
            FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTGET, NULL, "can't get HTTP response code")

        /* If the file exists, go ahead and delete it before proceeding */
        if (HTTP_SUCCESS(http_response)) {
#ifdef PLUGIN_DEBUG
            printf("  - File existed and H5F_ACC_TRUNC specified; deleting file\n\n");
#endif

#ifdef PLUGIN_DEBUG
            printf("   /********************************\\\n");
            printf("-> | Making a request to the server |\n");
            printf("   \\********************************/\n\n");
#endif

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE"))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set up cURL to make HTTP DELETE request: %s", curl_err_buf)

            CURL_PERFORM(curl, H5E_FILE, H5E_CANTREMOVE, NULL);

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't reset cURL custom request: %s", curl_err_buf)
        } /* end if */
    } /* end if */

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT"))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set up cURL to make HTTP PUT request: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDS, ""))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL PUT data: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL PUT data size: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
    printf("  - Creating file\n\n");

    printf("   /********************************\\\n");
    printf("-> | Making a request to the server |\n");
    printf("   \\********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_FILE, H5E_CANTCREATE, NULL);

    /* Store the newly-created file's URI */
    if (RV_parse_response(response_buffer.buffer, NULL, new_file->URI, RV_copy_object_URI_callback) < 0)
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTCREATE, NULL, "can't parse new file's URI")

    ret_value = (void *) new_file;

done:
#ifdef PLUGIN_DEBUG
    printf("File Create response buffer: %s\n\n", response_buffer.buffer);

    if (new_file) {
        printf("File H5VL_rest_object_t fields:\n");
        printf("  - File Path Name: %s\n", new_file->domain->u.file.filepath_name);
        printf("  - File URI: %s\n", new_file->URI);
        printf("  - File Object type: %d\n\n", new_file->obj_type);
    }
#endif

    if (host_header)
        RV_free(host_header);

    /* Clean up allocated file object if there was an issue */
    if (new_file && !ret_value)
        if (RV_file_close(new_file, FAIL, NULL) < 0)
            FUNC_DONE_ERROR(H5E_FILE, H5E_CANTCLOSEOBJ, NULL, "can't close file")

    /* Reset cURL custom request to prevent issues with future requests */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL))
        FUNC_DONE_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't reset cURL custom request: %s", curl_err_buf)

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    return ret_value;
} /* end RV_file_create() */


/*-------------------------------------------------------------------------
 * Function:    RV_file_open
 *
 * Purpose:     Opens an existing HDF5 file by retrieving its URI from the
 *              server and allocating an internal memory struct object for
 *              the file.
 *
 * Return:      Pointer to an RV_object_t struct corresponding to the
 *              opened file on success/NULL on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static void *
RV_file_open(const char *name, unsigned flags, hid_t fapl_id, hid_t dxpl_id, void H5_ATTR_UNUSED **req)
{
    RV_object_t *file = NULL;
    size_t       name_length;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    void        *ret_value = NULL;

#ifdef PLUGIN_DEBUG
    printf("Received File open call with following parameters:\n");
    printf("  - Name: %s\n", name);
    printf("  - Flags: %u\n", flags);
    printf("  - FAPL: %ld\n", fapl_id);
    printf("  - DXPL: %ld\n\n", dxpl_id);
#endif

    /* Allocate and setup internal File struct */
    if (NULL == (file = (RV_object_t *) RV_malloc(sizeof(*file))))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate file object")

    file->obj_type = H5I_FILE;
    file->u.file.intent = flags;
    file->u.file.filepath_name = NULL;
    file->u.file.fapl_id = FAIL;
    file->u.file.fcpl_id = FAIL;

    /* Store self-referential pointer in the domain field for this object
     * to simplify code for other types of objects
     */
    file->domain = file;

    /* Copy the path name into the new file object */
    name_length = strlen(name);
    if (NULL == (file->u.file.filepath_name = (char *) RV_malloc(name_length + 1)))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate space for filepath name")

    strncpy(file->u.file.filepath_name, name, name_length);
    file->u.file.filepath_name[name_length] = '\0';

    /* Setup the "Host: " header */
    host_header_len = name_length + strlen(host_string) + 1;
    if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate space for request Host header")

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, name, name_length));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set up cURL to make HTTP GET request: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, base_URL))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
    printf("  - Retrieving info for File open\n\n");

    printf("   /********************************\\\n");
    printf("-> | Making a request to the server |\n");
    printf("   \\********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_FILE, H5E_CANTOPENFILE, NULL);

    /* Store the opened file's URI */
    if (RV_parse_response(response_buffer.buffer, NULL, file->URI, RV_copy_object_URI_callback) < 0)
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, NULL, "can't parse file's URI")

    /* Set up a FAPL for the file so that H5Fget_access_plist() will function correctly */
    /* XXX: Set any properties necessary */
    if ((file->u.file.fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "can't create FAPL for file")

    /* Set up a FCPL for the file so that H5Fget_create_plist() will function correctly */
    if ((file->u.file.fcpl_id = H5Pcreate(H5P_FILE_CREATE)) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "can't create FCPL for file")

    ret_value = (void *) file;

done:
#ifdef PLUGIN_DEBUG
    printf("File Open response buffer: %s\n\n", response_buffer.buffer);

    if (file) {
        printf("File H5VL_rest_object_t fields:\n");
        printf("  - File Path Name: %s\n", file->domain->u.file.filepath_name);
        printf("  - File URI: %s\n", file->URI);
        printf("  - File Object type: %d\n\n", file->obj_type);
    }
#endif

    if (host_header)
        RV_free(host_header);

    /* Clean up allocated file object if there was an issue */
    if (file && !ret_value)
        if (RV_file_close(file, FAIL, NULL) < 0)
            FUNC_DONE_ERROR(H5E_FILE, H5E_CANTCLOSEOBJ, NULL, "can't close file")

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    return ret_value;
} /* end RV_file_open() */


/*-------------------------------------------------------------------------
 * Function:    RV_file_get
 *
 * Purpose:     Performs a "GET" operation on an HDF5 file, such as calling
 *              the H5Fget_info routine.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
RV_file_get(void *obj, H5VL_file_get_t get_type, hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req, va_list arguments)
{
    RV_object_t *_obj = (RV_object_t *) obj;
    herr_t       ret_value = SUCCEED;

#ifdef PLUGIN_DEBUG
    printf("Received File get call with following parameters:\n");
    printf("  - Get Type: %d\n", get_type);
    printf("  - Obj. URI: %s\n", _obj->URI);
    printf("  - File Pathname: %s\n\n", _obj->domain->u.file.filepath_name);
#endif

    switch (get_type) {
        /* H5Fget_access_plist */
        case H5VL_FILE_GET_FAPL:
        {
            hid_t *ret_id = va_arg(arguments, hid_t *);

            if ((*ret_id = H5Pcopy(_obj->u.file.fapl_id)) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, FAIL, "can't copy File FAPL")

            break;
        } /* H5VL_FILE_GET_FAPL */

        /* H5Fget_create_plist */
        case H5VL_FILE_GET_FCPL:
        {
            hid_t *ret_id = va_arg(arguments, hid_t *);

            if ((*ret_id = H5Pcopy(_obj->u.file.fcpl_id)) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, FAIL, "can't copy File FCPL")

            break;
        } /* H5VL_FILE_GET_FCPL */

        /* H5Fget_intent */
        case H5VL_FILE_GET_INTENT:
        {
            unsigned *ret_intent = va_arg(arguments, unsigned *);

            *ret_intent = _obj->u.file.intent;

            break;
        } /* H5VL_FILE_GET_INTENT */

        /* H5Fget_name */
        case H5VL_FILE_GET_NAME:
        {
            H5I_type_t  obj_type = va_arg(arguments, H5I_type_t);
            size_t      name_buf_size = va_arg(arguments, size_t);
            char       *name_buf = va_arg(arguments, char *);
            ssize_t    *ret_size = va_arg(arguments, ssize_t *);

            *ret_size = (ssize_t) strlen(_obj->domain->u.file.filepath_name);

            if (name_buf) {
                strncpy(name_buf, _obj->u.file.filepath_name, name_buf_size - 1);
                name_buf[name_buf_size - 1] = '\0';
            } /* end if */

            break;
        } /* H5VL_FILE_GET_NAME */

        /* H5Fget_obj_count */
        case H5VL_FILE_GET_OBJ_COUNT:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "get file object count unsupported")

        /* H5Fget_obj_ids */
        case H5VL_FILE_GET_OBJ_IDS:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "get file object IDs unsupported")

        case H5VL_OBJECT_GET_FILE:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "get file unsupported")

        default:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTGET, FAIL, "can't get this type of information from file")
    } /* end switch */

done:
    return ret_value;
} /* end RV_file_get() */


/*-------------------------------------------------------------------------
 * Function:    RV_file_specific
 *
 * Purpose:     Performs a plugin-specific operation on an HDF5 file, such
 *              as calling the H5Fflush routine.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
RV_file_specific(void *obj, H5VL_file_specific_t specific_type, hid_t H5_ATTR_UNUSED dxpl_id,
                 void H5_ATTR_UNUSED **req, va_list arguments)
{
    RV_object_t *file = (RV_object_t *) obj;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    herr_t       ret_value = SUCCEED;

#ifdef PLUGIN_DEBUG
    printf("Received File-specific call with following parameters:\n");
    printf("  - Specific Type: %d\n", specific_type);
    printf("  - File URI: %s\n", file->URI);
    printf("  - File Pathname: %s\n\n", file->domain->u.file.filepath_name);
#endif

    assert(H5I_FILE == file->obj_type && "not a file");

    /* Setup the "Host: " header */
    host_header_len = strlen(file->domain->u.file.filepath_name) + strlen(host_string) + 1;
    if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, file->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, base_URL))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

    switch (specific_type) {
        case H5VL_FILE_FLUSH:
        case H5VL_FILE_IS_ACCESSIBLE:
        case H5VL_FILE_MOUNT:
        case H5VL_FILE_UNMOUNT:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "unsupported file operation")

        default:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "unknown file operation")
    } /* end switch */

done:
    if (host_header)
        RV_free(host_header);

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    return ret_value;
} /* end RV_file_specific() */


/*-------------------------------------------------------------------------
 * Function:    RV_file_optional
 *
 * Purpose:     Performs an optional operation on an HDF5 file, such as
 *              calling the H5Freopen routine.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
RV_file_optional(void *obj, hid_t dxpl_id, void H5_ATTR_UNUSED **req, va_list arguments)
{
    H5VL_file_optional_t  optional_type = (H5VL_file_optional_t) va_arg(arguments, int);
    RV_object_t          *file = (RV_object_t *) obj;
    herr_t                ret_value = SUCCEED;

    assert(H5I_FILE == file->obj_type && "not a file");

#ifdef PLUGIN_DEBUG
    printf("Received file optional call with following parameters:\n");
    printf("  - Call type: %d\n", optional_type);
    printf("  - File URI: %s\n", file->URI);
    printf("  - File Pathname: %s\n\n", file->domain->u.file.filepath_name);
#endif

    switch (optional_type) {
        case H5VL_FILE_REOPEN:
        {
            void **ret_file = va_arg(arguments, void **);

            if (NULL == (*ret_file = RV_file_open(file->u.file.filepath_name, file->u.file.intent, file->u.file.fapl_id, dxpl_id, NULL)))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTOPENOBJ, FAIL, "can't re-open file")

            break;
        } /* H5VL_FILE_REOPEN */

        case H5VL_FILE_GET_INFO:
        {
            H5I_type_t   obj_type = va_arg(arguments, H5I_type_t);
            H5F_info2_t *file_info = va_arg(arguments, H5F_info2_t *);

            /* Initialize entire struct to 0 */
            memset(file_info, 0, sizeof(*file_info));

            break;
        } /* H5VL_FILE_GET_INFO */

        case H5VL_FILE_CLEAR_ELINK_CACHE:
        case H5VL_FILE_GET_FILE_IMAGE:
        case H5VL_FILE_GET_FREE_SECTIONS:
        case H5VL_FILE_GET_FREE_SPACE:
        case H5VL_FILE_GET_MDC_CONF:
        case H5VL_FILE_GET_MDC_HR:
        case H5VL_FILE_GET_MDC_SIZE:
        case H5VL_FILE_GET_SIZE:
        case H5VL_FILE_GET_VFD_HANDLE:
        case H5VL_FILE_RESET_MDC_HIT_RATE:
        case H5VL_FILE_SET_MDC_CONFIG:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "unsupported file operation")

        default:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "unknown file operation")
    } /* end switch */

done:
    return ret_value;
} /* end RV_file_optional() */


/*-------------------------------------------------------------------------
 * Function:    RV_file_close
 *
 * Purpose:     Closes an HDF5 file by freeing the memory allocated for its
 *              associated internal memory struct object. There is no
 *              interaction with the server, whose state is unchanged.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
RV_file_close(void *file, hid_t dxpl_id, void H5_ATTR_UNUSED **req)
{
    RV_object_t *_file = (RV_object_t *) file;
    herr_t       ret_value = SUCCEED;

#ifdef PLUGIN_DEBUG
    printf("Received File close call with following parameters:\n");
    printf("  - Name: %s\n", _file->domain->u.file.filepath_name);
    printf("  - URI: %s\n", _file->URI);
    printf("  - DXPL: %ld\n\n", dxpl_id);
#endif

    assert(H5I_FILE == _file->obj_type && "not a file");

    if (_file->u.file.filepath_name)
        RV_free(_file->u.file.filepath_name);

    if (_file->u.file.fapl_id >= 0) {
        if (_file->u.file.fapl_id != H5P_FILE_ACCESS_DEFAULT && H5Pclose(_file->u.file.fapl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close FAPL")
    } /* end if */

    if (_file->u.file.fcpl_id >= 0) {
        if (_file->u.file.fcpl_id != H5P_FILE_CREATE_DEFAULT && H5Pclose(_file->u.file.fcpl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close FCPL")
    } /* end if */

    RV_free(_file);

    return ret_value;
} /* end RV_file_close() */


/*-------------------------------------------------------------------------
 * Function:    RV_group_create
 *
 * Purpose:     Creates an HDF5 Group by making the appropriate REST API
 *              call to the server and allocating an internal memory struct
 *              object for the group.
 *
 * Return:      Pointer to an RV_object_t struct corresponding to the
 *              newly-created group on success/NULL on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 *
 *              XXX: Implement the case of creating intermediate
 *              groups if this property is set in the GCPL
 */
static void *
RV_group_create(void *obj, H5VL_loc_params_t H5_ATTR_UNUSED loc_params, const char *name, hid_t gcpl_id,
                hid_t gapl_id, hid_t dxpl_id, void H5_ATTR_UNUSED **req)
{
    RV_object_t *parent = (RV_object_t *) obj;
    RV_object_t *new_group = NULL;
    const char  *path_basename = RV_basename(name);
    curl_off_t   create_request_body_len = 0;
    hbool_t      empty_dirname;
    size_t       create_request_nalloc = 0;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    char        *create_request_body = NULL;
    char        *path_dirname = NULL;
    char         target_URI[URI_MAX_LENGTH];
    char         request_url[URL_MAX_LENGTH];
    void        *ret_value = NULL;

#ifdef PLUGIN_DEBUG
    printf("Received Group create call with following parameters:\n");
    printf("  - Name: %s\n", name);
    printf("  - GCPL: %ld\n", gcpl_id);
    printf("  - GAPL: %ld\n", gapl_id);
    printf("  - DXPL: %ld\n", dxpl_id);
    printf("  - Parent Object URI: %s\n", parent->URI);
    printf("  - Parent Object Type: %d\n\n", parent->obj_type);
#endif

    assert((H5I_FILE == parent->obj_type || H5I_GROUP == parent->obj_type)
          && "parent object not a file or group");

    /* Check for write access */
    if (!(parent->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, NULL, "no write intent on file")

    /* Allocate and setup internal Group struct */
    if (NULL == (new_group = (RV_object_t *) RV_malloc(sizeof(*new_group))))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTALLOC, NULL, "can't allocate group object")

    new_group->obj_type = H5I_GROUP;
    new_group->u.group.gcpl_id = FAIL;
    new_group->domain = parent->domain; /* Store pointer to file that the newly-created group is within */

    /* Copy the GCPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * H5Gget_create_plist() will function correctly
     */
    if (H5P_GROUP_CREATE_DEFAULT != gcpl_id) {
        if ((new_group->u.group.gcpl_id = H5Pcopy(gcpl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy GCPL")
    } /* end if */
    else
        new_group->u.group.gcpl_id = H5P_GROUP_CREATE_DEFAULT;

    /* In case the user specified a path which contains multiple groups on the way to the
     * one which this group will ultimately be linked under, extract out the path to the
     * final group in the chain */
    if (NULL == (path_dirname = RV_dirname(name)))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_BADVALUE, NULL, "invalid pathname for group link")
    empty_dirname = !strcmp(path_dirname, "");

#ifdef PLUGIN_DEBUG
    printf("  - Group path dirname is: %s\n\n", path_dirname);
#endif

    /* If the path to the final group in the chain wasn't empty, get the URI of the final
     * group in order to correctly link this group into the file structure. Otherwise,
     * the supplied parent group is the one housing this group, so just use its URI.
     */
    if (!empty_dirname) {
        H5I_type_t obj_type = H5I_GROUP;
        htri_t     search_ret;

        search_ret = RV_find_object_by_path(parent, path_dirname, &obj_type, RV_copy_object_URI_callback, NULL, target_URI);
        if (!search_ret || search_ret < 0)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_PATH, NULL, "can't locate target for group link")
    } /* end if */

    {
        const char * const fmt_string = "{"
                                            "\"link\": {"
                                                "\"id\": \"%s\", "
                                                "\"name\": \"%s\""
                                            "}"
                                        "}";

        /* Form the request body to link the new group to the parent object */
        create_request_nalloc = strlen(fmt_string) + strlen(path_basename) + (empty_dirname ? strlen(parent->URI) : strlen(target_URI)) + 1;
        if (NULL == (create_request_body = (char *) RV_malloc(create_request_nalloc)))
            FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTALLOC, NULL, "can't allocate space for group create request body")

        if ((create_request_body_len = snprintf(create_request_body, create_request_nalloc, fmt_string,
                empty_dirname ? parent->URI : target_URI,
                path_basename)
            ) < 0)
            FUNC_GOTO_ERROR(H5E_SYM, H5E_SYSERRSTR, NULL, "snprintf error")
    }

    /* Setup the "Host: " header */
    host_header_len = strlen(parent->domain->u.file.filepath_name) + strlen(host_string) + 1;
    if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTALLOC, NULL, "can't allocate space for request Host header")

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, parent->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    /* Instruct cURL that we are sending JSON */
    curl_headers = curl_slist_append(curl_headers, "Content-Type: application/json");

    /* Redirect cURL from the base URL to "/groups" to create the group */
    snprintf(request_url, URL_MAX_LENGTH, "%s/groups", base_URL);

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, NULL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POST, 1))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, NULL, "can't set up cURL to make HTTP POST request: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDS, create_request_body))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, NULL, "can't set cURL POST data: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, create_request_body_len))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, NULL, "can't set cURL POST data size: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, NULL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
    printf("  - Creating group\n\n");

    printf("   /********************************\\\n");
    printf("-> | Making a request to the server |\n");
    printf("   \\********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_SYM, H5E_CANTCREATE, NULL);

    /* Store the newly-created group's URI */
    if (RV_parse_response(response_buffer.buffer, NULL, new_group->URI, RV_copy_object_URI_callback) < 0)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTCREATE, NULL, "can't parse new group's URI")

    ret_value = (void *) new_group;

done:
#ifdef PLUGIN_DEBUG
    printf("Group create URL: %s\n\n", request_url);
    printf("Group create body: %s\n\n", create_request_body);
    printf("Group create body len: %ld\n\n", create_request_body_len);
    printf("Group Create response buffer: %s\n\n", response_buffer.buffer);

    if (new_group) {
        printf("Group H5VL_rest_object_t fields:\n");
        printf("  - Group URI: %s\n", new_group->URI);
        printf("  - Group Object type: %d\n", new_group->obj_type);
        printf("  - Group Parent Domain path: %s\n\n", new_group->domain->u.file.filepath_name);
    }
#endif

    if (path_dirname)
        RV_free(path_dirname);
    if (create_request_body)
        RV_free(create_request_body);
    if (host_header)
        RV_free(host_header);

    /* Clean up allocated group object if there was an issue */
    if (new_group && !ret_value)
        if (RV_group_close(new_group, FAIL, NULL) < 0)
            FUNC_DONE_ERROR(H5E_SYM, H5E_CANTCLOSEOBJ, NULL, "can't close group")

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    return ret_value;
} /* end RV_group_create() */


/*-------------------------------------------------------------------------
 * Function:    RV_group_open
 *
 * Purpose:     Opens an existing HDF5 Group by retrieving its URI from the
 *              server and allocating an internal memory struct object for
 *              the group.
 *
 * Return:      Pointer to an RV_object_t struct corresponding to the
 *              opened group on success/NULL on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static void *
RV_group_open(void *obj, H5VL_loc_params_t H5_ATTR_UNUSED loc_params, const char *name,
              hid_t gapl_id, hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req)
{
    RV_object_t *parent = (RV_object_t *) obj;
    RV_object_t *group = NULL;
    H5I_type_t   obj_type = H5I_GROUP;
    htri_t       search_ret;
    void        *ret_value = NULL;

#ifdef PLUGIN_DEBUG
    printf("Received Group open call with following parameters:\n");
    printf("  - Name: %s\n", name);
    printf("  - GAPL: %ld\n", gapl_id);
    printf("  - Parent Object Type: %d\n", parent->obj_type);
#endif

    assert((H5I_FILE == parent->obj_type || H5I_GROUP == parent->obj_type)
            && "parent object not a file or group");

    /* Allocate and setup internal Group struct */
    if (NULL == (group = (RV_object_t *) RV_malloc(sizeof(*group))))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTALLOC, NULL, "can't allocate group object")

    group->obj_type = H5I_GROUP;
    group->u.group.gcpl_id = FAIL;
    group->domain = parent->domain; /* Store pointer to file that the opened Group is within */

    /* Locate the Group */
    search_ret = RV_find_object_by_path(parent, name, &obj_type, RV_copy_object_URI_callback, NULL, group->URI);
    if (!search_ret || search_ret < 0)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_PATH, NULL, "can't locate group by path")

    /* Set up a GCPL for the group so that H5Gget_create_plist() will function correctly */
    /* XXX: Set any properties necessary */
    if ((group->u.group.gcpl_id = H5Pcreate(H5P_GROUP_CREATE)) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "can't create GCPL for group")

    ret_value = (void *) group;

done:
#ifdef PLUGIN_DEBUG
    if (group) {
        printf("Group H5VL_rest_object_t fields:\n");
        printf("  - Group URI: %s\n", group->URI);
        printf("  - Group Object type: %d\n\n", group->obj_type);
        printf("  - Group Parent Domain path: %s\n\n", group->domain->u.file.filepath_name);
    }
#endif

    /* Clean up allocated file object if there was an issue */
    if (group && !ret_value)
        if (RV_group_close(group, FAIL, NULL) < 0)
            FUNC_DONE_ERROR(H5E_SYM, H5E_CANTCLOSEOBJ, NULL, "can't close group")

    return ret_value;
} /* end RV_group_open() */


/*-------------------------------------------------------------------------
 * Function:    RV_group_get
 *
 * Purpose:     Performs a "GET" operation on an HDF5 Group, such as
 *              calling the H5Gget_info routine.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
RV_group_get(void *obj, H5VL_group_get_t get_type, hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req, va_list arguments)
{
    RV_object_t *group = (RV_object_t *) obj;
    hbool_t      curl_perform = FALSE;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    char         request_url[URL_MAX_LENGTH];
    herr_t       ret_value = SUCCEED;

#ifdef PLUGIN_DEBUG
    printf("Received Group get call with following parameters:\n");
    printf("  - Get Type: %d\n", get_type);
    printf("  - Group URI: %s\n", group->URI);
    printf("  - Group File: %s\n", group->domain->u.file.filepath_name);
#endif

    assert(( H5I_GROUP == group->obj_type
          || H5I_FILE == group->obj_type)
          && "not a group");

    curl_perform = H5VL_GROUP_GET_INFO == get_type;

    if (curl_perform) {
        /* Setup the "Host: " header */
        host_header_len = strlen(group->domain->u.file.filepath_name) + strlen(host_string) + 1;
        if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
            FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

        strcpy(host_header, host_string);

        curl_headers = curl_slist_append(curl_headers, strncat(host_header, group->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

        /* Disable use of Expect: 100 Continue HTTP response */
        curl_headers = curl_slist_append(curl_headers, "Expect:");

        /* Redirect cURL from the base URL to "/groups/<id>" to get information about the group */
        snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s", base_URL, group->URI);

        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
            FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
            FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP GET request: %s", curl_err_buf)
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
            FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
        printf("  - Retrieving group info\n\n");

        printf("   /********************************\\\n");
        printf("-> | Making a request to the server |\n");
        printf("   \\********************************/\n\n");
#endif

        /* Make request to server to retrieve the group info */
        CURL_PERFORM(curl, H5E_SYM, H5E_CANTGET, FAIL);
    } /* end if */

    switch (get_type) {
        /* H5Gget_create_plist */
        case H5VL_GROUP_GET_GCPL:
        {
            hid_t *ret_id = va_arg(arguments, hid_t *);

            if ((*ret_id = H5Pcopy(group->u.group.gcpl_id)) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, FAIL, "can't copy Group GCPL")

            break;
        } /* H5VL_GROUP_GET_GCPL */

        /* H5Gget_info */
        case H5VL_GROUP_GET_INFO:
        {
            H5VL_loc_params_t  loc_params = va_arg(arguments, H5VL_loc_params_t);
            H5G_info_t        *group_info = va_arg(arguments, H5G_info_t *);

            /* XXX: Handle _by_name and _by_idx cases */

            /* Initialize struct to 0 */
            memset(group_info, 0, sizeof(*group_info));

            /* Parse response from server and retrieve the relevant group information
             * (currently, just the number of links in the group)
             */
            if (RV_parse_response(response_buffer.buffer, NULL, group_info, RV_get_group_info_callback) < 0)
                FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTGET, FAIL, "can't retrieve group information")

            break;
        } /* H5VL_GROUP_GET_INFO */

        default:
            FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTGET, FAIL, "can't get this type of information from group");
    } /* end switch */

done:
#ifdef PLUGIN_DEBUG
    printf("Group Get response buffer: %s\n\n", response_buffer.buffer);
#endif

    if (curl_perform) {
        if (host_header)
            RV_free(host_header);

        if (curl_headers) {
            curl_slist_free_all(curl_headers);
            curl_headers = NULL;
        } /* end if */
    } /* end if */

    return ret_value;
} /* end RV_group_get() */


/*-------------------------------------------------------------------------
 * Function:    RV_group_close
 *
 * Purpose:     Closes an HDF5 group by freeing the memory allocated for
 *              its internal memory struct object. There is no interation
 *              with the server, whose state is unchanged.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
RV_group_close(void *grp, hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req)
{
    RV_object_t *_grp = (RV_object_t *) grp;
    herr_t       ret_value = SUCCEED;

#ifdef PLUGIN_DEBUG
    printf("Received Group close call with following parameters:\n");
    printf("  - URI: %s\n", _grp->URI);
    printf("  - DXPL: %ld\n\n", dxpl_id);
#endif

    assert(H5I_GROUP == _grp->obj_type && "not a group");

    if (_grp->u.group.gcpl_id >= 0) {
        if (_grp->u.group.gcpl_id != H5P_GROUP_CREATE_DEFAULT && H5Pclose(_grp->u.group.gcpl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close GCPL")
    } /* end if */

    RV_free(_grp);

    return ret_value;
} /* end RV_group_close() */


/*-------------------------------------------------------------------------
 * Function:    RV_link_create
 *
 * Purpose:     Creates an HDF5 link in the given object by making the
 *              appropriate REST API call to the server.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
static herr_t
RV_link_create(H5VL_link_create_type_t create_type, void *obj, H5VL_loc_params_t loc_params,
               hid_t lcpl_id, hid_t lapl_id, hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req)
{
    H5VL_loc_params_t  hard_link_target_obj_loc_params;
    RV_object_t       *new_link_loc_obj = (RV_object_t *) obj;
    curl_off_t         create_request_body_len = 0;
    size_t             create_request_nalloc = 0;
    size_t             host_header_len = 0;
    void              *hard_link_target_obj;
    char              *host_header = NULL;
    char              *create_request_body = NULL;
    char               request_url[URL_MAX_LENGTH];
    char              *url_encoded_link_name = NULL;
    herr_t             ret_value = SUCCEED;

#ifdef PLUGIN_DEBUG
    printf("Received Link create call with following parameters:\n");
    printf("  - Link Name: %s\n", loc_params.loc_data.loc_by_name.name);
    printf("  - Link Type: %d\n", create_type);
    printf("  - Link_loc obj type: %d\n", loc_params.obj_type);
#endif

    /* Since the usage of the H5L_SAME_LOC macro for hard link creation may cause new_link_loc_obj to
     * be NULL, do some special-case handling for the Hard Link creation case
     */
    if (H5VL_LINK_CREATE_HARD == create_type) {
        /* Pre-fetch the target object's relevant information in the case of hard link creation */
        if (H5Pget(lcpl_id, H5VL_PROP_LINK_TARGET, &hard_link_target_obj) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get property list value for link's target object")
        if (H5Pget(lcpl_id, H5VL_PROP_LINK_TARGET_LOC_PARAMS, &hard_link_target_obj_loc_params) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get property list value for link's target object loc params")

        /* If link_loc_new_obj was NULL, H5L_SAME_LOC was specified as the new link's loc_id.
         * In this case, we use the target object's location as the new link's location.
         */
        if (!new_link_loc_obj)
            new_link_loc_obj = (RV_object_t *) hard_link_target_obj;
    } /* end if */

    /* Validate loc_id and check for write access on the file */
    assert((H5I_FILE == new_link_loc_obj->obj_type || H5I_GROUP == new_link_loc_obj->obj_type)
          && "link location object not a file or group");
    assert(loc_params.loc_data.loc_by_name.name);

    if (!(new_link_loc_obj->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "no write intent on file")


    switch (create_type) {
        case H5VL_LINK_CREATE_HARD:
        {
            htri_t  search_ret;
            char    temp_URI[URI_MAX_LENGTH];
            char   *target_URI;

            /* Since the special-case handling above for hard link creation should have already fetched the target
             * object for the hard link, proceed forward.
             */

            /* Check to make sure that a hard link is being created in the same file as
             * the target object
             */
            if (strcmp(new_link_loc_obj->domain->u.file.filepath_name, ((RV_object_t *) hard_link_target_obj)->domain->u.file.filepath_name))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTCREATE, FAIL, "can't create soft or hard link to object outside of the current file")

            switch (hard_link_target_obj_loc_params.type) {
                case H5VL_OBJECT_BY_SELF:
                {
                    target_URI = ((RV_object_t *) hard_link_target_obj)->URI;
                    break;
                } /* H5VL_OBJECT_BY_SELF */

                case H5VL_OBJECT_BY_NAME:
                {
                    H5I_type_t obj_type = H5I_UNINIT;
#ifdef PLUGIN_DEBUG
                    printf("  - Link target loc params by name: %s\n", hard_link_target_obj_loc_params.loc_data.loc_by_name.name);
#endif

                    search_ret = RV_find_object_by_path((RV_object_t *) hard_link_target_obj, hard_link_target_obj_loc_params.loc_data.loc_by_name.name,
                            &obj_type, RV_copy_object_URI_callback, NULL, temp_URI);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't locate link target object")

                    target_URI = temp_URI;

                    break;
                } /* H5VL_OBJECT_BY_NAME */

                case H5VL_OBJECT_BY_IDX:
                case H5VL_OBJECT_BY_ADDR:
                case H5VL_OBJECT_BY_REF:
                default:
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "invalid loc_params type")
            } /* end switch */

#ifdef PLUGIN_DEBUG
            printf("  - Target object URI: %s\n\n", target_URI);
#endif

            {
                const char * const fmt_string = "{\"id\": \"%s\"}";

                /* Form the request body to create the Link */
                create_request_nalloc = (strlen(fmt_string) - 2) + strlen(target_URI) + 1;
                if (NULL == (create_request_body = (char *) RV_malloc(create_request_nalloc)))
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate space for link create request body")

                if ((create_request_body_len = snprintf(create_request_body, create_request_nalloc, fmt_string, target_URI)) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")
            }

            break;
        } /* H5VL_LINK_CREATE_HARD */

        case H5VL_LINK_CREATE_SOFT:
        {
            const char *link_target;

            if (H5Pget(lcpl_id, H5VL_PROP_LINK_TARGET_NAME, &link_target) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get property list value for link's target")

            /* XXX: Check to make sure that a soft link is being created in the same file as
             * the target object
             */

#ifdef PLUGIN_DEBUG
            printf("    Soft link target: %s\n\n", link_target);
#endif

            {
                const char * const fmt_string = "{\"h5path\": \"%s\"}";

                /* Form the request body to create the Link */
                create_request_nalloc = (strlen(fmt_string) - 2) + strlen(link_target) + 1;
                if (NULL == (create_request_body = (char *) RV_malloc(create_request_nalloc)))
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate space for link create request body")

                if ((create_request_body_len = snprintf(create_request_body, create_request_nalloc, fmt_string, link_target)) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")
            }

            break;
        } /* H5VL_LINK_CREATE_SOFT */

        case H5VL_LINK_CREATE_UD:
        {
            H5L_type_t  link_type;
            const char *file_path, *link_target;
            size_t      link_target_buf_size;
            void       *link_target_buf;

            if (H5Pget(lcpl_id, H5VL_PROP_LINK_TYPE, &link_type) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get property list value for link's type")

            /* XXX: For now, no support for user-defined links, beyond external links */
            if (H5L_TYPE_EXTERNAL != link_type)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_UNSUPPORTED, FAIL, "unsupported link type")

            /* Retrieve the buffer containing the external link's information */
            if (H5Pget(lcpl_id, H5VL_PROP_LINK_UDATA_SIZE, &link_target_buf_size) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get property list value for external link's information buffer size")
            if (H5Pget(lcpl_id, H5VL_PROP_LINK_UDATA, &link_target_buf) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get property list value for external link's information buffer")

            /* The first byte of the link_target_buf contains the external link's version
             * and flags
             */
            file_path = (const char *) link_target_buf + 1;
            link_target = file_path + (strlen(file_path) + 1);

            {
                const char * const fmt_string = "{\"h5domain\": \"%s\", \"h5path\": \"%s\"}";

                /* Form the request body to create the Link */
                create_request_nalloc = (strlen(fmt_string) - 4) + strlen(file_path) + strlen(link_target) + 1;
                if (NULL == (create_request_body = (char *) RV_malloc(create_request_nalloc)))
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate space for link create request body")

                if ((create_request_body_len = snprintf(create_request_body, create_request_nalloc, fmt_string, file_path, link_target)) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")
            }

            break;
        } /* H5VL_LINK_CREATE_UD */

        default:
            FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "Invalid link create type")
    } /* end switch */

    /* Setup the "Host: " header */
    host_header_len = strlen(new_link_loc_obj->domain->u.file.filepath_name) + strlen(host_string) + 1;
    if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, new_link_loc_obj->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    /* Instruct cURL that we are sending JSON */
    curl_headers = curl_slist_append(curl_headers, "Content-Type: application/json");

    /* URL-encode the name of the link to ensure that the resulting URL for the link
     * creation operation doesn't contain any illegal characters
     */
    if (NULL == (url_encoded_link_name = curl_easy_escape(curl, RV_basename(loc_params.loc_data.loc_by_name.name), 0)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode link name")

    /* Redirect cURL from the base URL to "/groups/<id>/links/<name>" to create the link */
    snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/links/%s", base_URL, new_link_loc_obj->URI, url_encoded_link_name);

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT"))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP PUT request: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDS, create_request_body))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL PUT data: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, create_request_body_len))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL PUT data size: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
    printf("  - Creating link\n\n");

    printf("   /********************************\\\n");
    printf("-> | Making a request to the server |\n");
    printf("   \\********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_LINK, H5E_CANTCREATE, FAIL);

done:
#ifdef PLUGIN_DEBUG
    printf("Link create URL: %s\n\n", request_url);
    printf("Link create body: %s\n\n", create_request_body);
    printf("Link create body len: %ld\n\n", create_request_body_len);
    printf("Link create response buffer: %s\n\n", response_buffer.buffer);
#endif

    if (create_request_body)
        RV_free(create_request_body);
    if (host_header)
        RV_free(host_header);
    if (url_encoded_link_name)
        curl_free(url_encoded_link_name);

    /* Reset cURL custom request to prevent issues with future requests */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL))
        FUNC_DONE_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't reset cURL custom request: %s", curl_err_buf)

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    return ret_value;
} /* end RV_link_create() */


/*-------------------------------------------------------------------------
 * Function:    RV_link_copy
 *
 * Purpose:     Copies an existing HDF5 link from the file or group
 *              specified by src_obj to the file or group specified by
 *              dst_obj by making the appropriate REST API call/s to the
 *              server.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
static herr_t
RV_link_copy(void *src_obj, H5VL_loc_params_t loc_params1,
             void *dst_obj, H5VL_loc_params_t loc_params2,
             hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void H5_ATTR_UNUSED **req)
{
    herr_t ret_value = SUCCEED;

done:
    return ret_value;
} /* end RV_link_copy() */


/*-------------------------------------------------------------------------
 * Function:    RV_link_move
 *
 * Purpose:     Moves an existing HDF5 link from the file or group
 *              specified by src_obj to the file or group specified by
 *              dst_obj by making the appropriate REST API call/s to the
 *              server. The original link will be removed as part of the
 *              operation.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
static herr_t
RV_link_move(void *src_obj, H5VL_loc_params_t loc_params1,
             void *dst_obj, H5VL_loc_params_t loc_params2,
             hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void H5_ATTR_UNUSED **req)
{
    herr_t ret_value = SUCCEED;

done:
    return ret_value;
} /* end RV_link_move() */


/*-------------------------------------------------------------------------
 * Function:    RV_link_get
 *
 * Purpose:     Performs a "GET" operation on an HDF5 link, such as calling
 *              the H5Lget_info or H5Lget_name routines.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
static herr_t
RV_link_get(void *obj, H5VL_loc_params_t loc_params, H5VL_link_get_t get_type,
            hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req, va_list arguments)
{
    RV_object_t *link = (RV_object_t *) obj;
    hbool_t      curl_perform = FALSE;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    char         request_url[URL_MAX_LENGTH];
    herr_t       ret_value = SUCCEED;

#ifdef PLUGIN_DEBUG
    printf("Received Link get call with following parameters:\n");
    printf("  - Get type: %d\n", get_type);
    printf("  - Link URI: %s\n", link->URI);
    printf("  - Link File: %s\n\n", link->domain->u.file.filepath_name);
#endif

    if (curl_perform) {
        /* Setup the "Host: " header */
        host_header_len = strlen(link->domain->u.file.filepath_name) + strlen(host_string) + 1;
        if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

        strcpy(host_header, host_string);

        curl_headers = curl_slist_append(curl_headers, strncat(host_header, link->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

        /* Disable use of Expect: 100 Continue HTTP response */
        curl_headers = curl_slist_append(curl_headers, "Expect:");

        /* Redirect cURL from the base URL to "/groups/<id>/links/<name>" to get information about the link */
        /* snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/links/%s", base_URL, link->obj.); */

        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP GET request: %s", curl_err_buf)
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
        printf("  - Retrieving link info\n\n");

        printf("   /********************************\\\n");
        printf("-> | Making a request to the server |\n");
        printf("   \\********************************/\n\n");
#endif

        CURL_PERFORM(curl, H5E_LINK, H5E_CANTGET, FAIL);
    } /* end if */

    switch (get_type) {
        /* H5Lget_info */
        case H5VL_LINK_GET_INFO:
        {
            /* XXX: */
            FUNC_GOTO_ERROR(H5E_LINK, H5E_UNSUPPORTED, FAIL, "get link info unsupported")
        } /* H5VL_LINK_GET_INFO */

        /* H5Lget_name */
        case H5VL_LINK_GET_NAME:
        {
            /* XXX: */
            break;
        } /* H5VL_LINK_GET_NAME */

        /* H5Lget_val */
        case H5VL_LINK_GET_VAL:
        {
            /* XXX: */
            break;
        } /* H5VL_LINK_GET_VAL */

        default:
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't get this type of information from link")
    } /* end switch */

done:
#ifdef PLUGIN_DEBUG
    printf("Link Get response buffer: %s\n\n", response_buffer.buffer);
#endif

    if (curl_perform) {
        if (host_header)
            RV_free(host_header);

        if (curl_headers) {
            curl_slist_free_all(curl_headers);
            curl_headers = NULL;
        } /* end if */
    } /* end if */

    return ret_value;
} /* end RV_link_get() */


/*-------------------------------------------------------------------------
 * Function:    RV_link_specific
 *
 * Purpose:     Performs a plugin-specific operation on an HDF5 link, such
 *              as calling the H5Ldelete routine.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
static herr_t
RV_link_specific(void *obj, H5VL_loc_params_t loc_params, H5VL_link_specific_t specific_type,
                 hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req, va_list arguments)
{
    RV_object_t *loc_obj = (RV_object_t *) obj;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    char        *link_path_dirname = NULL;
    char         request_url[URL_MAX_LENGTH];
    char        *url_encoded_link_name = NULL;
    herr_t       ret_value = SUCCEED;

#ifdef PLUGIN_DEBUG
    printf("Received Link-specific call with following parameters:\n");
    printf("  - Specific type: %d\n", specific_type);
    printf("  - Link URI: %s\n", loc_obj->URI);
#endif

    assert((H5I_FILE == loc_obj->obj_type || H5I_GROUP == loc_obj->obj_type)
              && "parent object not a file or group");


    switch (specific_type) {
        /* H5Ldelete */
        case H5VL_LINK_DELETE:
            /* URL-encode the link name so that the resulting URL for the link delete
             * operation doesn't contain any illegal characters
             */
            if (NULL == (url_encoded_link_name = curl_easy_escape(curl, RV_basename(loc_params.loc_data.loc_by_name.name), 0)))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode link name")

            /* Redirect cURL from the base URL to "/groups/<id>/links/<name>" to delete link */
            snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/links/%s", base_URL, loc_obj->URI, url_encoded_link_name);

#ifdef PLUGIN_DEBUG
            printf("  - Link Delete URL: %s\n", request_url);
#endif

            /* Setup cURL to make the DELETE request */

            /* Setup the "Host: " header */
            host_header_len = strlen(loc_obj->domain->u.file.filepath_name) + strlen(host_string) + 1;
            if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

            strcpy(host_header, host_string);

            curl_headers = curl_slist_append(curl_headers, strncat(host_header, loc_obj->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

            /* Disable use of Expect: 100 Continue HTTP response */
            curl_headers = curl_slist_append(curl_headers, "Expect:");

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE"))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP DELETE request: %s", curl_err_buf)
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
            printf("   /********************************\\\n");
            printf("-> | Making a request to the server |\n");
            printf("   \\********************************/\n\n");
#endif

            CURL_PERFORM(curl, H5E_LINK, H5E_CANTREMOVE, FAIL);

            break;

        /* H5Lexists */
        case H5VL_LINK_EXISTS:
        {
            hbool_t  empty_dirname;
            htri_t  *ret = va_arg(arguments, htri_t *);
            char     target_URI[URI_MAX_LENGTH];
            long     http_response;

            /* In case the user specified a path which contains multiple groups on the way to the
             * link in question, extract out the path to the final group in the chain */
            if (NULL == (link_path_dirname = RV_dirname(loc_params.loc_data.loc_by_name.name)))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "invalid pathname for link")
            empty_dirname = !strcmp(link_path_dirname, "");

            /* If the path to the final group in the chain wasn't empty, get the URI of the final
             * group and search for the link within that group. Otherwise, search for the link within
             * the supplied parent group.
             */
            if (!empty_dirname) {
                H5I_type_t obj_type = H5I_GROUP;
                htri_t     search_ret;

                search_ret = RV_find_object_by_path(loc_obj, link_path_dirname, &obj_type, RV_copy_object_URI_callback,
                        NULL, target_URI);
                if (!search_ret || search_ret < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_PATH, FAIL, "can't locate parent group for link")
            } /* end if */

            /* URL-encode the link name so that the resulting URL for the link GET
             * operation doesn't contain any illegal characters
             */
            if (NULL == (url_encoded_link_name = curl_easy_escape(curl, RV_basename(loc_params.loc_data.loc_by_name.name), 0)))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode link name")

            snprintf(request_url, URL_MAX_LENGTH,
                     "%s/groups/%s/links/%s",
                     base_URL,
                     empty_dirname ? loc_obj->URI : target_URI,
                     url_encoded_link_name);

            /* Setup cURL to make the GET request */

            /* Setup the "Host: " header */
            host_header_len = strlen(loc_obj->domain->u.file.filepath_name) + strlen(host_string) + 1;
            if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

            strcpy(host_header, host_string);

            curl_headers = curl_slist_append(curl_headers, strncat(host_header, loc_obj->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

            /* Disable use of Expect: 100 Continue HTTP response */
            curl_headers = curl_slist_append(curl_headers, "Expect:");

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP GET request: %s", curl_err_buf)
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
            printf("   /********************************\\\n");
            printf("-> | Making a request to the server |\n");
            printf("   \\********************************/\n\n");
#endif

            CURL_PERFORM_NO_ERR(curl, FAIL);

            if (CURLE_OK != curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_response))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't get HTTP response code")

            *ret = HTTP_SUCCESS(http_response);

            break;
        } /* H5VL_LINK_EXISTS */

        /* H5Literate/visit (_by_name) */
        case H5VL_LINK_ITER:
        {
            /* XXX: */
            FUNC_GOTO_ERROR(H5E_LINK, H5E_UNSUPPORTED, FAIL, "unsupported operation")
        } /* H5VL_LINK_ITER */

        default:
            FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "unknown link operation");
    } /* end switch */

done:
    if (link_path_dirname)
        RV_free(link_path_dirname);
    if (host_header)
        RV_free(host_header);

    /* In case a custom DELETE request was made, reset the request to NULL
     * to prevent any possible future issues with requests
     */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL))
        FUNC_DONE_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't reset cURL custom request: %s", curl_err_buf)

    /* Free the escaped portion of the URL */
    if (url_encoded_link_name)
        curl_free(url_encoded_link_name);

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    return ret_value;
} /* end RV_link_specific() */


/*-------------------------------------------------------------------------
 * Function:    RV_object_open
 *
 * Purpose:     Generically opens an existing HDF5 group, dataset, or
 *              committed datatype by first retrieving the object's type
 *              from the server and then calling the appropriate
 *              RV_*_open routine. This function is called as the result of
 *              calling the H5Oopen routine.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
static void *
RV_object_open(void *obj, H5VL_loc_params_t loc_params, H5I_type_t *opened_type,
               hid_t dxpl_id, void H5_ATTR_UNUSED **req)
{
    RV_object_t *parent = (RV_object_t *) obj;
    H5I_type_t   obj_type = H5I_UNINIT;
    htri_t       search_ret;
    hid_t        lapl_id;
    void        *ret_value = NULL;

#ifdef PLUGIN_DEBUG
    printf("Received Object open call with following parameters:\n");
    printf("  - Path: %s\n", loc_params.loc_data.loc_by_name.name);
    printf("  - DXPL: %ld\n", dxpl_id);
    printf("  - Parent Object Type: %d\n", parent->obj_type);
#endif

    assert((H5I_FILE == parent->obj_type || H5I_GROUP == parent->obj_type)
                && "parent object not a file or group");

    /* XXX: Currently only opening objects by name is supported */
    assert(H5VL_OBJECT_BY_NAME == loc_params.type && "loc_params type not H5VL_OBJECT_BY_NAME");

    /* Retrieve the type of object being dealt with by querying the server */
    search_ret = RV_find_object_by_path(parent, loc_params.loc_data.loc_by_name.name, &obj_type, NULL, NULL, NULL);
    if (!search_ret || search_ret < 0)
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTOPENOBJ, NULL, "can't find object by name")

    /* Call the appropriate RV_*_open call based upon the object type */
    switch (obj_type) {
        case H5I_DATATYPE:
            /* Setup the correct lapl_id. Note that if H5P_DEFAULT was specified for the LAPL in the H5Oopen(_by_name) call,
             * HDF5 will actually pass H5P_LINK_ACCESS_DEFAULT down to this layer */
            if (H5VL_OBJECT_BY_NAME == loc_params.type) {
                lapl_id = (loc_params.loc_data.loc_by_name.lapl_id != H5P_LINK_ACCESS_DEFAULT)
                        ? loc_params.loc_data.loc_by_name.lapl_id : H5P_DATATYPE_ACCESS_DEFAULT;
            } /* end if */
            else if (H5VL_OBJECT_BY_IDX == loc_params.type) {
                lapl_id = (loc_params.loc_data.loc_by_idx.lapl_id != H5P_LINK_ACCESS_DEFAULT)
                        ? loc_params.loc_data.loc_by_idx.lapl_id : H5P_DATATYPE_ACCESS_DEFAULT;
            } /* end else if */
            else
                lapl_id = H5P_DATATYPE_ACCESS_DEFAULT;

            if (NULL == (ret_value = RV_datatype_open(parent, loc_params, loc_params.loc_data.loc_by_name.name,
                    lapl_id, dxpl_id, req)))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTOPENOBJ, NULL, "can't open datatype")
            break;

        case H5I_DATASET:
            /* Setup the correct lapl_id. Note that if H5P_DEFAULT was specified for the LAPL in the H5Oopen(_by_name) call,
             * HDF5 will actually pass H5P_LINK_ACCESS_DEFAULT down to this layer */
            if (H5VL_OBJECT_BY_NAME == loc_params.type) {
                lapl_id = (loc_params.loc_data.loc_by_name.lapl_id != H5P_LINK_ACCESS_DEFAULT)
                        ? loc_params.loc_data.loc_by_name.lapl_id : H5P_DATASET_ACCESS_DEFAULT;
            } /* end if */
            else if (H5VL_OBJECT_BY_IDX == loc_params.type) {
                lapl_id = (loc_params.loc_data.loc_by_idx.lapl_id != H5P_LINK_ACCESS_DEFAULT)
                        ? loc_params.loc_data.loc_by_idx.lapl_id : H5P_DATASET_ACCESS_DEFAULT;
            } /* end else if */
            else
                lapl_id = H5P_DATASET_ACCESS_DEFAULT;

            if (NULL == (ret_value = RV_dataset_open(parent, loc_params, loc_params.loc_data.loc_by_name.name,
                    lapl_id, dxpl_id, req)))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTOPENOBJ, NULL, "can't open dataset")
            break;

        case H5I_GROUP:
            /* Setup the correct lapl_id. Note that if H5P_DEFAULT was specified for the LAPL in the H5Oopen(_by_name) call,
             * HDF5 will actually pass H5P_LINK_ACCESS_DEFAULT down to this layer */
            if (H5VL_OBJECT_BY_NAME == loc_params.type) {
                lapl_id = (loc_params.loc_data.loc_by_name.lapl_id != H5P_LINK_ACCESS_DEFAULT)
                        ? loc_params.loc_data.loc_by_name.lapl_id : H5P_GROUP_ACCESS_DEFAULT;
            } /* end if */
            else if (H5VL_OBJECT_BY_IDX == loc_params.type) {
                lapl_id = (loc_params.loc_data.loc_by_idx.lapl_id != H5P_LINK_ACCESS_DEFAULT)
                        ? loc_params.loc_data.loc_by_idx.lapl_id : H5P_GROUP_ACCESS_DEFAULT;
            } /* end else if */
            else
                lapl_id = H5P_GROUP_ACCESS_DEFAULT;

            if (NULL == (ret_value = RV_group_open(parent, loc_params, loc_params.loc_data.loc_by_name.name,
                    lapl_id, dxpl_id, req)))
                FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTOPENOBJ, NULL, "can't open group")
            break;

        case H5I_ATTR:
        case H5I_UNINIT:
        case H5I_BADID:
        case H5I_FILE:
        case H5I_DATASPACE:
        case H5I_REFERENCE:
        case H5I_VFL:
        case H5I_VOL:
        case H5I_GENPROP_CLS:
        case H5I_GENPROP_LST:
        case H5I_ERROR_CLASS:
        case H5I_ERROR_MSG:
        case H5I_ERROR_STACK:
        case H5I_NTYPES:
        default:
            FUNC_GOTO_ERROR(H5E_ARGS, H5E_CANTOPENOBJ, NULL, "invalid object type")
    } /* end switch */

    if (opened_type) *opened_type = obj_type;

done:
    return ret_value;
} /* end RV_object_open() */


/*-------------------------------------------------------------------------
 * Function:    RV_object_copy
 *
 * Purpose:     Copies an existing HDF5 group, dataset or committed
 *              datatype from the file or group specified by src_obj to the
 *              file or group specified by dst_obj by making the
 *              appropriate REST API call/s to the server.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
static herr_t
RV_object_copy(void *src_obj, H5VL_loc_params_t loc_params1, const char *src_name,
               void *dst_obj, H5VL_loc_params_t loc_params2, const char *dst_name,
               hid_t ocpypl_id, hid_t lcpl_id, hid_t dxpl_id, void H5_ATTR_UNUSED **req)
{
    herr_t ret_value = SUCCEED;

done:
    return ret_value;
} /* end RV_object_copy() */


/*-------------------------------------------------------------------------
 * Function:    RV_object_get
 *
 * Purpose:     Performs a "GET" operation on an HDF5 object, such as
 *              calling the H5Rget_obj_type routine.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static herr_t
RV_object_get(void *obj, H5VL_loc_params_t loc_params, H5VL_object_get_t get_type,
              hid_t dxpl_id, void H5_ATTR_UNUSED **req, va_list arguments)
{
    RV_object_t *theobj = (RV_object_t *) obj;
    herr_t       ret_value = SUCCEED;

#ifdef PLUGIN_DEBUG
    printf("Received object get call with following parameters:\n");
    printf("  - Call type: %d\n", get_type);
    printf("  - Object URI: %s\n", theobj->URI);
    printf("  - Object type: %d\n", theobj->obj_type);
#endif

    switch (get_type) {
        case H5VL_REF_GET_NAME:
            FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_UNSUPPORTED, FAIL, "unsupported reference operation")

        /* H5Rget_region */
        case H5VL_REF_GET_REGION:
        {
            hid_t      *ret = va_arg(arguments, hid_t *);
            H5R_type_t  ref_type = va_arg(arguments, H5R_type_t);
            void       *ref = va_arg(arguments, void *);

            if (H5R_DATASET_REGION != ((rv_obj_ref_t *) ref)->ref_type)
                FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_BADVALUE, FAIL, "not a dataset region reference")

            /* XXX: */
            FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_UNSUPPORTED, FAIL, "region references are currently unsupported")

            break;
        } /* H5VL_REF_GET_REGION */

        /* H5Rget_obj_type2 */
        case H5VL_REF_GET_TYPE:
        {
            H5O_type_t *obj_type = va_arg(arguments, H5O_type_t *);
            H5R_type_t  ref_type = va_arg(arguments, H5R_type_t);
            void       *ref = va_arg(arguments, void *);

            /* Even though the reference type is stored as a field inside a
             * rv_obj_ref_t struct, take the user's passed in ref. type at
             * face value
             */
            switch (ref_type) {
                case H5R_OBJECT:
                {
                    /* Since the type of the referenced object is embedded
                     * in the rv_obj_ref_t struct, just retrieve it
                     */
                    switch (((rv_obj_ref_t *) ref)->ref_obj_type) {
                        case H5I_FILE:
                        case H5I_GROUP:
                            *obj_type = H5O_TYPE_GROUP;
                            break;

                        case H5I_DATATYPE:
                            *obj_type = H5O_TYPE_NAMED_DATATYPE;
                            break;

                        case H5I_DATASET:
                            *obj_type = H5O_TYPE_DATASET;
                            break;

                        case H5I_ATTR:
                        case H5I_UNINIT:
                        case H5I_BADID:
                        case H5I_DATASPACE:
                        case H5I_REFERENCE:
                        case H5I_VFL:
                        case H5I_VOL:
                        case H5I_GENPROP_CLS:
                        case H5I_GENPROP_LST:
                        case H5I_ERROR_CLASS:
                        case H5I_ERROR_MSG:
                        case H5I_ERROR_STACK:
                        case H5I_NTYPES:
                        default:
                            FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_BADVALUE, FAIL, "referenced object not a group, datatype or dataset")
                    } /* end switch */

                    break;
                } /* H5R_OBJECT */

                case H5R_DATASET_REGION:
                {
                    /* XXX: */
                    FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_BADVALUE, FAIL, "region references are currently unsupported")
                } /* H5R_DATASET_REGION */

                case H5R_BADTYPE:
                case H5R_MAXTYPE:
                default:
                    FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_BADVALUE, FAIL, "invalid reference type")
            } /* end switch */

            break;
        } /* H5VL_REF_GET_TYPE */

        default:
            FUNC_GOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "unknown object operation")
    } /* end switch */

done:
    return ret_value;
} /* end RV_object_get() */


/*-------------------------------------------------------------------------
 * Function:    RV_object_specific
 *
 * Purpose:     Performs a plugin-specific operation on an HDF5 object,
 *              such as calling the H5Ovisit routine.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
static herr_t
RV_object_specific(void *obj, H5VL_loc_params_t loc_params, H5VL_object_specific_t specific_type,
                   hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req, va_list arguments)
{
    RV_object_t *theobj = (RV_object_t *) obj;
    herr_t       ret_value = SUCCEED;

#ifdef PLUGIN_DEBUG
    printf("Received object specific call with following parameters:\n");
    printf("  - Call type: %d\n", specific_type);
    printf("  - Object URI: %s\n", theobj->URI);
    printf("  - Object type: %d\n", theobj->obj_type);
#endif

    switch (specific_type) {
        /* H5Oincr/decr_refcount */
        case H5VL_OBJECT_CHANGE_REF_COUNT:

        /* H5Oexists_by_name */
        case H5VL_OBJECT_EXISTS:

        /* H5Ovisit(_by_name) */
        case H5VL_OBJECT_VISIT:
            FUNC_GOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "unsupported object operation")

        /* H5Rcreate */
        case H5VL_REF_CREATE:
        {
            void       *ref = va_arg(arguments, void *);
            const char *name = va_arg(arguments, const char *);
            H5R_type_t  ref_type = va_arg(arguments, H5R_type_t);
            hid_t       space_id = va_arg(arguments, hid_t);

            switch (ref_type) {
                case H5R_OBJECT:
                {
                    rv_obj_ref_t *objref = (rv_obj_ref_t *) ref;
                    htri_t        search_ret;

                    /* Locate the object for the reference, setting the ref_obj_type and ref_obj_URI fields in the process */
                    objref->ref_obj_type = H5I_UNINIT;
                    search_ret = RV_find_object_by_path(theobj, name, &objref->ref_obj_type, RV_copy_object_URI_callback,
                            NULL, objref->ref_obj_URI);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_PATH, FAIL, "can't locate ref obj. by path")

                    objref->ref_type = ref_type;

                    break;
                } /* H5R_OBJECT */

                case H5R_DATASET_REGION:
                {
                    FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_UNSUPPORTED, FAIL, "region references are currently unsupported")
                } /* H5R_DATASET_REGION */

                case H5R_BADTYPE:
                case H5R_MAXTYPE:
                default:
                    FUNC_GOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "invalid ref type")
            } /* end switch */

            break;
        } /* H5VL_REF_CREATE */

        default:
            FUNC_GOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "unknown object operation")
    } /* end switch */

done:
    return ret_value;
} /* end RV_object_specific() */


/*-------------------------------------------------------------------------
 * Function:    RV_object_optional
 *
 * Purpose:     Performs an optional operation on an HDF5 object, such as
 *              calling the H5Oget_info or H5Oset/get_comment routines.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
static herr_t
RV_object_optional(void *obj, hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req, va_list arguments)
{
    H5VL_object_optional_t  optional_type = (H5VL_object_optional_t) va_arg(arguments, int);
    RV_object_t            *theobj = (RV_object_t *) obj;
    size_t                  host_header_len = 0;
    char                   *host_header = NULL;
    char                    request_url[URL_MAX_LENGTH];
    herr_t                  ret_value = SUCCEED;

    assert(( H5I_FILE == theobj->obj_type
          || H5I_DATATYPE == theobj->obj_type
          || H5I_DATASET == theobj->obj_type
          || H5I_GROUP == theobj->obj_type)
          && "not a group, dataset or datatype");

#ifdef PLUGIN_DEBUG
    printf("Received object optional call with following parameters:\n");
    printf("  - Call type: %d\n", optional_type);
    printf("  - File URI: %s\n", theobj->domain->URI);
    printf("  - File Pathname: %s\n\n", theobj->domain->u.file.filepath_name);
#endif

    switch (optional_type) {
        case H5VL_OBJECT_SET_COMMENT:
        case H5VL_OBJECT_GET_COMMENT:
            FUNC_GOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "object comments are deprecated in favor of use of object attributes")

        /* H5Oget_info(_by_name/_by_idx) */
        case H5VL_OBJECT_GET_INFO:
        {
            H5VL_loc_params_t  loc_params = va_arg(arguments, H5VL_loc_params_t);
            H5O_info_t        *obj_info = va_arg(arguments, H5O_info_t *);
            long long          attr_count = 0;

            memset(obj_info, 0, sizeof(H5O_info_t));

            switch (loc_params.type) {
                /* H5Oget_info */
                case H5VL_OBJECT_BY_SELF:
                {
                    /* Redirect cURL from the base URL to
                     * "/groups/<id>", "/datasets/<id>" or "/datatypes/<id>",
                     * depending on the type of the object. Also set the
                     * object's type in the H5O_info_t struct.
                     */
                    switch (theobj->obj_type) {
                        case H5I_FILE:
                        case H5I_GROUP:
                        {
                            obj_info->type = H5O_TYPE_GROUP;
                            snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s", base_URL, theobj->URI);
                            break;
                        } /* H5I_FILE H5I_GROUP */

                        case H5I_DATATYPE:
                        {
                            obj_info->type = H5O_TYPE_NAMED_DATATYPE;
                            snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s", base_URL, theobj->URI);
                            break;
                        } /* H5I_DATATYPE */

                        case H5I_DATASET:
                        {
                            obj_info->type = H5O_TYPE_DATASET;
                            snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s", base_URL, theobj->URI);
                            break;
                        } /* H5I_DATASET */

                        case H5I_ATTR:
                        case H5I_UNINIT:
                        case H5I_BADID:
                        case H5I_DATASPACE:
                        case H5I_REFERENCE:
                        case H5I_VFL:
                        case H5I_VOL:
                        case H5I_GENPROP_CLS:
                        case H5I_GENPROP_LST:
                        case H5I_ERROR_CLASS:
                        case H5I_ERROR_MSG:
                        case H5I_ERROR_STACK:
                        case H5I_NTYPES:
                        default:
                            FUNC_GOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "loc_id object is not a group, datatype or dataset")
                    } /* end switch */

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Oget_info_by_name */
                case H5VL_OBJECT_BY_NAME:
                {
                    /* XXX: */
                    FUNC_GOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "H5Oget_info_by_name is unsupported")
                    break;
                } /* H5VL_OBJECT_BY_NAME */

                /* H5Oget_info_by_idx */
                case H5VL_OBJECT_BY_IDX:
                {
                    /* XXX: */
                    FUNC_GOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "H5Oget_info_by_idx is unsupported")
                    break;
                } /* H5VL_OBJECT_BY_IDX */

                case H5VL_OBJECT_BY_ADDR:
                case H5VL_OBJECT_BY_REF:
                default:
                    FUNC_GOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "invalid loc_params type")
            } /* end switch */

            /* Make a GET request to the server to retrieve the number of attributes attached to the object */

            /* Setup the "Host: " header */
            host_header_len = strlen(theobj->domain->u.file.filepath_name) + strlen(host_string) + 1;
            if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

            strcpy(host_header, host_string);

            curl_headers = curl_slist_append(curl_headers, strncat(host_header, theobj->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

            /* Disable use of Expect: 100 Continue HTTP response */
            curl_headers = curl_slist_append(curl_headers, "Expect:");

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP GET request: %s", curl_err_buf)
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
            printf("  - Retrieving object info\n\n");

            printf("   /********************************\\\n");
            printf("-> | Making a request to the server |\n");
            printf("   \\********************************/\n\n");
#endif

            CURL_PERFORM(curl, H5E_VOL, H5E_CANTGET, FAIL);

            /* Retrieve the attribute count for the object */
            if (RV_parse_response(response_buffer.buffer, NULL, &attr_count, RV_retrieve_attribute_count_callback) < 0)
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "can't retrieve object attribute count")

            assert(attr_count >= 0);
            obj_info->num_attrs = (hsize_t) attr_count;

            break;
        } /* H5VL_OBJECT_GET_INFO */

        default:
            FUNC_GOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "unknown object operation")
    } /* end switch */

done:
    if (host_header)
        RV_free(host_header);

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    return ret_value;
} /* end RV_object_optional() */

/************************************
 *         Helper functions         *
 ************************************/


/*-------------------------------------------------------------------------
 * Function:    write_data_callback
 *
 * Purpose:     A callback for cURL which allows cURL to write its
 *              responses from the server into a growing string buffer
 *              which is processed by this VOL plugin after each server
 *              interaction.
 *
 * Return:      Amount of bytes equal to the amount given to this callback
 *              by cURL on success/differing amount of bytes on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static size_t
write_data_callback(void *buffer, size_t size, size_t nmemb, void H5_ATTR_UNUSED *userp)
{
    size_t data_size = size * nmemb;
    size_t positive_ptrdiff;

    /* If the server response is larger than the currently allocated amount for the
     * response buffer, grow the response buffer by a factor of 2
     */
    H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, (response_buffer.curr_buf_ptr + data_size) - response_buffer.buffer, ptrdiff_t);
    while (positive_ptrdiff + 1 > response_buffer.buffer_size) {
        char *tmp_realloc;

        if (NULL == (tmp_realloc = (char *) RV_realloc(response_buffer.buffer, 2 * response_buffer.buffer_size)))
            return 0;

        response_buffer.curr_buf_ptr = tmp_realloc + (response_buffer.curr_buf_ptr - response_buffer.buffer);
        response_buffer.buffer = tmp_realloc;
        response_buffer.buffer_size *= 2;

#ifdef PLUGIN_DEBUG
        printf("  - Re-alloced response buffer to size %zu\n\n", response_buffer.buffer_size);
#endif
    } /* end while */

    memcpy(response_buffer.curr_buf_ptr, buffer, data_size);
    response_buffer.curr_buf_ptr += data_size;
    *response_buffer.curr_buf_ptr = '\0';

    return data_size;
} /* end write_data_callback() */


/*-------------------------------------------------------------------------
 * Function:    RV_basename
 *
 * Purpose:     A portable implementation of the basename routine which
 *              retrieves everything after the final '/' in a given
 *              pathname. This function exhibits the GNU behavior in that
 *              it will return the empty string if the pathname contains a
 *              trailing '/'.
 *
 * Return:      Basename portion of the given path
 *              (can't fail)
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
/* XXX: Potentially modify to deal with the trailing slash case */
static const char *
RV_basename(const char *path)
{
    char *substr = strrchr(path, '/');
    return substr ? substr + 1 : path;
} /* end RV_basename() */


/*-------------------------------------------------------------------------
 * Function:    RV_dirname
 *
 * Purpose:     A portable implementation of the dirname routine which
 *              retrieves everything before the final '/' in a given
 *              pathname. The pointer handed back by this function must
 *              be freed, else memory will be leaked.
 *
 * Return:      Dirname portion of the given path on success/ NULL on
 *              failure
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
static char *
RV_dirname(const char *path)
{
    size_t  len = (size_t) (RV_basename(path) - path);
    char   *dirname = NULL;

    if (NULL == (dirname = (char *) RV_malloc(len + 1)))
        return NULL;

    memcpy(dirname, path, len);
    dirname[len] = '\0';

    return dirname;
} /* end RV_dirname() */


/*-------------------------------------------------------------------------
 * Function:    dataset_read_scatter_op
 *
 * Purpose:     Callback for H5Dscatter() to scatter the read data into the
 *              supplied buffer
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
static herr_t
dataset_read_scatter_op(const void **src_buf, size_t *src_buf_bytes_used, void *op_data)
{
    *src_buf = response_buffer.buffer;
    *src_buf_bytes_used = *((size_t *) op_data);

#ifdef PLUGIN_DEBUG
    printf("Src_buf_bytes_used: %zu.\n", *src_buf_bytes_used);
#endif

    return 0;
} /* end dataset_read_scatter_op() */


/*-------------------------------------------------------------------------
 * Function:    RV_parse_response
 *
 * Purpose:     Helper function to simply ingest a string buffer containing
 *              an HTTP response given back by the server and call a
 *              supplied callback function on the result. The
 *              callback_data_in and callback_data_out parameters are used
 *              to pass data between the given callback.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 *
 */
static herr_t
RV_parse_response(char *HTTP_response, void *callback_data_in, void *callback_data_out,
                  herr_t (*parse_callback)(char *, void *, void *))
{
    herr_t ret_value = SUCCEED;

    assert(HTTP_response);

    if (parse_callback && parse_callback(HTTP_response, callback_data_in, callback_data_out) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "can't perform callback operation")

done:
    return ret_value;
} /* end RV_parse_response() */


/*-------------------------------------------------------------------------
 * Function:    RV_copy_object_URI_callback
 *
 * Purpose:     A callback for RV_parse_response which will, given a
 *              set of JSON keys through the callback_data_in parameter,
 *              search an HTTP response for the URI of an object and copy
 *              that URI into the callback_data_out parameter, which should
 *              be a char *. This callback is used throughout this VOL
 *              plugin to capture the URI of an object after making a
 *              request to the server, such as creating a new object or
 *              retrieving information from an existing object.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
static herr_t
RV_copy_object_URI_callback(char *HTTP_response, void H5_ATTR_UNUSED *callback_data_in, void *callback_data_out)
{
    const char *soft_link_class_keys[] = { "link", "class", (const char *) 0 };
    const char *hard_link_keys[] = { "link", "id", (const char *) 0 };
    const char *object_create_keys[] = { "id", (const char *) 0 };
    const char *root_group_keys[] = { "root", (const char *) 0 };
    yajl_val    parse_tree, key_obj;
    char       *parsed_string;
    herr_t      ret_value = SUCCEED;

    assert(callback_data_out && "invalid URI pointer");

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "parsing JSON failed")

    /* To handle the awkward case of soft and external links, which do not return an "ID",
     * first check for the link class field and short circuit if it is found to be
     * equal to "H5L_TYPE_SOFT"
     */
    if (NULL != (key_obj = yajl_tree_get(parse_tree, soft_link_class_keys, yajl_t_string))) {
        char *link_type;

        if (NULL == (link_type = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of link type failed")

        if (!strcmp(link_type, "H5L_TYPE_SOFT") || !strcmp(link_type, "H5L_TYPE_EXTERNAL") ||
                !strcmp(link_type, "H5L_TYPE_UD"))
            FUNC_GOTO_DONE(SUCCEED)
    } /* end if */

    /* First attempt to retrieve the URI of the object by using the JSON key sequence
     * "link" -> "id", which is returned when making a GET Link request.
     */
    key_obj = yajl_tree_get(parse_tree, hard_link_keys, yajl_t_string);
    if (key_obj) {
        if (!YAJL_IS_STRING(key_obj))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "returned URI is not a string")

        if (NULL == (parsed_string = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of URI failed")
    } /* end if */
    else {
        /* Could not find the object's URI by the sequence "link" -> "id". Try looking
         * for just the JSON key "id", which would generally correspond to trying to
         * retrieve the URI of a newly-created or opened object that isn't a file.
         */
        key_obj = yajl_tree_get(parse_tree, object_create_keys, yajl_t_string);
        if (key_obj) {
            if (!YAJL_IS_STRING(key_obj))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "returned URI is not a string")

            if (NULL == (parsed_string = YAJL_GET_STRING(key_obj)))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of URI failed")
        } /* end if */
        else {
            /* Could not find the object's URI by the JSON key "id". Try looking for
             * just the JSON key "root", which would generally correspond to trying to
             * retrieve the URI of a newly-created or opened file, or to a search for
             * the root group of a file.
             */
            if (NULL == (key_obj = yajl_tree_get(parse_tree, root_group_keys, yajl_t_string)))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of URI failed")

            if (!YAJL_IS_STRING(key_obj))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "returned URI is not a string")

            if (NULL == (parsed_string = YAJL_GET_STRING(key_obj)))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of URI failed")
        } /* end else */
    } /* end else */

    strncpy((char *) callback_data_out, parsed_string, URI_MAX_LENGTH);

done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_copy_object_URI_parse_callback() */


/*-------------------------------------------------------------------------
 * Function:    RV_get_link_type_callback
 *
 * Purpose:     A callback for RV_parse_response which will search
 *              an HTTP response for the type of an object and copy that
 *              type into the callback_data_out parameter, which should be
 *              a H5I_type_t *. This callback is used to help H5Oopen
 *              determine what type of object it is working with and make
 *              the appropriate object open call.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              September, 2017
 */
static herr_t
RV_get_link_type_callback(char *HTTP_response, void H5_ATTR_UNUSED *callback_data_in, void *callback_data_out)
{
    const char *soft_link_class_keys[] = { "link", "class", (const char *) 0 };
    const char *link_collection_keys[] = { "link", "collection", (const char *) 0 };
    yajl_val    parse_tree, key_obj;
    char       *parsed_string;
    herr_t      ret_value = SUCCEED;

    assert(callback_data_out && "invalid object type pointer");

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "parsing JSON failed")

    /* To handle the awkward case of soft and external links, which do not return an "ID",
     * first check for the link class field and short circuit if it is found to be
     * equal to "H5L_TYPE_SOFT"
     */
    if (NULL != (key_obj = yajl_tree_get(parse_tree, soft_link_class_keys, yajl_t_string))) {
        char *link_type;

        if (NULL == (link_type = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of link type failed")

        if (!strcmp(link_type, "H5L_TYPE_SOFT") || !strcmp(link_type, "H5L_TYPE_EXTERNAL") ||
                !strcmp(link_type, "H5L_TYPE_UD"))
            FUNC_GOTO_DONE(SUCCEED);
    } /* end if */

    /* Retrieve the object's type */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, link_collection_keys, yajl_t_string)))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of object parent collection failed")

    if (!YAJL_IS_STRING(key_obj))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "returned object parent collection is not a string")

    if (NULL == (parsed_string = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of object parent collection failed")

    if (!strcmp(parsed_string, "groups"))
        *((H5I_type_t *) callback_data_out) = H5I_GROUP;
    else if (!strcmp(parsed_string, "datasets"))
        *((H5I_type_t *) callback_data_out) = H5I_DATASET;
    else if (!strcmp(parsed_string, "datatypes"))
        *((H5I_type_t *) callback_data_out) = H5I_DATATYPE;
    else
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "invalid object type")

done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_get_link_type_callback() */


/*-------------------------------------------------------------------------
 * Function:    RV_retrieve_attribute_count_callback
 *
 * Purpose:     A callback for RV_parse_response which will search
 *              an HTTP response for the number of attributes on an object
 *              and copy that number into the callback_data_out parameter,
 *              which should be a long long *. This callback is used to
 *              help H5Oget_info fill out a H5O_info_t struct corresponding
 *              to the info about an object.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              November, 2017
 */
static herr_t
RV_retrieve_attribute_count_callback(char *HTTP_response,
    void H5_ATTR_UNUSED *callback_data_in, void *callback_data_out)
{
    const char *attribute_count_keys[] = { "attributeCount", (const char *) 0 };
    yajl_val    parse_tree, key_obj;
    herr_t      ret_value = SUCCEED;

    assert(callback_data_out && "invalid attribute count pointer");

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "parsing JSON failed")

    /* Retrieve the object's attribute count */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, attribute_count_keys, yajl_t_number)))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of object attribute count failed")

    if (!YAJL_IS_INTEGER(key_obj))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "returned object attribute count is not an integer")

    *((long long *) callback_data_out) = YAJL_GET_INTEGER(key_obj);

done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_retrieve_attribute_count_callback() */


/*-------------------------------------------------------------------------
 * Function:    RV_get_group_info_callback
 *
 * Purpose:     A callback for RV_parse_response which will search
 *              an HTTP response for the number of links contained in a
 *              group and copy that number into the callback_data_out
 *              parameter, which should be a H5G_info_t *. This callback is
 *              used to help H5Gget_info(_by_name) fill out a H5G_info_t
 *              struct corresponding to the info about a group and will
 *              fill in the rest of the fields with default values, as the
 *              current spec does not have provisions for these other
 *              fields.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              November, 2017
 */
static herr_t
RV_get_group_info_callback(char *HTTP_response,
    void H5_ATTR_UNUSED *callback_data_in, void *callback_data_out)
{
    const char *group_link_count_keys[] = { "linkCount", (const char *) 0 };
    H5G_info_t *info = (H5G_info_t *) callback_data_out;
    yajl_val    parse_tree, key_obj;
    herr_t      ret_value = SUCCEED;

    assert(callback_data_out && "invalid group info pointer");

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "parsing JSON failed")

    /* Retrieve the group's link count */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, group_link_count_keys, yajl_t_number)))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of group link count failed")

    if (!YAJL_IS_INTEGER(key_obj))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "returned group link count is not an integer")

    assert(YAJL_GET_INTEGER(key_obj) >= 0 && "group link count is not non-negative");
    info->nlinks = (hsize_t) YAJL_GET_INTEGER(key_obj);

    /* Since the spec doesn't currently include provisions for the extra fields, set them to defaults */
    /* XXX: These defaults may be incorrect for applications to interpret */
    info->storage_type = H5G_STORAGE_TYPE_SYMBOL_TABLE;
    info->max_corder = 0;
    info->mounted = FALSE;

done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_get_group_info_callback() */


/*-------------------------------------------------------------------------
 * Function:    RV_parse_dataset_creation_properties_callback
 *
 * Purpose:     A callback for RV_parse_response which will search
 *              an HTTP response for the creation properties of a dataset
 *              and set those properties on a DCPL given as input. This
 *              callback is used to help H5Dopen() correctly setup a DCPL
 *              for a dataset that has been "opened" from the server. When
 *              this happens, a default DCPL is created for the dataset,
 *              but does not immediately have any properties set on it.
 *
 *              Without this callback, if a client were to call H5Dopen(),
 *              then call H5Pget_chunk() (or similar) on the Dataset's
 *              contained DCPL, it would result in an error because the
 *              library does not have the chunking information associated
 *              with the DCPL yet. Therefore, this VOL plugin has to
 *              handle this case by retrieving all of the creation
 *              properties of a dataset from the server and manually set
 *              each one of the relevant creation properties on the DCPL.
 *
 *              Note that this is unnecessary when H5Pget_chunk() or
 *              similar is called directly after calling H5Dcreate()
 *              without closing the dataset. This is because the user's
 *              supplied DCPL (which would already have the properties set
 *              on it) is copied into the Dataset's in-memory struct
 *              representation for future use.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              November, 2017
 */
static herr_t
RV_parse_dataset_creation_properties_callback(char *HTTP_response,
    void H5_ATTR_UNUSED *callback_data_in, void *callback_data_out)
{
    const char *creation_properties_keys[]    = { "creationProperties", (const char *) 0 };
    const char *alloc_time_keys[]             = { "allocTime", (const char *) 0 };
    const char *creation_order_keys[]         = { "attributeCreationOrder", (const char *) 0 };
    const char *attribute_phase_change_keys[] = { "attributePhaseChange", (const char *) 0 };
    const char *fill_time_keys[]              = { "fillTime", (const char *) 0 };
    const char *fill_value_keys[]             = { "fillValue", (const char *) 0 };
    const char *filters_keys[]                = { "filters", (const char *) 0 };
    const char *layout_keys[]                 = { "layout", (const char *) 0 };
    const char *track_times_keys[]            = { "trackTimes", (const char *) 0 };
    yajl_val    parse_tree, creation_properties_obj, key_obj;
    hid_t      *DCPL = (hid_t *) callback_data_out;
    herr_t      ret_value = SUCCEED;

    assert(callback_data_out && "invalid DCPL pointer");

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "parsing JSON failed")

    /* Retrieve the creationProperties object */
    if (NULL == (creation_properties_obj = yajl_tree_get(parse_tree, creation_properties_keys, yajl_t_object)))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of creationProperties object failed")


    /********************************************************************************************
     *                                                                                          *
     *                              Space Allocation Time Section                               *
     *                                                                                          *
     * Determine the status of the space allocation time (default, early, late, incremental)    *
     * and set this on the DCPL                                                                 *
     *                                                                                          *
     ********************************************************************************************/
    if ((key_obj = yajl_tree_get(creation_properties_obj, alloc_time_keys, yajl_t_string))) {
        H5D_alloc_time_t  alloc_time = H5D_ALLOC_TIME_DEFAULT;
        char             *alloc_time_string;

        if (NULL == (alloc_time_string = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of space allocation time string failed")

        if (!strcmp(alloc_time_string, "H5D_ALLOC_TIME_EARLY")) {
            alloc_time = H5D_ALLOC_TIME_EARLY;
        } /* end if */
        else if (!strcmp(alloc_time_string, "H5D_ALLOC_TIME_INCR")) {
            alloc_time = H5D_ALLOC_TIME_INCR;
        } /* end else if */
        else if (!strcmp(alloc_time_string, "H5D_ALLOC_TIME_LATE")) {
            alloc_time = H5D_ALLOC_TIME_LATE;
        } /* end else if */

#ifdef PLUGIN_DEBUG
        printf("  - Setting AllocTime %d on DCPL\n", alloc_time);
#endif

        if (H5Pset_alloc_time(*DCPL, alloc_time) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "can't set space allocation time property on DCPL")
    } /* end if */


    /********************************************************************************************
     *                                                                                          *
     *                             Attribute Creation Order Section                             *
     *                                                                                          *
     * Determine the status of attribute creation order (tracked, tracked + indexed or neither) *
     * and set this on the DCPL                                                                 *
     *                                                                                          *
     ********************************************************************************************/
    if ((key_obj = yajl_tree_get(creation_properties_obj, creation_order_keys, yajl_t_string))) {
        unsigned  crt_order_flags = 0x0;
        char     *crt_order_string;

        if (NULL == (crt_order_string = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of attribute creation order string failed")

        if (!strcmp(crt_order_string, "H5P_CRT_ORDER_INDEXED")) {
            crt_order_flags = H5P_CRT_ORDER_INDEXED | H5P_CRT_ORDER_TRACKED;
        } /* end if */
        else {
            crt_order_flags = H5P_CRT_ORDER_TRACKED;
        } /* end else */

#ifdef PLUGIN_DEBUG
        printf("  - Setting attribute creation order %u on DCPL\n", crt_order_flags);
#endif

        if (H5Pset_attr_creation_order(*DCPL, crt_order_flags) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "can't set attribute creation order property on DCPL")
    } /* end if */


    /****************************************************************************
     *                                                                          *
     *                 Attribute Phase Change Threshold Section                 *
     *                                                                          *
     * Determine the phase change values for attribute storage and set these on *
     * the DCPL                                                                 *
     *                                                                          *
     ****************************************************************************/
    if ((key_obj = yajl_tree_get(creation_properties_obj, attribute_phase_change_keys, yajl_t_object))) {
        const char *max_compact_keys[] = { "maxCompact", (const char *) 0 };
        const char *min_dense_keys[] = { "minDense", (const char *) 0 };
        unsigned    minDense = DATASET_CREATE_MIN_DENSE_ATTRIBUTES_DEFAULT;
        unsigned    maxCompact = DATASET_CREATE_MAX_COMPACT_ATTRIBUTES_DEFAULT;
        yajl_val    sub_obj;

        if (NULL == (sub_obj = yajl_tree_get(key_obj, max_compact_keys, yajl_t_number)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of maxCompact attribute phase change value failed")

        if (!YAJL_IS_INTEGER(sub_obj))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "return maxCompact attribute phase change value is not an integer")

        if (YAJL_GET_INTEGER(sub_obj) >= 0)
            maxCompact = (unsigned) YAJL_GET_INTEGER(sub_obj);

        if (NULL == (sub_obj = yajl_tree_get(key_obj, min_dense_keys, yajl_t_number)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of minDense attribute phase change value failed")

        if (!YAJL_IS_INTEGER(sub_obj))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "returned minDense attribute phase change value is not an integer")

        if (YAJL_GET_INTEGER(sub_obj) >= 0)
            minDense = (unsigned) YAJL_GET_INTEGER(sub_obj);

        if (minDense != DATASET_CREATE_MIN_DENSE_ATTRIBUTES_DEFAULT || maxCompact != DATASET_CREATE_MAX_COMPACT_ATTRIBUTES_DEFAULT) {
#ifdef PLUGIN_DEBUG
            printf("  - Setting attr phase change values: [ minDense: %u, maxCompact: %u ] on DCPL\n", minDense, maxCompact);
#endif

            if (H5Pset_attr_phase_change(*DCPL, maxCompact, minDense) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "can't set attribute phase change values property on DCPL")
        } /* end if */
    } /* end if */


    /**********************************************************
     *                                                        *
     *                    Fill Time Section                   *
     *                                                        *
     * Determine the fill time value and set this on the DCPL *
     *                                                        *
     **********************************************************/
    if ((key_obj = yajl_tree_get(creation_properties_obj, fill_time_keys, yajl_t_string))) {
        H5D_fill_time_t  fill_time = H5D_FILL_TIME_IFSET;
        char            *fill_time_str;

        if (NULL == (fill_time_str = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of fill time string failed")

        if (!strcmp(fill_time_str, "H5D_FILL_TIME_ALLOC")) {
            fill_time = H5D_FILL_TIME_ALLOC;
        } /* end else if */
        else if (!strcmp(fill_time_str, "H5D_FILL_TIME_NEVER")) {
            fill_time = H5D_FILL_TIME_NEVER;
        } /* end else if */

#ifdef PLUGIN_DEBUG
        printf("  - Setting fill time %d on DCPL\n", fill_time);
#endif

        if (H5Pset_fill_time(*DCPL, fill_time) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "can't set fill time property on DCPL")
    } /* end if */


    /******************************************************************************
     *                                                                            *
     *                             Fill Value Section                             *
     *                                                                            *
     * Determine the fill value status for the Dataset and set this on the DCPL   *
     *                                                                            *
     ******************************************************************************/
    if ((key_obj = yajl_tree_get(creation_properties_obj, fill_value_keys, yajl_t_any))) {
        /* XXX: support for fill values */
    } /* end if */


    /***************************************************************
     *                                                             *
     *                       Filters Section                       *
     *                                                             *
     * Determine the filters that have been added to the Dataset   *
     * and set this on the DCPL                                    *
     *                                                             *
     ***************************************************************/
    if ((key_obj = yajl_tree_get(creation_properties_obj, filters_keys, yajl_t_array))) {
        /* XXX: support for filters */
    } /* end if */


    /****************************************************************************
     *                                                                          *
     *                                Layout Section                            *
     *                                                                          *
     * Determine the layout information of the Dataset and set this on the DCPL *
     *                                                                          *
     ****************************************************************************/
    if ((key_obj = yajl_tree_get(creation_properties_obj, layout_keys, yajl_t_object))) {
        const char *layout_class_keys[] = { "class", (const char *) 0 };
        yajl_val    sub_obj;
        size_t      i;
        char       *layout_class;

        if (NULL == (sub_obj = yajl_tree_get(key_obj, layout_class_keys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of layout class property failed")

        if (NULL == (layout_class = YAJL_GET_STRING(sub_obj)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of layout class string failed")

        if (!strcmp(layout_class, "H5D_CHUNKED")) {
            const char *chunk_dims_keys[] = { "dims", (const char *) 0 };
            yajl_val    chunk_dims_obj;
            hsize_t     chunk_dims[DATASPACE_MAX_RANK];

            if (NULL == (chunk_dims_obj = yajl_tree_get(key_obj, chunk_dims_keys, yajl_t_array)))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of chunk dimensionality failed")

            for (i = 0; i < YAJL_GET_ARRAY(chunk_dims_obj)->len; i++) {
                long long val;

                if (!YAJL_IS_INTEGER(YAJL_GET_ARRAY(chunk_dims_obj)->values[i]))
                    FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "one of the chunk dimension sizes was not an integer")

                if ((val = YAJL_GET_INTEGER(YAJL_GET_ARRAY(chunk_dims_obj)->values[i])) < 0)
                    FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "one of the chunk dimension sizes was negative")

                chunk_dims[i] = (hsize_t) val;
            } /* end for */

#ifdef PLUGIN_DEBUG
            printf("  - Setting chunked layout on DCPL\n");
            printf("    - Dims: [ ");
            for (i = 0; i < YAJL_GET_ARRAY(chunk_dims_obj)->len; i++) {
                if (i > 0) printf(", ");
                printf("%zu", chunk_dims[i]);
            }
            printf(" ]\n");
#endif

            if (H5Pset_chunk(*DCPL, (int) YAJL_GET_ARRAY(chunk_dims_obj)->len, chunk_dims) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "can't set chunked storage layout on DCPL")
        } /* end if */
        else if (!strcmp(layout_class, "H5D_CONTIGUOUS")) {
            const char *external_storage_keys[] = { "externalStorage", (const char *) 0 };
            yajl_val    external_storage_obj;

            if (NULL == (external_storage_obj = yajl_tree_get(key_obj, external_storage_keys, yajl_t_array)))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of external storage file extent array failed")

#ifdef PLUGIN_DEBUG
            printf("  - Setting contiguous layout on DCPL\n");
#endif

            if (H5Pset_layout(*DCPL, H5D_CONTIGUOUS) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "can't set contiguous storage layout on DCPL")
        } /* end if */
        else if (!strcmp(layout_class, "H5D_COMPACT")) {
#ifdef PLUGIN_DEBUG
            printf("  - Setting compact layout on DCPL\n");
#endif

            if (H5Pset_layout(*DCPL, H5D_COMPACT) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "can't set compact storage layout on DCPL")
        } /* end else */
    } /* end if */


    /*************************************************************************
     *                                                                       *
     *                       Object Time Tracking Section                    *
     *                                                                       *
     * Determine the status of object time tracking and set this on the DCPL *
     *                                                                       *
     *************************************************************************/
    if ((key_obj = yajl_tree_get(creation_properties_obj, track_times_keys, yajl_t_string))) {
        hbool_t  track_times = false;
        char    *track_times_str;

        if (NULL == (track_times_str = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "retrieval of track times string failed")

        track_times = !strcmp(track_times_str, "true");

#ifdef PLUGIN_DEBUG
        printf("  - Setting track times: %d on DCPL", track_times);
#endif

        if (H5Pset_obj_track_times(*DCPL, track_times) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "can't set track object times property on DCPL")
    } /* end if */

#ifdef PLUGIN_DEBUG
    printf("\n\n");
#endif

done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_parse_dataset_creation_properties_callback() */


/*-------------------------------------------------------------------------
 * Function:    RV_find_object_by_path
 *
 * Purpose:     XXX
 *
 *              Note that in order to support improved performance from the
 *              client side, a server operation was added to directly
 *              retrieve an object by using the "h5path" request parameter
 *              in the URL. If the request is using a relative path and the
 *              object being looked for is not a group, the "grpid"
 *              parameter must be added as well in order to supply the
 *              server with the URI of the object which the path is
 *              relative from. Previously, the approach to finding an
 *              object was to traverse links in the file, but this caused
 *              too much communication between client and server and would
 *              start to become problematic for deeply-nested objects.
 *
 * Return:      Non-negative on success, negative on failure
 *
 * Programmer:  Jordan Henderson
 *              November, 2017
 */
static htri_t
RV_find_object_by_path(RV_object_t *parent_obj, const char *obj_path,
                       H5I_type_t *target_object_type,
                       herr_t (*obj_found_callback)(char *, void *, void *),
                       void *callback_data_in, void *callback_data_out)
{
    hbool_t  intermediate_groups_in_path = FALSE;
    hbool_t  is_relative_path = FALSE;
    size_t   host_header_len = 0;
    char    *host_header = NULL;
    char    *link_dir_name = NULL;
    char    *url_encoded_link_name = NULL;
    char     temp_URI[URI_MAX_LENGTH];
    char     request_url[URL_MAX_LENGTH];
    long     http_response;
    htri_t   ret_value = FAIL;

    assert(parent_obj);
    assert(obj_path);
    assert(target_object_type);
    assert((H5I_FILE == parent_obj->obj_type || H5I_GROUP == parent_obj->obj_type)
          && "parent object not a file or group");

    /* XXX: Try to better organize the unknown object type case */
    /* XXX: support for url-encoding link names */

    /* In order to not confuse the server, make sure the path has no leading spaces */
    while (*obj_path == ' ') obj_path++;

    /* Check to see whether the path references the root group, which is a case that
     * must be specially handled
     */
    if (!strcmp(obj_path, "/")) {
        *target_object_type = H5I_GROUP;
    } /* end if */
    else {
        /* Check to see whether this path is a relative path by checking for the
         * absence of a leading '/' character.
         */
        is_relative_path = (*obj_path != '/');

        /* It is possible that the user may have specified a path such as 'dataset' or './dataset'
         * or even '../dataset', which would all be equivalent to searching for 'dataset' as a
         * relative path from the supplied parent_obj. Note that HDF5 path names do not adhere
         * to the UNIX '..' notation which would signify a parent group. Therefore, whenever we
         * encounter the "(.*)/" pattern, we skip past as many '.' characters as we can find until
         * we arrive at the final one, in order to prevent the server from getting confused.
         */
        if (is_relative_path)
            while (*obj_path == '.' && *(obj_path + 1) == '.')
                obj_path++;
    } /* end else */


    /* If the object type was specified as H5I_UNINIT, the caller does not know the type of the
     * target object, as in when calling something like H5Oopen(). Since the request URL to
     * retrieve the actual object cannot be properly formed without knowing what type of object
     * is being searched for, some extra preprocessing has to be done first in order to retrieve
     * the object's type.
     */
    if (H5I_UNINIT == *target_object_type) {
        hbool_t empty_dirname;

#ifdef PLUGIN_DEBUG
        printf("  - Unknown object type; retrieving object type\n\n");
#endif

        /* In case the user specified a path which contains multiple groups on the way to the
         * one which the object in question should be under, extract out the path to the final
         * group in the chain */
        if (NULL == (link_dir_name = RV_dirname(obj_path)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't get path dirname")
        empty_dirname = !strcmp(link_dir_name, "");

#ifdef PLUGIN_DEBUG
        printf("  - Path Dirname: %s\n\n", link_dir_name);
#endif

        /* If the path to the final group in the chain wasn't empty, get the URI of the final
         * group and search for the object in question within that group. Otherwise, the
         * supplied parent group is the one that should be housing the object, so search from
         * there.
         */
        if (!empty_dirname) {
            H5I_type_t obj_type = H5I_GROUP;
            htri_t     search_ret;

            search_ret = RV_find_object_by_path(parent_obj, link_dir_name, &obj_type,
                    RV_copy_object_URI_callback, NULL, temp_URI);
            if (!search_ret || search_ret < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't locate parent group")

#ifdef PLUGIN_DEBUG
            printf("  - Found new parent group %s at end of path chain\n\n", temp_URI);
#endif

            intermediate_groups_in_path = TRUE;
        } /* end if */
    } /* end if */


    /* Setup cURL for making GET requests */

    /* Setup the "Host: " header */
    host_header_len = strlen(parent_obj->domain->u.file.filepath_name) + strlen(host_string) + 1;
    if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, parent_obj->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP GET request: %s", curl_err_buf)


    if (H5I_UNINIT == *target_object_type) {
        /* Now that we have a base group to search from which will be guaranteed to contain the
         * target object (if it exists), try to grab the link to the target object and get its
         * object type. */
        snprintf(request_url, URL_MAX_LENGTH,
                 "%s/groups/%s/links/%s",
                 base_URL,
                 intermediate_groups_in_path ? temp_URI : parent_obj->URI,
                 RV_basename(obj_path));

        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
        printf("  - Retrieving link for object of unknown type at URL %s\n\n", request_url);

        printf("   /********************************\\\n");
        printf("-> | Making a request to the server |\n");
        printf("   \\********************************/\n\n");
#endif

        CURL_PERFORM(curl, H5E_LINK, H5E_PATH, FALSE);

#ifdef PLUGIN_DEBUG
        printf("  - Found link for object of unknown type; capturing link type\n\n");
#endif

        if (RV_parse_response(response_buffer.buffer, NULL, target_object_type, RV_get_link_type_callback) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't retrieve link type")
    } /* end if */


    /* Craft the request URL based on the type of the object we're looking for and whether or not
     * the path given is a relative path or not.
     */
    switch (*target_object_type) {
        case H5I_FILE:
        case H5I_GROUP:
            snprintf(request_url, URL_MAX_LENGTH,
                     "%s/groups/%s?h5path=%s",
                     base_URL,
                     is_relative_path ? parent_obj->URI : "",
                     obj_path);
            break;

        case H5I_DATATYPE:
            snprintf(request_url, URL_MAX_LENGTH,
                     "%s/datatypes/?%s%s%sh5path=%s",
                     base_URL,
                     is_relative_path ? "grpid=" : "",
                     is_relative_path ? parent_obj->URI : "",
                     is_relative_path ? "&" : "",
                     obj_path);
            break;

        case H5I_DATASET:
            snprintf(request_url, URL_MAX_LENGTH,
                     "%s/datasets/?%s%s%sh5path=%s",
                     base_URL,
                     is_relative_path ? "grpid=" : "",
                     is_relative_path ? parent_obj->URI : "",
                     is_relative_path ? "&" : "",
                     obj_path);
            break;

        case H5I_ATTR:
        case H5I_UNINIT:
        case H5I_BADID:
        case H5I_DATASPACE:
        case H5I_REFERENCE:
        case H5I_VFL:
        case H5I_VOL:
        case H5I_GENPROP_CLS:
        case H5I_GENPROP_LST:
        case H5I_ERROR_CLASS:
        case H5I_ERROR_MSG:
        case H5I_ERROR_STACK:
        case H5I_NTYPES:
        default:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "target object not a group, datatype or dataset")
    } /* end switch */

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef PLUGIN_DEBUG
    printf("Accessing URL: %s\n\n", request_url);

    printf("   /********************************\\\n");
    printf("-> | Making a request to the server |\n");
    printf("   \\********************************/\n\n");
#endif

    CURL_PERFORM_NO_ERR(curl, FAIL);

    if (CURLE_OK != curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_response))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't get HTTP response code")

    if (obj_found_callback && RV_parse_response(response_buffer.buffer,
            callback_data_in, callback_data_out, obj_found_callback) < 0)
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CALLBACK, FAIL, "can't perform callback operation")

    ret_value = HTTP_SUCCESS(http_response);

done:
    if (link_dir_name)
        RV_free(link_dir_name);
    if (host_header)
        RV_free(host_header);

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    return ret_value;
} /* end RV_find_object_by_path() */

/*-------------------------------------------------------------------------
 * Function:    RV_convert_predefined_datatype_to_string
 *
 * Purpose:     Given a predefined Datatype, returns a string
 *              representation of that Datatype.
 *
 * Return:      The string representation of the Datatype given by type_id,
 *              if type_id is a valid Datatype; NULL otherwise
 *
 * Programmer:  Jordan Henderson
 *              April, 2017
 */
static const char *
RV_convert_predefined_datatype_to_string(hid_t type_id)
{
    H5T_class_t  type_class = H5T_NO_CLASS;
    H5T_order_t  type_order = H5T_ORDER_NONE;
    H5T_sign_t   type_sign = H5T_SGN_NONE;
    static char  type_name[PREDEFINED_DATATYPE_NAME_MAX_LENGTH];
    size_t       type_size;
    char        *ret_value = type_name;

    if (H5T_NO_CLASS == (type_class = H5Tget_class(type_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, NULL, "invalid datatype")

    if (!(type_size = H5Tget_size(type_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, NULL, "invalid datatype size")

    if (H5T_ORDER_ERROR == (type_order = H5Tget_order(type_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, NULL, "invalid datatype ordering")

    if (type_class == H5T_INTEGER)
        if (H5T_SGN_ERROR == (type_sign = H5Tget_sign(type_id)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, NULL, "invalid datatype sign")

    snprintf(type_name, PREDEFINED_DATATYPE_NAME_MAX_LENGTH,
             "H5T_%s_%s%zu%s",
             (type_class == H5T_INTEGER) ? "STD" : "IEEE",
             (type_class == H5T_FLOAT) ? "F" : (type_sign == H5T_SGN_NONE) ? "U" : "I",
             type_size * 8,
             (type_order == H5T_ORDER_LE) ? "LE" : "BE");

done:
    return ret_value;
} /* end RV_convert_predefined_datatype_to_string() */


/*-------------------------------------------------------------------------
 * Function:    RV_convert_datatype_to_string
 *
 * Purpose:     Given a datatype, this function creates a JSON-formatted
 *              string representation of the datatype.
 *
 *              Can be called recursively for the case of Array and
 *              Compound Datatypes. The parameter 'nested' should always be
 *              supplied as FALSE, as the function itself handles the
 *              correct passing of the parameter when processing nested
 *              datatypes (such as the base type for an Array datatype).
 *
 *              The string buffer handed back by this function must be
 *              freed by the caller, else memory will be leaked.
 *
 * Return:      Non-negative on success/negative on failure
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
static herr_t
RV_convert_datatype_to_string(hid_t type_id, char **type_body, size_t *type_body_len, hbool_t nested)
{
    H5T_class_t   type_class;
    const char   *leading_string = "\"type\": "; /* Leading string for all datatypes */
    hsize_t      *array_dims = NULL;
    htri_t        type_is_committed;
    size_t        leading_string_len = strlen(leading_string);
    size_t        out_string_len;
    size_t        bytes_to_print = 0;            /* Used to calculate whether the datatype body buffer needs to be grown */
    size_t        positive_ptrdiff = 0;
    size_t        type_size;
    size_t        i;
    hid_t         type_base_class = FAIL;
    hid_t         compound_member = FAIL;
    void         *enum_value = NULL;
    char         *enum_value_name = NULL;
    char         *enum_mapping = NULL;
    char         *array_shape = NULL;
    char         *array_base_type = NULL;
    char        **compound_member_strings = NULL;
    char         *compound_member_name = NULL;
    char         *out_string = NULL;
    char         *out_string_curr_pos;           /* The "current position" pointer used to print to the appropriate place
                                                  in the buffer and not overwrite important leading data */
    int           bytes_printed = 0;
    herr_t        ret_value = SUCCEED;

    if (!type_body)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid NULL pointer for converted datatype's string buffer")

    out_string_len = DATATYPE_BODY_DEFAULT_SIZE;
    if (NULL == (out_string = (char *) RV_malloc(out_string_len)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for converted datatype's string buffer")

#ifdef PLUGIN_DEBUG
    printf("  - Initial datatype-to-string buffer size is %zu\n\n", out_string_len);
#endif

    /* Keep track of the current position in the resulting string so everything
     * gets added smoothly
     */
    out_string_curr_pos = out_string;

    /* Make sure the buffer is at least large enough to hold the leading "type" string */
    CHECKED_REALLOC(out_string, out_string_len, leading_string_len + 1,
            out_string_curr_pos, H5E_DATATYPE, FAIL);

#ifdef PLUGIN_DEBUG
    printf("  - After re-alloc check, buffer is %zu bytes\n\n", out_string_len);
#endif

    /* Add the leading "'type': " string */
    if (!nested) {
        strncpy(out_string, leading_string, out_string_len);
        out_string_curr_pos += leading_string_len;
    } /* end if */

    /* If the datatype is a committed type, append the datatype's URI and return */
    if ((type_is_committed = H5Tcommitted(type_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't determine if datatype is committed")

    if (type_is_committed) {
        H5VL_object_t *vol_container; /* XXX: Private definition currently prevents VOL plugin from being external */
        RV_object_t   *vol_obj;

#ifdef PLUGIN_DEBUG
        printf("  - Datatype was a committed type\n\n");
#endif

        /* Retrieve the VOL object's container */
        if (H5VLget_object(type_id, (void **) &vol_container) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get VOL object for committed datatype")

        vol_obj = (RV_object_t *) vol_container->vol_obj;

        /* Check whether the buffer needs to be grown */
        bytes_to_print = strlen(vol_obj->URI) + 2;

        H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
        CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATATYPE, FAIL);

        if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - positive_ptrdiff, "\"%s\"", vol_obj->URI)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error")

        out_string_curr_pos += bytes_printed;

        FUNC_GOTO_DONE(SUCCEED);
    } /* end if */

#ifdef PLUGIN_DEBUG
    printf("  - Datatype was not a committed type\n\n");
#endif

    if (!(type_size = H5Tget_size(type_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid datatype")

    switch ((type_class = H5Tget_class(type_id))) {
        case H5T_INTEGER:
        case H5T_FLOAT:
        {
            const char         *type_name;
            const char * const  int_class_str = "H5T_INTEGER";
            const char * const  float_class_str = "H5T_FLOAT";
            const char * const  fmt_string = "{"
                                                 "\"class\": \"%s\", "
                                                 "\"base\": \"%s\""
                                             "}";

            /* XXX: Need support for non-predefined integer and float types */

            /* Convert the class and name of the datatype to JSON */
            if (NULL == (type_name = RV_convert_predefined_datatype_to_string(type_id)))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid datatype")

            /* Check whether the buffer needs to be grown */
            bytes_to_print = (H5T_INTEGER == type_class ? strlen(int_class_str) : strlen(float_class_str))
                           + strlen(type_name) + (strlen(fmt_string) - 4) + 1;

            H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
            CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATATYPE, FAIL);

#ifdef PLUGIN_DEBUG
            printf("  - After re-alloc check, buffer is %zu bytes\n\n", out_string_len);
#endif

            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - positive_ptrdiff, fmt_string,
                    (H5T_INTEGER == type_class ? int_class_str : float_class_str), type_name)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error")

            out_string_curr_pos += bytes_printed;

            break;
        } /* H5T_INTEGER */ /* H5T_FLOAT */

        case H5T_STRING:
        {
            const char * const cset_ascii_string = "H5T_CSET_ASCII";
            htri_t             is_vlen;

            if ((is_vlen = H5Tis_variable_str(type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "can't determine if datatype is variable-length string")

            /* Build the Datatype body by appending the character set for the string type,
             * any type of string padding, and the length of the string */
            /* Note: currently only H5T_CSET_ASCII is supported for the character set and
             * only H5T_STR_NULLTERM is supported for string padding for variable-length
             * strings and only H5T_STR_NULLPAD is supported for string padding for
             * fixed-length strings, but these may change in the future.
             */
            if (is_vlen) {
                const char * const nullterm_string = "H5T_STR_NULLTERM";
                const char * const fmt_string = "{"
                                                    "\"class\": \"H5T_STRING\", "
                                                    "\"charSet\": \"%s\", "
                                                    "\"strPad\": \"%s\", "
                                                    "\"length\": \"H5T_VARIABLE\""
                                                "}";

                bytes_to_print = (strlen(fmt_string) - 4) + strlen(cset_ascii_string) + strlen(nullterm_string) + 1;

                H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
                CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATATYPE, FAIL);

#ifdef PLUGIN_DEBUG
                printf("  - After re-alloc check, buffer is %zu bytes\n\n", out_string_len);
#endif

                if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - leading_string_len,
                                              fmt_string, cset_ascii_string, nullterm_string)) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error")

                out_string_curr_pos += bytes_printed;
            } /* end if */
            else {
                const char * const nullpad_string = "H5T_STR_NULLPAD";
                const char * const fmt_string = "{"
                                                    "\"class\": \"H5T_STRING\", "
                                                    "\"charSet\": \"%s\", "
                                                    "\"strPad\": \"%s\", "
                                                    "\"length\": %zu"
                                                "}";

                bytes_to_print = (strlen(fmt_string) - 7) + strlen(cset_ascii_string) + strlen(nullpad_string) + MAX_NUM_LENGTH + 1;

                H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
                CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATATYPE, FAIL);

#ifdef PLUGIN_DEBUG
                printf("  - After re-alloc check, buffer is %zu bytes\n\n", out_string_len);
#endif

                if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - leading_string_len,
                                              fmt_string, cset_ascii_string, nullpad_string, type_size)) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error")

                out_string_curr_pos += bytes_printed;
            } /* end else */

            break;
        } /* H5T_STRING */

        case H5T_COMPOUND:
        {
            const char         *compound_type_leading_string = "{\"class\": \"H5T_COMPOUND\", \"fields\": [";
            size_t              compound_type_leading_strlen = strlen(compound_type_leading_string);
            size_t              complete_section_len;
            int                 nmembers;
            const char * const  fmt_string = "{"
                                                 "\"name\": \"%s\", "
                                                 "%s"
                                             "}%s";

            if ((nmembers = H5Tget_nmembers(type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't retrieve number of members in compound datatype")

            if (NULL == (compound_member_strings = (char **) RV_malloc(((size_t) nmembers + 1) * sizeof(*compound_member_strings))))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for compound datatype member strings")

            for (i = 0; i < (size_t) nmembers + 1; i++)
                compound_member_strings[i] = NULL;

            H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
            CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + compound_type_leading_strlen + 1,
                    out_string_curr_pos, H5E_DATATYPE, FAIL);

#ifdef PLUGIN_DEBUG
            printf("  - After re-alloc check, buffer is %zu bytes\n\n", out_string_len);
#endif

            strncpy(out_string_curr_pos, compound_type_leading_string, compound_type_leading_strlen);
            out_string_curr_pos += compound_type_leading_strlen;

            /* For each member in the compound type, convert it into its JSON representation
             * equivalent and append it to the growing datatype string
             */
            for (i = 0; i < (size_t) nmembers; i++) {
                if ((compound_member = H5Tget_member_type(type_id, (unsigned) i)) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get compound datatype member")

                if (RV_convert_datatype_to_string(compound_member, &compound_member_strings[i], NULL, FALSE) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL, "can't convert compound datatype member to string representation")

                if (NULL == (compound_member_name = H5Tget_member_name(type_id, (unsigned) i)))
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get compound datatype member name")

#ifdef PLUGIN_DEBUG
                printf("  - Compound Datatype member %zu name: %s\n", i, compound_member_name);
                printf("  - Compound Datatype member %zu: %s\n\n", i, compound_member_strings[i]);
#endif

                /* Check whether the buffer needs to be grown */
                bytes_to_print = strlen(compound_member_name) + strlen(compound_member_strings[i])
                        + (strlen(fmt_string) - 6) + (i < (size_t) nmembers - 1 ? 2 : 0) + 1;

                H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
                CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print,
                        out_string_curr_pos, H5E_DATATYPE, FAIL);

#ifdef PLUGIN_DEBUG
                printf("  - After re-alloc check, buffer is %zu bytes\n\n", out_string_len);
#endif

                if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - positive_ptrdiff,
                                              fmt_string, compound_member_name, compound_member_strings[i],
                                              i < (size_t) nmembers - 1 ? ", " : "")) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error")

                out_string_curr_pos += bytes_printed;

                if (H5Tclose(compound_member) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, FAIL, "can't close datatype")
                if (H5free_memory(compound_member_name) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTFREE, FAIL, "can't free compound datatype member name buffer")
                compound_member = FAIL;
                compound_member_name = NULL;
            } /* end for */

            /* Check if the buffer needs to grow to accomodate the closing ']' and '}' symbols, as well as
             * the NUL terminator
             */
            H5_CHECKED_ASSIGN(complete_section_len, size_t, out_string_curr_pos - out_string + 3, ptrdiff_t);
            CHECKED_REALLOC(out_string, out_string_len, complete_section_len, out_string_curr_pos, H5E_DATATYPE, FAIL);

#ifdef PLUGIN_DEBUG
            printf("  - After re-alloc check, buffer is %zu bytes\n\n", out_string_len);
#endif

            strcat(out_string_curr_pos, "]}");
            out_string_curr_pos += strlen("]}");

            break;
        } /* H5T_COMPOUND */

        case H5T_ENUM:
        {
            const char         *base_type_name;
            size_t              enum_mapping_length = 0;
            char               *mapping_curr_pos;
            int                 enum_nmembers;
            const char * const  mapping_fmt_string = "\"%s\": %lld%s";
            const char * const  fmt_string = "{"
                                                 "\"class\": \"H5T_ENUM\", "
                                                 "\"base\": {"
                                                     "\"class\": \"H5T_INTEGER\", "
                                                     "\"base\": \"%s\""
                                                 "}, "
                                                 "\"mapping\": {"
                                                     "%s"
                                                 "}"
                                             "}";

            if ((enum_nmembers = H5Tget_nmembers(type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "can't get number of members of enumerated type")

            if (NULL == (enum_value = RV_malloc(type_size)))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for enum member value")

            enum_mapping_length = ENUM_MAPPING_DEFAULT_SIZE;
            if (NULL == (enum_mapping = (char *) RV_malloc(enum_mapping_length)))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for enum mapping")

#ifdef PLUGIN_DEBUG
            printf("  - Enum mapping string buffer initial length is %zu bytes\n\n", enum_mapping_length);
#endif

            /* For each member in the enum type, retrieve the member's name and value, then
             * append these to the growing datatype string
             */
            for (i = 0, mapping_curr_pos = enum_mapping; i < (size_t) enum_nmembers; i++) {
                if (NULL == (enum_value_name = H5Tget_member_name(type_id, (unsigned) i)))
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "can't get name of enum member")

                if (H5Tget_member_value(type_id, (unsigned) i, enum_value) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't retrieve value of enum member")

                /* Check if the mapping buffer needs to grow */
                bytes_to_print = strlen(enum_value_name) + MAX_NUM_LENGTH + (strlen(mapping_fmt_string) - 8)
                        + (i < (size_t) enum_nmembers - 1 ? 2 : 0) + 1;

                H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, mapping_curr_pos - enum_mapping, ptrdiff_t);
                CHECKED_REALLOC(enum_mapping, enum_mapping_length, positive_ptrdiff + bytes_to_print,
                        mapping_curr_pos, H5E_DATATYPE, FAIL);

                /* Append the name of this enum mapping value and its corresponding numeric value to the mapping list */
                /* XXX: Need to cast the enum_value to the appropriate size type */
                if ((bytes_printed = snprintf(mapping_curr_pos, enum_mapping_length - positive_ptrdiff,
                                              mapping_fmt_string, enum_value_name, *((long long int *) enum_value),
                                              i < (size_t) enum_nmembers - 1 ? ", " : "")) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error")

                mapping_curr_pos += bytes_printed;

                if (H5free_memory(enum_value_name) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTFREE, FAIL, "can't free memory allocated for enum member name")
                enum_value_name = NULL;
            } /* end for */

#ifdef PLUGIN_DEBUG
            printf("  - After re-alloc checks, mapping buffer is %zu bytes\n\n", enum_mapping_length);
#endif

            /* Retrieve the enum type's base datatype and convert it into JSON as well */
            if ((type_base_class = H5Tget_super(type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "cant get base datatype for enum type")

            if (NULL == (base_type_name = RV_convert_predefined_datatype_to_string(type_base_class)))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid datatype")

            /* Check whether the buffer needs to be grown */
            bytes_to_print = strlen(base_type_name) + strlen(enum_mapping) + (strlen(fmt_string) - 4) + 1;

            H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
            CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print,
                    out_string_curr_pos, H5E_DATATYPE, FAIL);

#ifdef PLUGIN_DEBUG
            printf("  - After re-alloc check, buffer is %zu bytes\n\n", out_string_len);
#endif

            /* Build the Datatype body by appending the base integer type class for the enum
             * and the mapping values to map from numeric values to
             * string representations.
             */
            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - positive_ptrdiff,
                                          fmt_string, base_type_name, enum_mapping)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error")

            out_string_curr_pos += bytes_printed;

            break;
        } /* H5T_ENUM */

        case H5T_ARRAY:
        {
            size_t              array_base_type_len = 0;
            char               *array_shape_curr_pos;
            int                 ndims;
            const char * const  fmt_string = "{"
                                                 "\"class\": \"H5T_ARRAY\", "
                                                 "\"base\": %s, "
                                                 "\"dims\": %s"
                                             "}";

            if ((ndims = H5Tget_array_ndims(type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "can't get array datatype number of dimensions")

            if (NULL == (array_shape = (char *) RV_malloc((size_t) (ndims * MAX_NUM_LENGTH + ndims + 3))))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for array datatype dimensionality string")
            array_shape_curr_pos = array_shape;
            *array_shape_curr_pos = '\0';

            if (NULL == (array_dims = (hsize_t *) RV_malloc((size_t) ndims * sizeof(*array_dims))))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for array datatype dimensions")

            if (H5Tget_array_dims2(type_id, array_dims) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get array datatype dimensions")

            strcat(array_shape_curr_pos++, "[");

            /* Setup the shape of the array Datatype */
            for (i = 0; i < (size_t) ndims; i++) {
                if ((bytes_printed = snprintf(array_shape_curr_pos, MAX_NUM_LENGTH, "%s%llu", i > 0 ? "," : "", array_dims[i])) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error")

                array_shape_curr_pos += bytes_printed;
            } /* end for */

            strcat(array_shape_curr_pos, "]");

            /* Get the class and name of the base datatype */
            if ((type_base_class = H5Tget_super(type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get base datatype for array type")

            if ((type_is_committed = H5Tcommitted(type_base_class)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't determine if array base datatype is committed")

            if (RV_convert_datatype_to_string(type_base_class, &array_base_type, &array_base_type_len, TRUE) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL, "can't convert datatype to string representation")

            /* Check whether the buffer needs to be grown */
            bytes_to_print = array_base_type_len + strlen(array_shape) + (strlen(fmt_string) - 4) + 1;

            H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
            CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print,
                    out_string_curr_pos, H5E_DATATYPE, FAIL);

#ifdef PLUGIN_DEBUG
            printf("  - After re-alloc check, buffer is %zu bytes\n\n", out_string_len);
#endif

            /* Build the Datatype body by appending the array type class and base type and dimensions of the array */
            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - positive_ptrdiff,
                                          fmt_string, array_base_type, array_shape)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error")

            out_string_curr_pos += bytes_printed;

            break;
        } /* H5T_ARRAY */

        case H5T_BITFIELD:
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "unsupported datatype")
            break;
        case H5T_OPAQUE:
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "unsupported datatype")
            break;
        case H5T_REFERENCE:
        {
            htri_t             is_obj_ref;
            const char * const obj_ref_str = "H5T_STD_REF_OBJ";
            const char * const reg_ref_str = "H5T_STD_REF_DSETREG";
            const char * const fmt_string = "{"
                                                "\"class\": \"H5T_REFERENCE\","
                                                "\"base\": \"%s\""
                                            "}";

            is_obj_ref = H5Tequal(type_id, H5T_STD_REF_OBJ);
            if (is_obj_ref < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't determine type of reference")

            bytes_to_print = (strlen(fmt_string) - 2) + (is_obj_ref ? strlen(obj_ref_str) : strlen(reg_ref_str)) + 1;

            H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
            CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATATYPE, FAIL);

            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - positive_ptrdiff,
                    fmt_string, is_obj_ref ? obj_ref_str : reg_ref_str)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error")

            out_string_curr_pos += bytes_printed;

            break;
        } /* H5T_REFERENCE */

        case H5T_VLEN:
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "unsupported datatype")
            break;
        case H5T_TIME:
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "unsupported datatype")
            break;
        case H5T_NO_CLASS:
        case H5T_NCLASSES:
        default:
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADTYPE, FAIL, "invalid datatype")
    } /* end switch */

done:
    if (ret_value >= 0) {
#ifdef PLUGIN_DEBUG
        printf("  - Final datatype-to-string buffer size is %td\n\n", out_string_curr_pos - out_string);
#endif

        *type_body = out_string;
        if (type_body_len) H5_CHECKED_ASSIGN(*type_body_len, size_t, out_string_curr_pos - out_string, ptrdiff_t);
    } /* end if */
    else {
        if (out_string)
            RV_free(out_string);
    } /* end else */

    if (type_base_class >= 0)
        H5Tclose(type_base_class);
    if (compound_member >= 0)
        H5Tclose(compound_member);
    if (compound_member_name)
        H5free_memory(compound_member_name);
    if (compound_member_strings) {
        for (i = 0; compound_member_strings[i]; i++)
            RV_free(compound_member_strings[i]);
        RV_free(compound_member_strings);
    } /* end if */
    if (array_shape)
        RV_free(array_shape);
    if (array_base_type)
        RV_free(array_base_type);
    if (array_dims)
        RV_free(array_dims);
    if (enum_value)
        RV_free(enum_value);
    if (enum_value_name)
        H5free_memory(enum_value_name);
    if (enum_mapping)
        RV_free(enum_mapping);

    return ret_value;
} /* end RV_convert_datatype_to_string() */


/*-------------------------------------------------------------------------
 * Function:    RV_convert_string_to_datatype
 *
 * Purpose:     Given a JSON string representation of a datatype, creates
 *              and returns an hid_t for the datatype using H5Tcreate().
 *
 *              Can be called recursively for the case of Array and
 *              Compound Datatypes. The parameter 'nested' should always be
 *              supplied as FALSE, as the function itself handles the
 *              correct passing of the parameter when processing nested
 *              datatypes (such as the base type for an Array datatype).
 *
 *              NOTE: Support for Compound Datatypes is quite ugly. In
 *              order to be able to support Compound of Compound datatypes,
 *              Compound of Array, etc., as well as arbitrary whitespace
 *              inside the JSON string, all without modifying the string,
 *              sacrifices in performance had to be made. Not supporting
 *              these features would diminish the usefulness of this
 *              feature as a general JSON string-to-Datatype conversion
 *              function.
 *
 *              As an example of where performance dips, consider the
 *              recursive processing of a Compound Datatype. For each field
 *              in the Compound Datatype, the datatype string must be
 *              searched for the beginning and end of the field's datatype
 *              section, which corresponds to the JSON "type" key. This
 *              process generally involves at least a string search each
 *              for the beginning and end of the section, but possibly
 *              more if there are multiply nested types inside the field.
 *              E.g. a field of Compound w/ a field of Compound could take
 *              two or more string searches for both the beginning and end
 *              of the section. After the beginning and end of the datatype
 *              section for the given field has been found, this substring
 *              is then copied into a separate buffer so it can be
 *              recursively processed without modifying the original
 *              datatype string. Finally, after all of the recursive
 *              processing has been done and the singular field of the
 *              Compound Datatype has been converted from a string into an
 *              HDF5 Datatype, processing moves on to the next field in the
 *              Compound Datatype.
 *
 *
 * Return:      The identifier for the new datatype, which must be closed
 *              with H5Tclose(), if successful. Returns negative otherwise
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
static hid_t
RV_convert_string_to_datatype(const char *type)
{
    const char  *class_keys[] = { "type", "class", (const char *) 0 };
    const char  *type_base_keys[] = { "type", "base", (const char *) 0 };
    yajl_val     parse_tree = NULL, key_obj = NULL;
    hsize_t     *array_dims = NULL;
    size_t       i;
    hid_t        datatype = FAIL;
    hid_t       *compound_member_type_array = NULL;
    hid_t        enum_base_type = FAIL;
    char       **compound_member_names = NULL;
    char        *datatype_class = NULL;
    char        *array_base_type_substring = NULL;
    char        *tmp_cmpd_type_buffer = NULL;
    char        *tmp_enum_base_type_buffer = NULL;
    hid_t        ret_value = FAIL;

#ifdef PLUGIN_DEBUG
    printf("Converting String-to-Datatype buffer %s to hid_t\n", type);
#endif

    /* Retrieve the datatype class */
    if (NULL == (parse_tree = yajl_tree_parse(type, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "JSON parse tree creation failed")

    if (NULL == (key_obj = yajl_tree_get(parse_tree, class_keys, yajl_t_string)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't parse datatype from string representation")

    if (NULL == (datatype_class = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't parse datatype from string representation")

    /* Create the appropriate datatype or copy an existing one */
    if (!strcmp(datatype_class, "H5T_INTEGER")) {
        hbool_t  is_predefined = TRUE;
        char    *type_base = NULL;

        if (NULL == (key_obj = yajl_tree_get(parse_tree, type_base_keys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't retrieve datatype base type")

        if (NULL == (type_base = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't retrieve datatype base type")

        if (is_predefined) {
            hbool_t  is_unsigned;
            hid_t    predefined_type = FAIL;
            char    *type_base_ptr = type_base + 8;

#ifdef PLUGIN_DEBUG
            printf("  - Predefined Integer type sign: %c\n", *type_base_ptr);
#endif

            is_unsigned = (*type_base_ptr == 'U') ? TRUE : FALSE;

            switch (*(type_base_ptr + 1)) {
                /* 8-bit integer */
                case '8':
#ifdef PLUGIN_DEBUG
                    printf("  - 8-bit Integer type\n");
#endif

                    if (*(type_base_ptr + 2) == 'L') {
                        /* Litle-endian */
#ifdef PLUGIN_DEBUG
                        printf("  - Little-endian - %s\n\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U8LE : H5T_STD_I8LE;
                    } /* end if */
                    else {
#ifdef PLUGIN_DEBUG
                        printf("  - Big-endian - %s\n\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U8BE : H5T_STD_I8BE;
                    } /* end else */

                    break;

                /* 16-bit integer */
                case '1':
#ifdef PLUGIN_DEBUG
                    printf("  - 16-bit Integer type\n");
#endif

                    if (*(type_base_ptr + 3) == 'L') {
                        /* Litle-endian */
#ifdef PLUGIN_DEBUG
                        printf("  - Little-endian - %s\n\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U16LE : H5T_STD_I16LE;
                    } /* end if */
                    else {
#ifdef PLUGIN_DEBUG
                        printf("  - Big-endian - %s\n\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U16BE : H5T_STD_I16BE;
                    } /* end else */

                    break;

                /* 32-bit integer */
                case '3':
#ifdef PLUGIN_DEBUG
                    printf("  - 32-bit Integer type\n");
#endif

                    if (*(type_base_ptr + 3) == 'L') {
                        /* Litle-endian */
#ifdef PLUGIN_DEBUG
                        printf("  - Little-endian - %s\n\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U32LE : H5T_STD_I32LE;
                    } /* end if */
                    else {
#ifdef PLUGIN_DEBUG
                        printf("  - Big-endian - %s\n\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U32BE : H5T_STD_I32BE;
                    } /* end else */

                    break;

                /* 64-bit integer */
                case '6':
#ifdef PLUGIN_DEBUG
                    printf("  - 64-bit Integer type\n");
#endif

                    if (*(type_base_ptr + 3) == 'L') {
                        /* Litle-endian */
#ifdef PLUGIN_DEBUG
                        printf("  - Little-endian - %s\n\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U64LE : H5T_STD_I64LE;
                    } /* end if */
                    else {
#ifdef PLUGIN_DEBUG
                        printf("  - Big-endian - %s\n\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U64BE : H5T_STD_I64BE;
                    } /* end else */

                    break;
                default:
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "unknown predefined integer datatype")
            } /* end switch */

            if ((datatype = H5Tcopy(predefined_type)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCOPY, FAIL, "can't copy predefined integer datatype")
        } /* end if */
        else {
            /* XXX: Need support for non-predefined integer types */
        } /* end else */
    } /* end if */
    else if (!strcmp(datatype_class, "H5T_FLOAT")) {
        hbool_t  is_predefined = TRUE;
        hid_t    predefined_type = FAIL;
        char    *type_base = NULL;

        if (NULL == (key_obj = yajl_tree_get(parse_tree, type_base_keys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't retrieve datatype base type")

        if (NULL == (type_base = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't retrieve datatype base type")

        if (is_predefined) {
            char *type_base_ptr = type_base + 10;

#ifdef PLUGIN_DEBUG
            printf("  - Predefined Float type\n");
#endif

            switch (*type_base_ptr) {
                /* 32-bit floating point */
                case '3':
#ifdef PLUGIN_DEBUG
                    printf("  - 32-bit Floating Point - %s\n\n", (*(type_base_ptr + 2) == 'L') ? "Little-endian" : "Big-endian");
#endif

                    /* Determine whether the floating point type is big- or little-endian */
                    predefined_type = (*(type_base_ptr + 2) == 'L') ? H5T_IEEE_F32LE : H5T_IEEE_F32BE;

                    break;

                /* 64-bit floating point */
                case '6':
#ifdef PLUGIN_DEBUG
                    printf("  - 64-bit Floating Point - %s\n\n", (*(type_base_ptr + 2) == 'L') ? "Little-endian" : "Big-endian");
#endif

                    predefined_type = (*(type_base_ptr + 2) == 'L') ? H5T_IEEE_F64LE : H5T_IEEE_F64BE;

                    break;
                default:
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "unknown predefined floating-point datatype")
            } /* end switch */

            if ((datatype = H5Tcopy(predefined_type)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCOPY, FAIL, "can't copy predefined floating-point datatype")
        } /* end if */
        else {
            /* XXX: need support for non-predefined float types */
        } /* end else */
    } /* end if */
    else if (!strcmp(datatype_class, "H5T_STRING")) {
        const char *length_keys[] = { "type", "length", (const char *) 0 };
        const char *charSetKeys[] = { "type", "charSet", (const char *) 0 };
        const char *strPadKeys[] = { "type", "strPad", (const char *) 0 };
        long long   fixed_length = 0;
        hbool_t     is_variable_str;
        char       *charSet = NULL;
        char       *strPad = NULL;

#ifdef PLUGIN_DEBUG
        printf("  - String datatype:\n");
#endif

        /* Retrieve the string datatype's length and check if it's a variable-length string */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, length_keys, yajl_t_any)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't retrieve string datatype length")

        is_variable_str = YAJL_IS_STRING(key_obj);

#ifdef PLUGIN_DEBUG
        printf("  - is variable str? %d\n", is_variable_str);
#endif


        /* Retrieve and check the string datatype's character set */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, charSetKeys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't retrieve string datatype character set")

        if (NULL == (charSet = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't retrieve string datatype character set")

#ifdef PLUGIN_DEBUG
        printf("  - charSet: %s\n", charSet);
#endif

        /* Currently, only H5T_CSET_ASCII character set is supported */
        if (strcmp(charSet, "H5T_CSET_ASCII"))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "unsupported character set for string datatype")


        /* Retrieve and check the string datatype's string padding */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, strPadKeys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't retrieve string datatype padding")

        if (NULL == (strPad = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't retrieve string datatype padding")

        /* Currently, only H5T_STR_NULLPAD string padding is supported for fixed-length strings
         * and H5T_STR_NULLTERM for variable-length strings */
        if (strcmp(strPad, is_variable_str ? "H5T_STR_NULLTERM" : "H5T_STR_NULLPAD"))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "unsupported string padding for string datatype")

#ifdef PLUGIN_DEBUG
            printf("  - String padding: %s\n\n", strPad);
#endif

        /* Retrieve the length if the datatype is a fixed-length string */
        if (!is_variable_str) fixed_length = YAJL_GET_INTEGER(key_obj);
        if (fixed_length < 0) FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid datatype length")

        if ((datatype = H5Tcreate(H5T_STRING, is_variable_str ? H5T_VARIABLE : (size_t) fixed_length)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCREATE, FAIL, "can't create datatype")

        if (H5Tset_cset(datatype, H5T_CSET_ASCII) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCREATE, FAIL, "can't set character set for dataset string datatype")

        if (H5Tset_strpad(datatype, is_variable_str ? H5T_STR_NULLTERM : H5T_STR_NULLPAD) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCREATE, FAIL, "can't set string padding for dataset string datatype")
    } /* end if */
    else if (!strcmp(datatype_class, "H5T_OPAQUE")) {
        /* XXX: Need support for opaque types */
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "unsupported datatype - opaque")
    } /* end if */
    else if (!strcmp(datatype_class, "H5T_COMPOUND")) {
        const char *field_keys[] = { "type", "fields", (const char *) 0 };
        size_t      tmp_cmpd_type_buffer_size;
        size_t      total_type_size = 0;
        size_t      current_offset = 0;
        char       *type_section_ptr = NULL;

#ifdef PLUGIN_DEBUG
        printf("  - Compound Datatype:\n");
#endif

        /* Retrieve the compound member fields array */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, field_keys, yajl_t_array)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't retrieve compound datatype members array")

        if (NULL == (compound_member_type_array = (hid_t *) RV_malloc(YAJL_GET_ARRAY(key_obj)->len * sizeof(*compound_member_type_array))))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate compound datatype")
        for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++) compound_member_type_array[i] = FAIL;

        if (NULL == (compound_member_names = (char **) RV_malloc(YAJL_GET_ARRAY(key_obj)->len * sizeof(*compound_member_names))))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate compound datatype member names array")
        for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++) compound_member_names[i] = NULL;

        /* Allocate space for a temporary buffer used to extract and process the substring corresponding to
         * each compound member's datatype
         */
        tmp_cmpd_type_buffer_size = DATATYPE_BODY_DEFAULT_SIZE;
        if (NULL == (tmp_cmpd_type_buffer = (char *) RV_malloc(tmp_cmpd_type_buffer_size)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate temporary buffer for storing type information")

        /* Retrieve the names of all of the members of the Compound Datatype */
        for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++) {
            yajl_val compound_member_field;
            size_t   j;

            if (NULL == (compound_member_field = YAJL_GET_ARRAY(key_obj)->values[i]))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get compound field member %zu information", i)

            for (j = 0; j < YAJL_GET_OBJECT(compound_member_field)->len; j++) {
                if (!strcmp(YAJL_GET_OBJECT(compound_member_field)->keys[j], "name"))
                    if (NULL == (compound_member_names[i] = YAJL_GET_STRING(YAJL_GET_OBJECT(compound_member_field)->values[j])))
                        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get compound field member %zu name", j)
            } /* end for */
        } /* end for */

        /* For each field in the Compound Datatype's string representation, locate the beginning and end of its "type"
         * section and copy that substring into the temporary buffer. Then, convert that substring into an hid_t and
         * store it for later insertion once the Compound Datatype has been created.
         */

        /* Start the search from the "fields" JSON key */
        if (NULL == (type_section_ptr = strstr(type, "\"fields\"")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't find \"fields\" information section in datatype string")

        for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++) {
            size_t  type_section_len = 0;
            size_t  depth_counter = 0;
            char   *symbol_ptr = NULL;
            char    current_symbol;

            /* Find the beginning of the "type" section for this Compound Datatype member */
            if (NULL == (type_section_ptr = strstr(type_section_ptr, "\"type\"")))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't find \"type\" information section in datatype string")

            /* Search for the initial '{' brace that begins the subsection and set the initial value for the depth
             * counter, to keep track of brace depth level inside the subsection
             */
            symbol_ptr = type_section_ptr;
            while ((current_symbol = *symbol_ptr++) != '{')
                /* If we reached the end of the string before finding the beginning brace of the "type" subsection.
                 * the JSON must be misformatted
                 */
                if (!current_symbol)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't locate beginning of \"type\" subsection - misformatted JSON")

            depth_counter++;

            /* Continue forward through the string buffer character-by-character, incrementing the depth counter
             * for each '{' found and decrementing it for each '}' found, until the depth counter reaches 0 once
             * again, signalling the end of the "type" subsection
             */
            /* XXX: Note that this approach will have problems with '{' or '}' appearing inside the "type" subsection
             * where one would not normally expect it, such as in a compound datatype field name, and will either
             * cause early termination (an incomplete JSON representation) or will throw the below error about not
             * being able to locate the end of the "type" substring.
             */
            while (depth_counter) {
                current_symbol = *symbol_ptr++;

                /* If we reached the end of the string before finding the end of the "type" subsection, something is
                 * wrong. Could be misformatted JSON or could be something like a stray '{' in the subsection somewhere
                 */
                if (!current_symbol)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't locate end of \"type\" subsection - stray '{' is likely")

                if (current_symbol == '{')
                    depth_counter++;
                else if (current_symbol == '}')
                    depth_counter--;
            } /* end while */

            /* Check if the temporary buffer needs to grow to accomodate this "type" substring */
            H5_CHECKED_ASSIGN(type_section_len, size_t, symbol_ptr - type_section_ptr, ptrdiff_t);
            CHECKED_REALLOC_NO_PTR(tmp_cmpd_type_buffer, tmp_cmpd_type_buffer_size, type_section_len + 3, H5E_DATATYPE, FAIL);

            /* Copy the "type" substring into the temporary buffer, wrapping it in enclosing braces to ensure that the
             * string-to-datatype conversion function can correctly process the string
             */
            memcpy(tmp_cmpd_type_buffer + 1, type_section_ptr, type_section_len);
            tmp_cmpd_type_buffer[0] = '{'; tmp_cmpd_type_buffer[type_section_len + 1] = '}';
            tmp_cmpd_type_buffer[type_section_len + 2] = '\0';

#ifdef PLUGIN_DEBUG
            printf("  - Compound Datatype member %zu name: %s\n", i, compound_member_names[i]);
            printf("  - Compound datatype member %zu type string len: %zu\n", i, type_section_len);
#endif

            if ((compound_member_type_array[i] = RV_convert_string_to_datatype(tmp_cmpd_type_buffer)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL, "can't convert compound datatype member %zu from string representation", i)

            total_type_size += H5Tget_size(compound_member_type_array[i]);

            /* Increment the type section pointer so that the next search does not return the same subsection */
            type_section_ptr++;
        } /* end for */

        if ((datatype = H5Tcreate(H5T_COMPOUND, total_type_size)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCREATE, FAIL, "can't create compound datatype")

        /* Insert all fields into the Compound Datatype */
        for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++) {
            if (H5Tinsert(datatype, compound_member_names[i], current_offset, compound_member_type_array[i]) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTINSERT, FAIL, "can't insert compound datatype member")
            current_offset += H5Tget_size(compound_member_type_array[i]);
        } /* end for */
    } /* end if */
    else if (!strcmp(datatype_class, "H5T_ARRAY")) {
        const char * const  type_string = "{\"type\":"; /* Gets prepended to the array "base" datatype substring */
        const char         *dims_keys[] = { "type", "dims", (const char *) 0 };
        size_t              type_string_len = strlen(type_string);
        size_t              base_type_substring_len = 0;
        char               *base_type_substring_ptr = NULL;
        hid_t               base_type_id = FAIL;

        /* Retrieve the array dimensions */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, dims_keys, yajl_t_array)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't retrieve array datatype dimensions")

        if (NULL == (array_dims = (hsize_t *) RV_malloc(YAJL_GET_ARRAY(key_obj)->len * sizeof(*array_dims))))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for array dimensions")

        for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++) {
            if (YAJL_IS_INTEGER(YAJL_GET_ARRAY(key_obj)->values[i]))
                array_dims[i] = (hsize_t) YAJL_GET_INTEGER(YAJL_GET_ARRAY(key_obj)->values[i]);
        } /* end for */

#ifdef PLUGIN_DEBUG
        printf("  - Array datatype dimensions: [");
        for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++) {
            if (i > 0) printf(", ");
            printf("%llu", array_dims[i]);
        }
        printf("]\n\n");
#endif

        /* Locate the beginning and end braces of the "base" section for the array datatype */
        if (NULL == (base_type_substring_ptr = strstr(type, "\"base\"")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't find \"base\" type information in datatype string")
        if (NULL == (base_type_substring_ptr = strstr(base_type_substring_ptr, "{")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "incorrectly formatted \"base\" type information in datatype string")

        /* To find the end of the "base type" section, a quick solution is to repeatedly search for
         * '{' symbols, matching this with the same number of searches for '}', and taking the final
         * '}' to be the end of the section.
         */
        /* XXX: This will have the same issue as the _parse_datatype function in that '{' or '}' in
         * the name will cause issues
         */
        {
            size_t  key_braces_found = 0;
            char   *ptr = base_type_substring_ptr;
            char   *endptr = ptr;

            while ((ptr = strstr(ptr, "{"))) { key_braces_found++; ptr++; }
            ptr = base_type_substring_ptr;
            while (key_braces_found && (ptr = strstr(ptr, "}"))) { key_braces_found--; endptr = ptr++; }

            H5_CHECKED_ASSIGN(base_type_substring_len, size_t, endptr - base_type_substring_ptr + 1, ptrdiff_t);
        }

#ifdef PLUGIN_DEBUG
        printf("  - Array base type substring len: %zu\n", base_type_substring_len);
#endif

        /* Allocate enough memory to hold the "base" information substring, plus a few bytes for
         * the leading "type:" string and enclosing braces
         */
        if (NULL == (array_base_type_substring = (char *) RV_malloc(base_type_substring_len + type_string_len + 2)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for array base type substring")

        /* In order for the conversion function to correctly process the datatype string, it must be in the
         * form {"type": {...}}. Since the enclosing braces and the leading "type:" string are missing from
         * the substring we have extracted, add them here before processing occurs.
         */
        memcpy(array_base_type_substring, type_string, type_string_len);
        memcpy(array_base_type_substring + type_string_len, base_type_substring_ptr, base_type_substring_len);
        array_base_type_substring[type_string_len + base_type_substring_len] = '}';
        array_base_type_substring[type_string_len + base_type_substring_len + 1] = '\0';

        /* Convert the string representation of the array's base datatype to an hid_t */
        if ((base_type_id = RV_convert_string_to_datatype(array_base_type_substring)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL, "can't convert string representation of array base datatype to a usable form")

        if ((datatype = H5Tarray_create2(base_type_id, (unsigned) i, array_dims)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCREATE, FAIL, "creating array datatype failed")
    } /* end if */
    else if (!strcmp(datatype_class, "H5T_ENUM")) {
        const char * const  type_string = "{\"type\":"; /* Gets prepended to the enum "base" datatype substring */
        const char *        mapping_keys[] = { "type", "mapping", (const char *) 0 };
        size_t              type_string_len = strlen(type_string);
        size_t              base_section_len;
        char               *base_section_ptr = NULL;
        char               *base_section_end = NULL;

#ifdef PLUGIN_DEBUG
        printf("  - Enum Datatype:\n");
#endif

        /* Locate the beginning and end braces of the "base" section for the enum datatype */
        if (NULL == (base_section_ptr = strstr(type, "\"base\"")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "incorrectly formatted datatype string - missing \"base\" datatype section")
        if (NULL == (base_section_ptr = strstr(base_section_ptr, "{")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "incorrectly formatted \"base\" datatype section in datatype string")
        if (NULL == (base_section_end = strstr(base_section_ptr, "}")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "incorrectly formatted \"base\" datatype section in datatype string")

        /* Allocate enough memory to hold the "base" information substring, plus a few bytes for
         * the leading "type:" string and enclosing braces
         */
        H5_CHECKED_ASSIGN(base_section_len, size_t, (base_section_end - base_section_ptr + 1), ptrdiff_t);
        if (NULL == (tmp_enum_base_type_buffer = (char *) RV_malloc(base_section_len + type_string_len + 2)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for enum base datatype temporary buffer")

#ifdef PLUGIN_DEBUG
        printf("  - Allocated %zu bytes for enum base datatype section\n\n", base_section_len + type_string_len + 2);
#endif

        /* In order for the conversion function to correctly process the datatype string, it must be in the
         * form {"type": {...}}. Since the enclosing braces and the leading "type:" string are missing from
         * the substring we have extracted, add them here before processing occurs.
         */
        memcpy(tmp_enum_base_type_buffer, type_string, type_string_len); /* Prepend the "type" string */
        memcpy(tmp_enum_base_type_buffer + type_string_len, base_section_ptr, base_section_len); /* Append the "base" information substring */
        tmp_enum_base_type_buffer[type_string_len + base_section_len] = '}';
        tmp_enum_base_type_buffer[type_string_len + base_section_len + 1] = '\0';

#ifdef PLUGIN_DEBUG
        printf("Converting enum base datatype string to hid_t\n");
#endif

        /* Convert the enum's base datatype substring into an hid_t for use in the following H5Tenum_create call */
        if ((enum_base_type = RV_convert_string_to_datatype(tmp_enum_base_type_buffer)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL, "can't convert enum datatype's base datatype section from string into datatype")

#ifdef PLUGIN_DEBUG
        printf("Converted enum base datatype to hid_t\n\n");
#endif

        if ((datatype = H5Tenum_create(enum_base_type)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCREATE, FAIL, "can't create datatype")

        if (NULL == (key_obj = yajl_tree_get(parse_tree, mapping_keys, yajl_t_object)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't retrieve enum mapping from enum string representation")

        /* Retrieve the name and value of each member in the enum mapping, inserting them into the enum type as new members */
        for (i = 0; i < YAJL_GET_OBJECT(key_obj)->len; i++) {
            long long val;

            if (!YAJL_IS_INTEGER(YAJL_GET_OBJECT(key_obj)->values[i]))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "enum member %zu value is not an integer", i)

            val = YAJL_GET_INTEGER(YAJL_GET_OBJECT(key_obj)->values[i]);

            /* XXX: The insert call may potentially fail or produce incorrect results depending on the base
             * integer type of the enum datatype. In this case, the insert call always tries to pull data from
             * a long long.
             */
            if (H5Tenum_insert(datatype, YAJL_GET_OBJECT(key_obj)->keys[i], (void *) &val) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTINSERT, FAIL, "can't insert member into enum datatype")
        } /* end for */
    } /* end if */
    else if (!strcmp(datatype_class, "H5T_REFERENCE")) {
        char *type_base;

        if (NULL == (key_obj = yajl_tree_get(parse_tree, type_base_keys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't retrieve datatype base type")

        if (NULL == (type_base = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't retrieve datatype base type")

        if (!strcmp(type_base, "H5T_STD_REF_OBJ")) {
            if ((datatype = H5Tcopy(H5T_STD_REF_OBJ)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCOPY, FAIL, "can't copy object reference datatype")
        }
        else if (!strcmp(type_base, "H5T_STD_REF_DSETREG")) {
            if ((datatype = H5Tcopy(H5T_STD_REF_DSETREG)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCOPY, FAIL, "can't copy region reference datatype")
        }
        else
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid reference type")
    } /* end if */
    else
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "unknown datatype class")

    ret_value = datatype;

#ifdef PLUGIN_DEBUG
    printf("Converted String-to-Datatype buffer to hid_t ID %ld\n\n", datatype);
#endif

done:
    if (ret_value < 0 && datatype >= 0) {
        if (H5Tclose(datatype) < 0)
            FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, FAIL, "can't close datatype")
        if (compound_member_type_array) {
            while (FAIL != compound_member_type_array[i])
                if (H5Tclose(compound_member_type_array[i]) < 0)
                    FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, FAIL, "can't close datatype")
        } /* end if */
    } /* end if */

    if (array_dims)
        RV_free(array_dims);
    if (array_base_type_substring)
        RV_free(array_base_type_substring);
    if (compound_member_type_array)
        RV_free(compound_member_type_array);
    if (compound_member_names)
        RV_free(compound_member_names);
    if (tmp_cmpd_type_buffer)
        RV_free(tmp_cmpd_type_buffer);
    if (tmp_enum_base_type_buffer)
        RV_free(tmp_enum_base_type_buffer);
    if (FAIL != enum_base_type)
        if (H5Tclose(enum_base_type) < 0)
            FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, FAIL, "can't close enum base datatype")

    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_convert_string_to_datatype() */


/*-------------------------------------------------------------------------
 * Function:    RV_convert_obj_refs_to_buffer
 *
 * Purpose:     Given an array of rv_obj_ref_t structs, as well as the
 *              array's size, this function converts the array of object
 *              references into a binary buffer of object reference
 *              strings, which can then be transferred to the server.
 *
 *              Note that HSDS expects each element of an object reference
 *              typed dataset to be a 48-byte string, which should be
 *              enough to hold the URI of the referenced object, as well as
 *              a prefixed string corresponding to the type of the
 *              referenced object, e.g. an object reference to a group may
 *              look like "groups/g-7e538c7e-d9dd-11e7-b940-0242ac110009".
 *              Therefore, this function allocates a buffer of size
 *              (48 * number of elements in object reference array) bytes
 *              and continues to append strings until the end of the array
 *              is reached. If a string is less than 48 bytes in length,
 *              the bytes following the string's NUL terminator may be junk,
 *              but the server should be smart enough to handle this case.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static herr_t
RV_convert_obj_refs_to_buffer(const rv_obj_ref_t *ref_array, size_t ref_array_len,
    char **buf_out, size_t *buf_out_len)
{
    const char * const prefix_table[] = {
            "groups",
            "datatypes",
            "datasets"
    };
    size_t  i;
    size_t  prefix_index;
    size_t  out_len = 0;
    char   *out = NULL;
    char   *out_curr_pos;
    herr_t  ret_value = SUCCEED;

    assert(ref_array);
    assert(buf_out);
    assert(buf_out_len);

#ifdef PLUGIN_DEBUG
    printf("  - Converting object ref. array to binary buffer\n\n");
#endif

    out_len = ref_array_len * OBJECT_REF_STRING_LEN;
    if (NULL == (out = (char *) RV_malloc(out_len)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for object reference string buffer")
    out_curr_pos = out;

    for (i = 0; i < ref_array_len; i++) {
        switch (ref_array[i].ref_obj_type) {
            case H5I_FILE:
            case H5I_GROUP:
                prefix_index = 0;
                break;

            case H5I_DATATYPE:
                prefix_index = 1;
                break;

            case H5I_DATASET:
                prefix_index = 2;
                break;

            case H5I_ATTR:
            case H5I_UNINIT:
            case H5I_BADID:
            case H5I_DATASPACE:
            case H5I_REFERENCE:
            case H5I_VFL:
            case H5I_VOL:
            case H5I_GENPROP_CLS:
            case H5I_GENPROP_LST:
            case H5I_ERROR_CLASS:
            case H5I_ERROR_MSG:
            case H5I_ERROR_STACK:
            case H5I_NTYPES:
            default:
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid ref obj. type")
        } /* end switch */

        snprintf(out_curr_pos, OBJECT_REF_STRING_LEN, "%s/%s", prefix_table[prefix_index], ref_array[i].ref_obj_URI);

#ifdef PLUGIN_DEBUG
        printf("  - Ref. array[%zu] = %s\n", i, out_curr_pos);
#endif

        out_curr_pos += OBJECT_REF_STRING_LEN;
    } /* end for */

done:
    if (ret_value >= 0) {
        *buf_out = out;
        *buf_out_len = out_len;
    } /* end if */
    else {
        if (out)
            RV_free(out);
    } /* end else */

#ifdef PLUGIN_DEBUG
    printf("\n");
#endif

    return ret_value;
} /* end RV_convert_obj_refs_to_buffer() */


/*-------------------------------------------------------------------------
 * Function:    RV_convert_buffer_to_obj_refs
 *
 * Purpose:     Given a binary buffer of object reference strings, this
 *              function converts the binary buffer into a buffer of
 *              rv_obj_ref_t's which is then placed in the parameter
 *              buf_out.
 *
 *              Note that on the user's side, the buffer is expected to
 *              be an array of rv_obj_ref_t's, each of which has three
 *              fields to be populated. The first field is the reference
 *              type field, which gets set to H5R_OBJECT. The second is
 *              the URI of the object which is referenced and the final
 *              field is the type of the object which is referenced. This
 *              function is responsible for making sure each of those
 *              fields in each struct is setup correctly.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static herr_t
RV_convert_buffer_to_obj_refs(char *ref_buf, size_t ref_buf_len,
    rv_obj_ref_t **buf_out, size_t *buf_out_len)
{
    rv_obj_ref_t *out = NULL;
    size_t        i;
    size_t        out_len = 0;
    herr_t        ret_value = SUCCEED;

    assert(ref_buf);
    assert(buf_out);
    assert(buf_out_len);

#ifdef PLUGIN_DEBUG
    printf("  - Converting binary buffer to ref. array\n\n");
#endif

    out_len = ref_buf_len * sizeof(rv_obj_ref_t);
    if (NULL == (out = (rv_obj_ref_t *) RV_malloc(out_len)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for object reference array")

    for (i = 0; i < ref_buf_len; i++) {
        char *URI_start;

        out[i].ref_type = H5R_OBJECT;

        /* As the URI received from the server will have a string
         * prefix like "groups/", "datatypes/" or "datasets/", skip
         * past the prefix in order to get to the real URI.
         */
        URI_start = ref_buf + (i * OBJECT_REF_STRING_LEN);
        while ('/' != *URI_start) URI_start++;
        URI_start++;

        strncpy(out[i].ref_obj_URI, URI_start, OBJECT_REF_STRING_LEN);

#ifdef PLUGIN_DEBUG
        printf("  - Ref. array[%zu] = %s\n", i, out[i].ref_obj_URI);
#endif

        /* Since the first character of HSDS' object URIs denotes
         * the type of the object, e.g. 'g' denotes a group object,
         * capture this here.
         */
        if ('g' == *URI_start) {
            out[i].ref_obj_type = H5I_GROUP;
        } /* end if */
        else if ('t' == *URI_start) {
            out[i].ref_obj_type = H5I_DATATYPE;
        } /* end else if */
        else if ('d' == *URI_start) {
            out[i].ref_obj_type = H5I_DATASET;
        } /* end else if */
        else {
            out[i].ref_obj_type = H5I_BADID;
        } /* end else */
    } /* end for */

done:
    if (ret_value >= 0) {
        *buf_out = out;
        *buf_out_len = out_len;
    } /* end if */
    else {
        if (out)
            RV_free(out);
    } /* end else */

    return ret_value;
} /* end RV_convert_buffer_to_obj_refs() */


/*-------------------------------------------------------------------------
 * Function:    RV_parse_datatype
 *
 * Purpose:     Given a JSON representation of an HDF5 Datatype, parse the
 *              JSON and set up an actual Datatype with a corresponding
 *              hid_t for use.
 *
 *              If more information is contained within the string buffer
 *              than just the datatype information, need_truncate should
 *              be specified as TRUE to signal the fact that the substring
 *              corresponding to the datatype information should be
 *              extracted out before being passed to the
 *              string-to-datatype conversion function. Otherwise, if the
 *              caller is sure that only the datatype information is
 *              included, this function can safely be called with
 *              need_truncate specified as FALSE to avoid this processing.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 *
 */
static hid_t
RV_parse_datatype(char *type, hbool_t need_truncate)
{
    hbool_t  substring_allocated = FALSE;
    hid_t    datatype = FAIL;
    char    *type_string = type;
    char    *type_section_ptr = NULL;
    hid_t    ret_value = FAIL;

    assert(type);

    if (need_truncate) {
        size_t  substring_len;
        size_t  depth_counter = 0;
        char   *advancement_ptr;
        char    current_symbol;

        /* Start by locating the beginning of the "type" subsection, as indicated by the JSON "type" key */
        if (NULL == (type_section_ptr = strstr(type, "\"type\"")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't find \"type\" information section in datatype string")

        /* Search for the initial '{' brace that begins the subsection and set the initial value for the depth
         * counter, to keep track of brace depth level inside the subsectjon
         */
        advancement_ptr = type_section_ptr;
        while ((current_symbol = *advancement_ptr++) != '{')
            /* If we reached the end of the string before finding the beginning brace of the "type" subsection,
             * the JSON must be misformatted
             */
            if (!current_symbol)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't locate beginning of \"type\" subsection - misformatted JSON")

        depth_counter++;

        /* Continue forward through the string buffer character-by-character, incrementing the depth counter
         * for each '{' found and decrementing it for each '}' found, until the depth counter reaches 0 once
         * again, signalling the end of the "type" subsection
         */
        /* XXX: Note that this approach will have problems with '{' or '}' appearing inside the "type" subsection
         * where one would not normally expect it, such as in a compound datatype field name, and will either
         * cause early termination (an incomplete JSON representation) or will throw the below error about not
         * being able to locate the end of the "type" substring.
         */
        while (depth_counter) {
            current_symbol = *advancement_ptr++;

            /* If we reached the end of the string before finding the end of the "type" subsection, something is
             * wrong. Could be misformatted JSON or could be something like a stray '{' in the subsection somewhere
             */
            if (!current_symbol)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't locate end of \"type\" subsection - stray '{' is likely")

            if (current_symbol == '{')
                depth_counter++;
            else if (current_symbol == '}')
                depth_counter--;
        } /* end while */

        H5_CHECKED_ASSIGN(substring_len, size_t, advancement_ptr - type_section_ptr, ptrdiff_t);
        if (NULL == (type_string = (char *) RV_malloc(substring_len + 3)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for \"type\" subsection")

        memcpy(type_string + 1, type_section_ptr, substring_len);

        /* Wrap the "type" substring in braces and NULL terminate it */
        type_string[0] = '{'; type_string[substring_len + 1] = '}';
        type_string[substring_len + 2] = '\0';

        substring_allocated = TRUE;
    } /* end if */

    if ((datatype = RV_convert_string_to_datatype(type_string)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCREATE, FAIL, "can't convert string representation to datatype")

    ret_value = datatype;

done:
    if (type_string && substring_allocated)
        RV_free(type_string);

    if (ret_value < 0 && datatype >= 0)
        if (H5Tclose(datatype) < 0)
            FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, FAIL, "can't close datatype")

    return ret_value;
} /* end RV_parse_datatype() */


/*-------------------------------------------------------------------------
 * Function:    RV_parse_dataspace
 *
 * Purpose:     Given a JSON representation of an HDF5 dataspace, parse the
 *              JSON and set up an actual dataspace with a corresponding
 *              hid_t for use.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              May, 2017
 */
static hid_t
RV_parse_dataspace(char *space)
{
    const char *class_keys[] = { "shape", "class", (const char *) 0 };
    yajl_val    parse_tree = NULL, key_obj = NULL;
    hsize_t    *space_dims = NULL;
    hsize_t    *space_maxdims = NULL;
    hid_t       dataspace = FAIL;
    char       *dataspace_type = NULL;
    hid_t       ret_value = FAIL;

    assert(space);

    if (NULL == (parse_tree = yajl_tree_parse(space, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "JSON parse tree creation failed")

    /* Retrieve the Dataspace type */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, class_keys, yajl_t_string)))
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't retrieve dataspace class")

    if (NULL == (dataspace_type = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't retrieve dataspace class")

    /* Create the appropriate type of Dataspace */
    if (!strcmp(dataspace_type, "H5S_NULL")) {
        if ((dataspace = H5Screate(H5S_NULL)) < 0)
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCREATE, FAIL, "can't create null dataspace")
    } /* end if */
    else if (!strcmp(dataspace_type, "H5S_SCALAR")) {
        if ((dataspace = H5Screate(H5S_SCALAR)) < 0)
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCREATE, FAIL, "can't create scalar dataspace")
    } /* end if */
    else if (!strcmp(dataspace_type, "H5S_SIMPLE")) {
        const char *dims_keys[] =    { "shape", "dims", (const char *) 0 };
        const char *maxdims_keys[] = { "shape", "maxdims", (const char *) 0 };
        yajl_val    dims_obj = NULL, maxdims_obj = NULL;
        hbool_t     maxdims_specified = TRUE;
        size_t      i;

        if (NULL == (dims_obj = yajl_tree_get(parse_tree, dims_keys, yajl_t_array)))
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't retrieve dataspace dims")

        /* Check to see whether the maximum dimension size is specified as part of the
         * dataspace's JSON representation
         */
        if (NULL == (maxdims_obj = yajl_tree_get(parse_tree, maxdims_keys, yajl_t_array)))
            maxdims_specified = FALSE;

        if (NULL == (space_dims = (hsize_t *) RV_malloc(YAJL_GET_ARRAY(dims_obj)->len * sizeof(*space_dims))))
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate space for dataspace dimensionality array")

        if (maxdims_specified)
            if (NULL == (space_maxdims = (hsize_t *) RV_malloc(YAJL_GET_ARRAY(maxdims_obj)->len * sizeof(*space_maxdims))))
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate space for dataspace maximum dimensionality array")

        for (i = 0; i < dims_obj->u.array.len; i++) {
            long long val = YAJL_GET_INTEGER(dims_obj->u.array.values[i]);

            space_dims[i] = (hsize_t) val;

            if (maxdims_specified) {
                val = YAJL_GET_INTEGER(maxdims_obj->u.array.values[i]);

                space_maxdims[i] = (val == 0) ? H5S_UNLIMITED : (hsize_t) val;
            } /* end if */
        } /* end for */

#ifdef PLUGIN_DEBUG
        printf("Creating Simple dataspace with following: \n");
        printf("  - Dims: [ ");
        for (i = 0; i < dims_obj->u.array.len; i++) {
            if (i > 0) printf(", ");
            printf("%llu", space_dims[i]);
        }
        printf(" ]\n");
        if (maxdims_specified) {
            printf("  - MaxDims: [ ");
            for (i = 0; i < maxdims_obj->u.array.len; i++) {
                if (i > 0) printf(", ");
                printf("%llu", space_maxdims[i]);
            }
            printf(" ]\n\n");
        }
#endif

        if ((dataspace = H5Screate_simple((int) dims_obj->u.array.len, space_dims, space_maxdims)) < 0)
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCREATE, FAIL, "can't create simple dataspace")
    } /* end if */

    ret_value = dataspace;

done:
    if (space_dims)
        RV_free(space_dims);
    if (space_maxdims)
        RV_free(space_maxdims);

    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_parse_dataspace() */


/*-------------------------------------------------------------------------
 * Function:    RV_convert_dataspace_shape_to_string
 *
 * Purpose:     Given an HDF5 dataspace, converts the shape and maximum
 *              dimension size of the dataspace into JSON. The resulting
 *              string buffers must be freed by the caller, else memory
 *              will be leaked.
 *
 * Return:      Non-negative on success, negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
RV_convert_dataspace_shape_to_string(hid_t space_id, char **shape_body, char **maxdims_body)
{
    H5S_class_t  space_type;
    hsize_t     *dims = NULL;
    hsize_t     *maxdims = NULL;
    size_t       positive_ptrdiff;
    char        *shape_out_string = NULL;
    char        *maxdims_out_string = NULL;
    herr_t       ret_value = SUCCEED;

    if (H5S_NO_CLASS == (space_type = H5Sget_simple_extent_type(space_id)))
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "invalid dataspace")

    /* Scalar dataspaces operate upon the assumption that if no shape
     * is specified in the request body during the creation of an object,
     * the server will create the object with a scalar dataspace.
     */
    if (H5S_SCALAR == space_type) FUNC_GOTO_DONE(SUCCEED);

    /* Allocate space for each buffer */
    if (shape_body)
        if (NULL == (shape_out_string = (char *) RV_malloc(DATASPACE_SHAPE_BUFFER_DEFAULT_SIZE)))
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate space for dataspace shape buffer")
    if (maxdims_body)
        if (NULL == (maxdims_out_string = (char *) RV_malloc(DATASPACE_SHAPE_BUFFER_DEFAULT_SIZE)))
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate space for dataspace maximum dimension size buffer")

    /* Ensure that both buffers are NUL-terminated */
    if (shape_out_string) *shape_out_string = '\0';
    if (maxdims_out_string) *maxdims_out_string = '\0';

    switch (space_type) {
        case H5S_NULL:
        {
            const char * const null_str = "\"H5S_NULL\"";
            size_t             null_strlen = strlen(null_str);
            size_t             shape_out_string_curr_len = DATASPACE_SHAPE_BUFFER_DEFAULT_SIZE;

            CHECKED_REALLOC_NO_PTR(shape_out_string, shape_out_string_curr_len, null_strlen + 1, H5E_DATASPACE, FAIL);

            strncat(shape_out_string, null_str, null_strlen);
            break;
        } /* H5S_NULL */

        case H5S_SIMPLE:
        {
            const char * const  shape_key = "\"shape\": [";
            const char * const  maxdims_key = "\"maxdims\": [";
            size_t              shape_out_string_curr_len = DATASPACE_SHAPE_BUFFER_DEFAULT_SIZE;
            size_t              maxdims_out_string_curr_len = DATASPACE_SHAPE_BUFFER_DEFAULT_SIZE;
            size_t              i;
            char               *shape_out_string_curr_pos = shape_out_string;
            char               *maxdims_out_string_curr_pos = maxdims_out_string;
            int                 space_ndims;
            int                 bytes_printed;

            if ((space_ndims = H5Sget_simple_extent_ndims(space_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "can't get number of dimensions in dataspace")

            if (shape_out_string)
                if (NULL == (dims = (hsize_t *) RV_malloc((size_t) space_ndims * sizeof(*dims))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate memory for dataspace dimensions")

            if (maxdims_out_string)
                if (NULL == (maxdims = (hsize_t *) RV_malloc((size_t) space_ndims * sizeof(*dims))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate memory for dataspace maximum dimension sizes")

            if (H5Sget_simple_extent_dims(space_id, dims, maxdims) < 0)
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "can't retrieve dataspace dimensions and maximum dimension sizes")

            /* Add the JSON key prefixes to their respective buffers */
            if (shape_out_string) {
                size_t shape_key_len = strlen(shape_key);

                CHECKED_REALLOC_NO_PTR(shape_out_string, shape_out_string_curr_len, shape_key_len + 1, H5E_DATASPACE, FAIL);
                strncat(shape_out_string_curr_pos, shape_key, shape_key_len);
                shape_out_string_curr_pos += shape_key_len;
            } /* end if */

            if (maxdims_out_string) {
                size_t maxdims_key_len = strlen(maxdims_key);

                CHECKED_REALLOC_NO_PTR(maxdims_out_string, maxdims_out_string_curr_len, maxdims_key_len + 1, H5E_DATASPACE, FAIL);
                strncat(maxdims_out_string_curr_pos, maxdims_key, maxdims_key_len);
                maxdims_out_string_curr_pos += maxdims_key_len;
            } /* end if */

            /* For each dimension, append values to the respective string buffers according to
             * the dimension size and maximum dimension size of each dimension.
             */
            for (i = 0; i < (size_t) space_ndims; i++) {
                /* Check whether the shape and maximum dimension size string buffers
                 * need to be grown before appending the values for the next dimension
                 * into the buffers */
                if (shape_out_string) {
                    H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, shape_out_string_curr_pos - shape_out_string, ptrdiff_t);
                    CHECKED_REALLOC(shape_out_string, shape_out_string_curr_len, positive_ptrdiff + MAX_NUM_LENGTH + 1,
                                    shape_out_string_curr_pos, H5E_DATASPACE, FAIL);

                    if ((bytes_printed = sprintf(shape_out_string_curr_pos, "%s%llu", i > 0 ? "," : "", dims[i])) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "sprintf error")
                    shape_out_string_curr_pos += bytes_printed;
                } /* end if */

                if (maxdims_out_string) {
                    H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, maxdims_out_string_curr_pos - maxdims_out_string, ptrdiff_t);
                    CHECKED_REALLOC(maxdims_out_string, maxdims_out_string_curr_len, positive_ptrdiff + MAX_NUM_LENGTH + 1,
                                    maxdims_out_string_curr_pos, H5E_DATASPACE, FAIL);

                    /* According to the server specification, unlimited dimension extents should be specified
                     * as having a maxdims entry of '0'
                     */
                    if (H5S_UNLIMITED == maxdims[i]) {
                        if (i > 0) strcat(maxdims_out_string_curr_pos++, ",");
                        strcat(maxdims_out_string_curr_pos++, "0");
                    } /* end if */
                    else {
                        if ((bytes_printed = sprintf(maxdims_out_string_curr_pos, "%s%llu", i > 0 ? "," : "", maxdims[i])) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "sprintf error")
                        maxdims_out_string_curr_pos += bytes_printed;
                    } /* end else */
                } /* end if */
            } /* end for */

            if (shape_out_string) strcat(shape_out_string_curr_pos++, "]");
            if (maxdims_out_string) strcat(maxdims_out_string_curr_pos++, "]");

            break;
        } /* H5S_SIMPLE */

        case H5S_SCALAR: /* Should have already been handled above */
        case H5S_NO_CLASS:
        default:
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "can't get dataspace type")
    } /* end switch */

done:
    if (ret_value >= 0) {
        if (shape_body)
            *shape_body = shape_out_string;
        if (maxdims_body)
            *maxdims_body = maxdims_out_string;
    } /* end if */
    else {
        if (maxdims_out_string)
            RV_free(maxdims_out_string);
        if (shape_out_string)
            RV_free(shape_out_string);
    } /* end else */

    if (maxdims)
        RV_free(maxdims);
    if (dims)
        RV_free(dims);

    return ret_value;
} /* end RV_convert_dataspace_shape_to_string() */


/*-------------------------------------------------------------------------
 * Function:    RV_convert_dataspace_selection_to_string
 *
 * Purpose:     Given an HDF5 dataspace, formats the selection within the
 *              dataspace into either a JSON-based or purely string-based
 *              representation, depending on whether req_param is specified
 *              as FALSE or TRUE, respectively. This is used during dataset
 *              reads/writes in order to make a correct REST API call to
 *              the server for reading/writing a dataset by hyperslabs or
 *              point selections. The string buffer handed back by this
 *              function by the caller, else memory will be leaked.
 *
 *              When req_param is specified as TRUE, the selection is
 *              formatted purely as a string which can be included as a
 *              request parameter in the URL of a dataset write request,
 *              which is useful when doing a binary transfer of the data,
 *              since JSON can't be included in the request body in that
 *              case.
 *
 *              When req_param is specified as FALSE, the seleciton is
 *              formatted as JSON so that it can be included in the request
 *              body of a dataset read/write. This form is primarily used
 *              for point selections and hyperslab selections where the
 *              datatype of the dataset is variable-length.
 *
 * Return:      Non-negative on success, negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
RV_convert_dataspace_selection_to_string(hid_t space_id, char **selection_string, size_t *selection_string_len, hbool_t req_param)
{
    hsize_t *point_list = NULL;
    hsize_t *start = NULL;
    hsize_t *stride = NULL;
    hsize_t *count = NULL;
    hsize_t *block = NULL;
    size_t   i;
    size_t   out_string_len;
    size_t   positive_ptrdiff = 0;
    char    *out_string = NULL;
    char    *out_string_curr_pos;
    char    *start_body = NULL;
    char    *stop_body = NULL;
    char    *step_body = NULL;
    int      bytes_printed = 0;
    int      ndims;
    herr_t   ret_value = SUCCEED;

    assert(selection_string);

    out_string_len = DATASPACE_SELECTION_STRING_DEFAULT_SIZE;
    if (NULL == (out_string = (char *) RV_malloc(out_string_len)))
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate space for dataspace selection string")

    out_string_curr_pos = out_string;

    /* Ensure that the buffer is NUL-terminated */
    *out_string_curr_pos = '\0';

    if (H5I_DATASPACE != H5Iget_type(space_id))
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "not a dataspace")

    if ((ndims = H5Sget_simple_extent_ndims(space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCOUNT, FAIL, "can't retrieve dataspace dimensionality")

    if (req_param) {
        /* Format the selection in a manner such that it can be used as a request parameter in
         * an HTTP request. This is primarily the format used when the datatype of the Dataset
         * being written to/read from is a fixed-length datatype. In this case, the server can
         * support a purely binary data transfer, in which case the selection information has
         * to be sent as a request parameter instead of in the request body.
         */
        switch (H5Sget_select_type(space_id)) {
            case H5S_SEL_ALL:
            case H5S_SEL_NONE:
                break;
            case H5S_SEL_POINTS:
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "point selections are unsupported as a HTTP request parameter")

            case H5S_SEL_HYPERSLABS:
                /* Format the hyperslab selection according to the 'select' request/query parameter.
                 * This is composed of N triplets, one for each dimension of the dataspace, and looks like:
                 *
                 * [X:Y:Z, X:Y:Z, ...]
                 *
                 * where X is the starting coordinate of the selection, Y is the ending coordinate of
                 * the selection, and Z is the stride of the selection in that dimension.
                 */
                if (NULL == (start = (hsize_t *) RV_malloc((size_t) ndims * sizeof(*start))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate space for hyperslab selection 'start' values")
                if (NULL == (stride = (hsize_t *) RV_malloc((size_t) ndims * sizeof(*stride))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate space for hyperslab selection 'stride' values")
                if (NULL == (count = (hsize_t *) RV_malloc((size_t) ndims * sizeof(*count))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate space for hyperslab selection 'count' values")
                if (NULL == (block = (hsize_t *) RV_malloc((size_t) ndims * sizeof(*block))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate space for hyperslab selection 'block' values")

                /* XXX: Currently only regular hyperslabs supported */
                if (H5Sget_regular_hyperslab(space_id, start, stride, count, block) < 0)
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "can't get regular hyperslab selection")

                strcat(out_string_curr_pos++, "[");

                /* Append a tuple for each dimension of the dataspace */
                for (i = 0; i < (size_t) ndims; i++) {
                    H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
                    CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + (3 * MAX_NUM_LENGTH) + 4,
                            out_string_curr_pos, H5E_DATASPACE, FAIL);

                    /* XXX: stop values may be wrong */
                    if ((bytes_printed = sprintf(out_string_curr_pos,
                                                 "%s%llu:%llu:%llu",
                                                 i > 0 ? "," : "",
                                                 start[i],
                                                 start[i] + (stride[i] * count[i]),
                                                 stride[i]
                                         )) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "sprintf error")

                    out_string_curr_pos += bytes_printed;
                } /* end for */

                H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
                CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + 2, out_string_curr_pos, H5E_DATASPACE, FAIL);

                strcat(out_string_curr_pos++, "]");

                break;

            case H5S_SEL_ERROR:
            case H5S_SEL_N:
            default:
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "invalid selection type")
        } /* end switch */
    } /* end if */
    else {
        /* Format the selection as JSON so that it can be sent in the request body of an HTTP
         * request. This is primarily the format used when the datatype of the Dataset being
         * written to/read from is a variable-length datatype. In this case, the server cannot
         * support a purely binary data transfer, and the selection information as well as the
         * data has to be sent as JSON in the request body.
         */
        switch (H5Sget_select_type(space_id)) {
            case H5S_SEL_ALL:
            case H5S_SEL_NONE:
                break;
            case H5S_SEL_POINTS:
            {
                const char * const points_str = "\"points\": [";
                hssize_t           num_points;
                size_t             points_strlen = strlen(points_str);

                if ((num_points = H5Sget_select_npoints(space_id)) < 0)
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't get number of selected points")

                if (NULL == (point_list = (hsize_t *) RV_malloc((size_t) (ndims * num_points) * sizeof(*point_list))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate point list buffer")

                if (H5Sget_select_elem_pointlist(space_id, 0, (hsize_t) num_points, point_list) < 0)
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't retrieve point list")

                CHECKED_REALLOC(out_string, out_string_len, points_strlen + 1, out_string_curr_pos, H5E_DATASPACE, FAIL);

                strcat(out_string_curr_pos, points_str);
                out_string_curr_pos += strlen(points_str);

                for (i = 0; i < (hsize_t) num_points; i++) {
                    size_t j;

                    H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
                    CHECKED_REALLOC(out_string, out_string_len, ((size_t) ((ndims * MAX_NUM_LENGTH) + (ndims) + (ndims > 1 ? 3 : 1))),
                            out_string_curr_pos, H5E_DATASPACE, FAIL);

                    /* Add the delimiter between individual points */
                    if (i > 0) strcat(out_string_curr_pos++, ",");

                    /* Add starting bracket for the next point, if applicable */
                    if (ndims > 1) strcat(out_string_curr_pos++, "[");

                    for (j = 0; j < (size_t) ndims; j++) {
                        if ((bytes_printed = sprintf(out_string_curr_pos, "%s%llu", j > 0 ? "," : "", point_list[(i * (size_t) ndims) + j])) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "sprintf error")

                        out_string_curr_pos += bytes_printed;
                    } /* end for */

                    /* Enclose the current point in brackets */
                    if (ndims > 1) strcat(out_string_curr_pos++, "]");
                } /* end for */

                H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
                CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + 2, out_string_curr_pos, H5E_DATASPACE, FAIL);

                strcat(out_string_curr_pos++, "]");

                break;
            } /* H5S_SEL_POINTS */

            case H5S_SEL_HYPERSLABS:
            {
                /* Format the hyperslab selection according to the 'start', 'stop' and 'step' keys
                 * in a JSON request body. This looks like:
                 *
                 * "start": X, X, ...,
                 * "stop": Y, Y, ...,
                 * "step": Z, Z, ...
                 */
                char *start_body_curr_pos, *stop_body_curr_pos, *step_body_curr_pos;
                const char * const slab_format = "\"start\": %s,"
                                                 "\"stop\": %s,"
                                                 "\"step\": %s";

                if (NULL == (start = (hsize_t *) RV_malloc((size_t) ndims * sizeof(*start))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate space for hyperslab selection 'start' values")
                if (NULL == (stride = (hsize_t *) RV_malloc((size_t) ndims * sizeof(*stride))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate space for hyperslab selection 'stride' values")
                if (NULL == (count = (hsize_t *) RV_malloc((size_t) ndims * sizeof(*count))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate space for hyperslab selection 'count' values")
                if (NULL == (block = (hsize_t *) RV_malloc((size_t) ndims * sizeof(*block))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate space for hyperslab selection 'block' values")

                if (NULL == (start_body = (char *) RV_calloc((size_t) (ndims * MAX_NUM_LENGTH + ndims))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate space for hyperslab selection 'start' values string representation")
                start_body_curr_pos = start_body;

                if (NULL == (stop_body = (char *) RV_calloc((size_t) (ndims * MAX_NUM_LENGTH + ndims))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate space for hyperslab selection 'stop' values string representation")
                stop_body_curr_pos = stop_body;

                if (NULL == (step_body = (char *) RV_calloc((size_t) (ndims * MAX_NUM_LENGTH + ndims))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate space for hyperslab selection 'step' values string representation")
                step_body_curr_pos = step_body;

                strcat(start_body_curr_pos++, "[");
                strcat(stop_body_curr_pos++, "[");
                strcat(stop_body_curr_pos++, "[");

                /* XXX: Currently only regular hyperslabs supported */
                if (H5Sget_regular_hyperslab(space_id, start, stride, count, block) < 0)
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "can't get regular hyperslab selection")

                for (i = 0; i < (size_t) ndims; i++) {
                    if ((bytes_printed = sprintf(start_body_curr_pos, "%s%llu", (i > 0 ? "," : ""), start[i])) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "sprintf error")
                    start_body_curr_pos += bytes_printed;

                    /* XXX: stop body may be wrong */
                    if ((bytes_printed = sprintf(stop_body_curr_pos, "%s%llu", (i > 0 ? "," : ""), start[i] + (stride[i] * count[i]))) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "sprintf error")
                    stop_body_curr_pos += bytes_printed;

                    if ((bytes_printed = sprintf(step_body_curr_pos, "%s%llu", (i > 0 ? "," : ""), stride[i])) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "sprintf error")
                    step_body_curr_pos += bytes_printed;
                } /* end for */

                strcat(start_body_curr_pos, "]");
                strcat(stop_body_curr_pos, "]");
                strcat(step_body_curr_pos, "]");

                H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
                CHECKED_REALLOC(out_string, out_string_len,
                        positive_ptrdiff + strlen(start_body) + strlen(stop_body) + strlen(step_body) + 1,
                        out_string_curr_pos, H5E_DATASPACE, FAIL);

                if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - positive_ptrdiff,
                                              slab_format,
                                              start_body,
                                              stop_body,
                                              step_body
                                     )) < 0)
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "snprintf error")

                out_string_curr_pos += bytes_printed;

                break;
            } /* H5S_SEL_HYPERSLABS */

            case H5S_SEL_ERROR:
            case H5S_SEL_N:
            default:
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "invalid selection type")
        } /* end switch(H5Sget_select_type()) */
    } /* end else */

done:
    if (ret_value >= 0) {
        *selection_string = out_string;
        if (selection_string_len)
            H5_CHECKED_ASSIGN(*selection_string_len, size_t, out_string_curr_pos - out_string, ptrdiff_t);
    } /* end if */
    else {
        if (out_string)
            RV_free(out_string);
    } /* end else */

    if (step_body)
        RV_free(step_body);
    if (stop_body)
        RV_free(stop_body);
    if (start_body)
        RV_free(start_body);
    if (block)
        RV_free(block);
    if (count)
        RV_free(count);
    if (stride)
        RV_free(stride);
    if (start)
        RV_free(start);
    if (point_list)
        RV_free(point_list);

    return ret_value;
} /* end RV_convert_dataspace_selection_to_string() */


/*-------------------------------------------------------------------------
 * Function:    RV_setup_dataset_create_request_body
 *
 * Purpose:     Given a DCPL during a dataset create operation, converts
 *              the datatype and shape of a dataset into JSON, then
 *              combines these with a JSON-ified list of the Dataset
 *              Creation Properties, as well as an optional JSON-formatted
 *              link string to link the Dataset into the file structure,
 *              into one large string of JSON to be used as the request
 *              body during the Dataset create operation. The string
 *              buffer returned by this function must be freed by the
 *              caller, else memory will be leaked.
 *
 * Return:      Non-negative on success, negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
RV_setup_dataset_create_request_body(void *parent_obj, const char *name, hid_t dcpl,
                                     char **create_request_body, size_t *create_request_body_len)
{
    RV_object_t *pobj = (RV_object_t *) parent_obj;
    size_t       link_body_len = 0;
    size_t       creation_properties_body_len = 0;
    size_t       bytes_to_print = 0;
    size_t       datatype_body_len = 0;
    hid_t        type_id, space_id, lcpl_id;
    char        *datatype_body = NULL;
    char        *out_string = NULL;
    char        *shape_body = NULL;
    char        *maxdims_body = NULL;
    char        *creation_properties_body = NULL;
    char        *link_body = NULL;
    char        *path_dirname = NULL;
    int          bytes_printed = 0;
    herr_t       ret_value = SUCCEED;

    assert(create_request_body);
    assert((H5I_FILE == pobj->obj_type || H5I_GROUP == pobj->obj_type)
              && "parent object not a file or group");

    /* Get the type ID */
    if (H5Pget(dcpl, H5VL_PROP_DSET_TYPE_ID, &type_id) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get property value for datatype ID")

    /* Get the space ID */
    if (H5Pget(dcpl, H5VL_PROP_DSET_SPACE_ID, &space_id) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get property value for dataspace ID")

    /* Get the Link Creation property list ID */
    if (H5Pget(dcpl, H5VL_PROP_DSET_LCPL_ID, &lcpl_id) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get property value for link creation property list ID")

    /* Form the Datatype portion of the Dataset create request */
    if (RV_convert_datatype_to_string(type_id, &datatype_body, &datatype_body_len, FALSE) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCONVERT, FAIL, "can't convert datatype to string representation")

    /* If the Dataspace of the Dataset was not specified as H5P_DEFAULT, parse it. */
    if (H5P_DEFAULT != space_id)
        if (RV_convert_dataspace_shape_to_string(space_id, &shape_body, &maxdims_body) < 0)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCREATE, FAIL, "can't parse Dataset shape parameters")

    /* If the DCPL was not specified as H5P_DEFAULT, form the Dataset Creation Properties portion of the Dataset create request */
    if (H5P_DATASET_CREATE_DEFAULT != dcpl)
        if (RV_parse_dataset_creation_properties(dcpl, &creation_properties_body, &creation_properties_body_len) < 0)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCREATE, FAIL, "can't parse Dataset Creation Properties")

#ifdef PLUGIN_DEBUG
    printf("  - Dataset creation properties body: %s\n", creation_properties_body);
    printf("  - Dataset creation properties body len: %zu\n\n", creation_properties_body_len);
#endif

    /* If this isn't an H5Dcreate_anon call, create a link for the Dataset to
     * link it into the file structure */
    if (name) {
        hbool_t            empty_dirname;
        char               target_URI[URI_MAX_LENGTH];
        const char * const link_basename = RV_basename(name);
        const char * const link_body_format = "\"link\": {"
                                                 "\"id\": \"%s\", "
                                                 "\"name\": \"%s\""
                                              "}";

        /* In case the user specified a path which contains multiple groups on the way to the
         * one which the dataset will ultimately be linked under, extract out the path to the
         * final group in the chain */
        if (NULL == (path_dirname = RV_dirname(name)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "invalid pathname for dataset link")
        empty_dirname = !strcmp(path_dirname, "");

#ifdef PLUGIN_DEBUG
        printf("  - Dataset path dirname is: %s\n\n", path_dirname);
#endif

        /* If the path to the final group in the chain wasn't empty, get the URI of the final
         * group in order to correctly link the dataset into the file structure. Otherwise,
         * the supplied parent group is the one housing the dataset, so just use its URI.
         */
        if (!empty_dirname) {
            H5I_type_t obj_type = H5I_GROUP;
            htri_t     search_ret;

            search_ret = RV_find_object_by_path(pobj, path_dirname, &obj_type, RV_copy_object_URI_callback, NULL, target_URI);
            if (!search_ret || search_ret < 0)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_PATH, FAIL, "can't locate target for dataset link")
        } /* end if */

        link_body_len = strlen(link_body_format) + strlen(link_basename) + (empty_dirname ? strlen(pobj->URI) : strlen(target_URI)) + 1;
        if (NULL == (link_body = (char *) RV_malloc(link_body_len)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL, "can't allocate space for dataset link body")

        /* Form the Dataset Creation Link portion of the Dataset create request using the above format
         * specifier and the corresponding arguments */
        snprintf(link_body, link_body_len, link_body_format, empty_dirname ? pobj->URI : target_URI, link_basename);
    } /* end if */

    bytes_to_print = datatype_body_len
                   + (shape_body ? strlen(shape_body) + 2 : 0)
                   + (maxdims_body ? strlen(maxdims_body) + 2 : 0)
                   + (creation_properties_body ? creation_properties_body_len + 2 : 0)
                   + (link_body ? link_body_len + 2 : 0)
                   + 3;

    if (NULL == (out_string = (char *) RV_malloc(bytes_to_print)))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL, "can't allocate space for dataset creation request body")

    if ((bytes_printed = snprintf(out_string, bytes_to_print,
             "{%s%s%s%s%s%s%s%s%s}",
             datatype_body,                                            /* Add the required Dataset Datatype description */
             shape_body ? ", " : "",                                   /* Add separator for Dataset shape section, if specified */
             shape_body ? shape_body : "",                             /* Add the Dataset Shape description, if specified */
             maxdims_body ? ", " : "",                                 /* Add separator for Max Dims section, if specified */
             maxdims_body ? maxdims_body : "",                         /* Add the Dataset Maximum Dimension Size section, if specified */
             creation_properties_body ? ", " : "",                     /* Add separator for Dataset Creation properties section, if specified */
             creation_properties_body ? creation_properties_body : "", /* Add the Dataset Creation properties section, if specified */
             link_body ? ", " : "",                                    /* Add separator for Link Creation section, if specified */
             link_body ? link_body : "")                               /* Add the Link Creation section, if specified */
        ) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

done:
    if (ret_value >= 0) {
        *create_request_body = out_string;
        if (create_request_body_len) *create_request_body_len = (size_t) bytes_printed;
    } /* end if */
    else {
        if (out_string)
            RV_free(out_string);
    } /* end else */

    if (link_body)
        RV_free(link_body);
    if (path_dirname)
        RV_free(path_dirname);
    if (creation_properties_body)
        RV_free(creation_properties_body);
    if (maxdims_body)
        RV_free(maxdims_body);
    if (shape_body)
        RV_free(shape_body);
    if (datatype_body)
        RV_free(datatype_body);

    return ret_value;
} /* end RV_setup_dataset_create_request_body() */


/*-------------------------------------------------------------------------
 * Function:    RV_parse_dataset_creation_properties
 *
 * Purpose:     Given a DCPL during a dataset create operation, converts
 *              all of the Dataset Creation Properties, such as the
 *              filters used, Chunk Dimensionality, fill time/value, etc.,
 *              into JSON to be used during the Dataset create request.
 *              The string buffer handed back by this function must be
 *              freed by the caller, else memory will be leaked.
 *
 * Return:      Non-negative on success, negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
RV_parse_dataset_creation_properties(hid_t dcpl, char **creation_properties_body, size_t *creation_properties_body_len)
{
    const char * const  leading_string = "\"creationProperties\": {";
    H5D_alloc_time_t    alloc_time;
    size_t              leading_string_len = strlen(leading_string);
    size_t              bytes_to_print = 0;
    size_t              positive_ptrdiff = 0;
    size_t              out_string_len;
    char               *chunk_dims_string = NULL;
    char               *out_string = NULL;
    char               *out_string_curr_pos; /* The "current position" pointer used to print to the appropriate place
                                                in the buffer and not overwrite important leading data */
    int                 bytes_printed = 0;
    herr_t              ret_value = SUCCEED;

    if (!creation_properties_body)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "invalid NULL pointer for dataset creation properties string buffer")

    out_string_len = DATASET_CREATION_PROPERTIES_BODY_DEFAULT_SIZE;
    if (NULL == (out_string = (char *) RV_malloc(out_string_len)))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL, "can't allocate space for dataset creation properties string buffer")

#ifdef PLUGIN_DEBUG
    printf("  - Initial dataset creation properties string buffer size is: %zu\n\n", out_string_len);
#endif

    /* Keep track of the current position in the resulting string so everything
     * gets added smoothly
     */
    out_string_curr_pos = out_string;

    /* Make sure the buffer is at least large enough to hold the leading string */
    CHECKED_REALLOC(out_string, out_string_len, leading_string_len + 1,
            out_string_curr_pos, H5E_DATASET, FAIL);

    /* Add the leading string */
    strncpy(out_string, leading_string, out_string_len);
    out_string_curr_pos += leading_string_len;

    /* Note: At least one creation property needs to be guaranteed to be printed out in the resulting
     * output string so that each additional property can be safely appended to the string with a leading
     * comma to separate it from the other properties. Without the guarantee of at least one printed out
     * property, the result can be a missing or hanging comma in the string, depending on the combinations
     * of set/unset properties, which may result in server request errors. In this case, simply the Dataset
     * space allocation time property is chosen to always be printed to the resulting string.
     */
    if (H5Pget_alloc_time(dcpl, &alloc_time) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't retrieve alloc time property")

    /* Check whether the buffer needs to be grown */
    bytes_to_print = strlen("\"allocTime\": \"H5D_ALLOC_TIME_DEFAULT\"") + 1;

    H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
    CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

    switch (alloc_time) {
        case H5D_ALLOC_TIME_DEFAULT:
        {
            const char * const default_alloc_time = "\"allocTime\": \"H5D_ALLOC_TIME_DEFAULT\"";
            size_t             default_alloc_len = strlen(default_alloc_time);

            strncat(out_string_curr_pos, default_alloc_time, default_alloc_len);
            out_string_curr_pos += default_alloc_len;
            break;
        } /* H5D_ALLOC_TIME_DEFAULT */

        case H5D_ALLOC_TIME_EARLY:
        {
            const char * const early_alloc_time = "\"allocTime\": \"H5D_ALLOC_TIME_EARLY\"";
            size_t             early_alloc_len = strlen(early_alloc_time);

            strncat(out_string_curr_pos, early_alloc_time, early_alloc_len);
            out_string_curr_pos += early_alloc_len;
            break;
        } /* H5D_ALLOC_TIME_EARLY */

        case H5D_ALLOC_TIME_LATE:
        {
            const char * const late_alloc_time = "\"allocTime\": \"H5D_ALLOC_TIME_LATE\"";
            size_t             late_alloc_len = strlen(late_alloc_time);

            strncat(out_string_curr_pos, late_alloc_time, late_alloc_len);
            out_string_curr_pos += late_alloc_len;
            break;
        } /* H5D_ALLOC_TIME_LATE */

        case H5D_ALLOC_TIME_INCR:
        {
            const char * const incr_alloc_time = "\"allocTime\": \"H5D_ALLOC_TIME_INCR\"";
            size_t             incr_alloc_len = strlen(incr_alloc_time);

            strncat(out_string_curr_pos, incr_alloc_time, incr_alloc_len);
            out_string_curr_pos += incr_alloc_len;
            break;
        } /* H5D_ALLOC_TIME_INCR */

        case H5D_ALLOC_TIME_ERROR:
        default:
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "invalid dataset space alloc time")
    } /* end switch */


    /********************************************************************************************
     *                                                                                          *
     *                             Attribute Creation Order Section                             *
     *                                                                                          *
     * Determine the status of attribute creation order (tracked, tracked + indexed or neither) *
     * and append its string representation                                                     *
     *                                                                                          *
     ********************************************************************************************/
    {
        unsigned crt_order_flags;

        if (H5Pget_attr_creation_order(dcpl, &crt_order_flags) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't retrieve attribute creation order property")

        if (0 != crt_order_flags) {
            const char * const fmt_string = ", \"attributeCreationOrder\": \"H5P_CRT_ORDER_%s\"";

            /* Check whether the buffer needs to be grown */
            bytes_to_print = strlen(fmt_string) + strlen("INDEXED") + 1;

            H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
            CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - positive_ptrdiff, fmt_string,
                    (H5P_CRT_ORDER_INDEXED | H5P_CRT_ORDER_TRACKED) == crt_order_flags ? "INDEXED" : "TRACKED")
                ) < 0)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

            out_string_curr_pos += bytes_printed;
        } /* end if */
    }


    /****************************************************************************
     *                                                                          *
     *                 Attribute Phase Change Threshold Section                 *
     *                                                                          *
     * Determine the phase change values for attribute storage and append their *
     * string representations                                                   *
     *                                                                          *
     ****************************************************************************/
    {
        unsigned max_compact, min_dense;

        if (H5Pget_attr_phase_change(dcpl, &max_compact, &min_dense) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't retrieve attribute phase change property")

        if (DATASET_CREATE_MAX_COMPACT_ATTRIBUTES_DEFAULT != max_compact
                || DATASET_CREATE_MIN_DENSE_ATTRIBUTES_DEFAULT != min_dense) {
            const char * const fmt_string = ", \"attributePhaseChange\": {"
                                                  "\"maxCompact\": %u, "
                                                  "\"minDense\": %u"
                                              "}";

            /* Check whether the buffer needs to be grown */
            bytes_to_print = strlen(fmt_string) + (2 * MAX_NUM_LENGTH) + 1;

            H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
            CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - positive_ptrdiff, fmt_string, max_compact, min_dense)) < 0)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

            out_string_curr_pos += bytes_printed;
        } /* end if */
    }


    /**********************************************************************
     *                                                                    *
     *                         Fill Time Section                          *
     *                                                                    *
     * Determine the fill time value and append its string representation *
     *                                                                    *
     **********************************************************************/
    {
        H5D_fill_time_t fill_time;

        if (H5Pget_fill_time(dcpl, &fill_time) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't retrieve fill time property")

        if (H5D_FILL_TIME_IFSET != fill_time) {
            const char * const fmt_string = ", \"fillTime\": \"H5D_FILL_TIME_%s\"";

            /* Check whether the buffer needs to be grown */
            bytes_to_print = strlen(fmt_string) + strlen("ALLOC") + 1;

            H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
            CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - positive_ptrdiff, fmt_string,
                    H5D_FILL_TIME_ALLOC == fill_time ? "ALLOC" : "NEVER")
                ) < 0)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

            out_string_curr_pos += bytes_printed;
        } /* end if */
    }


    /******************************************************************
     *                                                                *
     *                      Fill Value Section                        *
     *                                                                *
     * Determine the fill value status for the Dataset and append its *
     * string representation if it is specified                       *
     *                                                                *
     ******************************************************************/
    {
        H5D_fill_value_t fill_status;

        if (H5Pfill_value_defined(dcpl, &fill_status) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't retrieve the \"fill value defined\" status")

        if (H5D_FILL_VALUE_DEFAULT != fill_status) {
            if (H5D_FILL_VALUE_UNDEFINED == fill_status) {
                const char * const null_value = ", \"fillValue\": null";
                size_t             null_value_len = strlen(null_value);

                /* Check whether the buffer needs to be grown */
                bytes_to_print = null_value_len + 1;

                H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
                CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

                strncat(out_string_curr_pos, null_value, null_value_len);
                out_string_curr_pos += null_value_len;
            } /* end if */
            else {
                /* XXX: Support for fill values */
            } /* end else */
        } /* end if */
    }


    /***************************************************************
     *                                                             *
     *                       Filters Section                       *
     *                                                             *
     * Determine the filters to be added to the Dataset and append *
     * their string representations                                *
     *                                                             *
     ***************************************************************/
    {
        int nfilters;

        if ((nfilters = H5Pget_nfilters(dcpl))) {
            const char * const filters_string = ", \"filters\": [ ";
            unsigned           filter_config;
            unsigned           flags;
            unsigned           cd_values[FILTER_MAX_CD_VALUES];
            size_t             filters_string_len = strlen(filters_string);
            size_t             cd_nelmts = FILTER_MAX_CD_VALUES;
            size_t             filter_namelen = FILTER_NAME_MAX_LENGTH;
            size_t             i;
            char               filter_name[FILTER_NAME_MAX_LENGTH];

            /* Check whether the buffer needs to be grown */
            bytes_to_print = filters_string_len + 1;

            H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
            CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

            strncat(out_string_curr_pos, filters_string, filters_string_len);
            out_string_curr_pos += filters_string_len;

            for (i = 0; i < (size_t) nfilters; i++) {
                if (i > 0) {
                    H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
                    CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + 1, out_string_curr_pos, H5E_DATASET, FAIL);

                    strcat(out_string_curr_pos++, ",");
                } /* end if */

                switch (H5Pget_filter2(dcpl, (unsigned) i, &flags, &cd_nelmts, cd_values, filter_namelen, filter_name, &filter_config)) {
                    case H5Z_FILTER_DEFLATE:
                    {
                        const char * const fmt_string = "{"
                                                            "\"class\": \"H5Z_FILTER_DEFLATE\","
                                                            "\"id\": %d,"
                                                            "\"level\": %d"
                                                        "}";

                        /* Check whether the buffer needs to be grown */
                        bytes_to_print = strlen(fmt_string) + (2 * MAX_NUM_LENGTH) + 1;

                        H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
                        CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - positive_ptrdiff, fmt_string,
                                H5Z_FILTER_DEFLATE,
                                cd_values[0])
                            ) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

                        out_string_curr_pos += bytes_printed;
                        break;
                    } /* H5Z_FILTER_DEFLATE */

                    case H5Z_FILTER_SHUFFLE:
                    {
                        const char * const fmt_string = "{"
                                                            "\"class\": \"H5Z_FILTER_SHUFFLE\","
                                                            "\"id\": %d"
                                                        "}";

                        /* Check whether the buffer needs to be grown */
                        bytes_to_print = strlen(fmt_string) + MAX_NUM_LENGTH + 1;

                        H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
                        CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - positive_ptrdiff, fmt_string,
                                H5Z_FILTER_SHUFFLE)
                            ) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

                        out_string_curr_pos += bytes_printed;
                        break;
                    } /* H5Z_FILTER_SHUFFLE */

                    case H5Z_FILTER_FLETCHER32:
                    {
                        const char * const fmt_string = "{"
                                                            "\"class\": \"H5Z_FILTER_FLETCHER32\","
                                                            "\"id\": %d"
                                                        "}";

                        /* Check whether the buffer needs to be grown */
                        bytes_to_print = strlen(fmt_string) + MAX_NUM_LENGTH + 1;

                        H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
                        CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - positive_ptrdiff, fmt_string,
                                H5Z_FILTER_FLETCHER32)
                            ) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

                        out_string_curr_pos += bytes_printed;
                        break;
                    } /* H5Z_FILTER_FLETCHER32 */

                    case H5Z_FILTER_SZIP:
                    {
                        const char * const fmt_string = "{"
                                                            "\"class\": \"H5Z_FILTER_SZIP\","
                                                            "\"id\": %d,"
                                                            "\"bitsPerPixel\": %u,"
                                                            "\"coding\": \"%s\","
                                                            "\"pixelsPerBlock\": %u,"
                                                            "\"pixelsPerScanline\": %u"
                                                        "}";

                        /* Check whether the buffer needs to be grown */
                        bytes_to_print = strlen(fmt_string) + (4 * MAX_NUM_LENGTH) + strlen("H5_SZIP_EC_OPTION_MASK") + 1;

                        H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
                        CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

                        /* XXX: SZIP filter shouldn't default to NN_OPTION_MASK when unsupported mask types are specified */
                        if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - positive_ptrdiff, fmt_string,
                                H5Z_FILTER_SZIP,
                                cd_values[H5Z_SZIP_PARM_BPP],
                                cd_values[H5Z_SZIP_PARM_MASK] == H5_SZIP_EC_OPTION_MASK ? "H5_SZIP_EC_OPTION_MASK" : "H5_SZIP_NN_OPTION_MASK",
                                cd_values[H5Z_SZIP_PARM_PPB],
                                cd_values[H5Z_SZIP_PARM_PPS])
                            ) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

                        out_string_curr_pos += bytes_printed;
                        break;
                    } /* H5Z_FILTER_SZIP */

                    case H5Z_FILTER_NBIT:
                    {
                        const char * const fmt_string = "{"
                                                            "\"class\": \"H5Z_FILTER_NBIT\","
                                                            "\"id\": %d"
                                                        "}";

                        /* Check whether the buffer needs to be grown */
                        bytes_to_print = strlen(fmt_string) + MAX_NUM_LENGTH + 1;

                        H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
                        CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - positive_ptrdiff, fmt_string,
                                H5Z_FILTER_NBIT)
                            ) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

                        out_string_curr_pos += bytes_printed;
                        break;
                    } /* H5Z_FILTER_NBIT */

                    case H5Z_FILTER_SCALEOFFSET:
                    {
                        const char * const fmt_string = "{"
                                                            "\"class\": \"H5Z_FILTER_SCALEOFFSET\","
                                                            "\"id\": %d,"
                                                            "\"scaleType\": \"%s\","
                                                            "\"scaleOffset\": %u"
                                                        "}";

                        /* Check whether the buffer needs to be grown */
                        bytes_to_print = strlen(fmt_string) + (2 * MAX_NUM_LENGTH) /* XXX: + */ + 1;

                        H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
                        CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - positive_ptrdiff, fmt_string,
                                H5Z_FILTER_SCALEOFFSET,
                                "", /* XXX: */
                                cd_values[1])
                            ) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

                        out_string_curr_pos += bytes_printed;
                        break;
                    } /* H5Z_FILTER_SCALEOFFSET */

                    case LZF_FILTER_ID: /* LZF Filter */
                    {
                        const char * const fmt_string = "{"
                                                            "\"class\": \"H5Z_FILTER_LZF\","
                                                            "\"id\": %d"
                                                        "}";

                        /* Check whether the buffer needs to be grown */
                        bytes_to_print = strlen(fmt_string) + MAX_NUM_LENGTH + 1;

                        H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
                        CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - positive_ptrdiff, fmt_string,
                                LZF_FILTER_ID)
                            ) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

                        out_string_curr_pos += bytes_printed;
                        break;
                    } /* LZF_FILTER_ID */

                    default: /* User-defined filter */
                    {
                        const char * const fmt_string = "{"
                                                            "\"class\": \"H5Z_FILTER_USER\","
                                                            "\"id\": %d,"
                                                            "\"parameters\": %s"
                                                        "}";

                        /* Check whether the buffer needs to be grown */
                        bytes_to_print = strlen(fmt_string) + MAX_NUM_LENGTH /* XXX: +*/ + 1;

                        H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
                        CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

                        /* XXX: Implement ID and parameter retrieval */
                        if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - positive_ptrdiff, fmt_string,
                                0,
                                "")
                            ) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

                        out_string_curr_pos += bytes_printed;
                        break;
                    } /* User-defined filter */

                    case H5Z_FILTER_ERROR:
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "invalid filter specified")
                } /* end switch */
            } /* end for */

            /* Make sure to add a closing ']' to close the filters section */
            H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
            CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + 1, out_string_curr_pos, H5E_DATASET, FAIL);

            strcat(out_string_curr_pos++, "]");
        } /* end if */
    }


    /****************************************************************************************
     *                                                                                      *
     *                                    Layout Section                                    *
     *                                                                                      *
     * Determine the layout information of the Dataset and append its string representation *
     *                                                                                      *
     ****************************************************************************************/
    switch (H5Pget_layout(dcpl)) {
        case H5D_COMPACT:
        {
            const char * const compact_layout_str = ", \"layout\": {"
                                                           "\"class\": \"H5D_COMPACT\""
                                                      "}";
            size_t             compact_layout_str_len = strlen(compact_layout_str);

            /* Check whether the buffer needs to be grown */
            bytes_to_print = compact_layout_str_len + 1;

            H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
            CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

            strncat(out_string_curr_pos, compact_layout_str, compact_layout_str_len);
            out_string_curr_pos += compact_layout_str_len;
            break;
        } /* H5D_COMPACT */

        case H5D_CONTIGUOUS:
        {
            const char * const contiguous_layout_str = ", \"layout\": {"
                                                              "\"class\": \"H5D_CONTIGUOUS\""
                                                         "}";
            size_t             contiguous_layout_str_len = strlen(contiguous_layout_str);

            /* Check whether the buffer needs to be grown */
            bytes_to_print = contiguous_layout_str_len + 1;

            H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
            CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

            /* XXX: Add support for external storage */
            strncat(out_string_curr_pos, contiguous_layout_str, contiguous_layout_str_len);
            out_string_curr_pos += contiguous_layout_str_len;
            break;
        } /* H5D_CONTIGUOUS */

        case H5D_CHUNKED:
        {
            hsize_t             chunk_dims[H5S_MAX_RANK + 1];
            size_t              i;
            char               *chunk_dims_string_curr_pos;
            int                 ndims;
            const char * const  fmt_string = ", \"layout\": {"
                                                   "\"class\": \"H5D_CHUNKED\","
                                                   "\"dims\": %s"
                                               "}";

            if ((ndims = H5Pget_chunk(dcpl, H5S_MAX_RANK + 1, chunk_dims)) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't retrieve dataset chunk dimensionality")
            assert(ndims > 0 && "no chunk dimensionality specified");

            if (NULL == (chunk_dims_string = (char *) RV_malloc((size_t) ((ndims * MAX_NUM_LENGTH) + ndims + 3))))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL, "can't allocate space for chunk dimensionality string")
            chunk_dims_string_curr_pos = chunk_dims_string;
            *chunk_dims_string_curr_pos = '\0';

            strcat(chunk_dims_string_curr_pos++, "[");

            for (i = 0; i < (size_t) ndims; i++) {
                if ((bytes_printed = snprintf(chunk_dims_string_curr_pos, MAX_NUM_LENGTH, "%s%llu", i > 0 ? "," : "", chunk_dims[i])) < 0)
                    FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

                chunk_dims_string_curr_pos += bytes_printed;
            } /* end for */

            strcat(chunk_dims_string_curr_pos++, "]");

            /* Check whether the buffer needs to be grown */
            bytes_to_print = strlen(fmt_string) + strlen(chunk_dims_string) + 1;

            H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
            CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - positive_ptrdiff, fmt_string, chunk_dims_string)) < 0)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

            out_string_curr_pos += bytes_printed;
            break;
        } /* H5D_CHUNKED */

        case H5D_VIRTUAL:
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_UNSUPPORTED, FAIL, "unsupported dataset layout: Virtual")

        case H5D_LAYOUT_ERROR:
        case H5D_NLAYOUTS:
        default:
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't retrieve dataset layout property")
    } /* end switch */


    /*************************************************************************************
     *                                                                                   *
     *                            Object Time Tracking Section                           *
     *                                                                                   *
     * Determine the status of object time tracking and append its string representation *
     *                                                                                   *
     *************************************************************************************/
    {
        hbool_t track_times;

        if (H5Pget_obj_track_times(dcpl, &track_times) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't retrieve object time tracking property")

        if (track_times) {
            const char * const track_times_true = ", \"trackTimes\": \"true\"";
            size_t             track_times_true_len = strlen(track_times_true);

            /* Check whether the buffer needs to be grown */
            bytes_to_print = track_times_true_len + 1;

            H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
            CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

            strncat(out_string_curr_pos, track_times_true, track_times_true_len);
            out_string_curr_pos += track_times_true_len;
        } /* end if */
        else {
            const char * const track_times_false = ", \"trackTimes\": \"false\"";
            size_t             track_times_false_len = strlen(track_times_false);

            /* Check whether the buffer needs to be grown */
            bytes_to_print = track_times_false_len + 1;

            H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
            CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

            strncat(out_string_curr_pos, track_times_false, track_times_false_len);
            out_string_curr_pos += track_times_false_len;
        } /* end else */
    }


    /* Make sure to add a closing '}' to close the creationProperties section */
    H5_CHECKED_ASSIGN(positive_ptrdiff, size_t, out_string_curr_pos - out_string, ptrdiff_t);
    CHECKED_REALLOC(out_string, out_string_len, positive_ptrdiff + 1, out_string_curr_pos, H5E_DATASET, FAIL);

    strcat(out_string_curr_pos++, "}");

done:
    if (ret_value >= 0) {
        *creation_properties_body = out_string;
        if (creation_properties_body_len) H5_CHECKED_ASSIGN(*creation_properties_body_len, size_t, out_string_curr_pos - out_string, ptrdiff_t);
    } /* end if */
    else {
        if (out_string)
            RV_free(out_string);
    } /* end else */

    if (chunk_dims_string)
        RV_free(chunk_dims_string);

    return ret_value;
} /* end RV_parse_dataset_creation_properties() */
