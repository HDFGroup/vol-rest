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
 * Implementations of the file callbacks for the HDF5 REST VOL connector.
 */

#include "rest_vol_file.h"

herr_t RV_iterate_copy_hid_cb(hid_t obj_id, void *udata);
herr_t RV_iterate_count_obj_cb(hid_t obj_id, void *udata);

struct get_obj_count_udata_t {
    size_t obj_count;
    char  *local_filename;
} typedef get_obj_count_udata_t;

struct get_obj_ids_udata_t {
    hid_t *obj_id_list;
    size_t obj_count;
    size_t max_obj_count;
    char  *local_filename;
} typedef get_obj_ids_udata_t;

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
void *
RV_file_create(const char *name, unsigned flags, hid_t fcpl_id, hid_t fapl_id, hid_t dxpl_id, void **req)
{
    RV_object_t *new_file = NULL;
    size_t       name_length;
    size_t       host_header_len         = 0;
    size_t       base64_buf_size         = 0;
    size_t       plist_nalloc            = 0;
    size_t       create_request_nalloc   = 0;
    int          create_request_body_len = 0;
    char        *host_header             = NULL;
    char        *base64_plist_buffer     = NULL;
    const char  *fmt_string              = NULL;
    char        *create_request_body     = NULL;
    void        *ret_value               = NULL;
    void        *binary_plist_buffer     = NULL;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received file create call with following parameters:\n");
    printf("     - Filename: %s\n", name);
    printf("     - Creation flags: %s\n", file_flags_to_string(flags));
    printf("     - Default FCPL? %s\n", (H5P_FILE_CREATE_DEFAULT == fcpl_id) ? "yes" : "no");
    printf("     - Default FAPL? %s\n\n", (H5P_FILE_ACCESS_DEFAULT == fapl_id) ? "yes" : "no");
#endif

    if (fapl_id == H5I_INVALID_HID)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "invalid FAPL");

    /* Allocate and setup internal File struct */
    if (NULL == (new_file = (RV_object_t *)RV_malloc(sizeof(*new_file))))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate space for file object");

    if (H5_rest_set_connection_information(&new_file->u.file.server_info) < 0)
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTINIT, NULL, "can't set REST VOL connector connection information");

    new_file->URI[0]               = '\0';
    new_file->obj_type             = H5I_FILE;
    new_file->u.file.intent        = H5F_ACC_RDWR;
    new_file->u.file.filepath_name = NULL;
    new_file->u.file.fapl_id       = FAIL;
    new_file->u.file.fcpl_id       = FAIL;
    new_file->u.file.ref_count     = 1;

    /* Allocate root "path" on heap for consistency with other RV_object_t types */
    if ((new_file->handle_path = RV_malloc(2)) == NULL)
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate space for filepath");

    strncpy(new_file->handle_path, "/", 2);

    /* Copy the FAPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * H5Fget_access_plist() will function correctly. Note that due to the nature
     * of VOLs and needing to supply a FAPL to work correctly, the default case
     * should theoretically never be touched. However, it is included here for
     * the sake of completeness.
     */
    if (H5P_FILE_ACCESS_DEFAULT != fapl_id) {
        if ((new_file->u.file.fapl_id = H5Pcopy(fapl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy FAPL");
    } /* end if */
    else
        new_file->u.file.fapl_id = H5P_FILE_ACCESS_DEFAULT;

    /* Copy the FCPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * H5Fget_create_plist() will function correctly
     */
    if (H5P_FILE_CREATE_DEFAULT != fcpl_id) {
        if ((new_file->u.file.fcpl_id = H5Pcopy(fcpl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy FCPL");
    } /* end if */
    else
        new_file->u.file.fcpl_id = H5P_FILE_CREATE_DEFAULT;

    /* Store self-referential pointer in the domain field for this object
     * to simplify code for other types of objects
     */
    new_file->domain = new_file;

    /* Copy the path name into the new file object */
    name_length = strlen(name);

    if (NULL == (new_file->u.file.filepath_name = (char *)RV_malloc(name_length + 1)))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate space for filepath name");

    strncpy(new_file->u.file.filepath_name, name, name_length);
    new_file->u.file.filepath_name[name_length] = '\0';

    /* Setup the host header */
    host_header_len = name_length + strlen(host_string) + 1;
    if (NULL == (host_header = (char *)RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate space for request Host header");

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, name, name_length));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_USERNAME, new_file->u.file.server_info.username))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL username: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_PASSWORD, new_file->u.file.server_info.password))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL password: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL HTTP headers: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, new_file->u.file.server_info.base_URL))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL request URL: %s", curl_err_buf);

    /* Before making the actual request, check the file creation flags for
     * the use of H5F_ACC_TRUNC. In this case, we want to check with the
     * server before trying to create a file which already exists.
     */
    if (flags & H5F_ACC_TRUNC) {
        long http_response;

        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
            FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set up cURL to make HTTP GET request: %s",
                            curl_err_buf);

#ifdef RV_CONNECTOR_DEBUG
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
            FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTGET, NULL, "can't get HTTP response code");

        /* If the file exists, go ahead and delete it before proceeding */
        if (HTTP_SUCCESS(http_response)) {
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE"))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL,
                                "can't set up cURL to make HTTP DELETE request: %s", curl_err_buf);

#ifdef RV_CONNECTOR_DEBUG
            printf("-> File existed and H5F_ACC_TRUNC specified; deleting file\n\n");

            printf("   /*************************************\\\n");
            printf("-> | Making DELETE request to the server |\n");
            printf("   \\*************************************/\n\n");
#endif

            CURL_PERFORM(curl, H5E_FILE, H5E_CANTREMOVE, NULL);

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't reset cURL custom request: %s",
                                curl_err_buf);
        } /* end if */
    }     /* end if */

    if (H5Pencode2(fcpl_id, binary_plist_buffer, &plist_nalloc, H5P_DEFAULT) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTENCODE, NULL, "can't determine size needed for encoded gcpl");

    if ((binary_plist_buffer = RV_malloc(plist_nalloc)) == NULL)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTALLOC, NULL, "can't allocate space for encoded gcpl");

    if (H5Pencode2(fcpl_id, binary_plist_buffer, &plist_nalloc, H5P_DEFAULT) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTENCODE, NULL, "can't encode gcpl");

    if (RV_base64_encode(binary_plist_buffer, plist_nalloc, &base64_plist_buffer, &base64_buf_size) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTENCODE, NULL, "failed to base64 encode plist binary");

    fmt_string = "{"
                 "\"group\": {"
                 "\"creationProperties\": \"%s\""
                 "}"
                 "}";

    create_request_nalloc = strlen(fmt_string) + base64_buf_size + 1;

    if (NULL == (create_request_body = (char *)RV_malloc(create_request_nalloc)))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTALLOC, NULL, "can't allocate space for file create request body");

    if ((create_request_body_len =
             snprintf(create_request_body, create_request_nalloc, fmt_string, base64_plist_buffer)) < 0)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_SYSERRSTR, NULL, "snprintf error");

    if ((size_t)create_request_body_len >= create_request_nalloc)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_SYSERRSTR, NULL,
                        "group link create request body size exceeded allocated buffer size");

    upload_info uinfo;
    uinfo.buffer      = create_request_body;
    uinfo.buffer_size = (size_t)create_request_body_len;
    uinfo.bytes_sent  = 0;

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_UPLOAD, 1))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set up cURL to make HTTP PUT request: %s",
                        curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_READDATA, &uinfo))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL PUT data: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)create_request_body_len))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL PUT data size: %s", curl_err_buf);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Creating file\n\n");

    printf("   /**********************************\\\n");
    printf("-> | Making PUT request to the server |\n");
    printf("   \\**********************************/\n\n");
#endif
    CURL_PERFORM(curl, H5E_FILE, H5E_CANTCREATE, NULL);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Created file\n\n");
#endif

    /* Store the newly-created file's URI */
    if (RV_parse_response(response_buffer.buffer, NULL, new_file->URI, RV_copy_object_URI_callback) < 0)
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTCREATE, NULL, "can't parse new file's URI");

    /* Store server version */
    if (RV_parse_response(response_buffer.buffer, NULL, &new_file->u.file.server_info.version,
                          RV_parse_server_version) < 0)
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTCREATE, NULL, "can't parse server  version");

    if (rv_hash_table_insert(RV_type_info_array_g[H5I_FILE]->table, (char *)new_file->URI,
                             (char *)new_file) == 0)
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "Failed to add file to type info array");

    ret_value = (void *)new_file;

done:
#ifdef RV_CONNECTOR_DEBUG
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

    RV_free(base64_plist_buffer);
    RV_free(binary_plist_buffer);
    RV_free(create_request_body);

    /* Clean up allocated file object if there was an issue */
    if (new_file && !ret_value)
        if (RV_file_close(new_file, FAIL, NULL) < 0)
            FUNC_DONE_ERROR(H5E_FILE, H5E_CANTCLOSEOBJ, NULL, "can't close file");

    /* Reset cURL custom request to prevent issues with future requests */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL))
        FUNC_DONE_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't reset cURL custom request: %s", curl_err_buf);

    /* Unset cURL UPLOAD option to ensure that future requests don't try to use PUT calls */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_UPLOAD, 0))
        FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTSET, NULL, "can't unset cURL PUT option: %s", curl_err_buf);

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    PRINT_ERROR_STACK;

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
void *
RV_file_open(const char *name, unsigned flags, hid_t fapl_id, hid_t dxpl_id, void **req)
{
    RV_object_t *file = NULL;
    size_t       name_length;
    size_t       host_header_len = 0;
    char        *host_header     = NULL;
    void        *ret_value       = NULL;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received file open call with following parameters:\n");
    printf("     - Filename: %s\n", name);
    printf("     - File access flags: %s\n", file_flags_to_string(flags));
    printf("     - Default FAPL? %s\n\n", (H5P_FILE_ACCESS_DEFAULT == fapl_id) ? "yes" : "no");
#endif

    if (fapl_id == H5I_INVALID_HID)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "invalid FAPL");

    /* Allocate and setup internal File struct */
    if (NULL == (file = (RV_object_t *)RV_malloc(sizeof(*file))))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate space for file object");

    if (H5_rest_set_connection_information(&file->u.file.server_info) < 0)
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTINIT, NULL, "can't set REST VOL connector connection information");

    file->URI[0]               = '\0';
    file->obj_type             = H5I_FILE;
    file->u.file.intent        = flags;
    file->u.file.filepath_name = NULL;
    file->u.file.fapl_id       = FAIL;
    file->u.file.fcpl_id       = FAIL;
    file->u.file.ref_count     = 1;

    /* Allocate root "path" on heap for consistency with other RV_object_t types */
    if ((file->handle_path = RV_malloc(2)) == NULL)
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate space for filepath");

    strncpy(file->handle_path, "/", 2);

    /* Store self-referential pointer in the domain field for this object
     * to simplify code for other types of objects
     */
    file->domain = file;

    /* Copy the path name into the new file object */
    name_length = strlen(name);
    if (NULL == (file->u.file.filepath_name = (char *)RV_malloc(name_length + 1)))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate space for filepath name");

    strncpy(file->u.file.filepath_name, name, name_length);
    file->u.file.filepath_name[name_length] = '\0';

    /* Setup the host header */
    host_header_len = name_length + strlen(host_string) + 1;
    if (NULL == (host_header = (char *)RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate space for request Host header");

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, name, name_length));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_USERNAME, file->u.file.server_info.username))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL username: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_PASSWORD, file->u.file.server_info.password))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL password: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, file->u.file.server_info.base_URL))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL request URL: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL HTTP headers: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set up cURL to make HTTP GET request: %s",
                        curl_err_buf);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Retrieving info for file open\n\n");

    printf("   /**********************************\\\n");
    printf("-> | Making GET request to the server |\n");
    printf("   \\**********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_FILE, H5E_CANTOPENFILE, NULL);

    /* Store the opened file's URI */
    if (RV_parse_response(response_buffer.buffer, NULL, file->URI, RV_copy_object_URI_callback) < 0)
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, NULL, "can't parse file's URI");

    /* Store server version */
    if (RV_parse_response(response_buffer.buffer, NULL, &file->u.file.server_info.version,
                          RV_parse_server_version) < 0)
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTCREATE, NULL, "can't parse server version");

    /* Copy the FAPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * H5Fget_access_plist() will function correctly
     */
    /* XXX: Set any properties necessary */
    if (H5P_FILE_ACCESS_DEFAULT != fapl_id) {
        if ((file->u.file.fapl_id = H5Pcopy(fapl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy FAPL");
    } /* end if */
    else
        file->u.file.fapl_id = H5P_FILE_ACCESS_DEFAULT;

    /* Set up a FCPL for the file so that H5Fget_create_plist() will function correctly */
    if ((file->u.file.fcpl_id = H5Pcreate(H5P_FILE_CREATE)) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "can't create FCPL for file");

    if (rv_hash_table_insert(RV_type_info_array_g[H5I_FILE]->table, (char *)file->URI, (char *)file) == 0)
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "Failed to add file to type info array");

    ret_value = (void *)file;

done:
#ifdef RV_CONNECTOR_DEBUG
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
            FUNC_DONE_ERROR(H5E_FILE, H5E_CANTCLOSEOBJ, NULL, "can't close file");

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    PRINT_ERROR_STACK;

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
herr_t
RV_file_get(void *obj, H5VL_file_get_args_t *args, hid_t dxpl_id, void **req)
{
    RV_object_t *_obj      = (RV_object_t *)obj;
    herr_t       ret_value = SUCCEED;
    unsigned int requested_types;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received file get call with following parameters:\n");
    printf("     - File get call type: %s\n", file_get_type_to_string(args->op_type));
    printf("     - File's URI: %s\n", _obj->URI);
    printf("     - File's pathname: %s\n\n", _obj->domain->u.file.filepath_name);
#endif

    if ((args->op_type != H5VL_FILE_GET_NAME) && (H5I_FILE != _obj->obj_type))
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a file");

    switch (args->op_type) {
        case H5VL_FILE_GET_CONT_INFO:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "get container info is unsupported");
            break;

        /* H5Fget_access_plist */
        case H5VL_FILE_GET_FAPL: {
            hid_t *ret_id = &args->args.get_fapl.fapl_id;

            if ((*ret_id = H5Pcopy(_obj->u.file.fapl_id)) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, FAIL, "can't copy File FAPL");

            break;
        } /* H5VL_FILE_GET_FAPL */

        /* H5Fget_create_plist */
        case H5VL_FILE_GET_FCPL: {
            hid_t *ret_id = &args->args.get_fcpl.fcpl_id;

            if ((*ret_id = H5Pcopy(_obj->u.file.fcpl_id)) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, FAIL, "can't copy File FCPL");

            break;
        } /* H5VL_FILE_GET_FCPL */

        case H5VL_FILE_GET_FILENO:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "get file number is unsupported");
            break;

        /* H5Fget_intent */
        case H5VL_FILE_GET_INTENT: {
            unsigned *ret_intent = args->args.get_intent.flags;

            *ret_intent = _obj->u.file.intent;

            break;
        } /* H5VL_FILE_GET_INTENT */

        /* H5Fget_name */
        case H5VL_FILE_GET_NAME: {
            H5I_type_t obj_type      = args->args.get_name.type;
            size_t     name_buf_size = args->args.get_name.buf_size;
            char      *name_buf      = args->args.get_name.buf;
            ssize_t   *ret_size      = args->args.get_name.file_name_len;

            *ret_size = (ssize_t)strlen(_obj->domain->u.file.filepath_name);

            if (name_buf && name_buf_size) {
                strncpy(name_buf, _obj->domain->u.file.filepath_name, name_buf_size - 1);
                name_buf[name_buf_size - 1] = '\0';
            } /* end if */

            break;
        } /* H5VL_FILE_GET_NAME */

        /* H5Fget_obj_count */
        case H5VL_FILE_GET_OBJ_COUNT:
            requested_types = args->args.get_obj_count.types;

            if (args->args.get_obj_count.count == NULL)
                FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "given object id count pointer is NULL");

            get_obj_count_udata_t count_cb_args;
            count_cb_args.obj_count = 0;

            /* Requests with H5F_OBJ_ALL for file_id do not pass through the
             * VOL layer - assume single-file request */
            count_cb_args.local_filename = _obj->domain->u.file.filepath_name;

            if (requested_types & H5F_OBJ_FILE)
                if (H5Iiterate(H5I_FILE, RV_iterate_count_obj_cb, &count_cb_args) < 0)
                    FUNC_GOTO_ERROR(H5E_FILE, H5E_OBJECTITERERROR, FAIL, "can't iterate over file ids");

            if (requested_types & H5F_OBJ_GROUP)
                if (H5Iiterate(H5I_GROUP, RV_iterate_count_obj_cb, &count_cb_args) < 0)
                    FUNC_GOTO_ERROR(H5E_FILE, H5E_OBJECTITERERROR, FAIL, "can't iterate over group ids");

            if (requested_types & H5F_OBJ_DATATYPE)
                if (H5Iiterate(H5I_DATATYPE, RV_iterate_count_obj_cb, &count_cb_args) < 0)
                    FUNC_GOTO_ERROR(H5E_FILE, H5E_OBJECTITERERROR, FAIL, "can't iterate over datatype ids");

            if (requested_types & H5F_OBJ_DATASET)
                if (H5Iiterate(H5I_DATASET, RV_iterate_count_obj_cb, &count_cb_args) < 0)
                    FUNC_GOTO_ERROR(H5E_FILE, H5E_OBJECTITERERROR, FAIL, "can't iterate over dataset ids");

            if (requested_types & H5F_OBJ_ATTR)
                if (H5Iiterate(H5I_ATTR, RV_iterate_count_obj_cb, &count_cb_args) < 0)
                    FUNC_GOTO_ERROR(H5E_FILE, H5E_OBJECTITERERROR, FAIL, "can't iterate over attribute ids");

            /* Return count */
            *args->args.get_obj_count.count = count_cb_args.obj_count;

            break;
        /* H5Fget_obj_ids */
        case H5VL_FILE_GET_OBJ_IDS:
            if (args->args.get_obj_ids.oid_list == NULL)
                FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "given object id list buffer is NULL");

            if (args->args.get_obj_ids.count == NULL)
                FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "given object id count buffer is NULL");

            if (args->args.get_obj_ids.max_objs < 0)
                FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "invalid max object parameter");

            if (args->args.get_obj_ids.max_objs == 0)
                FUNC_GOTO_DONE(SUCCEED);

            *args->args.get_obj_ids.count = 0;
            requested_types               = args->args.get_obj_ids.types;

            get_obj_ids_udata_t iterate_cb_args;
            iterate_cb_args.obj_id_list   = args->args.get_obj_ids.oid_list;
            iterate_cb_args.obj_count     = 0;
            iterate_cb_args.max_obj_count = args->args.get_obj_ids.max_objs;
            /* Requests for object ids in all files do not pass through the
             * VOL layer - assume single-file request */
            iterate_cb_args.local_filename = _obj->domain->u.file.filepath_name;

            if (requested_types & H5F_OBJ_FILE)
                if (H5Iiterate(H5I_FILE, RV_iterate_copy_hid_cb, &iterate_cb_args) < 0)
                    FUNC_GOTO_ERROR(H5E_FILE, H5E_OBJECTITERERROR, FAIL, "can't iterate over file ids");

            if (requested_types & H5F_OBJ_GROUP)
                if (H5Iiterate(H5I_GROUP, RV_iterate_copy_hid_cb, &iterate_cb_args) < 0)
                    FUNC_GOTO_ERROR(H5E_FILE, H5E_OBJECTITERERROR, FAIL, "can't iterate over group ids");

            if (requested_types & H5F_OBJ_DATATYPE)
                if (H5Iiterate(H5I_DATATYPE, RV_iterate_copy_hid_cb, &iterate_cb_args) < 0)
                    FUNC_GOTO_ERROR(H5E_FILE, H5E_OBJECTITERERROR, FAIL, "can't iterate over datatype ids");

            if (requested_types & H5F_OBJ_DATASET)
                if (H5Iiterate(H5I_DATASET, RV_iterate_copy_hid_cb, &iterate_cb_args) < 0)
                    FUNC_GOTO_ERROR(H5E_FILE, H5E_OBJECTITERERROR, FAIL, "can't iterate over dataset ids");

            if (requested_types & H5F_OBJ_ATTR)
                if (H5Iiterate(H5I_ATTR, RV_iterate_copy_hid_cb, &iterate_cb_args) < 0)
                    FUNC_GOTO_ERROR(H5E_FILE, H5E_OBJECTITERERROR, FAIL, "can't iterate over attribute ids");

            *args->args.get_obj_ids.count = iterate_cb_args.obj_count;

            break;
        default:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTGET, FAIL, "can't get this type of information from file");
    } /* end switch */

done:
    PRINT_ERROR_STACK;

    return ret_value;
} /* end RV_file_get() */

/*-------------------------------------------------------------------------
 * Function:    RV_file_specific
 *
 * Purpose:     Performs a connector-specific operation on an HDF5 file, such
 *              as calling the H5Fflush routine.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
herr_t
RV_file_specific(void *obj, H5VL_file_specific_args_t *args, hid_t dxpl_id, void **req)
{
    RV_object_t   *file      = (RV_object_t *)obj;
    herr_t         ret_value = SUCCEED;
    size_t         host_header_len;
    size_t         name_length;
    long           http_response;
    char          *host_header = NULL;
    const char    *filename    = NULL;
    char          *request_url = NULL;
    server_info_t *server_info = NULL;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received file-specific call with following parameters:\n");
    printf("     - File-specific call type: %s\n", file_specific_type_to_string(args->op_type));
    if (file) {
        printf("     - File's URI: %s\n", file->URI);
        printf("     - File's pathname: %s\n", file->domain->u.file.filepath_name);
    } /* end if */
    printf("\n");
#endif

    if (file && H5I_FILE != file->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a file");

    switch (args->op_type) {
        /* H5Fflush */
        case H5VL_FILE_FLUSH: {
            /* H5Fflush() may be passed an object within the domain, so explicitly target
             * the containing domain. */
            RV_object_t *target_domain = file->domain;
            filename                   = target_domain->u.file.filepath_name;
            const char *flush_string   = "/?flush=1&rescan=1";

            name_length = strlen(filename);

            /* Setup the host header */
            host_header_len = name_length + strlen(host_string) + 1;

            if (NULL == (host_header = (char *)RV_malloc(host_header_len)))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL,
                                "can't allocate space for request Host header");

            strcpy(host_header, host_string);

            curl_headers = curl_slist_append(curl_headers, strncat(host_header, filename, name_length));

            /* Disable use of Expect: 100 Continue HTTP response */
            curl_headers = curl_slist_append(curl_headers, "Expect:");

            if (NULL == (request_url = (char *)RV_malloc(
                             strlen(flush_string) + strlen(target_domain->u.file.server_info.base_URL) + 1)))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL, "can't allocate space for request URL");

            snprintf(request_url, URL_MAX_LENGTH, "%s%s", target_domain->u.file.server_info.base_URL,
                     flush_string);

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_USERNAME, file->u.file.server_info.username))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, FAIL, "can't set cURL username: %s", curl_err_buf);
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_PASSWORD, file->u.file.server_info.password))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, FAIL, "can't set cURL password: %s", curl_err_buf);
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf);
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf);
            /* Server only checks for flush parameter on PUT operations */
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, FAIL, "can't set cURL to make HTTP PUTrequest: %s",
                                curl_err_buf);
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)0))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, FAIL, "can't set cURL upload size: %s", curl_err_buf);

            CURL_PERFORM_NO_ERR(curl, FAIL);

            if (CURLE_OK != curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_response))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTGET, FAIL, "can't get HTTP response code from cURL: %s",
                                curl_err_buf);

            if (http_response != HTTP_NO_CONTENT)
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTFLUSH, FAIL, "invalid server response from flush");

            break;
        }

        /* H5Freopen */
        case H5VL_FILE_REOPEN: {
            void **ret_file = args->args.reopen.file;

            if (NULL == (*ret_file = RV_file_open(file->u.file.filepath_name, file->u.file.intent,
                                                  file->u.file.fapl_id, dxpl_id, NULL)))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTOPENOBJ, FAIL, "can't re-open file");

            break;
        } /* H5VL_FILE_REOPEN */

        /* H5Fis_accessible */
        case H5VL_FILE_IS_ACCESSIBLE: {
            hbool_t *ret_is_accessible = args->args.is_accessible.accessible;
            filename                   = args->args.is_accessible.filename;
            hid_t fapl_id              = args->args.is_accessible.fapl_id;
            void *ret_file             = NULL;
            /* Initialize in case of failure */

            *ret_is_accessible = FALSE;

            H5E_BEGIN_TRY
            {
                ret_file = RV_file_open(filename, 0, fapl_id, dxpl_id, NULL);
            }
            H5E_END_TRY;

            if (ret_file != NULL) {
                *ret_is_accessible = TRUE;

                if (RV_file_close(ret_file, dxpl_id, NULL) < 0)
                    FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTCLOSEFILE, FAIL, "unable to close accessible file");
            }

            break;
        } /* H5VL_FILE_IS_ACCESSIBLE */

        /* H5Fdelete */
        case H5VL_FILE_DELETE: {
            filename = args->args.del.filename;

            name_length = strlen(filename);

            /* Setup the host header */
            host_header_len = name_length + strlen(host_string) + 1;

            if (NULL == (host_header = (char *)RV_malloc(host_header_len)))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL,
                                "can't allocate space for request Host header");

            strcpy(host_header, host_string);

            curl_headers = curl_slist_append(curl_headers, strncat(host_header, filename, name_length));

            /* Disable use of Expect: 100 Continue HTTP response */
            curl_headers = curl_slist_append(curl_headers, "Expect:");

            /* H5Fdelete doesn't receive a file handle, so the username/password must be pulled
             * from environment for now */
            if ((server_info = RV_calloc(sizeof(server_info_t))) == NULL)
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL, "can't allocate space for server information");

            if (H5_rest_set_connection_information(server_info) < 0)
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTGET, FAIL, "can't get server connection information");

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_USERNAME, server_info->username))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, FAIL, "can't set cURL username: %s", curl_err_buf);
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_PASSWORD, server_info->password))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, FAIL, "can't set cURL password: %s", curl_err_buf);

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf);
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, server_info->base_URL))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf);

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE"))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, FAIL,
                                "can't set up cURL to make HTTP DELETE request: %s", curl_err_buf);

            CURL_PERFORM(curl, H5E_FILE, H5E_CLOSEERROR, FAIL);

            break;
        } /* H5VL_FILE_DELETE */

        case H5VL_FILE_IS_EQUAL:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "checking of file equality is unsupported");
            break;

        default:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "unknown file operation");
    } /* end switch */

done:
    PRINT_ERROR_STACK;

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    /* Restore CUSTOMREQUEST to internal default */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP DELETE request: %s",
                        curl_err_buf);

    if (host_header) {
        RV_free(host_header);
    }

    if (request_url)
        RV_free(request_url);

    if (server_info) {
        RV_free(server_info->base_URL);
        RV_free(server_info->username);
        RV_free(server_info->password);
        RV_free(server_info);
    }

    return ret_value;
} /* end RV_file_specific() */

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
herr_t
RV_file_close(void *file, hid_t dxpl_id, void **req)
{

    RV_object_t *_file     = (RV_object_t *)file;
    herr_t       ret_value = SUCCEED;

    if (!_file)
        FUNC_GOTO_DONE(SUCCEED);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received file close call with following parameters:\n");
    printf("     - File's URI: %s\n", _file->URI);
    printf("     - File's object type: %s\n", object_type_to_string(_file->obj_type));
    if (_file->domain && _file->domain->u.file.filepath_name)
        printf("     - Filename: %s\n", _file->domain->u.file.filepath_name);
    printf("\n");
#endif

    if (H5I_FILE != _file->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a file");

    _file->u.file.ref_count--;

    if (_file->u.file.ref_count == 0) {
        if (_file->u.file.fapl_id >= 0) {
            if (_file->u.file.fapl_id != H5P_FILE_ACCESS_DEFAULT && H5Pclose(_file->u.file.fapl_id) < 0)
                FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close FAPL");
        } /* end if */
        if (_file->u.file.fcpl_id >= 0) {
            if (_file->u.file.fcpl_id != H5P_FILE_CREATE_DEFAULT && H5Pclose(_file->u.file.fcpl_id) < 0)
                FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close FCPL");
        } /* end if */

        if (_file->u.file.filepath_name) {
            RV_free(_file->u.file.filepath_name);
            _file->u.file.filepath_name = NULL;
        }

        RV_free(_file->u.file.server_info.username);
        RV_free(_file->u.file.server_info.password);
        RV_free(_file->u.file.server_info.base_URL);

        _file->u.file.server_info.username = NULL;
        _file->u.file.server_info.password = NULL;
        _file->u.file.server_info.base_URL = NULL;

        RV_free(_file->handle_path);
        _file->handle_path = NULL;

        if (RV_type_info_array_g[H5I_FILE])
            rv_hash_table_remove(RV_type_info_array_g[H5I_FILE]->table, (char *)_file->URI);

        RV_free(_file);
    }

    _file = NULL;

done:
    PRINT_ERROR_STACK;

    return ret_value;
} /* end RV_file_close() */

/*-------------------------------------------------------------------------
 * Function:    RV_iterate_copy_hid_cb
 *
 * Purpose:     Callback for H5Iiterate() that copies the hid_t
 *              of each library object into a list.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Matthew Larson
 *              August, 2023
 */
herr_t
RV_iterate_copy_hid_cb(hid_t obj_id, void *udata)
{
    herr_t               ret_value               = H5_ITER_CONT;
    get_obj_ids_udata_t *iterate_cb_args         = (get_obj_ids_udata_t *)udata;
    char                *containing_filename     = NULL;
    size_t               containing_filename_len = 0;
    H5I_type_t           id_type                 = H5I_UNINIT;
    htri_t               is_committed            = FALSE;

    if (iterate_cb_args->obj_count >= iterate_cb_args->max_obj_count)
        FUNC_GOTO_DONE(H5_ITER_STOP);

    /* Do not copy the id of transient datatypes */
    if ((id_type = H5Iget_type(obj_id)) < 0)
        FUNC_GOTO_ERROR(H5E_CALLBACK, H5E_ID, FAIL, "can't get identifier type");

    if ((id_type == H5I_DATATYPE)) {
        if ((is_committed = H5Tcommitted(obj_id)) < 0)
            FUNC_GOTO_ERROR(H5E_CALLBACK, H5E_DATATYPE, FAIL, "can't check if datatype is committed");

        if (!is_committed)
            FUNC_GOTO_DONE(H5_ITER_CONT);
    }

    if (iterate_cb_args->local_filename) {
        /* Require that obj id reside in local file */

        /* Get size of name */
        if ((containing_filename_len = H5Fget_name(obj_id, NULL, 0)) < 0)
            FUNC_GOTO_ERROR(H5E_CALLBACK, H5E_CANTGET, FAIL, "unable to get length of filename");

        if ((containing_filename = RV_malloc(containing_filename_len + 1)) == NULL)
            FUNC_GOTO_ERROR(H5E_CALLBACK, H5E_CANTALLOC, FAIL, "can't allocate space for filename");

        /* Get name */
        if (H5Fget_name(obj_id, containing_filename, containing_filename_len + 1) < 0)
            FUNC_GOTO_ERROR(H5E_CALLBACK, H5E_CANTGET, FAIL, "unable to get filename");

        if (!strcmp(iterate_cb_args->local_filename, containing_filename)) {
            iterate_cb_args->obj_id_list[iterate_cb_args->obj_count] = obj_id;
            iterate_cb_args->obj_count++;
        }
    }
    else {
        iterate_cb_args->obj_id_list[iterate_cb_args->obj_count] = obj_id;
        iterate_cb_args->obj_count++;
    }

done:

    if (containing_filename)
        RV_free(containing_filename);

    return ret_value;
}

/*-------------------------------------------------------------------------
 * Function:    RV_iterate_copy_hid_cb
 *
 * Purpose:     Callback for H5Iiterate() that count the number
 *              of open objects in the library.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Matthew Larson
 *              September, 2023
 */
herr_t
RV_iterate_count_obj_cb(hid_t obj_id, void *udata)
{
    herr_t                 ret_value               = H5_ITER_CONT;
    get_obj_count_udata_t *count_cb_args           = (get_obj_count_udata_t *)udata;
    char                  *containing_filename     = NULL;
    ssize_t                containing_filename_len = 0;
    H5I_type_t             id_type                 = H5I_UNINIT;
    htri_t                 is_committed            = FALSE;

    /* Do not copy the id of transient datatypes */
    if ((id_type = H5Iget_type(obj_id)) < 0)
        FUNC_GOTO_ERROR(H5E_CALLBACK, H5E_ID, FAIL, "can't get identifier type");

    if (id_type == H5I_DATATYPE) {
        if ((is_committed = H5Tcommitted(obj_id)) < 0)
            FUNC_GOTO_ERROR(H5E_CALLBACK, H5E_DATATYPE, FAIL, "can't check if datatype is committed");

        if (!is_committed)
            FUNC_GOTO_DONE(H5_ITER_CONT);
    }

    if (count_cb_args->local_filename) {
        /* Require that obj id reside in local file */

        /* Get size of name */
        if ((containing_filename_len = H5Fget_name(obj_id, NULL, 0)) < 0)
            FUNC_GOTO_ERROR(H5E_CALLBACK, H5E_CANTGET, FAIL, "unable to get length of filename");

        if ((containing_filename = RV_malloc((size_t)containing_filename_len + 1)) == NULL)
            FUNC_GOTO_ERROR(H5E_CALLBACK, H5E_CANTALLOC, FAIL, "can't allocate space for filename");

        /* Get name */
        if (H5Fget_name(obj_id, containing_filename, (size_t)containing_filename_len + 1) < 0)
            FUNC_GOTO_ERROR(H5E_CALLBACK, H5E_CANTGET, FAIL, "unable to get filename");

        if (!strcmp(count_cb_args->local_filename, containing_filename)) {
            count_cb_args->obj_count++;
        }
    }
    else {
        count_cb_args->obj_count++;
    }

done:

    if (containing_filename)
        RV_free(containing_filename);

    return ret_value;
}
