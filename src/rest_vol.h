/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of the HDF5 REST VOL connector. The full copyright      *
 * notice, including terms governing use, modification, and redistribution,  *
 * is contained in the COPYING file, which can be found at the root of the   *
 * source code distribution tree.                                            *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * Programmer:  Jordan Henderson
 *              February, 2017
 *
 * Purpose: The private header file for the REST VOL connector.
 */

#ifndef rest_vol_H
#define rest_vol_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <time.h>

#include <curl/curl.h>
#include <yajl/yajl_tree.h>

/* Includes for HDF5 */
#include "hdf5.h"
#include "H5pubconf.h"
#include "H5VLnative.h" /* For HDF5 routines that are currently *incorrectly* hidden */

/* Includes for the REST VOL itself */
#include "rest_vol_public.h"   /* REST VOL's public header file */
#include "rest_vol_config.h"   /* Defines for enabling debugging functionality in the REST VOL */
#include "util/rest_vol_err.h" /* REST VOL error reporting macros */
#include "util/rest_vol_mem.h" /* REST VOL memory management functions */

#include "rest_vol_attr.h"
#include "rest_vol_dataset.h"
#include "rest_vol_datatype.h"
#include "rest_vol_file.h"
#include "rest_vol_group.h"
#include "rest_vol_link.h"
#include "rest_vol_object.h"

/* Includes for hash table to determine object uniqueness */
#include "util/rest_vol_hash_string.h"
#include "util/rest_vol_hash_table.h"

#ifdef RV_CONNECTOR_DEBUG
#include "rest_vol_debug.h" /* REST VOL debugging functions */
#endif

/* Version number of the REST VOL connector's struct */
#define HDF5_VOL_REST_VERSION 3

/* Version number of the REST VOL connector */
#define HDF5_VOL_REST_CONN_VERSION 1

/* Class value of the REST VOL connector, as defined in H5VLpublic.h */
#define HDF5_VOL_REST_CLS_VAL (H5VL_class_value_t)(520)

#define HDF5_VOL_REST_NAME "REST"

#define UNIX_SOCKET_PREFIX "http+unix"

/* Defines for the use of HTTP status codes */
#define HTTP_INFORMATIONAL_MIN 100 /* Minimum and maximum values for the 100 class of */
#define HTTP_INFORMATIONAL_MAX 199 /* HTTP information responses */

#define HTTP_SUCCESS_MIN 200 /* Minimum and maximum values for the 200 class of */
#define HTTP_SUCCESS_MAX 299 /* HTTP success responses */

#define HTTP_REDIRECT_MIN 300 /* Minimum and maximum values for the 300 class of */
#define HTTP_REDIRECT_MAX 399 /* HTTP redirect responses */

#define HTTP_CLIENT_ERROR_MIN 400 /* Minimum and maximum values for the 400 class of */
#define HTTP_CLIENT_ERROR_MAX 499 /* HTTP client error responses */

#define HTTP_SERVER_ERROR_MIN 500 /* Minimum and maximum values for the 500 class of */
#define HTTP_SERVER_ERROR_MAX 599 /* HTTP server error responses */

/* Macros to check for various classes of HTTP response */
#define HTTP_INFORMATIONAL(status_code)                                                                      \
    (status_code >= HTTP_INFORMATIONAL_MIN && status_code <= HTTP_INFORMATIONAL_MAX)
#define HTTP_SUCCESS(status_code)  (status_code >= HTTP_SUCCESS_MIN && status_code <= HTTP_SUCCESS_MAX)
#define HTTP_REDIRECT(status_code) (status_code >= HTTP_REDIRECT_MIN && status_code <= HTTP_REDIRECT_MAX)
#define HTTP_CLIENT_ERROR(status_code)                                                                       \
    (status_code >= HTTP_CLIENT_ERROR_MIN && status_code <= HTTP_CLIENT_ERROR_MAX)
#define HTTP_SERVER_ERROR(status_code)                                                                       \
    (status_code >= HTTP_SERVER_ERROR_MIN && status_code <= HTTP_SERVER_ERROR_MAX)

/************************
 *                      *
 *        Macros        *
 *                      *
 ************************/

/* Macro to handle various HTTP response codes */
#define HANDLE_RESPONSE(response_code, ERR_MAJOR, ERR_MINOR, ret_value)                                      \
    do {                                                                                                     \
        switch (response_code) {                                                                             \
            case 200:                                                                                        \
            case 201:                                                                                        \
                break;                                                                                       \
            case 400:                                                                                        \
                FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value,                                             \
                                "400 - Malformed/Bad request for resource");                                 \
                break;                                                                                       \
            case 401:                                                                                        \
                FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value,                                             \
                                "401 - Valid authentication needed to access resource");                     \
                break;                                                                                       \
            case 403:                                                                                        \
                FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "403 - Unauthorized access to resource");   \
                break;                                                                                       \
            case 404:                                                                                        \
                FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "404 - Resource not found");                \
                break;                                                                                       \
            case 405:                                                                                        \
                FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "405 - Method not allowed");                \
                break;                                                                                       \
            case 409:                                                                                        \
                FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "409 - Resource already exists");           \
                break;                                                                                       \
            case 410:                                                                                        \
                FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "410 - Resource has been deleted");         \
                break;                                                                                       \
            case 413:                                                                                        \
                FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "413 - Selection too large");               \
                break;                                                                                       \
            case 500:                                                                                        \
                FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "500 - An internal server error occurred"); \
                break;                                                                                       \
            case 501:                                                                                        \
                FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "501 - Functionality not implemented");     \
                break;                                                                                       \
            case 503:                                                                                        \
                FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "503 - Service unavailable");               \
                break;                                                                                       \
            case 504:                                                                                        \
                FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "504 - Gateway timeout");                   \
                break;                                                                                       \
            default:                                                                                         \
                FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "Unknown error occurred");                  \
                break;                                                                                       \
        } /* end switch */                                                                                   \
    } while (0)

/* Macro to perform cURL operation and handle errors. Note that
 * this macro should not generally be called directly. Use one
 * of the below macros to call this with the appropriate arguments. */
#define CURL_PERFORM_INTERNAL(curl_ptr, handle_HTTP_response, ERR_MAJOR, ERR_MINOR, ret_value)               \
    do {                                                                                                     \
        CURLcode result = curl_easy_perform(curl_ptr);                                                       \
                                                                                                             \
        /* Reset the cURL response buffer write position pointer */                                          \
        response_buffer.curr_buf_ptr = response_buffer.buffer;                                               \
                                                                                                             \
        if (CURLE_OK != result)                                                                              \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "%s", curl_easy_strerror(result));              \
                                                                                                             \
        if (handle_HTTP_response) {                                                                          \
            long response_code;                                                                              \
                                                                                                             \
            if (CURLE_OK != curl_easy_getinfo(curl_ptr, CURLINFO_RESPONSE_CODE, &response_code))             \
                FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "can't get HTTP response code");            \
                                                                                                             \
            HANDLE_RESPONSE(response_code, ERR_MAJOR, ERR_MINOR, ret_value);                                 \
        } /* end if */                                                                                       \
    } while (0)

/* Calls the CURL_PERFORM_INTERNAL macro in such a way that any
 * HTTP error responses will cause an HDF5-like error which
 * usually calls goto and causes the function to fail. This is
 * the default behavior for most of the server requests that
 * this VOL connector makes.
 */
#define CURL_PERFORM(curl_ptr, ERR_MAJOR, ERR_MINOR, ret_value)                                              \
    CURL_PERFORM_INTERNAL(curl_ptr, TRUE, ERR_MAJOR, ERR_MINOR, ret_value)

/* Calls the CURL_PERFORM_INTERNAL macro in such a way that any
 * HTTP error responses will not cause a function failure. This
 * is generally useful in cases where a request is sent to the
 * server to test for the existence of an object, such as in the
 * behavior for H5Fcreate()'s H5F_ACC_TRUNC flag.
 */
#define CURL_PERFORM_NO_ERR(curl_ptr, ret_value)                                                             \
    CURL_PERFORM_INTERNAL(curl_ptr, FALSE, H5E_NONE_MAJOR, H5E_NONE_MINOR, ret_value)

/* Helper macro to find the matching JSON '}' symbol for a given '{' symbol. This macro is
 * used to extract out all of the JSON within a JSON object so that processing can be done
 * on it.
 */
#define FIND_JSON_SECTION_END(start_ptr, end_ptr, ERR_MAJOR, ret_value)                                      \
    do {                                                                                                     \
        hbool_t suspend_processing =                                                                         \
            FALSE; /* Whether we are suspending processing for characters inside a JSON string */            \
        size_t depth_counter =                                                                               \
            1; /* Keep track of depth until it reaches 0 again, signalling end of section */                 \
        char *advancement_ptr =                                                                              \
            start_ptr + 1; /* Pointer to increment while searching for matching '}' symbols */               \
        char current_symbol;                                                                                 \
                                                                                                             \
        while (depth_counter) {                                                                              \
            current_symbol = *advancement_ptr++;                                                             \
                                                                                                             \
            /* If we reached the end of string before finding the end of the JSON object section, something  \
             * is wrong. Most likely the JSON is misformatted, with a stray '{' in the section somewhere.    \
             */                                                                                              \
            if (!current_symbol)                                                                             \
                FUNC_GOTO_ERROR(ERR_MAJOR, H5E_PARSEERROR, ret_value,                                        \
                                "can't locate end of section - misformatted JSON likely");                   \
                                                                                                             \
            /* If we encounter a " in the buffer, we assume that this is a JSON string and we suspend        \
             * processing of '{' and '}' symbols until the matching " is found that ends the JSON string.    \
             * Note however that it is possible for the JSON string to have an escaped \" combination within \
             * it, in which case this is not the ending " and we will still suspend processing. Note further \
             * that the JSON string may also have the escaped \\ sequence within it as well. Since it is     \
             * safer to search forward in the string buffer (as we know the next character must be valid or  \
             * the NUL terminator) we check each character for the presence of a \ symbol, and if the        \
             * following character is \ or ", we just skip ahead two characters and continue on.             \
             */                                                                                              \
            if (current_symbol == '\\') {                                                                    \
                if (*advancement_ptr == '\\' || *advancement_ptr == '"') {                                   \
                    advancement_ptr++;                                                                       \
                    continue;                                                                                \
                } /* end if */                                                                               \
            }     /* end if */                                                                               \
                                                                                                             \
            /* Now safe to suspend/resume processing */                                                      \
            if (current_symbol == '"')                                                                       \
                suspend_processing = !suspend_processing;                                                    \
            else if (current_symbol == '{' && !suspend_processing)                                           \
                depth_counter++;                                                                             \
            else if (current_symbol == '}' && !suspend_processing)                                           \
                depth_counter--;                                                                             \
        } /* end while */                                                                                    \
                                                                                                             \
        end_ptr = advancement_ptr;                                                                           \
    } while (0)

/* Macro borrowed from H5private.h to assign a value of a larger type to
 * a variable of a smaller type
 */
#define ASSIGN_TO_SMALLER_SIZE(dst, dsttype, src, srctype)                                                   \
    {                                                                                                        \
        srctype _tmp_src = (srctype)(src);                                                                   \
        dsttype _tmp_dst = (dsttype)(_tmp_src);                                                              \
        assert(_tmp_src == (srctype)_tmp_dst);                                                               \
        (dst) = _tmp_dst;                                                                                    \
    }

/* Macro borrowed from H5private.h to assign a value between two types of the
 * same size, where the source type is an unsigned type and the destination
 * type is a signed type
 */
#define ASSIGN_TO_SAME_SIZE_UNSIGNED_TO_SIGNED(dst, dsttype, src, srctype)                                   \
    {                                                                                                        \
        srctype _tmp_src = (srctype)(src);                                                                   \
        dsttype _tmp_dst = (dsttype)(_tmp_src);                                                              \
        assert(_tmp_dst >= 0);                                                                               \
        assert(_tmp_src == (srctype)_tmp_dst);                                                               \
        (dst) = _tmp_dst;                                                                                    \
    }

/* Macro to change the cast for an off_t type to try and be cross-platform portable */
#ifdef H5_SIZEOF_OFF_T
#if H5_SIZEOF_OFF_T == H5_SIZEOF_INT
#define OFF_T_SPECIFIER "%d"
#define OFF_T_CAST      (int)
#elif H5_SIZEOF_OFF_T == H5_SIZEOF_LONG
#define OFF_T_SPECIFIER "%ld"
#define OFF_T_CAST      (long)
#else
/* Check to see if long long is defined */
#if defined(H5_SIZEOF_LONG_LONG) && H5_SIZEOF_LONG_LONG == H5_SIZEOF_OFF_T
#define OFF_T_SPECIFIER "%lld"
#define OFF_T_CAST      (long long)
#else
#error no suitable cast for off_t
#endif
#endif
#else
#error type off_t does not exist!
#endif

/* Occasionally, some arguments passed to a callback by use of va_arg
 * are not utilized in the particular section of the callback. This
 * macro is for silencing compiler warnings about those arguments.
 */
#define UNUSED_VAR(arg) (void)arg;

/* Macro to ensure H5_rest_id_g is initialized. */
#define H5_REST_G_INIT(ERR)                                                                                  \
    do {                                                                                                     \
        if (H5_rest_id_g < 0)                                                                                \
            if ((H5_rest_id_g = H5VLpeek_connector_id_by_value(HDF5_VOL_REST_CLS_VAL)) < 0)                  \
                FUNC_GOTO_ERROR(H5E_ID, H5E_CANTGET, ERR,                                                    \
                                "unable to get registered ID for REST VOL connector");                       \
    } while (0)

/* Capability flags for the VOL REST connector */
#define H5VL_VOL_REST_CAP_FLAGS                                                                              \
    H5VL_CAP_FLAG_FILE_BASIC                                                                                 \
    | H5VL_CAP_FLAG_ATTR_BASIC | H5VL_CAP_FLAG_ATTR_MORE | H5VL_CAP_FLAG_DATASET_BASIC |                     \
        H5VL_CAP_FLAG_GROUP_BASIC | H5VL_CAP_FLAG_LINK_BASIC | H5VL_CAP_FLAG_OBJECT_BASIC |                  \
        H5VL_CAP_FLAG_OBJECT_MORE | H5VL_CAP_FLAG_CREATION_ORDER | H5VL_CAP_FLAG_ITERATE |                   \
        H5VL_CAP_FLAG_BY_IDX | H5VL_CAP_FLAG_GET_PLIST | H5VL_CAP_FLAG_EXTERNAL_LINKS |                      \
        H5VL_CAP_FLAG_HARD_LINKS | H5VL_CAP_FLAG_SOFT_LINKS | H5VL_CAP_FLAG_TRACK_TIMES |                    \
        H5VL_CAP_FLAG_FILTERS
/**********************************
 *                                *
 *        Global Variables        *
 *                                *
 **********************************/

/*
 * The VOL connector identification number.
 */
extern hid_t H5_rest_id_g;

/*
 * The CURL pointer used for all cURL operations.
 */
extern CURL *curl;

/*
 * cURL error message buffer.
 */
extern char curl_err_buf[];

/*
 * cURL header list
 */
extern struct curl_slist *curl_headers;

/*
 * Saved copy of the base URL for operating on
 */
extern char *base_URL;

#ifdef RV_TRACK_MEM_USAGE
/*
 * Counter to keep track of the currently allocated amount of bytes
 */
extern size_t H5_rest_curr_alloc_bytes;
#endif

/* Host header string for specifying the host (Domain) for requests */
extern const char *const host_string;

/* JSON key to retrieve the ID of an object from the server */
extern const char *object_id_keys[];

/* JSON keys to retrieve the class of a link (HARD, SOFT, EXTERNAL, etc.) */
extern const char *link_class_keys[];
extern const char *link_class_keys2[];

/* JSON key to retrieve the version of server from a request to a file. */
extern const char *server_version_keys[];

/* JSON keys to retrieve a list of attributes */
extern const char *attributes_keys[];

/* JSON keys to retrieve a list of links */
extern const char *links_keys[];

/* JSON keys to retrieve all of the information from a link when doing link iteration */
extern const char *link_title_keys[];
extern const char *link_creation_time_keys[];

/* JSON keys to retrieve the collection that a hard link belongs to
 * (the type of object it points to), "groups", "datasets" or "datatypes"
 */
extern const char *link_collection_keys2[];

/* A global struct containing the buffer which cURL will write its
 * responses out to after making a call to the server. The buffer
 * in this struct is allocated upon connector initialization and is
 * dynamically grown as needed throughout the lifetime of the connector.
 */
struct response_buffer {
    char  *buffer;
    char  *curr_buf_ptr;
    size_t buffer_size;
};
extern struct response_buffer response_buffer;

/**************************
 *                        *
 *        Typedefs        *
 *                        *
 **************************/

/*
 * A struct which is used to return a link's name or the size of
 * a link's name when calling H5Lget_name_by_idx.
 */
typedef struct link_name_by_idx_data {
    size_t link_name_len;
    char  *link_name;
} link_name_by_idx_data;

/*
 * A struct which is filled out during attribute iteration and
 * contains all of the information needed to iterate through
 * attributes by both alphabetical order and creation order in
 * increasing and decreasing fashion.
 */
typedef struct attr_table_entry {
    H5A_info_t attr_info;
    double     crt_time;
    char       attr_name[ATTRIBUTE_NAME_MAX_LENGTH];
} attr_table_entry;

/* A structure which is used each time an HTTP PUT call is to be
 * made to the server. This struct contains the data buffer and its
 * size and is passed to the H5_rest_curl_read_data_callback() function to
 * copy the data from the local buffer into cURL's internal buffer.
 */
typedef struct {
    const void *buffer;
    size_t      buffer_size;
    size_t      bytes_sent;
} upload_info;

/* Structure that keeps track of semantic version. */
typedef struct server_api_version {
    size_t major;
    size_t minor;
    size_t patch;
} server_api_version;

/*
 * Definitions for the basic objects which the REST VOL uses
 * to represent various HDF5 objects internally. The base object
 * used by the REST VOL is the RV_object_t and object-specific
 * fields can be accessed through an RV_object_t's object union.
 */
typedef struct RV_object_t RV_object_t;

typedef struct RV_file_t {
    unsigned           intent;
    unsigned           ref_count;
    char              *filepath_name;
    hid_t              fcpl_id;
    hid_t              fapl_id;
    server_api_version server_version;
} RV_file_t;

typedef struct RV_group_t {
    hid_t gapl_id;
    hid_t gcpl_id;
} RV_group_t;

typedef struct RV_dataset_t {
    hid_t space_id;
    hid_t dtype_id;
    hid_t dcpl_id;
    hid_t dapl_id;
} RV_dataset_t;

typedef struct RV_attr_t {
    H5I_type_t parent_obj_type;
    char       parent_obj_URI[URI_MAX_LENGTH];
    hid_t      space_id;
    hid_t      dtype_id;
    hid_t      aapl_id;
    hid_t      acpl_id;
    char      *attr_name;
    char      *parent_name;
} RV_attr_t;

typedef struct RV_datatype_t {
    hid_t dtype_id;
    hid_t tapl_id;
    hid_t tcpl_id;
} RV_datatype_t;

struct RV_object_t {
    RV_object_t *domain;
    H5I_type_t   obj_type;
    char         URI[URI_MAX_LENGTH];
    char        *handle_path;

    union {
        RV_datatype_t datatype;
        RV_dataset_t  dataset;
        RV_group_t    group;
        RV_attr_t     attribute;
        RV_file_t     file;
    } u;
};

/*
 * A struct which is filled out and passed to the
 * iteration callback functions when calling
 * H5Literate(_by_name)/H5Lvisit(_by_name), H5Aiterate(_by_name), or H5Ovisit(_by_name)
 */
typedef struct iter_data {
    H5_iter_order_t iter_order;
    H5_index_t      index_type;
    hbool_t         is_recursive;
    hsize_t        *idx_p;
    hid_t           iter_obj_id;
    void           *op_data;

    union {
        H5A_operator2_t attr_iter_op;
        H5L_iterate_t   link_iter_op;
    } iter_function;
} iter_data;

/*
 * A struct which is filled out during link iteration and contains
 * all of the information needed to iterate through links by both
 * alphabetical order and link creation order in increasing and
 * decreasing fashion.
 */
typedef struct link_table_entry link_table_entry;
struct link_table_entry {
    H5L_info2_t link_info;
    double      crt_time;
    char        link_name[LINK_NAME_MAX_LENGTH];

    struct {
        link_table_entry *subgroup_link_table;
        size_t            num_entries;
    } subgroup;
};

/* A structure which is filled out by a callback that reads
 * the server's response. If a field was not contained in a
 * server's response, its pointer will be NULL, and this
 * must be checked after the callback is used. */
typedef struct loc_info {
    char        *URI;
    char        *GCPL_base64;
    RV_object_t *domain;
} loc_info;

/****************************
 *                          *
 *        Prototypes        *
 *                          *
 ****************************/

#ifdef __cplusplus
extern "C" {
#endif

/* Function to set the connection information for the connector to connect to the server */
herr_t H5_rest_set_connection_information(void);

/* Alternate, more portable version of the basename function which doesn't modify its argument */
const char *H5_rest_basename(const char *path);

/* Alternate, more portable version of the dirname function which doesn't modify its argument */
char *H5_rest_dirname(const char *path);

/* Helper function to parse an HTTP response according to the given parse callback function */
herr_t RV_parse_response(char *HTTP_response, void *callback_data_in, void *callback_data_out,
                         herr_t (*parse_callback)(char *, void *, void *));

/* Callback for RV_parse_response() to capture an object's URI */
herr_t RV_copy_object_URI_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out);

/* Callback for RV_parse_response() to capture an object's creation properties */
herr_t RV_copy_object_loc_info_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out);

/* Callback for RV_parse_response() to access the name of the n-th returned attribute */
herr_t RV_copy_attribute_name_by_index(char *HTTP_response, void *callback_data_in, void *callback_data_out);

/* Callback for RV_parse_response() to access the name of the n-th returned link */
herr_t RV_copy_link_name_by_index(char *HTTP_response, void *callback_data_in, void *callback_data_out);

/* Callback for RV_parse_response() to capture the version of the server api */
herr_t RV_parse_server_version(char *HTTP_response, void *callback_data_in, void *callback_data_out);

/* Helper function to find an object given a starting object to search from and a path */

htri_t RV_find_object_by_path(RV_object_t *parent_obj, const char *obj_path, H5I_type_t *target_object_type,
                              herr_t (*obj_found_callback)(char *, void *, void *), void *callback_data_in,
                              void *callback_data_out);

/* Helper function to parse a JSON string representing an HDF5 Dataspace and
 * setup an hid_t for the Dataspace */
hid_t RV_parse_dataspace(char *space);

/* Helper function to interpret a dataspace's shape and convert it into JSON */
herr_t RV_convert_dataspace_shape_to_JSON(hid_t space_id, char **shape_body, char **maxdims_body);

/* Helper functions to base64 encode/decode a binary buffer */
herr_t RV_base64_encode(const void *in, size_t in_size, char **out, size_t *out_size);
herr_t RV_base64_decode(const char *in, size_t in_size, char **out, size_t *out_size);

/* Helper to turn an object type into a string for a server request */
herr_t RV_set_object_type_header(H5I_type_t parent_obj_type, const char **parent_obj_type_header);

#define SERVER_VERSION_MATCHES_OR_EXCEEDS(version, major_needed, minor_needed, patch_needed)                 \
    (version.major > major_needed) || (version.major == major_needed && version.minor > minor_needed) ||     \
        (version.major == major_needed && version.minor == minor_needed && version.patch >= patch_needed)

#ifdef __cplusplus
}
#endif

#endif /* rest_vol_H */
