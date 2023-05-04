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

/* Default initial size for the response buffer allocated which cURL writes
 * its responses into
 */
#define CURL_RESPONSE_BUFFER_DEFAULT_SIZE 1024
/*
 * The VOL connector identification number.
 */
hid_t H5_rest_id_g = H5I_UNINIT;

static hbool_t H5_rest_initialized_g = FALSE;

/* Identifiers for HDF5's error API */
hid_t H5_rest_err_stack_g               = H5I_INVALID_HID;
hid_t H5_rest_err_class_g               = H5I_INVALID_HID;
hid_t H5_rest_obj_err_maj_g             = H5I_INVALID_HID;
hid_t H5_rest_parse_err_min_g           = H5I_INVALID_HID;
hid_t H5_rest_link_table_err_min_g      = H5I_INVALID_HID;
hid_t H5_rest_link_table_iter_err_min_g = H5I_INVALID_HID;
hid_t H5_rest_attr_table_err_min_g      = H5I_INVALID_HID;
hid_t H5_rest_attr_table_iter_err_min_g = H5I_INVALID_HID;

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

/*
 * Saved copy of the base URL for operating on
 */
char *base_URL = NULL;

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

/* Host header string for specifying the host (Domain) for requests */
const char *const host_string = "X-Hdf-domain: ";

/* JSON key to retrieve the ID of the root group of a file */
const char *root_id_keys[] = {"root", (const char *)0};

/* JSON key to retrieve the ID of an object from the server */
const char *object_id_keys[] = {"id", (const char *)0};

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

/* Internal initialization/termination functions which are called by
 * the public functions H5rest_init() and H5rest_term() */
static herr_t H5_rest_init(hid_t vipl_id);
static herr_t H5_rest_term(void);

static herr_t H5_rest_authenticate_with_AD(H5_rest_ad_info_t *ad_info);

/* Introspection callbacks */
static herr_t H5_rest_get_conn_cls(void *obj, H5VL_get_conn_lvl_t lvl, const struct H5VL_class_t **conn_cls);
static herr_t H5_rest_get_cap_flags(const void *info, uint64_t *cap_flags);
static herr_t H5_rest_opt_query(void *obj, H5VL_subclass_t cls, int opt_type, uint64_t *flags);

/* cURL function callbacks */
static size_t H5_rest_curl_read_data_callback(char *buffer, size_t size, size_t nmemb, void *inptr);
static size_t H5_rest_curl_write_data_callback(char *buffer, size_t size, size_t nmemb, void *userp);

/* Helper function to URL-encode an entire pathname by URL-encoding each of its separate components */
static char *H5_rest_url_encode_path(const char *path);

/* The REST VOL connector's class structure. */
static const H5VL_class_t H5VL_rest_g = {
    HDF5_VOL_REST_VERSION,      /* Connector struct version number       */
    HDF5_VOL_REST_CLS_VAL,      /* Connector value                       */
    HDF5_VOL_REST_NAME,         /* Connector name                        */
    HDF5_VOL_REST_CONN_VERSION, /* Connector version #                   */
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
        char *socket_path = "/tmp/hs/sn_1.sock";

        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, socket_path))
            FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL socket path header: %s",
                            curl_err_buf);
    }

#ifdef RV_CURL_DEBUG
    /* Enable cURL debugging output if desired */
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
#endif

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

    /* Free base URL */
    if (base_URL) {
        RV_free(base_URL);
        base_URL = NULL;
    }

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

    if (H5_rest_set_connection_information() < 0)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't set REST VOL connector connection information");

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
H5_rest_set_connection_information(void)
{
    H5_rest_ad_info_t ad_info;
    const char       *URL;
    size_t            URL_len     = 0;
    FILE             *config_file = NULL;
    herr_t            ret_value   = SUCCEED;

    memset(&ad_info, 0, sizeof(ad_info));

    /*
     * Attempt to pull in configuration/authentication information from
     * the environment.
     */

    if ((URL = getenv("HSDS_ENDPOINT"))) {

        if (!strncmp(URL, UNIX_SOCKET_PREFIX, strlen(UNIX_SOCKET_PREFIX))) {
            /* This is just a placeholder URL for curl's syntax, its specific value is unimportant */
            URL     = "0";
            URL_len = 1;

            if (!base_URL || (0 != (strncmp(base_URL, URL, strlen(URL))))) {

                /* If previous value is incorrect, reassign */
                if (base_URL) {
                    free(base_URL);
                    base_URL = NULL;
                }

                if (NULL == (base_URL = (char *)RV_malloc(URL_len + 1)))
                    FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTALLOC, FAIL,
                                    "can't allocate space necessary for placeholder base URL");

                strncpy(base_URL, URL, URL_len);
                base_URL[URL_len] = '\0';
            }
        }
        else {
            /*
             * Save a copy of the base URL being worked on so that operations like
             * creating a Group can be redirected to "base URL"/groups by building
             * off of the base URL supplied.
             */
            URL_len = strlen(URL);

            if (!base_URL || (0 != (strncmp(base_URL, URL, strlen(URL))))) {

                /* If previous value is incorrect, reassign */
                if (base_URL) {
                    free(base_URL);
                    base_URL = NULL;
                }

                if (NULL == (base_URL = (char *)RV_malloc(URL_len + 1)))
                    FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTALLOC, FAIL,
                                    "can't allocate space necessary for placeholder base URL");

                strncpy(base_URL, URL, URL_len);
                base_URL[URL_len] = '\0';
            }
        }

        const char *username = getenv("HSDS_USERNAME");
        const char *password = getenv("HSDS_PASSWORD");

        if (username || password) {
            /* Attempt to set authentication information */
            if (username && strlen(username)) {
                if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_USERNAME, username))
                    FUNC_GOTO_ERROR(H5E_ARGS, H5E_CANTSET, FAIL, "can't set username: %s", curl_err_buf);
            } /* end if */

            if (password && strlen(password)) {
                if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_PASSWORD, password))
                    FUNC_GOTO_ERROR(H5E_ARGS, H5E_CANTSET, FAIL, "can't set password: %s", curl_err_buf);
            } /* end if */
        }     /* end if */
        else {
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
            if (H5_rest_authenticate_with_AD(&ad_info) < 0)
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
                    /*
                     * Save a copy of the base URL being worked on so that operations like
                     * creating a Group can be redirected to "base URL"/groups by building
                     * off of the base URL supplied.
                     */
                    URL_len = strlen(val);

                    if (!base_URL || (0 != (strncmp(base_URL, val, URL_len)))) {

                        /* If previous value is incorrect, reassign */
                        if (base_URL) {
                            free(base_URL);
                            base_URL = NULL;
                        }

                        if (NULL == (base_URL = (char *)RV_malloc(URL_len + 1)))
                            FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTALLOC, FAIL,
                                            "can't allocate space necessary for placeholder base URL");

                        strncpy(base_URL, val, URL_len);
                        base_URL[URL_len] = '\0';
                    }

                } /* end if */
            }     /* end if */
            else if (!strcmp(key, "hs_username")) {
                if (val && strlen(val)) {
                    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_USERNAME, val))
                        FUNC_GOTO_ERROR(H5E_ARGS, H5E_CANTSET, FAIL, "can't set username: %s", curl_err_buf);
                } /* end if */
            }     /* end else if */
            else if (!strcmp(key, "hs_password")) {
                if (val && strlen(val)) {
                    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_PASSWORD, val))
                        FUNC_GOTO_ERROR(H5E_ARGS, H5E_CANTSET, FAIL, "can't set password: %s", curl_err_buf);
                } /* end if */
            }     /* end else if */
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
            if (H5_rest_authenticate_with_AD(&ad_info) < 0)
                FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "can't authenticate with Active Directory");
    } /* end else */

    if (!base_URL)
        FUNC_GOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL,
                        "must specify a base URL - please set HSDS_ENDPOINT environment variable or create a "
                        "config file");

done:
    if (config_file)
        fclose(config_file);

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
H5_rest_authenticate_with_AD(H5_rest_ad_info_t *ad_info)
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
H5_rest_url_encode_path(const char *path)
{
    ptrdiff_t buf_ptrdiff;
    size_t    bytes_nalloc;
    size_t    path_prefix_len;
    size_t    path_component_len;
    char     *path_copy                  = NULL;
    char     *url_encoded_path_component = NULL;
    char     *token;
    char     *cur_pos;
    char     *tmp_buffer = NULL;
    char     *ret_value  = NULL;

    if (!path)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "path was NULL");

    /* Retrieve the length of the possible path prefix, which could be something like '/', '.', etc. */
    cur_pos = (char *)path;
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
 * Function:    RV_copy_object_URI_and_domain_callback
 *
 * Purpose:     Copies information from response to provided buffers.
 *
 *              This function should be used in place of
 *              RV_copy_object_URI_callback wherever the object
 *              may be an external link, whose domain is different
 *              from the domain of its parent object.
 *
 *              callback_data_out is expected to be a pointer to an array
 *              of two pointers, the first being the address of a buffer for the URI,
 *              and the second being the address of a pointer to a domain buffer of the target object.
 *
 *              callback_data_is provided inside RV_find_object_by_path.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Matthew Larson
 *              April, 2023
 */
herr_t
RV_copy_object_URI_and_domain_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out)
{
    yajl_val    parse_tree       = NULL, key_obj;
    char       *parsed_id_string = NULL;
    herr_t      ret_value        = SUCCEED;
    loc_info   *loc_info_out     = (loc_info *)callback_data_out;
    RV_object_t found_domain;
    bool        is_external_domain = false;

    /* Parse domain information from response */
#ifdef RV_CONNECTOR_DEBUG
    printf("-> Retrieving object's domain from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL");
    if (!loc_info_out)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "output buffer was NULL");

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "parsing JSON failed");

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

    /* If retrieved domain is different than the domain through which this object
     * was accessed, replace the returned object's domain. */
    is_external_domain =
        strcmp(found_domain.u.file.filepath_name, loc_info_out->domain->u.file.filepath_name);

    if (is_external_domain) {
        RV_object_t *new_domain = NULL;

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

        strncpy(new_domain->u.file.filepath_name, found_domain.u.file.filepath_name,
                strlen(found_domain.u.file.filepath_name) + 1);

        new_domain->u.file.intent    = loc_info_out->domain->u.file.intent;
        new_domain->u.file.fapl_id   = H5Pcopy(loc_info_out->domain->u.file.fapl_id);
        new_domain->u.file.fcpl_id   = H5Pcopy(loc_info_out->domain->u.file.fcpl_id);
        new_domain->u.file.ref_count = 1;

        RV_file_close(loc_info_out->domain, H5P_DEFAULT, NULL);

        loc_info_out->domain = new_domain;
    }

    ret_value = RV_copy_object_URI_callback(HTTP_response, NULL, loc_info_out->URI);

done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
}

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
htri_t
RV_find_object_by_path(RV_object_t *parent_obj, const char *obj_path, H5I_type_t *target_object_type,
                       herr_t (*obj_found_callback)(char *, void *, void *), void *callback_data_in,
                       void *callback_data_out)
{
    RV_object_t *external_file         = NULL;
    hbool_t      is_relative_path      = FALSE;
    size_t       host_header_len       = 0;
    char        *host_header           = NULL;
    char        *path_dirname          = NULL;
    char        *tmp_link_val          = NULL;
    char        *url_encoded_link_name = NULL;
    char        *url_encoded_path_name = NULL;
    char         request_url[URL_MAX_LENGTH];
    long         http_response;
    int          url_len   = 0;
    htri_t       ret_value = FAIL;

    if (!parent_obj)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "parent object pointer was NULL");
    if (!obj_path)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "target path was NULL");
    if (!target_object_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "target object type pointer was NULL");
    if (H5I_FILE != parent_obj->obj_type && H5I_GROUP != parent_obj->obj_type &&
        H5I_DATATYPE != parent_obj->obj_type && H5I_DATASET != parent_obj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "parent object not a file, group, datatype or dataset");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Finding object by path '%s' from parent object of type %s with URI %s\n\n", obj_path,
           object_type_to_string(parent_obj->obj_type), parent_obj->URI);
#endif

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

    /* If the target object type was specified as H5I_UNINIT and was not changed due to one of
     * the optimizations above, we must determine the target object's type before making the
     * appropriate GET request to the server. Otherwise, the target object type is known, so
     * we skip ahead to the GET request and optional callback function.
     */
    if (H5I_UNINIT == *target_object_type) {
        H5L_info2_t link_info;
        const char *ext_filename = NULL;
        const char *ext_obj_path = NULL;
        hbool_t     empty_dirname;
        htri_t      search_ret;
        char       *pobj_URI = parent_obj->URI;
        char        temp_URI[URI_MAX_LENGTH];

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
        } /* end if */

        /* Retrieve the link for the target object from the parent group and check to see if it
         * is a hard, soft or external link. If it is a hard link, we can directly make the request
         * to retrieve the target object's information. Otherwise, we need to do some extra processing
         * to retrieve the actual path to the target object.
         */

        /* URL-encode the link name so that the resulting URL for the link GET operation doesn't
         * contain any illegal characters
         */
        if (NULL == (url_encoded_link_name = curl_easy_escape(curl, H5_rest_basename(obj_path), 0)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode link name");

        if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/links/%s", base_URL, pobj_URI,
                                url_encoded_link_name)) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

        if (url_len >= URL_MAX_LENGTH)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                            "link GET request URL size exceeded maximum URL size");

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Retrieving link type for link to target object of unknown type at URL %s\n\n",
               request_url);
#endif

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

        CURL_PERFORM(curl, H5E_LINK, H5E_PATH, FALSE);

        if (RV_parse_response(response_buffer.buffer, NULL, &link_info, RV_get_link_info_callback) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't retrieve link type");

        /* Clean up the cURL headers to prevent issues in recursive call */
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;

        if (H5L_TYPE_HARD == link_info.type) {
#ifdef RV_CONNECTOR_DEBUG
            printf("-> Link was a hard link; retrieving target object's info\n\n");
#endif

            if (RV_parse_response(response_buffer.buffer, NULL, target_object_type,
                                  RV_get_link_obj_type_callback) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't retrieve hard link's target object type");
        } /* end if */
        else {
            size_t link_val_len = 0;

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Link was a %s link; retrieving link's value\n\n",
                   H5L_TYPE_SOFT == link_info.type ? "soft" : "external");
#endif

            if (RV_parse_response(response_buffer.buffer, &link_val_len, NULL, RV_get_link_val_callback) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't retrieve size of link's value");

            if (NULL == (tmp_link_val = RV_malloc(link_val_len)))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate space for link's value");

            if (RV_parse_response(response_buffer.buffer, &link_val_len, tmp_link_val,
                                  RV_get_link_val_callback) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't retrieve link's value");

            if (H5L_TYPE_EXTERNAL == link_info.type) {
                /* Unpack the external link's value buffer */
                if (H5Lunpack_elink_val(tmp_link_val, link_val_len, NULL, &ext_filename, &ext_obj_path) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't unpack external link's value buffer");

                /* Attempt to open the file referenced by the external link using the same access flags as
                 * used to open the file that the link resides within.
                 */
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
            } /* end else */
        }     /* end if */

        search_ret = RV_find_object_by_path(parent_obj, obj_path, target_object_type, obj_found_callback,
                                            callback_data_in, callback_data_out);
        if (!search_ret || search_ret < 0)
            FUNC_GOTO_ERROR(H5E_SYM, H5E_PATH, FAIL, "can't locate target object by path");

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
                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s", base_URL,
                                            parent_obj->URI)) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                        "link GET request URL size exceeded maximum URL size");
                } /* end if */
                else {
                    if (NULL == (url_encoded_path_name = H5_rest_url_encode_path(obj_path)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode object path");

                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s?h5path=%s", base_URL,
                                            is_relative_path ? parent_obj->URI : "", url_encoded_path_name)) <
                        0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                        "link GET request URL size exceeded maximum URL size");
                } /* end else */

                break;

            case H5I_DATATYPE:
                /* Handle the special case for the paths "." and "/" */
                if (!strcmp(obj_path, ".") || !strcmp(obj_path, "/")) {
                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s", base_URL,
                                            parent_obj->URI)) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                        "link GET request URL size exceeded maximum URL size");
                } /* end if */
                else {
                    if (NULL == (url_encoded_path_name = H5_rest_url_encode_path(obj_path)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode object path");

                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/?%s%s%sh5path=%s",
                                            base_URL, is_relative_path ? "grpid=" : "",
                                            is_relative_path ? parent_obj->URI : "",
                                            is_relative_path ? "&" : "", url_encoded_path_name)) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                        "link GET request URL size exceeded maximum URL size");
                } /* end else */

                break;

            case H5I_DATASET:
                /* Handle the special case for the paths "." and "/" */
                if (!strcmp(obj_path, ".") || !strcmp(obj_path, "/")) {
                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s", base_URL,
                                            parent_obj->URI)) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                        "link GET request URL size exceeded maximum URL size");
                } /* end if */
                else {
                    if (NULL == (url_encoded_path_name = H5_rest_url_encode_path(obj_path)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode object path");

                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/?%s%s%sh5path=%s",
                                            base_URL, is_relative_path ? "grpid=" : "",
                                            is_relative_path ? parent_obj->URI : "",
                                            is_relative_path ? "&" : "", url_encoded_path_name)) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                        "link GET request URL size exceeded maximum URL size");
                } /* end else */

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
                FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL,
                                "target object not a group, datatype or dataset");
        } /* end switch */

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Searching for object by URL: %s\n\n", request_url);
#endif

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

        CURL_PERFORM_NO_ERR(curl, FAIL);

        if (CURLE_OK != curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_response))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't get HTTP response code");

        ret_value = HTTP_SUCCESS(http_response);

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Object %s\n\n", ret_value ? "found" : "not found");
#endif

        if (ret_value > 0) {
            if (obj_found_callback && RV_parse_response(response_buffer.buffer, callback_data_in,
                                                        callback_data_out, obj_found_callback) < 0) {
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CALLBACK, FAIL, "can't perform callback operation");
            }

        } /* end if */
    }     /* end else */

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
} /* end RV_find_object_by_path() */

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
