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
RV_file_create(const char *name, unsigned flags, hid_t fcpl_id, hid_t fapl_id,
    hid_t dxpl_id, void **req)
{
    RV_object_t *new_file = NULL;
    size_t       name_length;
    size_t       host_header_len = 0;
    char        *host_header = NULL;
    void        *ret_value = NULL;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received file create call with following parameters:\n");
    printf("     - Filename: %s\n", name);
    printf("     - Creation flags: %s\n", file_flags_to_string(flags));
    printf("     - Default FCPL? %s\n", (H5P_FILE_CREATE_DEFAULT == fcpl_id) ? "yes" : "no");
    printf("     - Default FAPL? %s\n\n", (H5P_FILE_ACCESS_DEFAULT == fapl_id) ? "yes" : "no");
#endif

    /*
     * If the connector has been dynamically loaded, the FAPL used for
     * creating the file will be a default FAPL, so we need to ensure
     * that the connection information gets set.
     */
    if (fapl_id == H5P_FILE_ACCESS_DEFAULT)
        if (H5_rest_set_connection_information() < 0)
            FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTINIT, NULL, "can't set REST VOL connector connection information");

    /* Allocate and setup internal File struct */
    if (NULL == (new_file = (RV_object_t *) RV_malloc(sizeof(*new_file))))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate space for file object");

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
    if (NULL == (new_file->u.file.filepath_name = (char *) RV_malloc(name_length + 1)))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate space for filepath name");

    strncpy(new_file->u.file.filepath_name, name, name_length);
    new_file->u.file.filepath_name[name_length] = '\0';

    /* Setup the host header */
    host_header_len = name_length + strlen(host_string) + 1;
    if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate space for request Host header");

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, name, name_length));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL HTTP headers: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, base_URL))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL request URL: %s", curl_err_buf);

    /* Before making the actual request, check the file creation flags for
     * the use of H5F_ACC_TRUNC. In this case, we want to check with the
     * server before trying to create a file which already exists.
     */
    if (flags & H5F_ACC_TRUNC) {
        long http_response;

        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
            FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set up cURL to make HTTP GET request: %s", curl_err_buf);

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
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set up cURL to make HTTP DELETE request: %s", curl_err_buf);

#ifdef RV_CONNECTOR_DEBUG
            printf("-> File existed and H5F_ACC_TRUNC specified; deleting file\n\n");

            printf("   /*************************************\\\n");
            printf("-> | Making DELETE request to the server |\n");
            printf("   \\*************************************/\n\n");
#endif

            CURL_PERFORM(curl, H5E_FILE, H5E_CANTREMOVE, NULL);

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't reset cURL custom request: %s", curl_err_buf);
        } /* end if */
    } /* end if */

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_UPLOAD, 1))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set up cURL to make HTTP PUT request: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_READDATA, NULL))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL PUT data: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, 0))
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

    ret_value = (void *) new_file;

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
    char        *host_header = NULL;
    void        *ret_value = NULL;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received file open call with following parameters:\n");
    printf("     - Filename: %s\n", name);
    printf("     - File access flags: %s\n", file_flags_to_string(flags));
    printf("     - Default FAPL? %s\n\n", (H5P_FILE_ACCESS_DEFAULT == fapl_id) ? "yes" : "no");
#endif

    /*
     * If the connector has been dynamically loaded, the FAPL used for
     * creating the file will be a default FAPL, so we need to ensure
     * that the connection information gets set.
     */
    if (fapl_id == H5P_FILE_ACCESS_DEFAULT)
        if (H5_rest_set_connection_information() < 0)
            FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTINIT, NULL, "can't set REST VOL connector connection information");

    /* Allocate and setup internal File struct */
    if (NULL == (file = (RV_object_t *) RV_malloc(sizeof(*file))))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate space for file object");

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
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate space for filepath name");

    strncpy(file->u.file.filepath_name, name, name_length);
    file->u.file.filepath_name[name_length] = '\0';

    /* Setup the host header */
    host_header_len = name_length + strlen(host_string) + 1;
    if (NULL == (host_header = (char *) RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate space for request Host header");

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, name, name_length));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL HTTP headers: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set up cURL to make HTTP GET request: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, base_URL))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTSET, NULL, "can't set cURL request URL: %s", curl_err_buf);

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

    ret_value = (void *) file;

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
RV_file_get(void *obj, H5VL_file_get_t get_type, hid_t dxpl_id, void **req, va_list arguments)
{
    RV_object_t *_obj = (RV_object_t *) obj;
    herr_t       ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received file get call with following parameters:\n");
    printf("     - File get call type: %s\n", file_get_type_to_string(get_type));
    printf("     - File's URI: %s\n", _obj->URI);
    printf("     - File's pathname: %s\n\n", _obj->domain->u.file.filepath_name);
#endif

    if (H5I_FILE != _obj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a file");

    switch (get_type) {
        case H5VL_FILE_GET_CONT_INFO:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "get container info is unsupported");
            break;

        /* H5Fget_access_plist */
        case H5VL_FILE_GET_FAPL:
        {
            hid_t *ret_id = va_arg(arguments, hid_t *);

            if ((*ret_id = H5Pcopy(_obj->u.file.fapl_id)) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, FAIL, "can't copy File FAPL");

            break;
        } /* H5VL_FILE_GET_FAPL */

        /* H5Fget_create_plist */
        case H5VL_FILE_GET_FCPL:
        {
            hid_t *ret_id = va_arg(arguments, hid_t *);

            if ((*ret_id = H5Pcopy(_obj->u.file.fcpl_id)) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, FAIL, "can't copy File FCPL");

            break;
        } /* H5VL_FILE_GET_FCPL */

        case H5VL_FILE_GET_FILENO:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "get file number is unsupported");
            break;

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

            if (name_buf && name_buf_size) {
                strncpy(name_buf, _obj->u.file.filepath_name, name_buf_size - 1);
                name_buf[name_buf_size - 1] = '\0';
            } /* end if */

            break;
        } /* H5VL_FILE_GET_NAME */

        /* H5Fget_obj_count */
        case H5VL_FILE_GET_OBJ_COUNT:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fget_obj_count is unsupported");
            break;

        /* H5Fget_obj_ids */
        case H5VL_FILE_GET_OBJ_IDS:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fget_obj_ids is unsupported");
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
RV_file_specific(void *obj, H5VL_file_specific_t specific_type, hid_t dxpl_id,
    void **req, va_list arguments)
{
    RV_object_t *file = (RV_object_t *) obj;
    herr_t       ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received file-specific call with following parameters:\n");
    printf("     - File-specific call type: %s\n", file_specific_type_to_string(specific_type));
    if (file) {
        printf("     - File's URI: %s\n", file->URI);
        printf("     - File's pathname: %s\n", file->domain->u.file.filepath_name);
    } /* end if */
    printf("\n");
#endif

    if (file && H5I_FILE != file->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a file");

    switch (specific_type) {
        /* H5Fflush */
        case H5VL_FILE_FLUSH:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fflush is unsupported");
            break;

        /* H5Freopen */
        case H5VL_FILE_REOPEN:
        {
            void **ret_file = va_arg(arguments, void **);

            if (NULL == (*ret_file = RV_file_open(file->u.file.filepath_name, file->u.file.intent, file->u.file.fapl_id, dxpl_id, NULL)))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTOPENOBJ, FAIL, "can't re-open file");

            break;
        } /* H5VL_FILE_REOPEN */

        /* H5Fmount */
        /*
        case H5VL_FILE_MOUNT:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fmount is unsupported");
            break;
        */

        /* H5Funmount */
        /*
        case H5VL_FILE_UNMOUNT:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Funmount is unsupported");
            break;
        */

        /* H5Fis_accessible */
        case H5VL_FILE_IS_ACCESSIBLE:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fis_accessible is unsupported");
            break;

        /* H5Fdelete */
        case H5VL_FILE_DELETE:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "H5Fdelete is unsupported");
            break;

        case H5VL_FILE_IS_EQUAL:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_UNSUPPORTED, FAIL, "checking of file equality is unsupported");
            break;

        default:
            FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "unknown file operation");
    } /* end switch */

done:
    PRINT_ERROR_STACK;

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
    RV_object_t *_file = (RV_object_t *) file;
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

    if (_file->u.file.filepath_name) {
        RV_free(_file->u.file.filepath_name);
        _file->u.file.filepath_name = NULL;
    }

    if (_file->u.file.fapl_id >= 0) {
        if (_file->u.file.fapl_id != H5P_FILE_ACCESS_DEFAULT && H5Pclose(_file->u.file.fapl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close FAPL");
    } /* end if */
    if (_file->u.file.fcpl_id >= 0) {
        if (_file->u.file.fcpl_id != H5P_FILE_CREATE_DEFAULT && H5Pclose(_file->u.file.fcpl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close FCPL");
    } /* end if */

    RV_free(_file);
    _file = NULL;

done:
    PRINT_ERROR_STACK;

    return ret_value;
} /* end RV_file_close() */
