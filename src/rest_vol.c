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
 * Purpose: An implementation of a VOL connector to access HDF5 data in a
 *          REST-oriented manner.
 *
 *          Due to specialized improvements needed for performance reasons
 *          and otherwise, this VOL connector is currently only supported for
 *          use with the HDF Kita server (https://www.hdfgroup.org/hdf-kita).
 *
 *          HDF Kita is a web service that implements a REST-based web service
 *          for HDF5 data stores as described in the paper:
 *          http://hdfgroup.org/pubs/papers/RESTful_HDF5.pdf.
 */

#include "rest_vol.h"

/* Default size for buffer used when transforming an HDF5 dataspace into JSON. */
#define DATASPACE_SHAPE_BUFFER_DEFAULT_SIZE 256

/* Defines for multi-curl settings */
#define BACKOFF_INITIAL_DURATION 10000000 /* 10,000,000 ns -> 0.01 sec */
#define BACKOFF_SCALE_FACTOR     1.5
#define BACKOFF_MAX_BEFORE_FAIL  3000000000 /* 30,000,000,000 ns -> 30 sec */

/* Number of unique characters which need to be escaped before being sent as JSON */
#define NUM_JSON_ESCAPE_CHARS 7
/*
 * The VOL connector identification number.
 */
hid_t H5_rest_id_g = H5I_UNINIT;

static hbool_t H5_rest_initialized_g = FALSE;

/* Identifiers for HDF5's error API */
hid_t H5_rest_err_stack_g                 = H5I_INVALID_HID;
hid_t H5_rest_err_class_g                 = H5I_INVALID_HID;
hid_t H5_rest_obj_err_maj_g               = H5I_INVALID_HID;
hid_t H5_rest_parse_err_min_g             = H5I_INVALID_HID;
hid_t H5_rest_link_table_err_min_g        = H5I_INVALID_HID;
hid_t H5_rest_link_table_iter_err_min_g   = H5I_INVALID_HID;
hid_t H5_rest_attr_table_err_min_g        = H5I_INVALID_HID;
hid_t H5_rest_attr_table_iter_err_min_g   = H5I_INVALID_HID;
hid_t H5_rest_object_table_err_min_g      = H5I_INVALID_HID;
hid_t H5_rest_object_table_iter_err_min_g = H5I_INVALID_HID;

/*
 * The CURL pointer used for all cURL operations.
 */
CURL *curl = NULL;

/*
 * cURL error message buffer.
 */
char curl_err_buf[CURL_ERROR_SIZE];

/*
 * cURL header list
 */
struct curl_slist *curl_headers = NULL;

#ifdef RV_TRACK_MEM_USAGE
/*
 * Counter to keep track of the currently allocated amount of bytes
 */
size_t H5_rest_curr_alloc_bytes;
#endif

/* A global struct containing the buffer which cURL will write its
 * responses out to after making a call to the server. The buffer
 * in this struct is allocated upon connector initialization and is
 * dynamically grown as needed throughout the lifetime of the connector.
 */
struct response_buffer response_buffer;

/* Authentication information for authenticating
 * with Active Directory.
 */
typedef struct H5_rest_ad_info_t {
    char    clientID[128];
    char    tenantID[128];
    char    resourceID[128];
    char    client_secret[128];
    hbool_t unattended;
} H5_rest_ad_info_t;

/* Global array containing information about open objects */
RV_type_info *RV_type_info_array_g[H5I_MAX_NUM_TYPES] = {0};

/* Host header string for specifying the host (Domain) for requests */
const char *const host_string = "X-Hdf-domain: ";

/* JSON key to retrieve the ID of the root group of a file */
const char *root_id_keys[] = {"root", (const char *)0};

/* JSON key to retrieve the ID of an object from the server */
const char *object_id_keys[] = {"id", (const char *)0};

/* JSON keys to retrieve information about creation properties of an object */
const char *object_creation_properties_keys[] = {"creationProperties", (const char *)0};

/* JSON key to retrieve the ID of a link from the server */
const char *link_id_keys[] = {"link", "id", (const char *)0};

/* JSON keys to retrieve the class of a link (HARD, SOFT, EXTERNAL, etc.) */
const char *link_class_keys[]  = {"link", "class", (const char *)0};
const char *link_class_keys2[] = {"class", (const char *)0};

/* JSON keys to retrieve information about a dataspace */
const char *dataspace_class_keys[]    = {"shape", "class", (const char *)0};
const char *dataspace_dims_keys[]     = {"shape", "dims", (const char *)0};
const char *dataspace_max_dims_keys[] = {"shape", "maxdims", (const char *)0};

/* JSON keys to retrieve the path of a domain */
const char *domain_keys[] = {"domain", (const char *)0};

/* JSON keys to retrieve all of the information from a link when doing link iteration */
const char *links_keys[]              = {"links", (const char *)0};
const char *link_title_keys[]         = {"title", (const char *)0};
const char *link_creation_time_keys[] = {"created", (const char *)0};

/* JSON keys to retrieve the collection that a hard link belongs to
 * (the type of object it points to), "groups", "datasets" or "datatypes"
 */
const char *link_collection_keys2[] = {"collection", (const char *)0};

const char *attributes_keys[] = {"attributes", (const char *)0};

/* JSON keys to retrieve allocated size */
const char *allocated_size_keys[] = {"allocated_size", (const char *)0};

/* Default size for the buffer to allocate during base64-encoding if the caller
 * of RV_base64_encode supplies a 0-sized buffer.
 */
#define BASE64_ENCODE_DEFAULT_BUFFER_SIZE 33554432 /* 32MB */

/* JSON key to retrieve the version of server from a request to a file. */
const char *server_version_keys[] = {"version", (const char *)0};

/* Used for cURL's base URL if the connection is through a local socket */
const char *socket_base_url = "0";

/* Internal initialization/termination functions which are called by
 * the public functions H5rest_init() and H5rest_term() */
static herr_t H5_rest_init(hid_t vipl_id);
static herr_t H5_rest_term(void);

static herr_t H5_rest_authenticate_with_AD(H5_rest_ad_info_t *ad_info, const char *base_URL);

/* Introspection callbacks */
static herr_t H5_rest_get_conn_cls(void *obj, H5VL_get_conn_lvl_t lvl, const struct H5VL_class_t **conn_cls);
static herr_t H5_rest_get_cap_flags(const void *info, uint64_t *cap_flags);
static herr_t H5_rest_opt_query(void *obj, H5VL_subclass_t cls, int opt_type, uint64_t *flags);

/* cURL function callbacks */
static size_t H5_rest_curl_read_data_callback(char *buffer, size_t size, size_t nmemb, void *inptr);
static size_t H5_rest_curl_write_data_callback(char *buffer, size_t size, size_t nmemb, void *userp);

/* Helper function to URL-encode an entire pathname by URL-encoding each of its separate components */
static char *H5_rest_url_encode_path(const char *path);

/* Helper function to parse an object's type from server response */
herr_t RV_parse_type(char *HTTP_response, void *callback_data_in, void *callback_data_out);

/* Helper function to parse an object's creation properties from server response */
herr_t RV_parse_creation_properties_callback(yajl_val parse_tree, char **GCPL_buf);

/* Return the index of the curl handle into the array of handles */
herr_t RV_get_index_of_matching_handle(dataset_transfer_info *transfer_info, size_t count, CURL *handle,
                                       size_t *handle_index);

/* Stub that throws an error when called */
void *RV_wrap_get_object(const void *obj);

/* The REST VOL connector's class structure. */
static const H5VL_class_t H5VL_rest_g = {
    HDF5_VOL_REST_VERSION,      /* Connector struct version number       */
    HDF5_VOL_REST_CLS_VAL,      /* Connector value                       */
    HDF5_VOL_REST_NAME,         /* Connector name                        */
    HDF5_VOL_REST_CONN_VERSION, /* Connector version # */
    H5VL_VOL_REST_CAP_FLAGS,    /* Connector capability flags            */
    H5_rest_init,               /* Connector initialization function     */
    H5_rest_term,               /* Connector termination function        */

    /* Connector info callbacks */
    {
        0,    /* Connector info size                   */
        NULL, /* Connector info copy function          */
        NULL, /* Connector info compare function       */
        NULL, /* Connector info free function          */
        NULL, /* Connector info to string function     */
        NULL, /* Connector string to info function     */
    },

    /* Connector 'wrap' callbacks */
    {
        NULL, /* Connector get object function         */
        NULL, /* Connector get wrap context function   */
        NULL, /* Connector wrap object function        */
        NULL, /* Connector unwrap object function      */
        NULL, /* Connector free wrap context function  */
    },

    /* Connector attribute callbacks */
    {
        RV_attr_create,
        RV_attr_open,
        RV_attr_read,
        RV_attr_write,
        RV_attr_get,
        RV_attr_specific,
        NULL,
        RV_attr_close,
    },

    /* Connector dataset callbacks */
    {
        RV_dataset_create,
        RV_dataset_open,
        RV_dataset_read,
        RV_dataset_write,
        RV_dataset_get,
        RV_dataset_specific,
        NULL,
        RV_dataset_close,
    },

    /* Connector datatype callbacks */
    {
        RV_datatype_commit,
        RV_datatype_open,
        RV_datatype_get,
        NULL,
        NULL,
        RV_datatype_close,
    },

    /* Connector file callbacks */
    {
        RV_file_create,
        RV_file_open,
        RV_file_get,
        RV_file_specific,
        NULL,
        RV_file_close,
    },

    /* Connector group callbacks */
    {
        RV_group_create,
        RV_group_open,
        RV_group_get,
        NULL,
        NULL,
        RV_group_close,
    },

    /* Connector link callbacks */
    {
        RV_link_create,
        RV_link_copy,
        RV_link_move,
        RV_link_get,
        RV_link_specific,
        NULL,
    },

    /* Connect object callbacks */
    {
        RV_object_open,
        RV_object_copy,
        RV_object_get,
        RV_object_specific,
        NULL,
    },

    /* Connector introspection callbacks */
    {
        H5_rest_get_conn_cls,  /* Connector introspect 'get class' function */
        H5_rest_get_cap_flags, /* Capt flags */
        H5_rest_opt_query,     /* Connector introspect 'opt query' function */
    },

    /* Connector async request callbacks */
    {
        NULL, /* Connector request 'wait' function      */
        NULL, /* Connector request 'notify' function    */
        NULL, /* Connector request 'cancel' function    */
        NULL, /* Connector request 'specific' function  */
        NULL, /* Connector request 'optional' function  */
        NULL, /* Connector request 'free' function      */
    },

    /* Connector 'blob' callbacks */
    {
        NULL, /* Connector blob 'put' function          */
        NULL, /* Connector blob 'get' function          */
        NULL, /* Connector blob 'specific' function     */
        NULL, /* Connector blob 'optional' function     */
    },

    /* Connector object token callbacks */
    {
        NULL, /* Connector token compare function       */
        NULL, /* Connector token 'to string' function   */
        NULL, /* Connector token 'from string' function */
    },

    NULL, /* Connector 'catch-all' function         */
};

/*-------------------------------------------------------------------------
 * Function:    H5rest_init
 *
 * Purpose:     Initialize the HDF5 REST VOL connector by initializing cURL
 *              and then registering the connector with the library.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 *-------------------------------------------------------------------------
 */
herr_t
H5rest_init(void)
{
    H5I_type_t idType    = H5I_UNINIT;
    herr_t     ret_value = SUCCEED;

    /* Initialize HDF5 */
    if (H5open() < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "HDF5 failed to initialize");

    if (H5_rest_id_g >= 0 && (idType = H5Iget_type(H5_rest_id_g)) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "failed to retrieve REST VOL connector's ID type");

    /* Register the REST VOL connector, if it isn't already registered */
    if (H5I_VOL != idType) {
        htri_t is_registered;

        if ((is_registered = H5VLis_connector_registered_by_name(H5VL_rest_g.name)) < 0)
            FUNC_GOTO_ERROR(H5E_ID, H5E_CANTINIT, FAIL,
                            "can't determine if REST VOL connector is registered");

        if (!is_registered) {
            /* Register connector */
            if ((H5_rest_id_g = H5VLregister_connector((const H5VL_class_t *)&H5VL_rest_g, H5P_DEFAULT)) < 0)
                FUNC_GOTO_ERROR(H5E_ID, H5E_CANTINSERT, FAIL, "can't create ID for REST VOL connector");
        } /* end if */
        else {
            if ((H5_rest_id_g = H5VLget_connector_id_by_name(H5VL_rest_g.name)) < 0)
                FUNC_GOTO_ERROR(H5E_ID, H5E_CANTGET, FAIL,
                                "unable to get registered ID for REST VOL connector");
        } /* end else */

        if (H5_rest_id_g >= 0 && (idType = H5Iget_type(H5_rest_id_g)) < 0)
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "failed to retrieve REST VOL connector's ID type");
    } /* end if */

    if (!H5_rest_initialized_g && H5_rest_init(H5P_VOL_INITIALIZE_DEFAULT) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "failed to initialize connector");

done:
    /* Cleanup if REST VOL connector initialization failed */
    if (ret_value < 0)
        H5rest_term();

    PRINT_ERROR_STACK;

    return ret_value;
} /* end H5rest_init() */

/*-------------------------------------------------------------------------
 * Function:    H5_rest_init
 *
 * Purpose:     TODO
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
H5_rest_init(hid_t vipl_id)
{
    herr_t                  ret_value       = SUCCEED;
    curl_version_info_data *curl_ver        = NULL;
    char                    user_agent[128] = {'\0'};

    /* Check if already initialized */
    if (H5_rest_initialized_g)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "attempting to initialize connector twice");

#ifdef RV_TRACK_MEM_USAGE
    /* Initialize allocated memory counter */
    H5_rest_curr_alloc_bytes = 0;
#endif

    /* Initialize cURL */
    if (CURLE_OK != curl_global_init(CURL_GLOBAL_ALL))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't initialize cURL");

    if (NULL == (curl = curl_easy_init()))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't initialize cURL easy handle");

    /* Instruct cURL to use the buffer for error messages */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_err_buf))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set cURL error buffer");

    /* Allocate buffer for cURL to write responses to */
    if (NULL == (response_buffer.buffer = (char *)RV_malloc(CURL_RESPONSE_BUFFER_DEFAULT_SIZE)))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTALLOC, FAIL, "can't allocate cURL response buffer");
    response_buffer.buffer_size  = CURL_RESPONSE_BUFFER_DEFAULT_SIZE;
    response_buffer.curr_buf_ptr = response_buffer.buffer;

    /* Redirect cURL output to response buffer */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, H5_rest_curl_write_data_callback))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set cURL write function: %s", curl_err_buf);

    /* Set cURL read function for UPLOAD operations */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_READFUNCTION, H5_rest_curl_read_data_callback))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set cURL read function: %s", curl_err_buf);

    /* Set user agent string */
    curl_ver = curl_version_info(CURLVERSION_NOW);
    if (snprintf(user_agent, sizeof(user_agent), "libhdf5/%d.%d.%d (%s; %s v%s)", H5_VERS_MAJOR,
                 H5_VERS_MINOR, H5_VERS_RELEASE, curl_ver->host, HDF5_VOL_REST_LIB_NAME,
                 HDF5_VOL_REST_LIB_VER) < 0) {
        FUNC_GOTO_ERROR(H5E_VOL, H5E_SYSERRSTR, FAIL, "error creating user agent string");
    }
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent))
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "error while setting CURL option (CURLOPT_USERAGENT)");

    const char *URL = getenv("HSDS_ENDPOINT");

    if (URL && !strncmp(URL, UNIX_SOCKET_PREFIX, strlen(UNIX_SOCKET_PREFIX))) {
        const char *socket_path = "/tmp/hs/sn_1.sock";

        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, socket_path))
            FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, FAIL, "can't set cURL socket path header: %s",
                            curl_err_buf);
    }

#ifdef RV_CURL_DEBUG
    /* Enable cURL debugging output if desired */
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
#endif

    /* Set up global type info array */
    for (size_t i = 0; i < H5I_MAX_NUM_TYPES; i++) {
        RV_type_info_array_g[i]        = RV_calloc(sizeof(RV_type_info));
        RV_type_info_array_g[i]->table = rv_hash_table_new(rv_hash_string, H5_rest_compare_string_keys);
    }

    /* Register the connector with HDF5's error reporting API */
    if ((H5_rest_err_class_g = H5Eregister_class(HDF5_VOL_REST_ERR_CLS_NAME, HDF5_VOL_REST_LIB_NAME,
                                                 HDF5_VOL_REST_LIB_VER)) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't register with HDF5 error API");

    /* Create a separate error stack for the REST VOL to report errors with */
    if ((H5_rest_err_stack_g = H5Ecreate_stack()) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't create error stack");

    /* Set up a few REST VOL-specific error API message classes */
    if ((H5_rest_obj_err_maj_g = H5Ecreate_msg(H5_rest_err_class_g, H5E_MAJOR, "Object interface")) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't create error message for object interface");
    if ((H5_rest_parse_err_min_g =
             H5Ecreate_msg(H5_rest_err_class_g, H5E_MINOR, "Error occurred while parsing JSON")) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't create error message for JSON parsing failures");
    if ((H5_rest_link_table_err_min_g =
             H5Ecreate_msg(H5_rest_err_class_g, H5E_MINOR, "Can't build table of links for iteration")) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't create error message for link table build error");
    if ((H5_rest_link_table_iter_err_min_g =
             H5Ecreate_msg(H5_rest_err_class_g, H5E_MINOR, "Can't iterate through link table")) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL,
                        "can't create error message for link table iteration error");
    if ((H5_rest_attr_table_err_min_g = H5Ecreate_msg(H5_rest_err_class_g, H5E_MINOR,
                                                      "Can't build table of attributes for iteration")) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL,
                        "can't create error message for attribute table build error");
    if ((H5_rest_attr_table_iter_err_min_g =
             H5Ecreate_msg(H5_rest_err_class_g, H5E_MINOR, "Can't iterate through attribute table")) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't create message for attribute iteration error");
    if ((H5_rest_object_table_err_min_g =
             H5Ecreate_msg(H5_rest_err_class_g, H5E_MINOR, "Can't build table of objects for iteration")) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL,
                        "can't create error message for object table build error");
    if ((H5_rest_object_table_iter_err_min_g =
             H5Ecreate_msg(H5_rest_err_class_g, H5E_MINOR, "Can't iterate through object table")) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't create message for object iteration error");

    /* Initialized */
    H5_rest_initialized_g = TRUE;

done:
    /* Cleanup if REST VOL connector initialization failed */
    if (ret_value < 0)
        H5rest_term();

    return ret_value;
} /* end H5_rest_init() */

/*-------------------------------------------------------------------------
 * Function:    H5rest_term
 *
 * Purpose:     Shut down the REST VOL connector
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
herr_t
H5rest_term(void)
{
    herr_t ret_value = SUCCEED;

    if (H5_rest_term() < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CLOSEERROR, FAIL, "can't close REST VOL connector");

done:
#ifdef RV_TRACK_MEM_USAGE
    /* Check for allocated memory */
    if (0 != H5_rest_curr_alloc_bytes)
        FUNC_DONE_ERROR(H5E_VOL, H5E_CLOSEERROR, FAIL, "%zu bytes were still left allocated",
                        H5_rest_curr_alloc_bytes);

    H5_rest_curr_alloc_bytes = 0;
#endif

    /* Unregister from the HDF5 error API */
    if (H5_rest_err_class_g >= 0) {
        if (H5_rest_obj_err_maj_g >= 0 && H5Eclose_msg(H5_rest_obj_err_maj_g) < 0)
            FUNC_DONE_ERROR(H5E_VOL, H5E_CLOSEERROR, FAIL,
                            "can't unregister error message for object interface");
        if (H5_rest_parse_err_min_g >= 0 && H5Eclose_msg(H5_rest_parse_err_min_g) < 0)
            FUNC_DONE_ERROR(H5E_VOL, H5E_CLOSEERROR, FAIL,
                            "can't unregister error message for JSON parsing error");
        if (H5_rest_link_table_err_min_g >= 0 && H5Eclose_msg(H5_rest_link_table_err_min_g) < 0)
            FUNC_DONE_ERROR(H5E_VOL, H5E_CLOSEERROR, FAIL,
                            "can't unregister error message for building link table");
        if (H5_rest_link_table_iter_err_min_g >= 0 && H5Eclose_msg(H5_rest_link_table_iter_err_min_g) < 0)
            FUNC_DONE_ERROR(H5E_VOL, H5E_CLOSEERROR, FAIL,
                            "can't unregister error message for iterating over link table");
        if (H5_rest_attr_table_err_min_g >= 0 && H5Eclose_msg(H5_rest_attr_table_err_min_g) < 0)
            FUNC_DONE_ERROR(H5E_VOL, H5E_CLOSEERROR, FAIL,
                            "can't unregister error message for building attribute table");
        if (H5_rest_attr_table_iter_err_min_g >= 0 && H5Eclose_msg(H5_rest_attr_table_iter_err_min_g) < 0)
            FUNC_DONE_ERROR(H5E_VOL, H5E_CLOSEERROR, FAIL,
                            "can't unregister error message for iterating over attribute table");
        if (H5_rest_object_table_iter_err_min_g >= 0 && H5Eclose_msg(H5_rest_object_table_err_min_g) < 0)
            FUNC_DONE_ERROR(H5E_VOL, H5E_CLOSEERROR, FAIL,
                            "can't unregister error message for build object table");
        if (H5_rest_object_table_iter_err_min_g >= 0 && H5Eclose_msg(H5_rest_object_table_iter_err_min_g) < 0)
            FUNC_DONE_ERROR(H5E_VOL, H5E_CLOSEERROR, FAIL,
                            "can't unregister error message for iterating over object table");

        if (H5Eunregister_class(H5_rest_err_class_g) < 0)
            FUNC_DONE_ERROR(H5E_VOL, H5E_CLOSEERROR, FAIL, "can't unregister from HDF5 error API");

        /* Print the current error stack before destroying it */
        PRINT_ERROR_STACK;

        /* Destroy the error stack */
        if (H5Eclose_stack(H5_rest_err_stack_g) < 0) {
            FUNC_DONE_ERROR(H5E_VOL, H5E_CLOSEERROR, FAIL, "can't close error stack");
            PRINT_ERROR_STACK;
        } /* end if */

        H5_rest_err_stack_g               = H5I_INVALID_HID;
        H5_rest_err_class_g               = H5I_INVALID_HID;
        H5_rest_obj_err_maj_g             = H5I_INVALID_HID;
        H5_rest_parse_err_min_g           = H5I_INVALID_HID;
        H5_rest_link_table_err_min_g      = H5I_INVALID_HID;
        H5_rest_link_table_iter_err_min_g = H5I_INVALID_HID;
        H5_rest_attr_table_err_min_g      = H5I_INVALID_HID;
        H5_rest_attr_table_iter_err_min_g = H5I_INVALID_HID;
    } /* end if */

    return ret_value;
} /* end H5rest_term() */

/*-------------------------------------------------------------------------
 * Function:    H5_rest_term
 *
 * Purpose:     Shut down the REST VOL connector
 *
 * Return:      SUCCEED (can't fail)
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
H5_rest_term(void)
{
    herr_t ret_value = SUCCEED;

    if (!H5_rest_initialized_g)
        FUNC_GOTO_DONE(SUCCEED);

    /* Free memory for cURL response buffer */
    if (response_buffer.buffer) {
        RV_free(response_buffer.buffer);
        response_buffer.buffer = NULL;
    }

    /* Allow cURL to clean up */
    if (curl) {
        curl_easy_cleanup(curl);
        curl = NULL;
        curl_global_cleanup();
    } /* end if */

    /* Cleanup type info array */
    if (RV_type_info_array_g) {
        for (size_t i = 0; i < H5I_MAX_NUM_TYPES; i++) {
            if (RV_type_info_array_g[i]) {
                rv_hash_table_free(RV_type_info_array_g[i]->table);
                RV_free(RV_type_info_array_g[i]);
                RV_type_info_array_g[i] = NULL;
            }
        }
    }

    /*
     * "Forget" connector ID. This should normally be called by the library
     * when it is closing the id, so no need to close it here.
     */
    H5_rest_id_g = -1;

    /* No longer initialized */
    H5_rest_initialized_g = FALSE;

done:
    return ret_value;
} /* end H5_rest_term() */

/*-------------------------------------------------------------------------
 * Function:    H5Pset_fapl_rest_vol
 *
 * Purpose:     Modify the file access property list to use the REST VOL
 *              connector
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
herr_t
H5Pset_fapl_rest_vol(hid_t fapl_id)
{
    htri_t is_fapl;
    herr_t ret_value = SUCCEED;

    if (H5_rest_id_g < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_UNINITIALIZED, FAIL, "REST VOL connector not initialized");

    if (H5P_DEFAULT == fapl_id)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_BADVALUE, FAIL,
                        "can't set REST VOL connector for default property list");

    if ((is_fapl = H5Pisa_class(fapl_id, H5P_FILE_ACCESS)) < 0)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "couldn't determine property list class");
    if (!is_fapl)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a file access property list");

    if ((ret_value = H5Pset_vol(fapl_id, H5_rest_id_g, NULL)) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't set REST VOL connector in FAPL");

done:
    PRINT_ERROR_STACK;

    return ret_value;
} /* end H5Pset_fapl_rest_vol() */

const char *
H5rest_get_object_uri(hid_t obj_id)
{
    RV_object_t *VOL_obj;
    char        *ret_value = NULL;

    /* TODO: Route through REST VOL optional catch-all function */

    if (NULL == (VOL_obj = (RV_object_t *)H5VLobject(obj_id)))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_BADVALUE, NULL, "invalid identifier");
    ret_value = VOL_obj->URI;

done:
    PRINT_ERROR_STACK;

    return (const char *)ret_value;
} /* end H5rest_get_object_uri() */

/*-------------------------------------------------------------------------
 * Function:    H5_rest_set_connection_information
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
herr_t
H5_rest_set_connection_information(server_info_t *server_info)
{
    H5_rest_ad_info_t ad_info;
    FILE             *config_file = NULL;
    herr_t            ret_value   = SUCCEED;

    const char *username = NULL;
    const char *password = NULL;
    const char *base_URL = NULL;

    memset(&ad_info, 0, sizeof(ad_info));

    /*
     * Attempt to pull in configuration/authentication information from
     * the environment.
     */

    if (base_URL = getenv("HSDS_ENDPOINT")) {

        username = getenv("HSDS_USERNAME");
        password = getenv("HSDS_PASSWORD");

        if (!strncmp(base_URL, UNIX_SOCKET_PREFIX, strlen(UNIX_SOCKET_PREFIX))) {
            base_URL = socket_base_url;
        }

        if (!username && !password) {
            const char *clientID      = getenv("HSDS_AD_CLIENT_ID");
            const char *tenantID      = getenv("HSDS_AD_TENANT_ID");
            const char *resourceID    = getenv("HSDS_AD_RESOURCE_ID");
            const char *client_secret = getenv("HSDS_AD_CLIENT_SECRET");

            if (clientID)
                strncpy(ad_info.clientID, clientID, sizeof(ad_info.clientID) - 1);
            if (tenantID)
                strncpy(ad_info.tenantID, tenantID, sizeof(ad_info.tenantID) - 1);
            if (resourceID)
                strncpy(ad_info.resourceID, resourceID, sizeof(ad_info.resourceID) - 1);
            if (client_secret) {
                ad_info.unattended = TRUE;
                strncpy(ad_info.client_secret, client_secret, sizeof(ad_info.client_secret) - 1);
            } /* end if */

            /* Attempt authentication with Active Directory */
            if (H5_rest_authenticate_with_AD(&ad_info, base_URL) < 0)
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't authenticate with Active Directory");
        } /* end else */
    }     /* end if */
    else {
        const char *cfg_file_name = ".hscfg";
        size_t      pathname_len  = 0;
        char       *home_dir      = NULL;
        char       *pathname      = NULL;
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
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL,
                            "reading config file - unable to retrieve location of home directory");

        if (NULL == (home_dir = getenv("HOMEPATH")))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL,
                            "reading config file - unable to retrieve location of home directory");

        pathname_len = strlen(home_drive) + strlen(home_dir) + strlen(cfg_file_name) + 3;
        if (NULL == (pathname = RV_malloc(pathname_len)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTALLOC, FAIL,
                            "unable to allocate space for config file pathname");

        if ((file_path_len =
                 snprintf(pathname, pathname_len, "%s\\%s\\%s", home_drive, home_dir, cfg_file_name)) < 0)
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "snprintf error");

        if (file_path_len >= pathname_len)
            FUNC_GOTO_ERROR(H5E_VOL, H5E_SYSERRSTR, FAIL,
                            "config file path length exceeded maximum path length");
#else
        if (NULL == (home_dir = getenv("HOME")))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL,
                            "reading config file - unable to retrieve location of home directory");

        pathname_len = strlen(home_dir) + strlen(cfg_file_name) + 2;
        if (NULL == (pathname = RV_malloc(pathname_len)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTALLOC, FAIL,
                            "unable to allocate space for config file pathname");

        if ((file_path_len = snprintf(pathname, pathname_len, "%s/%s", home_dir, cfg_file_name)) < 0)
            FUNC_GOTO_ERROR(H5E_VOL, H5E_SYSERRSTR, FAIL, "snprintf error");

        if (file_path_len >= pathname_len)
            FUNC_GOTO_ERROR(H5E_VOL, H5E_SYSERRSTR, FAIL,
                            "config file path length exceeded maximum path length");
#endif

        if (NULL == (config_file = fopen(pathname, "r")))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTOPENFILE, FAIL, "unable to open config file");

        /*
         * Parse the connection information by searching for
         * URL, username and password key-value pairs.
         */
        while (fgets(file_line, sizeof(file_line), config_file) != NULL) {
            char *key;
            char *val;

            if ((file_line[0] == '#') || !strlen(file_line))
                continue;
            key = strtok(file_line, " =\n");
            val = strtok(NULL, " =\n");

            if (!strcmp(key, "hs_endpoint")) {
                if (val) {
                    if (!strncmp(base_URL, UNIX_SOCKET_PREFIX, strlen(UNIX_SOCKET_PREFIX))) {
                        base_URL = socket_base_url;
                    }
                    else {
                        base_URL = val;
                    }
                } /* end if */
            }     /* end if */
            else if (!strcmp(key, "hs_ad_app_id")) {
                if (val && strlen(val))
                    strncpy(ad_info.clientID, val, sizeof(ad_info.clientID) - 1);
            } /* end else */
            else if (!strcmp(key, "hs_ad_tenant_id")) {
                if (val && strlen(val))
                    strncpy(ad_info.tenantID, val, sizeof(ad_info.tenantID) - 1);
            } /* end else */
            else if (!strcmp(key, "hs_ad_resource_id")) {
                if (val && strlen(val))
                    strncpy(ad_info.resourceID, val, sizeof(ad_info.resourceID) - 1);
            } /* end else if */
            else if (!strcmp(key, "hs_ad_client_secret")) {
                if (val && strlen(val)) {
                    ad_info.unattended = TRUE;
                    strncpy(ad_info.client_secret, val, sizeof(ad_info.client_secret) - 1);
                } /* end if */
            }     /* end else if */
        }         /* end while */

        /* Attempt authentication with Active Directory if ID values are present */
        if (ad_info.clientID[0] != '\0' && ad_info.tenantID[0] != '\0' && ad_info.resourceID[0] != '\0')
            if (!base_URL || (H5_rest_authenticate_with_AD(&ad_info, base_URL) < 0))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't authenticate with Active Directory");
    } /* end else */

    if (!base_URL)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL,
                        "must specify a base URL - please set HSDS_ENDPOINT environment variable or create a "
                        "config file");

    /* Copy server information */
    if (server_info) {
        if (username) {
            if ((server_info->username = RV_calloc(strlen(username) + 1)) == NULL)
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL, "can't allocate space for username");

            strcpy(server_info->username, username);
        }

        if (password) {
            if ((server_info->password = RV_calloc(strlen(password) + 1)) == NULL)
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL, "can't allocate space for password");

            strcpy(server_info->password, password);
        }

        if (base_URL) {
            if ((server_info->base_URL = RV_calloc(strlen(base_URL) + 1)) == NULL)
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL, "can't allocate space for URL");

            strcpy(server_info->base_URL, base_URL);
        }
    }

done:
    if (config_file)
        fclose(config_file);

    if (ret_value < 0 && server_info) {
        RV_free(server_info->username);
        server_info->username = NULL;

        RV_free(server_info->password);
        server_info->password = NULL;

        RV_free(server_info->base_URL);
        server_info->base_URL = NULL;
    }

    PRINT_ERROR_STACK;

    return ret_value;
} /* end H5_rest_set_connection_information() */

/*-------------------------------------------------------------------------
 * Function:    H5_rest_authenticate_with_AD
 *
 * Purpose:     Given a client ID, tenant ID and resource ID, attempts to
 *              authenticate with Active Directory. If client_secret is not
 *              NULL, the authentication will be performed in an unattended
 *              manner. Otherwise, the user will need to manually
 *              authenticate according to instructions that get printed out
 *              before the authentication process can complete.
 *
 *              TODO: if the authentication token expires, there should be
 *                    logic for attempting a re-authentication.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5_rest_authenticate_with_AD(H5_rest_ad_info_t *ad_info, const char *base_URL)
{
    const char *access_token_key[]  = {"access_token", (const char *)0};
    const char *refresh_token_key[] = {"refresh_token", (const char *)0};
    const char *expires_in_key[]    = {"expires_in", (const char *)0};
    const char *token_cfg_file_name = ".hstokencfg";
    yajl_val    parse_tree = NULL, key_obj = NULL;
    size_t      token_cfg_file_pathname_len = 0;
    FILE       *token_cfg_file              = NULL;
    char       *token_cfg_file_pathname     = NULL;
    char       *home_dir                    = NULL;
    char       *access_token                = NULL;
    char       *refresh_token               = NULL;
    time_t      token_expires               = -1;
    char        tenant_string[1024];
    char        data_string[1024];
    herr_t      ret_value = SUCCEED;

    if (!ad_info)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "Active Directory info structure is NULL");

    /* Set up miscellaneous options */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BEARER))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set HTTP authentication method: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set cURL FOLLOWLOCATION opt.: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "https"))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set default protocol to HTTPS: %s", curl_err_buf);

    curl_headers = curl_slist_append(curl_headers, "Content-Type: application/x-www-form-urlencoded");

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf);

        /* Check if an access token config file exists in the user's home directory */
#ifdef WIN32
    {
        char *home_drive = NULL;

        if (NULL == (home_drive = getenv("HOMEDRIVE")))
            FUNC_GOTO_ERROR(
                H5E_VOL, H5E_CANTGET, FAIL,
                "reading access token config file - unable to retrieve location of home directory");

        if (NULL == (home_dir = getenv("HOMEPATH")))
            FUNC_GOTO_ERROR(
                H5E_VOL, H5E_CANTGET, FAIL,
                "reading access token config file - unable to retrieve location of home directory");

        token_cfg_file_pathname_len = strlen(home_drive) + strlen(home_dir) + strlen(token_cfg_file_name) + 3;
        if (NULL == (token_cfg_file_pathname = RV_malloc(token_cfg_file_pathname_len)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTALLOC, FAIL,
                            "unable to allocate space for access token config file pathname");

        if (snprintf(token_cfg_file_pathname, token_cfg_file_pathname_len, "%s\\%s\\%s", home_drive, home_dir,
                     token_cfg_file_name) < 0)
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "snprintf error");
    }
#else
    if (NULL == (home_dir = getenv("HOME")))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL,
                        "reading access token config file - unable to retrieve location of home directory");

    token_cfg_file_pathname_len = strlen(home_dir) + strlen(token_cfg_file_name) + 2;
    if (NULL == (token_cfg_file_pathname = RV_malloc(token_cfg_file_pathname_len)))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTALLOC, FAIL,
                        "unable to allocate space for access token config file pathname");

    if (snprintf(token_cfg_file_pathname, token_cfg_file_pathname_len, "%s/%s", home_dir,
                 token_cfg_file_name) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_SYSERRSTR, FAIL, "snprintf error");
#endif

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Token config file location: %s\n", token_cfg_file_pathname);
#endif
    if ((token_cfg_file = fopen(token_cfg_file_pathname, "rb"))) {
        char       *cfg_json = NULL; /* Buffer for token config file content */
        size_t      file_size;
        const char *cfg_access_token[]  = {base_URL, "accessToken", (const char *)0};
        const char *cfg_refresh_token[] = {base_URL, "refreshToken", (const char *)0};
        const char *cfg_token_expires[] = {base_URL, "expiresOn", (const char *)0};

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Token config file %s found\n", token_cfg_file_pathname);
#endif

        /* Read entire token config file content */
        if (fseek(token_cfg_file, 0, SEEK_END) != 0)
            FUNC_GOTO_ERROR(H5E_VOL, H5E_SEEKERROR, FAIL, "Failed to fseek(0, SEEK_END) token config file");
        if ((file_size = (size_t)ftell(token_cfg_file)) < 0)
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTGETSIZE, FAIL, "Failed to get token config file size");
        if ((cfg_json = (char *)RV_malloc(file_size + 1)) == NULL)
            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTALLOC, FAIL,
                            "Failed to allocate memory for token config file content");
        cfg_json[file_size] = '\0';
        if (fseek(token_cfg_file, 0, SEEK_SET) != 0)
            FUNC_GOTO_ERROR(H5E_VOL, H5E_SEEKERROR, FAIL, "Failed to fseek(0, SEEK_SET) token config file");
        if (fread((void *)cfg_json, sizeof(char), file_size, token_cfg_file) != file_size)
            FUNC_GOTO_ERROR(H5E_VOL, H5E_READERROR, FAIL, "Failed to read token config file");
        fclose(token_cfg_file);

#ifdef RV_CONNECTOR_DEBUG
        printf("-> JSON read from \"%s\":\n-> BEGIN JSON>%s<END JSON\n", token_cfg_file_pathname, cfg_json);
#endif

        /* Parse token config file */
        if (NULL == (parse_tree = yajl_tree_parse(cfg_json, NULL, 0)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_PARSEERROR, FAIL, "Failed to parse token config JSON");

        /* Get access token for the HSDS endpoint */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, cfg_access_token, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_PARSEERROR, FAIL, "can't retrieve access token");
        if (NULL == (access_token = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_PARSEERROR, FAIL, "can't retrieve access token's value");
#ifdef RV_CONNECTOR_DEBUG
        printf("-> Access token:\n-> \"%s\"\n", access_token);
#endif

        /* Get refresh token for the HSDS endpoint */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, cfg_refresh_token, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_PARSEERROR, FAIL, "can't retrieve refresh token");
        if (NULL == (refresh_token = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_PARSEERROR, FAIL, "can't retrieve refresh token's value");
#ifdef RV_CONNECTOR_DEBUG
        printf("-> Refresh token:\n-> \"%s\"\n", refresh_token);
#endif

        /* Get token expiration for the HSDS endpoint */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, cfg_token_expires, yajl_t_number)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_PARSEERROR, FAIL, "can't retrieve token expiration");
        if (!YAJL_IS_NUMBER(key_obj))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "token expiration's value is not a number");
        token_expires = (time_t)YAJL_GET_DOUBLE(key_obj);
        if (time(NULL) > token_expires)
            FUNC_GOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Access token expired");
#ifdef RV_CONNECTOR_DEBUG
        printf("-> Access token expires on: %ld\n", token_expires);
#endif

        /* Clean up */
        RV_free(cfg_json);
    } /* end if */
    else {
#ifdef RV_CONNECTOR_DEBUG
        printf("-> Token config file %s not found\n", token_cfg_file_pathname);
#endif
        if (ad_info->unattended) {
            if (ad_info->client_secret[0] == '\0')
                FUNC_GOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL,
                                "Active Directory authentication client secret is NULL");

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Performing unattended authentication\n");
#endif

            /* Set cURL up to authenticate device with AD in unattended manner */
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST"))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set HTTP GET operation: %s", curl_err_buf);

            /* Form URL from tenant ID string */
            if (snprintf(tenant_string, sizeof(tenant_string),
                         "https://login.microsoftonline.com/%s/oauth2/v2.0/token", ad_info->tenantID) < 0)
                FUNC_GOTO_ERROR(H5E_VOL, H5E_SYSERRSTR, FAIL, "snprintf error");

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, tenant_string))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf);

            /* Form token string */
            if (snprintf(data_string, sizeof(data_string),
                         "grant_type=client_credentials&client_id=%s&scope=%s/.default&client_secret=%s",
                         ad_info->clientID, ad_info->resourceID, ad_info->client_secret) < 0)
                FUNC_GOTO_ERROR(H5E_VOL, H5E_SYSERRSTR, FAIL, "snprintf error");

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data_string))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set cURL POST data: %s", curl_err_buf);

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Authentication server URL: \"%s\"\n", tenant_string);
            printf("-> Request payload: \"%s\"\n", data_string);
#endif
            CURL_PERFORM(curl, H5E_VOL, H5E_CANTGET, FAIL);
#ifdef RV_CONNECTOR_DEBUG
            printf("-> Authentication server response:\n-> \"%s\"\n", response_buffer.buffer);
#endif
        } /* end if */
        else {
            const char *ad_auth_message_keys[] = {"message", (const char *)0};
            const char *device_code_keys[]     = {"device_code", (const char *)0};
            char       *device_code            = NULL;
            char       *instruction_string;

            /* Set cURL up to authenticate device with AD in attended manner */

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST"))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set HTTP POST operation: %s",
                                curl_err_buf);

            /* Form URL from tenant ID string */
            if (snprintf(tenant_string, sizeof(tenant_string),
                         "https://login.microsoftonline.com/%s/oauth2/v2.0/devicecode",
                         ad_info->tenantID) < 0)
                FUNC_GOTO_ERROR(H5E_VOL, H5E_SYSERRSTR, FAIL, "snprintf error");

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, tenant_string))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf);

            /* Form client ID string */
            if (snprintf(data_string, sizeof(data_string), "client_id=%s&scope=%s/.default",
                         ad_info->clientID, ad_info->resourceID) < 0)
                FUNC_GOTO_ERROR(H5E_VOL, H5E_SYSERRSTR, FAIL, "snprintf error");

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data_string))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set cURL POST data: %s", curl_err_buf);

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Authentication server URL: \"%s\"\n", tenant_string);
            printf("-> Request payload: \"%s\"\n", data_string);
#endif
            CURL_PERFORM(curl, H5E_VOL, H5E_CANTGET, FAIL);

            /* Retrieve and print out authentication instructions message to user and wait for
             * their input after authenticating */
#ifdef RV_CONNECTOR_DEBUG
            printf("-> Authentication server response:\n-> \"%s\"\n", response_buffer.buffer);
#endif
            if (NULL == (parse_tree = yajl_tree_parse(response_buffer.buffer, NULL, 0)))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_PARSEERROR, FAIL, "JSON parse tree creation failed");

            /* Retrieve the authentication message */
            if (NULL == (key_obj = yajl_tree_get(parse_tree, ad_auth_message_keys, yajl_t_string)))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_PARSEERROR, FAIL,
                                "can't retrieve authentication instructions message");
            if (NULL == (instruction_string = YAJL_GET_STRING(key_obj)))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_PARSEERROR, FAIL,
                                "can't retrieve authentication instructions message");

            printf("Please follow the instructions below\n %s\n\nHit Enter when completed",
                   instruction_string);
            getchar();

            /* Assume that the user has now properly signed in - attempt to retrieve token */

            if (NULL == (key_obj = yajl_tree_get(parse_tree, device_code_keys, yajl_t_string)))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_PARSEERROR, FAIL, "can't retrieve authentication device code");

            if (NULL == (device_code = YAJL_GET_STRING(key_obj)))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_PARSEERROR, FAIL, "can't retrieve authentication device code");
#ifdef RV_CONNECTOR_DEBUG
            printf("-> Device code: \"%s\"\n", device_code);
#endif

            /* Form URL from tenant ID string */
            if (snprintf(tenant_string, sizeof(tenant_string),
                         "https://login.microsoftonline.com/%s/oauth2/v2.0/token", ad_info->tenantID) < 0)
                FUNC_GOTO_ERROR(H5E_VOL, H5E_SYSERRSTR, FAIL, "snprintf error");

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, tenant_string))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf);

            /* Form token string */
            if (snprintf(data_string, sizeof(data_string),
                         "grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Adevice_code&device_code="
                         "%s&client_id=%s",
                         device_code, ad_info->clientID) < 0)
                FUNC_GOTO_ERROR(H5E_VOL, H5E_SYSERRSTR, FAIL, "snprintf error");

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data_string))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set cURL POST data: %s", curl_err_buf);

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Authentication server URL: \"%s\"\n", tenant_string);
            printf("-> Request payload: \"%s\"\n", data_string);
#endif
            /* Request access token from the server */
            CURL_PERFORM(curl, H5E_VOL, H5E_CANTGET, FAIL);

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Authentication server response:\n-> %s\n", response_buffer.buffer);
#endif

        } /* end else */

        /* Parse response JSON */
        yajl_tree_free(parse_tree);
        parse_tree = NULL;
        if (NULL == (parse_tree = yajl_tree_parse(response_buffer.buffer, NULL, 0)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_PARSEERROR, FAIL, "JSON parse tree creation failed");

        /* Get access token */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, access_token_key, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_PARSEERROR, FAIL, "can't retrieve access token");
        if (NULL == (access_token = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_PARSEERROR, FAIL, "can't retrieve access token string");
#ifdef RV_CONNECTOR_DEBUG
        printf("-> Access token:\n-> \"%s\"\n", access_token);
#endif

        /* Get access token's validity period */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, expires_in_key, yajl_t_number)))
            FUNC_GOTO_ERROR(H5E_VOL, H5E_PARSEERROR, FAIL, "can't retrieve expires_in key");
        if (!YAJL_IS_INTEGER(key_obj))
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "returned expires_in value is not an integer");
        token_expires = YAJL_GET_INTEGER(key_obj);
#ifdef RV_CONNECTOR_DEBUG
        printf("-> Access token expires after %ld (duration: %ld seconds)\n", time(NULL) + token_expires,
               token_expires);
#endif
        token_expires = time(NULL) + token_expires - 1;

        /* Get refresh token (optional) */
        if (NULL != (key_obj = yajl_tree_get(parse_tree, refresh_token_key, yajl_t_string))) {
            if (NULL == (refresh_token = YAJL_GET_STRING(key_obj)))
                FUNC_GOTO_ERROR(H5E_VOL, H5E_PARSEERROR, FAIL, "can't retrieve refresh token's string value");
#ifdef RV_CONNECTOR_DEBUG
            printf("-> Refresh token:\n-> \"%s\"\n", refresh_token);
#endif
        }
        else {
#ifdef RV_CONNECTOR_DEBUG
            printf("-> Refresh token NOT PROVIDED\n");
#endif
        }
    } /* end else */

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Authentication successfully completed.\n");
#endif

    /* Set access token with cURL for future authentication */
    if (CURLE_OK != (curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, access_token)))
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't set OAuth access token: %s", curl_err_buf);

done:
    RV_free(token_cfg_file_pathname);

    /* Reset custom request on cURL pointer */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL))
        FUNC_DONE_ERROR(H5E_VOL, H5E_CANTSET, FAIL, "can't reset cURL custom request: %s", curl_err_buf);

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    if (parse_tree)
        yajl_tree_free(parse_tree);

    /* Clear out memory */
    memset(data_string, 0, sizeof(data_string));
    memset(tenant_string, 0, sizeof(tenant_string));

    return ret_value;
} /* end H5_rest_authenticate_with_AD() */

/************************************
 *         Helper functions         *
 ************************************/

/*-------------------------------------------------------------------------
 * Function:    H5_rest_curl_read_data_callback
 *
 * Purpose:     A callback for cURL which will copy the data from a given
 *              buffer into cURL's internal buffer when making an HTTP PUT
 *              call to the server.
 *
 * Return:      Amount of bytes uploaded, 0 if transfer finished or
 *              NULL data buffer is given
 *
 * Programmer:  Jordan Henderson
 *              January, 2018
 */
static size_t
H5_rest_curl_read_data_callback(char *buffer, size_t size, size_t nmemb, void *inptr)
{
    upload_info *uinfo        = (upload_info *)inptr;
    size_t       max_buf_size = size * nmemb;
    size_t       data_size    = 0;

    if (inptr) {
        size_t bytes_left = 0;

        /* If all bytes sent, indicate transfer is finished */
        if (uinfo->bytes_sent >= uinfo->buffer_size) {
            return 0;
        }

        bytes_left = uinfo->buffer_size - uinfo->bytes_sent;
        data_size  = (bytes_left > max_buf_size) ? max_buf_size : bytes_left;

        memcpy(buffer, (const char *)uinfo->buffer + uinfo->bytes_sent, data_size);

        uinfo->bytes_sent += data_size;
    } /* end if */

    return data_size;
} /* end H5_rest_curl_read_data_callback() */

/*-------------------------------------------------------------------------
 * Function:    H5_rest_curl_write_data_callback
 *
 * Purpose:     A callback for cURL which allows cURL to write its
 *              responses from the server into a growing string buffer
 *              which is processed by this VOL connector after each server
 *              interaction.
 *
 * Return:      Amount of bytes equal to the amount given to this callback
 *              by cURL on success/differing amount of bytes on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static size_t
H5_rest_curl_write_data_callback(char *buffer, size_t size, size_t nmemb, void *userp)
{
    ptrdiff_t buf_ptrdiff;
    size_t    data_size = size * nmemb;
    size_t    ret_value = 0;

    /* If the server response is larger than the currently allocated amount for the
     * response buffer, grow the response buffer by a factor of 2
     */
    buf_ptrdiff = (response_buffer.curr_buf_ptr + data_size) - response_buffer.buffer;
    if (buf_ptrdiff < 0)
        FUNC_GOTO_ERROR(
            H5E_INTERNAL, H5E_BADVALUE, 0,
            "unsafe cast: response buffer pointer difference was negative - this should not happen!");

    /* Avoid using the 'CHECKED_REALLOC' macro here because we don't necessarily
     * want to free the connector's response buffer if the reallocation fails.
     */
    while ((size_t)(buf_ptrdiff + 1) > response_buffer.buffer_size) {
        char *tmp_realloc;

        if (NULL ==
            (tmp_realloc = (char *)RV_realloc(response_buffer.buffer, 2 * response_buffer.buffer_size)))
            FUNC_GOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, 0, "can't reallocate space for response buffer");

        response_buffer.curr_buf_ptr = tmp_realloc + (response_buffer.curr_buf_ptr - response_buffer.buffer);
        response_buffer.buffer       = tmp_realloc;
        response_buffer.buffer_size *= 2;
    } /* end while */

    memcpy(response_buffer.curr_buf_ptr, buffer, data_size);
    response_buffer.curr_buf_ptr += data_size;
    *response_buffer.curr_buf_ptr = '\0';

    ret_value = data_size;

done:
    return ret_value;
} /* end H5_rest_curl_write_data_callback() */

/*-------------------------------------------------------------------------
 * Function:    H5_rest_curl_write_data_callback_no_global
 *
 * Purpose:     A callback for cURL which allows cURL to write its
 *              responses from the server into a growing string buffer
 *              which is processed by this VOL connector after each server
 *              interaction.
 *
 *              This callback use userp to find a buffer to write to, instead
 *              of using the global buffer. This allows it to safely be used by
 *              multiple curl handles at the same time.
 *
 * Return:      Amount of bytes equal to the amount given to this callback
 *              by cURL on success/differing amount of bytes on failure
 *
 * Programmer:  Matthew Larson
 *              June, 2023
 */
size_t
H5_rest_curl_write_data_callback_no_global(char *buffer, size_t size, size_t nmemb, void *userp)
{
    ptrdiff_t               buf_ptrdiff;
    size_t                  data_size             = size * nmemb;
    size_t                  ret_value             = 0;
    struct response_buffer *local_response_buffer = (struct response_buffer *)userp;

    /* If the server response is larger than the currently allocated amount for the
     * response buffer, grow the response buffer by a factor of 2
     */
    buf_ptrdiff = (local_response_buffer->curr_buf_ptr + data_size) - local_response_buffer->buffer;
    if (buf_ptrdiff < 0)
        FUNC_GOTO_ERROR(
            H5E_INTERNAL, H5E_BADVALUE, 0,
            "unsafe cast: response buffer pointer difference was negative - this should not happen!");

    /* Avoid using the 'CHECKED_REALLOC' macro here because we don't necessarily
     * want to free the connector's response buffer if the reallocation fails.
     */
    while ((size_t)(buf_ptrdiff + 1) > local_response_buffer->buffer_size) {
        char *tmp_realloc;

        if (NULL == (tmp_realloc = (char *)RV_realloc(local_response_buffer->buffer,
                                                      2 * local_response_buffer->buffer_size)))
            FUNC_GOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, 0, "can't reallocate space for response buffer");

        local_response_buffer->curr_buf_ptr =
            tmp_realloc + (local_response_buffer->curr_buf_ptr - local_response_buffer->buffer);
        local_response_buffer->buffer = tmp_realloc;
        local_response_buffer->buffer_size *= 2;
    } /* end while */

    memcpy(local_response_buffer->curr_buf_ptr, buffer, data_size);
    local_response_buffer->curr_buf_ptr += data_size;
    *local_response_buffer->curr_buf_ptr = '\0';

    ret_value = data_size;

done:
    return ret_value;
} /* end H5_rest_curl_write_data_callback_no_global() */

/*-------------------------------------------------------------------------
 * Function:    H5_rest_basename
 *
 * Purpose:     A portable implementation of the basename routine which
 *              retrieves everything after the final '/' in a given
 *              pathname.
 *
 *              Note that for performance and simplicity this function
 *              exhibits the GNU behavior in that it will return the empty
 *              string if the pathname contains a trailing '/'. Therefore,
 *              if a user provides a path that contains a trailing '/',
 *              this will likely confuse the connector and lead to incorrect
 *              behavior. If this becomes an issue in the future, this
 *              function may need to be reimplemented.
 *
 * Return:      Basename portion of the given path
 *              (can't fail)
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
const char *
H5_rest_basename(const char *path)
{
    char *substr = strrchr(path, '/');
    return substr ? substr + 1 : path;
} /* end H5_rest_basename() */

/*-------------------------------------------------------------------------
 * Function:    H5_rest_dirname
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
char *
H5_rest_dirname(const char *path)
{
    size_t len     = (size_t)(H5_rest_basename(path) - path);
    char  *dirname = NULL;

    if (NULL == (dirname = (char *)RV_malloc(len + 1)))
        return NULL;

    memcpy(dirname, path, len);
    dirname[len] = '\0';

    return dirname;
} /* end H5_rest_dirname() */

/*-------------------------------------------------------------------------
 * Function:    H5_rest_url_encode_path
 *
 * Purpose:     A helper function to URL-encode an entire path name by
 *              URL-encoding each of its separate components and then
 *              sticking them back together into a single string.
 *
 * Return:      URL-encoded version of the given path on success/NULL on
 *              failure
 *
 * Programmer:  Jordan Henderson
 *              January, 2018
 */
static char *
H5_rest_url_encode_path(const char *_path)
{
    ptrdiff_t buf_ptrdiff;
    size_t    bytes_nalloc;
    size_t    path_prefix_len;
    size_t    path_component_len;
    char     *path_copy                  = NULL;
    char     *url_encoded_path_component = NULL;
    char     *token;
    char     *cur_pos;
    char     *path       = NULL;
    char     *tmp_buffer = NULL;
    char     *ret_value  = NULL;

    if (!_path)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "path was NULL");

    path = (char *)_path;

    /* Retrieve the length of the possible path prefix, which could be something like '/', '.', etc. */
    cur_pos = path;
    while (*cur_pos && !isalnum(*cur_pos))
        cur_pos++;
    path_prefix_len = (size_t)(cur_pos - path);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Length of path prefix: %zu\n\n", path_prefix_len);
#endif

    /* Copy the given path (minus the path prefix) so that strtok() can safely modify it */
    bytes_nalloc = strlen(path) - path_prefix_len + 1;
    if (NULL == (path_copy = RV_malloc(bytes_nalloc)))
        FUNC_GOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, NULL, "can't allocate space for copy of path");

    strncpy(path_copy, path + path_prefix_len, bytes_nalloc);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Allocated copy of path: %s\n\n", path_copy);
#endif

    /* As URLs generally tend to be short and URL-encoding in the worst case will generally
     * introduce 3x the memory usage for a given URL (since URL-encoded characters are
     * represented by a '%' followed by two hexadecimal digits), go ahead and allocate
     * the resulting buffer to this size and grow it dynamically if really necessary.
     */
    bytes_nalloc = (3 * bytes_nalloc) + path_prefix_len + 1;
    if (NULL == (tmp_buffer = RV_malloc(bytes_nalloc)))
        FUNC_GOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, NULL,
                        "can't allocate space for resulting URL-encoded path buffer");

    cur_pos = tmp_buffer;

    /* Append the path's possible prefix, along with the first path component. This is to handle relative
     * vs absolute pathnames, where a leading '/' or '.' may or may not be present */
    if (path_prefix_len) {
        strncpy(cur_pos, path, path_prefix_len);
        cur_pos += path_prefix_len;
    } /* end if */

    if ((token = strtok(path_copy, "/"))) {
        if (NULL == (url_encoded_path_component = curl_easy_escape(curl, token, 0)))
            FUNC_GOTO_ERROR(H5E_NONE_MAJOR, H5E_CANTENCODE, NULL, "can't URL-encode path component");

        path_component_len = strlen(url_encoded_path_component);

        buf_ptrdiff = cur_pos - tmp_buffer;
        if (buf_ptrdiff < 0)
            FUNC_GOTO_ERROR(
                H5E_INTERNAL, H5E_BADVALUE, NULL,
                "unsafe cast: path buffer pointer difference was negative - this should not happen!");

        CHECKED_REALLOC(tmp_buffer, bytes_nalloc, (size_t)buf_ptrdiff + path_component_len, cur_pos,
                        H5E_RESOURCE, NULL);

        strncpy(cur_pos, url_encoded_path_component, path_component_len);
        cur_pos += path_component_len;

        curl_free(url_encoded_path_component);
        url_encoded_path_component = NULL;
    } /* end if */

    /* For each of the rest of the path components, URL-encode it and then append it into
     * the resulting path buffer, dynamically growing the buffer as necessary.
     */
    for (token = strtok(NULL, "/"); token; token = strtok(NULL, "/")) {
#ifdef RV_CONNECTOR_DEBUG
        printf("-> Processing next token: %s\n\n", token);
#endif

        if (NULL == (url_encoded_path_component = curl_easy_escape(curl, token, 0)))
            FUNC_GOTO_ERROR(H5E_NONE_MAJOR, H5E_CANTENCODE, NULL, "can't URL-encode path component");

#ifdef RV_CONNECTOR_DEBUG
        printf("-> URL-encoded form of token: %s\n\n", url_encoded_path_component);
#endif

        path_component_len = strlen(url_encoded_path_component);

        /* Check if the path components buffer needs to be grown */
        buf_ptrdiff = cur_pos - tmp_buffer;
        if (buf_ptrdiff < 0)
            FUNC_GOTO_ERROR(
                H5E_INTERNAL, H5E_BADVALUE, NULL,
                "unsafe cast: path buffer pointer difference was negative - this should not happen!");

        CHECKED_REALLOC(tmp_buffer, bytes_nalloc, (size_t)buf_ptrdiff + path_component_len + 1, cur_pos,
                        H5E_RESOURCE, NULL);

        *cur_pos++ = '/';
        strncpy(cur_pos, url_encoded_path_component, path_component_len);
        cur_pos += path_component_len;

        curl_free(url_encoded_path_component);
        url_encoded_path_component = NULL;
    } /* end for */

    buf_ptrdiff = cur_pos - tmp_buffer;
    if (buf_ptrdiff < 0)
        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, NULL,
                        "unsafe cast: path buffer pointer difference was negative - this should not happen!");

    CHECKED_REALLOC(tmp_buffer, bytes_nalloc, (size_t)buf_ptrdiff + 1, cur_pos, H5E_RESOURCE, NULL);

    *cur_pos = '\0';

#ifdef RV_CONNECTOR_DEBUG
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
} /* end H5_rest_url_encode_path() */

/* Helper function to parse an object's type from server response */
herr_t
RV_parse_type(char *HTTP_response, void *callback_data_in, void *callback_data_out)
{
    yajl_val    parse_tree = NULL, key_obj;
    char       *parsed_object_string;
    H5I_type_t *object_type = (H5I_type_t *)callback_data_out;
    herr_t      ret_value   = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Retrieving object's class from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL");
    if (!object_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "output buffer was NULL");

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "parsing JSON failed");

    const char *object_class_keys[] = {"class", (const char *)0};

    if (NULL == (key_obj = yajl_tree_get(parse_tree, object_class_keys, yajl_t_string))) {
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "couldn't parse object class");
    }

    if (!YAJL_IS_STRING(key_obj))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "parsed object class is not a string");

    if (NULL == (parsed_object_string = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "parsed object class is NULL");

    if (!strcmp(parsed_object_string, "group")) {
        *object_type = H5I_GROUP;
    }
    else if (!strcmp(parsed_object_string, "dataset")) {
        *object_type = H5I_DATASET;
    }
    else if (!strcmp(parsed_object_string, "datatype")) {
        *object_type = H5I_DATATYPE;
    }
    else {
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "parsed object class is not recognized");
    }

done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_parse_type */

/*---------------------------------------------------------------------------
 * Function:    H5_rest_get_conn_cls
 *
 * Purpose:     Query the connector class.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5_rest_get_conn_cls(void *obj, H5VL_get_conn_lvl_t lvl, const struct H5VL_class_t **conn_cls)
{
    herr_t ret_value = SUCCEED;

    if (!conn_cls)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "conn_cls is NULL");

    *conn_cls = &H5VL_rest_g;

done:
    return ret_value;
} /* end H5_rest_get_conn_cls() */

/*---------------------------------------------------------------------------
 * Function:    H5_rest_get_cap_flags
 *
 * Purpose:     Retrieves the capability flags for this VOL connector.
 *
 * Return:      Non-negative on Success/Negative on failure
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5_rest_get_cap_flags(const void *info, uint64_t *cap_flags)
{
    herr_t ret_value = SUCCEED;

    if (!cap_flags)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid cap_flags parameter");

    /* Set the flags from the connector's capability flags field */
    *cap_flags = H5VL_rest_g.cap_flags;

done:
    return ret_value;
} /* end H5_rest_get_cap_flags() */

/*---------------------------------------------------------------------------
 * Function:    H5_rest_opt_query
 *
 * Purpose:     Query if an optional operation is supported by this connector
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5_rest_opt_query(void *obj, H5VL_subclass_t cls, int opt_type, uint64_t *flags)
{
    herr_t ret_value = SUCCEED;

    if (!flags)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "\"flags\" is NULL");

    /* Advertise no current support for any optional operations that are queried. */
    *flags = FALSE;

done:
    return ret_value;
} /* end H5_rest_opt_query() */

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
herr_t
RV_parse_response(char *HTTP_response, void *callback_data_in, void *callback_data_out,
                  herr_t (*parse_callback)(char *, void *, void *))
{
    herr_t ret_value = SUCCEED;

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL");

    if (parse_callback && parse_callback(HTTP_response, callback_data_in, callback_data_out) < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CALLBACK, FAIL, "can't perform callback operation");

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
 *              connector to capture the URI of an object after making a
 *              request to the server, such as creating a new object or
 *              retrieving information from an existing object.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
herr_t
RV_copy_object_URI_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out)
{
    yajl_val parse_tree = NULL, key_obj;
    char    *parsed_string;
    char    *buf_out   = (char *)callback_data_out;
    herr_t   ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Retrieving object's URI from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL");
    if (!buf_out)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "output buffer was NULL");

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "parsing JSON failed");

    /* To handle the awkward case of soft and external links, which do not return an "ID",
     * first check for the link class field and short circuit if it is found to be
     * equal to "H5L_TYPE_SOFT"
     */
    if (NULL != (key_obj = yajl_tree_get(parse_tree, link_class_keys, yajl_t_string))) {
        char *link_type;

        if (NULL == (link_type = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "link type string was NULL");

        if (!strcmp(link_type, "H5L_TYPE_SOFT") || !strcmp(link_type, "H5L_TYPE_EXTERNAL") ||
            !strcmp(link_type, "H5L_TYPE_UD"))
            FUNC_GOTO_DONE(SUCCEED);
    } /* end if */

    /* First attempt to retrieve the URI of the object by using the JSON key sequence
     * "link" -> "id", which is returned when making a GET Link request.
     */
    key_obj = yajl_tree_get(parse_tree, link_id_keys, yajl_t_string);
    if (key_obj) {
        if (!YAJL_IS_STRING(key_obj))
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "returned URI is not a string");

        if (NULL == (parsed_string = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "URI was NULL");

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Found object's URI by \"link\" -> \"id\" JSON key path\n\n");
#endif
    } /* end if */
    else {
#ifdef RV_CONNECTOR_DEBUG
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
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "returned URI is not a string");

            if (NULL == (parsed_string = YAJL_GET_STRING(key_obj)))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "URI was NULL");

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Found object's URI by \"id\" JSON key path\n\n");
#endif
        } /* end if */
        else {
#ifdef RV_CONNECTOR_DEBUG
            printf("-> Could not find object's URI by \"id\" JSON key path\n");
            printf("-> Trying \"root\" JSON key path instead\n");
#endif

            /* Could not find the object's URI by the JSON key "id". Try looking for
             * just the JSON key "root", which would generally correspond to trying to
             * retrieve the URI of a newly-created or opened file, or to a search for
             * the root group of a file.
             */
            if (NULL == (key_obj = yajl_tree_get(parse_tree, root_id_keys, yajl_t_string)))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTGET, FAIL, "retrieval of URI failed");

            if (!YAJL_IS_STRING(key_obj))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "returned URI is not a string");

            if (NULL == (parsed_string = YAJL_GET_STRING(key_obj)))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "URI was NULL");

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Found object's URI by \"root\" JSON key path\n\n");
#endif
        } /* end else */
    }     /* end else */

    strncpy(buf_out, parsed_string, URI_MAX_LENGTH);

done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_copy_object_URI_parse_callback() */

/*-------------------------------------------------------------------------
 * Function:    RV_find_object_by_path
 *
 * Purpose:     Given a pathname, this function is responsible for making
 *              one or more HTTP GET requests to the server in order to retrieve
 *              information about an object.
 *
 *              The type of the retrieved object will be stored at the
 *              provided target_object_type field.
 *
 *              If the server version is >= 0.8.0, then the follow_soft_link and
 *              follow_hard_link parameters are used in the outgoing
 *              request, so that if the provided path is to a symbolic link,
 *              the server will follow that link and subsequent symbolic links until
 *              it encounters a hard link to the final object. The given value for
 *              target_object_type does not affect operation, but should be passed as
 *              H5I_UNINIT for backwards compatibility with old server versions.
 *
 *              If the server version is < 0.8.0, then there are two cases,
 *              based on whether or not the caller knows the type of the
 *              object being searched for. If the type of the target object is not known, or if it
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
 *              target object is contained within, resolving soft links
 *              by repeated requests to the server,
 *              until it locates the final hard link to
 *              the target object, at which point it will set the resulting
 *              type. Once the type of the target object is known, the
 *              HTTP GET request to directly retrieve the object's
 *              information is made.
 *
 * Return:      Non-negative on success, negative on failure
 *
 * Programmer:  Jordan Henderson
 *              November, 2017
 */
htri_t
RV_find_object_by_path(RV_object_t *parent_obj, const char *obj_path, H5I_type_t *target_object_type,
                       herr_t (*obj_found_callback)(char *, void *, void *), void *callback_data_in,
                       void *callback_data_out)
{
    RV_object_t       *external_file    = NULL;
    hbool_t            is_relative_path = FALSE;
    size_t             host_header_len  = 0;
    H5L_info2_t        link_info;
    char              *url_encoded_link_name = NULL;
    char              *host_header           = NULL;
    char              *path_dirname          = NULL;
    char              *tmp_link_val          = NULL;
    char              *url_encoded_path_name = NULL;
    const char        *ext_filename          = NULL;
    const char        *ext_obj_path          = NULL;
    const char        *base_URL;
    char               request_url[URL_MAX_LENGTH];
    long               http_response;
    int                url_len = 0;
    server_api_version version;

    htri_t ret_value = FAIL;

    if (!parent_obj)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "parent object pointer was NULL");
    if (!obj_path)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "target path was NULL");
    if (!target_object_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "target object type pointer was NULL");
    if (H5I_FILE != parent_obj->obj_type && H5I_GROUP != parent_obj->obj_type &&
        H5I_DATATYPE != parent_obj->obj_type && H5I_DATASET != parent_obj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "parent object not a file, group, datatype or dataset");
    if ((base_URL = parent_obj->domain->u.file.server_info.base_URL) == NULL)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "parent object does not have valid server URL");
#ifdef RV_CONNECTOR_DEBUG
    printf("-> Finding object by path '%s' from parent object of type %s with URI %s\n\n", obj_path,
           object_type_to_string(parent_obj->obj_type), parent_obj->URI);
#endif

    version = parent_obj->domain->u.file.server_info.version;

    /* In order to not confuse the server, make sure the path has no leading spaces */
    while (*obj_path == ' ')
        obj_path++;

    /* Do a bit of pre-processing for optimizing */
    if (!strcmp(obj_path, ".")) {
        /* If the path "." is being searched for, referring to the current object, retrieve
         * the information about the current object and supply it to the optional callback.
         */

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Path provided was '.', short-circuiting to GET request and callback function\n\n");
#endif

        *target_object_type = parent_obj->obj_type;
        is_relative_path    = TRUE;
    } /* end if */
    else if (!strcmp(obj_path, "/")) {
        /* If the path "/" is being searched for, referring to the root group, retrieve the
         * information about the root group and supply it to the optional callback.
         */

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Path provided was '/', short-circuiting to GET request and callback function\n\n");
#endif

        *target_object_type = H5I_GROUP;
        is_relative_path    = FALSE;
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

    /* Based on server version and given object type, set up request URL */

    if (SERVER_VERSION_MATCHES_OR_EXCEEDS(version, 0, 8, 0)) {

        /* Set up request URL to make server do repeated traversal of symbolic links */

        if (NULL == (url_encoded_path_name = H5_rest_url_encode_path(obj_path)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode object path");

        if ((url_len = snprintf(request_url, URL_MAX_LENGTH,
                                "%s/?h5path=%s%s%s&follow_soft_links=1&follow_external_links=1", base_URL,
                                url_encoded_path_name, is_relative_path ? "&parent_id=" : "",
                                is_relative_path ? parent_obj->URI : "")) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");
    }
    else {
        /* Server will not traverse symbolic links for us */

        if (H5I_UNINIT == *target_object_type) {
            /* Set up intermediate request to get information about object type via link */
            hbool_t empty_dirname;
            htri_t  search_ret;
            char   *pobj_URI = parent_obj->URI;
            char    temp_URI[URI_MAX_LENGTH];

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Unknown target object type; retrieving object type\n\n");
#endif
            /* In case the user specified a path which contains multiple groups on the way to the
             * one which the object in question should be under, extract out the path to the final
             * group in the chain */
            if (NULL == (path_dirname = H5_rest_dirname(obj_path)))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't get path dirname");
            empty_dirname = !strcmp(path_dirname, "");

#ifdef RV_CONNECTOR_DEBUG
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
                    FUNC_GOTO_ERROR(H5E_SYM, H5E_PATH, FAIL,
                                    "can't locate parent group for object of unknown type");

                pobj_URI = temp_URI;
            }

            /* URL-encode link name so the request URL doesn't contain illegal characters */
            if (NULL == (url_encoded_link_name = curl_easy_escape(curl, H5_rest_basename(obj_path), 0)))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode link name");

            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/links/%s", base_URL, pobj_URI,
                                    url_encoded_link_name)) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");
        }
        else {
            /* Set up final HTTP GET request to retrieve information about the target object */

            /* Craft the request URL based on the type of the object we're looking for and whether or not
             * the path given is a relative path or not.
             */
            const char *parent_obj_type_header = NULL;

            if (RV_set_object_type_header(*target_object_type, &parent_obj_type_header) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL,
                                "target object not a group, datatype or dataset");

            /* Handle the special case for the paths "." and "/" */
            if (!strcmp(obj_path, ".") || !strcmp(obj_path, "/")) {
                if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/%s/%s", base_URL,
                                        parent_obj_type_header, parent_obj->URI)) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");
            }
            else {
                /* Assemble request URL for non-special path */
                bool is_group = (*target_object_type == H5I_GROUP) || (*target_object_type == H5I_FILE);

                if (NULL == (url_encoded_path_name = H5_rest_url_encode_path(obj_path)))
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode object path");

                /*  For groups (and files), relative request URL is of the form
                 *  [base_URL]/groups/[group_uri]?h5path=[path_name]
                 *  For other objects, relative request URL is of the form
                 *  [base_URL]/[object_type]/?grpid=[group_id]&h5path=[path_name]
                 *
                 *  For any objects, absolute request URL is of the form
                 *  [base_URL]/[object_type]/?h5path=[path_name]
                 */
                if ((url_len = snprintf(
                         request_url, URL_MAX_LENGTH, "%s/%s/%s?%s%s%sh5path=%s", base_URL,
                         parent_obj_type_header, (is_relative_path && is_group) ? parent_obj->URI : "",
                         (is_relative_path && !is_group) ? "grpid=" : "",
                         (is_relative_path && !is_group) ? parent_obj->URI : "",
                         (is_relative_path && !is_group) ? "&" : "", url_encoded_path_name)) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");
            } /* end else */
        }     /* end else */
    }         /* end else */

    if (url_len >= URL_MAX_LENGTH)
        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "Request URL size exceeded maximum URL size");

    /* Setup cURL for making GET requests */

    /* Setup the host header */
    host_header_len = strlen(parent_obj->domain->u.file.filepath_name) + strlen(host_string) + 1;
    if (NULL == (host_header = (char *)RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header");

    strcpy(host_header, host_string);

    curl_headers =
        curl_slist_append(curl_headers, strncat(host_header, parent_obj->domain->u.file.filepath_name,
                                                host_header_len - strlen(host_string) - 1));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP GET request: %s",
                        curl_err_buf);

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf);

#ifdef RV_CONNECTOR_DEBUG
    printf("   /**********************************\\\n");
    printf("-> | Making GET request to the server |\n");
    printf("   \\**********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_LINK, H5E_PATH, FAIL);

    if (CURLE_OK != curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_response))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't get HTTP response code");

    ret_value = HTTP_SUCCESS(http_response);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Object %s\n\n", ret_value ? "found" : "not found");
#endif

    /* Clean up the cURL headers to prevent issues in potential recursive call */
    curl_slist_free_all(curl_headers);
    curl_headers = NULL;

    if (SERVER_VERSION_MATCHES_OR_EXCEEDS(version, 0, 8, 0)) {

        if (0 > RV_parse_response(response_buffer.buffer, NULL, target_object_type, RV_parse_type))
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTGET, FAIL, "failed to get type from URI");
    }
    else {
        /* Old server version */

        if (H5I_UNINIT == *target_object_type) {
            /* This was an intermediate request, recurse to make next request */
            htri_t search_ret;

            if (RV_parse_response(response_buffer.buffer, NULL, &link_info, RV_get_link_info_callback) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't retrieve link type");

            if (H5L_TYPE_HARD == link_info.type) {
#ifdef RV_CONNECTOR_DEBUG
                printf("-> Link was a hard link; retrieving target object's info\n\n");
#endif
                if (RV_parse_response(response_buffer.buffer, NULL, target_object_type,
                                      RV_get_link_obj_type_callback) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL,
                                    "can't retrieve hard link's target object type");
            }
            else {
                size_t link_val_len = 0;

#ifdef RV_CONNECTOR_DEBUG
                printf("-> Link was a %s link; retrieving link's value\n\n",
                       H5L_TYPE_SOFT == link_info.type ? "soft" : "external");
#endif

                if (RV_parse_response(response_buffer.buffer, &link_val_len, NULL, RV_get_link_val_callback) <
                    0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't retrieve size of link's value");

                if (NULL == (tmp_link_val = RV_malloc(link_val_len)))
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate space for link's value");

                if (RV_parse_response(response_buffer.buffer, &link_val_len, tmp_link_val,
                                      RV_get_link_val_callback) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't retrieve link's value");

                if (H5L_TYPE_EXTERNAL == link_info.type) {
                    /* Unpack the external link's value buffer */
                    if (H5Lunpack_elink_val(tmp_link_val, link_val_len, NULL, &ext_filename,
                                            (const char **)&ext_obj_path) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL,
                                        "can't unpack external link's value buffer");

                    /* Attempt to open the file referenced by the external link using the same access flags as
                     * used to open the file that the link resides within. */
                    if (NULL ==
                        (external_file = RV_file_open(ext_filename, parent_obj->domain->u.file.intent,
                                                      parent_obj->domain->u.file.fapl_id, H5P_DEFAULT, NULL)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTOPENOBJ, FAIL,
                                        "can't open file referenced by external link");

                    parent_obj = external_file;
                    obj_path   = ext_obj_path;
                } /* end if */
                else {
                    obj_path = tmp_link_val;
                }
            } /* end if */

            search_ret = RV_find_object_by_path(parent_obj, obj_path, target_object_type, obj_found_callback,
                                                callback_data_in, callback_data_out);
            if (!search_ret || search_ret < 0)
                FUNC_GOTO_ERROR(H5E_SYM, H5E_PATH, FAIL, "can't locate target object by path");

            ret_value = search_ret;
        } /* end if for intermediate request */
    }     /* end if for old server version */

    /* Perform user-request callback on retrieved object */
    if (ret_value > 0) {
        if (obj_found_callback && RV_parse_response(response_buffer.buffer, callback_data_in,
                                                    callback_data_out, obj_found_callback) < 0)
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CALLBACK, FAIL, "can't perform callback operation");
    }

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
            FUNC_DONE_ERROR(H5E_LINK, H5E_CANTCLOSEOBJ, FAIL, "can't close file referenced by external link");

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    return ret_value;
} /* end RV_find_object_by_path */

/*-------------------------------------------------------------------------
 * Function:    RV_parse_creation_properties_callback
 *
 * Purpose:     Helper function to try to parse creation properties
 *              from given parse tree, into ptr at address GCPL_buf.
 *
 *              This is a separate function to allow it to fail
 *              gracefully when the object we're operating on
 *              doesn't have a creation properties field.
 *
 *              If it succeeds, it will allocate memory at
 *              *GCPL_buf that must be freed by user.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Matthew Larson
 *              May, 2023
 */
herr_t
RV_parse_creation_properties_callback(yajl_val parse_tree, char **GCPL_buf_out)
{
    herr_t   ret_value      = SUCCEED;
    yajl_val key_obj        = NULL;
    char    *parsed_string  = NULL;
    char    *GCPL_buf_local = NULL;

    if (!GCPL_buf_out)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "given GCPL buffer was NULL");
    if (!parse_tree)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "parse tree was NULL");

    if (NULL == (key_obj = yajl_tree_get(parse_tree, object_creation_properties_keys, yajl_t_string)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "failed to parse creationProperties");

    if (!YAJL_IS_STRING(key_obj))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "returned creationProperties is not a string");

    if (NULL == (parsed_string = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "creationProperties was NULL");

    if (NULL == (GCPL_buf_local = RV_malloc(strlen(parsed_string) + 1)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTALLOC, FAIL, "failed to allocate memory for creationProperties");

    memcpy(GCPL_buf_local, parsed_string, strlen(parsed_string) + 1);

    *GCPL_buf_out = GCPL_buf_local;

done:

    if (ret_value < 0) {
        RV_free(GCPL_buf_local);
        GCPL_buf_local = NULL;
    }

    return ret_value;
}

/*-------------------------------------------------------------------------
 * Function:    RV_copy_object_loc_info_callback
 *
 * Purpose:     Callback for RV_parse_response to populate
 *              a loc_info struct. Sets NULL for any fields not
 *              found in the particular response.
 *
 *              Allocates memory at *callback_data_out for each
 *              found field that must be freed by caller.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Matthew Larson
 *              May, 2023
 */
herr_t
RV_copy_object_loc_info_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out)
{
    yajl_val       parse_tree = NULL, key_obj;
    char          *parsed_string;
    loc_info      *loc_info_out = (loc_info *)callback_data_out;
    server_info_t *server_info  = (server_info_t *)callback_data_in;
    herr_t         ret_value    = SUCCEED;

    char *GCPL_buf = NULL;

    char        *parsed_id_string   = NULL;
    bool         is_external_domain = false;
    RV_object_t  found_domain;
    RV_object_t *new_domain = NULL;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Retrieving object's creation properties from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL");
    if (!loc_info_out)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "output buffer was NULL");
    if (!server_info)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "server info was NULL");

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "parsing JSON failed");

    /* Not all objects have a creationProperties field, so fail this gracefully */
    H5E_BEGIN_TRY
    {
        RV_parse_creation_properties_callback(parse_tree, &GCPL_buf);
    }
    H5E_END_TRY

    if (GCPL_buf != NULL) {
        loc_info_out->GCPL_base64 = GCPL_buf;
    }

    /* Retrieve domain path */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, domain_keys, yajl_t_string)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "failed to parse domain");

    if (!YAJL_IS_STRING(key_obj))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "returned domain is not a string");

    if (NULL == (found_domain.u.file.filepath_name = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "domain was NULL");

    /* Retrieve domain id */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, root_id_keys, yajl_t_string)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "failed to parse domain id");

    if (!YAJL_IS_STRING(key_obj))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "returned domain id is not a string");

    if (NULL == (parsed_id_string = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "returned domain id is NULL");

    if (strlen(parsed_id_string) > URI_MAX_LENGTH)
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "parsed domain id too large");

    if (NULL == strncpy(found_domain.URI, parsed_id_string, URI_MAX_LENGTH))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "failed to copy memory for domain id");

    is_external_domain =
        strcmp(found_domain.u.file.filepath_name, loc_info_out->domain->u.file.filepath_name);

    /* If retrieved domain is different than the domain through which this object
     * was accessed, replace the returned object's domain. */
    if (is_external_domain) {
        if (NULL == (new_domain = RV_malloc(sizeof(RV_object_t))))
            FUNC_GOTO_ERROR(H5E_CALLBACK, H5E_CANTALLOC, FAIL, "failed to allocate memory for new domain");

        memcpy(new_domain, &found_domain, sizeof(RV_object_t));

        /* Wait until after heap allocation to set self pointer */
        new_domain->domain   = new_domain;
        new_domain->obj_type = H5I_FILE;

        if (NULL ==
            (new_domain->u.file.filepath_name = RV_malloc(strlen(found_domain.u.file.filepath_name) + 1)))
            FUNC_GOTO_ERROR(H5E_CALLBACK, H5E_CANTALLOC, FAIL,
                            "failed to allocate memory for new domain path");

        strcpy(new_domain->u.file.filepath_name, found_domain.u.file.filepath_name);

        if ((new_domain->u.file.server_info.username = RV_malloc(strlen(server_info->username) + 1)) == NULL)
            FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL, "can't allocate space for copied username");

        strcpy(new_domain->u.file.server_info.username, server_info->username);

        if ((new_domain->u.file.server_info.password = RV_malloc(strlen(server_info->password) + 1)) == NULL)
            FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL, "can't allocate space for copied password");

        strcpy(new_domain->u.file.server_info.password, server_info->password);

        if ((new_domain->u.file.server_info.base_URL = RV_malloc(strlen(server_info->base_URL) + 1)) == NULL)
            FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL, "can't allocate space for copied URL");

        strcpy(new_domain->u.file.server_info.base_URL, server_info->base_URL);

        new_domain->u.file.intent              = loc_info_out->domain->u.file.intent;
        new_domain->u.file.fapl_id             = H5Pcopy(loc_info_out->domain->u.file.fapl_id);
        new_domain->u.file.fcpl_id             = H5Pcopy(loc_info_out->domain->u.file.fcpl_id);
        new_domain->u.file.ref_count           = 1;
        new_domain->u.file.server_info.version = found_domain.u.file.server_info.version;

        /* Allocate root "path" on heap for consistency with other RV_object_t types */
        if ((new_domain->handle_path = RV_malloc(2)) == NULL)
            FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL, "can't allocate space for filepath");

        strncpy(new_domain->handle_path, "/", 2);

        /* Assume that original domain and external domain have the same server version.
         * This will always be true unless it becomes possible for external links to point to
         * objects on different servers entirely. */
        memcpy(&new_domain->u.file.server_info.version, &loc_info_out->domain->u.file.server_info.version,
               sizeof(server_api_version));

        if (RV_file_close(loc_info_out->domain, H5P_DEFAULT, NULL) < 0)
            FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL, "failed to allocate memory for new domain path");

        loc_info_out->domain = new_domain;
    }

    /* URI */
    ret_value = RV_copy_object_URI_callback(HTTP_response, NULL, loc_info_out->URI);
done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    if ((ret_value < 0) && GCPL_buf) {
        RV_free(GCPL_buf);
        GCPL_buf                  = NULL;
        loc_info_out->GCPL_base64 = NULL;
    }

    return ret_value;
} /* end RV_copy_object_loc_info_callback() */

/*-------------------------------------------------------------------------
 * Function:    RV_copy_link_name_by_index
 *
 * Purpose:     This callback is used to copy the name of an link
 *              in the server's response by index. It allocates heap memory for the name,
 *              and returns a pointer to the memory by callback_data_out.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Matthew Larson
 *              May, 2023
 */
herr_t
RV_copy_link_name_by_index(char *HTTP_response, void *callback_data_in, void *callback_data_out)
{
    yajl_val           parse_tree = NULL, key_obj = NULL, link_obj = NULL;
    const char        *parsed_link_name   = NULL;
    char              *parsed_link_buffer = NULL;
    H5VL_loc_by_idx_t *idx_params         = (H5VL_loc_by_idx_t *)callback_data_in;
    hsize_t            index              = 0;
    char             **link_name          = (char **)callback_data_out;
    const char        *curr_key           = NULL;
    herr_t             ret_value          = SUCCEED;

    if (!idx_params)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "given index params ptr was NULL");

    if (!link_name)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "given link_name ptr was NULL");

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL");

    index = idx_params->n;

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "parsing JSON failed");

    if (NULL == (key_obj = yajl_tree_get(parse_tree, links_keys, yajl_t_array)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "failed to parse links");

    if (key_obj->u.array.len == 0)
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "parsed link array was empty");

    if (index >= key_obj->u.array.len)
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "requested link index was out of bounds");

    switch (idx_params->order) {
        case (H5_ITER_DEC):
            if (NULL == (link_obj = key_obj->u.array.values[key_obj->u.object.len - 1 - index]))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "selected link was NULL");
            break;

        case (H5_ITER_NATIVE):
        case (H5_ITER_INC):
            if (NULL == (link_obj = key_obj->u.array.values[index]))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "selected link was NULL");
            break;
        case (H5_ITER_N):
        case (H5_ITER_UNKNOWN):
        default: {
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "invalid iteration order");
            break;
        }
    }

    /* Iterate through key/value pairs in link response to find name */
    for (size_t i = 0; i < link_obj->u.object.len; i++) {
        curr_key = link_obj->u.object.keys[i];
        if (!strcmp(curr_key, "title"))
            if (NULL == (parsed_link_name = YAJL_GET_STRING(link_obj->u.object.values[i])))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "failed to get link name");
    }

    if (NULL == parsed_link_name)
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "server response didn't contain link name");

    if (NULL == (parsed_link_buffer = RV_malloc(strlen(parsed_link_name) + 1)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "failed to allocate memory for link name");

    memcpy(parsed_link_buffer, parsed_link_name, strlen(parsed_link_name) + 1);

    *link_name = parsed_link_buffer;

done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    if (ret_value < 0) {
        RV_free(parsed_link_buffer);
        parsed_link_buffer = NULL;
    }

    return ret_value;
} /* end RV_copy_link_name_by_index() */

/*-------------------------------------------------------------------------
 * Function:    RV_copy_attribute_name_by_index
 *
 * Purpose:     This callback is used to copy the name of an attribute
 *              in the server's response by index. It allocates heap memory for the name,
 *              and returns a pointer to the memory by callback_data_out.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Matthew Larson
 *              May, 2023
 */
herr_t
RV_copy_attribute_name_by_index(char *HTTP_response, void *callback_data_in, void *callback_data_out)
{
    yajl_val           parse_tree           = NULL, key_obj;
    const char        *parsed_string        = NULL;
    char              *parsed_string_buffer = NULL;
    H5VL_loc_by_idx_t *idx_params           = (H5VL_loc_by_idx_t *)callback_data_in;
    hsize_t            index                = 0;
    char             **attr_name            = (char **)callback_data_out;
    herr_t             ret_value            = SUCCEED;

    if (!attr_name)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "given attr_name was NULL");

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL");

    if (!idx_params)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "given index params ptr was NULL");

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "parsing JSON failed");

    if (NULL == (key_obj = yajl_tree_get(parse_tree, attributes_keys, yajl_t_object)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "failed to parse attributes");

    if (key_obj->u.object.len == 0)
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "parsed attribute array was empty");

    if (index >= key_obj->u.object.len)
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "requested attribute index was out of bounds");

    index = idx_params->n;

    switch (idx_params->order) {

        case (H5_ITER_DEC):
            if (NULL == (parsed_string = key_obj->u.object.keys[key_obj->u.object.len - 1 - index]))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "selected attribute had NULL name");
            break;

        case (H5_ITER_NATIVE):
        case (H5_ITER_INC): {
            if (NULL == (parsed_string = key_obj->u.object.keys[index]))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "selected attribute had NULL name");
            break;
        }

        case (H5_ITER_N):
        case (H5_ITER_UNKNOWN):
        default: {
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "invalid iteration order");
            break;
        }
    }

    if (NULL == (parsed_string_buffer = RV_malloc(strlen(parsed_string) + 1)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "failed to allocate memory for attribute name");

    memcpy(parsed_string_buffer, parsed_string, strlen(parsed_string) + 1);

    *attr_name = parsed_string_buffer;
done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    if (ret_value < 0) {
        RV_free(parsed_string_buffer);
        *attr_name = NULL;
    }

    return ret_value;
} /* end RV_copy_attribute_name_by_index() */

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
hid_t
RV_parse_dataspace(char *space)
{
    yajl_val parse_tree = NULL, key_obj = NULL;
    hsize_t *space_dims     = NULL;
    hsize_t *space_maxdims  = NULL;
    hid_t    dataspace      = FAIL;
    char    *dataspace_type = NULL;
    hid_t    ret_value      = FAIL;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Parsing dataspace from HTTP response\n\n");
#endif

    if (!space)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "dataspace string buffer was NULL");

    if (NULL == (parse_tree = yajl_tree_parse(space, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_PARSEERROR, FAIL, "JSON parse tree creation failed");

    /* Retrieve the Dataspace type */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, dataspace_class_keys, yajl_t_string)))
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_PARSEERROR, FAIL, "can't retrieve dataspace class");

    if (NULL == (dataspace_type = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_PARSEERROR, FAIL, "can't retrieve dataspace class");

    /* Create the appropriate type of Dataspace */
    if (!strcmp(dataspace_type, "H5S_NULL")) {
#ifdef RV_CONNECTOR_DEBUG
        printf("-> NULL dataspace\n\n");
#endif

        if ((dataspace = H5Screate(H5S_NULL)) < 0)
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCREATE, FAIL, "can't create null dataspace");
    } /* end if */
    else if (!strcmp(dataspace_type, "H5S_SCALAR")) {
#ifdef RV_CONNECTOR_DEBUG
        printf("-> SCALAR dataspace\n\n");
#endif

        if ((dataspace = H5Screate(H5S_SCALAR)) < 0)
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCREATE, FAIL, "can't create scalar dataspace");
    } /* end if */
    else if (!strcmp(dataspace_type, "H5S_SIMPLE")) {
        yajl_val dims_obj = NULL, maxdims_obj = NULL;
        hbool_t  maxdims_specified = TRUE;
        size_t   i;

#ifdef RV_CONNECTOR_DEBUG
        printf("-> SIMPLE dataspace\n\n");
#endif

        if (NULL == (dims_obj = yajl_tree_get(parse_tree, dataspace_dims_keys, yajl_t_array)))
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_PARSEERROR, FAIL, "can't retrieve dataspace dims");

        /* Check to see whether the maximum dimension size is specified as part of the
         * dataspace's JSON representation
         */
        if (NULL == (maxdims_obj = yajl_tree_get(parse_tree, dataspace_max_dims_keys, yajl_t_array)))
            maxdims_specified = FALSE;

        if (!YAJL_GET_ARRAY(dims_obj)->len)
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "0-sized dataspace dimensionality array");

        if (NULL == (space_dims = (hsize_t *)RV_malloc(YAJL_GET_ARRAY(dims_obj)->len * sizeof(*space_dims))))
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL,
                            "can't allocate space for dataspace dimensionality array");

        if (maxdims_specified)
            if (NULL == (space_maxdims =
                             (hsize_t *)RV_malloc(YAJL_GET_ARRAY(maxdims_obj)->len * sizeof(*space_maxdims))))
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL,
                                "can't allocate space for dataspace maximum dimensionality array");

        for (i = 0; i < dims_obj->u.array.len; i++) {
            long long val = YAJL_GET_INTEGER(dims_obj->u.array.values[i]);

            space_dims[i] = (hsize_t)val;

            if (maxdims_specified) {
                val = YAJL_GET_INTEGER(maxdims_obj->u.array.values[i]);

                space_maxdims[i] = (val == 0) ? H5S_UNLIMITED : (hsize_t)val;
            } /* end if */
        }     /* end for */

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Creating simple dataspace\n");
        printf("-> Dims: [ ");
        for (i = 0; i < dims_obj->u.array.len; i++) {
            if (i > 0)
                printf(", ");
            printf("%llu", space_dims[i]);
        }
        printf(" ]\n\n");
        if (maxdims_specified) {
            printf("-> MaxDims: [ ");
            for (i = 0; i < maxdims_obj->u.array.len; i++) {
                if (i > 0)
                    printf(", ");
                printf("%llu", space_maxdims[i]);
            }
            printf(" ]\n\n");
        }
#endif

        if ((dataspace = H5Screate_simple((int)dims_obj->u.array.len, space_dims, space_maxdims)) < 0)
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCREATE, FAIL, "can't create simple dataspace");
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
herr_t
RV_convert_dataspace_shape_to_JSON(hid_t space_id, char **shape_body, char **maxdims_body)
{
    H5S_class_t space_type;
    ptrdiff_t   buf_ptrdiff;
    hsize_t    *dims               = NULL;
    hsize_t    *maxdims            = NULL;
    char       *shape_out_string   = NULL;
    char       *maxdims_out_string = NULL;
    herr_t      ret_value          = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Converting dataspace to JSON representation\n\n");
#endif

    if (H5S_NO_CLASS == (space_type = H5Sget_simple_extent_type(space_id)))
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "invalid dataspace");

    /* Scalar dataspaces operate upon the assumption that if no shape
     * is specified in the request body during the creation of an object,
     * the server will create the object with a scalar dataspace.
     */
    if (H5S_SCALAR == space_type)
        FUNC_GOTO_DONE(SUCCEED);

    /* Allocate space for each buffer */
    if (shape_body)
        if (NULL == (shape_out_string = (char *)RV_malloc(DATASPACE_SHAPE_BUFFER_DEFAULT_SIZE)))
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL,
                            "can't allocate space for dataspace shape buffer");
    if (H5S_NULL != space_type) {
        if (maxdims_body)
            if (NULL == (maxdims_out_string = (char *)RV_malloc(DATASPACE_SHAPE_BUFFER_DEFAULT_SIZE)))
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL,
                                "can't allocate space for dataspace maximum dimension size buffer");
    } /* end if */

    /* Ensure that both buffers are NUL-terminated */
    if (shape_out_string)
        *shape_out_string = '\0';
    if (maxdims_out_string)
        *maxdims_out_string = '\0';

    switch (space_type) {
        case H5S_NULL: {
            const char *const null_str                  = "\"shape\": \"H5S_NULL\"";
            size_t            null_strlen               = strlen(null_str);
            size_t            shape_out_string_curr_len = DATASPACE_SHAPE_BUFFER_DEFAULT_SIZE;

            CHECKED_REALLOC_NO_PTR(shape_out_string, shape_out_string_curr_len, null_strlen + 1,
                                   H5E_DATASPACE, FAIL);

            strncat(shape_out_string, null_str, null_strlen);
            break;
        } /* H5S_NULL */

        case H5S_SIMPLE: {
            const char *const shape_key                   = "\"shape\": [";
            const char *const maxdims_key                 = "\"maxdims\": [";
            size_t            shape_out_string_curr_len   = DATASPACE_SHAPE_BUFFER_DEFAULT_SIZE;
            size_t            maxdims_out_string_curr_len = DATASPACE_SHAPE_BUFFER_DEFAULT_SIZE;
            size_t            i;
            char             *shape_out_string_curr_pos   = shape_out_string;
            char             *maxdims_out_string_curr_pos = maxdims_out_string;
            int               space_ndims;
            int               bytes_printed;

            if ((space_ndims = H5Sget_simple_extent_ndims(space_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL,
                                "can't get number of dimensions in dataspace");
            if (!space_ndims)
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "0-dimension dataspace");

            if (shape_out_string)
                if (NULL == (dims = (hsize_t *)RV_malloc((size_t)space_ndims * sizeof(*dims))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL,
                                    "can't allocate memory for dataspace dimensions");

            if (maxdims_out_string)
                if (NULL == (maxdims = (hsize_t *)RV_malloc((size_t)space_ndims * sizeof(*dims))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL,
                                    "can't allocate memory for dataspace maximum dimension sizes");

            if (H5Sget_simple_extent_dims(space_id, dims, maxdims) < 0)
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL,
                                "can't retrieve dataspace dimensions and maximum dimension sizes");

            /* Add the JSON key prefixes to their respective buffers */
            if (shape_out_string) {
                size_t shape_key_len = strlen(shape_key);

                CHECKED_REALLOC_NO_PTR(shape_out_string, shape_out_string_curr_len, shape_key_len + 1,
                                       H5E_DATASPACE, FAIL);
                strncat(shape_out_string_curr_pos, shape_key, shape_key_len);
                shape_out_string_curr_pos += shape_key_len;
            } /* end if */

            if (maxdims_out_string) {
                size_t maxdims_key_len = strlen(maxdims_key);

                CHECKED_REALLOC_NO_PTR(maxdims_out_string, maxdims_out_string_curr_len, maxdims_key_len + 1,
                                       H5E_DATASPACE, FAIL);
                strncat(maxdims_out_string_curr_pos, maxdims_key, maxdims_key_len);
                maxdims_out_string_curr_pos += maxdims_key_len;
            } /* end if */

            /* For each dimension, append values to the respective string buffers according to
             * the dimension size and maximum dimension size of each dimension.
             */
            for (i = 0; i < (size_t)space_ndims; i++) {
                /* Check whether the shape and maximum dimension size string buffers
                 * need to be grown before appending the values for the next dimension
                 * into the buffers */
                if (shape_out_string) {
                    buf_ptrdiff = shape_out_string_curr_pos - shape_out_string;
                    if (buf_ptrdiff < 0)
                        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                        "unsafe cast: dataspace buffer pointer difference was negative - "
                                        "this should not happen!");

                    size_t shape_out_string_new_len = (size_t)buf_ptrdiff + MAX_NUM_LENGTH + 1;

                    CHECKED_REALLOC(shape_out_string, shape_out_string_curr_len, shape_out_string_new_len,
                                    shape_out_string_curr_pos, H5E_DATASPACE, FAIL);

                    if ((bytes_printed = snprintf(shape_out_string_curr_pos,
                                                  shape_out_string_new_len - (size_t)buf_ptrdiff,
                                                  "%s%" PRIuHSIZE, i > 0 ? "," : "", dims[i])) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "snprintf error");
                    shape_out_string_curr_pos += bytes_printed;
                } /* end if */

                if (maxdims_out_string) {
                    buf_ptrdiff = maxdims_out_string_curr_pos - maxdims_out_string;
                    if (buf_ptrdiff < 0)
                        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                        "unsafe cast: dataspace buffer pointer difference was negative - "
                                        "this should not happen!");

                    size_t maxdims_out_string_new_len = (size_t)buf_ptrdiff + MAX_NUM_LENGTH + 1;

                    CHECKED_REALLOC(maxdims_out_string, maxdims_out_string_curr_len,
                                    maxdims_out_string_new_len, maxdims_out_string_curr_pos, H5E_DATASPACE,
                                    FAIL);

                    /* According to the server specification, unlimited dimension extents should be specified
                     * as having a maxdims entry of '0'
                     */
                    if (H5S_UNLIMITED == maxdims[i]) {
                        if (i > 0)
                            strcat(maxdims_out_string_curr_pos++, ",");
                        strcat(maxdims_out_string_curr_pos++, "0");
                    } /* end if */
                    else {
                        if ((bytes_printed =
                                 snprintf(maxdims_out_string_curr_pos,
                                          maxdims_out_string_new_len - (size_t)maxdims_out_string_curr_pos,
                                          "%s%" PRIuHSIZE, i > 0 ? "," : "", maxdims[i])) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "snprintf error");
                        maxdims_out_string_curr_pos += bytes_printed;
                    } /* end else */
                }     /* end if */
            }         /* end for */

            if (shape_out_string)
                strcat(shape_out_string_curr_pos++, "]");
            if (maxdims_out_string)
                strcat(maxdims_out_string_curr_pos++, "]");

            break;
        } /* H5S_SIMPLE */

        case H5S_SCALAR: /* Should have already been handled above */
        case H5S_NO_CLASS:
        default:
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "invalid dataspace type");
    } /* end switch */

done:
    if (ret_value >= 0) {
        if (shape_body)
            *shape_body = shape_out_string;
        if (maxdims_body)
            *maxdims_body = maxdims_out_string;

#ifdef RV_CONNECTOR_DEBUG
        if (shape_out_string)
            printf("-> Dataspace dimensions:\n%s\n\n", shape_out_string);
        if (maxdims_out_string)
            printf("-> Dataspace maximum dimensions:\n%s\n\n", maxdims_out_string);
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
 * Function:    RV_base64_encode
 *
 * Purpose:     A helper function to base64 encode the given buffer. This
 *              is used specifically when dealing with writing data to a
 *              dataset using a point selection, and when sending plist
 *              information to the server.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              January, 2018
 */
herr_t
RV_base64_encode(const void *in, size_t in_size, char **out, size_t *out_size)
{
    const uint8_t *buf       = (const uint8_t *)in;
    const char     charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t       three_byte_set;
    uint8_t        c0, c1, c2, c3;
    size_t         i;
    size_t         nalloc;
    size_t         out_index = 0;
    int            npad;
    herr_t         ret_value = SUCCEED;

    if (!in)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "input buffer pointer was NULL");
    if (!out)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "output buffer pointer was NULL");

    /* If the caller has specified a 0-sized buffer, allocate one and set nalloc
     * so that the following 'nalloc *= 2' calls don't result in 0-sized
     * allocations.
     */
    if (!out_size || (out_size && !*out_size)) {
        nalloc = BASE64_ENCODE_DEFAULT_BUFFER_SIZE;
        if (NULL == (*out = (char *)RV_malloc(nalloc)))
            FUNC_GOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, FAIL,
                            "can't allocate space for base64-encoding output buffer");
    } /* end if */
    else
        nalloc = *out_size;

    for (i = 0; i < in_size; i += 3) {
        three_byte_set = ((uint32_t)buf[i]) << 16;

        if (i + 1 < in_size)
            three_byte_set += ((uint32_t)buf[i + 1]) << 8;

        if (i + 2 < in_size)
            three_byte_set += buf[i + 2];

        /* Split 3-byte number into four 6-bit groups for encoding */
        c0 = (uint8_t)(three_byte_set >> 18) & 0x3f;
        c1 = (uint8_t)(three_byte_set >> 12) & 0x3f;
        c2 = (uint8_t)(three_byte_set >> 6) & 0x3f;
        c3 = (uint8_t)three_byte_set & 0x3f;

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
    }     /* end for */

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
} /* end RV_base64_encode() */

/*-------------------------------------------------------------------------
 * Function:    RV_base64_decode
 *
 * Purpose:     A helper function to base64 decode the given buffer. This
 *              is used specifically when dealing with writing data to a
 *              dataset using a point selection, and when sending plist
 *              information to the server.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Matthew Larson
 *              May, 2023
 */
herr_t
RV_base64_decode(const char *in, size_t in_size, char **out, size_t *out_size)
{
    const uint8_t *buf       = (const uint8_t *)in;
    const char     charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    uint32_t       four_byte_set;
    uint8_t        c0, c1, c2, c3;
    size_t         nalloc    = 0;
    size_t         out_index = 0;
    herr_t         ret_value = SUCCEED;

    if (!in)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "input buffer pointer was NULL");
    if (!out)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "output buffer pointer was NULL");

    /* If the caller has specified a 0-sized buffer, allocate one and set nalloc
     * so that the following 'nalloc *= 2' calls don't result in 0-sized
     * allocations.
     */
    if (!out_size || (out_size && !*out_size)) {
        nalloc = BASE64_ENCODE_DEFAULT_BUFFER_SIZE;
        if (NULL == (*out = (char *)RV_malloc(nalloc)))
            FUNC_GOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, FAIL,
                            "can't allocate space for base64-encoding output buffer");
    } /* end if */
    else
        nalloc = *out_size;

    for (size_t i = 0; i < in_size; i += 4) {
        four_byte_set = ((uint32_t)buf[i]) << 24;

        if (i + 1 < in_size)
            four_byte_set += ((uint32_t)buf[i + 1]) << 16;

        if (i + 2 < in_size)
            four_byte_set += ((uint32_t)buf[i + 2]) << 8;

        if (i + 3 < in_size)
            four_byte_set += ((uint32_t)buf[i + 3]);

        if (((char)buf[i + 3]) == '=') {
            /* Two characters of padding */

            /* Ignore last two padding chars */
            c0 = (uint8_t)(four_byte_set >> 24);
            c1 = (uint8_t)(four_byte_set >> 16);

            c0 = (uint8_t)(strchr(charset, c0) - charset);
            c1 = (uint8_t)(strchr(charset, c1) - charset);

            four_byte_set = (((uint32_t)c0) << 6) | (((uint32_t)c1) << 0);

            /* Remove 4 trailing bits due to padding */
            four_byte_set = four_byte_set >> 4;

            c0 = (uint8_t)(four_byte_set >> 8);
            c1 = (uint8_t)(four_byte_set >> 0);

            CHECKED_REALLOC_NO_PTR(*out, nalloc, out_index + 1, H5E_RESOURCE, FAIL);
            (*out)[out_index++] = (char)c1;
        }
        else if (((char)buf[i + 2]) == '=') {
            /* One character of padding */

            /* Ignore last one padding char */
            c0 = (uint8_t)(four_byte_set >> 24);
            c1 = (uint8_t)(four_byte_set >> 16);
            c2 = (uint8_t)(four_byte_set >> 8);

            c0 = (uint8_t)(strchr(charset, c0) - charset);
            c1 = (uint8_t)(strchr(charset, c1) - charset);
            c2 = (uint8_t)(strchr(charset, c2) - charset);

            four_byte_set = (((uint32_t)c0) << 12) | (((uint32_t)c1) << 6) | (((uint32_t)c2) << 0);

            /* Remove 2 trailing bits due to padding */
            four_byte_set = four_byte_set >> 2;

            c0 = (uint8_t)(four_byte_set >> 16);
            c1 = (uint8_t)(four_byte_set >> 8);
            c2 = (uint8_t)(four_byte_set >> 0);

            CHECKED_REALLOC_NO_PTR(*out, nalloc, out_index + 2, H5E_RESOURCE, FAIL);
            (*out)[out_index++] = (char)c1;
            (*out)[out_index++] = (char)c2;
        }
        else {
            /* 0 bytes of padding */
            c0 = (uint8_t)(four_byte_set >> 24);
            c1 = (uint8_t)(four_byte_set >> 16);
            c2 = (uint8_t)(four_byte_set >> 8);
            c3 = (uint8_t)(four_byte_set >> 0);

            c0 = (uint8_t)(strchr(charset, c0) - charset);
            c1 = (uint8_t)(strchr(charset, c1) - charset);
            c2 = (uint8_t)(strchr(charset, c2) - charset);
            c3 = (uint8_t)(strchr(charset, c3) - charset);

            four_byte_set = (((uint32_t)c0) << 18) | (((uint32_t)c1) << 12) | (((uint32_t)c2) << 6) |
                            (((uint32_t)c3) << 0);

            c0 = (uint8_t)(four_byte_set >> 24);
            c1 = (uint8_t)(four_byte_set >> 16);
            c2 = (uint8_t)(four_byte_set >> 8);
            c3 = (uint8_t)(four_byte_set >> 0);

            CHECKED_REALLOC_NO_PTR(*out, nalloc, out_index + 3, H5E_RESOURCE, FAIL);
            (*out)[out_index++] = (char)c1;
            (*out)[out_index++] = (char)c2;
            (*out)[out_index++] = (char)c3;
        }
    } /* end for */

    (*out)[out_index++] = '\0';

    if (out_size)
        *out_size = out_index;
done:
    return ret_value;
} /* end RV_base64_decode() */

/* Helper function to store the version of the external HSDS server */
herr_t
RV_parse_server_version(char *HTTP_response, void *callback_data_in, void *callback_data_out)
{
    yajl_val            parse_tree     = NULL, key_obj;
    herr_t              ret_value      = SUCCEED;
    server_api_version *server_version = (server_api_version *)callback_data_out;

    char *version_response = NULL;
    char *version_field    = NULL;
    char *saveptr;
    int   numeric_version_field = 0;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Retrieving server version from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL");

    if (!server_version)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "server version buffer was NULL");

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "parsing JSON failed");

    /* Retrieve version */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, server_version_keys, yajl_t_string)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "failed to parse server version");

    if (!YAJL_IS_STRING(key_obj))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "parsed server version is not a string");

    if (NULL == (version_response = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "server version was NULL");

    /* Parse server version into struct */
    if (NULL == (version_field = strtok_r(version_response, ".", &saveptr)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "server major version field was NULL");

    if ((numeric_version_field = (int)strtol(version_field, NULL, 10)) < 0)
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "invalid server major version");

    server_version->major = (size_t)numeric_version_field;

    if (NULL == (version_field = strtok_r(NULL, ".", &saveptr)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "server minor version field was NULL");

    if ((numeric_version_field = (int)strtol(version_field, NULL, 10)) < 0)
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "invalid server minor version");

    server_version->minor = (size_t)numeric_version_field;

    if (NULL == (version_field = strtok_r(NULL, ".", &saveptr)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "server patch version field was NULL");

    if ((numeric_version_field = (int)strtol(version_field, NULL, 10)) < 0)
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "invalid server patch version");

    server_version->patch = (size_t)numeric_version_field;

done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
}

/* Helper function to parse an object's allocated size from server response */
herr_t
RV_parse_allocated_size_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out)
{
    yajl_val parse_tree = NULL, key_obj = NULL;
    herr_t   ret_value      = SUCCEED;
    size_t  *allocated_size = (size_t *)callback_data_out;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Retrieving allocated size from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL");

    if (!allocated_size)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "allocated size pointer was NULL");

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "parsing JSON failed");

    /* Retrieve size */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, allocated_size_keys, yajl_t_number)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "failed to parse allocated size");

    if (!YAJL_IS_INTEGER(key_obj))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "parsed allocated size is not an integer");

    if (YAJL_GET_INTEGER(key_obj) < 0)
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "parsed allocated size was negative");

    *allocated_size = (size_t)YAJL_GET_INTEGER(key_obj);
done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
}

/*-------------------------------------------------------------------------
 * Function:    RV_set_object_type_header
 *
 * Purpose:     Helper function to turn an object type into a string for
 *              a request to the server. Requires the address of a pointer
 *              to return the string. The given pointer should not point at
 *              any allocated memory, as its old value is overwritten.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Matthew Larson
 *              May, 2023
 */
herr_t
RV_set_object_type_header(H5I_type_t parent_obj_type, const char **parent_obj_type_header)
{
    herr_t ret_value = SUCCEED;

    if (!parent_obj_type_header)
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "given parent object type header pointer was NULL");

    switch (parent_obj_type) {
        case H5I_FILE:
        case H5I_GROUP:
            *parent_obj_type_header = "groups";
            break;
        case H5I_DATATYPE:
            *parent_obj_type_header = "datatypes";
            break;
        case H5I_DATASET:
            *parent_obj_type_header = "datasets";
            break;
        case H5I_ATTR:
        case H5I_UNINIT:
        case H5I_BADID:
        case H5I_DATASPACE:
        case H5I_VFL:
        case H5I_VOL:
        case H5I_GENPROP_CLS:
        case H5I_GENPROP_LST:
        case H5I_ERROR_CLASS:
        case H5I_ERROR_MSG:
        case H5I_ERROR_STACK:
        case H5I_NTYPES:
        default:
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "parent object not a group, datatype or dataset");
    }
done:
    return (ret_value);
} /* end RV_set_object_type_header */

/* Return the index of the curl handle into the array of handles */
herr_t
RV_get_index_of_matching_handle(dataset_transfer_info *transfer_info, size_t count, CURL *handle,
                                size_t *handle_index)
{
    herr_t ret_value = SUCCEED;

    if (!handle)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "cURL handle provided for index match is NULL");

    *handle_index = count + 1;

    for (size_t i = 0; i < count; i++) {
        /* May have been cleaned up early after successful request */
        if (!transfer_info[i].curl_easy_handle) {
            continue;
        }

        if (transfer_info[i].curl_easy_handle == handle) {
            *handle_index = i;
            break;
        }
    }
done:
    return ret_value;
}

herr_t
RV_curl_multi_perform(CURL *curl_multi_handle, dataset_transfer_info *transfer_info, size_t count,
                      herr_t(success_callback)(hid_t mem_type_id, hid_t mem_space_id, hid_t file_type_id,
                                               hid_t file_space_id, void *buf,
                                               struct response_buffer resp_buffer))
{

    herr_t         ret_value               = SUCCEED;
    int            num_still_running       = 0;
    int            num_prev_running        = 0;
    int            num_curlm_msgs          = 0;
    int            events_occurred         = 0;
    CURLMsg       *curl_multi_msg          = NULL;
    CURL         **failed_handles_to_retry = NULL;
    size_t         fail_count              = 0;
    size_t         succeed_count           = 0;
    size_t         num_finished            = 0;
    size_t         handle_index            = 0;
    fd_set         fdread;
    fd_set         fdwrite;
    fd_set         fdexcep;
    int            maxfd      = -1;
    long           timeout_ms = 0;
    struct timeval timeout;

    if ((failed_handles_to_retry = calloc(count, sizeof(CURL *))) == NULL)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL,
                        "can't allocate space for cURL headers to be retried");
    /*
    Lowers CPU usage dramatically, but also vastly increases time taken for requests to local storage
    when the number of datasets is small.
    struct timespec delay;
    delay.tv_sec  = 0;
    delay.tv_nsec = DELAY_BETWEEN_HANDLE_CHECKS;
    */

    memset(failed_handles_to_retry, 0, sizeof(CURL *) * count);

    do {
        maxfd           = -1;
        fail_count      = 0;
        succeed_count   = 0;
        timeout_ms      = 0;
        timeout.tv_sec  = 0;
        timeout.tv_usec = 0;

        if (CURLM_OK != curl_multi_timeout(curl_multi_handle, &timeout_ms))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "unable to get curl timeout");

        timeout_ms = ((timeout_ms < 0) || (timeout_ms > DEFAULT_POLL_TIMEOUT_MS)) ? DEFAULT_POLL_TIMEOUT_MS
                                                                                  : timeout_ms;

        timeout.tv_sec  = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;

        FD_ZERO(&fdread);
        FD_ZERO(&fdwrite);
        FD_ZERO(&fdexcep);

        if (CURLM_OK != curl_multi_fdset(curl_multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "unable to get curl fd set");

        if (maxfd != -1)
            select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);

        if (CURLM_OK != curl_multi_perform(curl_multi_handle, &num_still_running))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_WRITEERROR, FAIL, "cURL multi perform error");

        while ((num_prev_running != num_still_running) &&
               (curl_multi_msg = curl_multi_info_read(curl_multi_handle, &num_curlm_msgs))) {
            long response_code;

            if (curl_multi_msg && (curl_multi_msg->msg == CURLMSG_DONE)) {
                if (CURLE_OK !=
                    curl_easy_getinfo(curl_multi_msg->easy_handle, CURLINFO_RESPONSE_CODE, &response_code))
                    FUNC_GOTO_ERROR(H5E_DATASET, H5E_WRITEERROR, FAIL, "can't get HTTP response code");

                /* Gracefully handle 503 Error, which can result from sending too many simultaneous
                 * requests */
                if (response_code == 503) {

                    if (RV_get_index_of_matching_handle(transfer_info, count, curl_multi_msg->easy_handle,
                                                        &handle_index) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_WRITEERROR, FAIL,
                                        "can't get handle information for retry");

                    /* Restart request next time for writes */
                    if (transfer_info[handle_index].transfer_type == WRITE)
                        transfer_info[handle_index].u.write_info.uinfo.bytes_sent = 0;
                    /* Restart request next time for reads */
                    transfer_info[handle_index].resp_buffer.curr_buf_ptr =
                        transfer_info[handle_index].resp_buffer.buffer;

                    if (CURLM_OK != curl_multi_remove_handle(curl_multi_handle, curl_multi_msg->easy_handle))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_WRITEERROR, FAIL,
                                        "failed to remove denied cURL handle");

                    /* Identify the handle by its original index */
                    failed_handles_to_retry[handle_index] = curl_multi_msg->easy_handle;

                    struct timespec tms;

                    clock_gettime(CLOCK_MONOTONIC, &tms);

                    transfer_info[handle_index].time_of_fail =
                        (size_t)tms.tv_sec * 1000 * 1000 * 1000 + (size_t)tms.tv_nsec;

                    transfer_info[handle_index].current_backoff_duration =
                        (transfer_info[handle_index].current_backoff_duration == 0)
                            ? BACKOFF_INITIAL_DURATION
                            : (size_t)((double)transfer_info[handle_index].current_backoff_duration *
                                       BACKOFF_SCALE_FACTOR);

                    /* Randomize time to avoid doing all retry attempts at once */
                    int random_factor = rand();
                    transfer_info[handle_index].current_backoff_duration =
                        (size_t)((double)transfer_info[handle_index].current_backoff_duration *
                                 (1.0 + ((double)random_factor / (double)RAND_MAX)));

                    if (transfer_info[handle_index].current_backoff_duration >= BACKOFF_MAX_BEFORE_FAIL)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_WRITEERROR, FAIL,
                                        "Unable to reach server for write: 503 service unavailable");
                    fail_count++;
                }
                else if (response_code == 200) {
                    num_finished++;
                    succeed_count++;

                    if (RV_get_index_of_matching_handle(transfer_info, count, curl_multi_msg->easy_handle,
                                                        &handle_index) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_WRITEERROR, FAIL,
                                        "can't get handle information for retry");

                    if (success_callback(
                            transfer_info[handle_index].mem_type_id, transfer_info[handle_index].mem_space_id,
                            transfer_info[handle_index].file_type_id,
                            transfer_info[handle_index].file_space_id, transfer_info[handle_index].buf,
                            transfer_info[handle_index].resp_buffer) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_READERROR, FAIL,
                                        "failed to post-process data read from dataset");

                    /* Clean up */
                    if (CURLM_OK != curl_multi_remove_handle(curl_multi_handle, curl_multi_msg->easy_handle))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_WRITEERROR, FAIL,
                                        "failed to remove finished cURL handle");

                    curl_easy_cleanup(curl_multi_msg->easy_handle);

                    transfer_info[handle_index].curl_easy_handle = NULL;

                    if (transfer_info[handle_index].transfer_type == WRITE) {
                        RV_free(transfer_info[handle_index].u.write_info.write_body);
                        transfer_info[handle_index].u.write_info.write_body = NULL;

                        RV_free(transfer_info[handle_index].u.write_info.base64_encoded_values);
                        transfer_info[handle_index].u.write_info.base64_encoded_values = NULL;
                    }

                    RV_free(transfer_info[handle_index].request_url);
                    transfer_info[handle_index].request_url = NULL;

                    RV_free(transfer_info[handle_index].resp_buffer.buffer);
                    transfer_info[handle_index].resp_buffer.buffer = NULL;
                }
                else {
                    HANDLE_RESPONSE(response_code, H5E_DATASET, H5E_WRITEERROR, FAIL);
                }
            }
        } /* end while (curl_multi_msg); */

        /* TODO: Replace with an epoll-like structure of some kind, manually iterating this will probably
         * be slow */
        struct timespec curr_time;
        clock_gettime(CLOCK_MONOTONIC, &curr_time);
        size_t curr_time_ns = (size_t)curr_time.tv_sec * 1000 * 1000 * 1000 + (size_t)curr_time.tv_nsec;

        for (size_t i = 0; i < count; i++) {
            if (failed_handles_to_retry[i] && ((curr_time_ns - transfer_info[i].time_of_fail) >=
                                               transfer_info[i].current_backoff_duration)) {
                if (CURLM_OK != curl_multi_add_handle(curl_multi_handle, failed_handles_to_retry[i]))
                    FUNC_GOTO_ERROR(H5E_DATASET, H5E_WRITEERROR, FAIL, "failed to re-add denied cURL handle");

                failed_handles_to_retry[i] = NULL;
            }
        }

        /*
        nanosleep(&delay, NULL);
        */
        num_prev_running = num_still_running;
    } while (num_still_running > 0);

done:
    RV_free(failed_handles_to_retry);

    return ret_value;
}

/* Helper function to initialize an object's name based on its parent's name.
 * Allocates memory that must be closed by caller. */
herr_t
RV_set_object_handle_path(const char *obj_path, const char *parent_path, char **buf)
{
    herr_t  ret_value           = SUCCEED;
    hbool_t include_parent_path = false;
    size_t  path_size           = 0;
    size_t  path_len            = 0;
    char   *handle_path         = NULL;

    /* Objects can be created/opened without reference to their path. Leave handle_path NULL in this case */
    if (!obj_path) {
        *buf = NULL;
        FUNC_GOTO_DONE(SUCCEED);
    }

    /* Parent name is included if it is not the root and the object is opened by relative path */
    include_parent_path = parent_path && strcmp(parent_path, "/") && (obj_path[0] != '/');

    path_size =
        include_parent_path ? strlen(parent_path) + 1 + strlen(obj_path) + 1 : 1 + strlen(obj_path) + 1;

    if ((handle_path = RV_malloc(path_size)) == NULL)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTALLOC, FAIL, "can't allocate space for handle path");

    if (include_parent_path) {
        strncpy(handle_path, parent_path, path_size);
        path_len += strlen(parent_path);
    }

    /* Add leading slash if not in group name */
    if (obj_path[0] != '/') {
        handle_path[path_len] = '/';
        path_len += 1;
    }

    strncpy(handle_path + path_len, obj_path, strlen(obj_path) + 1);
    path_len += (strlen(obj_path) + 1);

    handle_path[path_size - 1] = '\0';

    /* Make user pointer point at allocated memory */
    *buf = handle_path;

done:
    if (ret_value < 0) {
        RV_free(handle_path);
        *buf = NULL;
    }

    return ret_value;
}
/*************************************************
 * The following two routines allow the REST VOL *
 * connector to be dynamically loaded by HDF5.   *
 *************************************************/

H5PLUGIN_DLL H5PL_type_t
H5PLget_plugin_type(void)
{
    return H5PL_TYPE_VOL;
} /* end H5PLget_plugin_type() */

H5PLUGIN_DLL const void *
H5PLget_plugin_info(void)
{
    return &H5VL_rest_g;
} /* end H5PLget_plugin_info() */

/*-------------------------------------------------------------------------
 * Function:    H5_rest_compare_string_keys
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
int
H5_rest_compare_string_keys(void *value1, void *value2)
{
    const char *val1 = (const char *)value1;
    const char *val2 = (const char *)value2;

    return !strcmp(val1, val2);
} /* end H5_rest_compare_string_keys() */

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
void
RV_free_visited_link_hash_table_key(rv_hash_table_key_t value)
{
    RV_free(value);
    value = NULL;
} /* end RV_free_visited_link_hash_table_key() */

/*-------------------------------------------------------------------------
 * Function:    RV_JSON_escape_string
 *
 * Purpose:     Helper function to escape control characters for JSON strings.
 *              If 'out' is NULL, out_size will be changed to the buffer size
 *              needed for the escaped version of 'in'.
 *              If 'out' is non-NULL, it should be a buffer of out_size bytes
 *              that will be populated with the escaped version of 'in'.
 *              If the provided buffer is too small and this operation fails,
 *              the value of the buffer will still be modified.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Matthew Larson
 *              January, 2024
 */
herr_t
RV_JSON_escape_string(const char *in, char *out, size_t *out_size)
{
    herr_t ret_value = SUCCEED;
    size_t in_size   = strlen(in);

    char *out_ptr                                  = NULL;
    char  escape_characters[NUM_JSON_ESCAPE_CHARS] = {'\b', '\f', '\n', '\r', '\t', '\"', '\\'};

    if (out == NULL) {
        /* Determine necessary buffer size */
        *out_size = in_size + 1;

        for (size_t i = 0; i < in_size; i++) {
            char c = in[i];

            for (size_t j = 0; j < NUM_JSON_ESCAPE_CHARS; j++) {
                char ec = escape_characters[j];

                /* Each escaped character requires additional '\' in final string */
                if (c == ec)
                    *out_size += 1;
            }
        }
    }
    else {
        /* Escaped string is at least as long as original */
        if (*out_size < strlen(in) + 1)
            FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "escaped buffer is smaller than original");

        /* Populate provided buffer */
        out_ptr = out;

        for (size_t i = 0; i < in_size; i++) {
            char c = in[i];

            for (size_t j = 0; j < NUM_JSON_ESCAPE_CHARS; j++) {
                char ec = escape_characters[j];

                if (c == ec) {
                    if ((out_ptr - out + 1) > *out_size)
                        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "buffer too small for encoded string");
                    out_ptr[0] = '\\';
                    out_ptr++;
                }
            }

            if ((out_ptr - out + 1) > *out_size)
                FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "buffer too small for encoded string");

            out_ptr[0] = c;
            out_ptr++;
        }

        if ((out_ptr - out + 1) > *out_size)
            FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "buffer too small for encoded string");

        out_ptr[0] = '\0';
    }

done:

    return ret_value;
}
