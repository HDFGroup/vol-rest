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
 * Implementations of the datatype callbacks for the HDF5 REST VOL connector.
 */

#include "rest_vol_datatype.h"

/* Defines for Datatype operations */
#define DATATYPE_BODY_DEFAULT_SIZE 2048
#define ENUM_MAPPING_DEFAULT_SIZE  4096

/* Maximum length (in characters) of the string representation of an HDF5
 * predefined integer or floating-point type, such as H5T_STD_I8LE or
 * H5T_IEEE_F32BE
 */
#define PREDEFINED_DATATYPE_NAME_MAX_LENGTH 20

/* JSON keys to retrieve information about a datatype */
const char *type_class_keys[] = {"type", "class", (const char *)0};
const char *type_base_keys[]  = {"type", "base", (const char *)0};

/* JSON keys to retrieve information about a string datatype */
const char *str_length_keys[]  = {"type", "length", (const char *)0};
const char *str_charset_keys[] = {"type", "charSet", (const char *)0};
const char *str_pad_keys[]     = {"type", "strPad", (const char *)0};

/* JSON keys to retrieve information about a compound datatype */
const char *compound_field_keys[] = {"type", "fields", (const char *)0};

/* JSON keys to retrieve information about an array datatype */
const char *array_dims_keys[] = {"type", "dims", (const char *)0};

/* JSON keys to retrieve information about an enum datatype */
const char *enum_mapping_keys[] = {"type", "mapping", (const char *)0};

/* Conversion functions to convert a JSON-format string to an HDF5 Datatype or vice versa */
static hid_t       RV_convert_JSON_to_datatype(const char *type);
static const char *RV_convert_predefined_datatype_to_string(hid_t type_id);

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
void *
RV_datatype_commit(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t type_id,
                   hid_t lcpl_id, hid_t tcpl_id, hid_t tapl_id, hid_t dxpl_id, void **req)
{
    RV_object_t *parent                = (RV_object_t *)obj;
    RV_object_t *new_datatype          = NULL;
    size_t       commit_request_nalloc = 0;
    size_t       link_body_nalloc      = 0;
    size_t       host_header_len       = 0;
    size_t       datatype_body_len     = 0;
    size_t       path_size             = 0;
    size_t       path_len              = 0;
    const char  *base_URL              = NULL;
    char        *host_header           = NULL;
    char        *commit_request_body   = NULL;
    char        *datatype_body         = NULL;
    char        *link_body             = NULL;
    char        *path_dirname          = NULL;
    char         request_url[URL_MAX_LENGTH];
    int          commit_request_len = 0;
    int          link_body_len      = 0;
    int          url_len            = 0;
    void        *ret_value          = NULL;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received datatype commit call with following parameters:\n");
    printf("     - H5Tcommit variant: %s\n", name ? "H5Tcommit2" : "H5Tcommit_anon");
    if (name)
        printf("     - Datatype's name: %s\n", name);
    printf("     - Datatype's class: %s\n", datatype_class_to_string(type_id));
    printf("     - Datatype's parent object URI: %s\n", parent->URI);
    printf("     - Datatype's parent object type: %s\n", object_type_to_string(parent->obj_type));
    printf("     - Datatype's parent object domain path: %s\n", parent->domain->u.file.filepath_name);
    printf("     - Default LCPL? %s\n", (H5P_LINK_CREATE_DEFAULT == lcpl_id) ? "yes" : "no");
    printf("     - Default TCPL? %s\n", (H5P_DATATYPE_CREATE_DEFAULT == tcpl_id) ? "yes" : "no");
    printf("     - Default TAPL? %s\n\n", (H5P_DATATYPE_ACCESS_DEFAULT == tapl_id) ? "yes" : "no");
#endif

    if (H5I_FILE != parent->obj_type && H5I_GROUP != parent->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object not a file or group");

    if ((base_URL = parent->domain->u.file.server_info.base_URL) == NULL)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object does not have valid server URL");

    /* Check for write access */
    if (!(parent->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, NULL, "no write intent on file");

    if (tapl_id == H5I_INVALID_HID)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "invalid TAPL");

    /* Allocate and setup internal Datatype struct */
    if (NULL == (new_datatype = (RV_object_t *)RV_malloc(sizeof(*new_datatype))))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, NULL, "can't allocate space for datatype object");

    new_datatype->URI[0]              = '\0';
    new_datatype->obj_type            = H5I_DATATYPE;
    new_datatype->u.datatype.dtype_id = FAIL;
    new_datatype->u.datatype.tapl_id  = FAIL;
    new_datatype->u.datatype.tcpl_id  = FAIL;

    new_datatype->domain = parent->domain;
    parent->domain->u.file.ref_count++;

    if (type_id > 0) {
        if ((new_datatype->u.datatype.dtype_id = H5Tcopy(type_id)) == H5I_INVALID_HID)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCOPY, NULL, "can't copy type id");
    }

    new_datatype->handle_path = NULL;

    if (RV_set_object_handle_path(name, parent->handle_path, &new_datatype->handle_path) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PATH, NULL, "can't set up object path");

    /* Copy the TAPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * datatype access property list functions will function correctly
     */
    if (H5P_DATATYPE_ACCESS_DEFAULT != tapl_id) {
        if ((new_datatype->u.datatype.tapl_id = H5Pcopy(tapl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy TAPL");
    } /* end if */
    else
        new_datatype->u.datatype.tapl_id = H5P_DATATYPE_ACCESS_DEFAULT;

    /* Copy the TCPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * H5Tget_create_plist() will function correctly
     */
    if (H5P_DATATYPE_CREATE_DEFAULT != tcpl_id) {
        if ((new_datatype->u.datatype.tcpl_id = H5Pcopy(tcpl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy TCPL");
    } /* end if */
    else
        new_datatype->u.datatype.tcpl_id = H5P_DATATYPE_CREATE_DEFAULT;

    /* Convert the datatype into JSON to be used in the request body */
    if (RV_convert_datatype_to_JSON(type_id, &datatype_body, &datatype_body_len, FALSE,
                                    parent->domain->u.file.server_info.version) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, NULL, "can't convert datatype to JSON representation");

    /* If this is not a H5Tcommit_anon call, create a link for the Datatype
     * to link it into the file structure */
    if (name) {
        hbool_t           empty_dirname;
        char              target_URI[URI_MAX_LENGTH];
        const char *const link_basename    = H5_rest_basename(name);
        const char *const link_body_format = "\"link\": {"
                                             "\"id\": \"%s\", "
                                             "\"name\": \"%s\""
                                             "}";

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Creating JSON link for datatype\n\n");
#endif

        /* In case the user specified a path which contains multiple groups on the way to the
         * one which the datatype will ultimately be linked under, extract out the path to the
         * final group in the chain */
        if (NULL == (path_dirname = H5_rest_dirname(name)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, NULL, "invalid pathname for datatype link");
        empty_dirname = !strcmp(path_dirname, "");

        /* If the path to the final group in the chain wasn't empty, get the URI of the final
         * group in order to correctly link the datatype into the file structure. Otherwise,
         * the supplied parent group is the one housing the datatype, so just use its URI.
         */
        if (!empty_dirname) {
            H5I_type_t obj_type = H5I_GROUP;
            htri_t     search_ret;

            search_ret = RV_find_object_by_path(parent, path_dirname, &obj_type, RV_copy_object_URI_callback,
                                                NULL, target_URI);
            if (!search_ret || search_ret < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PATH, NULL, "can't locate target for dataset link");
        } /* end if */

        link_body_nalloc = strlen(link_body_format) + strlen(link_basename) +
                           (empty_dirname ? strlen(parent->URI) : strlen(target_URI)) + 1;
        if (NULL == (link_body = (char *)RV_malloc(link_body_nalloc)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, NULL, "can't allocate space for datatype link body");

        /* Form the Datatype Commit Link portion of the commit request using the above format
         * specifier and the corresponding arguments */
        if ((link_body_len = snprintf(link_body, link_body_nalloc, link_body_format,
                                      empty_dirname ? parent->URI : target_URI, link_basename)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, NULL, "snprintf error");

        if ((size_t)link_body_len >= link_body_nalloc)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, NULL,
                            "datatype link create request body size exceeded allocated buffer size");
    } /* end if */

    /* Form the request body to commit the Datatype */
    commit_request_nalloc = datatype_body_len + (link_body ? (size_t)link_body_len + 2 : 0) + 3;
    if (NULL == (commit_request_body = (char *)RV_malloc(commit_request_nalloc)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, NULL,
                        "can't allocate space for datatype commit request body");

    if ((commit_request_len = snprintf(commit_request_body, commit_request_nalloc, "{%s%s%s}", datatype_body,
                                       link_body ? ", " : "", link_body ? link_body : "")) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, NULL, "snprintf error");

    if ((size_t)commit_request_len >= commit_request_nalloc)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, NULL,
                        "datatype create request body size exceeded allocated buffer size");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Datatype commit request body:\n%s\n\n", commit_request_body);
#endif

    /* Setup the host header */
    host_header_len = strlen(parent->domain->u.file.filepath_name) + strlen(host_string) + 1;
    if (NULL == (host_header = (char *)RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, NULL, "can't allocate space for request Host header");

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, parent->domain->u.file.filepath_name,
                                                           host_header_len - strlen(host_string) - 1));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    /* Instruct cURL that we are sending JSON */
    curl_headers = curl_slist_append(curl_headers, "Content-Type: application/json");

    /* Redirect cURL from the base URL to "/datatypes" to commit the datatype */
    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes", base_URL)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, NULL, "snprintf error");

    if (url_len >= URL_MAX_LENGTH)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, NULL,
                        "datatype create URL size exceeded maximum URL size");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Datatype commit URL: %s\n\n", request_url);
#endif

    if (CURLE_OK !=
        curl_easy_setopt(curl, CURLOPT_USERNAME, new_datatype->domain->u.file.server_info.username))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTSET, NULL, "can't set cURL username: %s", curl_err_buf);
    if (CURLE_OK !=
        curl_easy_setopt(curl, CURLOPT_PASSWORD, new_datatype->domain->u.file.server_info.password))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTSET, NULL, "can't set cURL password: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTSET, NULL, "can't set cURL HTTP headers: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POST, 1))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTSET, NULL, "can't set up cURL to make HTTP POST request: %s",
                        curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDS, commit_request_body))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTSET, NULL, "can't set cURL POST data: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)commit_request_len))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTSET, NULL, "can't set cURL POST data size: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTSET, NULL, "can't set cURL request URL: %s", curl_err_buf);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Committing datatype\n\n");

    printf("   /***********************************\\\n");
    printf("-> | Making POST request to the server |\n");
    printf("   \\***********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_DATATYPE, H5E_BADVALUE, NULL);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Committed datatype\n\n");
#endif

    /* Store the newly-committed Datatype's URI */
    if (RV_parse_response(response_buffer.buffer, NULL, new_datatype->URI, RV_copy_object_URI_callback) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, NULL, "can't parse committed datatype's URI");

    if (rv_hash_table_insert(RV_type_info_array_g[H5I_DATATYPE]->table, (char *)new_datatype->URI,
                             (char *)new_datatype) == 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, NULL, "Failed to add datatype to type info array");

    ret_value = (void *)new_datatype;

done:
#ifdef RV_CONNECTOR_DEBUG
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
            FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, NULL, "can't close datatype");

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    PRINT_ERROR_STACK;

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
void *
RV_datatype_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t tapl_id,
                 hid_t dxpl_id, void **req)
{
    RV_object_t *parent   = (RV_object_t *)obj;
    RV_object_t *datatype = NULL;
    H5I_type_t   obj_type = H5I_UNINIT;
    loc_info     loc_info_out;
    htri_t       search_ret;
    size_t       path_size = 0;
    size_t       path_len  = 0;
    void        *ret_value = NULL;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received datatype open call with following parameters:\n");
    printf("     - loc_id object's URI: %s\n", parent->URI);
    printf("     - loc_id object's type: %s\n", object_type_to_string(parent->obj_type));
    printf("     - loc_id object's domain path: %s\n", parent->domain->u.file.filepath_name);
    printf("     - Path to datatype: %s\n", name);
    printf("     - Default TAPL? %s\n\n", (H5P_DATATYPE_ACCESS_DEFAULT) ? "yes" : "no");
#endif

    if (H5I_FILE != parent->obj_type && H5I_GROUP != parent->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object not a file or group");

    if (tapl_id == H5I_INVALID_HID)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "invalid TAPL");

    /* Allocate and setup internal Datatype struct */
    if (NULL == (datatype = (RV_object_t *)RV_malloc(sizeof(*datatype))))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, NULL, "can't allocate space for datatype object");

    datatype->URI[0]              = '\0';
    datatype->obj_type            = H5I_DATATYPE;
    datatype->u.datatype.dtype_id = FAIL;
    datatype->u.datatype.tapl_id  = FAIL;
    datatype->u.datatype.tcpl_id  = FAIL;

    datatype->domain = parent->domain;
    parent->domain->u.file.ref_count++;

    datatype->handle_path = NULL;

    if (RV_set_object_handle_path(name, parent->handle_path, &datatype->handle_path) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PATH, NULL, "can't set up object path");

    loc_info_out.URI         = datatype->URI;
    loc_info_out.domain      = datatype->domain;
    loc_info_out.GCPL_base64 = NULL;

    /* Locate datatype and set domain */
    search_ret = RV_find_object_by_path(parent, name, &obj_type, RV_copy_object_loc_info_callback,
                                        &datatype->domain->u.file.server_info, &loc_info_out);
    if (!search_ret || search_ret < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PATH, NULL, "can't locate datatype by path");

    datatype->domain = loc_info_out.domain;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Found datatype by given path\n\n");
#endif

    /* Set up the actual datatype by converting the string representation into an hid_t */
    if ((datatype->u.datatype.dtype_id = RV_parse_datatype(response_buffer.buffer, TRUE)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, NULL, "can't convert JSON to usable datatype");

    /* Copy the TAPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * datatype access property list functions will function correctly
     */
    if (H5P_DATATYPE_ACCESS_DEFAULT != tapl_id) {
        if ((datatype->u.datatype.tapl_id = H5Pcopy(tapl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy TAPL");
    } /* end if */
    else
        datatype->u.datatype.tapl_id = H5P_DATATYPE_ACCESS_DEFAULT;

    /* Set up a TCPL for the datatype so that H5Tget_create_plist() will function correctly.
       Note that currently there aren't any properties that can be set for a TCPL, however
       we still use one here specifically for H5Tget_create_plist(). */
    if ((datatype->u.datatype.tcpl_id = H5Pcreate(H5P_DATATYPE_CREATE)) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "can't create TCPL for datatype");

    if (rv_hash_table_insert(RV_type_info_array_g[H5I_DATATYPE]->table, (char *)datatype->URI,
                             (char *)datatype) == 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, NULL, "Failed to add datatype to type info array");

    ret_value = (void *)datatype;

done:
#ifdef RV_CONNECTOR_DEBUG
    printf("-> Datatype open response buffer:\n%s\n\n", response_buffer.buffer);

    if (datatype && ret_value) {
        printf("-> Datatype's info:\n");
        printf("     - Datatype's URI: %s\n", datatype->URI);
        printf("     - Datatype's object type: %s\n", object_type_to_string(datatype->obj_type));
        printf("     - Datatype's domain path: %s\n", datatype->domain->u.file.filepath_name);
        printf("     - Datatype's datatype class: %s\n\n",
               datatype_class_to_string(datatype->u.datatype.dtype_id));
    } /* end if */
#endif

    /* Clean up allocated datatype object if there was an issue */
    if (datatype && !ret_value)
        if (RV_datatype_close(datatype, FAIL, NULL) < 0)
            FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, NULL, "can't close datatype");

    PRINT_ERROR_STACK;

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
herr_t
RV_datatype_get(void *obj, H5VL_datatype_get_args_t *args, hid_t dxpl_id, void **req)
{
    RV_object_t *dtype     = (RV_object_t *)obj;
    herr_t       ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received datatype get call with following parameters:\n");
    printf("     - Datatype get call type: %s\n", datatype_get_type_to_string(args->op_type));
    printf("     - Datatype's URI: %s\n", dtype->URI);
    printf("     - Datatype's object type: %s\n", object_type_to_string(dtype->obj_type));
    printf("     - Datatype's domain path: %s\n\n", dtype->domain->u.file.filepath_name);
#endif

    if (H5I_DATATYPE != dtype->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a datatype");

    switch (args->op_type) {
        case H5VL_DATATYPE_GET_BINARY_SIZE: {
            size_t *binary_size = args->args.get_binary_size.size;

            if (H5Tencode(dtype->u.datatype.dtype_id, NULL, binary_size) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADTYPE, FAIL,
                                "can't determine serialized length of datatype");

            break;
        }
        case H5VL_DATATYPE_GET_BINARY: {
            /* ssize_t *nalloc = va_arg(arguments, ssize_t *); */
            void  *buf  = args->args.get_binary.buf;
            size_t size = args->args.get_binary.buf_size;

            if (H5Tencode(dtype->u.datatype.dtype_id, buf, &size) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADTYPE, FAIL,
                                "can't determine serialized length of datatype");

            /* *nalloc = (ssize_t) size; */

            break;
        } /* H5VL_DATATYPE_GET_BINARY */

        /* H5Tget_create_plist */
        case H5VL_DATATYPE_GET_TCPL: {
            hid_t *plist_id = &args->args.get_tcpl.tcpl_id;

            /* Retrieve the datatype's creation property list */
            if ((*plist_id = H5Pcopy(dtype->u.datatype.tcpl_id)) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get datatype creation property list");

            break;
        } /* H5VL_DATATYPE_GET_TCPL */

        default:
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL,
                            "can't get this type of information from datatype");
    } /* end switch */

done:
    PRINT_ERROR_STACK;

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
herr_t
RV_datatype_close(void *dt, hid_t dxpl_id, void **req)
{
    RV_object_t *_dtype    = (RV_object_t *)dt;
    herr_t       ret_value = SUCCEED;

    if (!_dtype)
        FUNC_GOTO_DONE(SUCCEED);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received datatype close call with following parameters:\n");
    printf("     - Datatype's URI: %s\n", _dtype->URI);
    printf("     - Datatype's object type: %s\n", object_type_to_string(_dtype->obj_type));
    if (_dtype->domain && _dtype->domain->u.file.filepath_name)
        printf("     - Datatype's domain path: %s\n", _dtype->domain->u.file.filepath_name);
    printf("\n");
#endif

    if (H5I_DATATYPE != _dtype->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a datatype");

    if (_dtype->u.datatype.dtype_id >= 0 && H5Tclose(_dtype->u.datatype.dtype_id) < 0)
        FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, FAIL, "can't close datatype");

    if (_dtype->u.datatype.tapl_id >= 0) {
        if (_dtype->u.datatype.tapl_id != H5P_DATATYPE_ACCESS_DEFAULT &&
            H5Pclose(_dtype->u.datatype.tapl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close TAPL");
    } /* end if */
    if (_dtype->u.datatype.tcpl_id >= 0) {
        if (_dtype->u.datatype.tcpl_id != H5P_DATATYPE_CREATE_DEFAULT &&
            H5Pclose(_dtype->u.datatype.tcpl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close TCPL");
    } /* end if */

    if (RV_type_info_array_g[H5I_DATATYPE])
        rv_hash_table_remove(RV_type_info_array_g[H5I_DATATYPE]->table, (char *)_dtype->URI);

    if (RV_file_close(_dtype->domain, H5P_DEFAULT, NULL) < 0)
        FUNC_DONE_ERROR(H5E_FILE, H5E_CANTCLOSEFILE, FAIL, "can't close file");

    RV_free(_dtype->handle_path);
    RV_free(_dtype);
    _dtype = NULL;

done:
    PRINT_ERROR_STACK;

    return ret_value;
} /* end RV_datatype_close() */

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
hid_t
RV_parse_datatype(char *type, hbool_t need_truncate)
{
    hbool_t substring_allocated = FALSE;
    hid_t   datatype            = FAIL;
    char   *type_string         = type;
    hid_t   ret_value           = FAIL;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Parsing datatype from HTTP response\n\n");
#endif

    if (!type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "datatype JSON buffer was NULL");

    if (need_truncate) {
        ptrdiff_t buf_ptrdiff;
        char     *type_section_ptr = NULL;
        char     *type_section_start, *type_section_end;

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Extraneous information included in HTTP response, extracting out datatype section\n\n");
#endif

        /* Start by locating the beginning of the "type" subsection, as indicated by the JSON "type" key */
        if (NULL == (type_section_ptr = strstr(type, "\"type\"")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL,
                            "can't find \"type\" information section in datatype string");

        /* Search for the initial '{' brace that begins the section */
        if (NULL == (type_section_start = strstr(type_section_ptr, "{")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL,
                            "can't find beginning '{' of \"type\" information section in datatype string - "
                            "misformatted JSON likely");

        /* Continue forward through the string buffer character-by-character until the end of this JSON
         * object section is found.
         */
        FIND_JSON_SECTION_END(type_section_start, type_section_end, H5E_DATATYPE, FAIL);

        buf_ptrdiff = type_section_end - type_section_ptr;
        if (buf_ptrdiff < 0)
            FUNC_GOTO_ERROR(
                H5E_INTERNAL, H5E_BADVALUE, FAIL,
                "unsafe cast: datatype buffer pointer difference was negative - this should not happen!");

        if (NULL == (type_string = (char *)RV_malloc((size_t)buf_ptrdiff + 3)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL,
                            "can't allocate space for \"type\" subsection");

        memcpy(type_string + 1, type_section_ptr, (size_t)buf_ptrdiff);

        /* Wrap the "type" substring in braces and NULL terminate it */
        type_string[0]                       = '{';
        type_string[(size_t)buf_ptrdiff + 1] = '}';
        type_string[(size_t)buf_ptrdiff + 2] = '\0';

        substring_allocated = TRUE;
    } /* end if */

    if ((datatype = RV_convert_JSON_to_datatype(type_string)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCONVERT, FAIL, "can't convert JSON representation to datatype");

    ret_value = datatype;

done:
    if (type_string && substring_allocated)
        RV_free(type_string);

    if (ret_value < 0 && datatype >= 0)
        if (H5Tclose(datatype) < 0)
            FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, FAIL, "can't close datatype");

    return ret_value;
} /* end RV_parse_datatype() */

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
herr_t
RV_convert_datatype_to_JSON(hid_t type_id, char **type_body, size_t *type_body_len, hbool_t nested,
                            server_api_version server_version)
{
    H5T_class_t type_class;
    const char *leading_string = "\"type\": "; /* Leading string for all datatypes */
    ptrdiff_t   buf_ptrdiff;
    hsize_t    *array_dims = NULL;
    htri_t      type_is_committed;
    size_t      leading_string_len = strlen(leading_string);
    size_t      out_string_len;
    size_t      bytes_to_print = 0; /* Used to calculate whether the datatype body buffer needs to be grown */
    size_t      type_size;
    size_t      i;
    hid_t       type_base_class         = FAIL;
    hid_t       compound_member         = FAIL;
    void       *enum_value              = NULL;
    char       *enum_value_name         = NULL;
    char       *enum_mapping            = NULL;
    char       *array_shape             = NULL;
    char       *array_base_type         = NULL;
    char      **compound_member_strings = NULL;
    char       *compound_member_name    = NULL;
    char       *out_string              = NULL;
    char       *out_string_curr_pos; /* The "current position" pointer used to print to the appropriate place
                                      in the buffer and not overwrite important leading data */
    int    bytes_printed = 0;
    herr_t ret_value     = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Converting datatype to JSON\n\n");
#endif

    if (!type_body)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL,
                        "invalid NULL pointer for converted datatype's string buffer");

    out_string_len = DATATYPE_BODY_DEFAULT_SIZE;
    if (NULL == (out_string = (char *)RV_malloc(out_string_len)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL,
                        "can't allocate space for converted datatype's string buffer");

    /* Keep track of the current position in the resulting string so everything
     * gets added smoothly
     */
    out_string_curr_pos = out_string;

    /* Make sure the buffer is at least large enough to hold the leading "type" string */
    CHECKED_REALLOC(out_string, out_string_len, leading_string_len + 1, out_string_curr_pos, H5E_DATATYPE,
                    FAIL);

    /* Add the leading "'type': " string */
    if (!nested) {
        strncpy(out_string, leading_string, out_string_len);
        out_string_curr_pos += leading_string_len;
    } /* end if */

    /* If the datatype is a committed type, append the datatype's URI and return */
    if ((type_is_committed = H5Tcommitted(type_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't determine if datatype is committed");

    if (type_is_committed) {
        RV_object_t *vol_obj;

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Datatype was a committed type\n\n");
#endif

        /* Retrieve the VOL object (RV_object_t *) from the datatype container */
        if (NULL == (vol_obj = H5VLobject(type_id)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get VOL object for committed datatype");

        /* Check whether the buffer needs to be grown */
        bytes_to_print = strlen(vol_obj->URI) + 2;

        buf_ptrdiff = out_string_curr_pos - out_string;
        if (buf_ptrdiff < 0)
            FUNC_GOTO_ERROR(
                H5E_INTERNAL, H5E_BADVALUE, FAIL,
                "unsafe cast: datatype buffer pointer difference was negative - this should not happen!");

        CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print, out_string_curr_pos,
                        H5E_DATATYPE, FAIL);

        if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t)buf_ptrdiff, "\"%s\"",
                                      vol_obj->URI)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error");

        if ((size_t)bytes_printed >= out_string_len - (size_t)buf_ptrdiff)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL,
                            "datatype string size exceeded allocated buffer size");

        out_string_curr_pos += bytes_printed;

        FUNC_GOTO_DONE(SUCCEED);
    } /* end if */

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Datatype was not a committed type\n\n");
#endif

    if (!(type_size = H5Tget_size(type_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid datatype");

    switch ((type_class = H5Tget_class(type_id))) {
        case H5T_INTEGER:
        case H5T_FLOAT: {
            const char       *type_name;
            const char *const int_class_str   = "H5T_INTEGER";
            const char *const float_class_str = "H5T_FLOAT";
            const char *const fmt_string      = "{"
                                                "\"class\": \"%s\", "
                                                "\"base\": \"%s\""
                                                "}";

            /* Convert the class and name of the datatype to JSON */
            if (NULL == (type_name = RV_convert_predefined_datatype_to_string(type_id)))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid datatype");

            /* Check whether the buffer needs to be grown */
            bytes_to_print = (H5T_INTEGER == type_class ? strlen(int_class_str) : strlen(float_class_str)) +
                             strlen(type_name) + (strlen(fmt_string) - 4) + 1;

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(
                    H5E_INTERNAL, H5E_BADVALUE, FAIL,
                    "unsafe cast: datatype buffer pointer difference was negative - this should not happen!");

            CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                            out_string_curr_pos, H5E_DATATYPE, FAIL);

            if ((bytes_printed =
                     snprintf(out_string_curr_pos, out_string_len - (size_t)buf_ptrdiff, fmt_string,
                              (H5T_INTEGER == type_class ? int_class_str : float_class_str), type_name)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error");

            if ((size_t)bytes_printed >= out_string_len - (size_t)buf_ptrdiff)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL,
                                "datatype string size exceeded allocated buffer size");

            out_string_curr_pos += bytes_printed;

            break;
        } /* H5T_INTEGER */ /* H5T_FLOAT */

        case H5T_STRING: {
            const char *const cset_ascii_string = "H5T_CSET_ASCII";
            const char *const cset_utf8_string  = "H5T_CSET_UTF8";
            const char       *cset              = NULL;
            H5T_cset_t        char_set          = H5T_CSET_ERROR;

            char_set = H5Tget_cset(type_id);

            htri_t is_vlen;

            if ((is_vlen = H5Tis_variable_str(type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL,
                                "can't determine if datatype is variable-length string");

            switch (char_set) {
                case (H5T_CSET_ASCII):
                    cset = cset_ascii_string;
                    break;
                case (H5T_CSET_UTF8):
                    if (!is_vlen && !(SERVER_VERSION_SUPPORTS_FIXED_LENGTH_UTF8(server_version)))
                        FUNC_GOTO_ERROR(
                            H5E_DATATYPE, H5E_UNSUPPORTED, FAIL,
                            "fixed-length UTF8 strings not supported until server version 0.8.5+");

                    cset = cset_utf8_string;
                    break;
                case (H5T_CSET_ERROR):
                default:
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid character set for string");
                    break;
            }

            /* Build the Datatype body by appending the character set for the string type,
             * any type of string padding, and the length of the string */
            /* Note: currently only H5T_CSET_ASCII is supported for the character set and
             * only H5T_STR_NULLTERM is supported for string padding for variable-length
             * strings and only H5T_STR_NULLPAD is supported for string padding for
             * fixed-length strings, but these may change in the future.
             */
            if (is_vlen) {
                const char *const nullterm_string = "H5T_STR_NULLTERM";
                const char *const fmt_string      = "{"
                                                    "\"class\": \"H5T_STRING\", "
                                                    "\"charSet\": \"%s\", "
                                                    "\"strPad\": \"%s\", "
                                                    "\"length\": \"H5T_VARIABLE\""
                                                    "}";

                bytes_to_print = (strlen(fmt_string) - 4) + strlen(cset) + strlen(nullterm_string) + 1;

                buf_ptrdiff = out_string_curr_pos - out_string;
                if (buf_ptrdiff < 0)
                    FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                    "unsafe cast: datatype buffer pointer difference was negative - this "
                                    "should not happen!");

                CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                                out_string_curr_pos, H5E_DATATYPE, FAIL);

                if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - leading_string_len,
                                              fmt_string, cset, nullterm_string)) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error");

                if ((size_t)bytes_printed >= out_string_len - leading_string_len)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL,
                                    "datatype string size exceeded allocated buffer size");

                out_string_curr_pos += bytes_printed;
            } /* end if */
            else {
                const char *const nullpad_string = "H5T_STR_NULLPAD";
                const char *const fmt_string     = "{"
                                                   "\"class\": \"H5T_STRING\", "
                                                   "\"charSet\": \"%s\", "
                                                   "\"strPad\": \"%s\", "
                                                   "\"length\": %zu"
                                                   "}";

                bytes_to_print =
                    (strlen(fmt_string) - 7) + strlen(cset) + strlen(nullpad_string) + MAX_NUM_LENGTH + 1;

                buf_ptrdiff = out_string_curr_pos - out_string;
                if (buf_ptrdiff < 0)
                    FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                    "unsafe cast: datatype buffer pointer difference was negative - this "
                                    "should not happen!");

                CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                                out_string_curr_pos, H5E_DATATYPE, FAIL);

                if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - leading_string_len,
                                              fmt_string, cset, nullpad_string, type_size)) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error");

                if ((size_t)bytes_printed >= out_string_len - leading_string_len)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL,
                                    "datatype string size exceeded allocated buffer size");

                out_string_curr_pos += bytes_printed;
            } /* end else */

            break;
        } /* H5T_STRING */

        case H5T_COMPOUND: {
            const char       *compound_type_leading_string = "{\"class\": \"H5T_COMPOUND\", \"fields\": [";
            size_t            compound_type_leading_strlen = strlen(compound_type_leading_string);
            int               nmembers;
            const char *const fmt_string = "{"
                                           "\"name\": \"%s\", "
                                           "%s"
                                           "}%s";

            if ((nmembers = H5Tget_nmembers(type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL,
                                "can't retrieve number of members in compound datatype");

            if (NULL == (compound_member_strings =
                             (char **)RV_malloc(((size_t)nmembers + 1) * sizeof(*compound_member_strings))))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL,
                                "can't allocate space for compound datatype member strings");

            for (i = 0; i < (size_t)nmembers + 1; i++)
                compound_member_strings[i] = NULL;

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(
                    H5E_INTERNAL, H5E_BADVALUE, FAIL,
                    "unsafe cast: datatype buffer pointer difference was negative - this should not happen!");

            CHECKED_REALLOC(out_string, out_string_len,
                            (size_t)buf_ptrdiff + compound_type_leading_strlen + 1, out_string_curr_pos,
                            H5E_DATATYPE, FAIL);

            strncpy(out_string_curr_pos, compound_type_leading_string, compound_type_leading_strlen);
            out_string_curr_pos += compound_type_leading_strlen;

            /* For each member in the compound type, convert it into its JSON representation
             * equivalent and append it to the growing datatype string
             */
            for (i = 0; i < (size_t)nmembers; i++) {
                if ((compound_member = H5Tget_member_type(type_id, (unsigned)i)) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get compound datatype member");

#ifdef RV_CONNECTOR_DEBUG
                printf("-> Converting compound datatype member %zu to JSON\n\n", i);
#endif

                if (RV_convert_datatype_to_JSON(compound_member, &compound_member_strings[i], NULL, FALSE,
                                                server_version) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL,
                                    "can't convert compound datatype member to JSON representation");

                if (NULL == (compound_member_name = H5Tget_member_name(type_id, (unsigned)i)))
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL,
                                    "can't get compound datatype member name");

                /* Check whether the buffer needs to be grown */
                bytes_to_print = strlen(compound_member_name) + strlen(compound_member_strings[i]) +
                                 (strlen(fmt_string) - 6) + (i < (size_t)nmembers - 1 ? 2 : 0) + 1;

                buf_ptrdiff = out_string_curr_pos - out_string;
                if (buf_ptrdiff < 0)
                    FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                    "unsafe cast: datatype buffer pointer difference was negative - this "
                                    "should not happen!");

                CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                                out_string_curr_pos, H5E_DATATYPE, FAIL);

                if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t)buf_ptrdiff,
                                              fmt_string, compound_member_name, compound_member_strings[i],
                                              i < (size_t)nmembers - 1 ? ", " : "")) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error");

                if ((size_t)bytes_printed >= out_string_len - (size_t)buf_ptrdiff)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL,
                                    "datatype string size exceeded allocated buffer size");

                out_string_curr_pos += bytes_printed;

                if (H5Tclose(compound_member) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, FAIL, "can't close datatype");
                if (H5free_memory(compound_member_name) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTFREE, FAIL,
                                    "can't free compound datatype member name buffer");
                compound_member      = FAIL;
                compound_member_name = NULL;
            } /* end for */

            /* Check if the buffer needs to grow to accommodate the closing ']' and '}' symbols, as well as
             * the NUL terminator
             */
            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(
                    H5E_INTERNAL, H5E_BADVALUE, FAIL,
                    "unsafe cast: datatype buffer pointer difference was negative - this should not happen!");

            CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + 3, out_string_curr_pos,
                            H5E_DATATYPE, FAIL);

            strcat(out_string_curr_pos, "]}");
            out_string_curr_pos += strlen("]}");

            break;
        } /* H5T_COMPOUND */

        case H5T_ENUM: {
            H5T_sign_t        type_sign;
            const char       *base_type_name;
            size_t            enum_mapping_length = 0;
            char             *mapping_curr_pos;
            int               enum_nmembers;
            const char *const fmt_string = "{"
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
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get sign of enum base datatype");

            if ((enum_nmembers = H5Tget_nmembers(type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL,
                                "can't get number of members of enumerated type");

            if (NULL == (enum_value = RV_calloc(H5_SIZEOF_LONG_LONG)))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL,
                                "can't allocate space for enum member value");

            enum_mapping_length = ENUM_MAPPING_DEFAULT_SIZE;
            if (NULL == (enum_mapping = (char *)RV_malloc(enum_mapping_length)))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for enum mapping");

            /* For each member in the enum type, retrieve the member's name and value, then
             * append these to the growing datatype string
             */
            for (i = 0, mapping_curr_pos = enum_mapping; i < (size_t)enum_nmembers; i++) {
                if (NULL == (enum_value_name = H5Tget_member_name(type_id, (unsigned)i)))
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "can't get name of enum member");

                if (H5Tget_member_value(type_id, (unsigned)i, enum_value) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't retrieve value of enum member");

                /* Determine the correct cast type for the enum value buffer and then append this member's
                 * name and numeric value to the mapping list.
                 */
                if (H5T_SGN_NONE == type_sign) {
                    const char *const mapping_fmt_string = "\"%s\": %llu%s";

                    /* Check if the mapping buffer needs to grow */
                    bytes_to_print = strlen(enum_value_name) + MAX_NUM_LENGTH +
                                     (strlen(mapping_fmt_string) - 8) +
                                     (i < (size_t)enum_nmembers - 1 ? 2 : 0) + 1;

                    buf_ptrdiff = mapping_curr_pos - enum_mapping;
                    if (buf_ptrdiff < 0)
                        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                        "unsafe cast: datatype buffer pointer difference was negative - this "
                                        "should not happen!");

                    CHECKED_REALLOC(enum_mapping, enum_mapping_length, (size_t)buf_ptrdiff + bytes_to_print,
                                    mapping_curr_pos, H5E_DATATYPE, FAIL);

                    if ((bytes_printed = snprintf(mapping_curr_pos, enum_mapping_length - (size_t)buf_ptrdiff,
                                                  mapping_fmt_string, enum_value_name,
                                                  *((unsigned long long int *)enum_value),
                                                  i < (size_t)enum_nmembers - 1 ? ", " : "")) < 0)
                        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error");
                } /* end if */
                else {
                    const char *const mapping_fmt_string = "\"%s\": %lld%s";

                    /* Check if the mapping buffer needs to grow */
                    bytes_to_print = strlen(enum_value_name) + MAX_NUM_LENGTH +
                                     (strlen(mapping_fmt_string) - 8) +
                                     (i < (size_t)enum_nmembers - 1 ? 2 : 0) + 1;

                    buf_ptrdiff = mapping_curr_pos - enum_mapping;
                    if (buf_ptrdiff < 0)
                        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                        "unsafe cast: datatype buffer pointer difference was negative - this "
                                        "should not happen!");

                    CHECKED_REALLOC(enum_mapping, enum_mapping_length, (size_t)buf_ptrdiff + bytes_to_print,
                                    mapping_curr_pos, H5E_DATATYPE, FAIL);

                    if ((bytes_printed =
                             snprintf(mapping_curr_pos, enum_mapping_length - (size_t)buf_ptrdiff,
                                      mapping_fmt_string, enum_value_name, *((long long int *)enum_value),
                                      i < (size_t)enum_nmembers - 1 ? ", " : "")) < 0)
                        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error");
                } /* end else */

                if ((size_t)bytes_printed >= enum_mapping_length - (size_t)buf_ptrdiff)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL,
                                    "enum member string size exceeded allocated mapping buffer size");

                mapping_curr_pos += bytes_printed;

                if (H5free_memory(enum_value_name) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTFREE, FAIL,
                                    "can't free memory allocated for enum member name");
                enum_value_name = NULL;
            } /* end for */

            /* Retrieve the enum type's base datatype and convert it into JSON as well */
            if ((type_base_class = H5Tget_super(type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get base datatype for enum type");

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Converting enum datatype's base datatype to JSON\n\n");
#endif

            if (NULL == (base_type_name = RV_convert_predefined_datatype_to_string(type_base_class)))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid datatype");

            /* Check whether the buffer needs to be grown */
            bytes_to_print = strlen(base_type_name) + strlen(enum_mapping) + (strlen(fmt_string) - 4) + 1;

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(
                    H5E_INTERNAL, H5E_BADVALUE, FAIL,
                    "unsafe cast: datatype buffer pointer difference was negative - this should not happen!");

            CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                            out_string_curr_pos, H5E_DATATYPE, FAIL);

            /* Build the Datatype body by appending the base integer type class for the enum
             * and the mapping values to map from numeric values to
             * string representations.
             */
            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t)buf_ptrdiff,
                                          fmt_string, base_type_name, enum_mapping)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error");

            if ((size_t)bytes_printed >= out_string_len - (size_t)buf_ptrdiff)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL,
                                "datatype string size exceeded allocated buffer size");

            out_string_curr_pos += bytes_printed;

            break;
        } /* H5T_ENUM */

        case H5T_ARRAY: {
            size_t            array_base_type_len = 0;
            char             *array_shape_curr_pos;
            int               ndims;
            const char *const fmt_string = "{"
                                           "\"class\": \"H5T_ARRAY\", "
                                           "\"base\": %s, "
                                           "\"dims\": %s"
                                           "}";

            if ((ndims = H5Tget_array_ndims(type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL,
                                "can't get array datatype number of dimensions");
            if (!ndims)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "0-sized array datatype");

            if (NULL == (array_shape = (char *)RV_malloc((size_t)(ndims * MAX_NUM_LENGTH + ndims + 3))))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL,
                                "can't allocate space for array datatype dimensionality string");
            array_shape_curr_pos  = array_shape;
            *array_shape_curr_pos = '\0';

            if (NULL == (array_dims = (hsize_t *)RV_malloc((size_t)ndims * sizeof(*array_dims))))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL,
                                "can't allocate space for array datatype dimensions");

            if (H5Tget_array_dims2(type_id, array_dims) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get array datatype dimensions");

            strcat(array_shape_curr_pos++, "[");

            /* Setup the shape of the array Datatype */
            for (i = 0; i < (size_t)ndims; i++) {
                if ((bytes_printed = snprintf(array_shape_curr_pos, MAX_NUM_LENGTH, "%s%" PRIuHSIZE,
                                              i > 0 ? "," : "", array_dims[i])) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error");

                if (bytes_printed >= MAX_NUM_LENGTH)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL,
                                    "array dimension size string exceeded maximum number string size");

                array_shape_curr_pos += bytes_printed;
            } /* end for */

            strcat(array_shape_curr_pos, "]");

            /* Get the class and name of the base datatype */
            if ((type_base_class = H5Tget_super(type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get base datatype for array type");

            if ((type_is_committed = H5Tcommitted(type_base_class)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL,
                                "can't determine if array base datatype is committed");

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Converting array datatype's base datatype to JSON\n\n");
#endif

            if (RV_convert_datatype_to_JSON(type_base_class, &array_base_type, &array_base_type_len, TRUE,
                                            server_version) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL,
                                "can't convert datatype to JSON representation");

            /* Check whether the buffer needs to be grown */
            bytes_to_print = array_base_type_len + strlen(array_shape) + (strlen(fmt_string) - 4) + 1;

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(
                    H5E_INTERNAL, H5E_BADVALUE, FAIL,
                    "unsafe cast: datatype buffer pointer difference was negative - this should not happen!");

            CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                            out_string_curr_pos, H5E_DATATYPE, FAIL);

            /* Build the Datatype body by appending the array type class and base type and dimensions of the
             * array */
            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t)buf_ptrdiff,
                                          fmt_string, array_base_type, array_shape)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error");

            if ((size_t)bytes_printed >= out_string_len - (size_t)buf_ptrdiff)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL,
                                "datatype string size exceeded allocated buffer size");

            out_string_curr_pos += bytes_printed;

            break;
        } /* H5T_ARRAY */

        case H5T_BITFIELD: {
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "unsupported datatype - bitfield");
            break;
        } /* H5T_BITFIELD */

        case H5T_OPAQUE: {
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "unsupported datatype - opaque");
            break;
        } /* H5T_OPAQUE */

        case H5T_REFERENCE: {
            htri_t            is_obj_ref;
            const char *const obj_ref_str = "H5T_STD_REF_OBJ";
            const char *const reg_ref_str = "H5T_STD_REF_DSETREG";
            const char *const fmt_string  = "{"
                                            "\"class\": \"H5T_REFERENCE\","
                                            "\"base\": \"%s\""
                                            "}";

            is_obj_ref = H5Tequal(type_id, H5T_STD_REF_OBJ);
            if (is_obj_ref < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't determine type of reference");

            bytes_to_print =
                (strlen(fmt_string) - 2) + (is_obj_ref ? strlen(obj_ref_str) : strlen(reg_ref_str)) + 1;

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(
                    H5E_INTERNAL, H5E_BADVALUE, FAIL,
                    "unsafe cast: datatype buffer pointer difference was negative - this should not happen!");

            CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                            out_string_curr_pos, H5E_DATATYPE, FAIL);

            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t)buf_ptrdiff,
                                          fmt_string, is_obj_ref ? obj_ref_str : reg_ref_str)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL, "snprintf error");

            if ((size_t)bytes_printed >= out_string_len - (size_t)buf_ptrdiff)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, FAIL,
                                "datatype string size exceeded allocated buffer size");

            out_string_curr_pos += bytes_printed;

            break;
        } /* H5T_REFERENCE */

        case H5T_VLEN: {
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "unsupported datatype - VLEN");
            break;
        } /* H5T_VLEN */

        case H5T_TIME: {
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "unsupported datatype - time");
            break;
        } /* H5T_TIME */

        case H5T_NO_CLASS:
        case H5T_NCLASSES:
        default:
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADTYPE, FAIL, "invalid datatype");
    } /* end switch */

done:
    if (ret_value >= 0) {
        *type_body = out_string;
        if (type_body_len) {
            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_DONE_ERROR(
                    H5E_INTERNAL, H5E_BADVALUE, FAIL,
                    "unsafe cast: datatype buffer pointer difference was negative - this should not happen!");
            else
                *type_body_len = (size_t)buf_ptrdiff;
        } /* end if */

#ifdef RV_CONNECTOR_DEBUG
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
    yajl_val parse_tree = NULL, key_obj = NULL;
    hsize_t *array_dims = NULL;
    size_t   i;
    hid_t    datatype                   = FAIL;
    hid_t   *compound_member_type_array = NULL;
    hid_t    enum_base_type             = FAIL;
    char   **compound_member_names      = NULL;
    char    *datatype_class             = NULL;
    char    *array_base_type_substring  = NULL;
    char    *tmp_cmpd_type_buffer       = NULL;
    char    *tmp_enum_base_type_buffer  = NULL;
    hid_t    ret_value                  = FAIL;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Converting JSON buffer %s to hid_t\n", type);
#endif

    /* Retrieve the datatype class */
    if (NULL == (parse_tree = yajl_tree_parse(type, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "JSON parse tree creation failed");

    if (NULL == (key_obj = yajl_tree_get(parse_tree, type_class_keys, yajl_t_string)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't parse datatype from JSON representation");

    if (NULL == (datatype_class = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't parse datatype from JSON representation");

    /* Create the appropriate datatype or copy an existing one */
    if (!strcmp(datatype_class, "H5T_INTEGER")) {
        hbool_t is_predefined = TRUE;
        char   *type_base     = NULL;

        if (NULL == (key_obj = yajl_tree_get(parse_tree, type_base_keys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve datatype's base type");

        if (NULL == (type_base = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve datatype's base type");

        if (is_predefined) {
            hbool_t is_unsigned;
            hid_t   predefined_type = FAIL;
            char   *type_base_ptr   = type_base + 8;

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Predefined Integer type sign: %c\n", *type_base_ptr);
#endif

            is_unsigned = (*type_base_ptr == 'U') ? TRUE : FALSE;

            switch (*(type_base_ptr + 1)) {
                /* 8-bit integer */
                case '8':
#ifdef RV_CONNECTOR_DEBUG
                    printf("-> 8-bit Integer type\n");
#endif

                    if (*(type_base_ptr + 2) == 'L') {
                        /* Litle-endian */
#ifdef RV_CONNECTOR_DEBUG
                        printf("-> Little-endian - %s\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U8LE : H5T_STD_I8LE;
                    } /* end if */
                    else {
#ifdef RV_CONNECTOR_DEBUG
                        printf("-> Big-endian - %s\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U8BE : H5T_STD_I8BE;
                    } /* end else */

                    break;

                /* 16-bit integer */
                case '1':
#ifdef RV_CONNECTOR_DEBUG
                    printf("-> 16-bit Integer type\n");
#endif

                    if (*(type_base_ptr + 3) == 'L') {
                        /* Litle-endian */
#ifdef RV_CONNECTOR_DEBUG
                        printf("-> Little-endian - %s\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U16LE : H5T_STD_I16LE;
                    } /* end if */
                    else {
#ifdef RV_CONNECTOR_DEBUG
                        printf("-> Big-endian - %s\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U16BE : H5T_STD_I16BE;
                    } /* end else */

                    break;

                /* 32-bit integer */
                case '3':
#ifdef RV_CONNECTOR_DEBUG
                    printf("-> 32-bit Integer type\n");
#endif

                    if (*(type_base_ptr + 3) == 'L') {
                        /* Litle-endian */
#ifdef RV_CONNECTOR_DEBUG
                        printf("-> Little-endian - %s\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U32LE : H5T_STD_I32LE;
                    } /* end if */
                    else {
#ifdef RV_CONNECTOR_DEBUG
                        printf("-> Big-endian - %s\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U32BE : H5T_STD_I32BE;
                    } /* end else */

                    break;

                /* 64-bit integer */
                case '6':
#ifdef RV_CONNECTOR_DEBUG
                    printf("-> 64-bit Integer type\n");
#endif

                    if (*(type_base_ptr + 3) == 'L') {
                        /* Litle-endian */
#ifdef RV_CONNECTOR_DEBUG
                        printf("-> Little-endian - %s\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U64LE : H5T_STD_I64LE;
                    } /* end if */
                    else {
#ifdef RV_CONNECTOR_DEBUG
                        printf("-> Big-endian - %s\n", is_unsigned ? "unsigned" : "signed");
#endif

                        predefined_type = is_unsigned ? H5T_STD_U64BE : H5T_STD_I64BE;
                    } /* end else */

                    break;

                default:
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "unknown predefined integer datatype");
            } /* end switch */

            if ((datatype = H5Tcopy(predefined_type)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCOPY, FAIL, "can't copy predefined integer datatype");
        } /* end if */
        else {
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL,
                            "non-predefined integer types are unsupported");
        } /* end else */
    }     /* end if */
    else if (!strcmp(datatype_class, "H5T_FLOAT")) {
        hbool_t is_predefined   = TRUE;
        hid_t   predefined_type = FAIL;
        char   *type_base       = NULL;

        if (NULL == (key_obj = yajl_tree_get(parse_tree, type_base_keys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve datatype's base type");

        if (NULL == (type_base = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve datatype's base type");

        if (is_predefined) {
            char *type_base_ptr = type_base + 10;

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Predefined Float type\n");
#endif

            switch (*type_base_ptr) {
                /* 32-bit floating point */
                case '3':
#ifdef RV_CONNECTOR_DEBUG
                    printf("-> 32-bit Floating Point - %s\n",
                           (*(type_base_ptr + 2) == 'L') ? "Little-endian" : "Big-endian");
#endif

                    /* Determine whether the floating point type is big- or little-endian */
                    predefined_type = (*(type_base_ptr + 2) == 'L') ? H5T_IEEE_F32LE : H5T_IEEE_F32BE;

                    break;

                /* 64-bit floating point */
                case '6':
#ifdef RV_CONNECTOR_DEBUG
                    printf("-> 64-bit Floating Point - %s\n",
                           (*(type_base_ptr + 2) == 'L') ? "Little-endian" : "Big-endian");
#endif

                    predefined_type = (*(type_base_ptr + 2) == 'L') ? H5T_IEEE_F64LE : H5T_IEEE_F64BE;

                    break;

                default:
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL,
                                    "unknown predefined floating-point datatype");
            } /* end switch */

            if ((datatype = H5Tcopy(predefined_type)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCOPY, FAIL,
                                "can't copy predefined floating-point datatype");
        } /* end if */
        else {
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL,
                            "non-predefined floating-point types are unsupported");
        } /* end else */
    }     /* end if */
    else if (!strcmp(datatype_class, "H5T_STRING")) {
        long long fixed_length = 0;
        hbool_t   is_variable_str;
        char     *charSet = NULL;
        char     *strPad  = NULL;

#ifdef RV_CONNECTOR_DEBUG
        printf("-> String datatype\n");
#endif

        /* Retrieve the string datatype's length and check if it's a variable-length string */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, str_length_keys, yajl_t_any)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve string datatype's length");

        is_variable_str = YAJL_IS_STRING(key_obj);

#ifdef RV_CONNECTOR_DEBUG
        printf("-> %s string\n", is_variable_str ? "Variable-length" : "Fixed-length");
#endif

        /* Retrieve and check the string datatype's character set */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, str_charset_keys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL,
                            "can't retrieve string datatype's character set");

        if (NULL == (charSet = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL,
                            "can't retrieve string datatype's character set");

#ifdef RV_CONNECTOR_DEBUG
        printf("-> String charSet: %s\n", charSet);
#endif

        /* Currently, only H5T_CSET_ASCII character set is supported */
        if (strcmp(charSet, "H5T_CSET_ASCII") && strcmp(charSet, "H5T_CSET_UTF8"))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL,
                            "unsupported character set for string datatype");

        /* Retrieve and check the string datatype's string padding */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, str_pad_keys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL,
                            "can't retrieve string datatype's padding type");

        if (NULL == (strPad = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL,
                            "can't retrieve string datatype's padding type");

        /* Currently, only H5T_STR_NULLPAD string padding is supported for fixed-length strings
         * and H5T_STR_NULLTERM for variable-length strings */
        if (strcmp(strPad, is_variable_str ? "H5T_STR_NULLTERM" : "H5T_STR_NULLPAD"))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL,
                            "unsupported string padding type for string datatype");

#ifdef RV_CONNECTOR_DEBUG
        printf("-> String padding: %s\n", strPad);
#endif

        /* Retrieve the length if the datatype is a fixed-length string */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, str_length_keys, yajl_t_any)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve string datatype's length");

        if (!is_variable_str)
            fixed_length = YAJL_GET_INTEGER(key_obj);
        if (fixed_length < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid datatype length");

        if ((datatype = H5Tcreate(H5T_STRING, is_variable_str ? H5T_VARIABLE : (size_t)fixed_length)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCREATE, FAIL, "can't create string datatype");

        if (!strcmp(charSet, "H5T_CSET_ASCII") && (H5Tset_cset(datatype, H5T_CSET_ASCII) < 0))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCREATE, FAIL,
                            "can't set ASCII character set for string datatype");
        if (!strcmp(charSet, "H5T_CSET_UTF8") && (H5Tset_cset(datatype, H5T_CSET_UTF8) < 0))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCREATE, FAIL,
                            "can't set UTF-8 character set for string datatype");

        if (H5Tset_strpad(datatype, is_variable_str ? H5T_STR_NULLTERM : H5T_STR_NULLPAD) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCREATE, FAIL,
                            "can't set string padding for string datatype");
    } /* end if */
    else if (!strcmp(datatype_class, "H5T_OPAQUE")) {
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_UNSUPPORTED, FAIL, "unsupported datatype - opaque");
    } /* end if */
    else if (!strcmp(datatype_class, "H5T_COMPOUND")) {
        ptrdiff_t buf_ptrdiff;
        size_t    tmp_cmpd_type_buffer_size;
        size_t    total_type_size  = 0;
        size_t    current_offset   = 0;
        char     *type_section_ptr = NULL;
        char     *section_start, *section_end;

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Compound Datatype\n");
#endif

        /* Retrieve the compound member fields array */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, compound_field_keys, yajl_t_array)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL,
                            "can't retrieve compound datatype's members array");

        if (!YAJL_GET_ARRAY(key_obj)->len)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "0-sized compound datatype members array");

        if (NULL == (compound_member_type_array = (hid_t *)RV_malloc(YAJL_GET_ARRAY(key_obj)->len *
                                                                     sizeof(*compound_member_type_array))))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate compound datatype");
        for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++)
            compound_member_type_array[i] = FAIL;

        if (NULL == (compound_member_names =
                         (char **)RV_malloc(YAJL_GET_ARRAY(key_obj)->len * sizeof(*compound_member_names))))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL,
                            "can't allocate compound datatype member names array");
        for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++)
            compound_member_names[i] = NULL;

        /* Allocate space for a temporary buffer used to extract and process the substring corresponding to
         * each compound member's datatype
         */
        tmp_cmpd_type_buffer_size = DATATYPE_BODY_DEFAULT_SIZE;
        if (NULL == (tmp_cmpd_type_buffer = (char *)RV_malloc(tmp_cmpd_type_buffer_size)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL,
                            "can't allocate temporary buffer for storing type information");

        /* Retrieve the names of all of the members of the Compound Datatype */
        for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++) {
            yajl_val compound_member_field;
            size_t   j;

            if (NULL == (compound_member_field = YAJL_GET_ARRAY(key_obj)->values[i]))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL,
                                "can't get compound field member %zu information", i);

            for (j = 0; j < YAJL_GET_OBJECT(compound_member_field)->len; j++) {
                if (!strcmp(YAJL_GET_OBJECT(compound_member_field)->keys[j], "name"))
                    if (NULL == (compound_member_names[i] =
                                     YAJL_GET_STRING(YAJL_GET_OBJECT(compound_member_field)->values[j])))
                        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL,
                                        "can't get compound field member %zu name", j);
            } /* end for */
        }     /* end for */

        /* For each field in the Compound Datatype's string representation, locate the beginning and end of
         * its "type" section and copy that substring into the temporary buffer. Then, convert that substring
         * into an hid_t and store it for later insertion once the Compound Datatype has been created.
         */

        /* Start the search from the "fields" JSON key */
        if (NULL == (type_section_ptr = strstr(type, "\"fields\"")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL,
                            "can't find \"fields\" information section in datatype string");

        for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++) {
            /* Find the beginning of the "type" section for this Compound Datatype member */
            if (NULL == (type_section_ptr = strstr(type_section_ptr, "\"type\"")))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL,
                                "can't find \"type\" information section in datatype string");

            /* Search for the initial '{' brace that begins the section */
            if (NULL == (section_start = strstr(type_section_ptr, "{")))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL,
                                "can't find beginning '{' of \"type\" information section in datatype string "
                                "- misformatted JSON likely");

            /* Continue forward through the string buffer character-by-character until the end of this JSON
             * object section is found.
             */
            FIND_JSON_SECTION_END(section_start, section_end, H5E_DATATYPE, FAIL);

            /* Check if the temporary buffer needs to grow to accommodate this "type" substring */
            buf_ptrdiff = section_end - type_section_ptr;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(
                    H5E_INTERNAL, H5E_BADVALUE, FAIL,
                    "unsafe cast: datatype buffer pointer difference was negative - this should not happen!");

            CHECKED_REALLOC_NO_PTR(tmp_cmpd_type_buffer, tmp_cmpd_type_buffer_size, (size_t)buf_ptrdiff + 3,
                                   H5E_DATATYPE, FAIL);

            /* Copy the "type" substring into the temporary buffer, wrapping it in enclosing braces to ensure
             * that the string-to-datatype conversion function can correctly process the string
             */
            memcpy(tmp_cmpd_type_buffer + 1, type_section_ptr, (size_t)buf_ptrdiff);
            tmp_cmpd_type_buffer[0]                       = '{';
            tmp_cmpd_type_buffer[(size_t)buf_ptrdiff + 1] = '}';
            tmp_cmpd_type_buffer[(size_t)buf_ptrdiff + 2] = '\0';

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Compound datatype member %zu name: %s\n", i, compound_member_names[i]);
            printf("-> Converting compound datatype member %zu from JSON to hid_t\n", i);
#endif

            if ((compound_member_type_array[i] = RV_convert_JSON_to_datatype(tmp_cmpd_type_buffer)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL,
                                "can't convert compound datatype member %zu from JSON representation", i);

            total_type_size += H5Tget_size(compound_member_type_array[i]);

            /* Adjust the type section pointer so that the next search does not return the same subsection */
            type_section_ptr = section_end + 1;
        } /* end for */

        if ((datatype = H5Tcreate(H5T_COMPOUND, total_type_size)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCREATE, FAIL, "can't create compound datatype");

        /* Insert all fields into the Compound Datatype */
        for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++) {
            if (H5Tinsert(datatype, compound_member_names[i], current_offset, compound_member_type_array[i]) <
                0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTINSERT, FAIL, "can't insert compound datatype member");
            current_offset += H5Tget_size(compound_member_type_array[i]);
        } /* end for */
    }     /* end if */
    else if (!strcmp(datatype_class, "H5T_ARRAY")) {
        const char *const type_string =
            "{\"type\":"; /* Gets prepended to the array "base" datatype substring */
        ptrdiff_t buf_ptrdiff;
        size_t    type_string_len = strlen(type_string);
        char     *base_type_substring_start, *base_type_substring_end;
        hid_t     base_type_id = FAIL;

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Array datatype\n");
#endif

        /* Retrieve the array dimensions */
        if (NULL == (key_obj = yajl_tree_get(parse_tree, array_dims_keys, yajl_t_array)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve array datatype's dimensions");

        if (!YAJL_GET_ARRAY(key_obj)->len)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "0-sized array");

        if (NULL == (array_dims = (hsize_t *)RV_malloc(YAJL_GET_ARRAY(key_obj)->len * sizeof(*array_dims))))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for array dimensions");

        for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++) {
            if (YAJL_IS_INTEGER(YAJL_GET_ARRAY(key_obj)->values[i]))
                array_dims[i] = (hsize_t)YAJL_GET_INTEGER(YAJL_GET_ARRAY(key_obj)->values[i]);
        } /* end for */

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Array datatype dimensions: [");
        for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++) {
            if (i > 0)
                printf(", ");
            printf("%llu", array_dims[i]);
        }
        printf("]\n");
#endif

        /* Locate the beginning and end braces of the "base" section for the array datatype */
        if (NULL == (base_type_substring_start = strstr(type, "\"base\"")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL,
                            "can't find \"base\" type information in datatype string");
        if (NULL == (base_type_substring_start = strstr(base_type_substring_start, "{")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL,
                            "incorrectly formatted \"base\" type information in datatype string");

        FIND_JSON_SECTION_END(base_type_substring_start, base_type_substring_end, H5E_DATATYPE, FAIL);

        /* Allocate enough memory to hold the "base" information substring, plus a few bytes for
         * the leading "type:" string and enclosing braces
         */
        buf_ptrdiff = base_type_substring_end - base_type_substring_start;
        if (buf_ptrdiff < 0)
            FUNC_GOTO_ERROR(
                H5E_INTERNAL, H5E_BADVALUE, FAIL,
                "unsafe cast: datatype buffer pointer difference was negative - this should not happen!");

        if (NULL ==
            (array_base_type_substring = (char *)RV_malloc((size_t)buf_ptrdiff + type_string_len + 2)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL,
                            "can't allocate space for array base type substring");

        /* In order for the conversion function to correctly process the datatype string, it must be in the
         * form {"type": {...}}. Since the enclosing braces and the leading "type:" string are missing from
         * the substring we have extracted, add them here before processing occurs.
         */
        memcpy(array_base_type_substring, type_string, type_string_len);
        memcpy(array_base_type_substring + type_string_len, base_type_substring_start, (size_t)buf_ptrdiff);
        array_base_type_substring[type_string_len + (size_t)buf_ptrdiff]     = '}';
        array_base_type_substring[type_string_len + (size_t)buf_ptrdiff + 1] = '\0';

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Converting array base datatype string to hid_t\n");
#endif

        /* Convert the string representation of the array's base datatype to an hid_t */
        if ((base_type_id = RV_convert_JSON_to_datatype(array_base_type_substring)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL,
                            "can't convert JSON representation of array base datatype to a usable form");

        if ((datatype = H5Tarray_create2(base_type_id, (unsigned)i, array_dims)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCREATE, FAIL, "can't create array datatype");
    } /* end if */
    else if (!strcmp(datatype_class, "H5T_ENUM")) {
        const char *const type_string =
            "{\"type\":"; /* Gets prepended to the enum "base" datatype substring */
        ptrdiff_t buf_ptrdiff;
        size_t    type_string_len  = strlen(type_string);
        char     *base_section_ptr = NULL;
        char     *base_section_end = NULL;

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Enum Datatype\n");
#endif

        /* Locate the beginning and end braces of the "base" section for the enum datatype */
        if (NULL == (base_section_ptr = strstr(type, "\"base\"")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL,
                            "incorrectly formatted datatype string - missing \"base\" datatype section");
        if (NULL == (base_section_ptr = strstr(base_section_ptr, "{")))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL,
                            "incorrectly formatted \"base\" datatype section in datatype string");

        FIND_JSON_SECTION_END(base_section_ptr, base_section_end, H5E_DATATYPE, FAIL);

        /* Allocate enough memory to hold the "base" information substring, plus a few bytes for
         * the leading "type:" string and enclosing braces
         */
        buf_ptrdiff = base_section_end - base_section_ptr;
        if (buf_ptrdiff < 0)
            FUNC_GOTO_ERROR(
                H5E_INTERNAL, H5E_BADVALUE, FAIL,
                "unsafe cast: datatype buffer pointer difference was negative - this should not happen!");

        if (NULL ==
            (tmp_enum_base_type_buffer = (char *)RV_malloc((size_t)buf_ptrdiff + type_string_len + 2)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL,
                            "can't allocate space for enum base datatype temporary buffer");

        /* In order for the conversion function to correctly process the datatype string, it must be in the
         * form {"type": {...}}. Since the enclosing braces and the leading "type:" string are missing from
         * the substring we have extracted, add them here before processing occurs.
         */
        memcpy(tmp_enum_base_type_buffer, type_string, type_string_len); /* Prepend the "type" string */
        memcpy(tmp_enum_base_type_buffer + type_string_len, base_section_ptr,
               (size_t)buf_ptrdiff); /* Append the "base" information substring */
        tmp_enum_base_type_buffer[type_string_len + (size_t)buf_ptrdiff]     = '}';
        tmp_enum_base_type_buffer[type_string_len + (size_t)buf_ptrdiff + 1] = '\0';

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Converting enum base datatype string to hid_t\n");
#endif

        /* Convert the enum's base datatype substring into an hid_t for use in the following H5Tenum_create
         * call */
        if ((enum_base_type = RV_convert_JSON_to_datatype(tmp_enum_base_type_buffer)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL,
                            "can't convert enum datatype's base datatype section from JSON into datatype");

        if ((datatype = H5Tenum_create(enum_base_type)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCREATE, FAIL, "can't create enum datatype");

        if (NULL == (key_obj = yajl_tree_get(parse_tree, enum_mapping_keys, yajl_t_object)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL,
                            "can't retrieve enum mapping from enum JSON representation");

        /* Retrieve the name and value of each member in the enum mapping, inserting them into the enum type
         * as new members */
        for (i = 0; i < YAJL_GET_OBJECT(key_obj)->len; i++) {
            long long val;

            if (!YAJL_IS_INTEGER(YAJL_GET_OBJECT(key_obj)->values[i]))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "enum member %zu value is not an integer",
                                i);

            val = YAJL_GET_INTEGER(YAJL_GET_OBJECT(key_obj)->values[i]);

            /* Convert the value from YAJL's integer representation to the base type of the enum datatype */
            if (H5Tconvert(H5T_NATIVE_LLONG, enum_base_type, 1, &val, NULL, H5P_DEFAULT) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL, "can't convert enum value to base type");

            if (H5Tenum_insert(datatype, YAJL_GET_OBJECT(key_obj)->keys[i], (void *)&val) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTINSERT, FAIL, "can't insert member into enum datatype");
        } /* end for */
    }     /* end if */
    else if (!strcmp(datatype_class, "H5T_REFERENCE")) {
        char *type_base;

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Reference datatype\n");
#endif

        if (NULL == (key_obj = yajl_tree_get(parse_tree, type_base_keys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve datatype's base type");

        if (NULL == (type_base = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_PARSEERROR, FAIL, "can't retrieve datatype's base type");

        if (!strcmp(type_base, "H5T_STD_REF_OBJ")) {
#ifdef RV_CONNECTOR_DEBUG
            printf("-> Object reference\n");
#endif

            if ((datatype = H5Tcopy(H5T_STD_REF_OBJ)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCOPY, FAIL, "can't copy object reference datatype");
        } /* end if */
        else if (!strcmp(type_base, "H5T_STD_REF_DSETREG")) {
#ifdef RV_CONNECTOR_DEBUG
            printf("-> Region reference\n");
#endif

            if ((datatype = H5Tcopy(H5T_STD_REF_DSETREG)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCOPY, FAIL, "can't copy region reference datatype");
        } /* end else if */
        else
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid reference type");
    } /* end if */
    else
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "unknown datatype class");

    ret_value = datatype;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Converted JSON buffer to hid_t ID %ld\n", datatype);
#endif

done:
#ifdef RV_CONNECTOR_DEBUG
    printf("\n");
#endif

    if (ret_value < 0 && datatype >= 0) {
        if (H5Tclose(datatype) < 0)
            FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, FAIL, "can't close datatype");
        if (compound_member_type_array) {
            while (FAIL != compound_member_type_array[i])
                if (H5Tclose(compound_member_type_array[i]) < 0)
                    FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, FAIL,
                                    "can't close compound datatype members");
        } /* end if */
    }     /* end if */

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
            FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, FAIL, "can't close enum base datatype");

    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_convert_JSON_to_datatype() */

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
    H5T_class_t type_class = H5T_NO_CLASS;
    H5T_order_t type_order = H5T_ORDER_NONE;
    H5T_sign_t  type_sign  = H5T_SGN_NONE;
    static char type_name[PREDEFINED_DATATYPE_NAME_MAX_LENGTH];
    size_t      type_size;
    int         type_str_len = 0;
    char       *ret_value    = type_name;

    if (H5T_NO_CLASS == (type_class = H5Tget_class(type_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, NULL, "invalid datatype");

    if (!(type_size = H5Tget_size(type_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, NULL, "invalid datatype size");

    if (H5T_ORDER_ERROR == (type_order = H5Tget_order(type_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, NULL, "invalid datatype ordering");

    if (type_class == H5T_INTEGER)
        if (H5T_SGN_ERROR == (type_sign = H5Tget_sign(type_id)))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, NULL, "invalid datatype sign");

    if ((type_str_len = snprintf(type_name, PREDEFINED_DATATYPE_NAME_MAX_LENGTH, "H5T_%s_%s%zu%s",
                                 (type_class == H5T_INTEGER) ? "STD" : "IEEE",
                                 (type_class == H5T_FLOAT)     ? "F"
                                 : (type_sign == H5T_SGN_NONE) ? "U"
                                                               : "I",
                                 type_size * 8, (type_order == H5T_ORDER_LE) ? "LE" : "BE")) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, NULL, "snprintf error");

    if (type_str_len >= PREDEFINED_DATATYPE_NAME_MAX_LENGTH)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_SYSERRSTR, NULL,
                        "predefined datatype name string size exceeded maximum size");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Converted predefined datatype to string representation %s\n\n", type_name);
#endif

done:
    return ret_value;
} /* end RV_convert_predefined_datatype_to_string() */

/*-------------------------------------------------------------------------
 * Function:    RV_detect_vl_vlstr_ref
 *
 * Purpose:     Determine if datatype conversion is necessary even if the
 *              types are the same.
 *
 * Return:      Success:        1 if conversion needed, 0 otherwise
 *              Failure:        -1
 *
 *-------------------------------------------------------------------------
 */
static htri_t
RV_detect_vl_vlstr_ref(hid_t type_id)
{
    hid_t       memb_type_id = -1;
    H5T_class_t tclass;
    htri_t      ret_value = FALSE;

    /* Get datatype class */
    if (H5T_NO_CLASS == (tclass = H5Tget_class(type_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get type class");

    switch (tclass) {
        case H5T_INTEGER:
        case H5T_FLOAT:
        case H5T_TIME:
        case H5T_BITFIELD:
        case H5T_OPAQUE:
        case H5T_ENUM:
            /* No conversion necessary */
            ret_value = FALSE;

            break;

        case H5T_STRING:
            /* Check for vlen string, need conversion if it's vl */
            if ((ret_value = H5Tis_variable_str(type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't check for variable length string");

            break;

        case H5T_COMPOUND: {
            int nmemb;
            int i;

            /* Get number of compound members */
            if ((nmemb = H5Tget_nmembers(type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL,
                                "can't get number of destination compound members");

            /* Iterate over compound members, checking for a member in
             * dst_type_id with no match in src_type_id */
            for (i = 0; i < nmemb; i++) {
                /* Get member type */
                if ((memb_type_id = H5Tget_member_type(type_id, (unsigned)i)) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get compound member type");

                /* Recursively check member type, this will fill in the
                 * member size */
                if ((ret_value = RV_detect_vl_vlstr_ref(memb_type_id)) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTINIT, FAIL,
                                    "can't check if background buffer needed");

                /* Close member type */
                if (H5Tclose(memb_type_id) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CLOSEERROR, FAIL, "can't close member type");
                memb_type_id = -1;

                /* If any member needs conversion the entire compound does
                 */
                if (ret_value) {
                    ret_value = TRUE;
                    break;
                } /* end if */
            }     /* end for */

            break;
        } /* end block */

        case H5T_ARRAY:
            /* Get parent type */
            if ((memb_type_id = H5Tget_super(type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get array parent type");

            /* Recursively check parent type */
            if ((ret_value = RV_detect_vl_vlstr_ref(memb_type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTINIT, FAIL, "can't check if background buffer needed");

            /* Close parent type */
            if (H5Tclose(memb_type_id) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CLOSEERROR, FAIL, "can't close array parent type");
            memb_type_id = -1;

            break;

        case H5T_REFERENCE:
        case H5T_VLEN:
            /* Always need type conversion for references and vlens */
            ret_value = TRUE;

            break;

        case H5T_NO_CLASS:
        case H5T_NCLASSES:
        default:
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid type class");
    } /* end switch */

done:
    /* Cleanup on failure */
    if (memb_type_id >= 0)
        if (H5Idec_ref(memb_type_id) < 0)
            FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTDEC, FAIL, "failed to close member type");

    return ret_value;
} /* end RV_detect_vl_vlstr_ref*/

/*-------------------------------------------------------------------------
 * Function:    RV_need_tconv
 *
 * Purpose:     Determine if datatype conversion is necessary.
 *
 * Return:      Success:        1 if conversion needed, 0 otherwise
 *              Failure:        -1
 *
 *-------------------------------------------------------------------------
 */
htri_t
RV_need_tconv(hid_t src_type_id, hid_t dst_type_id)
{
    htri_t types_equal;
    htri_t ret_value;

    /* Check if the types are equal */
    if ((types_equal = H5Tequal(src_type_id, dst_type_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCOMPARE, FAIL, "can't check if types are equal");

    if (types_equal) {
        /* Check if conversion is needed anyways due to presence of a vlen or
         * reference type */
        if ((ret_value = RV_detect_vl_vlstr_ref(src_type_id)) < 0)
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTINIT, FAIL, "can't check for vlen or reference type");
    } /* end if */
    else
        ret_value = TRUE;

done:
    return ret_value;
} /* end RV_need_tconv() */

/*-------------------------------------------------------------------------
 * Function:    RV_need_bkg
 *
 * Purpose:     Determine if a background buffer is needed for conversion.
 *
 * Return:      Success:        1 if bkg buffer needed, 0 otherwise
 *              Failure:        -1
 *
 * Programmer:  Neil Fortner
 *              February, 2017
 *
 *-------------------------------------------------------------------------
 */
static htri_t
RV_need_bkg(hid_t src_type_id, hid_t dst_type_id, hbool_t dst_file, size_t *dst_type_size, hbool_t *fill_bkg)
{
    hid_t       memb_type_id     = -1;
    hid_t       src_memb_type_id = -1;
    char       *memb_name        = NULL;
    size_t      memb_size;
    H5T_class_t tclass;
    htri_t      ret_value = FALSE;

    assert(dst_type_size);
    assert(fill_bkg);

    /* Get destination type size */
    if ((*dst_type_size = H5Tget_size(dst_type_id)) == 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get source type size");

    /* Get datatype class */
    if (H5T_NO_CLASS == (tclass = H5Tget_class(dst_type_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get type class");

    switch (tclass) {
        case H5T_INTEGER:
        case H5T_FLOAT:
        case H5T_TIME:
        case H5T_STRING:
        case H5T_BITFIELD:
        case H5T_OPAQUE:
        case H5T_ENUM:

            /* No background buffer necessary */
            ret_value = FALSE;

            break;

        case H5T_REFERENCE:
        case H5T_VLEN:

            /* If the destination type is in the file, the background buffer
             * is necessary so we can delete old sequences. */
            if (dst_file) {
                ret_value = TRUE;
                *fill_bkg = TRUE;
            } /* end if */
            else
                ret_value = FALSE;

            break;

        case H5T_COMPOUND: {
            int    nmemb;
            size_t size_used = 0;
            int    src_i;
            int    i;

            /* We must always provide a background buffer for compound
             * conversions.  Only need to check further to see if it must be
             * filled. */
            ret_value = TRUE;

            /* Get number of compound members */
            if ((nmemb = H5Tget_nmembers(dst_type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL,
                                "can't get number of destination compound members");

            /* Iterate over compound members, checking for a member in
             * dst_type_id with no match in src_type_id */
            for (i = 0; i < nmemb; i++) {
                /* Get member type */
                if ((memb_type_id = H5Tget_member_type(dst_type_id, (unsigned)i)) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get compound member type");

                /* Get member name */
                if (NULL == (memb_name = H5Tget_member_name(dst_type_id, (unsigned)i)))
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get compound member name");

                /* Check for matching name in source type */
                H5E_BEGIN_TRY
                {
                    src_i = H5Tget_member_index(src_type_id, memb_name);
                }
                H5E_END_TRY

                /* Free memb_name */
                if (H5free_memory(memb_name) < 0)
                    FUNC_GOTO_ERROR(H5E_RESOURCE, H5E_CANTFREE, FAIL, "can't free member name");
                memb_name = NULL;

                /* If no match was found, this type is not being filled in,
                 * so we must fill the background buffer */
                if (src_i < 0) {
                    if (H5Tclose(memb_type_id) < 0)
                        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CLOSEERROR, FAIL, "can't close member type");
                    memb_type_id = -1;
                    *fill_bkg    = TRUE;
                    FUNC_GOTO_DONE(TRUE);
                } /* end if */

                /* Open matching source type */
                if ((src_memb_type_id = H5Tget_member_type(src_type_id, (unsigned)src_i)) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get compound member type");

                /* Recursively check member type, this will fill in the
                 * member size */
                if (RV_need_bkg(src_memb_type_id, memb_type_id, dst_file, &memb_size, fill_bkg) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTINIT, FAIL,
                                    "can't check if background buffer needed");

                /* Close source member type */
                if (H5Tclose(src_memb_type_id) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CLOSEERROR, FAIL, "can't close member type");
                src_memb_type_id = -1;

                /* Close member type */
                if (H5Tclose(memb_type_id) < 0)
                    FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CLOSEERROR, FAIL, "can't close member type");
                memb_type_id = -1;

                /* If the source member type needs the background filled, so
                 * does the parent */
                if (*fill_bkg)
                    FUNC_GOTO_DONE(TRUE);

                /* Keep track of the size used in compound */
                size_used += memb_size;
            } /* end for */

            /* Check if all the space in the type is used.  If not, we must
             * fill the background buffer. */
            /* TODO: This is only necessary on read, we don't care about
             * compound gaps in the "file" DSINC */
            assert(size_used <= *dst_type_size);
            if (size_used != *dst_type_size)
                *fill_bkg = TRUE;

            break;
        } /* end block */

        case H5T_ARRAY:
            /* Get parent type */
            if ((memb_type_id = H5Tget_super(dst_type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get array parent type");

            /* Get source parent type */
            if ((src_memb_type_id = H5Tget_super(src_type_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get array parent type");

            /* Recursively check parent type */
            if ((ret_value = RV_need_bkg(src_memb_type_id, memb_type_id, dst_file, &memb_size, fill_bkg)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTINIT, FAIL, "can't check if background buffer needed");

            /* Close source parent type */
            if (H5Tclose(src_memb_type_id) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CLOSEERROR, FAIL, "can't close array parent type");
            src_memb_type_id = -1;

            /* Close parent type */
            if (H5Tclose(memb_type_id) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CLOSEERROR, FAIL, "can't close array parent type");
            memb_type_id = -1;

            break;

        case H5T_NO_CLASS:
        case H5T_NCLASSES:
        default:
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "invalid type class");
    } /* end switch */

done:
    /* Cleanup on failure */
    if (ret_value < 0) {
        if (memb_type_id >= 0)
            if (H5Idec_ref(memb_type_id) < 0)
                FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTDEC, FAIL, "failed to close member type");
        if (src_memb_type_id >= 0)
            if (H5Idec_ref(src_memb_type_id) < 0)
                FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTDEC, FAIL, "failed to close source member type");
        RV_free(memb_name);
        memb_name = NULL;
    } /* end if */

    return ret_value;
} /* end RV_need_bkg() */

/*-------------------------------------------------------------------------
 * Function:    RV_tconv_init
 *
 * Purpose:     Initialize several variables necessary for type conversion.
 *              - Checks if background buffer must be allocated and filled
 *              - Allocates conversion buffer if reuse of dst buffer is not possible
 *              - Allocates background buffer if needed and reuse is not possible
 *
 * Return:      Success:        0
 *              Failure:        -1
 *
 *-------------------------------------------------------------------------
 */
herr_t
RV_tconv_init(hid_t src_type_id, size_t *src_type_size, hid_t dst_type_id, size_t *dst_type_size,
              size_t num_elem, hbool_t clear_tconv_buf, hbool_t dst_file, void **tconv_buf, void **bkg_buf,
              RV_tconv_reuse_t *reuse, hbool_t *fill_bkg)
{
    htri_t need_bkg;
    herr_t ret_value = SUCCEED;

    assert(src_type_size);
    assert(dst_type_size);
    assert(tconv_buf);
    assert(!*tconv_buf);
    assert(bkg_buf);
    assert(!*bkg_buf);
    assert(fill_bkg);
    assert(!*fill_bkg);

    /*
     * If there is no selection in the file dataspace, don't bother
     * trying to allocate any type conversion buffers.
     */
    if (num_elem == 0)
        FUNC_GOTO_DONE(SUCCEED);

    /* Get source type size */
    if ((*src_type_size = H5Tget_size(src_type_id)) == 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get source type size");

    /* Check if we need a background buffer */
    if ((need_bkg = RV_need_bkg(src_type_id, dst_type_id, dst_file, dst_type_size, fill_bkg)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTINIT, FAIL, "can't check if background buffer needed");

    /* Check for reusable destination buffer */
    if (reuse) {
        assert(*reuse == RV_TCONV_REUSE_NONE);

        /* Use dest buffer for type conversion if it large enough, otherwise
         * use it for the background buffer if one is needed. */
        if (*dst_type_size >= *src_type_size)
            *reuse = RV_TCONV_REUSE_TCONV;
        else if (need_bkg)
            *reuse = RV_TCONV_REUSE_BKG;
    } /* end if */

    /* Allocate conversion buffer if it is not being reused */
    if (!reuse || (*reuse != RV_TCONV_REUSE_TCONV)) {
        if (clear_tconv_buf) {
            if (NULL == (*tconv_buf = RV_calloc(
                             num_elem * (*src_type_size > *dst_type_size ? *src_type_size : *dst_type_size))))
                FUNC_GOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, FAIL, "can't allocate type conversion buffer");
        } /* end if */
        else if (NULL ==
                 (*tconv_buf = RV_malloc(
                      num_elem * (*src_type_size > *dst_type_size ? *src_type_size : *dst_type_size))))
            FUNC_GOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, FAIL, "can't allocate type conversion buffer");
    } /* end if */

    /* Allocate background buffer if one is needed and it is not being
     * reused */
    if (need_bkg && (!reuse || (*reuse != RV_TCONV_REUSE_BKG)))
        if (NULL == (*bkg_buf = RV_calloc(num_elem * *dst_type_size)))
            FUNC_GOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, FAIL, "can't allocate background buffer");

done:
    /* Cleanup on failure */
    if (ret_value < 0) {
        RV_free(*tconv_buf);
        RV_free(*bkg_buf);

        *tconv_buf = NULL;
        *bkg_buf   = NULL;
        if (reuse)
            *reuse = RV_TCONV_REUSE_NONE;
    } /* end if */

    return ret_value;
} /* end RV_tconv_init() */
