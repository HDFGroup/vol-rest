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

#define HTTP_NO_CONTENT 204 /* HTTP server code for 'No Content' response */

#define DEFAULT_POLL_TIMEOUT_MS 100

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

/* Counterpart of CURL_PERFORM that takes a response_buffer argument,
 * instead of using the global response buffer.
 * Currently not used. */
#define CURL_PERFORM_NO_GLOBAL(curl_ptr, local_response_buffer, ERR_MAJOR, ERR_MINOR, ret_value)             \
    CURL_PERFORM_INTERNAL_NO_GLOBAL(curl_ptr, response_buffer, TRUE, ERR_MAJOR, ERR_MINOR, ret_value)

#define CURL_PERFORM_INTERNAL_NO_GLOBAL(curl_ptr, local_response_buffer, handle_HTTP_response, ERR_MAJOR,    \
                                        ERR_MINOR, ret_value)                                                \
    do {                                                                                                     \
        CURLcode result = curl_easy_perform(curl_ptr);                                                       \
                                                                                                             \
        /* Reset the cURL response buffer write position pointer */                                          \
        local_response_buffer.curr_buf_ptr = local_response_buffer.buffer;                                   \
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

/* Helper macro to find the matching JSON '}' symbol for a given '{' symbol. This macro is                   \
 * used to extract out all of the JSON within a JSON object so that processing can be done                   \
 * on it.                                                                                                    \
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
        H5VL_CAP_FLAG_FILTERS | H5VL_CAP_FLAG_FILL_VALUES
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

/* Default initial size for the response buffer allocated which cURL writes
 * its responses into
 */
#define CURL_RESPONSE_BUFFER_DEFAULT_SIZE 1024

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

/* JSON keys to retrieve objects accessed through path(s) */
extern const char *h5paths_keys[];

/* JSON keys to retrieve the path to a domain in HSDS */
extern const char *domain_keys[];

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

/* Struct containing information about open objects of each type in the VOL*/
typedef struct RV_type_info {
    size_t           open_count;
    rv_hash_table_t *table;
} RV_type_info;

/* TODO - This is copied directly from the library */
#define TYPE_BITS         7
#define TYPE_MASK         (((hid_t)1 << TYPE_BITS) - 1)
#define H5I_MAX_NUM_TYPES TYPE_MASK
extern RV_type_info *RV_type_info_array_g[];

/**************************
 *                        *
 *        Typedefs        *
 *                        *
 **************************/

/* Values for the optimization of compound data reading and writing.  They indicate
 * whether the fields of the source and destination are subset of each other
 */
typedef enum {
    H5T_SUBSET_BADVALUE = -1, /* Invalid value */
    H5T_SUBSET_FALSE    = 0,  /* Source and destination aren't subset of each other */
    H5T_SUBSET_SRC,           /* Source is the subset of dest and no conversion is needed */
    H5T_SUBSET_DST,           /* Dest is the subset of source and no conversion is needed */
    H5T_SUBSET_CAP            /* Must be the last value */
} RV_subset_t;

/*
 * A struct which is used to return a link's name or the size of
 * a link's name when calling H5Lget_name_by_idx.
 */
typedef struct link_name_by_idx_data {
    size_t link_name_len;
    char  *link_name;
} link_name_by_idx_data;

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
typedef struct {
    size_t major;
    size_t minor;
    size_t patch;
} server_api_version;

/* Structure containing information to connect to and evaluate
 * features of a server */
typedef struct server_info_t {
    char              *username;
    char              *password;
    char              *base_URL;
    server_api_version version;
} server_info_t;

/*
 * Definitions for the basic objects which the REST VOL uses
 * to represent various HDF5 objects internally. The base object
 * used by the REST VOL is the RV_object_t and object-specific
 * fields can be accessed through an RV_object_t's object union.
 */
typedef struct RV_object_t RV_object_t;

typedef struct RV_file_t {
    unsigned      intent;
    unsigned      ref_count;
    char         *filepath_name;
    server_info_t server_info;
    hid_t         fcpl_id;
    hid_t         fapl_id;
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

/* Structures to hold information for cURL requests to read/write to datasets */
typedef struct dataset_write_info {
    char       *write_body;
    char       *base64_encoded_values;
    curl_off_t  write_len;
    upload_info uinfo;
    const void *buf;

    /* If writing using compound subsetting, this is a packed version of the
     *  compound type containing only the selected members */
    hid_t dense_cmpd_subset_dtype_id;
} dataset_write_info;

typedef struct dataset_read_info {
    H5S_sel_type sel_type;
    curl_off_t   post_len;
    void        *buf;
} dataset_read_info;

typedef enum transfer_type_t { UNINIT = 0, READ = 1, WRITE = 2 } transfer_type_t;

typedef enum content_type_t {
    CONTENT_TYPE_UNINIT       = 0,
    CONTENT_TYPE_JSON         = 1,
    CONTENT_TYPE_OCTET_STREAM = 2
} content_type_t;

typedef struct dataset_transfer_info {
    struct curl_slist     *curl_headers;
    char                  *host_headers;
    CURL                  *curl_easy_handle; /* An easy handle for a single transfer */
    char                   curl_err_buf[CURL_ERROR_SIZE];
    size_t                 current_backoff_duration;
    size_t                 time_of_fail;
    struct response_buffer resp_buffer;

    RV_object_t *dataset;
    char        *request_url;
    hid_t        mem_type_id;
    hid_t        mem_space_id;
    hid_t        file_space_id;
    hid_t        file_type_id;
    char        *selection_body;

    /* Fields for type conversion */
    void *tconv_buf;
    void *bkg_buf;

    transfer_type_t transfer_type;

    union {
        dataset_write_info write_info;
        dataset_read_info  read_info;
    } u;
} dataset_transfer_info;

/*
 * A struct which is filled out and passed to the link and attribute
 * iteration callback functions when calling
 * H5Literate(_by_name)/H5Lvisit(_by_name) or H5Aiterate(_by_name).
 */
typedef struct iter_data {
    H5_iter_order_t iter_order;
    H5_index_t      index_type;
    hbool_t         is_recursive;
    hsize_t        *idx_p;
    hid_t           iter_obj_id;
    unsigned        oinfo_fields;
    void           *op_data;
    RV_object_t    *iter_obj_parent;

    union {
        H5A_operator2_t attr_iter_op;
        H5L_iterate_t   link_iter_op;
        H5O_iterate2_t  object_iter_op;
    } iter_function;
} iter_data;

/*
 * A struct which is filled out during object iteration and contains
 * all of the information needed to iterate through objects by both
 * alphabetical order and object creation order in increasing and
 * decreasing fashion.
 */
typedef struct object_table_entry object_table_entry;
struct object_table_entry {
    H5O_info2_t object_info;
    H5L_info2_t link_info;
    double      crt_time;
    char        object_URI[URI_MAX_LENGTH];
    char        link_name[LINK_NAME_MAX_LENGTH];

    struct {
        object_table_entry *subgroup_object_table;
        size_t              num_entries;
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

/* Enum to indicate if the supplied read buffer can be used as a type conversion or background buffer */
typedef enum {
    RV_TCONV_REUSE_NONE,  /* Cannot reuse buffer */
    RV_TCONV_REUSE_TCONV, /* Use buffer as type conversion buffer */
    RV_TCONV_REUSE_BKG    /* Use buffer as background buffer */
} RV_tconv_reuse_t;

typedef struct get_link_val_out {
    size_t *in_buf_size;
    void   *buf;
} get_link_val_out;
/****************************
 *                          *
 *        Prototypes        *
 *                          *
 ****************************/

#ifdef __cplusplus
extern "C" {
#endif

/* Function to set the connection information on a file for the connector to connect to the server */
herr_t H5_rest_set_connection_information(server_info_t *server_info);

/* Alternate, more portable version of the basename function which doesn't modify its argument */
const char *H5_rest_basename(const char *path);

/* Alternate, more portable version of the dirname function which doesn't modify its argument */
char *H5_rest_dirname(const char *path);

/* Helper function to parse an HTTP response according to the given parse callback function */
herr_t RV_parse_response(char *HTTP_response, const void *callback_data_in, void *callback_data_out,
                         herr_t (*parse_callback)(char *, const void *, void *));

/* Callback for RV_parse_response() to capture an object's URI */
herr_t RV_copy_object_URI_callback(char *HTTP_response, const void *callback_data_in,
                                   void *callback_data_out);

/* Callback for RV_parse_response() to capture an object's creation properties */
herr_t RV_copy_object_loc_info_callback(char *HTTP_response, const void *callback_data_in,
                                        void *callback_data_out);

/* Callback for RV_parse_response() to access the name of the n-th returned attribute */
herr_t RV_copy_attribute_name_by_index(char *HTTP_response, const void *callback_data_in,
                                       void *callback_data_out);

/* Callback for RV_parse_response() to access the name of the n-th returned link */
herr_t RV_copy_link_name_by_index(char *HTTP_response, const void *callback_data_in, void *callback_data_out);

/* Callback for RV_parse_response() to capture the version of the server api */
herr_t RV_parse_server_version(char *HTTP_response, const void *callback_data_in, void *callback_data_out);

/* Helper function to find an object given a starting object to search from and a path */

htri_t RV_find_object_by_path(RV_object_t *parent_obj, const char *obj_path, H5I_type_t *target_object_type,
                              herr_t (*obj_found_callback)(char *, const void *, void *),
                              void *callback_data_in, void *callback_data_out);

/* Helper function to parse a JSON string representing an HDF5 Dataspace and
 * setup an hid_t for the Dataspace */
hid_t RV_parse_dataspace(char *space);

/* Helper function to interpret a dataspace's shape and convert it into JSON */
herr_t RV_convert_dataspace_shape_to_JSON(hid_t space_id, char **shape_body, char **maxdims_body);

/* Helper functions to base64 encode/decode a binary buffer */
herr_t RV_base64_encode(const void *in, size_t in_size, char **out, size_t *out_size);
herr_t RV_base64_decode(const char *in, size_t in_size, char **out, size_t *out_size);

/* Comparison function to compare two keys in an rv_hash_table_t */
int H5_rest_compare_string_keys(void *value1, void *value2);

/* Helper function to initialize an object's name based on its parent's name. */
herr_t RV_set_object_handle_path(const char *obj_path, const char *parent_path, char **buf);

size_t H5_rest_curl_write_data_callback_no_global(char *buffer, size_t size, size_t nmemb, void *userp);

/* Helper to turn an object type into a string for a server request */
herr_t RV_set_object_type_header(H5I_type_t parent_obj_type, const char **parent_obj_type_header);

/* Helper functions to parse an object's allocated size from server response */
herr_t RV_parse_allocated_size_cb(char *HTTP_response, void *callback_data_in, void *callback_data_out);
herr_t RV_parse_domain_allocated_size_cb(char *HTTP_response, const void *callback_data_in,
                                         void *callback_data_out);

void RV_free_visited_link_hash_table_key(rv_hash_table_key_t value);

/* Counterpart of CURL_PERFORM that takes a curl multi handle,
 * and waits until all requests on it have finished before returning. */
herr_t RV_curl_multi_perform(CURL *curl_multi_ptr, dataset_transfer_info *transfer_info, size_t count);

/* Callbacks used for post-processing after a curl request succeeds */
herr_t RV_dataset_read_cb(hid_t mem_type_id, hid_t mem_space_id, hid_t file_type_id, hid_t file_space_id,
                          void *buf, struct response_buffer resp_buffer);

/* Helper functions for cURL requests to the server */
long RV_curl_delete(CURL *curl_handle, server_info_t *server_info, const char *request_endpoint,
                    const char *filename);
long RV_curl_put(CURL *curl_handle, server_info_t *server_info, const char *request_endpoint,
                 const char *filename, upload_info *uinfo, content_type_t content_type);
long RV_curl_get(CURL *curl_handle, server_info_t *server_info, const char *request_endpoint,
                 const char *filename, content_type_t content_type);
long RV_curl_post(CURL *curl_handle, server_info_t *server_info, const char *request_endpoint,
                  const char *filename, const char *post_data, size_t post_size, content_type_t content_type);

/* Dtermine if datatype conversion is necessary */
htri_t RV_need_tconv(hid_t src_type_id, hid_t dst_type_id);

/* Initialize variables and buffers used for type conversion */
herr_t RV_tconv_init(hid_t src_type_id, size_t *src_type_size, hid_t dst_type_id, size_t *dst_type_size,
                     size_t num_elem, hbool_t clear_tconv_buf, hbool_t dst_file, void **tconv_buf,
                     void **bkg_buf, RV_tconv_reuse_t *reuse, hbool_t *fill_bkg);

/* REST VOL Datatype helper */
herr_t RV_convert_datatype_to_JSON(hid_t type_id, char **type_body, size_t *type_body_len, hbool_t nested,
                                   server_api_version server_version);

/* Helper function to escape control characters for JSON strings */
herr_t RV_JSON_escape_string(const char *in, char *out, size_t *out_size);

/* Determine if a read from file to mem dtype is a compound subset read */
herr_t RV_get_cmpd_subset_type(hid_t src_type_id, hid_t dst_type_id, RV_subset_t *subset_info);

/* Helper to get information about members in dst that are included in src compound */
herr_t RV_get_cmpd_subset_nmembers(hid_t src_type_id, hid_t dst_type_id, size_t *num_cmpd_members);

#define SERVER_VERSION_MATCHES_OR_EXCEEDS(version, major_needed, minor_needed, patch_needed)                 \
    (version.major > major_needed) || (version.major == major_needed && version.minor > minor_needed) ||     \
        (version.major == major_needed && version.minor == minor_needed && version.patch >= patch_needed)

#define SERVER_VERSION_SUPPORTS_FILL_VALUE_ENCODING(version)                                                 \
    (SERVER_VERSION_MATCHES_OR_EXCEEDS(version, 0, 8, 1))

#define SERVER_VERSION_SUPPORTS_GET_STORAGE_SIZE(version)                                                    \
    (SERVER_VERSION_MATCHES_OR_EXCEEDS(version, 0, 8, 5))

#define SERVER_VERSION_SUPPORTS_FIXED_LENGTH_UTF8(version)                                                   \
    (SERVER_VERSION_MATCHES_OR_EXCEEDS(version, 0, 8, 5))

#define SERVER_VERSION_SUPPORTS_LONG_NAMES(version) (SERVER_VERSION_MATCHES_OR_EXCEEDS(version, 0, 8, 6))

#define SERVER_VERSION_SUPPORTS_MEMBER_SELECTION(version)                                                    \
    (SERVER_VERSION_MATCHES_OR_EXCEEDS(version, 0, 8, 6))

#ifdef __cplusplus
}
#endif

#endif /* rest_vol_H */
