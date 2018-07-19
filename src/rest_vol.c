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
 *          use with the HDF Kita server (https://www.hdfgroup.org/hdf-kita).
 *
 *          HDF Kita is a web service that implements a REST-based web service
 *          for HDF5 data stores as described in the paper:
 *          http://hdfgroup.org/pubs/papers/RESTful_HDF5.pdf.
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

/* Includes for HDF5 */
#include "H5public.h"
#include "H5Fpublic.h"           /* File defines */
#include "H5Ppublic.h"           /* Property Lists    */
#include "H5Spublic.h"           /* Dataspaces        */
#include "H5VLpublic.h"          /* VOL plugins       */

/* Includes for the REST VOL itself */
#include "rest_vol.h"            /* REST VOL plugin   */
#include "rest_vol_public.h"     /* REST VOL's public header file */
#include "rest_vol_config.h"     /* Defines for enabling debugging functionality in the REST VOL */
#include "rest_vol_err.h"        /* REST VOL error reporting macros */

/* Includes for hash table to determine object uniqueness */
#include "util/rest_vol_hash_string.h"
#include "util/rest_vol_hash_table.h"

/* Macro to handle various HTTP response codes */
#define HANDLE_RESPONSE(response_code, ERR_MAJOR, ERR_MINOR, ret_value)                                                     \
do {                                                                                                                        \
    switch(response_code) {                                                                                                 \
        case 200:                                                                                                           \
        case 201:                                                                                                           \
            break;                                                                                                          \
        case 400:                                                                                                           \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "400 - Malformed/Bad request for resource\n");                 \
            break;                                                                                                          \
        case 401:                                                                                                           \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "401 - Valid username/Password needed to access resource\n");  \
            break;                                                                                                          \
        case 403:                                                                                                           \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "403 - Unauthorized access to resource\n");                    \
            break;                                                                                                          \
        case 404:                                                                                                           \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "404 - Resource not found\n");                                 \
            break;                                                                                                          \
        case 405:                                                                                                           \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "405 - Method not allowed\n");                                 \
            break;                                                                                                          \
        case 409:                                                                                                           \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "409 - Resource already exists\n");                            \
            break;                                                                                                          \
        case 410:                                                                                                           \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "410 - Resource has been deleted\n");                          \
            break;                                                                                                          \
        case 413:                                                                                                           \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "413 - Selection too large\n");                                \
            break;                                                                                                          \
        case 500:                                                                                                           \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "500 - An internal server error occurred\n");                  \
            break;                                                                                                          \
        case 501:                                                                                                           \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "501 - Functionality not implemented\n");                      \
            break;                                                                                                          \
        case 503:                                                                                                           \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "503 - Service unavailable\n");                                \
            break;                                                                                                          \
        case 504:                                                                                                           \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "504 - Gateway timeout\n");                                    \
            break;                                                                                                          \
        default:                                                                                                            \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "Unknown error occurred\n");                                   \
            break;                                                                                                          \
    } /* end switch */                                                                                                      \
} while(0)

/* Macro to perform cURL operation and handle errors. Note that
 * this macro should not generally be called directly. Use one
 * of the below macros to call this with the appropriate arguments. */
#define CURL_PERFORM_INTERNAL(curl_ptr, handle_HTTP_response, ERR_MAJOR, ERR_MINOR, ret_value)                              \
do {                                                                                                                        \
    CURLcode result = curl_easy_perform(curl_ptr);                                                                          \
                                                                                                                            \
    /* Reset the cURL response buffer write position pointer */                                                             \
    response_buffer.curr_buf_ptr = response_buffer.buffer;                                                                  \
                                                                                                                            \
    if (CURLE_OK != result)                                                                                                 \
        FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "%s", curl_easy_strerror(result))                                  \
                                                                                                                            \
    if (handle_HTTP_response) {                                                                                             \
        long response_code;                                                                                                 \
                                                                                                                            \
        if (CURLE_OK != curl_easy_getinfo(curl_ptr, CURLINFO_RESPONSE_CODE, &response_code))                                \
            FUNC_GOTO_ERROR(ERR_MAJOR, ERR_MINOR, ret_value, "can't get HTTP response code")                                \
                                                                                                                            \
        HANDLE_RESPONSE(response_code, ERR_MAJOR, ERR_MINOR, ret_value);                                                    \
    } /* end if */                                                                                                          \
} while(0)

/* Calls the CURL_PERFORM_INTERNAL macro in such a way that any
 * HTTP error responses will cause an HDF5-like error which
 * usually calls goto and causes the function to fail. This is
 * the default behavior for most of the server requests that
 * this VOL plugin makes.
 */
#define CURL_PERFORM(curl_ptr, ERR_MAJOR, ERR_MINOR, ret_value)                                                             \
CURL_PERFORM_INTERNAL(curl_ptr, TRUE, ERR_MAJOR, ERR_MINOR, ret_value)

/* Calls the CURL_PERFORM_INTERNAL macro in such a way that any
 * HTTP error responses will not cause a function failure. This
 * is generally useful in cases where a request is sent to the
 * server to test for the existence of an object, such as in the
 * behavior for H5Fcreate()'s H5F_ACC_TRUNC flag.
 */
#define CURL_PERFORM_NO_ERR(curl_ptr, ret_value)                                                                            \
CURL_PERFORM_INTERNAL(curl_ptr, FALSE, H5E_NONE_MAJOR, H5E_NONE_MINOR, ret_value)

/* Macro to check whether the size of a buffer matches the given target size
 * and reallocate the buffer if it is too small, keeping track of a given
 * pointer into the buffer. This is used when doing multiple formatted
 * prints to the same buffer. A pointer into the buffer is kept and
 * incremented so that the next print operation can continue where the
 * last one left off, and not overwrite the current contents of the buffer.
 */
#define CHECKED_REALLOC(buffer, buffer_len, target_size, ptr_to_buffer, ERR_MAJOR, ret_value)                               \
while (target_size > buffer_len) {                                                                                          \
    char *tmp_realloc;                                                                                                      \
                                                                                                                            \
    if (NULL == (tmp_realloc = (char *) RV_realloc(buffer, 2 * buffer_len))) {                                              \
        RV_free(buffer); buffer = NULL;                                                                                     \
        FUNC_GOTO_ERROR(ERR_MAJOR, H5E_CANTALLOC, ret_value, "can't allocate space")                                        \
    } /* end if */                                                                                                          \
                                                                                                                            \
    /* Place the "current position" pointer at the correct spot in the new buffer */                                        \
    if (ptr_to_buffer) ptr_to_buffer = tmp_realloc + ((char *) ptr_to_buffer - buffer);                                     \
    buffer = tmp_realloc;                                                                                                   \
    buffer_len *= 2;                                                                                                        \
}

/* Helper macro to call the above with a temporary useless variable, since directly passing
 * NULL to the macro generates invalid code
 */
#define CHECKED_REALLOC_NO_PTR(buffer, buffer_len, target_size, ERR_MAJOR, ret_value)                                       \
do {                                                                                                                        \
    char *tmp = NULL;                                                                                                       \
    CHECKED_REALLOC(buffer, buffer_len, target_size, tmp, ERR_MAJOR, ret_value);                                            \
} while (0)

/* Helper macro to find the matching JSON '}' symbol for a given '{' symbol. This macro is
 * used to extract out all of the JSON within a JSON object so that processing can be done
 * on it.
 */
#define FIND_JSON_SECTION_END(start_ptr, end_ptr, ERR_MAJOR, ret_value)                                                     \
do {                                                                                                                        \
    hbool_t  suspend_processing = FALSE; /* Whether we are suspending processing for characters inside a JSON string */     \
    size_t   depth_counter = 1; /* Keep track of depth until it reaches 0 again, signalling end of section */               \
    char    *advancement_ptr = start_ptr + 1; /* Pointer to increment while searching for matching '}' symbols */           \
    char     current_symbol;                                                                                                \
                                                                                                                            \
    while (depth_counter) {                                                                                                 \
        current_symbol = *advancement_ptr++;                                                                                \
                                                                                                                            \
        /* If we reached the end of string before finding the end of the JSON object section, something is                  \
         * wrong. Most likely the JSON is misformatted, with a stray '{' in the section somewhere.                          \
         */                                                                                                                 \
        if (!current_symbol)                                                                                                \
            FUNC_GOTO_ERROR(ERR_MAJOR, H5E_PARSEERROR, ret_value, "can't locate end of section - misformatted JSON likely") \
                                                                                                                            \
        /* If we encounter a " in the buffer, we assume that this is a JSON string and we suspend processing                \
         * of '{' and '}' symbols until the matching " is found that ends the JSON string. Note however that                \
         * it is possible for the JSON string to have an escaped \" combination within it, in which case this               \
         * is not the ending " and we will still suspend processing. Note further that the JSON string may                  \
         * also have the escaped \\ sequence within it as well. Since it is safer to search forward in the                  \
         * string buffer (as we know the next character must be valid or the NUL terminator) we check each                  \
         * character for the presence of a \ symbol, and if the following character is \ or ", we just skip                 \
         * ahead two characters and continue on.                                                                            \
         */                                                                                                                 \
        if (current_symbol == '\\') {                                                                                       \
            if (*advancement_ptr == '\\' || *advancement_ptr == '"') {                                                      \
                advancement_ptr++;                                                                                          \
                continue;                                                                                                   \
            } /* end if */                                                                                                  \
        } /* end if */                                                                                                      \
                                                                                                                            \
        /* Now safe to suspend/resume processing */                                                                         \
        if (current_symbol == '"')                                                                                          \
            suspend_processing = !suspend_processing;                                                                       \
        else if (current_symbol == '{' && !suspend_processing)                                                              \
            depth_counter++;                                                                                                \
        else if (current_symbol == '}' && !suspend_processing)                                                              \
            depth_counter--;                                                                                                \
    } /* end while */                                                                                                       \
                                                                                                                            \
    end_ptr = advancement_ptr;                                                                                              \
} while(0)


/* Macro borrowed from H5private.h to assign a value of a larger type to
 * a variable of a smaller type
 */
#define ASSIGN_TO_SMALLER_SIZE(dst, dsttype, src, srctype)                                                                  \
{                                                                                                                           \
    srctype _tmp_src = (srctype)(src);                                                                                      \
    dsttype _tmp_dst = (dsttype)(_tmp_src);                                                                                 \
    assert(_tmp_src == (srctype)_tmp_dst);                                                                                  \
    (dst) = _tmp_dst;                                                                                                       \
}

/* Macro borrowed from H5private.h to assign a value between two types of the
 * same size, where the source type is an unsigned type and the destination
 * type is a signed type
 */
#define ASSIGN_TO_SAME_SIZE_UNSIGNED_TO_SIGNED(dst, dsttype, src, srctype)                                                  \
{                                                                                                                           \
    srctype _tmp_src = (srctype)(src);                                                                                      \
    dsttype _tmp_dst = (dsttype)(_tmp_src);                                                                                 \
    assert(_tmp_dst >= 0);                                                                                                  \
    assert(_tmp_src == (srctype)_tmp_dst);                                                                                  \
    (dst) = _tmp_dst;                                                                                                       \
}

/* Macro to change the cast for an off_t type to try and be cross-platform portable */
#ifdef H5_SIZEOF_OFF_T
    #if H5_SIZEOF_OFF_T == H5_SIZEOF_INT
        #define OFF_T_SPECIFIER "%d"
        #define OFF_T_CAST (int)
    #elif H5_SIZEOF_OFF_T == H5_SIZEOF_LONG
        #define OFF_T_SPECIFIER "%ld"
        #define OFF_T_CAST (long)
    #else
        /* Check to see if long long is defined */
        #if defined(H5_SIZEOF_LONG_LONG) && H5_SIZEOF_LONG_LONG == H5_SIZEOF_OFF_T
            #define OFF_T_SPECIFIER "%lld"
            #define OFF_T_CAST (long long)
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
#define UNUSED_VAR(arg) (void) arg;

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

/* Default size for the buffer to allocate during base64-encoding if the caller
 * of RV_base64_encode supplies a 0-sized buffer.
 */
#define BASE64_ENCODE_DEFAULT_BUFFER_SIZE             33554432 /* 32MB */

/* Maximum length (in characters) of the string representation of an HDF5
 * predefined integer or floating-point type, such as H5T_STD_I8LE or
 * H5T_IEEE_F32BE
 */
#define PREDEFINED_DATATYPE_NAME_MAX_LENGTH           20

/* Defines for the use of filters */
#define LZF_FILTER_ID                    32000 /* Avoid calling this 'H5Z_FILTER_LZF'; The HDF5 Library could potentially add 'H5Z_FILTER_LZF' in the future */
#define H5Z_SCALEOFFSET_PARM_SCALETYPE   0     /* ScaleOffset filter "User" parameter for scale type */
#define H5Z_SCALEOFFSET_PARM_SCALEFACTOR 1     /* ScaleOffset filter "User" parameter for scale factor */

/*
 * The VOL plugin identification number.
 */
static hid_t REST_g = -1;

/* Identifiers for HDF5's error API */
hid_t rv_err_stack_g = -1;
hid_t rv_err_class_g = -1;
hid_t obj_err_maj_g = -1;
hid_t parse_err_min_g = -1;
hid_t link_table_err_min_g = -1;
hid_t link_table_iter_err_min_g = -1;
hid_t attr_table_err_min_g = -1;
hid_t attr_table_iter_err_min_g = -1;

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

#ifdef RV_TRACK_MEM_USAGE
/*
 * Counter to keep track of the currently allocated amount of bytes
 */
static size_t rest_curr_alloc_bytes;
#endif

/* A global struct containing the buffer which cURL will write its
 * responses out to after making a call to the server. The buffer
 * in this struct is allocated upon plugin initialization and is
 * dynamically grown as needed throughout the lifetime of the plugin.
 */
static struct {
    char   *buffer;
    char   *curr_buf_ptr;
    size_t  buffer_size;
} response_buffer;

/* A local struct which is used each time an HTTP PUT call is to be
 * made to the server. This struct contains the data buffer and its
 * size and is passed to the curl_read_data_callback() function to
 * copy the data from the local buffer into cURL's internal buffer.
 */
typedef struct {
    const void *buffer;
    size_t      buffer_size;
} upload_info;

/*
 * A struct which is filled out and passed to the callback function
 * RV_link_iter_callback or RV_attr_iter_callback when performing
 * link and attribute iteration through the calling of
 * H5Literate (_by_name)/H5Lvisit (_by_name) or H5Aiterate (_by_name).
 */
typedef struct iter_data {
    H5_iter_order_t  iter_order;
    H5_index_t       index_type;
    hbool_t          is_recursive;
    hsize_t         *idx_p;
    hid_t            iter_obj_id;
    void            *op_data;

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
    H5L_info_t link_info;
    double     crt_time;
    char       link_name[LINK_NAME_MAX_LENGTH];

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

/* Host header string for specifying the host (Domain) for requests */
const char * const host_string = "X-Hdf-domain: ";

/* List of all the JSON keys used by yajl_tree_get throughout this plugin
 * to retrieve various information from a section of JSON */

/* Keys to retrieve the ID of an object or link from the server */
const char *link_id_keys[]   = { "link", "id", (const char *) 0 };
const char *object_id_keys[] = { "id", (const char *) 0 };
const char *root_id_keys[]   = { "root", (const char *) 0 };

/* Keys to retrieve the class of a link (HARD, SOFT, EXTERNAL, etc.) */
const char *link_class_keys[]  = { "link", "class", (const char *) 0 };
const char *link_class_keys2[] = { "class", (const char *) 0 };

/* Keys to retrieve the collection that a hard link belongs to
 * (the type of object it points to), "groups", "datasets" or "datatypes"
 */
const char *link_collection_keys[]  = { "link", "collection", (const char *) 0 };
const char *link_collection_keys2[] = { "collection", (const char *) 0 };

/* Keys to retrieve the value of a soft or external link */
const char *link_path_keys[]    = { "link", "h5path", (const char *) 0 };
const char *link_path_keys2[]   = { "h5path", (const char *) 0 };
const char *link_domain_keys[]  = { "link", "h5domain", (const char *) 0 };
const char *link_domain_keys2[] = { "h5domain", (const char *) 0 };

/* Keys to retrieve all of the information from a link when doing link iteration */
const char *links_keys[]              = { "links", (const char *) 0 };
const char *link_title_keys[]         = { "title", (const char *) 0 };
const char *link_creation_time_keys[] = { "created", (const char *) 0 };

/* Keys to retrieve all of the information from an object when doing attribute iteration */
const char *attributes_keys[]         = { "attributes", (const char *) 0 };
const char *attr_name_keys[]          = { "name", (const char *) 0 };
const char *attr_creation_time_keys[] = { "created", (const char *) 0 };

/* Keys to retrieve relevant information for H5Oget_info */
const char *attribute_count_keys[] = { "attributeCount", (const char *) 0 };
const char *hrefs_keys[] = { "hrefs" , (const char *) 0 };

/* Keys to retrieve the number of links in a group */
const char *group_link_count_keys[] = { "linkCount", (const char *) 0 };

/* Keys to retrieve the various creation properties from a dataset */
const char *creation_properties_keys[]    = { "creationProperties", (const char *) 0 };
const char *alloc_time_keys[]             = { "allocTime", (const char *) 0 };
const char *creation_order_keys[]         = { "attributeCreationOrder", (const char *) 0 };
const char *attribute_phase_change_keys[] = { "attributePhaseChange", (const char *) 0 };
const char *fill_time_keys[]              = { "fillTime", (const char *) 0 };
const char *fill_value_keys[]             = { "fillValue", (const char *) 0 };
const char *filters_keys[]                = { "filters", (const char *) 0 };
const char *filter_class_keys[]           = { "class", (const char *) 0 };
const char *filter_ID_keys[]              = { "id", (const char *) 0 };
const char *layout_keys[]                 = { "layout", (const char *) 0 };
const char *track_times_keys[]            = { "trackTimes", (const char *) 0 };
const char *max_compact_keys[]            = { "maxCompact", (const char *) 0 };
const char *min_dense_keys[]              = { "minDense", (const char *) 0 };
const char *layout_class_keys[]           = { "class", (const char *) 0 };
const char *chunk_dims_keys[]             = { "dims", (const char *) 0 };
const char *external_storage_keys[]       = { "externalStorage", (const char *) 0 };

/* Keys to retrieve information about a datatype */
const char *type_class_keys[] = { "type", "class", (const char *) 0 };
const char *type_base_keys[]  = { "type", "base", (const char *) 0 };

/* Keys to retrieve information about a string datatype */
const char *str_length_keys[]  = { "type", "length", (const char *) 0 };
const char *str_charset_keys[] = { "type", "charSet", (const char *) 0 };
const char *str_pad_keys[]     = { "type", "strPad", (const char *) 0 };

/* Keys to retrieve information about a compound datatype */
const char *compound_field_keys[] = { "type", "fields", (const char *) 0 };

/* Keys to retrieve information about an array datatype */
const char *array_dims_keys[] = { "type", "dims", (const char *) 0 };

/* Keys to retrieve information about an enum datatype */
const char *enum_mapping_keys[] = { "type", "mapping", (const char *) 0 };

/* Keys to retrieve information about a dataspace */
const char *dataspace_class_keys[]    = { "shape", "class", (const char *) 0 };
const char *dataspace_dims_keys[]     = { "shape", "dims", (const char *) 0 };
const char *dataspace_max_dims_keys[] = { "shape", "maxdims", (const char *) 0 };

/* Internal initialization/termination functions which are called by
 * the public functions RVinit() and RVterm() */
static herr_t RV_init(void);
static herr_t RV_term(hid_t vtpl_id);

/* Internal malloc/free functions to track memory usage for debugging purposes */
static void *RV_malloc(size_t size);
static void *RV_calloc(size_t size);
static void *RV_realloc(void *mem, size_t size);
static void *RV_free(void *mem);

/* Function to set the connection information for the plugin to connect to the server */
static herr_t RV_set_connection_information(void);

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

/* cURL function callbacks */
static size_t curl_read_data_callback(char *buffer, size_t size, size_t nmemb, void *inptr);
static size_t curl_write_data_callback(char *buffer, size_t size, size_t nmemb, void *userp);

/* Alternate, more portable version of the basename function which doesn't modify its argument */
static const char *RV_basename(const char *path);

/* Alternate, more portable version of the dirname function which doesn't modify its argument */
static char *RV_dirname(const char *path);

/* Helper function to base64 encode a given buffer */
static herr_t RV_base64_encode(const void *in, size_t in_size, char **out, size_t *out_size);

/* Helper function to URL-encode an entire pathname by URL-encoding each of its separate components */
static char *RV_url_encode_path(const char *path);

/* H5Dscatter() callback for dataset reads */
static herr_t dataset_read_scatter_op(const void **src_buf, size_t *src_buf_bytes_used, void *op_data);

/* Qsort callback to sort links by creation order */
static int cmp_links_by_creation_order(const void *link1, const void *link2);

/* Comparison function to compare two keys in an rv_hash_table_t */
static int rv_compare_string_keys(void *value1, void *value2);

/* Helper function to parse an HTTP response according to the parse callback function */
static herr_t RV_parse_response(char *HTTP_response, void *callback_data_in, void *callback_data_out, herr_t (*parse_callback)(char *, void *, void *));

/* Set of callbacks for H5VL_rest_parse_response() */
static herr_t RV_copy_object_URI_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out);
static herr_t RV_get_link_obj_type_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out);
static herr_t RV_get_link_info_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out);
static herr_t RV_get_link_val_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out);
static herr_t RV_link_iter_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out);
static herr_t RV_attr_iter_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out);
static herr_t RV_get_attr_info_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out);
static herr_t RV_get_object_info_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out);
static herr_t RV_get_group_info_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out);
static herr_t RV_parse_dataset_creation_properties_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out);

/* Helper function to find an object given a starting object to search from and a path */
static htri_t RV_find_object_by_path(RV_object_t *parent_obj, const char *obj_path, H5I_type_t *target_object_type,
       herr_t (*obj_found_callback)(char *, void *, void *), void *callback_data_in, void *callback_data_out);

/* Conversion functions to convert a JSON-format string to an HDF5 Datatype or vice versa */
static const char *RV_convert_predefined_datatype_to_string(hid_t type_id);
static herr_t      RV_convert_datatype_to_JSON(hid_t type_id, char **type_body, size_t *type_body_len, hbool_t nested);
static hid_t       RV_convert_JSON_to_datatype(const char *type);

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
static herr_t RV_convert_dataspace_shape_to_JSON(hid_t space_id, char **shape_body, char **maxdims_body);

/* Helper function to convert a selection within an HDF5 Dataspace into a JSON-format string */
static herr_t RV_convert_dataspace_selection_to_string(hid_t space_id, char **selection_string, size_t *selection_string_len, hbool_t req_param);

/* Helper functions for creating a Dataset */
static herr_t RV_setup_dataset_create_request_body(void *parent_obj, const char *name, hid_t dcpl, char **create_request_body, size_t *create_request_body_len);
static herr_t RV_convert_dataset_creation_properties_to_JSON(hid_t dcpl_id, char **creation_properties_body, size_t *creation_properties_body_len);

/* Helper functions to work with a table of attributes for attribute iteration */
static herr_t RV_build_attr_table(char *HTTP_response, hbool_t sort, int(*sort_func)(const void *, const void *), attr_table_entry **attr_table, size_t *num_entries);
static herr_t RV_traverse_attr_table(attr_table_entry *attr_table, size_t num_entries, iter_data *iter_data);

/* Helper functions to work with a table of links for link iteration */
static herr_t RV_build_link_table(char *HTTP_response, hbool_t is_recursive, int (*sort_func)(const void *, const void *),
                                  link_table_entry **link_table, size_t *num_entries, rv_hash_table_t *visited_link_table);
static void   RV_free_link_table(link_table_entry *link_table, size_t num_entries);
static herr_t RV_traverse_link_table(link_table_entry *link_table, size_t num_entries, iter_data *iter_data, const char *cur_link_rel_path);

static void RV_free_visited_link_hash_table_key(rv_hash_table_key_t value);

#ifdef RV_PLUGIN_DEBUG
/* Helper functions to print out useful debugging information */
static const char *object_type_to_string(H5I_type_t obj_type);
static const char *object_type_to_string2(H5O_type_t obj_type);
static const char *datatype_class_to_string(hid_t dtype);
static const char *link_class_to_string(H5L_type_t link_type);
static const char *attr_get_type_to_string(H5VL_attr_get_t get_type);
static const char *attr_specific_type_to_string(H5VL_attr_specific_t specific_type);
static const char *datatype_get_type_to_string(H5VL_datatype_get_t get_type);
static const char *dataset_get_type_to_string(H5VL_dataset_get_t get_type);
static const char *dataset_specific_type_to_string(H5VL_dataset_specific_t specific_type);
static const char *file_flags_to_string(unsigned flags);
static const char *file_get_type_to_string(H5VL_file_get_t get_type);
static const char *file_specific_type_to_string(H5VL_file_specific_t specific_type);
static const char *file_optional_type_to_string(H5VL_file_optional_t optional_type);
static const char *group_get_type_to_string(H5VL_group_get_t get_type);
static const char *link_create_type_to_string(H5VL_link_create_type_t link_create_type);
static const char *link_get_type_to_string(H5VL_link_get_t get_type);
static const char *link_specific_type_to_string(H5VL_link_specific_t specific_type);
static const char *object_get_type_to_string(H5VL_object_get_t get_type);
static const char *object_specific_type_to_string(H5VL_object_specific_t specific_type);
static const char *object_optional_type_to_string(H5VL_object_optional_t optional_type);
#endif

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

#ifdef RV_TRACK_MEM_USAGE
    /* Initialize allocated memory counter */
    rest_curr_alloc_bytes = 0;
#endif

    /* Initialize cURL */
    if (CURLE_OK != curl_global_init(CURL_GLOBAL_ALL))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't initialize cURL")

    if (NULL == (curl = curl_easy_init()))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't initialize cURL easy handle")

    /* Instruct cURL to use the buffer for error messages */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_err_buf))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set cURL error buffer")

    /* Allocate buffer for cURL to write responses to */
    if (NULL == (response_buffer.buffer = (char *) RV_malloc(CURL_RESPONSE_BUFFER_DEFAULT_SIZE)))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTALLOC, FAIL, "can't allocate cURL response buffer")
    response_buffer.buffer_size = CURL_RESPONSE_BUFFER_DEFAULT_SIZE;
    response_buffer.curr_buf_ptr = response_buffer.buffer;

    /* Redirect cURL output to response buffer */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_data_callback))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set cURL write function: %s", curl_err_buf)

    /* Set cURL read function for UPLOAD operations */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_READFUNCTION, curl_read_data_callback))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set cURL read function: %s", curl_err_buf)

#ifdef RV_CURL_DEBUG
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
#endif

    /* Register the plugin with HDF5's error reporting API */
    if ((rv_err_class_g = H5Eregister_class(REST_VOL_CLS_NAME, REST_VOL_LIB_NAME, REST_VOL_VER)) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't register with HDF5 error API")

    /* Create a separate error stack for the REST VOL to report errors with */
    if ((rv_err_stack_g = H5Ecreate_stack()) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't create error stack")

    /* Set up a few REST VOL-specific error API message classes */
    if ((obj_err_maj_g = H5Ecreate_msg(rv_err_class_g, H5E_MAJOR, "Object interface")) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't create error message for object interface")
    if ((parse_err_min_g = H5Ecreate_msg(rv_err_class_g, H5E_MINOR, "Error occurred while parsing JSON")) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't create error message for JSON parsing failures")
    if ((link_table_err_min_g = H5Ecreate_msg(rv_err_class_g, H5E_MINOR, "Can't build table of links for iteration")) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't create error message for link table build error")
    if ((link_table_iter_err_min_g = H5Ecreate_msg(rv_err_class_g, H5E_MINOR, "Can't iterate through link table")) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't create error message for link table iteration error")
    if ((attr_table_err_min_g = H5Ecreate_msg(rv_err_class_g, H5E_MINOR, "Can't build table of attribute's for iteration")) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't create error message for attribute table build error")
    if ((attr_table_iter_err_min_g = H5Ecreate_msg(rv_err_class_g, H5E_MINOR, "Can't iterate through attribute table")) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't create message for attribute iteration error")

    /* Register the plugin with the library */
    if (RV_init() < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't initialize REST VOL plugin")

done:
    /* Cleanup if REST VOL plugin initialization failed */
    if (ret_value < 0)
        RVterm();

    PRINT_ERROR_STACK

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
        if ((REST_g = H5VLregister((const H5VL_class_t *) &H5VL_rest_g)) < 0)
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
#ifdef RV_TRACK_MEM_USAGE
    /* Check for allocated memory */
    if (0 != rest_curr_alloc_bytes)
        FUNC_DONE_ERROR(H5E_VOL, H5E_CLOSEERROR, FAIL, "%zu bytes were still left allocated", rest_curr_alloc_bytes)

    rest_curr_alloc_bytes = 0;
#endif

    /* Unregister from the HDF5 error API */
    if (rv_err_class_g >= 0) {
        if (H5Eunregister_class(rv_err_class_g) < 0)
            FUNC_DONE_ERROR(H5E_VOL, H5E_CLOSEERROR, FAIL, "can't unregister from HDF5 error API")

        /* Print the current error stack before destroying it */
        PRINT_ERROR_STACK

        /* Destroy the error stack */
        if (H5Eclose_stack(rv_err_stack_g) < 0) {
            FUNC_DONE_ERROR(H5E_VOL, H5E_CLOSEERROR, FAIL, "can't close error stack")
            PRINT_ERROR_STACK
        } /* end if */

        rv_err_stack_g = -1;
        rv_err_class_g = -1;
        obj_err_maj_g = -1;
        parse_err_min_g = -1;
        link_table_err_min_g = -1;
        link_table_iter_err_min_g = -1;
    } /* end if */

    /* Unregister the VOL */
    if (REST_g >= 0) {
        if (H5VLunregister(REST_g) < 0) {
            H5Epush2(H5E_DEFAULT, __FILE__, FUNC, __LINE__, H5E_ERR_CLS, H5E_VOL, H5E_CLOSEERROR, "can't unregister REST VOL plugin");
            H5Eprint2(H5E_DEFAULT, NULL);
            H5Eclear2(H5E_DEFAULT);
        } /* end if */

        /* Reset ID */
        REST_g = -1;
    } /* end if */

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
RV_term(hid_t vtpl_id)
{
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
        curl_global_cleanup();
    } /* end if */

    return SUCCEED;
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
H5Pset_fapl_rest_vol(hid_t fapl_id)
{
    herr_t ret_value;

    if (REST_g < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_UNINITIALIZED, FAIL, "REST VOL plugin not initialized")

    if (H5P_DEFAULT == fapl_id)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_BADVALUE, FAIL, "can't set REST VOL plugin for default property list")

    if ((ret_value = H5Pset_vol(fapl_id, REST_g, NULL)) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't set REST VOL plugin in FAPL")

    if (RV_set_connection_information() < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't set REST VOL plugin connection information")

done:
    PRINT_ERROR_STACK

    return ret_value;
} /* end H5Pset_fapl_rest_vol() */


/*-------------------------------------------------------------------------
 * Function:    RV_set_connection_information
 *
 * Purpose:     Set the connection information for the REST VOL by first
 *              attempting to get the information from the environment,
 *              then, failing that, attempting to pull the information from
 *              a config file in the user's home directory.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              June, 2018
 */
static herr_t
RV_set_connection_information(void)
{
    const char *URL;
    size_t      URL_len = 0;
    FILE       *config_file = NULL;
    herr_t      ret_value = SUCCEED;

    /*
     * Attempt to pull in configuration/authentication information from
     * the environment.
     */
    if ((URL = getenv("HSDS_ENDPOINT"))) {
        const char *username = getenv("HSDS_USERNAME");
        const char *password = getenv("HSDS_PASSWORD");

        /*
         * Save a copy of the base URL being worked on so that operations like
         * creating a Group can be redirected to "base URL"/groups by building
         * off of the base URL supplied.
         */
        URL_len = strlen(URL);
        if (NULL == (base_URL = (char *) RV_malloc(URL_len + 1)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTALLOC, FAIL, "can't allocate space necessary for base URL")

        strncpy(base_URL, URL, URL_len);
        base_URL[URL_len] = '\0';

        if (username && strlen(username)) {
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_USERNAME, username))
                FUNC_GOTO_ERROR(H5E_ARGS, H5E_CANTSET, FAIL, "can't set username: %s", curl_err_buf)
        } /* end if */

        if (password && strlen(password)) {
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_PASSWORD, password))
                FUNC_GOTO_ERROR(H5E_ARGS, H5E_CANTSET, FAIL, "can't set password: %s", curl_err_buf)
        } /* end if */
    } /* end if */
    else {
        const char *cfg_file_name = ".hscfg";
        size_t      pathname_len = 0;
        char       *home_dir = NULL;
        char       *pathname = NULL;
        char        file_line[1024];
        int         file_path_len = 0;

        /*
         * If a valid endpoint was not obtained from the environment,
         * try to get the connection information from a config file
         * in the user's home directory.
         */
#ifdef WIN32
        char *home_drive = NULL;

        if (NULL == (home_drive = getenv("HOMEDRIVE")))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "reading config file - unable to retrieve location of home directory")

        if (NULL == (home_dir = getenv("HOMEPATH")))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "reading config file - unable to retrieve location of home directory")

        pathname_len = strlen(home_drive) + strlen(home_dir) + strlen(cfg_file_name) + 3;
        if (NULL == (pathname = RV_malloc(pathname_len)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTALLOC, FAIL, "unable to allocate space for config file pathname")

        if ((file_path_len = snprintf(pathname, pathname_len, "%s\%s\%s", home_drive, home_dir, cfg_file_name)) < 0)
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "snprintf error")

        if (file_path_len >= pathname_len)
            FUNC_GOTO_ERROR(H5E_VOL, H5E_SYSERRSTR, FAIL, "config file path length exceeded maximum path length")
#else
        if (NULL == (home_dir = getenv("HOME")))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "reading config file - unable to retrieve location of home directory")

        pathname_len = strlen(home_dir) + strlen(cfg_file_name) + 2;
        if (NULL == (pathname = RV_malloc(pathname_len)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTALLOC, FAIL, "unable to allocate space for config file pathname")

        if ((file_path_len = snprintf(pathname, pathname_len, "%s/%s", home_dir, cfg_file_name)) < 0)
            FUNC_GOTO_ERROR(H5E_VOL, H5E_SYSERRSTR, FAIL, "snprintf error")

        if (file_path_len >= pathname_len)
            FUNC_GOTO_ERROR(H5E_VOL, H5E_SYSERRSTR, FAIL, "config file path length exceeded maximum path length")
#endif

        if (NULL == (config_file = fopen(pathname, "r")))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTOPENFILE, FAIL, "unable to open config file")

        /*
         * Parse the connection information by searching for
         * URL, username and password key-value pairs.
         */
        while (fgets(file_line, sizeof(file_line), config_file) != NULL) {
            const char *key = strtok(file_line, " =\n");
            const char *val = strtok(NULL, " =\n");

            if (!strcmp(key, "hs_endpoint")) {
                if (val) {
                    /*
                     * Save a copy of the base URL being worked on so that operations like
                     * creating a Group can be redirected to "base URL"/groups by building
                     * off of the base URL supplied.
                     */
                    URL_len = strlen(val);
                    if (NULL == (base_URL = (char *) RV_malloc(URL_len + 1)))
                        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTALLOC, FAIL, "can't allocate space necessary for base URL")

                    strncpy(base_URL, val, URL_len);
                    base_URL[URL_len] = '\0';
                } /* end if */
            } /* end if */
            else if (!strcmp(key, "hs_username")) {
                if (val && strlen(val)) {
                    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_USERNAME, val))
                        FUNC_GOTO_ERROR(H5E_ARGS, H5E_CANTSET, FAIL, "can't set username: %s", curl_err_buf)
                } /* end if */
            } /* end else if */
            else if (!strcmp(key, "hs_password")) {
                if (val && strlen(val)) {
                    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_PASSWORD, val))
                        FUNC_GOTO_ERROR(H5E_ARGS, H5E_CANTSET, FAIL, "can't set password: %s", curl_err_buf)
                } /* end if */
            } /* end else if */
        } /* end while */
    } /* end else */

    if (!base_URL)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "must specify a base URL - please set HSDS_ENDPOINT environment variable or create a config file")

done:
    if (config_file)
        fclose(config_file);

    PRINT_ERROR_STACK

    return ret_value;
} /* end RV_set_connection_information() */


const char *
RVget_uri(hid_t obj_id)
{
    RV_object_t *VOL_obj;
    char        *ret_value = NULL;

    if (NULL == (VOL_obj = (RV_object_t *) H5VLobject(obj_id)))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_BADVALUE, NULL, "invalid identifier")
    ret_value = VOL_obj->URI;

done:
    PRINT_ERROR_STACK

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
#ifdef RV_TRACK_MEM_USAGE
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
#ifdef RV_TRACK_MEM_USAGE
        if (NULL != (ret_value = RV_malloc(size)))
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
#ifdef RV_TRACK_MEM_USAGE
        if (size > 0) {
            if (mem) {
                size_t block_size;

                memcpy(&block_size, (char *) mem - sizeof(block_size), sizeof(block_size));

                ret_value = RV_malloc(size);
                memcpy(ret_value, mem, size < block_size ? size : block_size);
                RV_free(mem);
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
#ifdef RV_TRACK_MEM_USAGE
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
               hid_t aapl_id, hid_t dxpl_id, void **req)
{
    RV_object_t *parent = (RV_object_t *) obj;
    RV_object_t *new_attribute = NULL;
    upload_info  uinfo;
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
    int          create_request_body_len = 0;
    int          url_len = 0;
    void        *ret_value = NULL;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received attribute create call with following parameters:\n");

    if (H5VL_OBJECT_BY_NAME == loc_params.type) {
        printf("     - H5Acreate variant: H5Acreate_by_name\n");
        printf("     - loc_id object's URI: %s\n", parent->URI);
        printf("     - loc_id object's type: %s\n", object_type_to_string(parent->obj_type));
        printf("     - loc_id object's domain path: %s\n", parent->domain->u.file.filepath_name);
        printf("     - Path to object that attribute is to be attached to: %s\n", loc_params.loc_data.loc_by_name.name);
    } /* end if */
    else {
        printf("     - H5Acreate variant: H5Acreate2\n");
        printf("     - New attribute's parent object URI: %s\n", parent->URI);
        printf("     - New attribute's parent object type: %s\n", object_type_to_string(parent->obj_type));
        printf("     - New attribute's parent object domain path: %s\n", parent->domain->u.file.filepath_name);
    } /* end else */

    printf("     - New attribute's name: %s\n", attr_name);
    printf("     - Default ACPL? %s\n\n", (H5P_ATTRIBUTE_CREATE_DEFAULT == acpl_id) ? "yes" : "no");
#endif

    if (   H5I_FILE != parent->obj_type
        && H5I_GROUP != parent->obj_type
        && H5I_DATATYPE != parent->obj_type
        && H5I_DATASET != parent->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object not a group, datatype or dataset")

    /* Check for write access */
    if (!(parent->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, NULL, "no write intent on file")

    /* Allocate and setup internal Attribute struct */
    if (NULL == (new_attribute = (RV_object_t *) RV_malloc(sizeof(*new_attribute))))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL, "can't allocate space for attribute object")

    new_attribute->URI[0] = '\0';
    new_attribute->obj_type = H5I_ATTR;
    new_attribute->domain = parent->domain; /* Store pointer to file that the newly-created attribute is in */
    new_attribute->u.attribute.dtype_id = FAIL;
    new_attribute->u.attribute.space_id = FAIL;
    new_attribute->u.attribute.aapl_id = FAIL;
    new_attribute->u.attribute.acpl_id = FAIL;
    new_attribute->u.attribute.attr_name = NULL;

    /* If this is a call to H5Acreate_by_name, locate the real parent object */
    if (H5VL_OBJECT_BY_NAME == loc_params.type) {
        htri_t search_ret;

        new_attribute->u.attribute.parent_obj_type = H5I_UNINIT;

        search_ret = RV_find_object_by_path(parent, loc_params.loc_data.loc_by_name.name, &new_attribute->u.attribute.parent_obj_type,
                RV_copy_object_URI_callback, NULL, new_attribute->u.attribute.parent_obj_URI);
        if (!search_ret || search_ret < 0)
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_PATH, NULL, "can't locate object that attribute is to be attached to")

#ifdef RV_PLUGIN_DEBUG
        printf("-> H5Acreate_by_name(): found attribute's parent object by given path\n");
        printf("-> H5Acreate_by_name(): new attribute's parent object URI: %s\n", new_attribute->u.attribute.parent_obj_URI);
        printf("-> H5Acreate_by_name(): new attribute's parent object type: %s\n\n", object_type_to_string(new_attribute->u.attribute.parent_obj_type));
#endif
    } /* end if */
    else {
        new_attribute->u.attribute.parent_obj_type = parent->obj_type;
        strncpy(new_attribute->u.attribute.parent_obj_URI, parent->URI, URI_MAX_LENGTH);
    } /* end else */

    /* Copy the AAPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * attribute access property lists functions will function correctly
     */
    if (H5P_ATTRIBUTE_ACCESS_DEFAULT != aapl_id) {
        if ((new_attribute->u.attribute.aapl_id = H5Pcopy(aapl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy AAPL")
    } /* end if */
    else
        new_attribute->u.attribute.aapl_id = H5P_ATTRIBUTE_ACCESS_DEFAULT;

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
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCOPY, NULL, "failed to copy attribute's datatype")
    if ((new_attribute->u.attribute.space_id = H5Scopy(space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCOPY, NULL, "failed to copy attribute's dataspace")

    /* Copy the attribute's name */
    attr_name_len = strlen(attr_name);
    if (NULL == (new_attribute->u.attribute.attr_name = (char *) RV_malloc(attr_name_len + 1)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL, "can't allocate space for copy of attribute's name")
    memcpy(new_attribute->u.attribute.attr_name, attr_name, attr_name_len);
    new_attribute->u.attribute.attr_name[attr_name_len] = '\0';

    /* Form the request body to give the new Attribute its properties */

    /* Form the Datatype portion of the Attribute create request */
    if (RV_convert_datatype_to_JSON(type_id, &datatype_body, &datatype_body_len, FALSE) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, NULL, "can't convert attribute's datatype to JSON representation")

    /* If the Dataspace of the Attribute was specified, convert it to JSON. Otherwise, use defaults */
    if (H5P_DEFAULT != space_id)
        if (RV_convert_dataspace_shape_to_JSON(space_id, &shape_body, NULL) < 0)
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCONVERT, NULL, "can't convert attribute's dataspace to JSON representation")

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

    if ((size_t) create_request_body_len >= create_request_nalloc)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL, "attribute create request body size exceeded allocated buffer size")

#ifdef RV_PLUGIN_DEBUG
    printf("-> Attribute create request JSON:\n%s\n\n", create_request_body);
#endif

    /* Setup the host header */
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
    switch (new_attribute->u.attribute.parent_obj_type) {
        case H5I_FILE:
        case H5I_GROUP:
            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/attributes/%s",
                     base_URL, new_attribute->u.attribute.parent_obj_URI, url_encoded_attr_name)
                ) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL, "snprintf error")

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL, "attribute create URL exceeded maximum URL size")

            break;

        case H5I_DATATYPE:
            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s/attributes/%s",
                     base_URL, new_attribute->u.attribute.parent_obj_URI, url_encoded_attr_name)
                ) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL, "snprintf error")

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL, "attribute create URL exceeded maximum URL size")

            break;

        case H5I_DATASET:
            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s/attributes/%s",
                     base_URL, new_attribute->u.attribute.parent_obj_URI, url_encoded_attr_name)
                ) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL, "snprintf error")

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL, "attribute create URL exceeded maximum URL size")

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

#ifdef RV_PLUGIN_DEBUG
    printf("-> URL for attribute creation request: %s\n\n", request_url);
#endif

    uinfo.buffer = create_request_body;
    uinfo.buffer_size = (size_t) create_request_body_len;

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, NULL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_UPLOAD, 1))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, NULL, "can't set up cURL to make HTTP PUT request: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_READDATA, &uinfo))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, NULL, "can't set cURL PUT data: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t) create_request_body_len))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, NULL, "can't set cURL PUT data size: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, NULL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef RV_PLUGIN_DEBUG
    printf("-> Creating attribute\n\n");

    printf("   /**********************************\\\n");
    printf("-> | Making PUT request to the server |\n");
    printf("   \\**********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_ATTR, H5E_CANTCREATE, NULL);

#ifdef RV_PLUGIN_DEBUG
    printf("-> Created attribute\n\n");
#endif

    ret_value = (void *) new_attribute;

done:
#ifdef RV_PLUGIN_DEBUG
    printf("-> Attribute create response buffer:\n%s\n\n", response_buffer.buffer);

    if (new_attribute && ret_value) {
        printf("-> New attribute's info:\n");
        printf("     - New attribute's object type: %s\n", object_type_to_string(new_attribute->obj_type));
        printf("     - New attribute's domain path: %s\n", new_attribute->domain->u.file.filepath_name);
        printf("     - New attribute's name: %s\n", new_attribute->u.attribute.attr_name);
        printf("     - New attribute's datatype class: %s\n\n", datatype_class_to_string(new_attribute->u.attribute.dtype_id));
    } /* end if */
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

    /* Unset cURL UPLOAD option to ensure that future requests don't try to use PUT calls */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_UPLOAD, 0))
        FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTSET, NULL, "can't unset cURL PUT option: %s", curl_err_buf)

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    PRINT_ERROR_STACK

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
             hid_t aapl_id, hid_t dxpl_id, void **req)
{
    RV_object_t *parent = (RV_object_t *) obj;
    RV_object_t *attribute = NULL;
    size_t       attr_name_len = 0;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    char         request_url[URL_MAX_LENGTH];
    char        *url_encoded_attr_name = NULL;
    int          url_len = 0;
    void        *ret_value = NULL;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received attribute open call with following parameters:\n");

    if (H5VL_OBJECT_BY_NAME == loc_params.type) {
        printf("     - H5Aopen variant: H5Aopen_by_name\n");
        printf("     - loc_id object's URI: %s\n", parent->URI);
        printf("     - loc_id object's type: %s\n", object_type_to_string(parent->obj_type));
        printf("     - loc_id object's domain path: %s\n", parent->domain->u.file.filepath_name);
        printf("     - Path to object that attribute is attached to: %s\n", loc_params.loc_data.loc_by_name.name);
    } /* end if */
    else if (H5VL_OBJECT_BY_IDX == loc_params.type) {
        printf("     - H5Aopen variant: H5Aopen_by_idx\n");
    } /* end else if */
    else {
        printf("     - H5Aopen variant: H5Aopen\n");
        printf("     - Attribute's parent object URI: %s\n", parent->URI);
        printf("     - Attribute's parent object type: %s\n", object_type_to_string(parent->obj_type));
        printf("     - Attribute's parent object domain path: %s\n", parent->domain->u.file.filepath_name);
    } /* end else */

    if (attr_name) printf("     - Attribute's name: %s\n", attr_name);
    printf("\n");
#endif

    if (   H5I_FILE != parent->obj_type
        && H5I_GROUP != parent->obj_type
        && H5I_DATATYPE != parent->obj_type
        && H5I_DATASET != parent->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object not a group, datatype or dataset")

    /* Allocate and setup internal Attribute struct */
    if (NULL == (attribute = (RV_object_t *) RV_malloc(sizeof(*attribute))))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL, "can't allocate space for attribute object")

    attribute->URI[0] = '\0';
    attribute->obj_type = H5I_ATTR;
    attribute->domain = parent->domain; /* Store pointer to file that the opened Dataset is within */
    attribute->u.attribute.dtype_id = FAIL;
    attribute->u.attribute.space_id = FAIL;
    attribute->u.attribute.aapl_id = FAIL;
    attribute->u.attribute.acpl_id = FAIL;
    attribute->u.attribute.attr_name = NULL;

    /* Set the parent object's type and URI in the attribute's appropriate fields */
    switch (loc_params.type) {
        /* H5Aopen */
        case H5VL_OBJECT_BY_SELF:
        {
            attribute->u.attribute.parent_obj_type = parent->obj_type;
            strncpy(attribute->u.attribute.parent_obj_URI, parent->URI, URI_MAX_LENGTH);
            break;
        } /* H5VL_OBJECT_BY_SELF */

        /* H5Aopen_by_name */
        case H5VL_OBJECT_BY_NAME:
        {
            htri_t search_ret;

            /* If this is a call to H5Aopen_by_name, locate the real object that the attribute
             * is attached to by searching the given path
             */

            attribute->u.attribute.parent_obj_type = H5I_UNINIT;

            search_ret = RV_find_object_by_path(parent, loc_params.loc_data.loc_by_name.name, &attribute->u.attribute.parent_obj_type,
                    RV_copy_object_URI_callback, NULL, attribute->u.attribute.parent_obj_URI);
            if (!search_ret || search_ret < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_PATH, NULL, "can't locate object that attribute is attached to")

#ifdef RV_PLUGIN_DEBUG
            printf("-> H5Aopen_by_name(): found attribute's parent object by given path\n");
            printf("-> H5Aopen_by_name(): attribute's parent object URI: %s\n", attribute->u.attribute.parent_obj_URI);
            printf("-> H5Aopen_by_name(): attribute's parent object type: %s\n\n", object_type_to_string(attribute->u.attribute.parent_obj_type));
#endif

            break;
        } /* H5VL_OBJECT_BY_NAME */

        /* H5Aopen_by_idx */
        case H5VL_OBJECT_BY_IDX:
        {
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_UNSUPPORTED, NULL, "H5Aopen_by_idx is unsupported")
            break;
        } /* H5VL_OBJECT_BY_IDX */

        case H5VL_OBJECT_BY_REF:
        case H5VL_OBJECT_BY_ADDR:
        default:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, NULL, "invalid loc_params type")
    } /* end switch */

    /* Make a GET request to the server to retrieve information about the attribute */

    /* Setup the host header */
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
    switch (attribute->u.attribute.parent_obj_type) {
        case H5I_FILE:
        case H5I_GROUP:
            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/attributes/%s",
                     base_URL, attribute->u.attribute.parent_obj_URI, url_encoded_attr_name)
                ) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL, "snprintf error")

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL, "attribute open URL exceeded maximum URL size")

            break;

        case H5I_DATATYPE:
            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s/attributes/%s",
                     base_URL, attribute->u.attribute.parent_obj_URI, url_encoded_attr_name)
                ) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL, "snprintf error")

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL, "attribute open URL exceeded maximum URL size")

            break;

        case H5I_DATASET:
            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s/attributes/%s",
                     base_URL, attribute->u.attribute.parent_obj_URI, url_encoded_attr_name)
                ) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL, "snprintf error")

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL, "attribute open URL exceeded maximum URL size")

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

#ifdef RV_PLUGIN_DEBUG
    printf("-> URL for attribute open request: %s\n\n", request_url);
#endif

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, NULL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, NULL, "can't set up cURL to make HTTP GET request: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, NULL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef RV_PLUGIN_DEBUG
    printf("-> Retrieving attribute's info\n\n");

    printf("   /**********************************\\\n");
    printf("-> | Making GET request to the server |\n");
    printf("   \\**********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_ATTR, H5E_CANTGET, NULL);

    /* Set up a Dataspace for the opened Attribute */
    if ((attribute->u.attribute.space_id = RV_parse_dataspace(response_buffer.buffer)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCONVERT, NULL, "can't convert JSON into usable dataspace for attribute")

    /* Set up a Datatype for the opened Attribute */
    if ((attribute->u.attribute.dtype_id = RV_parse_datatype(response_buffer.buffer, TRUE)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, NULL, "can't convert JSON into usable datatype for attribute")

    /* Copy the attribute's name */
    if (NULL == (attribute->u.attribute.attr_name = (char *) RV_malloc(attr_name_len + 1)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL, "can't allocate space for copy of attribute's name")
    memcpy(attribute->u.attribute.attr_name, attr_name, attr_name_len);
    attribute->u.attribute.attr_name[attr_name_len] = '\0';

    /* Copy the AAPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * attribute access property list functions will function correctly
     */
    if (H5P_ATTRIBUTE_ACCESS_DEFAULT != aapl_id) {
        if ((attribute->u.attribute.aapl_id = H5Pcopy(aapl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy AAPL")
    } /* end if */
    else
        attribute->u.attribute.aapl_id = H5P_ATTRIBUTE_ACCESS_DEFAULT;

    /* Set up an ACPL for the attribute so that H5Aget_create_plist() will function correctly */
    /* XXX: Set any properties necessary */
    if ((attribute->u.attribute.acpl_id = H5Pcreate(H5P_ATTRIBUTE_CREATE)) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "can't create ACPL for attribute")

    ret_value = (void *) attribute;

done:
#ifdef RV_PLUGIN_DEBUG
    printf("-> Attribute open response buffer:\n%s\n\n", response_buffer.buffer);

    if (attribute && ret_value) {
        printf("-> Attribute's info:\n");
        printf("     - Attribute's object type: %s\n", object_type_to_string(attribute->obj_type));
        printf("     - Attribute's domain path: %s\n", attribute->domain->u.file.filepath_name);
        printf("     - Attribute's name: %s\n", attribute->u.attribute.attr_name);
        printf("     - Attribute's datatype class: %s\n\n", datatype_class_to_string(attribute->u.attribute.dtype_id));
    } /* end if */
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

    PRINT_ERROR_STACK

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
RV_attr_read(void *attr, hid_t dtype_id, void *buf, hid_t dxpl_id, void **req)
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
    int          url_len = 0;
    herr_t       ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received attribute read call with following parameters:\n");
    printf("     - Attribute's object type: %s\n", object_type_to_string(attribute->obj_type));
    if (H5I_ATTR == attribute->obj_type && attribute->u.attribute.attr_name)
        printf("     - Attribute's name: %s\n", attribute->u.attribute.attr_name);
    printf("     - Attribute's domain path: %s\n\n", attribute->domain->u.file.filepath_name);
#endif

    if (H5I_ATTR != attribute->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not an attribute")
    if (!buf)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "read buffer was NULL")

    /* Determine whether it's possible to receive the data as a binary blob instead of as JSON */
    if (H5T_NO_CLASS == (dtype_class = H5Tget_class(dtype_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid")

    if ((is_variable_str = H5Tis_variable_str(dtype_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid")

    is_transfer_binary = (H5T_VLEN != dtype_class) && !is_variable_str;

    if ((file_select_npoints = H5Sget_select_npoints(attribute->u.attribute.space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "attribute's dataspace is invalid")

    if (0 == (dtype_size = H5Tget_size(dtype_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid")

#ifdef RV_PLUGIN_DEBUG
    printf("-> %lld points selected for attribute read\n", file_select_npoints);
    printf("-> Attribute's datatype size: %zu\n\n", dtype_size);
#endif

    /* Setup the host header */
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
    switch (attribute->u.attribute.parent_obj_type) {
        case H5I_FILE:
        case H5I_GROUP:
            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/attributes/%s/value",
                     base_URL, attribute->u.attribute.parent_obj_URI, url_encoded_attr_name)
                ) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "attribute read URL exceeded maximum URL size")

            break;

        case H5I_DATATYPE:
            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s/attributes/%s/value",
                     base_URL, attribute->u.attribute.parent_obj_URI, url_encoded_attr_name)
                ) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "attribute read URL exceeded maximum URL size")

            break;

        case H5I_DATASET:
            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s/attributes/%s/value",
                     base_URL, attribute->u.attribute.parent_obj_URI, url_encoded_attr_name)
                ) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "attribute read URL exceeded maximum URL size")

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

#ifdef RV_PLUGIN_DEBUG
    printf("-> URL for attribute read request: %s\n\n", request_url);
#endif

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP GET request: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef RV_PLUGIN_DEBUG
    printf("-> Reading attribute\n\n");

    printf("   /**********************************\\\n");
    printf("-> | Making GET request to the server |\n");
    printf("   \\**********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_ATTR, H5E_READERROR, FAIL);

    memcpy(buf, response_buffer.buffer, (size_t) file_select_npoints * dtype_size);

done:
#ifdef RV_PLUGIN_DEBUG
    printf("-> Attribute read response buffer:\n%s\n\n", response_buffer.buffer);
#endif

    if (host_header)
        RV_free(host_header);
    if (url_encoded_attr_name)
        curl_free(url_encoded_attr_name);

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    PRINT_ERROR_STACK

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
RV_attr_write(void *attr, hid_t dtype_id, const void *buf, hid_t dxpl_id, void **req)
{
    RV_object_t *attribute = (RV_object_t *) attr;
    H5T_class_t  dtype_class;
    upload_info  uinfo;
    curl_off_t   write_len;
    hssize_t     file_select_npoints;
    htri_t       is_variable_str;
    size_t       dtype_size;
    size_t       write_body_len = 0;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    char        *url_encoded_attr_name = NULL;
    char         request_url[URL_MAX_LENGTH];
    int          url_len = 0;
    herr_t       ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received attribute write call with following parameters:\n");
    printf("     - Attribute's object type: %s\n", object_type_to_string(attribute->obj_type));
    if (H5I_ATTR == attribute->obj_type && attribute->u.attribute.attr_name)
        printf("     - Attribute's name: %s\n", attribute->u.attribute.attr_name);
    printf("     - Attribute's domain path: %s\n\n", attribute->domain->u.file.filepath_name);
#endif

    if (H5I_ATTR != attribute->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not an attribute")
    if (!buf)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "write buffer was NULL")

    /* Check for write access */
    if (!(attribute->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "no write intent on file")

    /* Determine whether it's possible to send the data as a binary blob instead of as JSON */
    if (H5T_NO_CLASS == (dtype_class = H5Tget_class(dtype_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid")

    if ((is_variable_str = H5Tis_variable_str(dtype_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid")

    if ((file_select_npoints = H5Sget_select_npoints(attribute->u.attribute.space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "attribute's dataspace is invalid")

    if (0 == (dtype_size = H5Tget_size(dtype_id)))
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "memory datatype is invalid")

#ifdef RV_PLUGIN_DEBUG
    printf("-> %lld points selected for attribute write\n", file_select_npoints);
    printf("-> Attribute's datatype size: %zu\n\n", dtype_size);
#endif

    write_body_len = (size_t) file_select_npoints * dtype_size;

    /* Setup the host header */
    host_header_len = strlen(attribute->domain->u.file.filepath_name) + strlen(host_string) + 1;
    if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, attribute->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    /* Instruct cURL on which type of transfer to perform, binary or JSON */
    curl_headers = curl_slist_append(curl_headers, "Content-Type: application/octet-stream");

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
    switch (attribute->u.attribute.parent_obj_type) {
        case H5I_FILE:
        case H5I_GROUP:
            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/attributes/%s/value",
                     base_URL, attribute->u.attribute.parent_obj_URI, url_encoded_attr_name)
                ) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "attribute write URL exceeded maximum URL size")

            break;

        case H5I_DATATYPE:
            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s/attributes/%s/value",
                     base_URL, attribute->u.attribute.parent_obj_URI, url_encoded_attr_name)
                ) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "attribute write URL exceeded maximum URL size")

            break;

        case H5I_DATASET:
            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s/attributes/%s/value",
                     base_URL, attribute->u.attribute.parent_obj_URI, url_encoded_attr_name)
                ) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "attribute write URL exceeded maximum URL size")

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

#ifdef RV_PLUGIN_DEBUG
    printf("-> URL for attribute write request: %s\n\n", request_url);
#endif

    /* Check to make sure that the size of the write body can safely be cast to a curl_off_t */
    if (sizeof(curl_off_t) < sizeof(size_t))
        ASSIGN_TO_SMALLER_SIZE(write_len, curl_off_t, write_body_len, size_t)
    else if (sizeof(curl_off_t) > sizeof(size_t))
        write_len = (curl_off_t) write_body_len;
    else
        ASSIGN_TO_SAME_SIZE_UNSIGNED_TO_SIGNED(write_len, curl_off_t, write_body_len, size_t)

    uinfo.buffer = buf;
    uinfo.buffer_size = write_body_len;

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_UPLOAD, 1))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP PUT request: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_READDATA, &uinfo))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set cURL PUT data: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, write_len))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set cURL PUT data size: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef RV_PLUGIN_DEBUG
    printf("-> Writing attribute\n\n");

    printf("   /**********************************\\\n");
    printf("-> | Making PUT request to the server |\n");
    printf("   \\**********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_ATTR, H5E_WRITEERROR, FAIL);

done:
#ifdef RV_PLUGIN_DEBUG
    printf("-> Attribute write response buffer:\n%s\n\n", response_buffer.buffer);
#endif

    if (host_header)
        RV_free(host_header);
    if (url_encoded_attr_name)
        curl_free(url_encoded_attr_name);

    /* Unset cURL UPLOAD option to ensure that future requests don't try to use PUT calls */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_UPLOAD, 0))
        FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't unset cURL PUT option: %s", curl_err_buf)

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    PRINT_ERROR_STACK

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
RV_attr_get(void *obj, H5VL_attr_get_t get_type, hid_t dxpl_id, void **req, va_list arguments)
{
    RV_object_t *loc_obj = (RV_object_t *) obj;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    char         request_url[URL_MAX_LENGTH];
    char        *url_encoded_attr_name = NULL;
    int          url_len = 0;
    herr_t       ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received attribute get call with following parameters:\n");
    printf("     - Attribute get call type: %s\n\n", attr_get_type_to_string(get_type));
#endif

    if (   H5I_ATTR != loc_obj->obj_type
        && H5I_FILE != loc_obj->obj_type
        && H5I_GROUP != loc_obj->obj_type
        && H5I_DATATYPE != loc_obj->obj_type
        && H5I_DATASET != loc_obj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "parent object not an attribute, group, datatype or dataset")

    switch (get_type) {
        /* H5Aget_create_plist */
        case H5VL_ATTR_GET_ACPL:
        {
            hid_t *ret_id = va_arg(arguments, hid_t *);

            if ((*ret_id = H5Pcopy(loc_obj->u.attribute.acpl_id)) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, FAIL, "can't copy attribute ACPL")

            break;
        } /* H5VL_ATTR_GET_ACPL */

        /* H5Aget_info (_by_name/_by_idx) */
        case H5VL_ATTR_GET_INFO:
        {
            H5VL_loc_params_t  loc_params = va_arg(arguments, H5VL_loc_params_t);
            H5A_info_t        *attr_info = va_arg(arguments, H5A_info_t *);

            switch (loc_params.type) {
                /* H5Aget_info */
                case H5VL_OBJECT_BY_SELF:
                {
#ifdef RV_PLUGIN_DEBUG
                    printf("-> H5Aget_info(): Attribute's parent object URI: %s\n", loc_obj->u.attribute.parent_obj_URI);
                    printf("-> H5Aget_info(): Attribute's parent object type: %s\n\n", object_type_to_string(loc_obj->u.attribute.parent_obj_type));
#endif

                    /* URL-encode the attribute name to ensure that the resulting URL for the creation
                     * operation contains no illegal characters
                     */
                    if (NULL == (url_encoded_attr_name = curl_easy_escape(curl, loc_obj->u.attribute.attr_name, 0)))
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTENCODE, FAIL, "can't URL-encode attribute name")

                    switch (loc_obj->u.attribute.parent_obj_type) {
                        case H5I_FILE:
                        case H5I_GROUP:
                            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/attributes/%s",
                                     base_URL, loc_obj->u.attribute.parent_obj_URI, url_encoded_attr_name)
                                ) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

                            if (url_len >= URL_MAX_LENGTH)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "H5Aget_info request URL exceeded maximum URL size")

                            break;

                        case H5I_DATATYPE:
                            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s/attributes/%s",
                                     base_URL, loc_obj->u.attribute.parent_obj_URI, url_encoded_attr_name)
                                ) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

                            if (url_len >= URL_MAX_LENGTH)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "H5Aget_info request URL exceeded maximum URL size")

                            break;

                        case H5I_DATASET:
                            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s/attributes/%s",
                                     base_URL, loc_obj->u.attribute.parent_obj_URI, url_encoded_attr_name)
                                ) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

                            if (url_len >= URL_MAX_LENGTH)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "H5Aget_info request URL exceeded maximum URL size")

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

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Aget_info_by_name */
                case H5VL_OBJECT_BY_NAME:
                {
                    const char *attr_name = va_arg(arguments, const char *);
                    H5I_type_t  parent_obj_type = H5I_UNINIT;
                    htri_t      search_ret;
                    char        parent_obj_URI[URI_MAX_LENGTH];

#ifdef RV_PLUGIN_DEBUG
                    printf("-> H5Aget_info_by_name(): loc_id object's URI: %s\n", loc_obj->URI);
                    printf("-> H5Aget_info_by_name(): loc_id object type: %s\n", object_type_to_string(loc_obj->obj_type));
                    printf("-> H5Aget_info_by_name(): Path to object that attribute is attached to: %s\n\n", loc_params.loc_data.loc_by_name.name);
#endif

                    /* Retrieve the type and URI of the object that the attribute is attached to */
                    search_ret = RV_find_object_by_path(loc_obj, loc_params.loc_data.loc_by_name.name, &parent_obj_type,
                            RV_copy_object_URI_callback, NULL, parent_obj_URI);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PATH, FAIL, "can't find parent object by name")

#ifdef RV_PLUGIN_DEBUG
                    printf("-> H5Aget_info_by_name(): found attribute's parent object by given path\n");
                    printf("-> H5Aget_info_by_name(): attribute's parent object URI: %s\n", parent_obj_URI);
                    printf("-> H5Aget_info_by_name(): attribute's parent object type: %s\n\n", object_type_to_string(parent_obj_type));
#endif

                    /* URL-encode the attribute name to ensure that the resulting URL for the creation
                     * operation contains no illegal characters
                     */
                    if (NULL == (url_encoded_attr_name = curl_easy_escape(curl, attr_name, 0)))
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTENCODE, FAIL, "can't URL-encode attribute name")

                    switch (parent_obj_type) {
                        case H5I_FILE:
                        case H5I_GROUP:
                            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/attributes/%s",
                                     base_URL, parent_obj_URI, url_encoded_attr_name)
                                ) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

                            if (url_len >= URL_MAX_LENGTH)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "H5Aget_info_by_name request URL exceeded maximum URL size")

                            break;

                        case H5I_DATATYPE:
                            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s/attributes/%s",
                                     base_URL, parent_obj_URI, url_encoded_attr_name)
                                ) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

                            if (url_len >= URL_MAX_LENGTH)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "H5Aget_info_by_name request URL exceeded maximum URL size")

                            break;

                        case H5I_DATASET:
                            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s/attributes/%s",
                                     base_URL, parent_obj_URI, url_encoded_attr_name)
                                ) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

                            if (url_len >= URL_MAX_LENGTH)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "H5Aget_info_by_name request URL exceeded maximum URL size")

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

                    break;
                } /* H5VL_OBJECT_BY_NAME */

                /* H5Aget_info_by_idx */
                case H5VL_OBJECT_BY_IDX:
                {
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_UNSUPPORTED, FAIL, "H5Aget_info_by_idx is unsupported")
                    break;
                } /* H5VL_OBJECT_BY_IDX */

                case H5VL_OBJECT_BY_ADDR:
                case H5VL_OBJECT_BY_REF:
                default:
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid loc_params type")
            } /* end switch */

            /* Make a GET request to the server to retrieve the attribute's info */

            /* Setup the host header */
            host_header_len = strlen(loc_obj->domain->u.file.filepath_name) + strlen(host_string) + 1;
            if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

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

#ifdef RV_PLUGIN_DEBUG
            printf("-> Retrieving attribute info at URL: %s\n\n", request_url);

            printf("   /**********************************\\\n");
            printf("-> | Making GET request to the server |\n");
            printf("   \\**********************************/\n\n");
#endif

            CURL_PERFORM(curl, H5E_ATTR, H5E_CANTGET, FAIL);

            /* Retrieve the attribute's info */
            if (RV_parse_response(response_buffer.buffer, NULL, attr_info, RV_get_attr_info_callback) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "can't get attribute info")

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
#ifdef RV_PLUGIN_DEBUG
                    printf("-> H5Aget_name(): Attribute's parent object URI: %s\n", loc_obj->u.attribute.parent_obj_URI);
                    printf("-> H5Aget_name(): Attribute's parent object type: %s\n\n", object_type_to_string(loc_obj->u.attribute.parent_obj_type));
#endif

                    *ret_size = (ssize_t) strlen(loc_obj->u.attribute.attr_name);

                    if (name_buf) {
                        strncpy(name_buf, loc_obj->u.attribute.attr_name, name_buf_size - 1);
                        name_buf[name_buf_size - 1] = '\0';
                    } /* end if */

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Aget_name_by_idx */
                case H5VL_OBJECT_BY_IDX:
                {
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

            if ((*ret_id = H5Scopy(loc_obj->u.attribute.space_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCOPY, FAIL, "can't copy attribute's dataspace")

            break;
        } /* H5VL_ATTR_GET_SPACE */

        /* H5Aget_storage_size */
        case H5VL_ATTR_GET_STORAGE_SIZE:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_UNSUPPORTED, FAIL, "H5Aget_storage_size is unsupported")

        /* H5Aget_type */
        case H5VL_ATTR_GET_TYPE:
        {
            hid_t *ret_id = va_arg(arguments, hid_t *);

            if ((*ret_id = H5Tcopy(loc_obj->u.attribute.dtype_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCOPY, FAIL, "can't copy attribute's datatype")

            break;
        } /* H5VL_ATTR_GET_TYPE */

        default:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "can't get this type of information from attribute")
    } /* end switch */

done:
    if (host_header)
        RV_free(host_header);
    if (url_encoded_attr_name)
        curl_free(url_encoded_attr_name);

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    }

    PRINT_ERROR_STACK

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
                 hid_t dxpl_id, void **req, va_list arguments)
{
    RV_object_t *loc_obj = (RV_object_t *) obj;
    H5I_type_t   parent_obj_type = H5I_UNINIT;
    size_t       host_header_len = 0;
    hid_t        attr_iter_object_id = -1;
    void        *attr_iter_object = NULL;
    char        *host_header = NULL;
    char        *obj_URI;
    char         temp_URI[URI_MAX_LENGTH];
    char         request_url[URL_MAX_LENGTH];
    char        *url_encoded_attr_name = NULL;
    int          url_len = 0;
    herr_t       ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received attribute-specific call with following parameters:\n");
    printf("     - Attribute-specific call type: %s\n\n", attr_specific_type_to_string(specific_type));
#endif

    if (   H5I_FILE != loc_obj->obj_type
        && H5I_GROUP != loc_obj->obj_type
        && H5I_DATATYPE != loc_obj->obj_type
        && H5I_DATASET != loc_obj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "parent object not a group, datatype or dataset")

    switch (specific_type) {
        /* H5Adelete (_by_name/_by_idx) */
        case H5VL_ATTR_DELETE:
        {
            char *attr_name = NULL;

            /* Check for write access */
            if (!(loc_obj->domain->u.file.intent & H5F_ACC_RDWR))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "no write intent on file")

            switch (loc_params.type) {
                /* H5Adelete */
                case H5VL_OBJECT_BY_SELF:
                {
                    attr_name = va_arg(arguments, char *);
                    obj_URI = loc_obj->URI;
                    parent_obj_type = loc_obj->obj_type;

#ifdef RV_PLUGIN_DEBUG
                    printf("-> H5Adelete(): Attribute's name: %s\n", attr_name);
                    printf("-> H5Adelete(): Attribute's parent object URI: %s\n", loc_obj->URI);
                    printf("-> H5Adelete(): Attribute's parent object type: %s\n\n", object_type_to_string(parent_obj_type));
#endif

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Adelete_by_name */
                case H5VL_OBJECT_BY_NAME:
                {
                    htri_t search_ret;

                    attr_name = va_arg(arguments, char *);

#ifdef RV_PLUGIN_DEBUG
                    printf("-> H5Adelete_by_name(): loc_id object type: %s\n", object_type_to_string(loc_obj->obj_type));
                    printf("-> H5Adelete_by_name(): Path to object that attribute is attached to: %s\n\n", loc_params.loc_data.loc_by_name.name);
#endif

                    search_ret = RV_find_object_by_path(loc_obj, loc_params.loc_data.loc_by_name.name, &parent_obj_type,
                            RV_copy_object_URI_callback, NULL, temp_URI);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PATH, FAIL, "can't locate object that attribute is attached to")

#ifdef RV_PLUGIN_DEBUG
                    printf("-> H5Adelete_by_name(): found attribute's parent object by given path\n");
                    printf("-> H5Adelete_by_name(): attribute's parent object URI: %s\n", temp_URI);
                    printf("-> H5Adelete_by_name(): attribute's parent object type: %s\n\n", object_type_to_string(parent_obj_type));
#endif

                    obj_URI = temp_URI;

                    break;
                } /* H5VL_OBJECT_BY_NAME */

                /* H5Adelete_by_idx */
                case H5VL_OBJECT_BY_IDX:
                {
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
            switch (parent_obj_type) {
                case H5I_FILE:
                case H5I_GROUP:
                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/attributes/%s",
                             base_URL, obj_URI, url_encoded_attr_name)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "H5Adelete(_by_name) request URL exceeded maximum URL size")

                    break;

                case H5I_DATATYPE:
                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s/attributes/%s",
                             base_URL, obj_URI, url_encoded_attr_name)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "H5Adelete(_by_name) request URL exceeded maximum URL size")

                    break;

                case H5I_DATASET:
                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s/attributes/%s",
                             base_URL, obj_URI, url_encoded_attr_name)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "H5Adelete(_by_name) request URL exceeded maximum URL size")

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

            /* Setup the host header */
            host_header_len = strlen(loc_obj->domain->u.file.filepath_name) + strlen(host_string) + 1;
            if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

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

#ifdef RV_PLUGIN_DEBUG
            printf("-> Deleting attribute at URL: %s\n\n", request_url);

            printf("   /*************************************\\\n");
            printf("-> | Making DELETE request to the server |\n");
            printf("   \\*************************************/\n\n");
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

            switch (loc_params.type) {
                /* H5Aexists */
                case H5VL_OBJECT_BY_SELF:
                {
                    obj_URI = loc_obj->URI;
                    parent_obj_type = loc_obj->obj_type;

#ifdef RV_PLUGIN_DEBUG
                    printf("-> H5Aexists(): Attribute's parent object URI: %s\n", loc_obj->URI);
                    printf("-> H5Aexists(): Attribute's parent object type: %s\n\n", object_type_to_string(parent_obj_type));
#endif

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Aexists_by_name */
                case H5VL_OBJECT_BY_NAME:
                {
                    htri_t search_ret;

#ifdef RV_PLUGIN_DEBUG
                    printf("-> H5Aexists_by_name(): loc_id object type: %s\n", object_type_to_string(loc_obj->obj_type));
                    printf("-> H5Aexists_by_name(): Path to object that attribute is attached to: %s\n\n", loc_params.loc_data.loc_by_name.name);
#endif

                    search_ret = RV_find_object_by_path(loc_obj, loc_params.loc_data.loc_by_name.name, &parent_obj_type,
                            RV_copy_object_URI_callback, NULL, temp_URI);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PATH, FAIL, "can't locate object that attribute is attached to")

#ifdef RV_PLUGIN_DEBUG
                    printf("-> H5Aexists_by_name(): found attribute's parent object by given path\n");
                    printf("-> H5Aexists_by_name(): attribute's parent object URI: %s\n", temp_URI);
                    printf("-> H5Aexists_by_name(): attribute's parent object type: %s\n\n", object_type_to_string(parent_obj_type));
#endif

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
            switch (parent_obj_type) {
                case H5I_FILE:
                case H5I_GROUP:
                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/attributes/%s",
                             base_URL, obj_URI, url_encoded_attr_name)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "H5Aexists(_by_name) request URL exceeded maximum URL size")

                    break;

                case H5I_DATATYPE:
                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s/attributes/%s",
                             base_URL, obj_URI, url_encoded_attr_name)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "H5Aexists(_by_name) request URL exceeded maximum URL size")

                    break;

                case H5I_DATASET:
                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s/attributes/%s",
                             base_URL, obj_URI, url_encoded_attr_name)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "H5Aexists(_by_name) request URL exceeded maximum URL size")

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

            /* Setup the host header */
            host_header_len = strlen(loc_obj->domain->u.file.filepath_name) + strlen(host_string) + 1;
            if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

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

#ifdef RV_PLUGIN_DEBUG
            printf("-> Attribute existence check at URL: %s\n\n", request_url);

            printf("   /**********************************\\\n");
            printf("-> | Making GET request to the server |\n");
            printf("   \\**********************************/\n\n");
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
        {
            iter_data attr_iter_data;

            attr_iter_data.is_recursive               = FALSE;
            attr_iter_data.index_type                 = va_arg(arguments, H5_index_t);
            attr_iter_data.iter_order                 = va_arg(arguments, H5_iter_order_t);
            attr_iter_data.idx_p                      = va_arg(arguments, hsize_t *);
            attr_iter_data.iter_function.attr_iter_op = va_arg(arguments, H5A_operator2_t);
            attr_iter_data.op_data                    = va_arg(arguments, void *);

            if (!attr_iter_data.iter_function.attr_iter_op)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_ATTRITERERROR, FAIL, "no attribute iteration function specified")

            switch (loc_params.type) {
                /* H5Aiterate2 */
                case H5VL_OBJECT_BY_SELF:
                {
                    obj_URI = loc_obj->URI;
                    parent_obj_type = loc_obj->obj_type;

                    if (NULL == (attr_iter_object = RV_malloc(sizeof(RV_object_t))))
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, FAIL, "can't allocate copy of attribute's parent object")

                    memcpy(attr_iter_object, loc_obj, sizeof(RV_object_t));

                    /* Since we already have the attribute's parent object, but still need an hid_t for it
                     * to pass to the user's object, we will just copy the current object, making sure to
                     * increment the ref. counts for the object's fields so that closing it at the end of
                     * this function does not close the fields themselves in the real object, such as a
                     * dataset's dataspace.
                     */
                    switch (parent_obj_type) {
                        case H5I_FILE:
                        case H5I_GROUP:
                            if (H5Iinc_ref(loc_obj->u.group.gcpl_id) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL, "can't increment field's ref. count for copy of attribute's parent group")
                            break;

                        case H5I_DATATYPE:
                            if (H5Iinc_ref(loc_obj->u.datatype.dtype_id) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL, "can't increment field's ref. count for copy of attribute's parent datatype")
                            if (H5Iinc_ref(loc_obj->u.datatype.tcpl_id) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL, "can't increment field's ref. count for copy of attribute's parent datatype")
                            break;

                        case H5I_DATASET:
                            if (H5Iinc_ref(loc_obj->u.dataset.dtype_id) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL, "can't increment field's ref. count for copy of attribute's parent dataset")
                            if (H5Iinc_ref(loc_obj->u.dataset.space_id) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL, "can't increment field's ref. count for copy of attribute's parent dataset")
                            if (H5Iinc_ref(loc_obj->u.dataset.dapl_id) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL, "can't increment field's ref. count for copy of attribute's parent dataset")
                            if (H5Iinc_ref(loc_obj->u.dataset.dcpl_id) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL, "can't increment field's ref. count for copy of attribute's parent dataset")
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

#ifdef RV_PLUGIN_DEBUG
                    printf("-> H5Aiterate2(): Attribute's parent object URI: %s\n", loc_obj->URI);
                    printf("-> H5Aiterate2(): Attribute's parent object type: %s\n\n", object_type_to_string(parent_obj_type));
#endif

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Aiterate_by_name */
                case H5VL_OBJECT_BY_NAME:
                {
                    htri_t search_ret;

#ifdef RV_PLUGIN_DEBUG
                    printf("-> H5Aiterate_by_name(): loc_id object type: %s\n", object_type_to_string(loc_obj->obj_type));
                    printf("-> H5Aiterate_by_name(): Path to object that attribute is attached to: %s\n\n", loc_params.loc_data.loc_by_name.name);
#endif

                    search_ret = RV_find_object_by_path(loc_obj, loc_params.loc_data.loc_by_name.name, &parent_obj_type,
                            RV_copy_object_URI_callback, NULL, temp_URI);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PATH, FAIL, "can't locate object that attribute is attached to")

#ifdef RV_PLUGIN_DEBUG
                    printf("-> H5Aiterate_by_name(): found attribute's parent object by given path\n");
                    printf("-> H5Aiterate_by_name(): attribute's parent object URI: %s\n", temp_URI);
                    printf("-> H5Aiterate_by_name(): attribute's parent object type: %s\n\n", object_type_to_string(parent_obj_type));

                    printf("-> Opening attribute's parent object to generate an hid_t and work around VOL layer\n\n");
#endif

                    /* Since the VOL layer doesn't directly pass down the parent object's ID for the attribute,
                     * explicitly open the object here so that a valid hid_t can be passed to the user's
                     * attribute iteration callback. In the case of H5Aiterate, we are already passed the
                     * attribute's parent object, so we just generate a second ID for it instead of needing
                     * to open it explicitly.
                     */
                    switch (parent_obj_type) {
                        case H5I_FILE:
                        case H5I_GROUP:
                            if (NULL == (attr_iter_object = RV_group_open(loc_obj, loc_params,
                                    loc_params.loc_data.loc_by_name.name, H5P_DEFAULT, H5P_DEFAULT, NULL)))
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTOPENOBJ, FAIL, "can't open attribute's parent group")
                            break;

                        case H5I_DATATYPE:
                            if (NULL == (attr_iter_object = RV_datatype_open(loc_obj, loc_params,
                                    loc_params.loc_data.loc_by_name.name, H5P_DEFAULT, H5P_DEFAULT, NULL)))
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTOPENOBJ, FAIL, "can't open attribute's parent datatype")
                            break;

                        case H5I_DATASET:
                            if (NULL == (attr_iter_object = RV_dataset_open(loc_obj, loc_params,
                                    loc_params.loc_data.loc_by_name.name, H5P_DEFAULT, H5P_DEFAULT, NULL)))
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTOPENOBJ, FAIL, "can't open attribute's parent dataset")
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

                    obj_URI = temp_URI;

                    break;
                } /* H5VL_OBJECT_BY_NAME */

                case H5VL_OBJECT_BY_IDX:
                case H5VL_OBJECT_BY_ADDR:
                case H5VL_OBJECT_BY_REF:
                default:
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid loc_params type")
            } /* end switch */

            /* Redirect cURL from the base URL to
             * "/groups/<id>/attributes",
             * "/datatypes/<id>/attributes"
             * or
             * "/datasets/<id>/attributes",
             * depending on the type of the object the attribute is attached to. */
            switch (parent_obj_type) {
                case H5I_FILE:
                case H5I_GROUP:
                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/attributes",
                             base_URL, obj_URI)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "H5Aiterate(_by_name) request URL exceeded maximum URL size")

                    break;

                case H5I_DATATYPE:
                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s/attributes",
                             base_URL, obj_URI)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "H5Aiterate(_by_name) request URL exceeded maximum URL size")

                    break;

                case H5I_DATASET:
                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s/attributes",
                             base_URL, obj_URI)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "H5Aiterate(_by_name) request URL exceeded maximum URL size")

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

            /* Register an hid_t for the attribute's parent object */

            /* In order to appease H5VLobject_register(), ensure that the proper interface is initialized before
             * calling it, just as in the code for link iteration.
             */
            if (H5I_FILE == parent_obj_type || H5I_GROUP == parent_obj_type) {
                H5E_BEGIN_TRY {
                    H5Gopen2(-1, NULL, H5P_DEFAULT);
                } H5E_END_TRY;
            } /* end if */
            else if (H5I_DATATYPE == parent_obj_type) {
                H5E_BEGIN_TRY {
                    H5Topen2(-1, NULL, H5P_DEFAULT);
                } H5E_END_TRY;
            } /* end else if */
            else {
                H5E_BEGIN_TRY {
                    H5Dopen2(-1, NULL, H5P_DEFAULT);
                } H5E_END_TRY;
            } /* end else */

            if ((attr_iter_object_id = H5VLobject_register(attr_iter_object, parent_obj_type, REST_g)) < 0)
                FUNC_GOTO_ERROR(H5E_ATOM, H5E_CANTREGISTER, FAIL, "can't create ID for parent object for attribute iteration")
            attr_iter_data.iter_obj_id = attr_iter_object_id;

            /* Make a GET request to the server to retrieve all of the attributes attached to the given object */

            /* Setup the host header */
            host_header_len = strlen(loc_obj->domain->u.file.filepath_name) + strlen(host_string) + 1;
            if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

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

#ifdef RV_PLUGIN_DEBUG
            printf("-> Retrieving all attributes attached to object using URL: %s\n\n", request_url);

            printf("   /**********************************\\\n");
            printf("-> | Making GET request to the server |\n");
            printf("   \\**********************************/\n\n");
#endif

            CURL_PERFORM(curl, H5E_ATTR, H5E_CANTGET, FAIL);

            if (RV_parse_response(response_buffer.buffer, &attr_iter_data, NULL, RV_attr_iter_callback) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "can't iterate over attributes")

            break;
        } /* H5VL_ATTR_ITER */

        /* H5Arename (_by_name) */
        case H5VL_ATTR_RENAME:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_UNSUPPORTED, FAIL, "H5Arename and H5Arename_by_name are unsupported")

        default:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "unknown attribute operation")
    } /* end switch */

done:
    if (host_header)
        RV_free(host_header);

    if (attr_iter_object_id >= 0) {
        if (H5I_GROUP == parent_obj_type) {
            if (H5Gclose(attr_iter_object_id) < 0)
                FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTCLOSEOBJ, FAIL, "can't close attribute iteration parent group")
        }
        else if (H5I_DATATYPE == parent_obj_type) {
            if (H5Tclose(attr_iter_object_id) < 0)
                FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTCLOSEOBJ, FAIL, "can't close attribute iteration parent datatype")
        }
        else if (H5I_DATASET == parent_obj_type) {
            if (H5Dclose(attr_iter_object_id) < 0)
                FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTCLOSEOBJ, FAIL, "can't close attribute iteration parent dataset")
        } /* end else if */
        else
            FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTCLOSEOBJ, FAIL, "invalid attribute parent object type")
    } /* end if */

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

    PRINT_ERROR_STACK

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
RV_attr_close(void *attr, hid_t dxpl_id, void **req)
{
    RV_object_t *_attr = (RV_object_t *) attr;
    herr_t       ret_value = SUCCEED;

    if (!_attr)
        FUNC_GOTO_DONE(SUCCEED);

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received attribute close call with following parameters:\n");
    printf("     - Attribute's object type: %s\n", object_type_to_string(_attr->obj_type));
    if (H5I_ATTR == _attr->obj_type && _attr->u.attribute.attr_name)
        printf("     - Attribute's name: %s\n", _attr->u.attribute.attr_name);
    if (_attr->domain && _attr->domain->u.file.filepath_name)
        printf("     - Attribute's domain path: %s\n", _attr->domain->u.file.filepath_name);
    printf("\n");
#endif

    if (H5I_ATTR != _attr->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not an attribute")

    if (_attr->u.attribute.attr_name)
        _attr->u.attribute.attr_name = RV_free(_attr->u.attribute.attr_name);

    if (_attr->u.attribute.dtype_id >= 0 && H5Tclose(_attr->u.attribute.dtype_id) < 0)
        FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, FAIL, "can't close attribute's datatype")
    if (_attr->u.attribute.space_id >= 0 && H5Sclose(_attr->u.attribute.space_id) < 0)
        FUNC_DONE_ERROR(H5E_DATASPACE, H5E_CANTCLOSEOBJ, FAIL, "can't close attribute's dataspace")

    if (_attr->u.attribute.aapl_id >= 0) {
        if (_attr->u.attribute.aapl_id != H5P_ATTRIBUTE_ACCESS_DEFAULT && H5Pclose(_attr->u.attribute.aapl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close AAPL")
    } /* end if */
    if (_attr->u.attribute.acpl_id >= 0) {
        if (_attr->u.attribute.acpl_id != H5P_ATTRIBUTE_CREATE_DEFAULT && H5Pclose(_attr->u.attribute.acpl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close ACPL")
    } /* end if */

    _attr = RV_free(_attr);

done:
    PRINT_ERROR_STACK

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
RV_datatype_commit(void *obj, H5VL_loc_params_t loc_params, const char *name, hid_t type_id,
                   hid_t lcpl_id, hid_t tcpl_id, hid_t tapl_id, hid_t dxpl_id, void **req)
{
    RV_object_t *parent = (RV_object_t *) obj;
    RV_object_t *new_datatype = NULL;
    size_t       commit_request_nalloc = 0;
    size_t       link_body_nalloc = 0;
    size_t       host_header_len = 0;
    size_t       datatype_body_len = 0;
    char        *host_header = NULL;
    char        *commit_request_body = NULL;
    char        *datatype_body = NULL;
    char        *link_body = NULL;
    char        *path_dirname = NULL;
    char         request_url[URL_MAX_LENGTH];
    int          commit_request_len = 0;
    int          link_body_len = 0;
    int          url_len = 0;
    void        *ret_value = NULL;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received datatype commit call with following parameters:\n");
    printf("     - H5Tcommit variant: %s\n", name ? "H5Tcommit2" : "H5Tcommit_anon");
    if (name) printf("     - Datatype's name: %s\n", name);
    printf("     - Datatype's class: %s\n", datatype_class_to_string(type_id));
    printf("     - Datatype's parent object URI: %s\n", parent->URI);
    printf("     - Datatype's parent object type: %s\n", object_type_to_string(parent->obj_type));
    printf("     - Datatype's parent object domain path: %s\n", parent->domain->u.file.filepath_name);
    printf("     - Default LCPL? %s\n", (H5P_LINK_CREATE_DEFAULT == lcpl_id) ? "yes" : "no");
    printf("     - Default TCPL? %s\n", (H5P_DATATYPE_CREATE_DEFAULT == tcpl_id) ? "yes" : "no");
    printf("     - Default TAPL? %s\n\n", (H5P_DATATYPE_ACCESS_DEFAULT == tapl_id) ? "yes" : "no");
#endif

    if (H5I_FILE != parent->obj_type && H5I_GROUP != parent->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object not a file or group")

    /* Check for write access */
    if (!(parent->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, NULL, "no write intent on file")

    /* Allocate and setup internal Datatype struct */
    if (NULL == (new_datatype = (RV_object_t *) RV_malloc(sizeof(*new_datatype))))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, NULL, "can't allocate space for datatype object")

    new_datatype->URI[0] = '\0';
    new_datatype->obj_type = H5I_DATATYPE;
    new_datatype->domain = parent->domain; /* Store pointer to file that the newly-committed datatype is in */
    new_datatype->u.datatype.dtype_id = FAIL;
    new_datatype->u.datatype.tapl_id = FAIL;
    new_datatype->u.datatype.tcpl_id = FAIL;

    /* Copy the TAPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * datatype access property list functions will function correctly
     */
    if (H5P_DATATYPE_ACCESS_DEFAULT != tapl_id) {
        if ((new_datatype->u.datatype.tapl_id = H5Pcopy(tapl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy TAPL")
    } /* end if */
    else
        new_datatype->u.datatype.tapl_id = H5P_DATATYPE_ACCESS_DEFAULT;

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
    if (RV_convert_datatype_to_JSON(type_id, &datatype_body, &datatype_body_len, FALSE) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, NULL, "can't convert datatype to JSON representation")

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

#ifdef RV_PLUGIN_DEBUG
        printf("-> Creating JSON link for datatype\n\n");
#endif

        /* In case the user specified a path which contains multiple groups on the way to the
         * one which the datatype will ultimately be linked under, extract out the path to the
         * final group in the chain */
        if (NULL == (path_dirname = RV_dirname(name)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, NULL, "invalid pathname for datatype link")
        empty_dirname = !strcmp(path_dirname, "");

        /* If the path to the final group in the chain wasn't empty, get the URI of the final
         * group in order to correctly link the datatype into the file structure. Otherwise,
         * the supplied parent group is the one housing the datatype, so just use its URI.
         */
        if (!empty_dirname) {
            H5I_type_t obj_type = H5I_GROUP;
            htri_t     search_ret;

            search_ret = RV_find_object_by_path(parent, path_dirname, &obj_type, RV_copy_object_URI_callback, NULL, target_URI);
            if (!search_ret || search_ret < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PATH, NULL, "can't locate target for dataset link")
        } /* end if */

        link_body_nalloc = strlen(link_body_format) + strlen(link_basename) + (empty_dirname ? strlen(parent->URI) : strlen(target_URI)) + 1;
        if (NULL == (link_body = (char *) RV_malloc(link_body_nalloc)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, NULL, "can't allocate space for datatype link body")

        /* Form the Datatype Commit Link portion of the commit request using the above format
         * specifier and the corresponding arguments */
        if ((link_body_len = snprintf(link_body, link_body_nalloc, link_body_format, empty_dirname ? parent->URI : target_URI, link_basename)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, NULL, "snprintf error")

        if ((size_t) link_body_len >= link_body_nalloc)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, NULL, "datatype link create request body size exceeded allocated buffer size")
    } /* end if */

    /* Form the request body to commit the Datatype */
    commit_request_nalloc = datatype_body_len + (link_body ? (size_t) link_body_len + 2 : 0) + 3;
    if (NULL == (commit_request_body = (char *) RV_malloc(commit_request_nalloc)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, NULL, "can't allocate space for datatype commit request body")

    if ((commit_request_len = snprintf(commit_request_body, commit_request_nalloc,
             "{%s%s%s}",
             datatype_body,
             link_body ? ", " : "",
             link_body ? link_body : "")
        ) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, NULL, "snprintf error")

    if ((size_t) commit_request_len >= commit_request_nalloc)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, NULL, "datatype create request body size exceeded allocated buffer size")

#ifdef RV_PLUGIN_DEBUG
    printf("-> Datatype commit request body:\n%s\n\n", commit_request_body);
#endif

    /* Setup the host header */
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
    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes", base_URL)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, NULL, "snprintf error")

    if (url_len >= URL_MAX_LENGTH)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, NULL, "datatype create URL size exceeded maximum URL size")

#ifdef RV_PLUGIN_DEBUG
    printf("-> Datatype commit URL: %s\n\n", request_url);
#endif

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTSET, NULL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POST, 1))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTSET, NULL, "can't set up cURL to make HTTP POST request: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDS, commit_request_body))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTSET, NULL, "can't set cURL POST data: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t) commit_request_len))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTSET, NULL, "can't set cURL POST data size: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTSET, NULL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef RV_PLUGIN_DEBUG
    printf("-> Committing datatype\n\n");

    printf("   /***********************************\\\n");
    printf("-> | Making POST request to the server |\n");
    printf("   \\***********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_DATATYPE, H5E_BADVALUE, NULL);

#ifdef RV_PLUGIN_DEBUG
    printf("-> Committed datatype\n\n");
#endif

    /* Store the newly-committed Datatype's URI */
    if (RV_parse_response(response_buffer.buffer, NULL, new_datatype->URI, RV_copy_object_URI_callback) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, NULL, "can't parse committed datatype's URI")

    ret_value = (void *) new_datatype;

done:
#ifdef RV_PLUGIN_DEBUG
    printf("-> Datatype commit response buffer:\n%s\n\n", response_buffer.buffer);

    if (new_datatype && ret_value) {
        printf("-> Datatype's info:\n");
        printf("     - Datatype's URI: %s\n", new_datatype->URI);
        printf("     - Datatype's object type: %s\n", object_type_to_string(new_datatype->obj_type));
        printf("     - Datatype's domain path: %s\n\n", new_datatype->domain->u.file.filepath_name);
    } /* end if */
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

    PRINT_ERROR_STACK

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
RV_datatype_open(void *obj, H5VL_loc_params_t loc_params, const char *name,
                 hid_t tapl_id, hid_t dxpl_id, void **req)
{
    RV_object_t *parent = (RV_object_t *) obj;
    RV_object_t *datatype = NULL;
    H5I_type_t   obj_type = H5I_UNINIT;
    htri_t       search_ret;
    void        *ret_value = NULL;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received datatype open call with following parameters:\n");
    printf("     - loc_id object's URI: %s\n", parent->URI);
    printf("     - loc_id object's type: %s\n", object_type_to_string(parent->obj_type));
    printf("     - loc_id object's domain path: %s\n", parent->domain->u.file.filepath_name);
    printf("     - Path to datatype: %s\n", name);
    printf("     - Default TAPL? %s\n\n", (H5P_DATATYPE_ACCESS_DEFAULT) ? "yes" : "no");
#endif

    if (H5I_FILE != parent->obj_type && H5I_GROUP != parent->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object not a file or group")

    /* Allocate and setup internal Datatype struct */
    if (NULL == (datatype = (RV_object_t *) RV_malloc(sizeof(*datatype))))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, NULL, "can't allocate space for datatype object")

    datatype->URI[0] = '\0';
    datatype->obj_type = H5I_DATATYPE;
    datatype->domain = parent->domain; /* Store pointer to file that the opened Dataset is within */
    datatype->u.datatype.dtype_id = FAIL;
    datatype->u.datatype.tapl_id = FAIL;
    datatype->u.datatype.tcpl_id = FAIL;

    /* Locate the named Datatype */
    search_ret = RV_find_object_by_path(parent, name, &obj_type, RV_copy_object_URI_callback, NULL, datatype->URI);
    if (!search_ret || search_ret < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PATH, NULL, "can't locate datatype by path")

#ifdef RV_PLUGIN_DEBUG
    printf("-> Found datatype by given path\n\n");
#endif

    /* Set up the actual datatype by converting the string representation into an hid_t */
    if ((datatype->u.datatype.dtype_id = RV_parse_datatype(response_buffer.buffer, TRUE)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, NULL, "can't convert JSON to usable datatype")

    /* Copy the TAPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * datatype access property list functions will function correctly
     */
    if (H5P_DATATYPE_ACCESS_DEFAULT != tapl_id) {
        if ((datatype->u.datatype.tapl_id = H5Pcopy(tapl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy TAPL")
    } /* end if */
    else
        datatype->u.datatype.tapl_id = H5P_DATATYPE_ACCESS_DEFAULT;

    /* Set up a TCPL for the datatype so that H5Tget_create_plist() will function correctly.
       Note that currently there aren't any properties that can be set for a TCPL, however
       we still use one here specifically for H5Tget_create_plist(). */
    if ((datatype->u.datatype.tcpl_id = H5Pcreate(H5P_DATATYPE_CREATE)) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "can't create TCPL for datatype")

    ret_value = (void *) datatype;

done:
#ifdef RV_PLUGIN_DEBUG
    printf("-> Datatype open response buffer:\n%s\n\n", response_buffer.buffer);

    if (datatype && ret_value) {
        printf("-> Datatype's info:\n");
        printf("     - Datatype's URI: %s\n", datatype->URI);
        printf("     - Datatype's object type: %s\n", object_type_to_string(datatype->obj_type));
        printf("     - Datatype's domain path: %s\n", datatype->domain->u.file.filepath_name);
        printf("     - Datatype's datatype class: %s\n\n", datatype_class_to_string(datatype->u.datatype.dtype_id));
    } /* end if */
#endif

    /* Clean up allocated datatype object if there was an issue */
    if (datatype && !ret_value)
        if (RV_datatype_close(datatype, FAIL, NULL) < 0)
            FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, NULL, "can't close datatype")

    PRINT_ERROR_STACK

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
RV_datatype_get(void *obj, H5VL_datatype_get_t get_type, hid_t dxpl_id,
                void **req, va_list arguments)
{
    RV_object_t *dtype = (RV_object_t *) obj;
    herr_t       ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received datatype get call with following parameters:\n");
    printf("     - Datatype get call type: %s\n", datatype_get_type_to_string(get_type));
    printf("     - Datatype's URI: %s\n", dtype->URI);
    printf("     - Datatype's object type: %s\n", object_type_to_string(dtype->obj_type));
    printf("     - Datatype's domain path: %s\n\n", dtype->domain->u.file.filepath_name);
#endif

    if (H5I_DATATYPE != dtype->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a datatype")

    switch (get_type) {
        case H5VL_DATATYPE_GET_BINARY:
        {
            ssize_t *nalloc = va_arg(arguments, ssize_t *);
            void    *buf = va_arg(arguments, void *);
            size_t   size = va_arg(arguments, size_t);

            if (H5Tencode(dtype->u.datatype.dtype_id, buf, &size) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADTYPE, FAIL, "can't determine serialized length of datatype")

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
    PRINT_ERROR_STACK

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
RV_datatype_close(void *dt, hid_t dxpl_id, void **req)
{
    RV_object_t *_dtype = (RV_object_t *) dt;
    herr_t       ret_value = SUCCEED;

    if (!_dtype)
        FUNC_GOTO_DONE(SUCCEED);

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received datatype close call with following parameters:\n");
    printf("     - Datatype's URI: %s\n", _dtype->URI);
    printf("     - Datatype's object type: %s\n", object_type_to_string(_dtype->obj_type));
    if (_dtype->domain && _dtype->domain->u.file.filepath_name)
        printf("     - Datatype's domain path: %s\n", _dtype->domain->u.file.filepath_name);
    printf("\n");
#endif

    if (H5I_DATATYPE != _dtype->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a datatype")

    if (_dtype->u.datatype.dtype_id >= 0 && H5Tclose(_dtype->u.datatype.dtype_id) < 0)
        FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, FAIL, "can't close datatype")

    if (_dtype->u.datatype.tapl_id >= 0) {
        if (_dtype->u.datatype.tapl_id != H5P_DATATYPE_ACCESS_DEFAULT && H5Pclose(_dtype->u.datatype.tapl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close TAPL")
    } /* end if */
    if (_dtype->u.datatype.tcpl_id >= 0) {
        if (_dtype->u.datatype.tcpl_id != H5P_DATATYPE_CREATE_DEFAULT && H5Pclose(_dtype->u.datatype.tcpl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close TCPL")
    } /* end if */

    _dtype = RV_free(_dtype);

done:
    PRINT_ERROR_STACK

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
RV_dataset_create(void *obj, H5VL_loc_params_t loc_params, const char *name, hid_t dcpl_id,
                  hid_t dapl_id, hid_t dxpl_id, void **req)
{
    RV_object_t *parent = (RV_object_t *) obj;
    RV_object_t *new_dataset = NULL;
    curl_off_t   create_request_body_len = 0;
    size_t       host_header_len = 0;
    hid_t        space_id, type_id;
    char        *host_header = NULL;
    char        *create_request_body = NULL;
    char         request_url[URL_MAX_LENGTH];
    int          url_len = 0;
    void        *ret_value = NULL;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received dataset create call with following parameters:\n");
    printf("     - H5Dcreate variant: %s\n", name ? "H5Dcreate2" : "H5Dcreate_anon");
    if (name) printf("     - Dataset's name: %s\n", name);
    printf("     - Dataset's parent object URI: %s\n", parent->URI);
    printf("     - Dataset's parent object type: %s\n", object_type_to_string(parent->obj_type));
    printf("     - Dataset's parent object domain path: %s\n", parent->domain->u.file.filepath_name);
    printf("     - Default DCPL? %s\n", (H5P_DATASET_CREATE_DEFAULT == dcpl_id) ? "yes" : "no");
    printf("     - Default DAPL? %s\n\n", (H5P_DATASET_ACCESS_DEFAULT == dapl_id) ? "yes" : "no");
#endif

    if (H5I_FILE != parent->obj_type && H5I_GROUP != parent->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object not a file or group")

    /* Check for write access */
    if (!(parent->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, NULL, "no write intent on file")

    /* Allocate and setup internal Dataset struct */
    if (NULL == (new_dataset = (RV_object_t *) RV_malloc(sizeof(*new_dataset))))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, NULL, "can't allocate space for dataset object")

    new_dataset->URI[0] = '\0';
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
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCONVERT, NULL, "can't convert dataset creation parameters to JSON")

        /* Check to make sure that the size of the create request HTTP body can safely be cast to a curl_off_t */
        if (sizeof(curl_off_t) < sizeof(size_t))
            ASSIGN_TO_SMALLER_SIZE(create_request_body_len, curl_off_t, tmp_len, size_t)
        else if (sizeof(curl_off_t) > sizeof(size_t))
            create_request_body_len = (curl_off_t) tmp_len;
        else
            ASSIGN_TO_SAME_SIZE_UNSIGNED_TO_SIGNED(create_request_body_len, curl_off_t, tmp_len, size_t)
    }

    /* Setup the host header */
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
    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datasets", base_URL)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, NULL, "snprintf error")

    if (url_len >= URL_MAX_LENGTH)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, NULL, "dataset create URL size exceeded maximum URL size")

#ifdef RV_PLUGIN_DEBUG
    printf("-> Dataset creation request URL: %s\n\n", request_url);
#endif

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

#ifdef RV_PLUGIN_DEBUG
    printf("-> Creating dataset\n\n");

    printf("   /***********************************\\\n");
    printf("-> | Making POST request to the server |\n");
    printf("   \\***********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_DATASET, H5E_CANTCREATE, NULL);

#ifdef RV_PLUGIN_DEBUG
    printf("-> Created dataset\n\n");
#endif

    /* Store the newly-created dataset's URI */
    if (RV_parse_response(response_buffer.buffer, NULL, new_dataset->URI, RV_copy_object_URI_callback) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCREATE, NULL, "can't parse new dataset's URI")

    if (H5Pget(dcpl_id, H5VL_PROP_DSET_TYPE_ID, &type_id) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, NULL, "can't get property list value for dataset's datatype ID")
    if (H5Pget(dcpl_id, H5VL_PROP_DSET_SPACE_ID, &space_id) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, NULL, "can't get property list value for dataset's dataspace ID")

    if ((new_dataset->u.dataset.dtype_id = H5Tcopy(type_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCOPY, NULL, "failed to copy dataset's datatype")
    if ((new_dataset->u.dataset.space_id = H5Scopy(space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCOPY, NULL, "failed to copy dataset's dataspace")

    ret_value = (void *) new_dataset;

done:
#ifdef RV_PLUGIN_DEBUG
    printf("-> Dataset create response buffer:\n%s\n\n", response_buffer.buffer);

    if (new_dataset && ret_value) {
        printf("-> New dataset's info:\n");
        printf("     - New dataset's URI: %s\n", new_dataset->URI);
        printf("     - New dataset's object type: %s\n", object_type_to_string(new_dataset->obj_type));
        printf("     - New dataset's domain path: %s\n\n", new_dataset->domain->u.file.filepath_name);
    } /* end if */
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

    PRINT_ERROR_STACK

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
RV_dataset_open(void *obj, H5VL_loc_params_t loc_params, const char *name,
                hid_t dapl_id, hid_t dxpl_id, void **req)
{
    RV_object_t *parent = (RV_object_t *) obj;
    RV_object_t *dataset = NULL;
    H5I_type_t   obj_type = H5I_UNINIT;
    htri_t       search_ret;
    void        *ret_value = NULL;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received dataset open call with following parameters:\n");
    printf("     - loc_id object's URI: %s\n", parent->URI);
    printf("     - loc_id object's type: %s\n", object_type_to_string(parent->obj_type));
    printf("     - loc_id object's domain path: %s\n", parent->domain->u.file.filepath_name);
    printf("     - Path to dataset: %s\n", name);
    printf("     - Default DAPL? %s\n\n", (H5P_DATASET_ACCESS_DEFAULT == dapl_id) ? "yes" : "no");
#endif

    if (H5I_FILE != parent->obj_type && H5I_GROUP != parent->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object not a file or group")

    /* Allocate and setup internal Dataset struct */
    if (NULL == (dataset = (RV_object_t *) RV_malloc(sizeof(*dataset))))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, NULL, "can't allocate space for dataset object")

    dataset->URI[0] = '\0';
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

#ifdef RV_PLUGIN_DEBUG
    printf("-> Found dataset by given path\n\n");
#endif

    /* Set up a Dataspace for the opened Dataset */
    if ((dataset->u.dataset.space_id = RV_parse_dataspace(response_buffer.buffer)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCONVERT, NULL, "can't convert JSON to usable dataspace for dataset")

    /* Set up a Datatype for the opened Dataset */
    if ((dataset->u.dataset.dtype_id = RV_parse_datatype(response_buffer.buffer, TRUE)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, NULL, "can't convert JSON to usable datatype for dataset")

    /* Copy the DAPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * H5Dget_access_plist() will function correctly
     */
    if (H5P_DATASET_ACCESS_DEFAULT != dapl_id) {
        if ((dataset->u.dataset.dapl_id = H5Pcopy(dapl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "can't copy DAPL")
    } /* end if */
    else
        dataset->u.dataset.dapl_id = H5P_DATASET_ACCESS_DEFAULT;

    /* Set up a DCPL for the dataset so that H5Dget_create_plist() will function correctly */
    if ((dataset->u.dataset.dcpl_id = H5Pcreate(H5P_DATASET_CREATE)) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "can't create DCPL for dataset")

    /* Set any necessary creation properties on the DCPL setup for the dataset */
    if (RV_parse_response(response_buffer.buffer, NULL, &dataset->u.dataset.dcpl_id, RV_parse_dataset_creation_properties_callback) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCREATE, NULL, "can't parse dataset's creation properties from JSON representation")

    ret_value = (void *) dataset;

done:
#ifdef RV_PLUGIN_DEBUG
    printf("-> Dataset open response buffer:\n%s\n\n", response_buffer.buffer);

    if (dataset && ret_value) {
        printf("-> Dataset's info:\n");
        printf("     - Dataset's URI: %s\n", dataset->URI);
        printf("     - Dataset's object type: %s\n", object_type_to_string(dataset->obj_type));
        printf("     - Dataset's domain path: %s\n", dataset->domain->u.file.filepath_name);
        printf("     - Dataset's datatype class: %s\n\n", datatype_class_to_string(dataset->u.dataset.dtype_id));
    }
#endif

    /* Clean up allocated dataset object if there was an issue */
    if (dataset && !ret_value)
        if (RV_dataset_close(dataset, FAIL, NULL) < 0)
            FUNC_DONE_ERROR(H5E_DATASET, H5E_CANTCLOSEOBJ, NULL, "can't close dataset")

    PRINT_ERROR_STACK

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
                hid_t file_space_id, hid_t dxpl_id, void *buf, void **req)
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
    int           url_len = 0;
    herr_t        ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received dataset read call with following parameters:\n");
    printf("     - Dataset's URI: %s\n", dataset->URI);
    printf("     - Dataset's object type: %s\n", object_type_to_string(dataset->obj_type));
    printf("     - Dataset's domain path: %s\n", dataset->domain->u.file.filepath_name);
    printf("     - Entire memory dataspace selected? %s\n", (mem_space_id == H5S_ALL) ? "yes" : "no");
    printf("     - Entire file dataspace selected? %s\n", (file_space_id == H5S_ALL) ? "yes" : "no");
    printf("     - Default DXPL? %s\n\n", (dxpl_id == H5P_DATASET_XFER_DEFAULT) ? "yes" : "no");
#endif

    if (H5I_DATASET != dataset->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a dataset")
    if (!buf)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "read buffer was NULL")

    /* Determine whether it's possible to send the data as a binary blob instead of a JSON array */
    if (H5T_NO_CLASS == (dtype_class = H5Tget_class(mem_type_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid")

    if ((is_variable_str = H5Tis_variable_str(mem_type_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid")

    /* Only perform a binary transfer for fixed-length datatype datasets with an
     * All or Hyperslab selection. Point selections are dealt with by POSTing the
     * point list as JSON in the request body.
     */
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

            /* Copy the selection from file_space_id into the mem_space_id. */
            if (H5Sselect_copy(mem_space_id, file_space_id) < 0)
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCOPY, FAIL, "can't copy selection from file space to memory space")
        } /* end if */

        /* Since the selection in the dataset's file dataspace is not set
         * to "all", convert the selection into JSON */

        /* Retrieve the selection type to choose how to format the dataspace selection */
        if (H5S_SEL_ERROR == (sel_type = H5Sget_select_type(file_space_id)))
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't get dataspace selection type")
        is_transfer_binary = is_transfer_binary && (H5S_SEL_POINTS != sel_type);

        if (RV_convert_dataspace_selection_to_string(file_space_id, &selection_body, &selection_body_len, is_transfer_binary) < 0)
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCONVERT, FAIL, "can't convert dataspace selection to string representation")
    } /* end else */

    /* Verify that the number of selected points matches */
    if ((mem_select_npoints = H5Sget_select_npoints(mem_space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "memory dataspace is invalid")
    if ((file_select_npoints = H5Sget_select_npoints(file_space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "file dataspace is invalid")
    if (mem_select_npoints != file_select_npoints)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "memory selection num points != file selection num points")

#ifdef RV_PLUGIN_DEBUG
    printf("-> %lld points selected in file dataspace\n", file_select_npoints);
    printf("-> %lld points selected in memory dataspace\n\n", mem_select_npoints);
#endif

    /* Setup the host header */
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
    if ((url_len = snprintf(request_url, URL_MAX_LENGTH,
                            "%s/datasets/%s/value%s%s",
                            base_URL,
                            dataset->URI,
                            is_transfer_binary && selection_body && (H5S_SEL_POINTS != sel_type) ? "?select=" : "",
                            is_transfer_binary && selection_body && (H5S_SEL_POINTS != sel_type) ? selection_body : "")
        ) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

    if (url_len >= URL_MAX_LENGTH)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "dataset read URL size exceeded maximum URL size")

#ifdef RV_PLUGIN_DEBUG
    printf("-> Dataset read URL: %s\n\n", request_url);
#endif

    /* If using a point selection, instruct cURL to perform a POST request
     * in order to post the point list. Otherwise, a simple GET request
     * can be made, where the selection body should have already been
     * added as a request parameter to the GET URL.
     */
    if (H5S_SEL_POINTS == sel_type) {
        curl_off_t post_len;

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

        /* Check to make sure that the size of the selection HTTP body can safely be cast to a curl_off_t */
        if (sizeof(curl_off_t) < sizeof(size_t))
            ASSIGN_TO_SMALLER_SIZE(post_len, curl_off_t, selection_body_len + 2, size_t)
        else if (sizeof(curl_off_t) > sizeof(size_t))
            post_len = (curl_off_t) (selection_body_len + 2);
        else
            ASSIGN_TO_SAME_SIZE_UNSIGNED_TO_SIGNED(post_len, curl_off_t, selection_body_len + 2, size_t)

        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POST, 1))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP POST request: %s", curl_err_buf)
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDS, selection_body))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL POST data: %s", curl_err_buf)
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, post_len))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL POST data size: %s", curl_err_buf)

        curl_headers = curl_slist_append(curl_headers, "Content-Type: application/json");

#ifdef RV_PLUGIN_DEBUG
        printf("-> Setup cURL to POST point list for dataset read\n\n");
#endif
    } /* end if */
    else {
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP GET request: %s", curl_err_buf)
    } /* end else */

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef RV_PLUGIN_DEBUG
    printf("-> Reading dataset\n\n");

    printf("   /***************************************\\\n");
    printf("-> | Making GET/POST request to the server |\n");
    printf("   \\***************************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_DATASET, H5E_READERROR, FAIL);

    if ((H5T_REFERENCE != dtype_class) && (H5T_VLEN != dtype_class) && !is_variable_str) {
        size_t dtype_size;

        if (0 == (dtype_size = H5Tget_size(mem_type_id)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid")

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
#ifdef RV_PLUGIN_DEBUG
    printf("-> Dataset read response buffer:\n%s\n\n", response_buffer.buffer);
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

    PRINT_ERROR_STACK

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
                 hid_t file_space_id, hid_t dxpl_id, const void *buf, void **req)
{
    H5S_sel_type  sel_type = H5S_SEL_ALL;
    RV_object_t  *dataset = (RV_object_t *) obj;
    upload_info   uinfo;
    H5T_class_t   dtype_class;
    curl_off_t    write_len;
    hssize_t      mem_select_npoints, file_select_npoints;
    hbool_t       is_transfer_binary = FALSE;
    htri_t        is_variable_str;
    size_t        host_header_len = 0;
    size_t        write_body_len = 0;
    size_t        selection_body_len = 0;
    char         *selection_body = NULL;
    char         *base64_encoded_value = NULL;
    char         *host_header = NULL;
    char         *write_body = NULL;
    char          request_url[URL_MAX_LENGTH];
    int           url_len = 0;
    herr_t        ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received dataset write call with following parameters:\n");
    printf("     - Dataset's URI: %s\n", dataset->URI);
    printf("     - Dataset's object type: %s\n", object_type_to_string(dataset->obj_type));
    printf("     - Dataset's domain path: %s\n", dataset->domain->u.file.filepath_name);
    printf("     - Entire memory dataspace selected? %s\n", (mem_space_id == H5S_ALL) ? "yes" : "no");
    printf("     - Entire file dataspace selected? %s\n", (file_space_id == H5S_ALL) ? "yes" : "no");
    printf("     - Default DXPL? %s\n\n", (dxpl_id == H5P_DATASET_XFER_DEFAULT) ? "yes" : "no");
#endif

    if (H5I_DATASET != dataset->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a dataset")
    if (!buf)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "write buffer was NULL")

    /* Check for write access */
    if (!(dataset->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "no write intent on file")

    /* Determine whether it's possible to send the data as a binary blob instead of as JSON */
    if (H5T_NO_CLASS == (dtype_class = H5Tget_class(mem_type_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid")

    if ((is_variable_str = H5Tis_variable_str(mem_type_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid")

    /* Only perform a binary transfer for fixed-length datatype datasets with an
     * All or Hyperslab selection. Point selections are dealt with by POSTing the
     * point list as JSON in the request body.
     */
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
            if (H5Sselect_copy(mem_space_id, file_space_id) < 0)
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCOPY, FAIL, "can't copy selection from file space to memory space")
        } /* end if */

        /* Since the selection in the dataset's file dataspace is not set
         * to "all", convert the selection into JSON */

        /* Retrieve the selection type here for later use */
        if (H5S_SEL_ERROR == (sel_type = H5Sget_select_type(file_space_id)))
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't get dataspace selection type")
        is_transfer_binary = is_transfer_binary && (H5S_SEL_POINTS != sel_type);

        if (RV_convert_dataspace_selection_to_string(file_space_id, &selection_body, &selection_body_len, is_transfer_binary) < 0)
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCONVERT, FAIL, "can't convert dataspace selection to string representation")
    } /* end else */

    /* Verify that the number of selected points matches */
    if ((mem_select_npoints = H5Sget_select_npoints(mem_space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "memory dataspace is invalid")
    if ((file_select_npoints = H5Sget_select_npoints(file_space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "file dataspace is invalid")
    if (mem_select_npoints != file_select_npoints)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "memory selection num points != file selection num points")

#ifdef RV_PLUGIN_DEBUG
    printf("-> %lld points selected in file dataspace\n", file_select_npoints);
    printf("-> %lld points selected in memory dataspace\n\n", mem_select_npoints);
#endif

    /* Setup the size of the data being transferred and the data buffer itself (for non-simple
     * types like object references or variable length types)
     */
    if ((H5T_REFERENCE != dtype_class) && (H5T_VLEN != dtype_class) && !is_variable_str) {
        size_t dtype_size;

        if (0 == (dtype_size = H5Tget_size(mem_type_id)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid")

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


    /* Setup the host header */
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
    if ((url_len = snprintf(request_url, URL_MAX_LENGTH,
                            "%s/datasets/%s/value%s%s",
                            base_URL,
                            dataset->URI,
                            is_transfer_binary && selection_body && (H5S_SEL_POINTS != sel_type) ? "?select=" : "",
                            is_transfer_binary && selection_body && (H5S_SEL_POINTS != sel_type) ? selection_body : "")
        ) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

    if (url_len >= URL_MAX_LENGTH)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "dataset write URL size exceeded maximum URL size")

#ifdef RV_PLUGIN_DEBUG
    printf("-> Dataset write URL: %s\n\n", request_url);
#endif

    /* If using a point selection, instruct cURL to perform a POST request in order to post the
     * point list. Otherwise, a PUT request is made to the server.
     */
    if (H5S_SEL_POINTS == sel_type) {
        const char * const fmt_string = "{%s,\"value_base64\": \"%s\"}";
        size_t             value_body_len;
        int                bytes_printed;

        /* Since base64 encoding generally introduces 33% overhead for encoding,
         * go ahead and allocate a buffer 4/3 the size of the given write buffer
         * in order to try and avoid reallocations inside the encoding function.
         */
        value_body_len = ((double) 4.0 / 3.0) * write_body_len;
        if (NULL == (base64_encoded_value = RV_malloc(value_body_len)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL, "can't allocate temporary buffer for base64-encoded write buffer")

        if (RV_base64_encode(buf, write_body_len, &base64_encoded_value, &value_body_len) < 0)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTENCODE, FAIL, "can't base64-encode write buffer")

#ifdef RV_PLUGIN_DEBUG
        printf("-> Base64-encoded data buffer: %s\n\n", base64_encoded_value);
#endif

        write_body_len = (strlen(fmt_string) - 4) + selection_body_len + value_body_len;
        if (NULL == (write_body = RV_malloc(write_body_len + 1)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL, "can't allocate space for write buffer")

        if ((bytes_printed = snprintf(write_body, write_body_len + 1, fmt_string, selection_body, base64_encoded_value)) < 0)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

#ifdef RV_PLUGIN_DEBUG
        printf("-> Write body: %s\n\n", write_body);
#endif

        if (bytes_printed >= write_body_len + 1)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "point selection write buffer exceeded allocated buffer size")

        curl_headers = curl_slist_append(curl_headers, "Content-Type: application/json");

#ifdef RV_PLUGIN_DEBUG
        printf("-> Setup cURL to POST point list for dataset write\n\n");
#endif
    } /* end if */

    uinfo.buffer = is_transfer_binary ? buf : write_body;
    uinfo.buffer_size = write_body_len;

    /* Check to make sure that the size of the write body can safely be cast to a curl_off_t */
    if (sizeof(curl_off_t) < sizeof(size_t))
        ASSIGN_TO_SMALLER_SIZE(write_len, curl_off_t, write_body_len, size_t)
    else if (sizeof(curl_off_t) > sizeof(size_t))
        write_len = (curl_off_t) write_body_len;
    else
        ASSIGN_TO_SAME_SIZE_UNSIGNED_TO_SIGNED(write_len, curl_off_t, write_body_len, size_t)

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_UPLOAD, 1))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP PUT request: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_READDATA, &uinfo))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL PUT data: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, write_len))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL PUT data size: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef RV_PLUGIN_DEBUG
    printf("-> Writing dataset\n\n");

    printf("   /**********************************\\\n");
    printf("-> | Making PUT request to the server |\n");
    printf("   \\**********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_DATASET, H5E_WRITEERROR, FAIL);

done:
#ifdef RV_PLUGIN_DEBUG
    printf("-> Dataset write response buffer:\n%s\n\n", response_buffer.buffer);
#endif

    if (base64_encoded_value)
        RV_free(base64_encoded_value);
    if (host_header)
        RV_free(host_header);
    if (write_body)
        RV_free(write_body);
    if (selection_body)
        RV_free(selection_body);

    /* Unset cURL UPLOAD option to ensure that future requests don't try to use PUT calls */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_UPLOAD, 0))
        FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't unset cURL PUT option: %s", curl_err_buf)

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    PRINT_ERROR_STACK

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
RV_dataset_get(void *obj, H5VL_dataset_get_t get_type, hid_t dxpl_id,
               void **req, va_list arguments)
{
    RV_object_t *dset = (RV_object_t *) obj;
    herr_t       ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received dataset get call with following parameters:\n");
    printf("     - Dataset get call type: %s\n", dataset_get_type_to_string(get_type));
    printf("     - Dataset's URI: %s\n", dset->URI);
    printf("     - Dataset's object type: %s\n", object_type_to_string(dset->obj_type));
    printf("     - Dataset's domain path: %s\n\n", dset->domain->u.file.filepath_name);
#endif

    if (H5I_DATASET != dset->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a dataset")

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
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_UNSUPPORTED, FAIL, "H5Dget_offset is unsupported")

        /* H5Dget_space */
        case H5VL_DATASET_GET_SPACE:
        {
            hid_t *ret_id = va_arg(arguments, hid_t *);

            if ((*ret_id = H5Scopy(dset->u.dataset.space_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't get dataspace of dataset")

            break;
        } /* H5VL_DATASET_GET_SPACE */

        /* H5Dget_space_status */
        case H5VL_DATASET_GET_SPACE_STATUS:
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_UNSUPPORTED, FAIL, "H5Dget_space_status is unsupported")

        /* H5Dget_storage_size */
        case H5VL_DATASET_GET_STORAGE_SIZE:
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_UNSUPPORTED, FAIL, "H5Dget_storage_size is unsupported")

        /* H5Dget_type */
        case H5VL_DATASET_GET_TYPE:
        {
            hid_t *ret_id = va_arg(arguments, hid_t *);

            if ((*ret_id = H5Tcopy(dset->u.dataset.dtype_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCOPY, FAIL, "can't copy dataset's datatype")

            break;
        } /* H5VL_DATASET_GET_TYPE */

        default:
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "can't get this type of information from dataset")
    } /* end switch */

done:
    PRINT_ERROR_STACK

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
                    hid_t dxpl_id, void **req, va_list arguments)
{
    RV_object_t *dset = (RV_object_t *) obj;
    herr_t       ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received dataset-specific call with following parameters:\n");
    printf("     - Dataset-specific call type: %s\n", dataset_specific_type_to_string(specific_type));
    printf("     - Dataset's URI: %s\n", dset->URI);
    printf("     - Dataset's object type: %s\n", object_type_to_string(dset->obj_type));
    printf("     - Dataset's domain path: %s\n\n", dset->domain->u.file.filepath_name);
#endif

    if (H5I_DATASET != dset->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a dataset")

    switch (specific_type) {
        /* H5Dset_extent */
        case H5VL_DATASET_SET_EXTENT:
            /* Check for write access */
            if (!(dset->domain->u.file.intent & H5F_ACC_RDWR))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "no write intent on file")

            FUNC_GOTO_ERROR(H5E_DATASET, H5E_UNSUPPORTED, FAIL, "H5Dset_extent is unsupported")
            break;

        default:
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "unknown dataset operation")
    } /* end switch */

done:
    PRINT_ERROR_STACK

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
RV_dataset_close(void *dset, hid_t dxpl_id, void **req)
{
    RV_object_t *_dset = (RV_object_t *) dset;
    herr_t       ret_value = SUCCEED;

    if (!_dset)
        FUNC_GOTO_DONE(SUCCEED);

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received dataset close call with following parameters:\n");
    printf("     - Dataset's URI: %s\n", _dset->URI);
    printf("     - Dataset's object type: %s\n", object_type_to_string(_dset->obj_type));
    if (_dset->domain && _dset->domain->u.file.filepath_name)
        printf("     - Dataset's domain path: %s\n", _dset->domain->u.file.filepath_name);
    printf("\n");
#endif

    if (H5I_DATASET != _dset->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a dataset")

    if (_dset->u.dataset.dtype_id >= 0 && H5Tclose(_dset->u.dataset.dtype_id) < 0)
        FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, FAIL, "can't close dataset's datatype")
    if (_dset->u.dataset.space_id >= 0 && H5Sclose(_dset->u.dataset.space_id) < 0)
        FUNC_DONE_ERROR(H5E_DATASPACE, H5E_CANTCLOSEOBJ, FAIL, "can't close dataset's dataspace")

    if (_dset->u.dataset.dapl_id >= 0) {
        if (_dset->u.dataset.dapl_id != H5P_DATASET_ACCESS_DEFAULT && H5Pclose(_dset->u.dataset.dapl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close DAPL")
    } /* end if */
    if (_dset->u.dataset.dcpl_id >= 0) {
        if (_dset->u.dataset.dcpl_id != H5P_DATASET_CREATE_DEFAULT && H5Pclose(_dset->u.dataset.dcpl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close DCPL")
    } /* end if */

    _dset = RV_free(_dset);

done:
    PRINT_ERROR_STACK

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
               hid_t dxpl_id, void **req)
{
    RV_object_t *new_file = NULL;
    size_t       name_length;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    void        *ret_value = NULL;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received file create call with following parameters:\n");
    printf("     - Filename: %s\n", name);
    printf("     - Creation flags: %s\n", file_flags_to_string(flags));
    printf("     - Default FCPL? %s\n", (H5P_FILE_CREATE_DEFAULT == fcpl_id) ? "yes" : "no");
    printf("     - Default FAPL? %s\n\n", (H5P_FILE_ACCESS_DEFAULT == fapl_id) ? "yes" : "no");
#endif

    /* Allocate and setup internal File struct */
    if (NULL == (new_file = (RV_object_t *) RV_malloc(sizeof(*new_file))))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate space for file object")

    new_file->URI[0] = '\0';
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

    /* Setup the host header */
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

#ifdef RV_PLUGIN_DEBUG
        printf("-> H5F_ACC_TRUNC specified; checking if file exists\n\n");

        printf("   /**********************************\\\n");
        printf("-> | Making GET request to the server |\n");
        printf("   \\**********************************/\n\n");
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
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE"))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set up cURL to make HTTP DELETE request: %s", curl_err_buf)

#ifdef RV_PLUGIN_DEBUG
            printf("-> File existed and H5F_ACC_TRUNC specified; deleting file\n\n");

            printf("   /*************************************\\\n");
            printf("-> | Making DELETE request to the server |\n");
            printf("   \\*************************************/\n\n");
#endif

            CURL_PERFORM(curl, H5E_FILE, H5E_CANTREMOVE, NULL);

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't reset cURL custom request: %s", curl_err_buf)
        } /* end if */
    } /* end if */

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_UPLOAD, 1))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set up cURL to make HTTP PUT request: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_READDATA, NULL))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL PUT data: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, 0))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL PUT data size: %s", curl_err_buf)

#ifdef RV_PLUGIN_DEBUG
    printf("-> Creating file\n\n");

    printf("   /**********************************\\\n");
    printf("-> | Making PUT request to the server |\n");
    printf("   \\**********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_FILE, H5E_CANTCREATE, NULL);

#ifdef RV_PLUGIN_DEBUG
    printf("-> Created file\n\n");
#endif

    /* Store the newly-created file's URI */
    if (RV_parse_response(response_buffer.buffer, NULL, new_file->URI, RV_copy_object_URI_callback) < 0)
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTCREATE, NULL, "can't parse new file's URI")

    ret_value = (void *) new_file;

done:
#ifdef RV_PLUGIN_DEBUG
    printf("-> File create response buffer:\n%s\n\n", response_buffer.buffer);

    if (new_file && ret_value) {
        printf("-> New file's info:\n");
        printf("     - New file's pathname: %s\n", new_file->domain->u.file.filepath_name);
        printf("     - New file's URI: %s\n", new_file->URI);
        printf("     - New file's object type: %s\n\n", object_type_to_string(new_file->obj_type));
    } /* end if */
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

    /* Unset cURL UPLOAD option to ensure that future requests don't try to use PUT calls */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_UPLOAD, 0))
        FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTSET, NULL, "can't unset cURL PUT option: %s", curl_err_buf)

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    PRINT_ERROR_STACK

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
RV_file_open(const char *name, unsigned flags, hid_t fapl_id, hid_t dxpl_id, void **req)
{
    RV_object_t *file = NULL;
    size_t       name_length;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    void        *ret_value = NULL;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received file open call with following parameters:\n");
    printf("     - Filename: %s\n", name);
    printf("     - File access flags: %s\n", file_flags_to_string(flags));
    printf("     - Default FAPL? %s\n\n", (H5P_FILE_ACCESS_DEFAULT == fapl_id) ? "yes" : "no");
#endif

    /* Allocate and setup internal File struct */
    if (NULL == (file = (RV_object_t *) RV_malloc(sizeof(*file))))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate space for file object")

    file->URI[0] = '\0';
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

    /* Setup the host header */
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

#ifdef RV_PLUGIN_DEBUG
    printf("-> Retrieving info for file open\n\n");

    printf("   /**********************************\\\n");
    printf("-> | Making GET request to the server |\n");
    printf("   \\**********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_FILE, H5E_CANTOPENFILE, NULL);

    /* Store the opened file's URI */
    if (RV_parse_response(response_buffer.buffer, NULL, file->URI, RV_copy_object_URI_callback) < 0)
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, NULL, "can't parse file's URI")

    /* Copy the FAPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * H5Fget_access_plist() will function correctly
     */
    /* XXX: Set any properties necessary */
    if (H5P_FILE_ACCESS_DEFAULT != fapl_id) {
        if ((file->u.file.fapl_id = H5Pcopy(fapl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy FAPL")
    } /* end if */
    else
        file->u.file.fapl_id = H5P_FILE_ACCESS_DEFAULT;

    /* Set up a FCPL for the file so that H5Fget_create_plist() will function correctly */
    if ((file->u.file.fcpl_id = H5Pcreate(H5P_FILE_CREATE)) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "can't create FCPL for file")

    ret_value = (void *) file;

done:
#ifdef RV_PLUGIN_DEBUG
    printf("-> File open response buffer:\n%s\n\n", response_buffer.buffer);

    if (file && ret_value) {
        printf("-> File's info:\n");
        printf("     - File's URI: %s\n", file->URI);
        printf("     - File's object type: %s\n", object_type_to_string(file->obj_type));
        printf("     - File's pathname: %s\n\n", file->domain->u.file.filepath_name);
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

    PRINT_ERROR_STACK

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
RV_file_get(void *obj, H5VL_file_get_t get_type, hid_t dxpl_id, void **req, va_list arguments)
{
    RV_object_t *_obj = (RV_object_t *) obj;
    herr_t       ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received file get call with following parameters:\n");
    printf("     - File get call type: %s\n", file_get_type_to_string(get_type));
    printf("     - File's URI: %s\n", _obj->URI);
    printf("     - File's pathname: %s\n\n", _obj->domain->u.file.filepath_name);
#endif

    if (H5I_FILE != _obj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a file")

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

            /* Shut up compiler warnings */
            UNUSED_VAR(obj_type);

            *ret_size = (ssize_t) strlen(_obj->domain->u.file.filepath_name);

            if (name_buf) {
                strncpy(name_buf, _obj->u.file.filepath_name, name_buf_size - 1);
                name_buf[name_buf_size - 1] = '\0';
            } /* end if */

            break;
        } /* H5VL_FILE_GET_NAME */

        /* H5Fget_obj_count */
        case H5VL_FILE_GET_OBJ_COUNT:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fget_obj_count is unsupported")

        /* H5Fget_obj_ids */
        case H5VL_FILE_GET_OBJ_IDS:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fget_obj_ids is unsupported")

        case H5VL_OBJECT_GET_FILE:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "get file is unsupported")

        default:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTGET, FAIL, "can't get this type of information from file")
    } /* end switch */

done:
    PRINT_ERROR_STACK

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
RV_file_specific(void *obj, H5VL_file_specific_t specific_type, hid_t dxpl_id,
                 void **req, va_list arguments)
{
    RV_object_t *file = (RV_object_t *) obj;
    herr_t       ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received file-specific call with following parameters:\n");
    printf("     - File-specific call type: %s\n", file_specific_type_to_string(specific_type));
    if (file) {
        printf("     - File's URI: %s\n", file->URI);
        printf("     - File's pathname: %s\n", file->domain->u.file.filepath_name);
    } /* end if */
    printf("\n");
#endif

    if (file && H5I_FILE != file->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a file")

    switch (specific_type) {
        /* H5Fflush */
        case H5VL_FILE_FLUSH:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fflush is unsupported")

        /* H5Fis_accessible */
        case H5VL_FILE_IS_ACCESSIBLE:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fis_accessible is unsupported")

        /* H5Fmount */
        case H5VL_FILE_MOUNT:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fmount is unsupported")

        /* H5Funmount */
        case H5VL_FILE_UNMOUNT:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Funmount is unsupported")

        default:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "unknown file operation")
    } /* end switch */

done:
    PRINT_ERROR_STACK

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
RV_file_optional(void *obj, hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_file_optional_t  optional_type = (H5VL_file_optional_t) va_arg(arguments, int);
    RV_object_t          *file = (RV_object_t *) obj;
    herr_t                ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received file optional call with following parameters:\n");
    printf("     - File optional call type: %s\n", file_optional_type_to_string(optional_type));
    printf("     - File's URI: %s\n", file->URI);
    printf("     - File's pathname: %s\n\n", file->domain->u.file.filepath_name);
#endif

    if (H5I_FILE != file->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a file")

    switch (optional_type) {
        /* H5Freopen */
        case H5VL_FILE_REOPEN:
        {
            void **ret_file = va_arg(arguments, void **);

            if (NULL == (*ret_file = RV_file_open(file->u.file.filepath_name, file->u.file.intent, file->u.file.fapl_id, dxpl_id, NULL)))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTOPENOBJ, FAIL, "can't re-open file")

            break;
        } /* H5VL_FILE_REOPEN */

        /* H5Fget_info */
        case H5VL_FILE_GET_INFO:
        {
            H5I_type_t   obj_type = va_arg(arguments, H5I_type_t);
            H5F_info2_t *file_info = va_arg(arguments, H5F_info2_t *);

            /* Shut up compiler warnings */
            UNUSED_VAR(obj_type);

            /* Initialize entire struct to 0 */
            memset(file_info, 0, sizeof(*file_info));

            break;
        } /* H5VL_FILE_GET_INFO */

        /* H5Fclear_elink_file_cache */
        case H5VL_FILE_CLEAR_ELINK_CACHE:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fclear_elink_file_cache is unsupported")

        /* H5Fget_file_image */
        case H5VL_FILE_GET_FILE_IMAGE:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fget_file_image is unsupported")

        /* H5Fget_free_sections */
        case H5VL_FILE_GET_FREE_SECTIONS:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fget_free_sections is unsupported")

        /* H5Fget_freespace */
        case H5VL_FILE_GET_FREE_SPACE:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fget_freespace is unsupported")

        /* H5Fget_mdc_config */
        case H5VL_FILE_GET_MDC_CONF:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fget_mdc_config is unsupported")

        /* H5Fget_mdc_hit_rate */
        case H5VL_FILE_GET_MDC_HR:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fget_mdc_hit_rate is unsupported")

        /* H5Fget_mdc_size */
        case H5VL_FILE_GET_MDC_SIZE:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fget_mdc_size is unsupported")

        /* H5Fget_filesize */
        case H5VL_FILE_GET_SIZE:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fget_filesize is unsupported")

        /* H5Fget_vfd_handle */
        case H5VL_FILE_GET_VFD_HANDLE:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fget_vfd_handle is unsupported")

        /* H5Freset_mdc_hit_rate_stats */
        case H5VL_FILE_RESET_MDC_HIT_RATE:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Freset_mdc_hit_rate_stats is unsupported")

        /* H5Fset_mdc_config */
        case H5VL_FILE_SET_MDC_CONFIG:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fset_mdc_config is unsupported")

        default:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "unknown file operation")
    } /* end switch */

done:
    PRINT_ERROR_STACK

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
RV_file_close(void *file, hid_t dxpl_id, void **req)
{
    RV_object_t *_file = (RV_object_t *) file;
    herr_t       ret_value = SUCCEED;

    if (!_file)
        FUNC_GOTO_DONE(SUCCEED);

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received file close call with following parameters:\n");
    printf("     - File's URI: %s\n", _file->URI);
    printf("     - File's object type: %s\n", object_type_to_string(_file->obj_type));
    if (_file->domain && _file->domain->u.file.filepath_name)
        printf("     - Filename: %s\n", _file->domain->u.file.filepath_name);
    printf("\n");
#endif

    if (H5I_FILE != _file->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a file")

    if (_file->u.file.filepath_name)
        _file->u.file.filepath_name = RV_free(_file->u.file.filepath_name);

    if (_file->u.file.fapl_id >= 0) {
        if (_file->u.file.fapl_id != H5P_FILE_ACCESS_DEFAULT && H5Pclose(_file->u.file.fapl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close FAPL")
    } /* end if */
    if (_file->u.file.fcpl_id >= 0) {
        if (_file->u.file.fcpl_id != H5P_FILE_CREATE_DEFAULT && H5Pclose(_file->u.file.fcpl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close FCPL")
    } /* end if */

    _file = RV_free(_file);

done:
    PRINT_ERROR_STACK

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
 */
static void *
RV_group_create(void *obj, H5VL_loc_params_t loc_params, const char *name, hid_t gcpl_id,
                hid_t gapl_id, hid_t dxpl_id, void **req)
{
    RV_object_t *parent = (RV_object_t *) obj;
    RV_object_t *new_group = NULL;
    size_t       create_request_nalloc = 0;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    char        *create_request_body = NULL;
    char        *path_dirname = NULL;
    char         target_URI[URI_MAX_LENGTH];
    char         request_url[URL_MAX_LENGTH];
    int          create_request_body_len = 0;
    int          url_len = 0;
    void        *ret_value = NULL;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received group create call with following parameters:\n");
    printf("     - H5Gcreate variant: %s\n", name ? "H5Gcreate2" : "H5Gcreate_anon");
    if (name) printf("     - Group's name: %s\n", name);
    printf("     - Group parent object's URI: %s\n", parent->URI);
    printf("     - Group parent object's type: %s\n", object_type_to_string(parent->obj_type));
    printf("     - Group parent object's domain path: %s\n", parent->domain->u.file.filepath_name);
    printf("     - Default GCPL? %s\n", (H5P_GROUP_CREATE_DEFAULT == gcpl_id) ? "yes" : "no");
    printf("     - Default GAPL? %s\n\n", (H5P_GROUP_ACCESS_DEFAULT == gapl_id) ? "yes" : "no");
#endif

    if (H5I_FILE != parent->obj_type && H5I_GROUP != parent->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object not a file or group")

    /* Check for write access */
    if (!(parent->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, NULL, "no write intent on file")

    /* Allocate and setup internal Group struct */
    if (NULL == (new_group = (RV_object_t *) RV_malloc(sizeof(*new_group))))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTALLOC, NULL, "can't allocate space for group object")

    new_group->URI[0] = '\0';
    new_group->obj_type = H5I_GROUP;
    new_group->u.group.gapl_id = FAIL;
    new_group->u.group.gcpl_id = FAIL;
    new_group->domain = parent->domain; /* Store pointer to file that the newly-created group is within */

    /* Copy the GAPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * group access property list functions will function correctly
     */
    if (H5P_GROUP_ACCESS_DEFAULT != gapl_id) {
        if ((new_group->u.group.gapl_id = H5Pcopy(gapl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy GAPL")
    } /* end if */
    else
        new_group->u.group.gapl_id = H5P_GROUP_ACCESS_DEFAULT;

    /* Copy the GCPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * H5Gget_create_plist() will function correctly
     */
    if (H5P_GROUP_CREATE_DEFAULT != gcpl_id) {
        if ((new_group->u.group.gcpl_id = H5Pcopy(gcpl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy GCPL")
    } /* end if */
    else
        new_group->u.group.gcpl_id = H5P_GROUP_CREATE_DEFAULT;

    /* If this is not a H5Gcreate_anon call, create a link for the Group
     * to link it into the file structure
     */
    if (name) {
        const char *path_basename = RV_basename(name);
        hbool_t     empty_dirname;

#ifdef RV_PLUGIN_DEBUG
        printf("-> Creating JSON link for group\n\n");
#endif

        /* In case the user specified a path which contains multiple groups on the way to the
         * one which this group will ultimately be linked under, extract out the path to the
         * final group in the chain */
        if (NULL == (path_dirname = RV_dirname(name)))
            FUNC_GOTO_ERROR(H5E_SYM, H5E_BADVALUE, NULL, "invalid pathname for group link")
        empty_dirname = !strcmp(path_dirname, "");

        /* If the path to the final group in the chain wasn't empty, get the URI of the final
         * group in order to correctly link this group into the file structure. Otherwise,
         * the supplied parent group is the one housing this group, so just use its URI.
         */
        if (!empty_dirname) {
            H5I_type_t obj_type = H5I_GROUP;
            htri_t     search_ret;

            search_ret = RV_find_object_by_path(parent, path_dirname, &obj_type, RV_copy_object_URI_callback, NULL, target_URI);
            if (!search_ret || search_ret < 0)
                FUNC_GOTO_ERROR(H5E_SYM, H5E_PATH, NULL, "can't locate target for group link")
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
                    empty_dirname ? parent->URI : target_URI, path_basename)
                ) < 0)
                FUNC_GOTO_ERROR(H5E_SYM, H5E_SYSERRSTR, NULL, "snprintf error")

            if ((size_t) create_request_body_len >= create_request_nalloc)
                FUNC_GOTO_ERROR(H5E_SYM, H5E_SYSERRSTR, NULL, "group link create request body size exceeded allocated buffer size")
        }

#ifdef RV_PLUGIN_DEBUG
        printf("-> Group create request body:\n%s\n\n", create_request_body);
#endif
    } /* end if */

    /* Setup the host header */
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
    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups", base_URL)) < 0)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_SYSERRSTR, NULL, "snprintf error")

    if (url_len >= URL_MAX_LENGTH)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_SYSERRSTR, NULL, "group create URL size exceeded maximum URL size")

#ifdef RV_PLUGIN_DEBUG
    printf("-> Group create request URL: %s\n\n", request_url);
#endif

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, NULL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POST, 1))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, NULL, "can't set up cURL to make HTTP POST request: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDS, create_request_body ? create_request_body : ""))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, NULL, "can't set cURL POST data: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t) create_request_body_len))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, NULL, "can't set cURL POST data size: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, NULL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef RV_PLUGIN_DEBUG
    printf("-> Creating group\n\n");

    printf("   /***********************************\\\n");
    printf("-> | Making POST request to the server |\n");
    printf("   \\***********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_SYM, H5E_CANTCREATE, NULL);

#ifdef RV_PLUGIN_DEBUG
    printf("-> Created group\n\n");
#endif

    /* Store the newly-created group's URI */
    if (RV_parse_response(response_buffer.buffer, NULL, new_group->URI, RV_copy_object_URI_callback) < 0)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTCREATE, NULL, "can't parse new group's URI")

    ret_value = (void *) new_group;

done:
#ifdef RV_PLUGIN_DEBUG
    printf("-> Group create response buffer:\n%s\n\n", response_buffer.buffer);

    if (new_group && ret_value) {
        printf("-> New group's info:\n");
        printf("     - New group's URI: %s\n", new_group->URI);
        printf("     - New group's object type: %s\n", object_type_to_string(new_group->obj_type));
        printf("     - New group's domain path: %s\n\n", new_group->domain->u.file.filepath_name);
    } /* end if */
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

    PRINT_ERROR_STACK

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
RV_group_open(void *obj, H5VL_loc_params_t loc_params, const char *name,
              hid_t gapl_id, hid_t dxpl_id, void **req)
{
    RV_object_t *parent = (RV_object_t *) obj;
    RV_object_t *group = NULL;
    H5I_type_t   obj_type = H5I_UNINIT;
    htri_t       search_ret;
    void        *ret_value = NULL;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received group open call with following parameters:\n");
    printf("     - loc_id object's URI: %s\n", parent->URI);
    printf("     - loc_id object's type: %s\n", object_type_to_string(parent->obj_type));
    printf("     - loc_id object's domain path: %s\n", parent->domain->u.file.filepath_name);
    printf("     - Path to group: %s\n", name ? name : "(null)");
    printf("     - Default GAPL? %s\n\n", (H5P_GROUP_ACCESS_DEFAULT == gapl_id) ? "yes" : "no");
#endif

    if (H5I_FILE != parent->obj_type && H5I_GROUP != parent->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object not a file or group")

    /* Allocate and setup internal Group struct */
    if (NULL == (group = (RV_object_t *) RV_malloc(sizeof(*group))))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTALLOC, NULL, "can't allocate space for group object")

    group->URI[0] = '\0';
    group->obj_type = H5I_GROUP;
    group->u.group.gapl_id = FAIL;
    group->u.group.gcpl_id = FAIL;
    group->domain = parent->domain; /* Store pointer to file that the opened Group is within */

    /* Locate the Group */
    search_ret = RV_find_object_by_path(parent, name, &obj_type, RV_copy_object_URI_callback, NULL, group->URI);
    if (!search_ret || search_ret < 0)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_PATH, NULL, "can't locate group by path")

#ifdef RV_PLUGIN_DEBUG
    printf("-> Found group by given path\n\n");
#endif

    /* Copy the GAPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * group access property list functions will function correctly
     */
    if (H5P_GROUP_ACCESS_DEFAULT != gapl_id) {
        if ((group->u.group.gapl_id = H5Pcopy(gapl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy GAPL")
    } /* end if */
    else
        group->u.group.gapl_id = H5P_GROUP_ACCESS_DEFAULT;

    /* Set up a GCPL for the group so that H5Gget_create_plist() will function correctly */
    /* XXX: Set any properties necessary */
    if ((group->u.group.gcpl_id = H5Pcreate(H5P_GROUP_CREATE)) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "can't create GCPL for group")

    ret_value = (void *) group;

done:
#ifdef RV_PLUGIN_DEBUG
    printf("-> Group open response buffer:\n%s\n\n", response_buffer.buffer);

    if (group && ret_value) {
        printf("-> Group's info:\n");
        printf("     - Group's URI: %s\n", group->URI);
        printf("     - Group's object type: %s\n", object_type_to_string(group->obj_type));
        printf("     - Group's domain path: %s\n\n", group->domain->u.file.filepath_name);
    } /* end if */
#endif

    /* Clean up allocated file object if there was an issue */
    if (group && !ret_value)
        if (RV_group_close(group, FAIL, NULL) < 0)
            FUNC_DONE_ERROR(H5E_SYM, H5E_CANTCLOSEOBJ, NULL, "can't close group")

    PRINT_ERROR_STACK

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
RV_group_get(void *obj, H5VL_group_get_t get_type, hid_t dxpl_id, void **req, va_list arguments)
{
    RV_object_t *loc_obj = (RV_object_t *) obj;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    char         request_url[URL_MAX_LENGTH];
    int          url_len = 0;
    herr_t       ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received group get call with following parameters:\n");
    printf("     - Group get call type: %s\n\n", group_get_type_to_string(get_type));
#endif

    if (H5I_FILE != loc_obj->obj_type && H5I_GROUP != loc_obj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a group")

    switch (get_type) {
        /* H5Gget_create_plist */
        case H5VL_GROUP_GET_GCPL:
        {
            hid_t *ret_id = va_arg(arguments, hid_t *);

            if ((*ret_id = H5Pcopy(loc_obj->u.group.gcpl_id)) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, FAIL, "can't get group's GCPL")

            break;
        } /* H5VL_GROUP_GET_GCPL */

        /* H5Gget_info */
        case H5VL_GROUP_GET_INFO:
        {
            H5VL_loc_params_t  loc_params = va_arg(arguments, H5VL_loc_params_t);
            H5G_info_t        *group_info = va_arg(arguments, H5G_info_t *);

            switch (loc_params.type) {
                /* H5Gget_info */
                case H5VL_OBJECT_BY_SELF:
                {
#ifdef RV_PLUGIN_DEBUG
                    printf("-> H5Gget_info(): Group's URI: %s\n", loc_obj->URI);
                    printf("-> H5Gget_info(): Group's object type: %s\n\n", object_type_to_string(loc_obj->obj_type));
#endif

                    /* Redirect cURL from the base URL to "/groups/<id>" to get information about the group */
                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s", base_URL, loc_obj->URI)) < 0)
                        FUNC_GOTO_ERROR(H5E_SYM, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_SYM, H5E_SYSERRSTR, FAIL, "H5Gget_info request URL size exceeded maximum URL size")

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Gget_info_by_name */
                case H5VL_OBJECT_BY_NAME:
                {
                    H5I_type_t obj_type = H5I_GROUP;
                    htri_t     search_ret;
                    char       temp_URI[URI_MAX_LENGTH];

#ifdef RV_PLUGIN_DEBUG
                    printf("-> H5Gget_info_by_name(): loc_id object's URI: %s\n", loc_obj->URI);
                    printf("-> H5Gget_info_by_name(): loc_id object's type: %s\n", object_type_to_string(loc_obj->obj_type));
                    printf("-> H5Gget_info_by_name(): Path to group's parent object: %s\n\n", loc_params.loc_data.loc_by_name.name);
#endif

                    search_ret = RV_find_object_by_path(loc_obj, loc_params.loc_data.loc_by_name.name, &obj_type,
                            RV_copy_object_URI_callback, NULL, temp_URI);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_SYM, H5E_PATH, FAIL, "can't locate group")

#ifdef RV_PLUGIN_DEBUG
                    printf("-> H5Gget_info_by_name(): found group's parent object by given path\n");
                    printf("-> H5Gget_info_by_name(): group's parent object URI: %s\n", temp_URI);
                    printf("-> H5Gget_info_by_name(): group's parent object type: %s\n\n", object_type_to_string(obj_type));
#endif

                    /* Redirect cURL from the base URL to "/groups/<id>" to get information about the group */
                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s", base_URL, temp_URI)) < 0)
                        FUNC_GOTO_ERROR(H5E_SYM, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_SYM, H5E_SYSERRSTR, FAIL, "H5Gget_info_by_name request URL size exceeded maximum URL size")

                    break;
                } /* H5VL_OBJECT_BY_NAME */

                /* H5Gget_info_by_idx */
                case H5VL_OBJECT_BY_IDX:
                {
                    FUNC_GOTO_ERROR(H5E_SYM, H5E_UNSUPPORTED, FAIL, "H5Gget_info_by_idx is unsupported")
                    break;
                } /* H5VL_OBJECT_BY_IDX */

                case H5VL_OBJECT_BY_ADDR:
                case H5VL_OBJECT_BY_REF:
                default:
                    FUNC_GOTO_ERROR(H5E_SYM, H5E_BADVALUE, FAIL, "invalid loc_params type")
            } /* end switch */

            /* Setup the host header */
            host_header_len = strlen(loc_obj->domain->u.file.filepath_name) + strlen(host_string) + 1;
            if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
                FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

            strcpy(host_header, host_string);

            curl_headers = curl_slist_append(curl_headers, strncat(host_header, loc_obj->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

            /* Disable use of Expect: 100 Continue HTTP response */
            curl_headers = curl_slist_append(curl_headers, "Expect:");

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
                FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
                FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP GET request: %s", curl_err_buf)
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
                FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef RV_PLUGIN_DEBUG
            printf("-> Retrieving group info at URL: %s\n\n", request_url);

            printf("   /**********************************\\\n");
            printf("-> | Making GET request to the server |\n");
            printf("   \\**********************************/\n\n");
#endif

            /* Make request to server to retrieve the group info */
            CURL_PERFORM(curl, H5E_SYM, H5E_CANTGET, FAIL);

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
#ifdef RV_PLUGIN_DEBUG
    printf("-> Group get response buffer:\n%s\n\n", response_buffer.buffer);
#endif

    if (host_header)
        RV_free(host_header);

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    PRINT_ERROR_STACK

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
RV_group_close(void *grp, hid_t dxpl_id, void **req)
{
    RV_object_t *_grp = (RV_object_t *) grp;
    herr_t       ret_value = SUCCEED;

    if (!_grp)
        FUNC_GOTO_DONE(SUCCEED);

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received group close call with following parameters:\n");
    printf("     - Group's URI: %s\n", _grp->URI);
    printf("     - Group's object type: %s\n", object_type_to_string(_grp->obj_type));
    if (_grp->domain && _grp->domain->u.file.filepath_name)
        printf("     - Group's domain path: %s\n", _grp->domain->u.file.filepath_name);
    printf("\n");
#endif

    if (H5I_GROUP != _grp->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a group")

    if (_grp->u.group.gapl_id >= 0) {
        if (_grp->u.group.gapl_id != H5P_GROUP_ACCESS_DEFAULT && H5Pclose(_grp->u.group.gapl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close GAPL")
    } /* end if */
    if (_grp->u.group.gcpl_id >= 0) {
        if (_grp->u.group.gcpl_id != H5P_GROUP_CREATE_DEFAULT && H5Pclose(_grp->u.group.gcpl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close GCPL")
    } /* end if */

    _grp = RV_free(_grp);

done:
    PRINT_ERROR_STACK

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
               hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req)
{
    H5VL_loc_params_t  hard_link_target_obj_loc_params;
    RV_object_t       *new_link_loc_obj = (RV_object_t *) obj;
    upload_info        uinfo;
    size_t             create_request_nalloc = 0;
    size_t             host_header_len = 0;
    void              *hard_link_target_obj;
    char              *host_header = NULL;
    char              *create_request_body = NULL;
    char               request_url[URL_MAX_LENGTH];
    char              *url_encoded_link_name = NULL;
    int                create_request_body_len = 0;
    int                url_len = 0;
    herr_t             ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received link create call with following parameters:\n");
    printf("     - Link Name: %s\n", loc_params.loc_data.loc_by_name.name);
    printf("     - Link Type: %s\n", link_create_type_to_string(create_type));
    if (new_link_loc_obj) {
        printf("     - Link loc_obj's URI: %s\n", new_link_loc_obj->URI);
        printf("     - Link loc_obj's type: %s\n", object_type_to_string(new_link_loc_obj->obj_type));
        printf("     - Link loc_obj's domain path: %s\n", new_link_loc_obj->domain->u.file.filepath_name);
    } /* end if */
    printf("     - Default LCPL? %s\n", (H5P_LINK_CREATE_DEFAULT == lcpl_id) ? "yes" : "no");
    printf("     - Default LAPL? %s\n\n", (H5P_LINK_ACCESS_DEFAULT == lapl_id) ? "yes" : "no");
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
    if (H5I_FILE != new_link_loc_obj->obj_type && H5I_GROUP != new_link_loc_obj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "link location object not a file or group")
    if (!loc_params.loc_data.loc_by_name.name)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "link name data was NULL")

    if (!(new_link_loc_obj->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "no write intent on file")


    switch (create_type) {
        /* H5Lcreate_hard */
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
                /* H5Olink */
                case H5VL_OBJECT_BY_SELF:
                {
                    target_URI = ((RV_object_t *) hard_link_target_obj)->URI;
                    break;
                } /* H5VL_OBJECT_BY_SELF */

                case H5VL_OBJECT_BY_NAME:
                {
                    H5I_type_t obj_type = H5I_UNINIT;

#ifdef RV_PLUGIN_DEBUG
                    printf("-> Locating hard link's target object\n\n");
#endif

                    search_ret = RV_find_object_by_path((RV_object_t *) hard_link_target_obj, hard_link_target_obj_loc_params.loc_data.loc_by_name.name,
                            &obj_type, RV_copy_object_URI_callback, NULL, temp_URI);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_PATH, FAIL, "can't locate link target object")

#ifdef RV_PLUGIN_DEBUG
                    printf("-> Found hard link's target object by given path\n\n");
#endif

                    target_URI = temp_URI;

                    break;
                } /* H5VL_OBJECT_BY_NAME */

                case H5VL_OBJECT_BY_IDX:
                case H5VL_OBJECT_BY_ADDR:
                case H5VL_OBJECT_BY_REF:
                default:
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "invalid loc_params type")
            } /* end switch */

#ifdef RV_PLUGIN_DEBUG
            printf("-> Hard link target object's URI: %s\n\n", target_URI);
#endif

            {
                const char * const fmt_string = "{\"id\": \"%s\"}";

                /* Form the request body to create the Link */
                create_request_nalloc = (strlen(fmt_string) - 2) + strlen(target_URI) + 1;
                if (NULL == (create_request_body = (char *) RV_malloc(create_request_nalloc)))
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate space for link create request body")

                if ((create_request_body_len = snprintf(create_request_body, create_request_nalloc, fmt_string, target_URI)) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")

                if ((size_t) create_request_body_len >= create_request_nalloc)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "link create request body size exceeded allocated buffer size")
            }

#ifdef RV_PLUGIN_DEBUG
            printf("-> Hard link create request JSON:\n%s\n\n", create_request_body);
#endif

            break;
        } /* H5VL_LINK_CREATE_HARD */

        /* H5Lcreate_soft */
        case H5VL_LINK_CREATE_SOFT:
        {
            const char *link_target;

            if (H5Pget(lcpl_id, H5VL_PROP_LINK_TARGET_NAME, &link_target) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get property list value for link's target")

#ifdef RV_PLUGIN_DEBUG
            printf("-> Soft link target: %s\n\n", link_target);
#endif

            {
                const char * const fmt_string = "{\"h5path\": \"%s\"}";

                /* Form the request body to create the Link */
                create_request_nalloc = (strlen(fmt_string) - 2) + strlen(link_target) + 1;
                if (NULL == (create_request_body = (char *) RV_malloc(create_request_nalloc)))
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate space for link create request body")

                if ((create_request_body_len = snprintf(create_request_body, create_request_nalloc, fmt_string, link_target)) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")

                if ((size_t) create_request_body_len >= create_request_nalloc)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "link create request body size exceeded allocated buffer size")
            }

#ifdef RV_PLUGIN_DEBUG
            printf("-> Soft link create request JSON:\n%s\n\n", create_request_body);
#endif

            break;
        } /* H5VL_LINK_CREATE_SOFT */

        /* H5Lcreate_external and H5Lcreate_ud */
        case H5VL_LINK_CREATE_UD:
        {
            H5L_type_t  link_type;
            const char *file_path, *link_target;
            unsigned    elink_flags;
            size_t      elink_buf_size;
            void       *elink_buf;

            if (H5Pget(lcpl_id, H5VL_PROP_LINK_TYPE, &link_type) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get property list value for link's type")

            if (H5L_TYPE_EXTERNAL != link_type)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_UNSUPPORTED, FAIL, "unsupported link type")

            /* Retrieve the buffer containing the external link's information */
            if (H5Pget(lcpl_id, H5VL_PROP_LINK_UDATA_SIZE, &elink_buf_size) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get property list value for external link's information buffer size")
            if (H5Pget(lcpl_id, H5VL_PROP_LINK_UDATA, &elink_buf) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get property list value for external link's information buffer")

            if (H5Lunpack_elink_val(elink_buf, elink_buf_size, &elink_flags, &file_path, &link_target) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't unpack contents of external link buffer")

#ifdef RV_PLUGIN_DEBUG
            printf("-> External link's domain path: %s\n", file_path);
            printf("-> External link's link target: %s\n\n", link_target);
#endif

            {
                const char * const fmt_string = "{\"h5domain\": \"%s\", \"h5path\": \"%s\"}";

                /* Form the request body to create the Link */
                create_request_nalloc = (strlen(fmt_string) - 4) + strlen(file_path) + strlen(link_target) + 1;
                if (NULL == (create_request_body = (char *) RV_malloc(create_request_nalloc)))
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate space for link create request body")

                if ((create_request_body_len = snprintf(create_request_body, create_request_nalloc, fmt_string, file_path, link_target)) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")

                if ((size_t) create_request_body_len >= create_request_nalloc)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "link create request body size exceeded allocated buffer size")
            }

#ifdef RV_PLUGIN_DEBUG
            printf("-> External link create request JSON:\n%s\n\n", create_request_body);
#endif

            break;
        } /* H5VL_LINK_CREATE_UD */

        default:
            FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "Invalid link create type")
    } /* end switch */

    /* Setup the host header */
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
    if ((url_len = snprintf(request_url, URL_MAX_LENGTH,
                            "%s/groups/%s/links/%s",
                            base_URL,
                            new_link_loc_obj->URI,
                            url_encoded_link_name)
        ) < 0)
        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")

    if (url_len >= URL_MAX_LENGTH)
        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "link create URL size exceeded maximum URL size")

#ifdef RV_PLUGIN_DEBUG
    printf("-> Link create request URL: %s\n\n", request_url);
#endif

    uinfo.buffer = create_request_body;
    uinfo.buffer_size = (size_t) create_request_body_len;

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_UPLOAD, 1))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP PUT request: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_READDATA, &uinfo))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL PUT data: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t) create_request_body_len))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL PUT data size: %s", curl_err_buf)
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef RV_PLUGIN_DEBUG
    printf("-> Creating link\n\n");

    printf("   /**********************************\\\n");
    printf("-> | Making PUT request to the server |\n");
    printf("   \\**********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_LINK, H5E_CANTCREATE, FAIL);

#ifdef RV_PLUGIN_DEBUG
    printf("-> Created link\n\n");
#endif

done:
#ifdef RV_PLUGIN_DEBUG
    printf("-> Link create response buffer:\n%s\n\n", response_buffer.buffer);
#endif

    if (create_request_body)
        RV_free(create_request_body);
    if (host_header)
        RV_free(host_header);
    if (url_encoded_link_name)
        curl_free(url_encoded_link_name);

    /* Unset cURL UPLOAD option to ensure that future requests don't try to use PUT calls */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_UPLOAD, 0))
        FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't unset cURL PUT option: %s", curl_err_buf)

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    PRINT_ERROR_STACK

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
             hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req)
{
    herr_t ret_value = SUCCEED;

    FUNC_GOTO_ERROR(H5E_LINK, H5E_UNSUPPORTED, FAIL, "H5Lcopy is unsupported")

done:
    PRINT_ERROR_STACK

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
             hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req)
{
    herr_t ret_value = SUCCEED;

    FUNC_GOTO_ERROR(H5E_LINK, H5E_UNSUPPORTED, FAIL, "H5Lmove is unsupported")

done:
    PRINT_ERROR_STACK

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
            hid_t dxpl_id, void **req, va_list arguments)
{
    RV_object_t *loc_obj = (RV_object_t *) obj;
    hbool_t      empty_dirname;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    char        *link_dir_name = NULL;
    char        *url_encoded_link_name = NULL;
    char         temp_URI[URI_MAX_LENGTH];
    char         request_url[URL_MAX_LENGTH];
    int          url_len = 0;
    herr_t       ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received link get call with following parameters:\n");
    printf("     - Link get call type: %s\n", link_get_type_to_string(get_type));
    printf("     - Link loc_obj's URI: %s\n", loc_obj->URI);
    printf("     - Link loc_obj's object type: %s\n", object_type_to_string(loc_obj->obj_type));
    printf("     - Link loc_obj's domain path: %s\n\n", loc_obj->domain->u.file.filepath_name);
#endif

    switch (get_type) {
        /* H5Lget_info */
        case H5VL_LINK_GET_INFO:
        {
            H5L_info_t *link_info = va_arg(arguments, H5L_info_t *);

            switch (loc_params.type) {
                /* H5Lget_info */
                case H5VL_OBJECT_BY_NAME:
                {
                    /* In case the user specified a path which contains any groups on the way to the
                     * link in question, extract out the path to the final group in the chain */
                    if (NULL == (link_dir_name = RV_dirname(loc_params.loc_data.loc_by_name.name)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't get path dirname")
                    empty_dirname = !strcmp(link_dir_name, "");

                    /* If the path to the final group in the chain wasn't empty, get the URI of the final
                     * group and search for the link in question within that group. Otherwise, the
                     * supplied parent group is the one that should be housing the link, so search from
                     * there.
                     */
                    if (!empty_dirname) {
                        H5I_type_t obj_type = H5I_GROUP;
                        htri_t     search_ret;

                        search_ret = RV_find_object_by_path(loc_obj, link_dir_name, &obj_type,
                                RV_copy_object_URI_callback, NULL, temp_URI);
                        if (!search_ret || search_ret < 0)
                            FUNC_GOTO_ERROR(H5E_SYM, H5E_PATH, FAIL, "can't locate parent group")

#ifdef RV_PLUGIN_DEBUG
                        printf("-> H5Lget_info(): Found link's parent object by given path\n");
                        printf("-> H5Lget_info(): link's parent object URI: %s\n", temp_URI);
                        printf("-> H5Lget_info(): link's parent object type: %s\n\n", object_type_to_string(obj_type));
#endif
                    } /* end if */

                    /* URL-encode the name of the link to ensure that the resulting URL for the get
                     * link info operation doesn't contain any illegal characters
                     */
                    if (NULL == (url_encoded_link_name = curl_easy_escape(curl, RV_basename(loc_params.loc_data.loc_by_name.name), 0)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode link name")

                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH,
                                            "%s/groups/%s/links/%s",
                                            base_URL,
                                            empty_dirname ? loc_obj->URI : temp_URI,
                                            url_encoded_link_name)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "H5Lget_info request URL size exceeded maximum URL size")

                    break;
                } /* H5VL_OBJECT_BY_NAME */

                /* H5Lget_info_by_idx */
                case H5VL_OBJECT_BY_IDX:
                {
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_UNSUPPORTED, FAIL, "H5Lget_info_by_idx is unsupported")
                    break;
                } /* H5VL_OBJECT_BY_IDX */

                case H5VL_OBJECT_BY_SELF:
                case H5VL_OBJECT_BY_ADDR:
                case H5VL_OBJECT_BY_REF:
                default:
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "invalid loc_params type")
            } /* end switch */

            /* Make a GET request to the server to retrieve the number of attributes attached to the object */

            /* Setup the host header */
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

#ifdef RV_PLUGIN_DEBUG
            printf("-> Retrieving link info at URL: %s\n\n", request_url);

            printf("   /**********************************\\\n");
            printf("-> | Making GET request to the server |\n");
            printf("   \\**********************************/\n\n");
#endif

            CURL_PERFORM(curl, H5E_LINK, H5E_CANTGET, FAIL);

            /* Retrieve the link info */
            if (RV_parse_response(response_buffer.buffer, NULL, link_info, RV_get_link_info_callback) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't retrieve link info")

            break;
        } /* H5VL_LINK_GET_INFO */

        /* H5Lget_name_by_idx */
        case H5VL_LINK_GET_NAME:
        {
            FUNC_GOTO_ERROR(H5E_LINK, H5E_UNSUPPORTED, FAIL, "H5Lget_name_by_idx is unsupported")
            break;
        } /* H5VL_LINK_GET_NAME */

        /* H5Lget_val */
        case H5VL_LINK_GET_VAL:
        {
            void   *out_buf = va_arg(arguments, void *);
            size_t  buf_size = va_arg(arguments, size_t);

            switch (loc_params.type) {
                /* H5Lget_val */
                case H5VL_OBJECT_BY_NAME:
                {
                    /* In case the user specified a path which contains any groups on the way to the
                     * link in question, extract out the path to the final group in the chain */
                    if (NULL == (link_dir_name = RV_dirname(loc_params.loc_data.loc_by_name.name)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't get path dirname")
                    empty_dirname = !strcmp(link_dir_name, "");

                    /* If the path to the final group in the chain wasn't empty, get the URI of the final
                     * group and search for the link in question within that group. Otherwise, the
                     * supplied parent group is the one that should be housing the link, so search from
                     * there.
                     */
                    if (!empty_dirname) {
                        H5I_type_t obj_type = H5I_GROUP;
                        htri_t     search_ret;

                        search_ret = RV_find_object_by_path(loc_obj, link_dir_name, &obj_type,
                                RV_copy_object_URI_callback, NULL, temp_URI);
                        if (!search_ret || search_ret < 0)
                            FUNC_GOTO_ERROR(H5E_SYM, H5E_PATH, FAIL, "can't locate parent group")

#ifdef RV_PLUGIN_DEBUG
                        printf("-> H5Lget_val(): Found link's parent object by given path\n");
                        printf("-> H5Lget_val(): link's parent object URI: %s\n", temp_URI);
                        printf("-> H5Lget_val(): link's parent object type: %s\n\n", object_type_to_string(obj_type));
#endif
                    } /* end if */

                    /* URL-encode the name of the link to ensure that the resulting URL for the get
                     * link value operation doesn't contain any illegal characters
                     */
                    if (NULL == (url_encoded_link_name = curl_easy_escape(curl, RV_basename(loc_params.loc_data.loc_by_name.name), 0)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode link name")

                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH,
                                            "%s/groups/%s/links/%s",
                                            base_URL,
                                            empty_dirname ? loc_obj->URI : temp_URI,
                                            url_encoded_link_name)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "H5Lget_val request URL size exceeded maximum URL size")

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Lget_val_by_idx */
                case H5VL_OBJECT_BY_IDX:
                {
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_UNSUPPORTED, FAIL, "H5Lget_val_by_idx is unsupported")
                    break;
                } /* H5VL_OBJECT_BY_IDX */

                case H5VL_OBJECT_BY_SELF:
                case H5VL_OBJECT_BY_ADDR:
                case H5VL_OBJECT_BY_REF:
                default:
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "invalid loc_params type")
            } /* end switch */

            /* Make a GET request to the server to retrieve the number of attributes attached to the object */

            /* Setup the host header */
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

#ifdef RV_PLUGIN_DEBUG
            printf("-> Retrieving link value from URL: %s\n\n", request_url);

            printf("   /**********************************\\\n");
            printf("-> | Making GET request to the server |\n");
            printf("   \\**********************************/\n\n");
#endif

            CURL_PERFORM(curl, H5E_LINK, H5E_CANTGET, FAIL);

            /* Retrieve the link value */
            if (RV_parse_response(response_buffer.buffer, &buf_size, out_buf, RV_get_link_val_callback) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't retrieve link value")

            break;
        } /* H5VL_LINK_GET_VAL */

        default:
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't get this type of information from link")
    } /* end switch */

done:
#ifdef RV_PLUGIN_DEBUG
    printf("-> Link get response buffer:\n%s\n\n", response_buffer.buffer);
#endif

    if (host_header)
        RV_free(host_header);
    if (url_encoded_link_name)
        curl_free(url_encoded_link_name);
    if (link_dir_name)
        RV_free(link_dir_name);

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    PRINT_ERROR_STACK

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
                 hid_t dxpl_id, void **req, va_list arguments)
{
    RV_object_t *loc_obj = (RV_object_t *) obj;
    hbool_t      empty_dirname;
    size_t       host_header_len = 0;
    hid_t        link_iter_group_id = -1;
    void        *link_iter_group_object = NULL;
    char        *host_header = NULL;
    char        *link_path_dirname = NULL;
    char         temp_URI[URI_MAX_LENGTH];
    char         request_url[URL_MAX_LENGTH];
    char        *url_encoded_link_name = NULL;
    int          url_len = 0;
    herr_t       ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received link-specific call with following parameters:\n");
    printf("     - Link-specific call type: %s\n", link_specific_type_to_string(specific_type));
    printf("     - Link loc_obj's URI: %s\n", loc_obj->URI);
    printf("     - Link loc_obj's object type: %s\n", object_type_to_string(loc_obj->obj_type));
    printf("     - Link loc_obj's domain path: %s\n\n", loc_obj->domain->u.file.filepath_name);
#endif

    if (H5I_FILE != loc_obj->obj_type && H5I_GROUP != loc_obj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "parent object not a file or group")

    switch (specific_type) {
        /* H5Ldelete */
        case H5VL_LINK_DELETE:
        {
            switch (loc_params.type) {
                /* H5Ldelete */
                case H5VL_OBJECT_BY_NAME:
                {
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

                        search_ret = RV_find_object_by_path(loc_obj, link_path_dirname, &obj_type,
                                RV_copy_object_URI_callback, NULL, temp_URI);
                        if (!search_ret || search_ret < 0)
                            FUNC_GOTO_ERROR(H5E_LINK, H5E_PATH, FAIL, "can't locate parent group for link")
                    } /* end if */

                    /* URL-encode the link name so that the resulting URL for the link delete
                     * operation doesn't contain any illegal characters
                     */
                    if (NULL == (url_encoded_link_name = curl_easy_escape(curl, RV_basename(loc_params.loc_data.loc_by_name.name), 0)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode link name")

                    /* Redirect cURL from the base URL to "/groups/<id>/links/<name>" to delete link */
                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH,
                             "%s/groups/%s/links/%s",
                             base_URL,
                             empty_dirname ? loc_obj->URI : temp_URI,
                             url_encoded_link_name)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "H5Ldelete request URL size exceeded maximum URL size")

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Ldelete_by_idx */
                case H5VL_OBJECT_BY_IDX:
                {
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_UNSUPPORTED, FAIL, "H5Ldelete_by_idx is unsupported")
                    break;
                } /* H5VL_OBJECT_BY_IDX */

                case H5VL_OBJECT_BY_SELF:
                case H5VL_OBJECT_BY_ADDR:
                case H5VL_OBJECT_BY_REF:
                default:
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "invalid loc_params type")
            } /* end switch */

            /* Setup cURL to make the DELETE request */

            /* Setup the host header */
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

#ifdef RV_PLUGIN_DEBUG
            printf("-> Deleting link using URL: %s\n\n", request_url);

            printf("   /*************************************\\\n");
            printf("-> | Making DELETE request to the server |\n");
            printf("   \\*************************************/\n\n");
#endif

            CURL_PERFORM(curl, H5E_LINK, H5E_CANTREMOVE, FAIL);

            break;
        } /* H5VL_LINK_DELETE */

        /* H5Lexists */
        case H5VL_LINK_EXISTS:
        {
            htri_t *ret = va_arg(arguments, htri_t *);
            long    http_response;

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

                search_ret = RV_find_object_by_path(loc_obj, link_path_dirname, &obj_type,
                        RV_copy_object_URI_callback, NULL, temp_URI);
                if (!search_ret || search_ret < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_PATH, FAIL, "can't locate parent group for link")
            } /* end if */

            /* URL-encode the link name so that the resulting URL for the link GET
             * operation doesn't contain any illegal characters
             */
            if (NULL == (url_encoded_link_name = curl_easy_escape(curl, RV_basename(loc_params.loc_data.loc_by_name.name), 0)))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode link name")

            if ((url_len = snprintf(request_url, URL_MAX_LENGTH,
                                    "%s/groups/%s/links/%s",
                                    base_URL,
                                    empty_dirname ? loc_obj->URI : temp_URI,
                                    url_encoded_link_name)
                ) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "H5Lexists request URL size exceeded maximum URL size")

            /* Setup cURL to make the GET request */

            /* Setup the host header */
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

#ifdef RV_PLUGIN_DEBUG
            printf("-> Checking for existence of link using URL: %s\n\n", request_url);

            printf("   /**********************************\\\n");
            printf("-> | Making GET request to the server |\n");
            printf("   \\**********************************/\n\n");
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
            iter_data link_iter_data;

            link_iter_data.is_recursive               = va_arg(arguments, int);
            link_iter_data.index_type                 = va_arg(arguments, H5_index_t);
            link_iter_data.iter_order                 = va_arg(arguments, H5_iter_order_t);
            link_iter_data.idx_p                      = va_arg(arguments, hsize_t *);
            link_iter_data.iter_function.link_iter_op = va_arg(arguments, H5L_iterate_t);
            link_iter_data.op_data                    = va_arg(arguments, void *);

            if (!link_iter_data.iter_function.link_iter_op)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_LINKITERERROR, FAIL, "no link iteration function specified")

            switch (loc_params.type) {
                /* H5Literate/H5Lvisit */
                case H5VL_OBJECT_BY_SELF:
                {
#ifdef RV_PLUGIN_DEBUG
                    printf("-> Opening group for link iteration to generate an hid_t and work around VOL layer\n\n");
#endif

                    /* Since the VOL doesn't directly pass down the group's hid_t, explicitly open the group
                     * here so that a valid hid_t can be passed to the user's link iteration callback.
                     */
                    if (NULL == (link_iter_group_object = RV_group_open(loc_obj, loc_params, ".",
                            H5P_DEFAULT, H5P_DEFAULT, NULL)))
                        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTOPENOBJ, FAIL, "can't open link iteration group")

                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH,
                             "%s/groups/%s/links",
                             base_URL,
                             loc_obj->URI)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "H5Literate/visit request URL size exceeded maximum URL size")

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Literate_by_name/H5Lvisit_by_name */
                case H5VL_OBJECT_BY_NAME:
                {
#ifdef RV_PLUGIN_DEBUG
                    printf("-> Opening group for link iteration to generate an hid_t and work around VOL layer\n\n");
#endif

                    /* Since the VOL doesn't directly pass down the group's hid_t, explicitly open the group
                     * here so that a valid hid_t can be passed to the user's link iteration callback.
                     */
                    if (NULL == (link_iter_group_object = RV_group_open(loc_obj, loc_params, loc_params.loc_data.loc_by_name.name,
                            H5P_DEFAULT, H5P_DEFAULT, NULL)))
                        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTOPENOBJ, FAIL, "can't open link iteration group")

                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH,
                             "%s/groups/%s/links",
                             base_URL,
                             ((RV_object_t *) link_iter_group_object)->URI)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "H5Literate/visit_by_name request URL size exceeded maximum URL size")

                    break;
                } /* H5VL_OBJECT_BY_NAME */

                case H5VL_OBJECT_BY_IDX:
                case H5VL_OBJECT_BY_ADDR:
                case H5VL_OBJECT_BY_REF:
                default:
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "invalid loc_params type")
            } /* end switch */

#ifdef RV_PLUGIN_DEBUG
            printf("-> Registering hid_t for opened group\n\n");
#endif

            /* Note: The case of handling the group ID is awkward as it is, but is made even more
             * awkward by the fact that this might be the first call to register an ID for an object
             * of type H5I_GROUP. Since the group was opened using a VOL-internal routine and was not
             * able to go through the public API call H5Gopen2(), this means that it is possible for
             * the H5G interface to be uninitialized at this point in time, which will cause the below
             * H5VLobject_register() call to fail. Therefore, we have to make a fake call to H5Gopen2()
             * to make sure that the H5G interface is initialized. The call will of course fail, but
             * the FUNC_ENTER_API macro should ensure that the H5G interface is initialized.
             */
            H5E_BEGIN_TRY {
                H5Gopen2(-1, NULL, H5P_DEFAULT);
            } H5E_END_TRY;

            /* Register an hid_t for the group object */
            if ((link_iter_group_id = H5VLobject_register(link_iter_group_object, H5I_GROUP, REST_g)) < 0)
                FUNC_GOTO_ERROR(H5E_ATOM, H5E_CANTREGISTER, FAIL, "can't create ID for group to be iterated over")
            link_iter_data.iter_obj_id = link_iter_group_id;

            /* Make a GET request to the server to retrieve all of the links in the given group */

            /* Setup the host header */
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

#ifdef RV_PLUGIN_DEBUG
            printf("-> Retrieving all links in group using URL: %s\n\n", request_url);

            printf("   /**********************************\\\n");
            printf("-> | Making GET request to the server |\n");
            printf("   \\**********************************/\n\n");
#endif

            CURL_PERFORM(curl, H5E_LINK, H5E_CANTGET, FAIL);

            if (RV_parse_response(response_buffer.buffer, &link_iter_data, NULL, RV_link_iter_callback) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't iterate over links")

            break;
        } /* H5VL_LINK_ITER */

        default:
            FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "unknown link operation");
    } /* end switch */

done:
    if (link_path_dirname)
        RV_free(link_path_dirname);
    if (host_header)
        RV_free(host_header);

    if (link_iter_group_id >= 0)
        if (H5Gclose(link_iter_group_id) < 0)
            FUNC_DONE_ERROR(H5E_LINK, H5E_CANTCLOSEOBJ, FAIL, "can't close link iteration group")

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

    PRINT_ERROR_STACK

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
               hid_t dxpl_id, void **req)
{
    RV_object_t *loc_obj = (RV_object_t *) obj;
    H5I_type_t   obj_type = H5I_UNINIT;
    hid_t        lapl_id;
    void        *ret_value = NULL;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received object open call with following parameters:\n");
    if (H5VL_OBJECT_BY_NAME == loc_params.type) {
        printf("     - H5Oopen variant: H5Oopen\n");
    } /* end if */
    else if (H5VL_OBJECT_BY_IDX == loc_params.type) {
        printf("     - H5Oopen variant: H5Oopen_by_idx\n");
    } /* end else if */
    else if (H5VL_OBJECT_BY_ADDR == loc_params.type) {
        printf("     - H5Oopen variant: H5Oopen_by_addr\n");
    } /* end else if */

    if (loc_params.loc_data.loc_by_name.name) printf("     - Path to object: %s\n", loc_params.loc_data.loc_by_name.name);
    printf("     - loc_id object's URI: %s\n", loc_obj->URI);
    printf("     - loc_id object's type: %s\n", object_type_to_string(loc_obj->obj_type));
    printf("     - loc_id object's domain path: %s\n\n", loc_obj->domain->u.file.filepath_name);
#endif

    if (H5I_FILE != loc_obj->obj_type && H5I_GROUP != loc_obj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object not a file or group")

    switch (loc_params.type) {
        /* H5Oopen */
        case H5VL_OBJECT_BY_NAME:
        {
            htri_t search_ret;

            /* Retrieve the type of object being dealt with by querying the server */
            search_ret = RV_find_object_by_path(loc_obj, loc_params.loc_data.loc_by_name.name, &obj_type, NULL, NULL, NULL);
            if (!search_ret || search_ret < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_PATH, NULL, "can't find object by name")

#ifdef RV_PLUGIN_DEBUG
            printf("-> Found object by given path\n\n");
#endif

            break;
        } /* H5VL_OBJECT_BY_NAME */

        /* H5Oopen_by_idx */
        case H5VL_OBJECT_BY_IDX:
        {
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, NULL, "H5Oopen_by_idx is unsupported")
            break;
        } /* H5VL_OBJECT_BY_IDX */

        /* H5Oopen_by_addr */
        case H5VL_OBJECT_BY_ADDR:
        {
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, NULL, "H5Oopen_by_addr is unsupported")
            break;
        } /* H5VL_OBJECT_BY_ADDR */

        /* H5Rdereference2 */
        case H5VL_OBJECT_BY_REF:
        {
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, NULL, "H5Rdereference2 is unsupported")
            break;
        } /* H5VL_OBJECT_BY_REF */

        case H5VL_OBJECT_BY_SELF:
        default:
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, NULL, "invalid loc_params type")
    } /* end switch */

    /* Call the appropriate RV_*_open call based upon the object type */
    switch (obj_type) {
        case H5I_DATATYPE:
#ifdef RV_PLUGIN_DEBUG
            printf("-> Opening datatype\n\n");
#endif

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

            if (NULL == (ret_value = RV_datatype_open(loc_obj, loc_params, loc_params.loc_data.loc_by_name.name,
                    lapl_id, dxpl_id, req)))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTOPENOBJ, NULL, "can't open datatype")
            break;

        case H5I_DATASET:
#ifdef RV_PLUGIN_DEBUG
            printf("-> Opening dataset\n\n");
#endif

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

            if (NULL == (ret_value = RV_dataset_open(loc_obj, loc_params, loc_params.loc_data.loc_by_name.name,
                    lapl_id, dxpl_id, req)))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTOPENOBJ, NULL, "can't open dataset")
            break;

        case H5I_GROUP:
#ifdef RV_PLUGIN_DEBUG
            printf("-> Opening group\n\n");
#endif

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

            if (NULL == (ret_value = RV_group_open(loc_obj, loc_params, loc_params.loc_data.loc_by_name.name,
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
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTOPENOBJ, NULL, "invalid object type")
    } /* end switch */

    if (opened_type) *opened_type = obj_type;

done:
    PRINT_ERROR_STACK

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
               hid_t ocpypl_id, hid_t lcpl_id, hid_t dxpl_id, void **req)
{
    herr_t ret_value = SUCCEED;

    FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, FAIL, "H5Ocopy is unsupported")

done:
    PRINT_ERROR_STACK

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
              hid_t dxpl_id, void **req, va_list arguments)
{
    RV_object_t *loc_obj = (RV_object_t *) obj;
    herr_t       ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received object get call with following parameters:\n");
    printf("     - Object get call type: %s\n", object_get_type_to_string(get_type));
    printf("     - loc_id object's URI: %s\n", loc_obj->URI);
    printf("     - loc_id object's type: %s\n", object_type_to_string(loc_obj->obj_type));
    printf("     - loc_id object's domain path: %s\n\n", loc_obj->domain->u.file.filepath_name);
#endif

    switch (get_type) {
        /* H5Rget_name */
        case H5VL_REF_GET_NAME:
            FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_UNSUPPORTED, FAIL, "H5Rget_name is unsupported")

        /* H5Rget_region */
        case H5VL_REF_GET_REGION:
        {
            hid_t      *ret = va_arg(arguments, hid_t *);
            H5R_type_t  ref_type = va_arg(arguments, H5R_type_t);
            void       *ref = va_arg(arguments, void *);

            /* Unused until support for region references can be implemented */
            UNUSED_VAR(loc_obj);
            UNUSED_VAR(ret);
            UNUSED_VAR(ref);

            /* Though the actual ref type should be stored in the ref itself, we take the user's
             * passed in ref type at face value here.
             */
            if (H5R_DATASET_REGION != ref_type)
                FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_BADVALUE, FAIL, "not a dataset region reference")

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

#ifdef RV_PLUGIN_DEBUG
                    printf("-> Object reference to object of type: %s\n\n", object_type_to_string2(*obj_type));
#endif

                    break;
                } /* H5R_OBJECT */

                case H5R_DATASET_REGION:
                {
                    FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_BADVALUE, FAIL, "region references are currently unsupported")
                    break;
                } /* H5R_DATASET_REGION */

                case H5R_BADTYPE:
                case H5R_MAXTYPE:
                default:
                    FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_BADVALUE, FAIL, "invalid reference type")
            } /* end switch */

            break;
        } /* H5VL_REF_GET_TYPE */

        default:
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "unknown object operation")
    } /* end switch */

done:
    PRINT_ERROR_STACK

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
                   hid_t dxpl_id, void **req, va_list arguments)
{
    RV_object_t *loc_obj = (RV_object_t *) obj;
    herr_t       ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received object-specific call with following parameters:\n");
    printf("     - Object-specific call type: %s\n", object_specific_type_to_string(specific_type));
    printf("     - loc_id object's URI: %s\n", loc_obj->URI);
    printf("     - loc_id object's type: %s\n", object_type_to_string(loc_obj->obj_type));
    printf("     - loc_id object's domain path: %s\n\n", loc_obj->domain->u.file.filepath_name);
#endif

    switch (specific_type) {
        /* H5Oincr/decr_refcount */
        case H5VL_OBJECT_CHANGE_REF_COUNT:
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, FAIL, "H5Oincr_refcount and H5Odecr_refcount are unsupported")

        /* H5Oexists_by_name */
        case H5VL_OBJECT_EXISTS:
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, FAIL, "H5Oexists_by_name is unsupported")

        /* H5Ovisit(_by_name) */
        case H5VL_OBJECT_VISIT:
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, FAIL, "H5Ovisit and H5Ovisit_by_name are unsupported")

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

#ifdef RV_PLUGIN_DEBUG
                    printf("-> Object reference; locating referenced object\n\n");
#endif

                    /* Locate the object for the reference, setting the ref_obj_type and ref_obj_URI fields in the process */
                    objref->ref_obj_type = H5I_UNINIT;
                    search_ret = RV_find_object_by_path(loc_obj, name, &objref->ref_obj_type,
                            RV_copy_object_URI_callback, NULL, objref->ref_obj_URI);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_PATH, FAIL, "can't locate ref obj. by path")

#ifdef RV_PLUGIN_DEBUG
                    printf("-> Found referenced object\n");
                    printf("-> Referenced object's URI: %s\n", objref->ref_obj_URI);
                    printf("-> Referenced object's type: %s\n\n", object_type_to_string(objref->ref_obj_type));
#endif

                    objref->ref_type = ref_type;

                    break;
                } /* H5R_OBJECT */

                case H5R_DATASET_REGION:
                {
                    /* Unused until support for region references can be implemented */
                    UNUSED_VAR(space_id);

                    FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_UNSUPPORTED, FAIL, "region references are currently unsupported")
                    break;
                } /* H5R_DATASET_REGION */

                case H5R_BADTYPE:
                case H5R_MAXTYPE:
                default:
                    FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_BADVALUE, FAIL, "invalid ref type")
            } /* end switch */

            break;
        } /* H5VL_REF_CREATE */

        default:
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "unknown object operation")
    } /* end switch */

done:
    PRINT_ERROR_STACK

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
RV_object_optional(void *obj, hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_object_optional_t  optional_type = (H5VL_object_optional_t) va_arg(arguments, int);
    H5VL_loc_params_t       loc_params = va_arg(arguments, H5VL_loc_params_t);
    RV_object_t            *loc_obj = (RV_object_t *) obj;
    size_t                  host_header_len = 0;
    char                   *host_header = NULL;
    char                    request_url[URL_MAX_LENGTH];
    int                     url_len = 0;
    herr_t                  ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Received object optional call with following parameters:\n");
    printf("     - Object optional call type: %s\n", object_optional_type_to_string(optional_type));
    printf("     - loc_id object's URI: %s\n", loc_obj->URI);
    printf("     - loc_id object's type: %s\n", object_type_to_string(loc_obj->obj_type));
    printf("     - loc_id object's domain path: %s\n\n", loc_obj->domain->u.file.filepath_name);
#endif

    if (   H5I_FILE != loc_obj->obj_type
        && H5I_GROUP != loc_obj->obj_type
        && H5I_DATATYPE != loc_obj->obj_type
        && H5I_DATASET != loc_obj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a group, dataset or datatype")

    switch (optional_type) {
        /* H5Oset_comment and H5Oset_comment_by_name */
        case H5VL_OBJECT_SET_COMMENT:
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, FAIL, "object comments are deprecated in favor of use of object attributes")

        /* H5Oget_comment and H5Oget_comment_by_name */
        case H5VL_OBJECT_GET_COMMENT:
        {
            char    *comment_buf = va_arg(arguments, char *);
            size_t   comment_buf_size = va_arg(arguments, size_t);
            ssize_t *ret_size = va_arg(arguments, ssize_t *);

            UNUSED_VAR(comment_buf);
            UNUSED_VAR(comment_buf_size);

            /* Even though H5Oset_comment is deprecated in favor of attributes, H5Oget_comment is
             * still used in h5dump, so we just return a comment size of 0 and don't support object
             * comments.
             */
            *ret_size = 0;

            break;
        } /* H5VL_OBJECT_GET_COMMENT */

        /* H5Oget_info (_by_name/_by_idx) */
        case H5VL_OBJECT_GET_INFO:
        {
            H5O_info_t *obj_info = va_arg(arguments, H5O_info_t *);
            H5I_type_t  obj_type;

            switch (loc_params.type) {
                /* H5Oget_info */
                case H5VL_OBJECT_BY_SELF:
                {
                    obj_type = loc_obj->obj_type;

                    /* Redirect cURL from the base URL to
                     * "/groups/<id>", "/datasets/<id>" or "/datatypes/<id>",
                     * depending on the type of the object. Also set the
                     * object's type in the H5O_info_t struct.
                     */
                    switch (obj_type) {
                        case H5I_FILE:
                        case H5I_GROUP:
                        {
                            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s", base_URL, loc_obj->URI)) < 0)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "snprintf error")

                            if (url_len >= URL_MAX_LENGTH)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "H5Oget_info request URL size exceeded maximum URL size")

                            break;
                        } /* H5I_FILE H5I_GROUP */

                        case H5I_DATATYPE:
                        {
                            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s", base_URL, loc_obj->URI)) < 0)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "snprintf error")

                            if (url_len >= URL_MAX_LENGTH)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "H5Oget_info request URL size exceeded maximum URL size")

                            break;
                        } /* H5I_DATATYPE */

                        case H5I_DATASET:
                        {
                            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s", base_URL, loc_obj->URI)) < 0)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "snprintf error")

                            if (url_len >= URL_MAX_LENGTH)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "H5Oget_info request URL size exceeded maximum URL size")

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
                            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "loc_id object is not a group, datatype or dataset")
                    } /* end switch */

#ifdef RV_PLUGIN_DEBUG
                    printf("-> H5Oget_info(): Object type: %s\n\n", object_type_to_string(obj_type));
#endif

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Oget_info_by_name */
                case H5VL_OBJECT_BY_NAME:
                {
                    htri_t search_ret;
                    char   temp_URI[URI_MAX_LENGTH];

                    obj_type = H5I_UNINIT;

#ifdef RV_PLUGIN_DEBUG
                    printf("-> H5Oget_info_by_name(): locating object by given path\n\n");
#endif

                    search_ret = RV_find_object_by_path(loc_obj, loc_params.loc_data.loc_by_name.name, &obj_type,
                            RV_copy_object_URI_callback, NULL, temp_URI);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PATH, FAIL, "can't locate object")

#ifdef RV_PLUGIN_DEBUG
                    printf("-> H5Oget_info_by_name(): found object by given path\n");
                    printf("-> H5Oget_info_by_name(): object's URI: %s\n", temp_URI);
                    printf("-> H5Oget_info_by_name(): object's type: %s\n\n", object_type_to_string(obj_type));
#endif

                    /* Redirect cURL from the base URL to
                     * "/groups/<id>", "/datasets/<id>" or "/datatypes/<id>",
                     * depending on the type of the object. Also set the
                     * object's type in the H5O_info_t struct.
                     */
                    switch (obj_type) {
                        case H5I_FILE:
                        case H5I_GROUP:
                        {
                            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s", base_URL, temp_URI)) < 0)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "snprintf error")

                            if (url_len >= URL_MAX_LENGTH)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "H5Oget_info_by_name request URL size exceeded maximum URL size")

                            break;
                        } /* H5I_FILE H5I_GROUP */

                        case H5I_DATATYPE:
                        {
                            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s", base_URL, temp_URI)) < 0)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "snprintf error")

                            if (url_len >= URL_MAX_LENGTH)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "H5Oget_info_by_name request URL size exceeded maximum URL size")

                            break;
                        } /* H5I_DATATYPE */

                        case H5I_DATASET:
                        {
                            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s", base_URL, temp_URI)) < 0)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "snprintf error")

                            if (url_len >= URL_MAX_LENGTH)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "H5Oget_info_by_name request URL size exceeded maximum URL size")

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
                            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "loc_id object is not a group, datatype or dataset")
                    } /* end switch */

                    break;
                } /* H5VL_OBJECT_BY_NAME */

                /* H5Oget_info_by_idx */
                case H5VL_OBJECT_BY_IDX:
                {
                    FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, FAIL, "H5Oget_info_by_idx is unsupported")
                    break;
                } /* H5VL_OBJECT_BY_IDX */

                case H5VL_OBJECT_BY_ADDR:
                case H5VL_OBJECT_BY_REF:
                default:
                    FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "invalid loc_params type")
            } /* end switch */

            /* Make a GET request to the server to retrieve the number of attributes attached to the object */

            /* Setup the host header */
            host_header_len = strlen(loc_obj->domain->u.file.filepath_name) + strlen(host_string) + 1;
            if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header")

            strcpy(host_header, host_string);

            curl_headers = curl_slist_append(curl_headers, strncat(host_header, loc_obj->domain->u.file.filepath_name, host_header_len - strlen(host_string) - 1));

            /* Disable use of Expect: 100 Continue HTTP response */
            curl_headers = curl_slist_append(curl_headers, "Expect:");

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf)
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP GET request: %s", curl_err_buf)
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef RV_PLUGIN_DEBUG
            printf("-> Retrieving object info using URL: %s\n\n", request_url);

            printf("   /**********************************\\\n");
            printf("-> | Making GET request to the server |\n");
            printf("   \\**********************************/\n\n");
#endif

            CURL_PERFORM(curl, H5E_OBJECT, H5E_CANTGET, FAIL);

            /* Retrieve the attribute count for the object */
            if (RV_parse_response(response_buffer.buffer, NULL, obj_info, RV_get_object_info_callback) < 0)
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTGET, FAIL, "can't get object info")

            /* Set the type of the object */
            if (H5I_GROUP == obj_type)
                obj_info->type = H5O_TYPE_GROUP;
            else if (H5I_DATATYPE == obj_type)
                obj_info->type = H5O_TYPE_NAMED_DATATYPE;
            else if (H5I_DATASET == obj_type)
                obj_info->type = H5O_TYPE_DATASET;
            else
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "object type is not group, datatype or dataset")

            break;
        } /* H5VL_OBJECT_GET_INFO */

        default:
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "unknown object operation")
    } /* end switch */

done:
    if (host_header)
        RV_free(host_header);

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    PRINT_ERROR_STACK

    return ret_value;
} /* end RV_object_optional() */

/************************************
 *         Helper functions         *
 ************************************/


/*-------------------------------------------------------------------------
 * Function:    curl_read_data_callback
 *
 * Purpose:     A callback for cURL which will copy the data from a given
 *              buffer into cURL's internal buffer when making an HTTP PUT
 *              call to the server.
 *
 * Return:      Amount of bytes equal to the amount given in the upload
 *              info struct on success/0 on failure or if NULL data buffer
 *              is given
 *
 * Programmer:  Jordan Henderson
 *              January, 2018
 */
static size_t
curl_read_data_callback(char *buffer, size_t size, size_t nmemb, void *inptr)
{
    upload_info *uinfo = (upload_info *) inptr;
    size_t       max_buf_size = size * nmemb;
    size_t       data_size = 0;

    if (inptr) {
        data_size = (uinfo->buffer_size > max_buf_size) ? max_buf_size : uinfo->buffer_size;

        memcpy(buffer, uinfo->buffer, data_size);
    } /* end if */

    return data_size;
} /* end curl_read_data_callback() */


/*-------------------------------------------------------------------------
 * Function:    curl_write_data_callback
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
curl_write_data_callback(char *buffer, size_t size, size_t nmemb, void *userp)
{
    ptrdiff_t buf_ptrdiff;
    size_t    data_size = size * nmemb;
    size_t    ret_value = 0;

    /* If the server response is larger than the currently allocated amount for the
     * response buffer, grow the response buffer by a factor of 2
     */
    buf_ptrdiff = (response_buffer.curr_buf_ptr + data_size) - response_buffer.buffer;
    if (buf_ptrdiff < 0)
        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, 0, "unsafe cast: response buffer pointer difference was negative - this should not happen!")

    /* Avoid using the 'CHECKED_REALLOC' macro here because we don't necessarily
     * want to free the plugin's response buffer if the reallocation fails.
     */
    while ((size_t) (buf_ptrdiff + 1) > response_buffer.buffer_size) {
        char *tmp_realloc;

        if (NULL == (tmp_realloc = (char *) RV_realloc(response_buffer.buffer, 2 * response_buffer.buffer_size)))
            FUNC_GOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, 0, "can't reallocate space for response buffer")

        response_buffer.curr_buf_ptr = tmp_realloc + (response_buffer.curr_buf_ptr - response_buffer.buffer);
        response_buffer.buffer = tmp_realloc;
        response_buffer.buffer_size *= 2;
    } /* end while */

    memcpy(response_buffer.curr_buf_ptr, buffer, data_size);
    response_buffer.curr_buf_ptr += data_size;
    *response_buffer.curr_buf_ptr = '\0';

    ret_value = data_size;

done:
    return ret_value;
} /* end curl_write_data_callback() */


/*-------------------------------------------------------------------------
 * Function:    RV_basename
 *
 * Purpose:     A portable implementation of the basename routine which
 *              retrieves everything after the final '/' in a given
 *              pathname.
 *
 *              Note that for performance and simplicity this function
 *              exhibits the GNU behavior in that it will return the empty
 *              string if the pathname contains a trailing '/'. Therefore,
 *              if a user provides a path that contains a trailing '/',
 *              this will likely confuse the plugin and lead to incorrect
 *              behavior. If this becomes an issue in the future, this
 *              function may need to be reimplemented.
 *
 * Return:      Basename portion of the given path
 *              (can't fail)
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
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
 * Function:    base64_encode
 *
 * Purpose:     A helper function to base64 encode the given buffer. This
 *              is used specifically when dealing with writing data to a
 *              dataset using a point selection.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              January, 2018
 */
static herr_t
RV_base64_encode(const void *in, size_t in_size, char **out, size_t *out_size)
{
    const uint8_t *buf = (const uint8_t *) in;
    const char     charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    uint32_t       three_byte_set;
    uint8_t        c0, c1, c2, c3;
    size_t         i;
    size_t         nalloc;
    size_t         out_index = 0;
    int            npad;
    herr_t         ret_value = SUCCEED;

    if (!in)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "input buffer pointer was NULL")
    if (!out)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "output buffer pointer was NULL")

    /* If the caller has specified a 0-sized buffer, allocate one and set nalloc
     * so that the following 'nalloc *= 2' calls don't result in 0-sized
     * allocations.
     */
    if (!out_size || (out_size && !*out_size)) {
        nalloc = BASE64_ENCODE_DEFAULT_BUFFER_SIZE;
        if (NULL == (*out = (char *) RV_malloc(nalloc)))
            FUNC_GOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, FAIL, "can't allocate space for base64-encoding output buffer")
    } /* end if */
    else
        nalloc = *out_size;

    for (i = 0; i < in_size; i += 3) {
        three_byte_set = ((uint32_t) buf[i]) << 16;

        if (i + 1 < in_size)
            three_byte_set += ((uint32_t) buf[i + 1]) << 8;

        if (i + 2 < in_size)
            three_byte_set += buf[i + 2];

        /* Split 3-byte number into four 6-bit groups for encoding */
        c0 = (uint8_t) (three_byte_set >> 18) & 0x3f;
        c1 = (uint8_t) (three_byte_set >> 12) & 0x3f;
        c2 = (uint8_t) (three_byte_set >> 6)  & 0x3f;
        c3 = (uint8_t)  three_byte_set        & 0x3f;

        CHECKED_REALLOC_NO_PTR(*out, nalloc, out_index + 2, H5E_RESOURCE, FAIL);

        (*out)[out_index++] = charset[c0];
        (*out)[out_index++] = charset[c1];

        if (i + 1 < in_size) {
            CHECKED_REALLOC_NO_PTR(*out, nalloc, out_index + 1, H5E_RESOURCE, FAIL);

            (*out)[out_index++] = charset[c2];
        } /* end if */

        if (i + 2 < in_size) {
            CHECKED_REALLOC_NO_PTR(*out, nalloc, out_index + 1, H5E_RESOURCE, FAIL);

            (*out)[out_index++] = charset[c3];
        } /* end if */
    } /* end for */

    /* Add trailing padding when out_index does not fall on the beginning of a 4-byte set */
    npad = (4 - (out_index % 4)) % 4;
    while (npad) {
        CHECKED_REALLOC_NO_PTR(*out, nalloc, out_index + 1, H5E_RESOURCE, FAIL);

        (*out)[out_index++] = '=';

        npad--;
    } /* end while */

    CHECKED_REALLOC_NO_PTR(*out, nalloc, out_index + 1, H5E_RESOURCE, FAIL);

    (*out)[out_index] = '\0';

    if (out_size)
        *out_size = out_index;

done:
    return ret_value;
} /* end base64_encode() */


/*-------------------------------------------------------------------------
 * Function:    RV_url_encode_path
 *
 * Purpose:     A helper function to URL-encode an entire path name by
 *              URL-encoding each of its separate components and then
 *              sticking them back together into a single string.
 *
 * Return:      URL-encoded version of the given path on success/ NULL on
 *              failure
 *
 * Programmer:  Jordan Henderson
 *              January, 2018
 */
static char *
RV_url_encode_path(const char *path)
{
    ptrdiff_t  buf_ptrdiff;
    size_t     bytes_nalloc;
    size_t     path_prefix_len;
    size_t     path_component_len;
    char      *path_copy = NULL;
    char      *url_encoded_path_component = NULL;
    char      *token;
    char      *cur_pos;
    char      *tmp_buffer = NULL;
    char      *ret_value = NULL;

    if (!path)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "path was NULL")

    /* Retrieve the length of the possible path prefix, which could be something like '/', '.', etc. */
    cur_pos = (char *) path;
    while (*cur_pos && !isalnum(*cur_pos))
        cur_pos++;
    path_prefix_len = (size_t) (cur_pos - path);

#ifdef RV_PLUGIN_DEBUG
    printf("-> Length of path prefix: %zu\n\n", path_prefix_len);
#endif

    /* Copy the given path (minus the path prefix) so that strtok() can safely modify it */
    bytes_nalloc = strlen(path) - path_prefix_len + 1;
    if (NULL == (path_copy = RV_malloc(bytes_nalloc)))
        FUNC_GOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, NULL, "can't allocate space for copy of path")

    strncpy(path_copy, path + path_prefix_len, bytes_nalloc);

#ifdef RV_PLUGIN_DEBUG
    printf("-> Allocated copy of path: %s\n\n", path_copy);
#endif

    /* As URLs generally tend to be short and URL-encoding in the worst case will generally
     * introduce 3x the memory usage for a given URL (since URL-encoded characters are
     * represented by a '%' followed by two hexadecimal digits), go ahead and allocate
     * the resulting buffer to this size and grow it dynamically if really necessary.
     */
    bytes_nalloc = (3 * bytes_nalloc) + path_prefix_len + 1;
    if (NULL == (tmp_buffer = RV_malloc(bytes_nalloc)))
        FUNC_GOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, NULL, "can't allocate space for resulting URL-encoded path buffer")

    cur_pos = tmp_buffer;

    /* Append the path's possible prefix, along with the first path component. This is to handle relative
     * vs absolute pathnames, where a leading '/' or '.' may or may not be present */
    if (path_prefix_len) {
        strncpy(cur_pos, path, path_prefix_len);
        cur_pos += path_prefix_len;
    } /* end if */

    if ((token = strtok(path_copy, "/"))) {
        if (NULL == (url_encoded_path_component = curl_easy_escape(curl, token, 0)))
            FUNC_GOTO_ERROR(H5E_NONE_MAJOR, H5E_CANTENCODE, NULL, "can't URL-encode path component")

        path_component_len = strlen(url_encoded_path_component);

        buf_ptrdiff = cur_pos - tmp_buffer;
        if (buf_ptrdiff < 0)
            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, NULL, "unsafe cast: path buffer pointer difference was negative - this should not happen!")

        CHECKED_REALLOC(tmp_buffer, bytes_nalloc, (size_t) buf_ptrdiff + path_component_len, cur_pos, H5E_RESOURCE, NULL);

        strncpy(cur_pos, url_encoded_path_component, path_component_len);
        cur_pos += path_component_len;

        curl_free(url_encoded_path_component);
        url_encoded_path_component = NULL;
    } /* end if */

    /* For each of the rest of the path components, URL-encode it and then append it into
     * the resulting path buffer, dynamically growing the buffer as necessary.
     */
    for (token = strtok(NULL, "/"); token; token = strtok(NULL, "/")) {
#ifdef RV_PLUGIN_DEBUG
        printf("-> Processing next token: %s\n\n", token);
#endif

        if (NULL == (url_encoded_path_component = curl_easy_escape(curl, token, 0)))
            FUNC_GOTO_ERROR(H5E_NONE_MAJOR, H5E_CANTENCODE, NULL, "can't URL-encode path component")

#ifdef RV_PLUGIN_DEBUG
        printf("-> URL-encoded form of token: %s\n\n", url_encoded_path_component);
#endif

        path_component_len = strlen(url_encoded_path_component);

        /* Check if the path components buffer needs to be grown */
        buf_ptrdiff = cur_pos - tmp_buffer;
        if (buf_ptrdiff < 0)
            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, NULL, "unsafe cast: path buffer pointer difference was negative - this should not happen!")

        CHECKED_REALLOC(tmp_buffer, bytes_nalloc, (size_t) buf_ptrdiff + path_component_len + 1, cur_pos, H5E_RESOURCE, NULL);

        *cur_pos++ = '/';
        strncpy(cur_pos, url_encoded_path_component, path_component_len);
        cur_pos += path_component_len;

        curl_free(url_encoded_path_component);
        url_encoded_path_component = NULL;
    } /* end for */

    buf_ptrdiff = cur_pos - tmp_buffer;
    if (buf_ptrdiff < 0)
        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, NULL, "unsafe cast: path buffer pointer difference was negative - this should not happen!")

    CHECKED_REALLOC(tmp_buffer, bytes_nalloc, (size_t) buf_ptrdiff + 1, cur_pos, H5E_RESOURCE, NULL);

    *cur_pos = '\0';

#ifdef RV_PLUGIN_DEBUG
    printf("-> Final URL-encoded string: %s\n\n", tmp_buffer);
#endif

    ret_value = tmp_buffer;

done:
    if (url_encoded_path_component)
        curl_free(url_encoded_path_component);
    if (!ret_value && tmp_buffer)
        RV_free(tmp_buffer);
    if (path_copy)
        RV_free(path_copy);

    return ret_value;
} /* end RV_url_encode_path() */


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

    return 0;
} /* end dataset_read_scatter_op() */


/*-------------------------------------------------------------------------
 * Function:    cmp_attributes_by_creation_order
 *
 * Purpose:     Qsort callback to sort attributes by creation order when
 *              doing attribute iteration
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              January, 2018
 */
static int
cmp_attributes_by_creation_order(const void *attr1, const void *attr2)
{
    const attr_table_entry *_attr1 = (const attr_table_entry *) attr1;
    const attr_table_entry *_attr2 = (const attr_table_entry *) attr2;

    return ((_attr1->crt_time > _attr2->crt_time) - (_attr1->crt_time < _attr2->crt_time));
} /* end cmp_attributes_by_creation_order() */


/*-------------------------------------------------------------------------
 * Function:    cmp_links_by_creation_order
 *
 * Purpose:     Qsort callback to sort links by creation order when doing
 *              link iteration
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static int
cmp_links_by_creation_order(const void *link1, const void *link2)
{
    const link_table_entry *_link1 = (const link_table_entry *) link1;
    const link_table_entry *_link2 = (const link_table_entry *) link2;

    return ((_link1->crt_time > _link2->crt_time) - (_link1->crt_time < _link2->crt_time));
} /* end cmp_links_by_creation_order() */


/*-------------------------------------------------------------------------
 * Function:    rv_compare_string_keys
 *
 * Purpose:     Comparison function to compare two string keys in an
 *              rv_hash_table_t. This function is mostly used when
 *              attempting to determine object uniqueness by some
 *              information from the server, such as an object ID.
 *
 * Return:      Non-zero if the two string keys are equal/Zero if the two
 *              string keys are not equal
 *
 * Programmer:  Jordan Henderson
 *              May, 2018
 */
static int
rv_compare_string_keys(void *value1, void *value2)
{
    const char *val1 = (const char *) value1;
    const char *val2 = (const char *) value2;

    return !strcmp(val1, val2);
} /* end rv_compare_string_keys() */


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

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL")

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
RV_copy_object_URI_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out)
{
    yajl_val  parse_tree = NULL, key_obj;
    char     *parsed_string;
    char     *buf_out = (char *) callback_data_out;
    herr_t    ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Retrieving object's URI from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL")
    if (!buf_out)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "output buffer was NULL")

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "parsing JSON failed")

    /* To handle the awkward case of soft and external links, which do not return an "ID",
     * first check for the link class field and short circuit if it is found to be
     * equal to "H5L_TYPE_SOFT"
     */
    if (NULL != (key_obj = yajl_tree_get(parse_tree, link_class_keys, yajl_t_string))) {
        char *link_type;

        if (NULL == (link_type = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "link type string was NULL")

        if (!strcmp(link_type, "H5L_TYPE_SOFT") || !strcmp(link_type, "H5L_TYPE_EXTERNAL") ||
                !strcmp(link_type, "H5L_TYPE_UD"))
            FUNC_GOTO_DONE(SUCCEED)
    } /* end if */

    /* First attempt to retrieve the URI of the object by using the JSON key sequence
     * "link" -> "id", which is returned when making a GET Link request.
     */
    key_obj = yajl_tree_get(parse_tree, link_id_keys, yajl_t_string);
    if (key_obj) {
        if (!YAJL_IS_STRING(key_obj))
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "returned URI is not a string")

        if (NULL == (parsed_string = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "URI was NULL")

#ifdef RV_PLUGIN_DEBUG
        printf("-> Found object's URI by \"link\" -> \"id\" JSON key path\n\n");
#endif
    } /* end if */
    else {
#ifdef RV_PLUGIN_DEBUG
        printf("-> Could not find object's URI by \"link\" -> \"id\" JSON key path\n");
        printf("-> Trying \"id\" JSON key path instead\n");
#endif

        /* Could not find the object's URI by the sequence "link" -> "id". Try looking
         * for just the JSON key "id", which would generally correspond to trying to
         * retrieve the URI of a newly-created or opened object that isn't a file.
         */
        key_obj = yajl_tree_get(parse_tree, object_id_keys, yajl_t_string);
        if (key_obj) {
            if (!YAJL_IS_STRING(key_obj))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "returned URI is not a string")

            if (NULL == (parsed_string = YAJL_GET_STRING(key_obj)))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "URI was NULL")

#ifdef RV_PLUGIN_DEBUG
            printf("-> Found object's URI by \"id\" JSON key path\n\n");
#endif
        } /* end if */
        else {
#ifdef RV_PLUGIN_DEBUG
            printf("-> Could not find object's URI by \"id\" JSON key path\n");
            printf("-> Trying \"root\" JSON key path instead\n");
#endif

            /* Could not find the object's URI by the JSON key "id". Try looking for
             * just the JSON key "root", which would generally correspond to trying to
             * retrieve the URI of a newly-created or opened file, or to a search for
             * the root group of a file.
             */
            if (NULL == (key_obj = yajl_tree_get(parse_tree, root_id_keys, yajl_t_string)))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTGET, FAIL, "retrieval of URI failed")

            if (!YAJL_IS_STRING(key_obj))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "returned URI is not a string")

            if (NULL == (parsed_string = YAJL_GET_STRING(key_obj)))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "URI was NULL")

#ifdef RV_PLUGIN_DEBUG
            printf("-> Found object's URI by \"root\" JSON key path\n\n");
#endif
        } /* end else */
    } /* end else */

    strncpy(buf_out, parsed_string, URI_MAX_LENGTH);

done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_copy_object_URI_parse_callback() */


/*-------------------------------------------------------------------------
 * Function:    RV_get_link_obj_type_callback
 *
 * Purpose:     A callback for RV_parse_response which will search
 *              an HTTP response for the type of an object that a link
 *              points to and copy that type into the callback_data_out
 *              parameter, which should be a H5I_type_t *. This callback
 *              is used to help RV_find_object_by_path() determine what
 *              type of object it is working with.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              September, 2017
 */
static herr_t
RV_get_link_obj_type_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out)
{
    H5I_type_t *obj_type = (H5I_type_t *) callback_data_out;
    yajl_val    parse_tree = NULL, key_obj;
    char       *parsed_string;
    herr_t      ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Retrieving object's type from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL")
    if (!obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "object type pointer was NULL")

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "parsing JSON failed")

    /* To handle the awkward case of soft and external links, which do not have the link
     * collection element, first check for the link class field and short circuit if it
     * is found not to be equal to "H5L_TYPE_HARD"
     */
    if (NULL != (key_obj = yajl_tree_get(parse_tree, link_class_keys, yajl_t_string))) {
        char *link_type;

        if (NULL == (link_type = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "link type string was NULL")

        if (strcmp(link_type, "H5L_TYPE_HARD"))
            FUNC_GOTO_DONE(SUCCEED);
    } /* end if */

    /* Retrieve the object's type */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, link_collection_keys, yajl_t_string)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTGET, FAIL, "retrieval of object parent collection failed")

    if (!YAJL_IS_STRING(key_obj))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "returned object parent collection is not a string")

    if (NULL == (parsed_string = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "object parent collection string was NULL")

    if (!strcmp(parsed_string, "groups"))
        *obj_type = H5I_GROUP;
    else if (!strcmp(parsed_string, "datasets"))
        *obj_type = H5I_DATASET;
    else if (!strcmp(parsed_string, "datatypes"))
        *obj_type = H5I_DATATYPE;
    else
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "invalid object type")

#ifdef RV_PLUGIN_DEBUG
    printf("-> Retrieved object's type: %s\n\n", object_type_to_string(*obj_type));
#endif

done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_get_link_obj_type_callback() */


/*-------------------------------------------------------------------------
 * Function:    RV_get_link_info_callback
 *
 * Purpose:     A callback for RV_parse_response which will search an HTTP
 *              response for information about a link, such as the link
 *              type, and copy that info into the callback_data_out
 *              parameter, which should be a H5L_info_t *. This callback
 *              is used specifically for H5Lget_info (_by_idx). Currently
 *              only the link class, and for soft, external and
 *              user-defined links, the link value, is returned by this
 *              function. All other information in the H5L_info_t struct is
 *              initialized to 0.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static herr_t
RV_get_link_info_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out)
{
    H5L_info_t *link_info = (H5L_info_t *) callback_data_out;
    yajl_val    parse_tree = NULL, key_obj;
    char       *parsed_string;
    herr_t      ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Retrieving link's info from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL")
    if (!link_info)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "link info pointer was NULL")

    memset(link_info, 0, sizeof(H5L_info_t));

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_PARSEERROR, FAIL, "parsing JSON failed")

    /* Retrieve the link's class */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, link_class_keys, yajl_t_string))) {
        if (NULL == (key_obj = yajl_tree_get(parse_tree, link_class_keys2, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "retrieval of object parent collection failed")
    }

    if (!YAJL_IS_STRING(key_obj))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "returned object parent collection is not a string")

    if (NULL == (parsed_string = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "object parent collection string was NULL")

    if (!strcmp(parsed_string, "H5L_TYPE_HARD"))
        link_info->type = H5L_TYPE_HARD;
    else if (!strcmp(parsed_string, "H5L_TYPE_SOFT"))
        link_info->type = H5L_TYPE_SOFT;
    else if (!strcmp(parsed_string, "H5L_TYPE_EXTERNAL"))
        link_info->type = H5L_TYPE_EXTERNAL;
    else
        FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "invalid link class")

#ifdef RV_PLUGIN_DEBUG
    printf("-> Retrieved link's class: %s\n\n", link_class_to_string(link_info->type));
#endif

    /* If this is not a hard link, determine the value for the 'val_size' field corresponding
     * to the size of a soft, external or user-defined link's value, including the NULL terminator
     */
    if (strcmp(parsed_string, "H5L_TYPE_HARD")) {
        if (RV_parse_response(HTTP_response, &link_info->u.val_size, NULL, RV_get_link_val_callback) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't retrieve link value size")

#ifdef RV_PLUGIN_DEBUG
        printf("-> Retrieved link's value size: %zu\n\n", link_info->u.val_size);
#endif
    } /* end if */

done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_get_link_info_callback() */


/*-------------------------------------------------------------------------
 * Function:    RV_get_link_val_callback
 *
 * Purpose:     A callback for RV_parse_response which will search an HTTP
 *              response for a link's value, and do one of two things,
 *              based on the value of the buffer size given through the
 *              callback_data_in parameter.
 *
 *              If the buffer size given is non-positive, this callback
 *              will just set the buffer size parameter to be the size
 *              needed to actually store the link's value.
 *
 *              If the buffer size given is positive, this callback will
 *              copy the link's value into the callback_data_out
 *              parameter, which should be a char *, corresponding to the
 *              link value buffer, of size equal to the given buffer size
 *              parameter.
 *
 *              This callback is used by H5Lget_info to store the size of
 *              the link's value in an H5L_info_t struct's 'val_size'
 *              field, and also by H5Lget_val (_by_idx) to actually
 *              retrieve the link's value.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static herr_t
RV_get_link_val_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out)
{
    yajl_val  parse_tree = NULL, key_obj;
    size_t   *in_buf_size = (size_t *) callback_data_in;
    char     *link_path;
    char     *link_class;
    char     *out_buf = (char *) callback_data_out;
    herr_t    ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Retrieving link's value from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL")
    if (!in_buf_size)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "buffer size pointer was NULL")

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_PARSEERROR, FAIL, "parsing JSON failed")

    /* Retrieve the link's class */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, link_class_keys, yajl_t_string))) {
        if (NULL == (key_obj = yajl_tree_get(parse_tree, link_class_keys2, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "retrieval of link class failed")
    }

    if (!YAJL_IS_STRING(key_obj))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "returned link class is not a string")

    if (NULL == (link_class = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "link class was NULL")

    if (!strcmp(link_class, "H5L_TYPE_HARD"))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "H5Lget_val should not be called for hard links")

    /* Retrieve the link's value */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, link_path_keys, yajl_t_string))) {
        if (NULL == (key_obj = yajl_tree_get(parse_tree, link_path_keys2, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "retrieval of link value failed")
    }

    if (!YAJL_IS_STRING(key_obj))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "returned link value is not a string")

    if (NULL == (link_path = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "link value was NULL")

    if (!strcmp(link_class, "H5L_TYPE_SOFT")) {
        if ((!*in_buf_size) || (*in_buf_size < 0)) {
            /* If the buffer size was specified as non-positive, simply set the size that
             * the buffer needs to be to contain the link value, which should just be
             * large enough to contain the link's target path
             */
            *in_buf_size = strlen(link_path) + 1;

#ifdef RV_PLUGIN_DEBUG
            printf("-> Returning size of soft link's value\n\n");
#endif
        } /* end if */
        else {
            if (out_buf)
                strncpy(out_buf, link_path, *in_buf_size);

            /* Ensure that the buffer is NULL-terminated */
            out_buf[*in_buf_size - 1] = '\0';

#ifdef RV_PLUGIN_DEBUG
            printf("-> Returning soft link's value\n\n");
#endif
        } /* end else */
    } /* end if */
    else {
        yajl_val  link_domain_obj;
        unsigned  link_version;
        unsigned  link_flags;
        char     *link_domain;

        if (NULL == (link_domain_obj = yajl_tree_get(parse_tree, link_domain_keys, yajl_t_string))) {
            if (NULL == (link_domain_obj = yajl_tree_get(parse_tree, link_domain_keys2, yajl_t_string)))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "retrieval of external link domain failed")
        }

        if (!YAJL_IS_STRING(link_domain_obj))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "returned external link domain is not a string")

        if (NULL == (link_domain = YAJL_GET_STRING(link_domain_obj)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "link domain was NULL")

        /* Process external links; user-defined links are currently unsupported */
        if ((!*in_buf_size) || (*in_buf_size < 0)) {
            /* If the buffer size was specified as non-positive, simply set the size that
             * the buffer needs to be to contain the link value, which should contain
             * the link's flags, target file and target path in the case of external links
             */
            *in_buf_size = 1 + (strlen(link_domain) + 1) + (strlen(link_path) + 1);

#ifdef RV_PLUGIN_DEBUG
            printf("-> Returning size of external link's value\n\n");
#endif
        } /* end if */
        else {
            uint8_t *p = (uint8_t *) out_buf;

            if (p) {
                /* Pack an external link's flags, target object and target file into a single buffer
                 * for later unpacking with H5Lunpack_elink_val(). Note that the implementation
                 * for unpacking the external link buffer may change in the future and thus this
                 * implementation for packing it up will have to change as well.
                 */

                /* First pack the link version and flags into the output buffer */
                link_version = 0;
                link_flags = 0;

                /* The correct usage should be H5L_EXT_VERSION and H5L_EXT_FLAGS_ALL, but these
                 * are not currently exposed to the VOL
                 */
                /* *p++ = (H5L_EXT_VERSION << 4) | H5L_EXT_FLAGS_ALL; */

                *p++ = (link_version << 4) | link_flags;

                /* Next copy the external link's target filename into the link value buffer */
                strncpy((char *) p, link_domain, *in_buf_size - 1);
                p += strlen(link_domain) + 1;

                /* Finally comes the external link's target path */
                strncpy((char *) p, link_path, (*in_buf_size - 1) - (strlen(link_domain) + 1));

#ifdef RV_PLUGIN_DEBUG
                printf("-> Returning external link's value\n\n");
#endif
            } /* end if */
        } /* end else */
    } /* end else */

done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_get_link_val_callback() */

/*-------------------------------------------------------------------------
 * Function:    RV_link_iter_callback
 *
 * Purpose:     A callback for RV_parse_response which will search an HTTP
 *              response for links in a group and iterate through them,
 *              setting up a H5L_info_t struct and calling the supplied
 *              callback function for each link. The callback_data_in
 *              parameter should be a iter_data struct *, containing all
 *              the data necessary for link iteration, such as the callback
 *              function, iteration order, index type, etc.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static herr_t
RV_link_iter_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out)
{
    link_table_entry *link_table = NULL;
    rv_hash_table_t  *visited_link_table = NULL;
    iter_data        *link_iter_data = (iter_data *) callback_data_in;
    size_t            link_table_num_entries;
    herr_t            ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Iterating %s through links according to server's HTTP response\n\n", link_iter_data->is_recursive ? "recursively" : "non-recursively");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL")
    if (!link_iter_data)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "link iteration data pointer was NULL")

    /* If this is a call to H5Lvisit, setup a hash table to keep track of visited links
     * so that cyclic links can be dealt with appropriately.
     */
    if (link_iter_data->is_recursive) {
        if (NULL == (visited_link_table = rv_hash_table_new(rv_hash_string, rv_compare_string_keys)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate hash table for determining cyclic links")

        /* Since the JSON parse trees aren't persistent, the keys inserted into the visited link hash table
         * are RV_malloc()ed copies. Make sure to free these when freeing the table.
         */
        rv_hash_table_register_free_functions(visited_link_table, RV_free_visited_link_hash_table_key, NULL);
    } /* end if */

    /* Build a table of all of the links in the given group */
    if (H5_INDEX_CRT_ORDER == link_iter_data->index_type) {
        /* This code assumes that links are returned in alphabetical order by default. If the user has requested them
         * by creation order, sort them this way while building the link table. If, in the future, links are not returned
         * in alphabetical order by default, this code should be changed to reflect this.
         */
        if (RV_build_link_table(HTTP_response, link_iter_data->is_recursive, cmp_links_by_creation_order,
                &link_table, &link_table_num_entries, visited_link_table) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTBUILDLINKTABLE, FAIL, "can't build link table")

#ifdef RV_PLUGIN_DEBUG
        printf("-> Link table sorted according to link creation order\n\n");
#endif
    } /* end if */
    else {
        if (RV_build_link_table(HTTP_response, link_iter_data->is_recursive, NULL,
                &link_table, &link_table_num_entries, visited_link_table) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTBUILDLINKTABLE, FAIL, "can't build link table")
    } /* end else */

    /* Begin iteration */
    if (link_table)
        if (RV_traverse_link_table(link_table, link_table_num_entries, link_iter_data, NULL) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_LINKITERERROR, FAIL, "can't iterate over link table")

done:
    if (link_table)
        RV_free_link_table(link_table, link_table_num_entries);

    /* Free the visited link hash table if necessary */
    if (visited_link_table) {
        rv_hash_table_free(visited_link_table);
        visited_link_table = NULL;
    } /* end if */

    return ret_value;
} /* end RV_link_iter_callback() */


/*-------------------------------------------------------------------------
 * Function:    RV_attr_iter_callback
 *
 * Purpose:     A callback for RV_parse_response which will search an HTTP
 *              response for attributes attached to an object and iterate
 *              through them, setting up a H5A_info_t struct and calling
 *              the supplied callback function for each attribute. The
 *              callback_data_in parameter should be a iter_data struct *,
 *              containing all the data necessary for attribute iteration,
 *              such as the callback function, iteration order, index type,
 *              etc.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              January, 2018
 */
static herr_t
RV_attr_iter_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out)
{
    attr_table_entry *attr_table = NULL;
    iter_data        *attr_iter_data = (iter_data *) callback_data_in;
    size_t            attr_table_num_entries;
    herr_t            ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Iterating through attributes according to server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL")
    if (!attr_iter_data)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "attribute iteration data pointer was NULL")

    /* Build a table of all of the attributes attached to the given object */
    if (H5_INDEX_CRT_ORDER == attr_iter_data->index_type) {
        /* This code assumes that attributes are returned in alphabetical order by default. If the user has requested them
         * by creation order, sort them this way while building the attribute table. If, in the future, attributes are not
         * returned in alphabetical order by default, this code should be changed to reflect this.
         */
        if (RV_build_attr_table(HTTP_response, TRUE, cmp_attributes_by_creation_order, &attr_table, &attr_table_num_entries) < 0)
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTBUILDATTRTABLE, FAIL, "can't build attribute table")

#ifdef RV_PLUGIN_DEBUG
        printf("-> Attribute table sorted according to creation order\n\n");
#endif
    } /* end if */
    else {
        if (RV_build_attr_table(HTTP_response, FALSE, NULL, &attr_table, &attr_table_num_entries) < 0)
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTBUILDATTRTABLE, FAIL, "can't build attribute table")
    } /* end else */

    /* Begin iteration */
    if (attr_table)
        if (RV_traverse_attr_table(attr_table, attr_table_num_entries, attr_iter_data) < 0)
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_ATTRITERERROR, FAIL, "can't iterate over attribute table")

done:
    if (attr_table)
        RV_free(attr_table);

    return ret_value;
} /* end RV_attr_iter_callback() */


/*-------------------------------------------------------------------------
 * Function:    RV_get_attr_info_callback
 *
 * Purpose:     A callback for RV_parse_response which will search
 *              an HTTP response for info about an attribute and copy that
 *              info into the callback_data_out parameter, which should be
 *              a H5A_info_t *. This callback is used to help
 *              H5Aget_info (_by_name/_by_idx); currently the H5A_info_t
 *              struct is just initialized to 0, as HDF Kita does not have
 *              any provisions for returning any of the relevant
 *              information in the H5A_info_t struct.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static herr_t
RV_get_attr_info_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out)
{
    H5A_info_t *attr_info = (H5A_info_t *) callback_data_out;
    herr_t      ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Retrieving attribute info from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL")
    if (!attr_info)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "attribute info pointer was NULL")

    memset(attr_info, 0, sizeof(*attr_info));

done:
    return ret_value;
} /* end RV_get_attr_info_callback() */


/*-------------------------------------------------------------------------
 * Function:    RV_get_object_info_callback
 *
 * Purpose:     A callback for RV_parse_response which will search
 *              an HTTP response for info about an object and copy that
 *              info into the callback_data_out parameter, which should be
 *              a H5O_info_t *. This callback is used to help H5Oget_info;
 *              currently only the file number, object address and number
 *              of attributes fields are filled out in the H5O_info_t
 *              struct. All other fields are cleared and should not be
 *              relied upon.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              November, 2017
 */
static herr_t
RV_get_object_info_callback(char *HTTP_response,
    void *callback_data_in, void *callback_data_out)
{
    H5O_info_t *obj_info = (H5O_info_t *) callback_data_out;
    yajl_val    parse_tree = NULL, key_obj;
    size_t      i;
    char       *object_id, *domain_path = NULL;
    herr_t      ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Retrieving object's info from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL")
    if (!obj_info)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "object info pointer was NULL")

    memset(obj_info, 0, sizeof(*obj_info));

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "parsing JSON failed")

    /*
     * Fill out the fileno and addr fields with somewhat faked data, as these fields are used
     * in other places to verify that two objects are different. The domain path is hashed
     * and converted to an unsigned long for the fileno field and the object's UUID string
     * is hashed to an haddr_t for the addr field.
     */

    if (NULL == (key_obj = yajl_tree_get(parse_tree, hrefs_keys, yajl_t_array)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTGET, FAIL, "retrieval of object HREFs failed")

    /* Find the "home" href that corresponds to the object's domain path */
    for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++) {
        yajl_val  href_obj = YAJL_GET_ARRAY(key_obj)->values[i];
        size_t    j;

        if (!YAJL_IS_OBJECT(href_obj))
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "HREFs array value is not an object")

        for (j = 0; j < YAJL_GET_OBJECT(href_obj)->len; j++) {
            char *key_val;

            if (NULL == (key_val = YAJL_GET_STRING(YAJL_GET_OBJECT(href_obj)->values[j])))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "HREF object key value was NULL")

            /* If this object's "rel" key does not have the value "home", skip this object */
            if (!strcmp(YAJL_GET_OBJECT(href_obj)->keys[j], "rel") && strcmp(key_val, "home")) {
                domain_path = NULL;
                break;
            } /* end if */

            if (!strcmp(YAJL_GET_OBJECT(href_obj)->keys[j], "href"))
                domain_path = key_val;
        } /* end for */

        if (domain_path)
            break;
    } /* end for */

    if (!domain_path)
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTSET, FAIL, "unable to determine a value for object info file number field")

    obj_info->fileno = (unsigned long) rv_hash_string(domain_path);

#ifdef RV_PLUGIN_DEBUG
    printf("-> Object's file number: %lu\n", (unsigned long) obj_info->fileno);
#endif

    if (NULL == (key_obj = yajl_tree_get(parse_tree, object_id_keys, yajl_t_string)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTGET, FAIL, "retrieval of object ID failed")

    if (NULL == (object_id = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "object ID string was NULL")

    obj_info->addr = (haddr_t) rv_hash_string(object_id);

#ifdef RV_PLUGIN_DEBUG
    printf("-> Object's address: %lu\n", (unsigned long) obj_info->addr);
#endif

    /* Retrieve the object's attribute count */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, attribute_count_keys, yajl_t_number)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTGET, FAIL, "retrieval of object attribute count failed")

    if (!YAJL_IS_INTEGER(key_obj))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "returned object attribute count is not an integer")

    if (YAJL_GET_INTEGER(key_obj) < 0)
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "returned object attribute count was negative")

    obj_info->num_attrs = (hsize_t) YAJL_GET_INTEGER(key_obj);

#ifdef RV_PLUGIN_DEBUG
    printf("-> Object had %llu attributes attached to it\n\n", obj_info->num_attrs);
#endif

done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_get_object_info_callback() */


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
    void *callback_data_in, void *callback_data_out)
{
    H5G_info_t *group_info = (H5G_info_t *) callback_data_out;
    yajl_val    parse_tree = NULL, key_obj;
    herr_t      ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Retrieving group's info from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL")
    if (!group_info)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "group info pointer was NULL")

    memset(group_info, 0, sizeof(*group_info));

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_PARSEERROR, FAIL, "parsing JSON failed")

    /* Retrieve the group's link count */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, group_link_count_keys, yajl_t_number)))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTGET, FAIL, "retrieval of group link count failed")

    if (!YAJL_IS_INTEGER(key_obj))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_BADVALUE, FAIL, "returned group link count is not an integer")

    if (YAJL_GET_INTEGER(key_obj) < 0)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_BADVALUE, FAIL, "group link count was negative")

    group_info->nlinks = (hsize_t) YAJL_GET_INTEGER(key_obj);

#ifdef RV_PLUGIN_DEBUG
    printf("-> Group had %llu links in it\n\n", group_info->nlinks);
#endif

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
    void *callback_data_in, void *callback_data_out)
{
    yajl_val  parse_tree = NULL, creation_properties_obj, key_obj;
    hid_t    *DCPL = (hid_t *) callback_data_out;
    herr_t    ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Retrieving dataset's creation properties from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL")
    if (!DCPL)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "DCPL pointer was NULL")

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_PARSEERROR, FAIL, "parsing JSON failed")

    /* Retrieve the creationProperties object */
    if (NULL == (creation_properties_obj = yajl_tree_get(parse_tree, creation_properties_keys, yajl_t_object)))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "retrieval of creationProperties object failed")


    /********************************************************************************************
     *                                                                                          *
     *                              Space Allocation Time Section                               *
     *                                                                                          *
     * Determine the status of the space allocation time (default, early, late, incremental)    *
     * and set this on the DCPL                                                                 *
     *                                                                                          *
     ********************************************************************************************/
    if ((key_obj = yajl_tree_get(creation_properties_obj, alloc_time_keys, yajl_t_string))) {
        H5D_alloc_time_t  alloc_time;
        char             *alloc_time_string;

        if (NULL == (alloc_time_string = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "space allocation time string was NULL")

        if (!strcmp(alloc_time_string, "H5D_ALLOC_TIME_EARLY")) {
            alloc_time = H5D_ALLOC_TIME_EARLY;

#ifdef RV_PLUGIN_DEBUG
            printf("-> Setting AllocTime H5D_ALLOC_TIME_EARLY on DCPL\n");
#endif
        } /* end if */
        else if (!strcmp(alloc_time_string, "H5D_ALLOC_TIME_INCR")) {
            alloc_time = H5D_ALLOC_TIME_INCR;

#ifdef RV_PLUGIN_DEBUG
            printf("-> Setting AllocTime H5D_ALLOC_TIME_INCR on DCPL\n");
#endif
        } /* end else if */
        else if (!strcmp(alloc_time_string, "H5D_ALLOC_TIME_LATE")) {
            alloc_time = H5D_ALLOC_TIME_LATE;

#ifdef RV_PLUGIN_DEBUG
            printf("-> Setting AllocTime H5D_ALLOC_TIME_LATE on DCPL\n");
#endif
        } /* end else if */
        else {
            alloc_time = H5D_ALLOC_TIME_DEFAULT;

#ifdef RV_PLUGIN_DEBUG
            printf("-> Setting AllocTime H5D_ALLOC_TIME_DEFAULT on DCPL\n");
#endif
        } /* end else */

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
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "attribute creation order string was NULL")

        if (!strcmp(crt_order_string, "H5P_CRT_ORDER_INDEXED")) {
            crt_order_flags = H5P_CRT_ORDER_INDEXED | H5P_CRT_ORDER_TRACKED;

#ifdef RV_PLUGIN_DEBUG
            printf("-> Setting attribute creation order H5P_CRT_ORDER_INDEXED + H5P_CRT_ORDER_TRACKED on DCPL\n");
#endif
        } /* end if */
        else {
            crt_order_flags = H5P_CRT_ORDER_TRACKED;

#ifdef RV_PLUGIN_DEBUG
            printf("-> Setting attribute creation order H5P_CRT_ORDER_TRACKED on DCPL\n");
#endif
        } /* end else */

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
        unsigned minDense = DATASET_CREATE_MIN_DENSE_ATTRIBUTES_DEFAULT;
        unsigned maxCompact = DATASET_CREATE_MAX_COMPACT_ATTRIBUTES_DEFAULT;
        yajl_val sub_obj;

        if (NULL == (sub_obj = yajl_tree_get(key_obj, max_compact_keys, yajl_t_number)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "retrieval of maxCompact attribute phase change value failed")

        if (!YAJL_IS_INTEGER(sub_obj))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "return maxCompact attribute phase change value is not an integer")

        if (YAJL_GET_INTEGER(sub_obj) >= 0)
            maxCompact = (unsigned) YAJL_GET_INTEGER(sub_obj);

        if (NULL == (sub_obj = yajl_tree_get(key_obj, min_dense_keys, yajl_t_number)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "retrieval of minDense attribute phase change value failed")

        if (!YAJL_IS_INTEGER(sub_obj))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "returned minDense attribute phase change value is not an integer")

        if (YAJL_GET_INTEGER(sub_obj) >= 0)
            minDense = (unsigned) YAJL_GET_INTEGER(sub_obj);

        if (minDense != DATASET_CREATE_MIN_DENSE_ATTRIBUTES_DEFAULT || maxCompact != DATASET_CREATE_MAX_COMPACT_ATTRIBUTES_DEFAULT) {
#ifdef RV_PLUGIN_DEBUG
            printf("-> Setting attribute phase change values: [ minDense: %u, maxCompact: %u ] on DCPL\n", minDense, maxCompact);
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
        H5D_fill_time_t  fill_time;
        char            *fill_time_str;

        if (NULL == (fill_time_str = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "fill time string was NULL")

        if (!strcmp(fill_time_str, "H5D_FILL_TIME_ALLOC")) {
            fill_time = H5D_FILL_TIME_ALLOC;

#ifdef RV_PLUGIN_DEBUG
            printf("-> Setting fill time H5D_FILL_TIME_ALLOC on DCPL\n");
#endif
        } /* end else if */
        else if (!strcmp(fill_time_str, "H5D_FILL_TIME_NEVER")) {
            fill_time = H5D_FILL_TIME_NEVER;

#ifdef RV_PLUGIN_DEBUG
            printf("-> Setting fill time H5D_FILL_TIME_NEVER on DCPL\n");
#endif
        } /* end else if */
        else {
            fill_time = H5D_FILL_TIME_IFSET;

#ifdef RV_PLUGIN_DEBUG
            printf("-> Setting fill time H5D_FILL_TIME_IFSET on DCPL\n");
#endif
        } /* end else */

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
        /* TODO: Until fill value support is implemented, just push an error to the stack but continue ahead */
        FUNC_DONE_ERROR(H5E_DATASET, H5E_UNSUPPORTED, SUCCEED, "warning: dataset fill values are unsupported")
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
        size_t i;

        /* Grab the relevant information from each filter and set them on the DCPL in turn. */
        for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++) {
            yajl_val   filter_obj = YAJL_GET_ARRAY(key_obj)->values[i];
            yajl_val   filter_field;
            char      *filter_class;
            long long  filter_ID;

            if (NULL == (filter_field = yajl_tree_get(filter_obj, filter_class_keys, yajl_t_string)))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "retrieval of filter class failed")

            if (NULL == (filter_class = YAJL_GET_STRING(filter_field)))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "filter class string was NULL")

            if (NULL == (filter_field = yajl_tree_get(filter_obj, filter_ID_keys, yajl_t_number)))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "retrieval of filter ID failed")

            if (!YAJL_IS_INTEGER(filter_field))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "returned filter ID is not an integer")

            filter_ID = YAJL_GET_INTEGER(filter_field);

            switch (filter_ID) {
                case H5Z_FILTER_DEFLATE:
                {
                    const char *deflate_level_keys[] = { "level", (const char *) 0 };
                    long long   deflate_level;

#ifdef RV_PLUGIN_DEBUG
                    printf("-> Discovered filter class H5Z_FILTER_DEFLATE in JSON response; setting deflate filter on DCPL\n");
#endif

                    /* Quick sanity check; push an error to the stack on failure, but don't fail this function */
                    if (strcmp(filter_class, "H5Z_FILTER_DEFLATE"))
                        FUNC_DONE_ERROR(H5E_DATASET, H5E_BADVALUE, SUCCEED, "warning: filter class '%s' does not match H5Z_FILTER_DEFLATE; DCPL should not be trusted", filter_class)

                    /* Grab the level of compression */
                    if (NULL == (filter_field = yajl_tree_get(filter_obj, deflate_level_keys, yajl_t_number)))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "retrieval of deflate filter compression level value failed")

                    if (!YAJL_IS_INTEGER(filter_field))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "returned deflate filter compression level is not an integer")

                    deflate_level = YAJL_GET_INTEGER(filter_field);
                    if (deflate_level < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "deflate filter compression level invalid (level < 0)")

                    if (H5Pset_deflate(*DCPL, (unsigned) deflate_level) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set deflate filter on DCPL")

                    break;
                }

                case H5Z_FILTER_SHUFFLE:
                {
#ifdef RV_PLUGIN_DEBUG
                    printf("-> Discovered filter class H5Z_FILTER_SHUFFLE in JSON response; setting shuffle filter on DCPL\n");
#endif

                    /* Quick sanity check; push an error to the stack on failure, but don't fail this function */
                    if (strcmp(filter_class, "H5Z_FILTER_SHUFFLE"))
                        FUNC_DONE_ERROR(H5E_DATASET, H5E_BADVALUE, SUCCEED, "warning: filter class '%s' does not match H5Z_FILTER_SHUFFLE; DCPL should not be trusted", filter_class)

                    if (H5Pset_shuffle(*DCPL) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set shuffle filter on DCPL")

                    break;
                }

                case H5Z_FILTER_FLETCHER32:
                {
#ifdef RV_PLUGIN_DEBUG
                    printf("-> Discovered filter class H5Z_FILTER_FLETCHER32 in JSON response; setting fletcher32 filter on DCPL\n");
#endif

                    /* Quick sanity check; push an error to the stack on failure, but don't fail this function */
                    if (strcmp(filter_class, "H5Z_FILTER_FLETCHER32"))
                        FUNC_DONE_ERROR(H5E_DATASET, H5E_BADVALUE, SUCCEED, "warning: filter class '%s' does not match H5Z_FILTER_FLETCHER32; DCPL should not be trusted", filter_class)

                    if (H5Pset_fletcher32(*DCPL) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set fletcher32 filter on DCPL")

                    break;
                }

                case H5Z_FILTER_SZIP:
                {
                    const char *szip_option_mask_keys[] = { "coding", (const char *) 0 };
                    const char *szip_ppb_keys[]         = { "pixelsPerBlock", (const char *) 0 };
                    char       *szip_option_mask;
                    long long   szip_ppb;

#ifdef RV_PLUGIN_DEBUG
                    printf("-> Discovered filter class H5Z_FILTER_SZIP in JSON response; setting SZIP filter on DCPL\n");
#endif

                    /* Quick sanity check; push an error to the stack on failure, but don't fail this function */
                    if (strcmp(filter_class, "H5Z_FILTER_SZIP"))
                        FUNC_DONE_ERROR(H5E_DATASET, H5E_BADVALUE, SUCCEED, "warning: filter class '%s' does not match H5Z_FILTER_SZIP; DCPL should not be trusted", filter_class)

                    /* Retrieve the value of the SZIP option mask */
                    if (NULL == (filter_field = yajl_tree_get(filter_obj, szip_option_mask_keys, yajl_t_string)))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "retrieval of SZIP option mask failed")

                    if (NULL == (szip_option_mask = YAJL_GET_STRING(filter_field)))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "SZIP option mask string was NULL")

                    if (strcmp(szip_option_mask, "H5_SZIP_EC_OPTION_MASK") && strcmp(szip_option_mask, "H5_SZIP_NN_OPTION_MASK")) {
                        /* Push an error to the stack, but don't fail this function */
                        FUNC_DONE_ERROR(H5E_DATASET, H5E_BADVALUE, SUCCEED, "invalid SZIP option mask value '%s'", szip_option_mask)
                        continue;
                    }

                    /* Retrieve the value of the SZIP "pixels per block" option */
                    if (NULL == (filter_field = yajl_tree_get(filter_obj, szip_ppb_keys, yajl_t_number)))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "retrieval of SZIP pixels per block option failed")

                    if (!YAJL_IS_INTEGER(filter_field))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "returned SZIP pixels per block option value is not an integer")

                    szip_ppb = YAJL_GET_INTEGER(filter_field);
                    if (szip_ppb < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "invalid SZIP pixels per block option value (PPB < 0)")

                    if (H5Pset_szip(*DCPL, (!strcmp(szip_option_mask, "H5_SZIP_EC_OPTION_MASK") ? H5_SZIP_EC_OPTION_MASK : H5_SZIP_NN_OPTION_MASK),
                            (unsigned) szip_ppb) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set SZIP filter on DCPL")

                    break;
                }

                case H5Z_FILTER_NBIT:
                {
#ifdef RV_PLUGIN_DEBUG
                    printf("-> Discovered filter class H5Z_FILTER_NBIT in JSON response; setting N-Bit filter on DCPL\n");
#endif

                    /* Quick sanity check; push an error to the stack on failure, but don't fail this function */
                    if (strcmp(filter_class, "H5Z_FILTER_NBIT"))
                        FUNC_DONE_ERROR(H5E_DATASET, H5E_BADVALUE, SUCCEED, "warning: filter class '%s' does not match H5Z_FILTER_NBIT; DCPL should not be trusted", filter_class)

                    if (H5Pset_nbit(*DCPL) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set N-Bit filter on DCPL")

                    break;
                }

                case H5Z_FILTER_SCALEOFFSET:
                {
                    H5Z_SO_scale_type_t  scale_type;
                    const char          *scale_type_keys[]   = { "scaleType", (const char *) 0 };
                    const char          *scale_offset_keys[] = { "scaleOffset", (const char *) 0 };
                    long long            scale_offset;
                    char                *scale_type_str;

#ifdef RV_PLUGIN_DEBUG
                    printf("-> Discovered filter class H5Z_FILTER_SCALEOFFSET in JSON response; setting scale-offset filter on DCPL\n");
#endif

                    /* Quick sanity check; push an error to the stack on failure, but don't fail this function */
                    if (strcmp(filter_class, "H5Z_FILTER_SCALEOFFSET"))
                        FUNC_DONE_ERROR(H5E_DATASET, H5E_BADVALUE, SUCCEED, "warning: filter class '%s' does not match H5Z_FILTER_SCALEOFFSET; DCPL should not be trusted", filter_class)

                    /* Retrieve the scale type */
                    if (NULL == (filter_field = yajl_tree_get(filter_obj, scale_type_keys, yajl_t_string)))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "retrieval of scale type failed")

                    if (NULL == (scale_type_str = YAJL_GET_STRING(filter_field)))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "scale type string was NULL")

                    if (!strcmp(scale_type_str, "H5Z_SO_FLOAT_DSCALE"))
                        scale_type = H5Z_SO_FLOAT_DSCALE;
                    else if (!strcmp(scale_type_str, "H5Z_SO_FLOAT_ESCALE"))
                        scale_type = H5Z_SO_FLOAT_ESCALE;
                    else if (!strcmp(scale_type_str, "H5Z_SO_INT"))
                        scale_type = H5Z_SO_INT;
                    else {
                        FUNC_DONE_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "invalid scale type '%s'", scale_type_str)
                        continue;
                    }

                    /* Retrieve the scale offset value */
                    if (NULL == (filter_field = yajl_tree_get(filter_obj, scale_offset_keys, yajl_t_number)))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "retrieval of scale offset value failed")

                    if (!YAJL_IS_INTEGER(filter_field))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "returned scale offset value is not an integer")

                    scale_offset = YAJL_GET_INTEGER(filter_field);

                    if (H5Pset_scaleoffset(*DCPL, scale_type, (int) scale_offset) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set scale-offset filter on DCPL")

                    break;
                }

                case LZF_FILTER_ID:
                {
#ifdef RV_PLUGIN_DEBUG
                    printf("-> Discovered filter class H5Z_FILTER_LZF in JSON response; setting LZF filter on DCPL\n");
#endif

                    /* Quick sanity check; push an error to the stack on failure, but don't fail this function */
                    if (strcmp(filter_class, "H5Z_FILTER_LZF"))
                        FUNC_DONE_ERROR(H5E_DATASET, H5E_BADVALUE, SUCCEED, "warning: filter class '%s' does not match H5Z_FILTER_LZF; DCPL should not be trusted", filter_class)

                    /* Note that it may be more appropriate to set the LZF filter as mandatory here, but for now optional is used */
                    if (H5Pset_filter(*DCPL, LZF_FILTER_ID, H5Z_FLAG_OPTIONAL, 0, NULL) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set LZF filter on DCPL")

                    break;
                }

                /* TODO: support for other/user-defined filters */

                default:
                    /* Push error to stack; but don't fail this function */
                    FUNC_DONE_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "warning: invalid filter with class '%s' and ID '%lld' on DCPL", filter_class, filter_ID);
            }

#ifdef RV_PLUGIN_DEBUG
            printf("-> Filter %zu:\n", i);
            printf("->   Class: %s\n", filter_class);
            printf("->   ID: %lld\n", filter_ID);
#endif
        } /* end for */
    } /* end if */


    /****************************************************************************
     *                                                                          *
     *                                Layout Section                            *
     *                                                                          *
     * Determine the layout information of the Dataset and set this on the DCPL *
     *                                                                          *
     ****************************************************************************/
    if ((key_obj = yajl_tree_get(creation_properties_obj, layout_keys, yajl_t_object))) {
        yajl_val  sub_obj;
        size_t    i;
        char     *layout_class;

        if (NULL == (sub_obj = yajl_tree_get(key_obj, layout_class_keys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "retrieval of layout class property failed")

        if (NULL == (layout_class = YAJL_GET_STRING(sub_obj)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "layout class string was NULL")

        if (!strcmp(layout_class, "H5D_CHUNKED")) {
            yajl_val chunk_dims_obj;
            hsize_t  chunk_dims[DATASPACE_MAX_RANK];

            if (NULL == (chunk_dims_obj = yajl_tree_get(key_obj, chunk_dims_keys, yajl_t_array)))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "retrieval of chunk dimensionality failed")

            for (i = 0; i < YAJL_GET_ARRAY(chunk_dims_obj)->len; i++) {
                long long val;

                if (!YAJL_IS_INTEGER(YAJL_GET_ARRAY(chunk_dims_obj)->values[i]))
                    FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "one of the chunk dimension sizes was not an integer")

                if ((val = YAJL_GET_INTEGER(YAJL_GET_ARRAY(chunk_dims_obj)->values[i])) < 0)
                    FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "one of the chunk dimension sizes was negative")

                chunk_dims[i] = (hsize_t) val;
            } /* end for */

#ifdef RV_PLUGIN_DEBUG
            printf("-> Setting chunked layout on DCPL\n");
            printf("-> Chunk dims: [ ");
            for (i = 0; i < YAJL_GET_ARRAY(chunk_dims_obj)->len; i++) {
                if (i > 0) printf(", ");
                printf("%llu", chunk_dims[i]);
            }
            printf(" ]\n");
#endif

            if (H5Pset_chunk(*DCPL, (int) YAJL_GET_ARRAY(chunk_dims_obj)->len, chunk_dims) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "can't set chunked storage layout on DCPL")
        } /* end if */
        else if (!strcmp(layout_class, "H5D_CONTIGUOUS")) {
            /* Check to see if there is any external storage information */
            if (yajl_tree_get(key_obj, external_storage_keys, yajl_t_array)) {
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_UNSUPPORTED, FAIL, "dataset external file storage is unsupported")
            } /* end if */

#ifdef RV_PLUGIN_DEBUG
            printf("-> Setting contiguous layout on DCPL\n");
#endif

            if (H5Pset_layout(*DCPL, H5D_CONTIGUOUS) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "can't set contiguous storage layout on DCPL")
        } /* end if */
        else if (!strcmp(layout_class, "H5D_COMPACT")) {
#ifdef RV_PLUGIN_DEBUG
            printf("-> Setting compact layout on DCPL\n");
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
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "track times string was NULL")

        track_times = !strcmp(track_times_str, "true");

#ifdef RV_PLUGIN_DEBUG
        printf("-> Setting track times: %s on DCPL\n", track_times ? "true" : "false");
#endif

        if (H5Pset_obj_track_times(*DCPL, track_times) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "can't set track object times property on DCPL")
    } /* end if */

done:
#ifdef RV_PLUGIN_DEBUG
    printf("\n");
#endif

    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_parse_dataset_creation_properties_callback() */


/*-------------------------------------------------------------------------
 * Function:    RV_find_object_by_path
 *
 * Purpose:     Given a pathname, this function is responsible for making
 *              HTTP GET requests to the server in order to retrieve
 *              information about an object. It is also responsible for
 *              retrieving a link's value when a pathname refers to a soft
 *              link to an object and then making the appropriate HTTP GET
 *              request using the link's value as the real pathname to the
 *              object.
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
 *              There are two main cases that have to be handled in this
 *              function, the case where the caller knows the type of the
 *              object being searched for and the case where the caller
 *              does not. In the end, both cases will make a final request
 *              to retrieve the desired information about the resulting
 *              target object, but the two cases arrive in different ways.
 *              If the type of the target object is not known, or if it
 *              is possible for there to be a soft link among the path to
 *              the target object, the target object type parameter should
 *              be passed as H5I_UNINIT. This is the safest case and should
 *              generally always be passed, unless the caller is
 *              absolutely sure that the path given points directly to an
 *              object of the given type, without soft links included
 *              along the way.
 *
 *              While the type of the target object is unknown, this
 *              function will keep locating the group that the link to the
 *              target object is contained within, resolving soft links as
 *              it is processing, until it locates the final hard link to
 *              the target object, at which point it will set the resulting
 *              type. Once the type of the target object is known, we can
 *              directly make the HTTP GET request to retrieve the object's
 *              information.
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
    RV_object_t *external_file = NULL;
    hbool_t      is_relative_path = FALSE;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    char        *path_dirname = NULL;
    char        *tmp_link_val = NULL;
    char        *url_encoded_link_name = NULL;
    char        *url_encoded_path_name = NULL;
    char         request_url[URL_MAX_LENGTH];
    long         http_response;
    int          url_len = 0;
    htri_t       ret_value = FAIL;

    if (!parent_obj)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "parent object pointer was NULL")
    if (!obj_path)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "target path was NULL")
    if (!target_object_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "target object type pointer was NULL")
    if (   H5I_FILE != parent_obj->obj_type
        && H5I_GROUP != parent_obj->obj_type
        && H5I_DATATYPE != parent_obj->obj_type
        && H5I_DATASET != parent_obj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "parent object not a file, group, datatype or dataset")

#ifdef RV_PLUGIN_DEBUG
    printf("-> Finding object by path '%s' from parent object of type %s with URI %s\n\n",
            obj_path, object_type_to_string(parent_obj->obj_type), parent_obj->URI);
#endif

    /* In order to not confuse the server, make sure the path has no leading spaces */
    while (*obj_path == ' ') obj_path++;

    /* Do a bit of pre-processing for optimizing */
    if (!strcmp(obj_path, ".")) {
        /* If the path "." is being searched for, referring to the current object, retrieve
         * the information about the current object and supply it to the optional callback.
         */

#ifdef RV_PLUGIN_DEBUG
        printf("-> Path provided was '.', short-circuiting to GET request and callback function\n\n");
#endif

        *target_object_type = parent_obj->obj_type;
        is_relative_path = TRUE;
    } /* end if */
    else if (!strcmp(obj_path, "/")) {
        /* If the path "/" is being searched for, referring to the root group, retrieve the
         * information about the root group and supply it to the optional callback.
         */

#ifdef RV_PLUGIN_DEBUG
        printf("-> Path provided was '/', short-circuiting to GET request and callback function\n\n");
#endif

        *target_object_type = H5I_GROUP;
        is_relative_path = FALSE;
    } /* end else if */
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

    /* If the target object type was specified as H5I_UNINIT and was not changed due to one of
     * the optimizations above, we must determine the target object's type before making the
     * appropriate GET request to the server. Otherwise, the target object type is known, so
     * we skip ahead to the GET request and optional callback function.
     */
    if (H5I_UNINIT == *target_object_type) {
        H5L_info_t  link_info;
        const char *ext_filename = NULL;
        const char *ext_obj_path = NULL;
        hbool_t     empty_dirname;
        htri_t      search_ret;
        char       *pobj_URI = parent_obj->URI;
        char        temp_URI[URI_MAX_LENGTH];

#ifdef RV_PLUGIN_DEBUG
        printf("-> Unknown target object type; retrieving object type\n\n");
#endif

        /* In case the user specified a path which contains multiple groups on the way to the
         * one which the object in question should be under, extract out the path to the final
         * group in the chain */
        if (NULL == (path_dirname = RV_dirname(obj_path)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't get path dirname")
        empty_dirname = !strcmp(path_dirname, "");

#ifdef RV_PLUGIN_DEBUG
        printf("-> Retrieving URI for final group in path chain\n\n");
#endif

        if (!empty_dirname) {
            H5I_type_t obj_type = H5I_GROUP;

            /* If the path to the final group in the chain wasn't empty, get the URI of the final
             * group and search for the object in question within that group. Otherwise, the
             * supplied parent group is the one that should be housing the object, so search from
             * there.
             */
            search_ret = RV_find_object_by_path(parent_obj, path_dirname, &obj_type,
                    RV_copy_object_URI_callback, NULL, temp_URI);
            if (!search_ret || search_ret < 0)
                FUNC_GOTO_ERROR(H5E_SYM, H5E_PATH, FAIL, "can't locate parent group for object of unknown type")

            pobj_URI = temp_URI;
        } /* end if */

        /* Retrieve the link for the target object from the parent group and check to see if it
         * is a hard, soft or external link. If it is a hard link, we can directly make the request
         * to retrieve the target object's information. Otherwise, we need to do some extra processing
         * to retrieve the actual path to the target object.
         */

        /* URL-encode the link name so that the resulting URL for the link GET operation doesn't
         * contain any illegal characters
         */
        if (NULL == (url_encoded_link_name = curl_easy_escape(curl, RV_basename(obj_path), 0)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode link name")

        if ((url_len = snprintf(request_url, URL_MAX_LENGTH,
                                "%s/groups/%s/links/%s",
                                base_URL,
                                pobj_URI,
                                url_encoded_link_name)
            ) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")

        if (url_len >= URL_MAX_LENGTH)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "link GET request URL size exceeded maximum URL size")

#ifdef RV_PLUGIN_DEBUG
        printf("-> Retrieving link type for link to target object of unknown type at URL %s\n\n", request_url);
#endif

        /* Setup cURL for making GET requests */

        /* Setup the host header */
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
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef RV_PLUGIN_DEBUG
        printf("   /**********************************\\\n");
        printf("-> | Making GET request to the server |\n");
        printf("   \\**********************************/\n\n");
#endif

        CURL_PERFORM(curl, H5E_LINK, H5E_PATH, FALSE);

        if (RV_parse_response(response_buffer.buffer, NULL, &link_info, RV_get_link_info_callback) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't retrieve link type")

        /* Clean up the cURL headers to prevent issues in recursive call */
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;

        if (H5L_TYPE_HARD == link_info.type) {
#ifdef RV_PLUGIN_DEBUG
            printf("-> Link was a hard link; retrieving target object's info\n\n");
#endif

            if (RV_parse_response(response_buffer.buffer, NULL, target_object_type, RV_get_link_obj_type_callback) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't retrieve hard link's target object type")
        } /* end if */
        else {
            size_t link_val_len = 0;

#ifdef RV_PLUGIN_DEBUG
            printf("-> Link was a %s link; retrieving link's value\n\n", H5L_TYPE_SOFT == link_info.type ? "soft" : "external");
#endif

            if (RV_parse_response(response_buffer.buffer, &link_val_len, NULL, RV_get_link_val_callback) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't retrieve size of link's value")

            if (NULL == (tmp_link_val = RV_malloc(link_val_len)))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate space for link's value")

            if (RV_parse_response(response_buffer.buffer, &link_val_len, tmp_link_val, RV_get_link_val_callback) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't retrieve link's value")

            if (H5L_TYPE_EXTERNAL == link_info.type) {
                /* Unpack the external link's value buffer */
                if (H5Lunpack_elink_val(tmp_link_val, link_val_len, NULL, &ext_filename, &ext_obj_path) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't unpack external link's value buffer")

                /* Attempt to open the file referenced by the external link using the same access flags as
                 * used to open the file that the link resides within.
                 */
                if (NULL == (external_file = RV_file_open(ext_filename, parent_obj->domain->u.file.intent,
                        parent_obj->domain->u.file.fapl_id, H5P_DEFAULT, NULL)))
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTOPENOBJ, FAIL, "can't open file referenced by external link")

                parent_obj = external_file;
                obj_path = ext_obj_path;
            } /* end if */
            else {
                obj_path = tmp_link_val;
            } /* end else */
        } /* end if */

        search_ret = RV_find_object_by_path(parent_obj, obj_path, target_object_type,
                obj_found_callback, callback_data_in, callback_data_out);
        if (!search_ret || search_ret < 0)
            FUNC_GOTO_ERROR(H5E_SYM, H5E_PATH, FAIL, "can't locate target object by path")

        ret_value = search_ret;
    } /* end if */
    else {
        /* Make the final HTTP GET request to retrieve information about the target object */

        /* Craft the request URL based on the type of the object we're looking for and whether or not
         * the path given is a relative path or not.
         */
        switch (*target_object_type) {
            case H5I_FILE:
            case H5I_GROUP:
                /* Handle the special case for the paths "." and "/" */
                if (!strcmp(obj_path, ".") || !strcmp(obj_path, "/")) {
                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH,
                                            "%s/groups/%s",
                                            base_URL,
                                            parent_obj->URI)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "link GET request URL size exceeded maximum URL size")
                } /* end if */
                else {
                    if (NULL == (url_encoded_path_name = RV_url_encode_path(obj_path)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode object path")

                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH,
                                            "%s/groups/%s?h5path=%s",
                                            base_URL,
                                            is_relative_path ? parent_obj->URI : "",
                                            url_encoded_path_name)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "link GET request URL size exceeded maximum URL size")
                } /* end else */

                break;

            case H5I_DATATYPE:
                /* Handle the special case for the paths "." and "/" */
                if (!strcmp(obj_path, ".") || !strcmp(obj_path, "/")) {
                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH,
                                            "%s/datatypes/%s",
                                            base_URL,
                                            parent_obj->URI)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "link GET request URL size exceeded maximum URL size")
                } /* end if */
                else {
                    if (NULL == (url_encoded_path_name = RV_url_encode_path(obj_path)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode object path")

                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH,
                                            "%s/datatypes/?%s%s%sh5path=%s",
                                            base_URL,
                                            is_relative_path ? "grpid=" : "",
                                            is_relative_path ? parent_obj->URI : "",
                                            is_relative_path ? "&" : "",
                                            url_encoded_path_name)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "link GET request URL size exceeded maximum URL size")
                } /* end else */

                break;

            case H5I_DATASET:
                /* Handle the special case for the paths "." and "/" */
                if (!strcmp(obj_path, ".") || !strcmp(obj_path, "/")) {
                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH,
                                            "%s/datasets/%s",
                                            base_URL,
                                            parent_obj->URI)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "link GET request URL size exceeded maximum URL size")
                } /* end if */
                else {
                    if (NULL == (url_encoded_path_name = RV_url_encode_path(obj_path)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode object path")

                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH,
                                            "%s/datasets/?%s%s%sh5path=%s",
                                            base_URL,
                                            is_relative_path ? "grpid=" : "",
                                            is_relative_path ? parent_obj->URI : "",
                                            is_relative_path ? "&" : "",
                                            url_encoded_path_name)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "link GET request URL size exceeded maximum URL size")
                } /* end else */

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
                FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "target object not a group, datatype or dataset")
        } /* end switch */

#ifdef RV_PLUGIN_DEBUG
        printf("-> Searching for object by URL: %s\n\n", request_url);
#endif

        /* Setup cURL for making GET requests */

        /* Setup the host header */
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
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef RV_PLUGIN_DEBUG
        printf("   /**********************************\\\n");
        printf("-> | Making GET request to the server |\n");
        printf("   \\**********************************/\n\n");
#endif

        CURL_PERFORM_NO_ERR(curl, FAIL);

        if (CURLE_OK != curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_response))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't get HTTP response code")

        ret_value = HTTP_SUCCESS(http_response);

#ifdef RV_PLUGIN_DEBUG
        printf("-> Object %s\n\n", ret_value ? "found" : "not found");
#endif

        if (ret_value > 0) {
            if (obj_found_callback && RV_parse_response(response_buffer.buffer,
                    callback_data_in, callback_data_out, obj_found_callback) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CALLBACK, FAIL, "can't perform callback operation")
        } /* end if */
    } /* end else */

done:
    if (tmp_link_val)
        RV_free(tmp_link_val);
    if (host_header)
        RV_free(host_header);
    if (url_encoded_path_name)
        RV_free(url_encoded_path_name);
    if (url_encoded_link_name)
        curl_free(url_encoded_link_name);
    if (path_dirname)
        RV_free(path_dirname);

    if (external_file)
        if (RV_file_close(external_file, H5P_DEFAULT, NULL) < 0)
            FUNC_DONE_ERROR(H5E_LINK, H5E_CANTCLOSEOBJ, FAIL, "can't close file referenced by external link")

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
    int          type_str_len = 0;
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

    if ((type_str_len = snprintf(type_name, PREDEFINED_DATATYPE_NAME_MAX_LENGTH,
             "H5T_%s_%s%zu%s",
             (type_class == H5T_INTEGER) ? "STD" : "IEEE",
             (type_class == H5T_FLOAT) ? "F" : (type_sign == H5T_SGN_NONE) ? "U" : "I",
             type_size * 8,
             (type_order == H5T_ORDER_LE) ? "LE" : "BE")
        ) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, NULL, "snprintf error")

    if (type_str_len >= PREDEFINED_DATATYPE_NAME_MAX_LENGTH)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, NULL, "predefined datatype name string size exceeded maximum size")

#ifdef RV_PLUGIN_DEBUG
    printf("-> Converted predefined datatype to string representation %s\n\n", type_name);
#endif

done:
    return ret_value;
} /* end RV_convert_predefined_datatype_to_string() */


/*-------------------------------------------------------------------------
 * Function:    RV_convert_datatype_to_JSON
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
RV_convert_datatype_to_JSON(hid_t type_id, char **type_body, size_t *type_body_len, hbool_t nested)
{
    H5T_class_t   type_class;
    const char   *leading_string = "\"type\": "; /* Leading string for all datatypes */
    ptrdiff_t     buf_ptrdiff;
    hsize_t      *array_dims = NULL;
    htri_t        type_is_committed;
    size_t        leading_string_len = strlen(leading_string);
    size_t        out_string_len;
    size_t        bytes_to_print = 0;            /* Used to calculate whether the datatype body buffer needs to be grown */
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

#ifdef RV_PLUGIN_DEBUG
    printf("-> Converting datatype to JSON\n\n");
#endif

    if (!type_body)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid NULL pointer for converted datatype's string buffer")

    out_string_len = DATATYPE_BODY_DEFAULT_SIZE;
    if (NULL == (out_string = (char *) RV_malloc(out_string_len)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for converted datatype's string buffer")

    /* Keep track of the current position in the resulting string so everything
     * gets added smoothly
     */
    out_string_curr_pos = out_string;

    /* Make sure the buffer is at least large enough to hold the leading "type" string */
    CHECKED_REALLOC(out_string, out_string_len, leading_string_len + 1,
            out_string_curr_pos, H5E_DATATYPE, FAIL);

    /* Add the leading "'type': " string */
    if (!nested) {
        strncpy(out_string, leading_string, out_string_len);
        out_string_curr_pos += leading_string_len;
    } /* end if */

    /* If the datatype is a committed type, append the datatype's URI and return */
    if ((type_is_committed = H5Tcommitted(type_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't determine if datatype is committed")

    if (type_is_committed) {
        RV_object_t *vol_obj;

#ifdef RV_PLUGIN_DEBUG
        printf("-> Datatype was a committed type\n\n");
#endif

        /* Retrieve the VOL object (RV_object_t *) from the datatype container */
        if (NULL == (vol_obj = H5VLobject(type_id)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get VOL object for committed datatype")

        /* Check whether the buffer needs to be grown */
        bytes_to_print = strlen(vol_obj->URI) + 2;

        buf_ptrdiff = out_string_curr_pos - out_string;
        if (buf_ptrdiff < 0)
            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: datatype buffer pointer difference was negative - this should not happen!")

        CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATATYPE, FAIL);

        if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t) buf_ptrdiff, "\"%s\"", vol_obj->URI)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error")

        if ((size_t) bytes_printed >= out_string_len - (size_t) buf_ptrdiff)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "datatype string size exceeded allocated buffer size")

        out_string_curr_pos += bytes_printed;

        FUNC_GOTO_DONE(SUCCEED);
    } /* end if */

#ifdef RV_PLUGIN_DEBUG
    printf("-> Datatype was not a committed type\n\n");
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

            /* Convert the class and name of the datatype to JSON */
            if (NULL == (type_name = RV_convert_predefined_datatype_to_string(type_id)))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid datatype")

            /* Check whether the buffer needs to be grown */
            bytes_to_print = (H5T_INTEGER == type_class ? strlen(int_class_str) : strlen(float_class_str))
                           + strlen(type_name) + (strlen(fmt_string) - 4) + 1;

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: datatype buffer pointer difference was negative - this should not happen!")

            CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATATYPE, FAIL);

            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t) buf_ptrdiff, fmt_string,
                    (H5T_INTEGER == type_class ? int_class_str : float_class_str), type_name)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error")

            if ((size_t) bytes_printed >= out_string_len - (size_t) buf_ptrdiff)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "datatype string size exceeded allocated buffer size")

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

                buf_ptrdiff = out_string_curr_pos - out_string;
                if (buf_ptrdiff < 0)
                    FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: datatype buffer pointer difference was negative - this should not happen!")

                CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATATYPE, FAIL);

                if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - leading_string_len,
                                              fmt_string, cset_ascii_string, nullterm_string)) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error")

                if ((size_t) bytes_printed >= out_string_len - leading_string_len)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "datatype string size exceeded allocated buffer size")

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

                buf_ptrdiff = out_string_curr_pos - out_string;
                if (buf_ptrdiff < 0)
                    FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: datatype buffer pointer difference was negative - this should not happen!")

                CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATATYPE, FAIL);

                if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - leading_string_len,
                                              fmt_string, cset_ascii_string, nullpad_string, type_size)) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error")

                if ((size_t) bytes_printed >= out_string_len - leading_string_len)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "datatype string size exceeded allocated buffer size")

                out_string_curr_pos += bytes_printed;
            } /* end else */

            break;
        } /* H5T_STRING */

        case H5T_COMPOUND:
        {
            const char         *compound_type_leading_string = "{\"class\": \"H5T_COMPOUND\", \"fields\": [";
            size_t              compound_type_leading_strlen = strlen(compound_type_leading_string);
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

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: datatype buffer pointer difference was negative - this should not happen!")

            CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + compound_type_leading_strlen + 1,
                    out_string_curr_pos, H5E_DATATYPE, FAIL);

            strncpy(out_string_curr_pos, compound_type_leading_string, compound_type_leading_strlen);
            out_string_curr_pos += compound_type_leading_strlen;

            /* For each member in the compound type, convert it into its JSON representation
             * equivalent and append it to the growing datatype string
             */
            for (i = 0; i < (size_t) nmembers; i++) {
                if ((compound_member = H5Tget_member_type(type_id, (unsigned) i)) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get compound datatype member")

#ifdef RV_PLUGIN_DEBUG
                printf("-> Converting compound datatype member %zu to JSON\n\n", i);
#endif

                if (RV_convert_datatype_to_JSON(compound_member, &compound_member_strings[i], NULL, FALSE) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL, "can't convert compound datatype member to JSON representation")

                if (NULL == (compound_member_name = H5Tget_member_name(type_id, (unsigned) i)))
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get compound datatype member name")

                /* Check whether the buffer needs to be grown */
                bytes_to_print = strlen(compound_member_name) + strlen(compound_member_strings[i])
                        + (strlen(fmt_string) - 6) + (i < (size_t) nmembers - 1 ? 2 : 0) + 1;

                buf_ptrdiff = out_string_curr_pos - out_string;
                if (buf_ptrdiff < 0)
                    FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: datatype buffer pointer difference was negative - this should not happen!")

                CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print,
                        out_string_curr_pos, H5E_DATATYPE, FAIL);

                if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t) buf_ptrdiff,
                                              fmt_string, compound_member_name, compound_member_strings[i],
                                              i < (size_t) nmembers - 1 ? ", " : "")) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error")

                if ((size_t) bytes_printed >= out_string_len - (size_t) buf_ptrdiff)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "datatype string size exceeded allocated buffer size")

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
            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: datatype buffer pointer difference was negative - this should not happen!")

            CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + 3, out_string_curr_pos, H5E_DATATYPE, FAIL);

            strcat(out_string_curr_pos, "]}");
            out_string_curr_pos += strlen("]}");

            break;
        } /* H5T_COMPOUND */

        case H5T_ENUM:
        {
            H5T_sign_t          type_sign;
            const char         *base_type_name;
            size_t              enum_mapping_length = 0;
            char               *mapping_curr_pos;
            int                 enum_nmembers;
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

            if (H5T_SGN_ERROR == (type_sign = H5Tget_sign(type_id)))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get sign of enum base datatype")

            if ((enum_nmembers = H5Tget_nmembers(type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "can't get number of members of enumerated type")

            if (NULL == (enum_value = RV_calloc(H5_SIZEOF_LONG_LONG)))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for enum member value")

            enum_mapping_length = ENUM_MAPPING_DEFAULT_SIZE;
            if (NULL == (enum_mapping = (char *) RV_malloc(enum_mapping_length)))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for enum mapping")

            /* For each member in the enum type, retrieve the member's name and value, then
             * append these to the growing datatype string
             */
            for (i = 0, mapping_curr_pos = enum_mapping; i < (size_t) enum_nmembers; i++) {
                if (NULL == (enum_value_name = H5Tget_member_name(type_id, (unsigned) i)))
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "can't get name of enum member")

                if (H5Tget_member_value(type_id, (unsigned) i, enum_value) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't retrieve value of enum member")

                /* Determine the correct cast type for the enum value buffer and then append this member's
                 * name and numeric value to the mapping list.
                 */
                if (H5T_SGN_NONE == type_sign) {
                    const char * const mapping_fmt_string = "\"%s\": %llu%s";

                    /* Check if the mapping buffer needs to grow */
                    bytes_to_print = strlen(enum_value_name) + MAX_NUM_LENGTH + (strlen(mapping_fmt_string) - 8)
                                   + (i < (size_t) enum_nmembers - 1 ? 2 : 0) + 1;

                    buf_ptrdiff = mapping_curr_pos - enum_mapping;
                    if (buf_ptrdiff < 0)
                        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: datatype buffer pointer difference was negative - this should not happen!")

                    CHECKED_REALLOC(enum_mapping, enum_mapping_length, (size_t) buf_ptrdiff + bytes_to_print,
                            mapping_curr_pos, H5E_DATATYPE, FAIL);

                    if ((bytes_printed = snprintf(mapping_curr_pos, enum_mapping_length - (size_t) buf_ptrdiff,
                                                  mapping_fmt_string, enum_value_name, *((unsigned long long int *) enum_value),
                                                  i < (size_t) enum_nmembers - 1 ? ", " : "")) < 0)
                        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error")
                } /* end if */
                else {
                    const char * const mapping_fmt_string = "\"%s\": %lld%s";

                    /* Check if the mapping buffer needs to grow */
                    bytes_to_print = strlen(enum_value_name) + MAX_NUM_LENGTH + (strlen(mapping_fmt_string) - 8)
                                   + (i < (size_t) enum_nmembers - 1 ? 2 : 0) + 1;

                    buf_ptrdiff = mapping_curr_pos - enum_mapping;
                    if (buf_ptrdiff < 0)
                        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: datatype buffer pointer difference was negative - this should not happen!")

                    CHECKED_REALLOC(enum_mapping, enum_mapping_length, (size_t) buf_ptrdiff + bytes_to_print,
                            mapping_curr_pos, H5E_DATATYPE, FAIL);

                    if ((bytes_printed = snprintf(mapping_curr_pos, enum_mapping_length - (size_t) buf_ptrdiff,
                                                  mapping_fmt_string, enum_value_name, *((long long int *) enum_value),
                                                  i < (size_t) enum_nmembers - 1 ? ", " : "")) < 0)
                        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error")
                } /* end else */

                if ((size_t) bytes_printed >= enum_mapping_length - (size_t) buf_ptrdiff)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "enum member string size exceeded allocated mapping buffer size")

                mapping_curr_pos += bytes_printed;

                if (H5free_memory(enum_value_name) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTFREE, FAIL, "can't free memory allocated for enum member name")
                enum_value_name = NULL;
            } /* end for */

            /* Retrieve the enum type's base datatype and convert it into JSON as well */
            if ((type_base_class = H5Tget_super(type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "cant get base datatype for enum type")

#ifdef RV_PLUGIN_DEBUG
            printf("-> Converting enum datatype's base datatype to JSON\n\n");
#endif

            if (NULL == (base_type_name = RV_convert_predefined_datatype_to_string(type_base_class)))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid datatype")

            /* Check whether the buffer needs to be grown */
            bytes_to_print = strlen(base_type_name) + strlen(enum_mapping) + (strlen(fmt_string) - 4) + 1;

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: datatype buffer pointer difference was negative - this should not happen!")

            CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print,
                    out_string_curr_pos, H5E_DATATYPE, FAIL);

            /* Build the Datatype body by appending the base integer type class for the enum
             * and the mapping values to map from numeric values to
             * string representations.
             */
            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t) buf_ptrdiff,
                                          fmt_string, base_type_name, enum_mapping)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error")

            if ((size_t) bytes_printed >= out_string_len - (size_t) buf_ptrdiff)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "datatype string size exceeded allocated buffer size")

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
            if (!ndims)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "0-sized array datatype")

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

                if (bytes_printed >= MAX_NUM_LENGTH)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "array dimension size string exceeded maximum number string size")

                array_shape_curr_pos += bytes_printed;
            } /* end for */

            strcat(array_shape_curr_pos, "]");

            /* Get the class and name of the base datatype */
            if ((type_base_class = H5Tget_super(type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get base datatype for array type")

            if ((type_is_committed = H5Tcommitted(type_base_class)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't determine if array base datatype is committed")

#ifdef RV_PLUGIN_DEBUG
            printf("-> Converting array datatype's base datatype to JSON\n\n");
#endif

            if (RV_convert_datatype_to_JSON(type_base_class, &array_base_type, &array_base_type_len, TRUE) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL, "can't convert datatype to JSON representation")

            /* Check whether the buffer needs to be grown */
            bytes_to_print = array_base_type_len + strlen(array_shape) + (strlen(fmt_string) - 4) + 1;

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: datatype buffer pointer difference was negative - this should not happen!")

            CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print,
                    out_string_curr_pos, H5E_DATATYPE, FAIL);

            /* Build the Datatype body by appending the array type class and base type and dimensions of the array */
            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t) buf_ptrdiff,
                                          fmt_string, array_base_type, array_shape)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error")

            if ((size_t) bytes_printed >= out_string_len - (size_t) buf_ptrdiff)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "datatype string size exceeded allocated buffer size")

            out_string_curr_pos += bytes_printed;

            break;
        } /* H5T_ARRAY */

        case H5T_BITFIELD:
        {
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "unsupported datatype - bitfield")
            break;
        } /* H5T_BITFIELD */

        case H5T_OPAQUE:
        {
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "unsupported datatype - opaque")
            break;
        } /* H5T_OPAQUE */

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

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: datatype buffer pointer difference was negative - this should not happen!")

            CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATATYPE, FAIL);

            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t) buf_ptrdiff,
                    fmt_string, is_obj_ref ? obj_ref_str : reg_ref_str)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error")

            if ((size_t) bytes_printed >= out_string_len - (size_t) buf_ptrdiff)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "datatype string size exceeded allocated buffer size")

            out_string_curr_pos += bytes_printed;

            break;
        } /* H5T_REFERENCE */

        case H5T_VLEN:
        {
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "unsupported datatype - VLEN")
            break;
        } /* H5T_VLEN */

        case H5T_TIME:
        {
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "unsupported datatype - time")
            break;
        } /* H5T_TIME */

        case H5T_NO_CLASS:
        case H5T_NCLASSES:
        default:
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADTYPE, FAIL, "invalid datatype")
    } /* end switch */

done:
    if (ret_value >= 0) {
        *type_body = out_string;
        if (type_body_len) {
            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_DONE_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: datatype buffer pointer difference was negative - this should not happen!")
            else
                *type_body_len = (size_t) buf_ptrdiff;
        } /* end if */

#ifdef RV_PLUGIN_DEBUG
        printf("-> Datatype JSON representation:\n%s\n\n", out_string);
#endif
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
} /* end RV_convert_datatype_to_JSON() */


/*-------------------------------------------------------------------------
 * Function:    RV_convert_JSON_to_datatype
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
RV_convert_JSON_to_datatype(const char *type)
{
    yajl_val   parse_tree = NULL, key_obj = NULL;
    hsize_t   *array_dims = NULL;
    size_t     i;
    hid_t      datatype = FAIL;
    hid_t     *compound_member_type_array = NULL;
    hid_t      enum_base_type = FAIL;
    char     **compound_member_names = NULL;
    char      *datatype_class = NULL;
    char      *array_base_type_substring = NULL;
    char      *tmp_cmpd_type_buffer = NULL;
    char      *tmp_enum_base_type_buffer = NULL;
    hid_t      ret_value = FAIL;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Converting JSON buffer %s to hid_t\n", type);
#endif

    /* Retrieve the datatype class */
    if (NULL == (parse_tree = yajl_tree_parse(type, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "JSON parse tree creation failed")

    if (NULL == (key_obj = yajl_tree_get(parse_tree, type_class_keys, yajl_t_string)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't parse datatype from JSON representation")

    if (NULL == (datatype_class = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't parse datatype from JSON representation")

    /* Create the appropriate datatype or copy an existing one */
    if (!strcmp(datatype_class, "H5T_INTEGER")) {
        hbool_t  is_predefined = TRUE;
        char    *type_base = NULL;

        if (NULL == (key_obj = yajl_tree_get(parse_tree, type_base_keys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve datatype's base type")

        if (NULL == (type_base = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve datatype's base type")

        if (is_predefined) {
            hbool_t  is_unsigned;
            hid_t    predefined_type = FAIL;
            char    *type_base_ptr = type_base + 8;

#ifdef RV_PLUGIN_DEBUG
            printf("-> Predefined Integer type sign: %c\n", *type_base_ptr);
#endif

            is_unsigned = (*type_base_ptr == 'U') ? TRUE : FALSE;

            switch (*(type_base_ptr + 1)) {
                /* 8-bit integer */
                case '8':
#ifdef RV_PLUGIN_DEBUG
                    printf("-> 8-bit Integer type\n");
#endif

                    if (*(type_base_ptr + 2) == 'L') {
                        /* Litle-endian */
#ifdef RV_PLUGIN_DEBUG
                        printf("-> Little-endian - %s\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U8LE : H5T_STD_I8LE;
                    } /* end if */
                    else {
#ifdef RV_PLUGIN_DEBUG
                        printf("-> Big-endian - %s\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U8BE : H5T_STD_I8BE;
                    } /* end else */

                    break;

                /* 16-bit integer */
                case '1':
#ifdef RV_PLUGIN_DEBUG
                    printf("-> 16-bit Integer type\n");
#endif

                    if (*(type_base_ptr + 3) == 'L') {
                        /* Litle-endian */
#ifdef RV_PLUGIN_DEBUG
                        printf("-> Little-endian - %s\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U16LE : H5T_STD_I16LE;
                    } /* end if */
                    else {
#ifdef RV_PLUGIN_DEBUG
                        printf("-> Big-endian - %s\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U16BE : H5T_STD_I16BE;
                    } /* end else */

                    break;

                /* 32-bit integer */
                case '3':
#ifdef RV_PLUGIN_DEBUG
                    printf("-> 32-bit Integer type\n");
#endif

                    if (*(type_base_ptr + 3) == 'L') {
                        /* Litle-endian */
#ifdef RV_PLUGIN_DEBUG
                        printf("-> Little-endian - %s\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U32LE : H5T_STD_I32LE;
                    } /* end if */
                    else {
#ifdef RV_PLUGIN_DEBUG
                        printf("-> Big-endian - %s\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U32BE : H5T_STD_I32BE;
                    } /* end else */

                    break;

                /* 64-bit integer */
                case '6':
#ifdef RV_PLUGIN_DEBUG
                    printf("-> 64-bit Integer type\n");
#endif

                    if (*(type_base_ptr + 3) == 'L') {
                        /* Litle-endian */
#ifdef RV_PLUGIN_DEBUG
                        printf("-> Little-endian - %s\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U64LE : H5T_STD_I64LE;
                    } /* end if */
                    else {
#ifdef RV_PLUGIN_DEBUG
                        printf("-> Big-endian - %s\n", is_unsigned ? "unsigned" : "signed");
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
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "non-predefined integer types are unsupported")
        } /* end else */
    } /* end if */
    else if (!strcmp(datatype_class, "H5T_FLOAT")) {
        hbool_t  is_predefined = TRUE;
        hid_t    predefined_type = FAIL;
        char    *type_base = NULL;

        if (NULL == (key_obj = yajl_tree_get(parse_tree, type_base_keys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve datatype's base type")

        if (NULL == (type_base = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve datatype's base type")

        if (is_predefined) {
            char *type_base_ptr = type_base + 10;

#ifdef RV_PLUGIN_DEBUG
            printf("-> Predefined Float type\n");
#endif

            switch (*type_base_ptr) {
                /* 32-bit floating point */
                case '3':
#ifdef RV_PLUGIN_DEBUG
                    printf("-> 32-bit Floating Point - %s\n", (*(type_base_ptr + 2) == 'L') ? "Little-endian" : "Big-endian");
#endif

                    /* Determine whether the floating point type is big- or little-endian */
                    predefined_type = (*(type_base_ptr + 2) == 'L') ? H5T_IEEE_F32LE : H5T_IEEE_F32BE;

                    break;

                /* 64-bit floating point */
                case '6':
#ifdef RV_PLUGIN_DEBUG
                    printf("-> 64-bit Floating Point - %s\n", (*(type_base_ptr + 2) == 'L') ? "Little-endian" : "Big-endian");
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
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "non-predefined floating-point types are unsupported")
        } /* end else */
    } /* end if */
    else if (!strcmp(datatype_class, "H5T_STRING")) {
        long long  fixed_length = 0;
        hbool_t    is_variable_str;
        char      *charSet = NULL;
        char      *strPad = NULL;

#ifdef RV_PLUGIN_DEBUG
        printf("-> String datatype\n");
#endif

        /* Retrieve the string datatype's length and check if it's a variable-length string */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, str_length_keys, yajl_t_any)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve string datatype's length")

        is_variable_str = YAJL_IS_STRING(key_obj);

#ifdef RV_PLUGIN_DEBUG
        printf("-> %s string\n", is_variable_str ? "Variable-length" : "Fixed-length");
#endif


        /* Retrieve and check the string datatype's character set */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, str_charset_keys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve string datatype's character set")

        if (NULL == (charSet = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve string datatype's character set")

#ifdef RV_PLUGIN_DEBUG
        printf("-> String charSet: %s\n", charSet);
#endif

        /* Currently, only H5T_CSET_ASCII character set is supported */
        if (strcmp(charSet, "H5T_CSET_ASCII"))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "unsupported character set for string datatype")

        /* Retrieve and check the string datatype's string padding */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, str_pad_keys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve string datatype's padding type")

        if (NULL == (strPad = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve string datatype's padding type")

        /* Currently, only H5T_STR_NULLPAD string padding is supported for fixed-length strings
         * and H5T_STR_NULLTERM for variable-length strings */
        if (strcmp(strPad, is_variable_str ? "H5T_STR_NULLTERM" : "H5T_STR_NULLPAD"))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "unsupported string padding type for string datatype")

#ifdef RV_PLUGIN_DEBUG
        printf("-> String padding: %s\n", strPad);
#endif

        /* Retrieve the length if the datatype is a fixed-length string */
        if (!is_variable_str) fixed_length = YAJL_GET_INTEGER(key_obj);
        if (fixed_length < 0) FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid datatype length")

        if ((datatype = H5Tcreate(H5T_STRING, is_variable_str ? H5T_VARIABLE : (size_t) fixed_length)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCREATE, FAIL, "can't create string datatype")

        if (H5Tset_cset(datatype, H5T_CSET_ASCII) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCREATE, FAIL, "can't set character set for string datatype")

        if (H5Tset_strpad(datatype, is_variable_str ? H5T_STR_NULLTERM : H5T_STR_NULLPAD) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCREATE, FAIL, "can't set string padding for string datatype")
    } /* end if */
    else if (!strcmp(datatype_class, "H5T_OPAQUE")) {
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "unsupported datatype - opaque")
    } /* end if */
    else if (!strcmp(datatype_class, "H5T_COMPOUND")) {
        ptrdiff_t  buf_ptrdiff;
        size_t     tmp_cmpd_type_buffer_size;
        size_t     total_type_size = 0;
        size_t     current_offset = 0;
        char      *type_section_ptr = NULL;
        char      *section_start, *section_end;

#ifdef RV_PLUGIN_DEBUG
        printf("-> Compound Datatype\n");
#endif

        /* Retrieve the compound member fields array */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, compound_field_keys, yajl_t_array)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve compound datatype's members array")

        if (!YAJL_GET_ARRAY(key_obj)->len)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "0-sized compound datatype members array")

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
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't get compound field member %zu information", i)

            for (j = 0; j < YAJL_GET_OBJECT(compound_member_field)->len; j++) {
                if (!strcmp(YAJL_GET_OBJECT(compound_member_field)->keys[j], "name"))
                    if (NULL == (compound_member_names[i] = YAJL_GET_STRING(YAJL_GET_OBJECT(compound_member_field)->values[j])))
                        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't get compound field member %zu name", j)
            } /* end for */
        } /* end for */

        /* For each field in the Compound Datatype's string representation, locate the beginning and end of its "type"
         * section and copy that substring into the temporary buffer. Then, convert that substring into an hid_t and
         * store it for later insertion once the Compound Datatype has been created.
         */

        /* Start the search from the "fields" JSON key */
        if (NULL == (type_section_ptr = strstr(type, "\"fields\"")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't find \"fields\" information section in datatype string")

        for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++) {
            /* Find the beginning of the "type" section for this Compound Datatype member */
            if (NULL == (type_section_ptr = strstr(type_section_ptr, "\"type\"")))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't find \"type\" information section in datatype string")

            /* Search for the initial '{' brace that begins the section */
            if (NULL == (section_start = strstr(type_section_ptr, "{")))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't find beginning '{' of \"type\" information section in datatype string - misformatted JSON likely")

            /* Continue forward through the string buffer character-by-character until the end of this JSON
             * object section is found.
             */
            FIND_JSON_SECTION_END(section_start, section_end, H5E_DATATYPE, FAIL);

            /* Check if the temporary buffer needs to grow to accomodate this "type" substring */
            buf_ptrdiff = section_end - type_section_ptr;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: datatype buffer pointer difference was negative - this should not happen!")

            CHECKED_REALLOC_NO_PTR(tmp_cmpd_type_buffer, tmp_cmpd_type_buffer_size, (size_t) buf_ptrdiff + 3, H5E_DATATYPE, FAIL);

            /* Copy the "type" substring into the temporary buffer, wrapping it in enclosing braces to ensure that the
             * string-to-datatype conversion function can correctly process the string
             */
            memcpy(tmp_cmpd_type_buffer + 1, type_section_ptr, (size_t) buf_ptrdiff);
            tmp_cmpd_type_buffer[0] = '{'; tmp_cmpd_type_buffer[(size_t) buf_ptrdiff + 1] = '}';
            tmp_cmpd_type_buffer[(size_t) buf_ptrdiff + 2] = '\0';

#ifdef RV_PLUGIN_DEBUG
            printf("-> Compound datatype member %zu name: %s\n", i, compound_member_names[i]);
            printf("-> Converting compound datatype member %zu from JSON to hid_t\n", i);
#endif

            if ((compound_member_type_array[i] = RV_convert_JSON_to_datatype(tmp_cmpd_type_buffer)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL, "can't convert compound datatype member %zu from JSON representation", i)

            total_type_size += H5Tget_size(compound_member_type_array[i]);

            /* Adjust the type section pointer so that the next search does not return the same subsection */
            type_section_ptr = section_end + 1;
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
        ptrdiff_t           buf_ptrdiff;
        size_t              type_string_len = strlen(type_string);
        char               *base_type_substring_start, *base_type_substring_end;
        hid_t               base_type_id = FAIL;

#ifdef RV_PLUGIN_DEBUG
        printf("-> Array datatype\n");
#endif

        /* Retrieve the array dimensions */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, array_dims_keys, yajl_t_array)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve array datatype's dimensions")

        if (!YAJL_GET_ARRAY(key_obj)->len)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "0-sized array")

        if (NULL == (array_dims = (hsize_t *) RV_malloc(YAJL_GET_ARRAY(key_obj)->len * sizeof(*array_dims))))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for array dimensions")

        for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++) {
            if (YAJL_IS_INTEGER(YAJL_GET_ARRAY(key_obj)->values[i]))
                array_dims[i] = (hsize_t) YAJL_GET_INTEGER(YAJL_GET_ARRAY(key_obj)->values[i]);
        } /* end for */

#ifdef RV_PLUGIN_DEBUG
        printf("-> Array datatype dimensions: [");
        for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++) {
            if (i > 0) printf(", ");
            printf("%llu", array_dims[i]);
        }
        printf("]\n");
#endif

        /* Locate the beginning and end braces of the "base" section for the array datatype */
        if (NULL == (base_type_substring_start = strstr(type, "\"base\"")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't find \"base\" type information in datatype string")
        if (NULL == (base_type_substring_start = strstr(base_type_substring_start, "{")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "incorrectly formatted \"base\" type information in datatype string")

        FIND_JSON_SECTION_END(base_type_substring_start, base_type_substring_end, H5E_DATATYPE, FAIL);

        /* Allocate enough memory to hold the "base" information substring, plus a few bytes for
         * the leading "type:" string and enclosing braces
         */
        buf_ptrdiff = base_type_substring_end - base_type_substring_start;
        if (buf_ptrdiff < 0)
            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: datatype buffer pointer difference was negative - this should not happen!")

        if (NULL == (array_base_type_substring = (char *) RV_malloc((size_t) buf_ptrdiff + type_string_len + 2)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for array base type substring")

        /* In order for the conversion function to correctly process the datatype string, it must be in the
         * form {"type": {...}}. Since the enclosing braces and the leading "type:" string are missing from
         * the substring we have extracted, add them here before processing occurs.
         */
        memcpy(array_base_type_substring, type_string, type_string_len);
        memcpy(array_base_type_substring + type_string_len, base_type_substring_start, (size_t) buf_ptrdiff);
        array_base_type_substring[type_string_len + (size_t) buf_ptrdiff] = '}';
        array_base_type_substring[type_string_len + (size_t) buf_ptrdiff + 1] = '\0';

#ifdef RV_PLUGIN_DEBUG
        printf("-> Converting array base datatype string to hid_t\n");
#endif

        /* Convert the string representation of the array's base datatype to an hid_t */
        if ((base_type_id = RV_convert_JSON_to_datatype(array_base_type_substring)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL, "can't convert JSON representation of array base datatype to a usable form")

        if ((datatype = H5Tarray_create2(base_type_id, (unsigned) i, array_dims)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCREATE, FAIL, "can't create array datatype")
    } /* end if */
    else if (!strcmp(datatype_class, "H5T_ENUM")) {
        const char * const  type_string = "{\"type\":"; /* Gets prepended to the enum "base" datatype substring */
        ptrdiff_t           buf_ptrdiff;
        size_t              type_string_len = strlen(type_string);
        char               *base_section_ptr = NULL;
        char               *base_section_end = NULL;

#ifdef RV_PLUGIN_DEBUG
        printf("-> Enum Datatype\n");
#endif

        /* Locate the beginning and end braces of the "base" section for the enum datatype */
        if (NULL == (base_section_ptr = strstr(type, "\"base\"")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "incorrectly formatted datatype string - missing \"base\" datatype section")
        if (NULL == (base_section_ptr = strstr(base_section_ptr, "{")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "incorrectly formatted \"base\" datatype section in datatype string")

        FIND_JSON_SECTION_END(base_section_ptr, base_section_end, H5E_DATATYPE, FAIL);

        /* Allocate enough memory to hold the "base" information substring, plus a few bytes for
         * the leading "type:" string and enclosing braces
         */
        buf_ptrdiff = base_section_end - base_section_ptr;
        if (buf_ptrdiff < 0)
            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: datatype buffer pointer difference was negative - this should not happen!")

        if (NULL == (tmp_enum_base_type_buffer = (char *) RV_malloc((size_t) buf_ptrdiff + type_string_len + 2)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for enum base datatype temporary buffer")

        /* In order for the conversion function to correctly process the datatype string, it must be in the
         * form {"type": {...}}. Since the enclosing braces and the leading "type:" string are missing from
         * the substring we have extracted, add them here before processing occurs.
         */
        memcpy(tmp_enum_base_type_buffer, type_string, type_string_len); /* Prepend the "type" string */
        memcpy(tmp_enum_base_type_buffer + type_string_len, base_section_ptr, (size_t) buf_ptrdiff); /* Append the "base" information substring */
        tmp_enum_base_type_buffer[type_string_len + (size_t) buf_ptrdiff] = '}';
        tmp_enum_base_type_buffer[type_string_len + (size_t) buf_ptrdiff + 1] = '\0';

#ifdef RV_PLUGIN_DEBUG
        printf("-> Converting enum base datatype string to hid_t\n");
#endif

        /* Convert the enum's base datatype substring into an hid_t for use in the following H5Tenum_create call */
        if ((enum_base_type = RV_convert_JSON_to_datatype(tmp_enum_base_type_buffer)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL, "can't convert enum datatype's base datatype section from JSON into datatype")

        if ((datatype = H5Tenum_create(enum_base_type)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCREATE, FAIL, "can't create enum datatype")

        if (NULL == (key_obj = yajl_tree_get(parse_tree, enum_mapping_keys, yajl_t_object)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve enum mapping from enum JSON representation")

        /* Retrieve the name and value of each member in the enum mapping, inserting them into the enum type as new members */
        for (i = 0; i < YAJL_GET_OBJECT(key_obj)->len; i++) {
            long long val;

            if (!YAJL_IS_INTEGER(YAJL_GET_OBJECT(key_obj)->values[i]))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "enum member %zu value is not an integer", i)

            val = YAJL_GET_INTEGER(YAJL_GET_OBJECT(key_obj)->values[i]);

            /* Convert the value from YAJL's integer representation to the base type of the enum datatype */
            if (H5Tconvert(H5T_NATIVE_LLONG, enum_base_type, 1, &val, NULL, H5P_DEFAULT) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL, "can't convert enum value to base type")

            if (H5Tenum_insert(datatype, YAJL_GET_OBJECT(key_obj)->keys[i], (void *) &val) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTINSERT, FAIL, "can't insert member into enum datatype")
        } /* end for */
    } /* end if */
    else if (!strcmp(datatype_class, "H5T_REFERENCE")) {
        char *type_base;

#ifdef RV_PLUGIN_DEBUG
        printf("-> Reference datatype\n");
#endif

        if (NULL == (key_obj = yajl_tree_get(parse_tree, type_base_keys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve datatype's base type")

        if (NULL == (type_base = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve datatype's base type")

        if (!strcmp(type_base, "H5T_STD_REF_OBJ")) {
#ifdef RV_PLUGIN_DEBUG
            printf("-> Object reference\n");
#endif

            if ((datatype = H5Tcopy(H5T_STD_REF_OBJ)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCOPY, FAIL, "can't copy object reference datatype")
        } /* end if */
        else if (!strcmp(type_base, "H5T_STD_REF_DSETREG")) {
#ifdef RV_PLUGIN_DEBUG
            printf("-> Region reference\n");
#endif

            if ((datatype = H5Tcopy(H5T_STD_REF_DSETREG)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCOPY, FAIL, "can't copy region reference datatype")
        } /* end else if */
        else
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid reference type")
    } /* end if */
    else
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "unknown datatype class")

    ret_value = datatype;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Converted JSON buffer to hid_t ID %ld\n", datatype);
#endif

done:
#ifdef RV_PLUGIN_DEBUG
    printf("\n");
#endif

    if (ret_value < 0 && datatype >= 0) {
        if (H5Tclose(datatype) < 0)
            FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, FAIL, "can't close datatype")
        if (compound_member_type_array) {
            while (FAIL != compound_member_type_array[i])
                if (H5Tclose(compound_member_type_array[i]) < 0)
                    FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, FAIL, "can't close compound datatype members")
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
} /* end RV_convert_JSON_to_datatype() */


/*-------------------------------------------------------------------------
 * Function:    RV_convert_obj_refs_to_buffer
 *
 * Purpose:     Given an array of rv_obj_ref_t structs, as well as the
 *              array's size, this function converts the array of object
 *              references into a binary buffer of object reference
 *              strings, which can then be transferred to the server.
 *
 *              Note that HDF Kita expects each element of an object
 *              reference typed dataset to be a 48-byte string, which
 *              should be enough to hold the URI of the referenced object,
 *              as well as a prefixed string corresponding to the type of
 *              the referenced object, e.g. an object reference to a group
 *              may look like
 *              "groups/g-7e538c7e-d9dd-11e7-b940-0242ac110009".
 *
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
    int     ref_string_len = 0;
    herr_t  ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Converting object ref. array to binary buffer\n\n");
#endif

    if (!ref_array)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "reference array pointer was NULL")
    if (!buf_out)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "output buffer was NULL")
    if (!buf_out_len)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "output buffer size pointer was NULL")
    if (!ref_array_len)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid reference array length specified")

    out_len = ref_array_len * OBJECT_REF_STRING_LEN;
    if (NULL == (out = (char *) RV_malloc(out_len)))
        FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_CANTALLOC, FAIL, "can't allocate space for object reference string buffer")
    out_curr_pos = out;

    for (i = 0; i < ref_array_len; i++) {
        memset(out_curr_pos, 0, OBJECT_REF_STRING_LEN);

        if (!strcmp(ref_array[i].ref_obj_URI, "")) {
            out_curr_pos += OBJECT_REF_STRING_LEN;

            continue;
        } /* end if */

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
                FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_BADVALUE, FAIL, "invalid ref obj. type")
        } /* end switch */

        if ((ref_string_len = snprintf(out_curr_pos, OBJECT_REF_STRING_LEN,
                                       "%s/%s",
                                       prefix_table[prefix_index],
                                       ref_array[i].ref_obj_URI)
            ) < 0)
            FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_SYSERRSTR, FAIL, "snprintf error")

        if (ref_string_len >= OBJECT_REF_STRING_LEN + 1)
            FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_SYSERRSTR, FAIL, "object reference string size exceeded maximum reference string size")

        out_curr_pos += OBJECT_REF_STRING_LEN;
    } /* end for */

done:
    if (ret_value >= 0) {
        *buf_out = out;
        *buf_out_len = out_len;

#ifdef RV_PLUGIN_DEBUG
        for (i = 0; i < ref_array_len; i++) {
            printf("-> Ref_array[%zu]: %s\n", i, (out + (i * OBJECT_REF_STRING_LEN)));
        } /* end for */
        printf("\n");
#endif
    } /* end if */
    else {
        if (out)
            RV_free(out);
    } /* end else */


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

#ifdef RV_PLUGIN_DEBUG
    printf("-> Converting binary buffer to ref. array\n\n");
#endif

    if (!ref_buf)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "reference string buffer was NULL")
    if (!buf_out)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "output buffer was NULL")
    if (!buf_out_len)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "output buffer size pointer was NULL")
    if (!ref_buf_len)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid reference buffer size specified")

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
        while (*URI_start && *URI_start != '/') URI_start++;

        /* Handle empty ref data */
        if (!*URI_start) {
            out[i].ref_obj_URI[0] = '\0';
            continue;
        } /* end if */

        URI_start++;

        strncpy(out[i].ref_obj_URI, URI_start, OBJECT_REF_STRING_LEN);

        /* Since the first character of the server's object URIs denotes
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

#ifdef RV_PLUGIN_DEBUG
        for (i = 0; i < ref_buf_len; i++) {
            printf("-> Ref_array[%zu]: %s\n", i, out[i].ref_obj_URI);
        } /* end for */
        printf("\n");
#endif
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
    hid_t    ret_value = FAIL;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Parsing datatype from HTTP response\n\n");
#endif

    if (!type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "datatype JSON buffer was NULL")

    if (need_truncate) {
        ptrdiff_t  buf_ptrdiff;
        char      *type_section_ptr = NULL;
        char      *type_section_start, *type_section_end;

#ifdef RV_PLUGIN_DEBUG
        printf("-> Extraneous information included in HTTP response, extracting out datatype section\n\n");
#endif

        /* Start by locating the beginning of the "type" subsection, as indicated by the JSON "type" key */
        if (NULL == (type_section_ptr = strstr(type, "\"type\"")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't find \"type\" information section in datatype string")

        /* Search for the initial '{' brace that begins the section */
        if (NULL == (type_section_start = strstr(type_section_ptr, "{")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't find beginning '{' of \"type\" information section in datatype string - misformatted JSON likely")

        /* Continue forward through the string buffer character-by-character until the end of this JSON
         * object section is found.
         */
        FIND_JSON_SECTION_END(type_section_start, type_section_end, H5E_DATATYPE, FAIL);

        buf_ptrdiff = type_section_end - type_section_ptr;
        if (buf_ptrdiff < 0)
            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: datatype buffer pointer difference was negative - this should not happen!")

        if (NULL == (type_string = (char *) RV_malloc((size_t) buf_ptrdiff + 3)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for \"type\" subsection")

        memcpy(type_string + 1, type_section_ptr, (size_t) buf_ptrdiff);

        /* Wrap the "type" substring in braces and NULL terminate it */
        type_string[0] = '{'; type_string[(size_t) buf_ptrdiff + 1] = '}';
        type_string[(size_t) buf_ptrdiff + 2] = '\0';

        substring_allocated = TRUE;
    } /* end if */

    if ((datatype = RV_convert_JSON_to_datatype(type_string)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCONVERT, FAIL, "can't convert JSON representation to datatype")

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
    yajl_val  parse_tree = NULL, key_obj = NULL;
    hsize_t  *space_dims = NULL;
    hsize_t  *space_maxdims = NULL;
    hid_t     dataspace = FAIL;
    char     *dataspace_type = NULL;
    hid_t     ret_value = FAIL;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Parsing dataspace from HTTP response\n\n");
#endif

    if (!space)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "dataspace string buffer was NULL")

    if (NULL == (parse_tree = yajl_tree_parse(space, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_PARSEERROR, FAIL, "JSON parse tree creation failed")

    /* Retrieve the Dataspace type */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, dataspace_class_keys, yajl_t_string)))
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_PARSEERROR, FAIL, "can't retrieve dataspace class")

    if (NULL == (dataspace_type = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_PARSEERROR, FAIL, "can't retrieve dataspace class")

    /* Create the appropriate type of Dataspace */
    if (!strcmp(dataspace_type, "H5S_NULL")) {
#ifdef RV_PLUGIN_DEBUG
        printf("-> NULL dataspace\n\n");
#endif

        if ((dataspace = H5Screate(H5S_NULL)) < 0)
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCREATE, FAIL, "can't create null dataspace")
    } /* end if */
    else if (!strcmp(dataspace_type, "H5S_SCALAR")) {
#ifdef RV_PLUGIN_DEBUG
        printf("-> SCALAR dataspace\n\n");
#endif

        if ((dataspace = H5Screate(H5S_SCALAR)) < 0)
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCREATE, FAIL, "can't create scalar dataspace")
    } /* end if */
    else if (!strcmp(dataspace_type, "H5S_SIMPLE")) {
        yajl_val dims_obj = NULL, maxdims_obj = NULL;
        hbool_t  maxdims_specified = TRUE;
        size_t   i;

#ifdef RV_PLUGIN_DEBUG
        printf("-> SIMPLE dataspace\n\n");
#endif

        if (NULL == (dims_obj = yajl_tree_get(parse_tree, dataspace_dims_keys, yajl_t_array)))
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_PARSEERROR, FAIL, "can't retrieve dataspace dims")

        /* Check to see whether the maximum dimension size is specified as part of the
         * dataspace's JSON representation
         */
        if (NULL == (maxdims_obj = yajl_tree_get(parse_tree, dataspace_max_dims_keys, yajl_t_array)))
            maxdims_specified = FALSE;

        if (!YAJL_GET_ARRAY(dims_obj)->len)
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "0-sized dataspace dimensionality array")

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

#ifdef RV_PLUGIN_DEBUG
        printf("-> Creating simple dataspace\n");
        printf("-> Dims: [ ");
        for (i = 0; i < dims_obj->u.array.len; i++) {
            if (i > 0) printf(", ");
            printf("%llu", space_dims[i]);
        }
        printf(" ]\n\n");
        if (maxdims_specified) {
            printf("-> MaxDims: [ ");
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
 * Function:    RV_convert_dataspace_shape_to_JSON
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
RV_convert_dataspace_shape_to_JSON(hid_t space_id, char **shape_body, char **maxdims_body)
{
    H5S_class_t  space_type;
    ptrdiff_t    buf_ptrdiff;
    hsize_t     *dims = NULL;
    hsize_t     *maxdims = NULL;
    char        *shape_out_string = NULL;
    char        *maxdims_out_string = NULL;
    herr_t       ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Converting dataspace to JSON representation\n\n");
#endif

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
    if (H5S_NULL != space_type) {
        if (maxdims_body)
            if (NULL == (maxdims_out_string = (char *) RV_malloc(DATASPACE_SHAPE_BUFFER_DEFAULT_SIZE)))
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate space for dataspace maximum dimension size buffer")
    } /* end if */

    /* Ensure that both buffers are NUL-terminated */
    if (shape_out_string) *shape_out_string = '\0';
    if (maxdims_out_string) *maxdims_out_string = '\0';

    switch (space_type) {
        case H5S_NULL:
        {
            const char * const null_str = "\"shape\": \"H5S_NULL\"";
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
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't get number of dimensions in dataspace")
            if (!space_ndims)
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "0-dimension dataspace")

            if (shape_out_string)
                if (NULL == (dims = (hsize_t *) RV_malloc((size_t) space_ndims * sizeof(*dims))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate memory for dataspace dimensions")

            if (maxdims_out_string)
                if (NULL == (maxdims = (hsize_t *) RV_malloc((size_t) space_ndims * sizeof(*dims))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate memory for dataspace maximum dimension sizes")

            if (H5Sget_simple_extent_dims(space_id, dims, maxdims) < 0)
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't retrieve dataspace dimensions and maximum dimension sizes")

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
                    buf_ptrdiff = shape_out_string_curr_pos - shape_out_string;
                    if (buf_ptrdiff < 0)
                        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataspace buffer pointer difference was negative - this should not happen!")

                    CHECKED_REALLOC(shape_out_string, shape_out_string_curr_len, (size_t) buf_ptrdiff + MAX_NUM_LENGTH + 1,
                                    shape_out_string_curr_pos, H5E_DATASPACE, FAIL);

                    if ((bytes_printed = sprintf(shape_out_string_curr_pos, "%s%llu", i > 0 ? "," : "", dims[i])) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "sprintf error")
                    shape_out_string_curr_pos += bytes_printed;
                } /* end if */

                if (maxdims_out_string) {
                    buf_ptrdiff = maxdims_out_string_curr_pos - maxdims_out_string;
                    if (buf_ptrdiff < 0)
                        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataspace buffer pointer difference was negative - this should not happen!")

                    CHECKED_REALLOC(maxdims_out_string, maxdims_out_string_curr_len, (size_t) buf_ptrdiff + MAX_NUM_LENGTH + 1,
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
                            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "sprintf error")
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
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "invalid dataspace type")
    } /* end switch */

done:
    if (ret_value >= 0) {
        if (shape_body)
            *shape_body = shape_out_string;
        if (maxdims_body)
            *maxdims_body = maxdims_out_string;

#ifdef RV_PLUGIN_DEBUG
        if (shape_out_string) printf("-> Dataspace dimensions:\n%s\n\n", shape_out_string);
        if (maxdims_out_string) printf("-> Dataspace maximum dimensions:\n%s\n\n", maxdims_out_string);
#endif
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
} /* end RV_convert_dataspace_shape_to_JSON() */


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
RV_convert_dataspace_selection_to_string(hid_t space_id,
    char **selection_string, size_t *selection_string_len, hbool_t req_param)
{
    ptrdiff_t  buf_ptrdiff;
    hsize_t   *point_list = NULL;
    hsize_t   *start = NULL;
    hsize_t   *stride = NULL;
    hsize_t   *count = NULL;
    hsize_t   *block = NULL;
    size_t     i;
    size_t     out_string_len;
    char      *out_string = NULL;
    char      *out_string_curr_pos;
    char      *start_body = NULL;
    char      *stop_body = NULL;
    char      *step_body = NULL;
    int        bytes_printed = 0;
    int        ndims;
    herr_t     ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Converting selection within dataspace to JSON\n\n");
#endif

    if (!selection_string)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "dataspace selection string was NULL")

    out_string_len = DATASPACE_SELECTION_STRING_DEFAULT_SIZE;
    if (NULL == (out_string = (char *) RV_malloc(out_string_len)))
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate space for dataspace selection string")

    out_string_curr_pos = out_string;

    /* Ensure that the buffer is NUL-terminated */
    *out_string_curr_pos = '\0';

    if (H5I_DATASPACE != H5Iget_type(space_id))
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "not a dataspace")

    if ((ndims = H5Sget_simple_extent_ndims(space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't retrieve dataspace dimensionality")
    if (!ndims)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "0-dimension dataspace specified")

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
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_UNSUPPORTED, FAIL, "point selections are unsupported as a HTTP request parameter")

            case H5S_SEL_HYPERSLABS:
            {
#ifdef RV_PLUGIN_DEBUG
                printf("-> Hyperslab selection\n\n");
#endif

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

                if (H5Sget_regular_hyperslab(space_id, start, stride, count, block) < 0)
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't get regular hyperslab selection")

                strcat(out_string_curr_pos++, "[");

                /* Append a tuple for each dimension of the dataspace */
                for (i = 0; i < (size_t) ndims; i++) {
                    buf_ptrdiff = out_string_curr_pos - out_string;
                    if (buf_ptrdiff < 0)
                        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataspace buffer pointer difference was negative - this should not happen!")

                    CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + (3 * MAX_NUM_LENGTH) + 4,
                            out_string_curr_pos, H5E_DATASPACE, FAIL);

                    if ((bytes_printed = sprintf(out_string_curr_pos,
                                                 "%s%llu:%llu:%llu",
                                                 i > 0 ? "," : "",
                                                 start[i],
                                                 start[i] + (stride[i] * (count[i] - 1)) + (block[i] - 1) + 1,
                                                 (stride[i] / block[i])
                                         )) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "sprintf error")

                    out_string_curr_pos += bytes_printed;
                } /* end for */

                buf_ptrdiff = out_string_curr_pos - out_string;
                if (buf_ptrdiff < 0)
                    FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataspace buffer pointer difference was negative - this should not happen!")

                CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + 2, out_string_curr_pos, H5E_DATASPACE, FAIL);

                strcat(out_string_curr_pos++, "]");

                break;
            } /* H5S_SEL_HYPERSLABS */

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

#ifdef RV_PLUGIN_DEBUG
                printf("-> Point selection\n\n");
#endif

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

                    /* Check whether the buffer needs to grow to accomodate the next point */
                    buf_ptrdiff = out_string_curr_pos - out_string;
                    if (buf_ptrdiff < 0)
                        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataspace buffer pointer difference was negative - this should not happen!")

                    CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + ((size_t) ((ndims * MAX_NUM_LENGTH) + (ndims) + (ndims > 1 ? 3 : 1))),
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

                buf_ptrdiff = out_string_curr_pos - out_string;
                if (buf_ptrdiff < 0)
                    FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataspace buffer pointer difference was negative - this should not happen!")

                CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + 2, out_string_curr_pos, H5E_DATASPACE, FAIL);

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

#ifdef RV_PLUGIN_DEBUG
                printf("-> Hyperslab selection\n\n");
#endif

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

                if (H5Sget_regular_hyperslab(space_id, start, stride, count, block) < 0)
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't get regular hyperslab selection")

                for (i = 0; i < (size_t) ndims; i++) {
                    if ((bytes_printed = sprintf(start_body_curr_pos, "%s%llu", (i > 0 ? "," : ""), start[i])) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "sprintf error")
                    start_body_curr_pos += bytes_printed;

                    if ((bytes_printed = sprintf(stop_body_curr_pos, "%s%llu", (i > 0 ? "," : ""), start[i] + (stride[i] * (count[i] - 1)) + (block[i] - 1) + 1)) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "sprintf error")
                    stop_body_curr_pos += bytes_printed;

                    if ((bytes_printed = sprintf(step_body_curr_pos, "%s%llu", (i > 0 ? "," : ""), (stride[i] / block[i]))) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "sprintf error")
                    step_body_curr_pos += bytes_printed;
                } /* end for */

                strcat(start_body_curr_pos, "]");
                strcat(stop_body_curr_pos, "]");
                strcat(step_body_curr_pos, "]");

                buf_ptrdiff = out_string_curr_pos - out_string;
                if (buf_ptrdiff < 0)
                    FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataspace buffer pointer difference was negative - this should not happen!")

                CHECKED_REALLOC(out_string, out_string_len,
                        (size_t) buf_ptrdiff + strlen(start_body) + strlen(stop_body) + strlen(step_body) + 1,
                        out_string_curr_pos, H5E_DATASPACE, FAIL);

                if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t) buf_ptrdiff,
                                              slab_format,
                                              start_body,
                                              stop_body,
                                              step_body
                                     )) < 0)
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "snprintf error")

                if ((size_t) bytes_printed >= out_string_len - (size_t) buf_ptrdiff)
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "dataspace string size exceeded allocated buffer size")

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
        if (selection_string_len) {
            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_DONE_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataspace buffer pointer difference was negative - this should not happen!")
            else
                *selection_string_len = (size_t) buf_ptrdiff;
        } /* end if */

#ifdef RV_PLUGIN_DEBUG
        printf("-> Dataspace selection JSON representation:\n%s\n\n", out_string);
#endif
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
    size_t       creation_properties_body_len = 0;
    size_t       create_request_nalloc = 0;
    size_t       datatype_body_len = 0;
    size_t       link_body_nalloc = 0;
    hid_t        type_id, space_id, lcpl_id;
    char        *datatype_body = NULL;
    char        *out_string = NULL;
    char        *shape_body = NULL;
    char        *maxdims_body = NULL;
    char        *creation_properties_body = NULL;
    char        *link_body = NULL;
    char        *path_dirname = NULL;
    int          create_request_len = 0;
    int          link_body_len = 0;
    herr_t       ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Setting up dataset creation request\n\n");
#endif

    if (H5I_FILE != pobj->obj_type && H5I_GROUP != pobj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "parent object not a file or group")
    if (!create_request_body)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "dataset create request output buffer was NULL")

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
    if (RV_convert_datatype_to_JSON(type_id, &datatype_body, &datatype_body_len, FALSE) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCONVERT, FAIL, "can't convert dataset's datatype to JSON representation")

    /* If the Dataspace of the Dataset was not specified as H5P_DEFAULT, parse it. */
    if (H5P_DEFAULT != space_id)
        if (RV_convert_dataspace_shape_to_JSON(space_id, &shape_body, &maxdims_body) < 0)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCREATE, FAIL, "can't convert dataset's dataspace to JSON representation")

    /* If the DCPL was not specified as H5P_DEFAULT, form the Dataset Creation Properties portion of the Dataset create request */
    if (H5P_DATASET_CREATE_DEFAULT != dcpl)
        if (RV_convert_dataset_creation_properties_to_JSON(dcpl, &creation_properties_body, &creation_properties_body_len) < 0)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCONVERT, FAIL, "can't convert Dataset Creation Properties to JSON representation")

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

#ifdef RV_PLUGIN_DEBUG
        printf("-> Creating JSON link for dataset\n\n");
#endif

        /* In case the user specified a path which contains multiple groups on the way to the
         * one which the dataset will ultimately be linked under, extract out the path to the
         * final group in the chain */
        if (NULL == (path_dirname = RV_dirname(name)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "invalid pathname for dataset link")
        empty_dirname = !strcmp(path_dirname, "");


        /* If the path to the final group in the chain wasn't empty, get the URI of the final
         * group in order to correctly link the dataset into the file structure. Otherwise,
         * the supplied parent group is the one housing the dataset, so just use its URI.
         */
        if (!empty_dirname) {
            H5I_type_t obj_type = H5I_GROUP;
            htri_t     search_ret;

            search_ret = RV_find_object_by_path(pobj, path_dirname, &obj_type,
                    RV_copy_object_URI_callback, NULL, target_URI);
            if (!search_ret || search_ret < 0)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_PATH, FAIL, "can't locate target for dataset link")
        } /* end if */

        link_body_nalloc = strlen(link_body_format) + strlen(link_basename) + (empty_dirname ? strlen(pobj->URI) : strlen(target_URI)) + 1;
        if (NULL == (link_body = (char *) RV_malloc(link_body_nalloc)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL, "can't allocate space for dataset link body")

        /* Form the Dataset Creation Link portion of the Dataset create request using the above format
         * specifier and the corresponding arguments */
        if ((link_body_len = snprintf(link_body, link_body_nalloc, link_body_format, empty_dirname ? pobj->URI : target_URI, link_basename)) < 0)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

        if ((size_t) link_body_len >= link_body_nalloc)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "dataset link create request body size exceeded allocated buffer size")
    } /* end if */

    create_request_nalloc = datatype_body_len
                          + (shape_body ? strlen(shape_body) + 2 : 0)
                          + (maxdims_body ? strlen(maxdims_body) + 2 : 0)
                          + (creation_properties_body ? creation_properties_body_len + 2 : 0)
                          + (link_body ? (size_t) link_body_len + 2 : 0)
                          + 3;

    if (NULL == (out_string = (char *) RV_malloc(create_request_nalloc)))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL, "can't allocate space for dataset creation request body")

    if ((create_request_len = snprintf(out_string, create_request_nalloc,
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

    if ((size_t) create_request_len >= create_request_nalloc)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "dataset create request body size exceeded allocated buffer size")

done:
#ifdef RV_PLUGIN_DEBUG
    printf("\n");
#endif

    if (ret_value >= 0) {
        *create_request_body = out_string;
        if (create_request_body_len) *create_request_body_len = (size_t) create_request_len;

#ifdef RV_PLUGIN_DEBUG
        printf("-> Dataset creation request JSON:\n%s\n\n", out_string);
#endif
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
 * Function:    RV_convert_dataset_creation_properties_to_JSON
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
RV_convert_dataset_creation_properties_to_JSON(hid_t dcpl, char **creation_properties_body, size_t *creation_properties_body_len)
{
    const char * const  leading_string = "\"creationProperties\": {";
    H5D_alloc_time_t    alloc_time;
    ptrdiff_t           buf_ptrdiff;
    size_t              leading_string_len = strlen(leading_string);
    size_t              bytes_to_print = 0;
    size_t              out_string_len;
    char               *chunk_dims_string = NULL;
    char               *out_string = NULL;
    char               *out_string_curr_pos; /* The "current position" pointer used to print to the appropriate place
                                                in the buffer and not overwrite important leading data */
    int                 bytes_printed = 0;
    herr_t              ret_value = SUCCEED;

#ifdef RV_PLUGIN_DEBUG
    printf("-> Converting dataset creation properties from DCPL to JSON\n\n");
#endif

    if (!creation_properties_body)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "invalid NULL pointer for dataset creation properties string buffer")

    out_string_len = DATASET_CREATION_PROPERTIES_BODY_DEFAULT_SIZE;
    if (NULL == (out_string = (char *) RV_malloc(out_string_len)))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL, "can't allocate space for dataset creation properties string buffer")

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

    buf_ptrdiff = out_string_curr_pos - out_string;
    if (buf_ptrdiff < 0)
        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

    CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

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

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

            CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t) buf_ptrdiff, fmt_string,
                    (H5P_CRT_ORDER_INDEXED | H5P_CRT_ORDER_TRACKED) == crt_order_flags ? "INDEXED" : "TRACKED")
                ) < 0)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

            if ((size_t) bytes_printed >= out_string_len - (size_t) buf_ptrdiff)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "dataset creation order property string size exceeded allocated buffer size")

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

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

            CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t) buf_ptrdiff, fmt_string, max_compact, min_dense)) < 0)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

            if ((size_t) bytes_printed >= out_string_len - (size_t) buf_ptrdiff)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "dataset attribute phase change property string size exceeded allocated buffer size")

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

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

            CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t) buf_ptrdiff, fmt_string,
                    H5D_FILL_TIME_ALLOC == fill_time ? "ALLOC" : "NEVER")
                ) < 0)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

            if ((size_t) bytes_printed >= out_string_len - (size_t) buf_ptrdiff)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "dataset fill time property string size exceeded allocated buffer size")

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

                buf_ptrdiff = out_string_curr_pos - out_string;
                if (buf_ptrdiff < 0)
                    FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

                CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

                strncat(out_string_curr_pos, null_value, null_value_len);
                out_string_curr_pos += null_value_len;
            } /* end if */
            else {
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_UNSUPPORTED, FAIL, "dataset fill values are unsupported")
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
            H5Z_filter_t       filter_id;
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

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

            CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

            strncat(out_string_curr_pos, filters_string, filters_string_len);
            out_string_curr_pos += filters_string_len;

            for (i = 0; i < (size_t) nfilters; i++) {
                /* Reset the value of cd_nelmts to make sure all of the filter's CD values are retrieved correctly */
                cd_nelmts = FILTER_MAX_CD_VALUES;

                switch ((filter_id = H5Pget_filter2(dcpl, (unsigned) i, &flags, &cd_nelmts, cd_values, filter_namelen, filter_name, &filter_config))) {
                    case H5Z_FILTER_DEFLATE:
                    {
                        const char * const fmt_string = "{"
                                                            "\"class\": \"H5Z_FILTER_DEFLATE\","
                                                            "\"id\": %d,"
                                                            "\"level\": %u"
                                                        "}";

                        /* Check whether the buffer needs to be grown */
                        bytes_to_print = strlen(fmt_string) + (2 * MAX_NUM_LENGTH) + 1;

                        buf_ptrdiff = out_string_curr_pos - out_string;
                        if (buf_ptrdiff < 0)
                            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

                        CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t) buf_ptrdiff, fmt_string,
                                H5Z_FILTER_DEFLATE,
                                cd_values[0])
                            ) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

                        if ((size_t) bytes_printed >= out_string_len - (size_t) buf_ptrdiff)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "dataset deflate filter property string size exceeded allocated buffer size")

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

                        buf_ptrdiff = out_string_curr_pos - out_string;
                        if (buf_ptrdiff < 0)
                            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

                        CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t) buf_ptrdiff, fmt_string,
                                H5Z_FILTER_SHUFFLE)
                            ) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

                        if ((size_t) bytes_printed >= out_string_len - (size_t) buf_ptrdiff)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "dataset shuffle filter property string size exceeded allocated buffer size")

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

                        buf_ptrdiff = out_string_curr_pos - out_string;
                        if (buf_ptrdiff < 0)
                            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

                        CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t) buf_ptrdiff, fmt_string,
                                H5Z_FILTER_FLETCHER32)
                            ) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

                        if ((size_t) bytes_printed >= out_string_len - (size_t) buf_ptrdiff)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "dataset fletcher32 filter property string size exceeded allocated buffer size")

                        out_string_curr_pos += bytes_printed;
                        break;
                    } /* H5Z_FILTER_FLETCHER32 */

                    case H5Z_FILTER_SZIP:
                    {
                        const char *       szip_option_mask;
                        const char * const fmt_string = "{"
                                                            "\"class\": \"H5Z_FILTER_SZIP\","
                                                            "\"id\": %d,"
                                                            "\"bitsPerPixel\": %u,"
                                                            "\"coding\": \"%s\","
                                                            "\"pixelsPerBlock\": %u,"
                                                            "\"pixelsPerScanline\": %u"
                                                        "}";

                        switch (cd_values[H5Z_SZIP_PARM_MASK]) {
                            case H5_SZIP_EC_OPTION_MASK:
                                szip_option_mask = "H5_SZIP_EC_OPTION_MASK";
                                break;

                            case H5_SZIP_NN_OPTION_MASK:
                                szip_option_mask = "H5_SZIP_NN_OPTION_MASK";
                                break;

                            default:
#ifdef RV_PLUGIN_DEBUG
                                printf("-> Unable to add SZIP filter to DCPL - unsupported mask value specified (not H5_SZIP_EC_OPTION_MASK or H5_SZIP_NN_OPTION_MASK)\n\n");
#endif

                                if (flags & H5Z_FLAG_OPTIONAL)
                                    continue;
                                else
                                    FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set SZIP filter on DCPL - unsupported mask value specified (not H5_SZIP_EC_OPTION_MASK or H5_SZIP_NN_OPTION_MASK)")

                                break;
                        } /* end switch */

                        /* Check whether the buffer needs to be grown */
                        bytes_to_print = strlen(fmt_string) + (4 * MAX_NUM_LENGTH) + strlen(szip_option_mask) + 1;

                        buf_ptrdiff = out_string_curr_pos - out_string;
                        if (buf_ptrdiff < 0)
                            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

                        CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t) buf_ptrdiff, fmt_string,
                                H5Z_FILTER_SZIP,
                                cd_values[H5Z_SZIP_PARM_BPP],
                                cd_values[H5Z_SZIP_PARM_MASK] == H5_SZIP_EC_OPTION_MASK ? "H5_SZIP_EC_OPTION_MASK" : "H5_SZIP_NN_OPTION_MASK",
                                cd_values[H5Z_SZIP_PARM_PPB],
                                cd_values[H5Z_SZIP_PARM_PPS])
                            ) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

                        if ((size_t) bytes_printed >= out_string_len - (size_t) buf_ptrdiff)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "dataset szip filter property string size exceeded allocated buffer size")

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

                        buf_ptrdiff = out_string_curr_pos - out_string;
                        if (buf_ptrdiff < 0)
                            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

                        CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t) buf_ptrdiff, fmt_string,
                                H5Z_FILTER_NBIT)
                            ) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

                        if ((size_t) bytes_printed >= out_string_len - (size_t) buf_ptrdiff)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "dataset nbit filter property string size exceeded allocated buffer size")

                        out_string_curr_pos += bytes_printed;
                        break;
                    } /* H5Z_FILTER_NBIT */

                    case H5Z_FILTER_SCALEOFFSET:
                    {
                        const char *       scaleType;
                        const char * const fmt_string = "{"
                                                            "\"class\": \"H5Z_FILTER_SCALEOFFSET\","
                                                            "\"id\": %d,"
                                                            "\"scaleType\": \"%s\","
                                                            "\"scaleOffset\": %u"
                                                        "}";

                        switch (cd_values[H5Z_SCALEOFFSET_PARM_SCALETYPE]) {
                            case H5Z_SO_FLOAT_DSCALE:
                                scaleType = "H5Z_SO_FLOAT_DSCALE";
                                break;

                            case H5Z_SO_FLOAT_ESCALE:
                                scaleType = "H5Z_SO_FLOAT_ESCALE";
                                break;

                            case H5Z_SO_INT:
                                scaleType = "H5Z_FLOAT_SO_INT";
                                break;

                            default:
#ifdef RV_PLUGIN_DEBUG
                                printf("-> Unable to add ScaleOffset filter to DCPL - unsupported scale type specified (not H5Z_SO_FLOAT_DSCALE, H5Z_SO_FLOAT_ESCALE or H5Z_SO_INT)\n\n");
#endif

                                if (flags & H5Z_FLAG_OPTIONAL)
                                    continue;
                                else
                                    FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set ScaleOffset filter on DCPL - unsupported scale type specified (not H5Z_SO_FLOAT_DSCALE, H5Z_SO_FLOAT_ESCALE or H5Z_SO_INT)")

                                break;
                        } /* end switch */

                        /* Check whether the buffer needs to be grown */
                        bytes_to_print = strlen(fmt_string) + (2 * MAX_NUM_LENGTH) + strlen(scaleType) + 1;

                        buf_ptrdiff = out_string_curr_pos - out_string;
                        if (buf_ptrdiff < 0)
                            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

                        CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t) buf_ptrdiff, fmt_string,
                                H5Z_FILTER_SCALEOFFSET,
                                scaleType,
                                cd_values[H5Z_SCALEOFFSET_PARM_SCALEFACTOR])
                            ) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

                        if ((size_t) bytes_printed >= out_string_len - (size_t) buf_ptrdiff)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "dataset scaleoffset filter property string size exceeded allocated buffer size")

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

                        buf_ptrdiff = out_string_curr_pos - out_string;
                        if (buf_ptrdiff < 0)
                            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

                        CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t) buf_ptrdiff, fmt_string,
                                LZF_FILTER_ID)
                            ) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

                        if ((size_t) bytes_printed >= out_string_len - (size_t) buf_ptrdiff)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "dataset lzf filter property string size exceeded allocated buffer size")

                        out_string_curr_pos += bytes_printed;
                        break;
                    } /* LZF_FILTER_ID */

                    case H5Z_FILTER_ERROR:
                    {
#ifdef RV_PLUGIN_DEBUG
                        printf("-> Unknown filter specified for filter %zu - not adding to DCPL\n\n", i);
#endif

                        if (flags & H5Z_FLAG_OPTIONAL)
                            continue;
                        else
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "invalid filter specified")
                    } /* H5Z_FILTER_ERROR */

                    default: /* User-defined filter */
                    {
                        char               *parameters = NULL;
                        const char * const  fmt_string = "{"
                                                             "\"class\": \"H5Z_FILTER_USER\","
                                                             "\"id\": %d,"
                                                             "\"parameters\": %s"
                                                         "}";

                        if (filter_id < 0) {
                            if (flags & H5Z_FLAG_OPTIONAL)
                                continue;
                            else
                                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "Unable to set filter on DCPL - invalid filter specified for filter %zu", i)
                        } /* end if */

                        /* Retrieve all of the parameters for the user-defined filter */

                        /* Check whether the buffer needs to be grown */
                        bytes_to_print = strlen(fmt_string) + MAX_NUM_LENGTH + strlen(parameters) + 1;

                        buf_ptrdiff = out_string_curr_pos - out_string;
                        if (buf_ptrdiff < 0)
                            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

                        CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t) buf_ptrdiff, fmt_string,
                                filter_id,
                                parameters)
                            ) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

                        if ((size_t) bytes_printed >= out_string_len - (size_t) buf_ptrdiff)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "dataset user-defined filter property string size exceeded allocated buffer size")

                        out_string_curr_pos += bytes_printed;
                        break;
                    } /* User-defined filter */
                } /* end switch */

                /* TODO: When the addition of an optional filter fails, it should use the continue statement
                 * to allow this loop to continue instead of throwing an error stack and failing the whole
                 * function. However, when this happens, a trailing comma may be left behind if the optional
                 * filter was the last one to be added. The resulting JSON may look like:
                 *
                 * [{filter},{filter},{filter},]
                 *
                 * and this currently will cause the server to return a 500 error.
                 */
                if (i < nfilters - 1) {
                    buf_ptrdiff = out_string_curr_pos - out_string;
                    if (buf_ptrdiff < 0)
                        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

                    CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + 1, out_string_curr_pos, H5E_DATASET, FAIL);

                    strcat(out_string_curr_pos++, ",");
                } /* end if */
            } /* end for */

            /* Make sure to add a closing ']' to close the 'filters' section */
            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

            CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + 1, out_string_curr_pos, H5E_DATASET, FAIL);

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

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

            CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

            strncat(out_string_curr_pos, compact_layout_str, compact_layout_str_len);
            out_string_curr_pos += compact_layout_str_len;
            break;
        } /* H5D_COMPACT */

        case H5D_CONTIGUOUS:
        {
            const char * const contiguous_layout_str = ", \"layout\": {\"class\": \"H5D_CONTIGUOUS\"";
            int                external_file_count;

            /* Check whether the buffer needs to be grown */
            bytes_to_print = strlen(contiguous_layout_str);

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

            CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

            /* Append the "contiguous layout" string */
            strncat(out_string_curr_pos, contiguous_layout_str, bytes_to_print);
            out_string_curr_pos += bytes_to_print;

            /* Determine if there are external files for the dataset */
            if ((external_file_count = H5Pget_external_count(dcpl)) < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_CANTGET, FAIL, "can't retrieve external file count")

            if (external_file_count > 0) {
                size_t             i;
                const char * const external_storage_str = ", externalStorage: [";
                const char * const external_file_str    = "%s{"
                                                              "\"name\": %s,"
                                                              "\"offset\": " OFF_T_SPECIFIER ","
                                                              "\"size\": %llu"
                                                          "}";

                /* Check whether the buffer needs to be grown */
                bytes_to_print += strlen(external_storage_str);

                buf_ptrdiff = out_string_curr_pos - out_string;
                if (buf_ptrdiff < 0)
                    FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

                CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

                /* Append the "external storage" string */
                strncat(out_string_curr_pos, external_storage_str, bytes_to_print);
                out_string_curr_pos += bytes_to_print;

                /* Append an entry for each of the external files */
                for (i = 0; i < (size_t) external_file_count; i++) {
                    hsize_t file_size;
                    off_t   file_offset;
                    char    file_name[EXTERNAL_FILE_NAME_MAX_LENGTH];

                    if (H5Pget_external(dcpl, (unsigned) i, (size_t) EXTERNAL_FILE_NAME_MAX_LENGTH, file_name, &file_offset, &file_size) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "can't get information for external file %zu from DCPL", i)

                    /* Ensure that the file name buffer is NULL-terminated */
                    file_name[EXTERNAL_FILE_NAME_MAX_LENGTH - 1] = '\0';

                    bytes_to_print += strlen(external_file_str) + strlen(file_name) + (2 * MAX_NUM_LENGTH) + (i > 0 ? 1 : 0) - 8;

                    /* Check whether the buffer needs to be grown */
                    buf_ptrdiff = out_string_curr_pos - out_string;
                    if (buf_ptrdiff < 0)
                        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

                    CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

                    if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t) buf_ptrdiff,
                            external_file_str,
                            (i > 0) ? "," : "",
                            file_name,
                            OFF_T_CAST file_offset,
                            file_size)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if ((size_t) bytes_printed >= out_string_len - (size_t) buf_ptrdiff)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "dataset external file list property string size exceeded allocated buffer size")

                    out_string_curr_pos += bytes_printed;
                } /* end for */

                /* Make sure to add a closing ']' to close the external file section */
                buf_ptrdiff = out_string_curr_pos - out_string;
                if (buf_ptrdiff < 0)
                    FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

                CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + 1, out_string_curr_pos, H5E_DATASET, FAIL);

                strcat(out_string_curr_pos++, "]");
            } /* end if */

            /* Make sure to add a closing '}' to close the 'layout' section */
            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

            CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + 1, out_string_curr_pos, H5E_DATASET, FAIL);

            strcat(out_string_curr_pos++, "}");

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

            if (!ndims)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "no chunk dimensionality specified")

            if (NULL == (chunk_dims_string = (char *) RV_malloc((size_t) ((ndims * MAX_NUM_LENGTH) + ndims + 3))))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL, "can't allocate space for chunk dimensionality string")
            chunk_dims_string_curr_pos = chunk_dims_string;
            *chunk_dims_string_curr_pos = '\0';

            strcat(chunk_dims_string_curr_pos++, "[");

            for (i = 0; i < (size_t) ndims; i++) {
                if ((bytes_printed = snprintf(chunk_dims_string_curr_pos, MAX_NUM_LENGTH, "%s%llu", i > 0 ? "," : "", chunk_dims[i])) < 0)
                    FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

                if (bytes_printed >= MAX_NUM_LENGTH)
                    FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "chunk 'dimension size' string size exceeded maximum number string size")

                chunk_dims_string_curr_pos += bytes_printed;
            } /* end for */

            strcat(chunk_dims_string_curr_pos++, "]");

            /* Check whether the buffer needs to be grown */
            bytes_to_print = strlen(fmt_string) + strlen(chunk_dims_string) + 1;

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

            CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t) buf_ptrdiff, fmt_string, chunk_dims_string)) < 0)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error")

            if ((size_t) bytes_printed >= out_string_len - (size_t) buf_ptrdiff)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "dataset chunk dimensionality property string size exceeded allocated buffer size")

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

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

            CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

            strncat(out_string_curr_pos, track_times_true, track_times_true_len);
            out_string_curr_pos += track_times_true_len;
        } /* end if */
        else {
            const char * const track_times_false = ", \"trackTimes\": \"false\"";
            size_t             track_times_false_len = strlen(track_times_false);

            /* Check whether the buffer needs to be grown */
            bytes_to_print = track_times_false_len + 1;

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

            CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + bytes_to_print, out_string_curr_pos, H5E_DATASET, FAIL);

            strncat(out_string_curr_pos, track_times_false, track_times_false_len);
            out_string_curr_pos += track_times_false_len;
        } /* end else */
    }

    /* Make sure to add a closing '}' to close the creationProperties section */
    buf_ptrdiff = out_string_curr_pos - out_string;
    if (buf_ptrdiff < 0)
        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")

    CHECKED_REALLOC(out_string, out_string_len, (size_t) buf_ptrdiff + 1, out_string_curr_pos, H5E_DATASET, FAIL);

    strcat(out_string_curr_pos++, "}");

done:
    if (ret_value >= 0) {
        *creation_properties_body = out_string;
        if (creation_properties_body_len) {
            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_DONE_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL, "unsafe cast: dataset creation properties buffer pointer difference was negative - this should not happen!")
            else
                *creation_properties_body_len = (size_t) buf_ptrdiff;
        } /* end if */

#ifdef RV_PLUGIN_DEBUG
        printf("-> DCPL JSON representation:\n%s\n\n", out_string);
#endif
    } /* end if */
    else {
        if (out_string)
            RV_free(out_string);
    } /* end else */

    if (chunk_dims_string)
        RV_free(chunk_dims_string);

    return ret_value;
} /* end RV_convert_dataset_creation_properties_to_JSON() */


/*-------------------------------------------------------------------------
 * Function:    RV_build_attr_table
 *
 * Purpose:     Given an HTTP response that contains the information about
 *              all of the attributes attached to a given object, this
 *              function builds a list of attr_table_entry structs
 *              (defined near the top of this file), one for each
 *              attribute, which each contain an attribute's name, creation
 *              time and an attribute info H5A_info_t struct.
 *
 *              This list is used during attribute iteration in order to
 *              supply the user's optional iteration callback function
 *              with all of the information it needs to process each
 *              attribute on a given object.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              January, 2018
 */
static herr_t
RV_build_attr_table(char *HTTP_response, hbool_t sort, int (*sort_func)(const void *, const void *),
    attr_table_entry **attr_table, size_t *num_entries)
{
    attr_table_entry *table = NULL;
    yajl_val          parse_tree = NULL, key_obj;
    yajl_val          attr_obj, attr_field_obj;
    size_t            i, num_attributes;
    char             *attribute_section_start, *attribute_section_end;
    herr_t            ret_value = SUCCEED;

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response was NULL")
    if (!attr_table)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "attr table pointer was NULL")
    if (!num_entries)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "attr table num. entries pointer was NULL")

#ifdef RV_PLUGIN_DEBUG
    printf("-> Building table of attributes\n\n");
#endif

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PARSEERROR, FAIL, "parsing JSON failed")

    if (NULL == (key_obj = yajl_tree_get(parse_tree, attributes_keys, yajl_t_array)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "retrieval of attributes object failed")

    num_attributes = YAJL_GET_ARRAY(key_obj)->len;
    if (num_attributes < 0) FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "number of attributes attached to object was negative")

    /* If this object has no attributes, just finish */
    if (!num_attributes)
        FUNC_GOTO_DONE(SUCCEED);

    if (NULL == (table = RV_malloc(num_attributes * sizeof(*table))))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, FAIL, "can't allocate space for attribute table")

    /* Find the beginning of the "attributes" section */
    if (NULL == (attribute_section_start = strstr(HTTP_response, "\"attributes\"")))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PARSEERROR, FAIL, "can't find \"attributes\" information section in HTTP response")

    /* For each attribute, grab its name and creation time, then find its corresponding JSON
     * subsection, place a NULL terminator at the end of it in order to "extract out" that
     * subsection, and pass it to the "get attribute info" callback function in order to fill
     * out a H5A_info_t struct for the attribute.
     */
    for (i = 0; i < num_attributes; i++) {
        char *attr_name;

        attr_obj = YAJL_GET_ARRAY(key_obj)->values[i];

        /* Get the current attribute's name */
        if (NULL == (attr_field_obj = yajl_tree_get(attr_obj, attr_name_keys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "retrieval of attribute name failed")

        if (NULL == (attr_name = YAJL_GET_STRING(attr_field_obj)))
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "returned attribute name was NULL")

        strncpy(table[i].attr_name, attr_name, ATTRIBUTE_NAME_MAX_LENGTH);

        /* Get the current attribute's creation time */
        if (NULL == (attr_field_obj = yajl_tree_get(attr_obj, attr_creation_time_keys, yajl_t_number)))
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "retrieval of attribute creation time failed")

        if (!YAJL_IS_DOUBLE(attr_field_obj))
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "returned attribute creation time is not a double")

        table[i].crt_time = YAJL_GET_DOUBLE(attr_field_obj);

        /* Process the JSON for the current attribute and fill out a H5A_info_t struct for it */

        /* Find the beginning and end of the JSON section for this attribute */
        if (NULL == (attribute_section_start = strstr(attribute_section_start, "{")))
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_PARSEERROR, FAIL, "can't find start of current attribute's JSON section")

        /* Continue forward through the string buffer character-by-character until the end of this JSON
         * object section is found.
         */
        FIND_JSON_SECTION_END(attribute_section_start, attribute_section_end, H5E_ATTR, FAIL);

        /* Since it is not important if we destroy the contents of the HTTP response buffer,
         * NULL terminators will be placed in the buffer strategically at the end of each attribute
         * subsection (in order to "extract out" that subsection) corresponding to each individual
         * attribute, and pass it to the "get attribute info" callback.
         */
        *attribute_section_end = '\0';

        /* Fill out a H5A_info_t struct for this attribute */
        if (RV_parse_response(attribute_section_start, NULL, &table[i].attr_info, RV_get_attr_info_callback) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "couldn't get link info")

        /* Continue on to the next attribute subsection */
        attribute_section_start = attribute_section_end + 1;
    } /* end for */

#ifdef RV_PLUGIN_DEBUG
    printf("-> Attribute table built\n\n");
#endif

    if (sort) qsort(table, num_attributes, sizeof(*table), sort_func);

done:
    if (ret_value >= 0) {
        if (attr_table)
            *attr_table = table;
        if (num_entries)
            *num_entries = num_attributes;
    } /* end if */

    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_build_attr_table() */


/*-------------------------------------------------------------------------
 * Function:    RV_traverse_attr_table
 *
 * Purpose:     Helper function to actually iterate over an attribute
 *              table, calling the user's callback for each attribute
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              January, 2018
 */
static herr_t
RV_traverse_attr_table(attr_table_entry *attr_table, size_t num_entries, iter_data *attr_iter_data)
{
    size_t last_idx;
    herr_t callback_ret;
    herr_t ret_value = SUCCEED;

    switch (attr_iter_data->iter_order) {
        case H5_ITER_NATIVE:
        case H5_ITER_INC:
        {
#ifdef RV_PLUGIN_DEBUG
            printf("-> Beginning iteration in increasing order\n\n");
#endif

            for (last_idx = (attr_iter_data->idx_p ? *attr_iter_data->idx_p : 0); last_idx < num_entries; last_idx++) {
#ifdef RV_PLUGIN_DEBUG
                printf("-> Attribute %zu name: %s\n", last_idx, attr_table[last_idx].attr_name);
                printf("-> Attribute %zu creation time: %f\n", last_idx, attr_table[last_idx].crt_time);
                printf("-> Attribute %zu data size: %llu\n\n", last_idx, attr_table[last_idx].attr_info.data_size);

                printf("-> Calling supplied callback function\n\n");
#endif

                /* Call the user's callback */
                callback_ret = attr_iter_data->iter_function.attr_iter_op(attr_iter_data->iter_obj_id, attr_table[last_idx].attr_name, &attr_table[last_idx].attr_info, attr_iter_data->op_data);
                if (callback_ret < 0)
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_CALLBACK, callback_ret, "H5Aiterate (_by_name) user callback failed for attribute '%s'", attr_table[last_idx].attr_name)
                else if (callback_ret > 0)
                    FUNC_GOTO_DONE(callback_ret)
            } /* end for */

            break;
        } /* H5_ITER_NATIVE H5_ITER_INC */

        case H5_ITER_DEC:
        {
#ifdef RV_PLUGIN_DEBUG
            printf("-> Beginning iteration in decreasing order\n\n");
#endif

            for (last_idx = (attr_iter_data->idx_p ? *attr_iter_data->idx_p : num_entries - 1); last_idx >= 0; last_idx--) {
#ifdef RV_PLUGIN_DEBUG
                printf("-> Attribute %zu name: %s\n", last_idx, attr_table[last_idx].attr_name);
                printf("-> Attribute %zu creation time: %f\n", last_idx, attr_table[last_idx].crt_time);
                printf("-> Attribute %zu data size: %llu\n\n", last_idx, attr_table[last_idx].attr_info.data_size);

                printf("-> Calling supplied callback function\n\n");
#endif

                /* Call the user's callback */
                callback_ret = attr_iter_data->iter_function.attr_iter_op(attr_iter_data->iter_obj_id, attr_table[last_idx].attr_name, &attr_table[last_idx].attr_info, attr_iter_data->op_data);
                if (callback_ret < 0)
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_CALLBACK, callback_ret, "H5Aiterate (_by_name) user callback failed for attribute '%s'", attr_table[last_idx].attr_name)
                else if (callback_ret > 0)
                    FUNC_GOTO_DONE(callback_ret)

                if (last_idx == 0) break;
            } /* end for */

            break;
        } /* H5_ITER_DEC */

        case H5_ITER_UNKNOWN:
        case H5_ITER_N:
        default:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "unknown attribute iteration order")
    } /* end switch */

#ifdef RV_PLUGIN_DEBUG
    printf("-> Attribute iteration finished\n\n");
#endif

done:
    return ret_value;
} /* end RV_traverse_attr_table() */


/*-------------------------------------------------------------------------
 * Function:    RV_build_link_table
 *
 * Purpose:     Given an HTTP response that contains the information about
 *              all of the links contained within a given group, this
 *              function builds a list of link_table_entry structs
 *              (defined near the top of this file), one for each link,
 *              which each contain a link's name, creation time and a link
 *              info H5L_info_t struct.
 *
 *              Each link_table_entry struct may additionally contain a
 *              pointer to another link table in the case that the link in
 *              question points to a subgroup of the parent group and a
 *              call to H5Lvisit has been made. H5Lvisit visits all the
 *              links in the given object and its subgroups, as opposed to
 *              H5Literate which only iterates over the links in the given
 *              group.
 *
 *              This list is used during link iteration in order to supply
 *              the user's optional iteration callback function with all
 *              of the information it needs to process each link contained
 *              within a group (for H5Literate) or within a group and all
 *              of its subgroups (for H5Lvisit).
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              January, 2018
 */
static herr_t
RV_build_link_table(char *HTTP_response, hbool_t is_recursive, int (*sort_func)(const void *, const void *),
    link_table_entry **link_table, size_t *num_entries, rv_hash_table_t *visited_link_table)
{
    link_table_entry *table = NULL;
    yajl_val          parse_tree = NULL, key_obj;
    yajl_val          link_obj, link_field_obj;
    size_t            i, num_links;
    char             *HTTP_buffer = HTTP_response;
    char             *visit_buffer = NULL;
    char             *link_section_start, *link_section_end;
    char             *url_encoded_link_name = NULL;
    char              request_url[URL_MAX_LENGTH];
    herr_t            ret_value = SUCCEED;

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response was NULL")
    if (!link_table)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "link table pointer was NULL")
    if (!num_entries)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "link table num. entries pointer was NULL")
    if (is_recursive && !visited_link_table)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "visited link hash table was NULL")

#ifdef RV_PLUGIN_DEBUG
    printf("-> Building table of links %s\n\n", is_recursive ? "recursively" : "non-recursively");
#endif

    /* If this is a call to H5Lvisit, make a copy of the HTTP response since the
     * buffer that cURL writes to is currently global and will be changed when the
     * next request is made to the server when recursing into a subgroup to iterate
     * over its links.
     */
    if (is_recursive) {
        size_t buffer_len = strlen(HTTP_response);

        if (NULL == (visit_buffer = RV_malloc(buffer_len + 1)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate temporary buffer for H5Lvisit")

        memcpy(visit_buffer, HTTP_response, buffer_len);
        visit_buffer[buffer_len] = '\0';

        HTTP_buffer = visit_buffer;
    } /* end if */

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_buffer, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_PARSEERROR, FAIL, "parsing JSON failed")

    if (NULL == (key_obj = yajl_tree_get(parse_tree, links_keys, yajl_t_array)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "retrieval of links object failed")

    num_links = YAJL_GET_ARRAY(key_obj)->len;
    if (num_links < 0) FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "number of links in group was negative")

    /* If this group has no links, leave its sub-table alone */
    if (!num_links)
        FUNC_GOTO_DONE(SUCCEED);

    /* Build a table of link information for each link so that we can sort in order
     * of link creation if needed and can also work in decreasing order if desired
     */
    if (NULL == (table = RV_malloc(num_links * sizeof(*table))))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate space for link table")

    /* Find the beginning of the "links" section */
    if (NULL == (link_section_start = strstr(HTTP_buffer, "\"links\"")))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_PARSEERROR, FAIL, "can't find \"links\" information section in HTTP response")

    /* For each link, grab its name and creation order, then find its corresponding JSON
     * subsection, place a NULL terminator at the end of it in order to "extract out" that
     * subsection, and pass it to the "get link info" callback function in order to fill
     * out a H5L_info_t struct for the link.
     */
    for (i = 0; i < num_links; i++) {
        char *link_name;

        link_obj = YAJL_GET_ARRAY(key_obj)->values[i];

        /* Get the current link's name */
        if (NULL == (link_field_obj = yajl_tree_get(link_obj, link_title_keys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "retrieval of link name failed")

        if (NULL == (link_name = YAJL_GET_STRING(link_field_obj)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "returned link name was NULL")

        strncpy(table[i].link_name, link_name, LINK_NAME_MAX_LENGTH);

        /* Get the current link's creation time */
        if (NULL == (link_field_obj = yajl_tree_get(link_obj, link_creation_time_keys, yajl_t_number)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "retrieval of link creation time failed")

        if (!YAJL_IS_DOUBLE(link_field_obj))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "returned link creation time is not a double")

        table[i].crt_time = YAJL_GET_DOUBLE(link_field_obj);

        /* Process the JSON for the current link and fill out a H5L_info_t struct for it */

        /* Find the beginning and end of the JSON section for this link */
        if (NULL == (link_section_start = strstr(link_section_start, "{")))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_PARSEERROR, FAIL, "can't find start of current link's JSON section")

        /* Continue forward through the string buffer character-by-character until the end of this JSON
         * object section is found.
         */
        FIND_JSON_SECTION_END(link_section_start, link_section_end, H5E_LINK, FAIL);

        /* Since it is not important if we destroy the contents of the HTTP response buffer,
         * NULL terminators will be placed in the buffer strategically at the end of each link
         * subsection (in order to "extract out" that subsection) corresponding to each individual
         * link, and pass it to the "get link info" callback.
         */
        *link_section_end = '\0';

        /* Fill out a H5L_info_t struct for this link */
        if (RV_parse_response(link_section_start, NULL, &table[i].link_info, RV_get_link_info_callback) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "couldn't get link info")

        /*
         * If this is a call to H5Lvisit and the current link points to a group, hash the link object ID and
         * check to see if the key exists in the visited link hash table. If it does, this is a cyclic
         * link, so do not include it in the list of links. Otherwise, add it to the visited link hash
         * table and recursively process the group, building a link table for it as well.
         */
        table[i].subgroup.subgroup_link_table = NULL;
        if (is_recursive && (H5L_TYPE_HARD == table[i].link_info.type)) {
            char *link_collection;
            int   url_len = 0;

            if (NULL == (link_field_obj = yajl_tree_get(link_obj, link_collection_keys2, yajl_t_string)))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "retrieval of link collection failed")

            if (NULL == (link_collection = YAJL_GET_STRING(link_field_obj)))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "returned link collection was NULL")

            if (!strcmp(link_collection, "groups")) {
                char *link_id;

                /* Retrieve the ID of the current link */
                if (NULL == (link_field_obj = yajl_tree_get(link_obj, object_id_keys, yajl_t_string)))
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "retrieval of link ID failed")

                if (NULL == (link_id = YAJL_GET_STRING(link_field_obj)))
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "returned link ID was NULL")

                /* Check if this link has been visited already before processing it */
                if (RV_HASH_TABLE_NULL == rv_hash_table_lookup(visited_link_table, link_id)) {
                    size_t link_id_len = strlen(link_id);
                    char*  link_id_copy;

                    /* Make a copy of the key and add it to the hash table to prevent future cyclic links from being visited */
                    if (NULL == (link_id_copy = RV_malloc(link_id_len + 1)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "unable to allocate space for key in visited link hash table")

                    strncpy(link_id_copy, link_id, link_id_len);
                    link_id_copy[link_id_len] = '\0';

                    if (!rv_hash_table_insert(visited_link_table, link_id_copy, link_id_copy))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTINSERT, FAIL, "unable to insert key into visited link hash table")

                    /* Make a GET request to the server to retrieve all of the links in the subgroup */

                    /* URL-encode the name of the link to ensure that the resulting URL for the link
                     * iteration operation doesn't contain any illegal characters
                     */
                    if (NULL == (url_encoded_link_name = curl_easy_escape(curl, RV_basename(YAJL_GET_STRING(link_field_obj)), 0)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode link name")

                    if ((url_len = snprintf(request_url,
                                            URL_MAX_LENGTH,
                                            "%s/groups/%s/links",
                                            base_URL,
                                            url_encoded_link_name)
                        ) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "link GET request URL size exceeded maximum URL size")

                    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf)

#ifdef RV_PLUGIN_DEBUG
                    printf("-> Retrieving all links in subgroup using URL: %s\n\n", request_url);

                    printf("   /**********************************\\\n");
                    printf("-> | Making GET request to the server |\n");
                    printf("   \\**********************************/\n\n");
#endif

                    CURL_PERFORM(curl, H5E_LINK, H5E_CANTGET, FAIL);

                    if (RV_build_link_table(response_buffer.buffer, is_recursive, sort_func,
                            &table[i].subgroup.subgroup_link_table, &table[i].subgroup.num_entries, visited_link_table) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTBUILDLINKTABLE, FAIL, "can't build link table for subgroup '%s'", table[i].link_name)

                    curl_free(url_encoded_link_name);
                    url_encoded_link_name = NULL;
                } /* end if */
#ifdef RV_PLUGIN_DEBUG
                else {
                    printf("-> Cyclic link detected; not following into subgroup\n\n");
                } /* end else */
#endif
            } /* end if */
        } /* end if */

        /* Continue on to the next link subsection */
        link_section_start = link_section_end + 1;
    } /* end for */

#ifdef RV_PLUGIN_DEBUG
    printf("-> Link table built\n\n");
#endif

    if (sort_func) qsort(table, num_links, sizeof(*table), sort_func);

done:
    if (ret_value >= 0) {
        if (link_table)
            *link_table = table;
        if (num_entries)
            *num_entries = num_links;
    } /* end if */

    if (url_encoded_link_name)
        curl_free(url_encoded_link_name);
    if (parse_tree)
        yajl_tree_free(parse_tree);
    if (visit_buffer)
        RV_free(visit_buffer);

    return ret_value;
} /* end RV_build_link_table() */


/*-------------------------------------------------------------------------
 * Function:    RV_free_link_table
 *
 * Purpose:     Helper function to free a built up link table, freeing its
 *              individual subgroup link tables as necessary
 *
 * Return:      Nothing
 *
 * Programmer:  Jordan Henderson
 *              January, 2018
 */
static void
RV_free_link_table(link_table_entry *link_table, size_t num_entries)
{
    size_t i;

    for (i = 0; i < num_entries; i++) {
        if (link_table[i].subgroup.subgroup_link_table)
            RV_free_link_table(link_table[i].subgroup.subgroup_link_table, link_table[i].subgroup.num_entries);
    } /* end for */

    RV_free(link_table);
} /* end RV_free_link_table() */


/*-------------------------------------------------------------------------
 * Function:    RV_traverse_link_table
 *
 * Purpose:     Helper function to actually iterate over a link
 *              table, calling the user's callback for each link
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              January, 2018
 */
static herr_t
RV_traverse_link_table(link_table_entry *link_table, size_t num_entries, iter_data *link_iter_data, const char *cur_link_rel_path)
{
    static size_t  depth = 0;
    size_t         last_idx;
    herr_t         callback_ret;
    size_t         link_rel_path_len = (cur_link_rel_path ? strlen(cur_link_rel_path) : 0) + LINK_NAME_MAX_LENGTH + 2;
    char          *link_rel_path = NULL;
    int            snprintf_ret = 0;
    herr_t         ret_value = SUCCEED;

    if (NULL == (link_rel_path = (char *) RV_malloc(link_rel_path_len)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate space for link's relative pathname buffer")

    switch (link_iter_data->iter_order) {
        case H5_ITER_NATIVE:
        case H5_ITER_INC:
        {
#ifdef RV_PLUGIN_DEBUG
            printf("-> Beginning iteration in increasing order\n\n");
#endif

            for (last_idx = (link_iter_data->idx_p ? *link_iter_data->idx_p : 0); last_idx < num_entries; last_idx++) {
#ifdef RV_PLUGIN_DEBUG
                printf("-> Link %zu name: %s\n", last_idx, link_table[last_idx].link_name);
                printf("-> Link %zu creation time: %f\n", last_idx, link_table[last_idx].crt_time);
                printf("-> Link %zu type: %s\n\n", last_idx, link_class_to_string(link_table[last_idx].link_info.type));
#endif

                /* Form the link's relative path from the parent group by combining the current relative path with the link's name */
                if ((snprintf_ret = snprintf(link_rel_path, link_rel_path_len,
                         "%s%s%s",
                         cur_link_rel_path ? cur_link_rel_path : "",
                         cur_link_rel_path ? "/" : "",
                         link_table[last_idx].link_name)
                    ) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")

                if ((size_t) snprintf_ret >= link_rel_path_len)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "link's relative path string size exceeded allocated buffer size")

#ifdef RV_PLUGIN_DEBUG
                printf("-> Calling supplied callback function with relative link path %s\n\n", link_rel_path);
#endif

                /* Call the user's callback */
                callback_ret = link_iter_data->iter_function.link_iter_op(link_iter_data->iter_obj_id, link_rel_path, &link_table[last_idx].link_info, link_iter_data->op_data);
                if (callback_ret < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CALLBACK, callback_ret, "H5Literate/H5Lvisit (_by_name) user callback failed for link '%s'", link_table[last_idx].link_name)
                else if (callback_ret > 0)
                    FUNC_GOTO_DONE(callback_ret)

                /* If this is a group and H5Lvisit has been called, descend into the group */
                if (link_table[last_idx].subgroup.subgroup_link_table) {
#ifdef RV_PLUGIN_DEBUG
                    printf("-> Descending into subgroup '%s'\n\n", link_table[last_idx].link_name);
#endif

                    depth++;
                    if (RV_traverse_link_table(link_table[last_idx].subgroup.subgroup_link_table, link_table[last_idx].subgroup.num_entries, link_iter_data, link_rel_path) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_LINKITERERROR, FAIL, "can't iterate over links in subgroup '%s'", link_table[last_idx].link_name)
                    depth--;

#ifdef RV_PLUGIN_DEBUG
                    printf("-> Exiting subgroup '%s'\n\n", link_table[last_idx].link_name);
#endif
                } /* end if */
                else {
                    char *last_slash = strrchr(link_rel_path, '/');

                    /* Truncate the relative path buffer by cutting off the trailing link name from the current path chain */
                    if (last_slash) *last_slash = '\0';

#ifdef RV_PLUGIN_DEBUG
                    printf("-> Relative link path after truncating trailing link name: %s\n\n", link_rel_path);
#endif
                } /* end else */
            } /* end for */

            break;
        } /* H5_ITER_NATIVE H5_ITER_INC */

        case H5_ITER_DEC:
        {
#ifdef RV_PLUGIN_DEBUG
            printf("-> Beginning iteration in decreasing order\n\n");
#endif

            for (last_idx = (link_iter_data->idx_p ? *link_iter_data->idx_p : num_entries - 1); last_idx >= 0; last_idx--) {
#ifdef RV_PLUGIN_DEBUG
                printf("-> Link %zu name: %s\n", last_idx, link_table[last_idx].link_name);
                printf("-> Link %zu creation time: %f\n", last_idx, link_table[last_idx].crt_time);
                printf("-> Link %zu type: %s\n\n", last_idx, link_class_to_string(link_table[last_idx].link_info.type));
#endif

                /* Form the link's relative path from the parent group by combining the current relative path with the link's name */
                if ((snprintf_ret = snprintf(link_rel_path, link_rel_path_len, "%s%s%s",
                         cur_link_rel_path ? cur_link_rel_path : "",
                         cur_link_rel_path ? "/" : "",
                         link_table[last_idx].link_name)
                    ) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error")

                if ((size_t) snprintf_ret >= link_rel_path_len)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "link's relative path string size exceeded allocated buffer size")

#ifdef RV_PLUGIN_DEBUG
                printf("-> Calling supplied callback function with relative link path %s\n\n", link_rel_path);
#endif

                /* Call the user's callback */
                callback_ret = link_iter_data->iter_function.link_iter_op(link_iter_data->iter_obj_id, link_rel_path, &link_table[last_idx].link_info, link_iter_data->op_data);
                if (callback_ret < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CALLBACK, callback_ret, "H5Literate/H5Lvisit (_by_name) user callback failed for link '%s'", link_table[last_idx].link_name)
                else if (callback_ret > 0)
                    FUNC_GOTO_DONE(callback_ret)

                /* If this is a group and H5Lvisit has been called, descend into the group */
                if (link_table[last_idx].subgroup.subgroup_link_table) {
#ifdef RV_PLUGIN_DEBUG
                    printf("-> Descending into subgroup '%s'\n\n", link_table[last_idx].link_name);
#endif

                    depth++;
                    if (RV_traverse_link_table(link_table[last_idx].subgroup.subgroup_link_table, link_table[last_idx].subgroup.num_entries, link_iter_data, link_rel_path) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_LINKITERERROR, FAIL, "can't iterate over links in subgroup '%s'", link_table[last_idx].link_name)
                    depth--;

#ifdef RV_PLUGIN_DEBUG
                    printf("-> Exiting subgroup '%s'\n\n", link_table[last_idx].link_name);
#endif
                } /* end if */
                else {
                    char *last_slash = strrchr(link_rel_path, '/');

                    /* Truncate the relative path buffer by cutting off the trailing link name from the current path chain */
                    if (last_slash) *last_slash = '\0';

#ifdef RV_PLUGIN_DEBUG
                    printf("-> Relative link path after truncating trailing link name: %s\n\n", link_rel_path);
#endif
                } /* end else */

                if (last_idx == 0) break;
            } /* end for */

            break;
        } /* H5_ITER_DEC */

        case H5_ITER_UNKNOWN:
        case H5_ITER_N:
        default:
            FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "unknown link iteration order")
    } /* end switch */

#ifdef RV_PLUGIN_DEBUG
    if (depth == 0)
        printf("-> Link iteration finished\n\n");
#endif

done:
    /* Keep track of the last index where we left off */
    if (link_iter_data->idx_p && (ret_value >= 0) && (depth == 0))
        *link_iter_data->idx_p = last_idx;

    if (link_rel_path)
        RV_free(link_rel_path);

    return ret_value;
} /* end RV_traverse_link_table() */


/*-------------------------------------------------------------------------
 * Function:    RV_free_visited_link_hash_table_key
 *
 * Purpose:     Helper function to free keys in the visited link hash table
 *              used by link iteration.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              June, 2018
 */
static void
RV_free_visited_link_hash_table_key(rv_hash_table_key_t value)
{
    value = RV_free(value);
}

#ifdef RV_PLUGIN_DEBUG

/*-------------------------------------------------------------------------
 * Function:    object_type_to_string
 *
 * Purpose:     Helper function to convert an object's type into a string
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static const char*
object_type_to_string(H5I_type_t obj_type)
{
    switch (obj_type) {
        case H5I_UNINIT:      return "H5I_UNINIT";
        case H5I_BADID:       return "H5I_BADID";
        case H5I_FILE:        return "H5I_FILE";
        case H5I_GROUP:       return "H5I_GROUP";
        case H5I_DATATYPE:    return "H5I_DATATYPE";
        case H5I_DATASPACE:   return "H5I_DATASPACE";
        case H5I_DATASET:     return "H5I_DATASET";
        case H5I_ATTR:        return "H5I_ATTR";
        case H5I_REFERENCE:   return "H5I_REFERENCE";
        case H5I_VFL:         return "H5I_VFL";
        case H5I_VOL:         return "H5I_VOL";
        case H5I_GENPROP_CLS: return "H5I_GENPROP_CLS";
        case H5I_GENPROP_LST: return "H5I_GENPROP_LST";
        case H5I_ERROR_CLASS: return "H5I_ERROR_CLASS";
        case H5I_ERROR_MSG:   return "H5I_ERROR_MSG";
        case H5I_ERROR_STACK: return "H5I_ERROR_STACK";
        case H5I_NTYPES:      return "H5I_NTYPES";
        default:              return "(unknown)";
    } /* end switch */
} /* end object_type_to_string() */


/*-------------------------------------------------------------------------
 * Function:    object_type_to_string2
 *
 * Purpose:     Helper function to convert an object's type into a string
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static const char*
object_type_to_string2(H5O_type_t obj_type)
{
    switch (obj_type) {
        case H5O_TYPE_UNKNOWN:        return "H5O_TYPE_UNKNOWN";
        case H5O_TYPE_GROUP:          return "H5O_TYPE_GROUP";
        case H5O_TYPE_DATASET:        return "H5O_TYPE_DATASET";
        case H5O_TYPE_NAMED_DATATYPE: return "H5O_TYPE_NAMED_DATATYPE";
        case H5O_TYPE_NTYPES:         return "H5O_TYPE_NTYPES";
        default:                      return "(unknown)";
    } /* end switch */
} /* end object_type_to_string2() */


/*-------------------------------------------------------------------------
 * Function:    datatype_class_to_string
 *
 * Purpose:     Helper function to convert a datatype's class into a string
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static const char*
datatype_class_to_string(hid_t dtype)
{
    switch (H5Tget_class(dtype)) {
        case H5T_NO_CLASS:  return "H5T_NO_CLASS";
        case H5T_INTEGER:   return "H5T_INTEGER";
        case H5T_FLOAT:     return "H5T_FLOAT";
        case H5T_TIME:      return "H5T_TIME";
        case H5T_STRING:    return "H5T_STRING";
        case H5T_BITFIELD:  return "H5T_BITFIELD";
        case H5T_OPAQUE:    return "H5T_OPAQUE";
        case H5T_COMPOUND:  return "H5T_COMPOUND";
        case H5T_REFERENCE: return "H5T_REFERENCE";
        case H5T_ENUM:      return "H5T_ENUM";
        case H5T_VLEN:      return "H5T_VLEN";
        case H5T_ARRAY:     return "H5T_ARRAY";
        case H5T_NCLASSES:  return "H5T_NCLASSES";
        default:            return "(unknown)";
    } /* end switch */
} /* end datatype_class_to_string() */


/*-------------------------------------------------------------------------
 * Function:    link_class_to_string
 *
 * Purpose:     Helper function to convert a link's class into a string
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static const char*
link_class_to_string(H5L_type_t link_type)
{
    switch (link_type) {
        case H5L_TYPE_ERROR:    return "H5L_TYPE_ERROR";
        case H5L_TYPE_HARD:     return "H5L_TYPE_HARD";
        case H5L_TYPE_SOFT:     return "H5L_TYPE_SOFT";
        case H5L_TYPE_EXTERNAL: return "H5L_TYPE_EXTERNAL";
        case H5L_TYPE_MAX:      return "H5L_TYPE_MAX";
        default:                return "(unknown)";
    } /* end switch */
} /* end link_class_to_string() */


/*-------------------------------------------------------------------------
 * Function:    attr_get_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_attr_get_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static const char*
attr_get_type_to_string(H5VL_attr_get_t get_type)
{
    switch (get_type) {
        case H5VL_ATTR_GET_ACPL:         return "H5VL_ATTR_GET_ACPL";
        case H5VL_ATTR_GET_INFO:         return "H5VL_ATTR_GET_INFO";
        case H5VL_ATTR_GET_NAME:         return "H5VL_ATTR_GET_NAME";
        case H5VL_ATTR_GET_SPACE:        return "H5VL_ATTR_GET_SPACE";
        case H5VL_ATTR_GET_STORAGE_SIZE: return "H5VL_ATTR_GET_STORAGE_SIZE";
        case H5VL_ATTR_GET_TYPE:         return "H5VL_ATTR_GET_TYPE";
        default:                         return "(unknown)";
    } /* end switch */
} /* end attr_get_type_to_string() */


/*-------------------------------------------------------------------------
 * Function:    attr_specific_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_attr_specific_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static const char*
attr_specific_type_to_string(H5VL_attr_specific_t specific_type)
{
    switch (specific_type) {
        case H5VL_ATTR_DELETE: return "H5VL_ATTR_DELETE";
        case H5VL_ATTR_EXISTS: return "H5VL_ATTR_EXISTS";
        case H5VL_ATTR_ITER:   return "H5VL_ATTR_ITER";
        case H5VL_ATTR_RENAME: return "H5VL_ATTR_RENAME";
        default:               return "(unknown)";
    } /* end switch */
} /* end attr_specific_type_to_string() */


/*-------------------------------------------------------------------------
 * Function:    datatype_get_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_datatype_get_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static const char*
datatype_get_type_to_string(H5VL_datatype_get_t get_type)
{
    switch (get_type) {
        case H5VL_DATATYPE_GET_BINARY: return "H5VL_DATATYPE_GET_BINARY";
        case H5VL_DATATYPE_GET_TCPL:   return "H5VL_DATATYPE_GET_TCPL";
        default:                       return "(unknown)";
    } /* end switch */
} /* end datatype_get_type_to_string() */


/*-------------------------------------------------------------------------
 * Function:    dataset_get_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_dataset_get_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static const char*
dataset_get_type_to_string(H5VL_dataset_get_t get_type)
{
    switch (get_type) {
        case H5VL_DATASET_GET_DAPL:         return "H5VL_DATASET_GET_DAPL";
        case H5VL_DATASET_GET_DCPL:         return "H5VL_DATASET_GET_DCPL";
        case H5VL_DATASET_GET_OFFSET:       return "H5VL_DATASET_GET_OFFSET";
        case H5VL_DATASET_GET_SPACE:        return "H5VL_DATASET_GET_SPACE";
        case H5VL_DATASET_GET_SPACE_STATUS: return "H5VL_DATASET_GET_SPACE_STATUS";
        case H5VL_DATASET_GET_STORAGE_SIZE: return "H5VL_DATASET_GET_STORAGE_SIZE";
        case H5VL_DATASET_GET_TYPE:         return "H5VL_DATASET_GET_TYPE";
        default:                            return "(unknown)";
    } /* end switch */
} /* end dataset_get_type_to_string() */


/*-------------------------------------------------------------------------
 * Function:    dataset_specific_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_dataset_specific_t enum into its string
 *              representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static const char*
dataset_specific_type_to_string(H5VL_dataset_specific_t specific_type)
{
    switch (specific_type) {
        case H5VL_DATASET_SET_EXTENT: return "H5VL_DATASET_SET_EXTENT";
        default:                      return "(unknown)";
    } /* end switch */
} /* end dataset_specific_type_to_string() */


/*-------------------------------------------------------------------------
 * Function:    file_flags_to_string
 *
 * Purpose:     Helper function to convert File creation/access flags
 *              into their string representations
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static const char*
file_flags_to_string(unsigned flags)
{
    /* When included from a public header, these macros cannot be switched upon */
    if (flags == H5F_ACC_TRUNC)
        return "H5F_ACC_TRUNC";
    else if (flags == H5F_ACC_EXCL)
        return "H5F_ACC_EXCL";
    else if (flags == H5F_ACC_RDWR)
        return "H5F_ACC_RDWR";
    else if (flags == H5F_ACC_RDONLY)
        return "H5F_ACC_RDONLY";
    else
        return "(unknown)";
} /* end file_flags_to_string() */


/*-------------------------------------------------------------------------
 * Function:    file_get_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_file_get_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static const char*
file_get_type_to_string(H5VL_file_get_t get_type)
{
    switch (get_type) {
        case H5VL_FILE_GET_FAPL:      return "H5VL_FILE_GET_FAPL";
        case H5VL_FILE_GET_FCPL:      return "H5VL_FILE_GET_FCPL";
        case H5VL_FILE_GET_INTENT:    return "H5VL_FILE_GET_INTENT";
        case H5VL_FILE_GET_NAME:      return "H5VL_FILE_GET_NAME";
        case H5VL_FILE_GET_OBJ_COUNT: return "H5VL_FILE_GET_OBJ_COUNT";
        case H5VL_FILE_GET_OBJ_IDS:   return "H5VL_FILE_GET_OBJ_IDS";
        case H5VL_OBJECT_GET_FILE:    return "H5VL_OBJECT_GET_FILE";
        default:                      return "(unknown)";
    } /* end switch */
} /* end file_get_type_to_string() */


/*-------------------------------------------------------------------------
 * Function:    file_specific_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_file_specific_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static const char*
file_specific_type_to_string(H5VL_file_specific_t specific_type)
{
    switch (specific_type) {
        case H5VL_FILE_FLUSH:         return "H5VL_FILE_FLUSH";
        case H5VL_FILE_IS_ACCESSIBLE: return "H5VL_FILE_IS_ACCESSIBLE";
        case H5VL_FILE_MOUNT:         return "H5VL_FILE_MOUNT";
        case H5VL_FILE_UNMOUNT:       return "H5VL_FILE_UNMOUNT";
        default:                      return "(unknown)";
    } /* end switch */
} /* end file_specific_type_to_string() */


/*-------------------------------------------------------------------------
 * Function:    file_optional_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_file_optional_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static const char*
file_optional_type_to_string(H5VL_file_optional_t optional_type)
{
    switch (optional_type) {
        case H5VL_FILE_CLEAR_ELINK_CACHE:  return "H5VL_FILE_CLEAR_ELINK_CACHE";
        case H5VL_FILE_GET_FILE_IMAGE:     return "H5VL_FILE_GET_FILE_IMAGE";
        case H5VL_FILE_GET_FREE_SECTIONS:  return "H5VL_FILE_GET_FREE_SECTIONS";
        case H5VL_FILE_GET_FREE_SPACE:     return "H5VL_FILE_GET_FREE_SPACE";
        case H5VL_FILE_GET_INFO:           return "H5VL_FILE_GET_INFO";
        case H5VL_FILE_GET_MDC_CONF:       return "H5VL_FILE_GET_MDC_CONF";
        case H5VL_FILE_GET_MDC_HR:         return "H5VL_FILE_GET_MDC_HR";
        case H5VL_FILE_GET_MDC_SIZE:       return "H5VL_FILE_GET_MDC_SIZE";
        case H5VL_FILE_GET_SIZE:           return "H5VL_FILE_GET_SIZE";
        case H5VL_FILE_GET_VFD_HANDLE:     return "H5VL_FILE_GET_VFD_HANDLE";
        case H5VL_FILE_REOPEN:             return "H5VL_FILE_REOPEN";
        case H5VL_FILE_RESET_MDC_HIT_RATE: return "H5VL_FILE_RESET_MDC_HIT_RATE";
        case H5VL_FILE_SET_MDC_CONFIG:     return "H5VL_FILE_SET_MDC_CONFIG";
        default:                           return "(unknown)";
    } /* end switch */
} /* end file_optional_type_to_string() */


/*-------------------------------------------------------------------------
 * Function:    group_get_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_group_get_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static const char*
group_get_type_to_string(H5VL_group_get_t get_type)
{
    switch (get_type) {
        case H5VL_GROUP_GET_GCPL: return "H5VL_GROUP_GET_GCPL";
        case H5VL_GROUP_GET_INFO: return "H5VL_GROUP_GET_INFO";
        default:                  return "(unknown)";
    } /* end switch */
} /* end group_get_type_to_string() */


/*-------------------------------------------------------------------------
 * Function:    link_create_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_link_create_type_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static const char*
link_create_type_to_string(H5VL_link_create_type_t link_create_type)
{
    switch (link_create_type) {
        case H5VL_LINK_CREATE_HARD: return "H5VL_LINK_CREATE_HARD";
        case H5VL_LINK_CREATE_SOFT: return "H5VL_LINK_CREATE_SOFT";
        case H5VL_LINK_CREATE_UD:   return "H5VL_LINK_CREATE_UD";
        default:                    return "(unknown)";
    } /* end switch */
} /* end link_create_type_to_string() */


/*-------------------------------------------------------------------------
 * Function:    link_get_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_link_get_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static const char*
link_get_type_to_string(H5VL_link_get_t get_type)
{
    switch (get_type) {
        case H5VL_LINK_GET_INFO: return "H5VL_LINK_GET_INFO";
        case H5VL_LINK_GET_NAME: return "H5VL_LINK_GET_NAME";
        case H5VL_LINK_GET_VAL:  return "H5VL_LINK_GET_VAL";
        default:                 return "(unknown)";
    } /* end switch */
} /* end link_get_type_to_string() */


/*-------------------------------------------------------------------------
 * Function:    link_specific_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_link_specific_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static const char*
link_specific_type_to_string(H5VL_link_specific_t specific_type)
{
    switch (specific_type) {
        case H5VL_LINK_DELETE: return "H5VL_LINK_DELETE";
        case H5VL_LINK_EXISTS: return "H5VL_LINK_EXISTS";
        case H5VL_LINK_ITER:   return "H5VL_LINK_ITER";
        default:               return "(unknown)";
    } /* end switch */
} /* end link_specific_type_to_string() */


/*-------------------------------------------------------------------------
 * Function:    object_get_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_object_get_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static const char*
object_get_type_to_string(H5VL_object_get_t get_type)
{
    switch (get_type) {
        case H5VL_REF_GET_NAME:   return "H5VL_REF_GET_NAME";
        case H5VL_REF_GET_REGION: return "H5VL_REF_GET_REGION";
        case H5VL_REF_GET_TYPE:   return "H5VL_REF_GET_TYPE";
        default:                  return "(unknown)";
    } /* end switch */
} /* end object_get_type_to_string() */


/*-------------------------------------------------------------------------
 * Function:    object_specific_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_object_specific_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static const char*
object_specific_type_to_string(H5VL_object_specific_t specific_type)
{
    switch (specific_type) {
        case H5VL_OBJECT_CHANGE_REF_COUNT: return "H5VL_OBJECT_CHANGE_REF_COUNT";
        case H5VL_OBJECT_EXISTS:           return "H5VL_OBJECT_EXISTS";
        case H5VL_OBJECT_VISIT:            return "H5VL_OBJECT_VISIT";
        case H5VL_REF_CREATE:              return "H5VL_REF_CREATE";
        default:                           return "(unknown)";
    } /* end switch */
} /* end object_specific_type_to_string() */


/*-------------------------------------------------------------------------
 * Function:    object_optional_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_object_optional_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static const char*
object_optional_type_to_string(H5VL_object_optional_t optional_type)
{
    switch (optional_type) {
        case H5VL_OBJECT_GET_COMMENT: return "H5VL_OBJECT_GET_COMMENT";
        case H5VL_OBJECT_GET_INFO:    return "H5VL_OBJECT_GET_INFO";
        case H5VL_OBJECT_SET_COMMENT: return "H5VL_OBJECT_SET_COMMENT";
        default:                      return "(unknown)";
    } /* end switch */
} /* end object_optional_type_to_string() */
#endif
