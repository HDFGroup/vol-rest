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
 * Implementations of the group callbacks for the HDF5 REST VOL connector.
 */

#include "rest_vol_group.h"

/* Set of callbacks for RV_parse_response() */
static herr_t RV_get_group_info_callback(char *HTTP_response, void *callback_data_in,
                                         void *callback_data_out);

/* JSON keys to retrieve the number of links in a group */
const char *group_link_count_keys[] = {"linkCount", (const char *)0};

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
void *
RV_group_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t lcpl_id,
                hid_t gcpl_id, hid_t gapl_id, hid_t dxpl_id, void **req)
{
    RV_object_t *parent                = (RV_object_t *)obj;
    RV_object_t *new_group             = NULL;
    size_t       create_request_nalloc = 0;
    size_t       host_header_len       = 0;
    size_t       base64_buf_size       = 0;
    size_t       plist_nalloc          = 0;
    size_t       path_size             = 0;
    size_t       path_len              = 0;
    const char  *base_URL              = NULL;
    char        *host_header           = NULL;
    char        *create_request_body   = NULL;
    char        *path_dirname          = NULL;
    char        *base64_plist_buffer   = NULL;
    char         target_URI[URI_MAX_LENGTH];
    char         request_url[URL_MAX_LENGTH];
    char        *escaped_group_name      = NULL;
    int          create_request_body_len = 0;
    int          url_len                 = 0;
    void        *binary_plist_buffer     = NULL;
    void        *ret_value               = NULL;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received group create call with following parameters:\n");
    printf("     - H5Gcreate variant: %s\n", name ? "H5Gcreate2" : "H5Gcreate_anon");
    if (name)
        printf("     - Group's name: %s\n", name);
    printf("     - Group parent object's URI: %s\n", parent->URI);
    printf("     - Group parent object's type: %s\n", object_type_to_string(parent->obj_type));
    printf("     - Group parent object's domain path: %s\n", parent->domain->u.file.filepath_name);
    printf("     - Default GCPL? %s\n", (H5P_GROUP_CREATE_DEFAULT == gcpl_id) ? "yes" : "no");
    printf("     - Default GAPL? %s\n\n", (H5P_GROUP_ACCESS_DEFAULT == gapl_id) ? "yes" : "no");
#endif

    if (H5I_FILE != parent->obj_type && H5I_GROUP != parent->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object not a file or group");

    if ((base_URL = parent->domain->u.file.server_info.base_URL) == NULL)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object does not have valid server URL");

    if (gapl_id == H5I_INVALID_HID)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "invalid GAPL");

    /* Check for write access */
    if (!(parent->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, NULL, "no write intent on file");

    /* Allocate and setup internal Group struct */
    if (NULL == (new_group = (RV_object_t *)RV_malloc(sizeof(*new_group))))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTALLOC, NULL, "can't allocate space for group object");

    if (!parent->handle_path)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_BADVALUE, NULL, "parent object has NULL path");

    new_group->URI[0]          = '\0';
    new_group->obj_type        = H5I_GROUP;
    new_group->u.group.gapl_id = FAIL;
    new_group->u.group.gcpl_id = FAIL;

    new_group->domain = parent->domain;
    parent->domain->u.file.ref_count++;

    new_group->handle_path = NULL;

    if (RV_set_object_handle_path(name, parent->handle_path, &new_group->handle_path) < 0)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_PATH, NULL, "can't set up object path");

    /* Copy the GAPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * group access property list functions will function correctly
     */
    if (H5P_GROUP_ACCESS_DEFAULT != gapl_id) {
        if ((new_group->u.group.gapl_id = H5Pcopy(gapl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy GAPL");
    } /* end if */
    else
        new_group->u.group.gapl_id = H5P_GROUP_ACCESS_DEFAULT;

    /* Copy the GCPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * H5Gget_create_plist() will function correctly
     */
    if (H5P_GROUP_CREATE_DEFAULT != gcpl_id) {
        if ((new_group->u.group.gcpl_id = H5Pcopy(gcpl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy GCPL");
    } /* end if */
    else
        new_group->u.group.gcpl_id = H5P_GROUP_CREATE_DEFAULT;

    /* If this is not a H5Gcreate_anon call, create a link for the Group
     * to link it into the file structure
     */
    if (name) {
        const char *path_basename = H5_rest_basename(name);
        hbool_t     empty_dirname;
        size_t      escaped_name_size = 0;

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Creating JSON link for group\n\n");
#endif

        /* In case the user specified a path which contains multiple groups on the way to the
         * one which this group will ultimately be linked under, extract out the path to the
         * final group in the chain */
        if (NULL == (path_dirname = H5_rest_dirname(name)))
            FUNC_GOTO_ERROR(H5E_SYM, H5E_BADVALUE, NULL, "invalid pathname for group link");
        empty_dirname = !strcmp(path_dirname, "");

        /* If the path to the final group in the chain wasn't empty, get the URI of the final
         * group in order to correctly link this group into the file structure. Otherwise,
         * the supplied parent group is the one housing this group, so just use its URI.
         */
        if (!empty_dirname) {
            H5I_type_t obj_type = H5I_GROUP;
            htri_t     search_ret;

            search_ret = RV_find_object_by_path(parent, path_dirname, &obj_type, RV_copy_object_URI_callback,
                                                NULL, target_URI);

            if (!search_ret || search_ret < 0) {
                unsigned     crt_intmd_group;
                hid_t        intmd_group_id = H5I_INVALID_HID;
                RV_object_t *intmd_group;

                if (H5Pget_create_intermediate_group(lcpl_id, &crt_intmd_group))
                    FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, NULL, "can't get flag value in lcpl");

                if (crt_intmd_group) {
                    /* Remove trailing slash to avoid infinite loop due to H5_dirname */
                    if (path_dirname[strlen(path_dirname) - 1] == '/')
                        path_dirname[strlen(path_dirname) - 1] = '\0';

                    if (NULL == (intmd_group = RV_group_create(obj, loc_params, path_dirname, lcpl_id,
                                                               gcpl_id, gapl_id, dxpl_id, req)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTOPENOBJ, NULL,
                                        "can't create intermediate group automatically");

                    /* Get URI of final group now that it has been created */
                    search_ret = RV_find_object_by_path(parent, path_dirname, &obj_type,
                                                        RV_copy_object_URI_callback, NULL, target_URI);

                    RV_group_close(intmd_group, H5P_DEFAULT, NULL);
                }
                else
                    FUNC_GOTO_ERROR(H5E_SYM, H5E_PATH, NULL, "can't locate target for group link");
            }

        } /* end if */

        const char *const fmt_string = "{"
                                       "\"link\": {"
                                       "\"id\": \"%s\", "
                                       "\"name\": \"%s\""
                                       "},"
                                       "\"creationProperties\": \"%s\""
                                       "}";

        /* Form the request body to link the new group to the parent object */

        /* Encode GCPL to send to server */
        if (H5Pencode2(gcpl_id, binary_plist_buffer, &plist_nalloc, H5P_DEFAULT) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTENCODE, NULL, "can't determine size needed for encoded gcpl");

        if ((binary_plist_buffer = RV_malloc(plist_nalloc)) == NULL)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTALLOC, NULL, "can't allocate space for encoded gcpl");

        if (H5Pencode2(gcpl_id, binary_plist_buffer, &plist_nalloc, H5P_DEFAULT) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTENCODE, NULL, "can't encode gcpl");

        if (RV_base64_encode(binary_plist_buffer, plist_nalloc, &base64_plist_buffer, &base64_buf_size) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTENCODE, NULL, "failed to base64 encode plist binary");

        /* Escape group name to be sent as JSON */
        if (RV_JSON_escape_string(path_basename, escaped_group_name, &escaped_name_size) < 0)
            FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTENCODE, NULL, "can't get size of JSON escaped group name");

        if ((escaped_group_name = RV_malloc(escaped_name_size)) == NULL)
            FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTALLOC, NULL, "can't allocate space for escaped group name");

        if (RV_JSON_escape_string(path_basename, escaped_group_name, &escaped_name_size) < 0)
            FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTENCODE, NULL, "can't JSON escape group name");

        create_request_nalloc = strlen(fmt_string) + strlen(escaped_group_name) +
                                (empty_dirname ? strlen(parent->URI) : strlen(target_URI)) + base64_buf_size +
                                1;
        if (NULL == (create_request_body = (char *)RV_malloc(create_request_nalloc)))
            FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTALLOC, NULL,
                            "can't allocate space for group create request body");

        if ((create_request_body_len = snprintf(create_request_body, create_request_nalloc, fmt_string,
                                                empty_dirname ? parent->URI : target_URI, escaped_group_name,
                                                (char *)base64_plist_buffer)) < 0)
            FUNC_GOTO_ERROR(H5E_SYM, H5E_SYSERRSTR, NULL, "snprintf error");

        if ((size_t)create_request_body_len >= create_request_nalloc)
            FUNC_GOTO_ERROR(H5E_SYM, H5E_SYSERRSTR, NULL,
                            "group link create request body size exceeded allocated buffer size");

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Group create request body:\n%s\n\n", create_request_body);
#endif
    } /* end if */

    /* Setup the host header */
    host_header_len = strlen(parent->domain->u.file.filepath_name) + strlen(host_string) + 1;
    if (NULL == (host_header = (char *)RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTALLOC, NULL, "can't allocate space for request Host header");

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, parent->domain->u.file.filepath_name,
                                                           host_header_len - strlen(host_string) - 1));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    /* Instruct cURL that we are sending JSON */
    curl_headers = curl_slist_append(curl_headers, "Content-Type: application/json");

    /* Redirect cURL from the base URL to "/groups" to create the group */
    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups", base_URL)) < 0)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_SYSERRSTR, NULL, "snprintf error");

    if (url_len >= URL_MAX_LENGTH)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_SYSERRSTR, NULL, "group create URL size exceeded maximum URL size");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Group create request URL: %s\n\n", request_url);
#endif

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_USERNAME, new_group->domain->u.file.server_info.username))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, NULL, "can't set cURL username: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_PASSWORD, new_group->domain->u.file.server_info.password))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, NULL, "can't set cURL password: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, NULL, "can't set cURL HTTP headers: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POST, 1))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, NULL, "can't set up cURL to make HTTP POST request: %s",
                        curl_err_buf);
    if (CURLE_OK !=
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, create_request_body ? create_request_body : ""))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, NULL, "can't set cURL POST data: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)create_request_body_len))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, NULL, "can't set cURL POST data size: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, NULL, "can't set cURL request URL: %s", curl_err_buf);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Creating group\n\n");

    printf("   /***********************************\\\n");
    printf("-> | Making POST request to the server |\n");
    printf("   \\***********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_SYM, H5E_CANTCREATE, NULL);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Created group\n\n");
#endif

    /* Store the newly-created group's URI */
    if (RV_parse_response(response_buffer.buffer, NULL, new_group->URI, RV_copy_object_URI_callback) < 0)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTCREATE, NULL, "can't parse new group's URI");

    if (rv_hash_table_insert(RV_type_info_array_g[H5I_GROUP]->table, (char *)new_group->URI,
                             (char *)new_group) == 0)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTALLOC, NULL, "Failed to add group to type info array");

    ret_value = (void *)new_group;

done:
#ifdef RV_CONNECTOR_DEBUG
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
            FUNC_DONE_ERROR(H5E_SYM, H5E_CANTCLOSEOBJ, NULL, "can't close group");

    if (base64_plist_buffer)
        RV_free(base64_plist_buffer);
    if (binary_plist_buffer)
        RV_free(binary_plist_buffer);

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    if (escaped_group_name)
        RV_free(escaped_group_name);

    PRINT_ERROR_STACK;

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
void *
RV_group_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t gapl_id, hid_t dxpl_id,
              void **req)
{
    RV_object_t *parent       = (RV_object_t *)obj;
    RV_object_t *group        = NULL;
    loc_info     loc_info_out = {0};
    htri_t       search_ret;
    void        *ret_value = NULL;
    // char        *base64_binary_gcpl = NULL;
    void      *binary_gcpl      = NULL;
    size_t    *binary_gcpl_size = 0;
    size_t     path_size        = 0;
    size_t     path_len         = 0;
    H5I_type_t obj_type         = H5I_UNINIT;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received group open call with following parameters:\n");
    printf("     - loc_id object's URI: %s\n", parent->URI);
    printf("     - loc_id object's type: %s\n", object_type_to_string(parent->obj_type));
    printf("     - loc_id object's domain path: %s\n", parent->domain->u.file.filepath_name);
    printf("     - Path to group: %s\n", name ? name : "(null)");
    printf("     - Default GAPL? %s\n\n", (H5P_GROUP_ACCESS_DEFAULT == gapl_id) ? "yes" : "no");
#endif

    if (H5I_FILE != parent->obj_type && H5I_GROUP != parent->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object not a file or group");

    if (!parent->handle_path)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_BADVALUE, NULL, "parent object has NULL path");

    if (gapl_id == H5I_INVALID_HID)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "invalid GAPL");

    /* Allocate and setup internal Group struct */
    if (NULL == (group = (RV_object_t *)RV_malloc(sizeof(*group))))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTALLOC, NULL, "can't allocate space for group object");

    group->URI[0]          = '\0';
    group->obj_type        = H5I_GROUP;
    group->u.group.gapl_id = FAIL;
    group->u.group.gcpl_id = FAIL;

    /* Copy information about file the group is in */
    group->domain = parent->domain;
    parent->domain->u.file.ref_count++;

    group->handle_path = NULL;

    if (RV_set_object_handle_path(name, parent->handle_path, &group->handle_path) < 0)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_PATH, NULL, "can't set up object path");

    /* Locate group and set domain */

    loc_info_out.URI         = group->URI;
    loc_info_out.domain      = group->domain;
    loc_info_out.GCPL_base64 = NULL;

    search_ret = RV_find_object_by_path(parent, name, &obj_type, RV_copy_object_loc_info_callback,
                                        &group->domain->u.file.server_info, &loc_info_out);
    if (!search_ret || search_ret < 0)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_PATH, NULL, "can't locate group by path");

    group->domain = loc_info_out.domain;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Found group by given path\n\n");
#endif

    /* Decode creation properties, if server supports them and file has them */
    if (SERVER_VERSION_MATCHES_OR_EXCEEDS(parent->domain->u.file.server_info.version, 0, 8, 0) &&
        loc_info_out.GCPL_base64) {
        if (RV_base64_decode(loc_info_out.GCPL_base64, strlen(loc_info_out.GCPL_base64),
                             (char **)&binary_gcpl, binary_gcpl_size) < 0)
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTDECODE, NULL, "can't decode gcpl from base64");

        /* Set up a GCPL for the group, so that API calls like H5Gget_create_plist() will work */
        if (0 > (group->u.group.gcpl_id = H5Pdecode(binary_gcpl)))
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTDECODE, NULL,
                            "can't decode creation property list from binary");
    }
    else {
        /* Server versions before 0.8.0 do not store GCPL; return default */
        if ((group->u.group.gcpl_id = H5Pcreate(H5P_GROUP_CREATE)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "can't create GCPL for group");
    }

    /* Copy the GAPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * group access property list functions will function correctly
     */
    if (H5P_GROUP_ACCESS_DEFAULT != gapl_id) {
        if ((group->u.group.gapl_id = H5Pcopy(gapl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy GAPL");
    } /* end if */
    else
        group->u.group.gapl_id = H5P_GROUP_ACCESS_DEFAULT;

    if (rv_hash_table_insert(RV_type_info_array_g[H5I_GROUP]->table, (char *)group->URI, (char *)group) == 0)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTALLOC, NULL, "Failed to add group to type info array");

    ret_value = (void *)group;

done:
#ifdef RV_CONNECTOR_DEBUG
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
            FUNC_DONE_ERROR(H5E_SYM, H5E_CANTCLOSEOBJ, NULL, "can't close group");

    PRINT_ERROR_STACK;

    RV_free(loc_info_out.GCPL_base64);
    loc_info_out.GCPL_base64 = NULL;
    // RV_free(base64_binary_gcpl);
    RV_free(binary_gcpl);

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
herr_t
RV_group_get(void *obj, H5VL_group_get_args_t *args, hid_t dxpl_id, void **req)
{
    RV_object_t *loc_obj         = (RV_object_t *)obj;
    size_t       host_header_len = 0;
    char        *host_header     = NULL;
    char         request_url[URL_MAX_LENGTH];
    int          url_len   = 0;
    const char  *base_URL  = NULL;
    herr_t       ret_value = SUCCEED;

    loc_info loc_info_out;
    memset(&loc_info_out, 0, sizeof(loc_info));

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received group get call with following parameters:\n");
    printf("     - Group get call type: %s\n\n", group_get_type_to_string(args->op_type));
#endif

    if (H5I_FILE != loc_obj->obj_type && H5I_GROUP != loc_obj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a group");
    if ((base_URL = loc_obj->domain->u.file.server_info.base_URL) == NULL)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "parent object does not have valid server URL");

    switch (args->op_type) {
        /* H5Gget_create_plist */
        case H5VL_GROUP_GET_GCPL: {
            hid_t *ret_id = &args->args.get_gcpl.gcpl_id;

            if ((*ret_id = H5Pcopy(loc_obj->u.group.gcpl_id)) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, FAIL, "can't get group's GCPL");

            break;
        } /* H5VL_GROUP_GET_GCPL */

        /* H5Gget_info */
        case H5VL_GROUP_GET_INFO: {
            H5VL_loc_params_t *loc_params = &args->args.get_info.loc_params;
            H5G_info_t        *group_info = args->args.get_info.ginfo;

            switch (loc_params->type) {
                /* H5Gget_info */
                case H5VL_OBJECT_BY_SELF: {
#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Gget_info(): Group's URI: %s\n", loc_obj->URI);
                    printf("-> H5Gget_info(): Group's object type: %s\n\n",
                           object_type_to_string(loc_obj->obj_type));
#endif

                    /* Redirect cURL from the base URL to "/groups/<id>" to get information about the group */
                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s", base_URL,
                                            loc_obj->URI)) < 0)
                        FUNC_GOTO_ERROR(H5E_SYM, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_SYM, H5E_SYSERRSTR, FAIL,
                                        "H5Gget_info request URL size exceeded maximum URL size");

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Gget_info_by_name */
                case H5VL_OBJECT_BY_NAME: {

                    if (H5I_INVALID_HID == loc_params->loc_data.loc_by_name.lapl_id)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid LAPL");

                    H5I_type_t obj_type = H5I_GROUP;
                    htri_t     search_ret;
                    char       temp_URI[URI_MAX_LENGTH];

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Gget_info_by_name(): loc_id object's URI: %s\n", loc_obj->URI);
                    printf("-> H5Gget_info_by_name(): loc_id object's type: %s\n",
                           object_type_to_string(loc_obj->obj_type));
                    printf("-> H5Gget_info_by_name(): Path to group's parent object: %s\n\n",
                           loc_params->loc_data.loc_by_name.name);
#endif

                    loc_info_out.URI         = temp_URI;
                    loc_info_out.domain      = loc_obj->domain;
                    loc_info_out.GCPL_base64 = NULL;

                    search_ret = RV_find_object_by_path(loc_obj, loc_params->loc_data.loc_by_name.name,
                                                        &obj_type, RV_copy_object_loc_info_callback,
                                                        &loc_obj->domain->u.file.server_info, &loc_info_out);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_SYM, H5E_PATH, FAIL, "can't locate group");

                    loc_obj->domain = loc_info_out.domain;

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Gget_info_by_name(): found group's parent object by given path\n");
                    printf("-> H5Gget_info_by_name(): group's parent object URI: %s\n", temp_URI);
                    printf("-> H5Gget_info_by_name(): group's parent object type: %s\n\n",
                           object_type_to_string(obj_type));
#endif

                    /* Redirect cURL from the base URL to "/groups/<id>" to get information about the group */
                    if ((url_len =
                             snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s", base_URL, temp_URI)) < 0)
                        FUNC_GOTO_ERROR(H5E_SYM, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_SYM, H5E_SYSERRSTR, FAIL,
                                        "H5Gget_info_by_name request URL size exceeded maximum URL size");

                    if (loc_info_out.GCPL_base64) {
                        RV_free(loc_info_out.GCPL_base64);
                        loc_info_out.GCPL_base64 = NULL;
                    }

                    break;
                } /* H5VL_OBJECT_BY_NAME */

                /* H5Gget_info_by_idx */
                case H5VL_OBJECT_BY_IDX: {
                    FUNC_GOTO_ERROR(H5E_SYM, H5E_UNSUPPORTED, FAIL, "H5Gget_info_by_idx is unsupported");
                    break;
                } /* H5VL_OBJECT_BY_IDX */

                case H5VL_OBJECT_BY_TOKEN:
                default:
                    FUNC_GOTO_ERROR(H5E_SYM, H5E_BADVALUE, FAIL, "invalid loc_params type");
            } /* end switch */

            /* Setup the host header */
            host_header_len = strlen(loc_obj->domain->u.file.filepath_name) + strlen(host_string) + 1;
            if (NULL == (host_header = (char *)RV_malloc(host_header_len)))
                FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header");

            strcpy(host_header, host_string);

            curl_headers =
                curl_slist_append(curl_headers, strncat(host_header, loc_obj->domain->u.file.filepath_name,
                                                        host_header_len - strlen(host_string) - 1));

            /* Disable use of Expect: 100 Continue HTTP response */
            curl_headers = curl_slist_append(curl_headers, "Expect:");

            if (CURLE_OK !=
                curl_easy_setopt(curl, CURLOPT_USERNAME, loc_obj->domain->u.file.server_info.username))
                FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, FAIL, "can't set cURL username: %s", curl_err_buf);
            if (CURLE_OK !=
                curl_easy_setopt(curl, CURLOPT_PASSWORD, loc_obj->domain->u.file.server_info.password))
                FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, FAIL, "can't set cURL password: %s", curl_err_buf);
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
                FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf);
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
                FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP GET request: %s",
                                curl_err_buf);
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
                FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf);

#ifdef RV_CONNECTOR_DEBUG
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
                FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTGET, FAIL, "can't retrieve group information");

            break;
        } /* H5VL_GROUP_GET_INFO */

        default:
            FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTGET, FAIL, "can't get this type of information from group");
            ;
    } /* end switch */

done:
#ifdef RV_CONNECTOR_DEBUG
    printf("-> Group get response buffer:\n%s\n\n", response_buffer.buffer);
#endif

    if (loc_info_out.GCPL_base64) {
        RV_free(loc_info_out.GCPL_base64);
        loc_info_out.GCPL_base64 = NULL;
    }

    if (host_header)
        RV_free(host_header);

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    PRINT_ERROR_STACK;

    return ret_value;
} /* end RV_group_get() */

/*-------------------------------------------------------------------------
 * Function:    RV_group_close
 *
 * Purpose:     Closes an HDF5 group by freeing the memory allocated for
 *              its internal memory struct object. There is no interaction
 *              with the server, whose state is unchanged.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
herr_t
RV_group_close(void *grp, hid_t dxpl_id, void **req)
{
    RV_object_t *_grp      = (RV_object_t *)grp;
    herr_t       ret_value = SUCCEED;

    if (!_grp)
        FUNC_GOTO_DONE(SUCCEED);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received group close call with following parameters:\n");
    printf("     - Group's URI: %s\n", _grp->URI);
    printf("     - Group's object type: %s\n", object_type_to_string(_grp->obj_type));
    if (_grp->domain && _grp->domain->u.file.filepath_name)
        printf("     - Group's domain path: %s\n", _grp->domain->u.file.filepath_name);
    printf("\n");
#endif

    if (H5I_GROUP != _grp->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a group");

    if (_grp->u.group.gapl_id >= 0) {
        if (_grp->u.group.gapl_id != H5P_GROUP_ACCESS_DEFAULT && H5Pclose(_grp->u.group.gapl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close GAPL");
    } /* end if */
    if (_grp->u.group.gcpl_id >= 0) {
        if (_grp->u.group.gcpl_id != H5P_GROUP_CREATE_DEFAULT && H5Pclose(_grp->u.group.gcpl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close GCPL");
    } /* end if */

    if (RV_type_info_array_g[H5I_GROUP])
        rv_hash_table_remove(RV_type_info_array_g[H5I_GROUP]->table, (char *)_grp->URI);

    if (RV_file_close(_grp->domain, H5P_DEFAULT, NULL) < 0) {
        FUNC_DONE_ERROR(H5E_FILE, H5E_CANTCLOSEFILE, FAIL, "can't close file");
    }

    RV_free(_grp->handle_path);

    RV_free(_grp);
    _grp = NULL;

done:
    PRINT_ERROR_STACK;

    return ret_value;
} /* end RV_group_close() */

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
RV_get_group_info_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out)
{
    H5G_info_t *group_info = (H5G_info_t *)callback_data_out;
    yajl_val    parse_tree = NULL, key_obj;
    herr_t      ret_value  = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Retrieving group's info from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL");
    if (!group_info)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "group info pointer was NULL");

    memset(group_info, 0, sizeof(*group_info));

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_PARSEERROR, FAIL, "parsing JSON failed");

    /* Retrieve the group's link count */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, group_link_count_keys, yajl_t_number)))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTGET, FAIL, "retrieval of group link count failed");

    if (!YAJL_IS_INTEGER(key_obj))
        FUNC_GOTO_ERROR(H5E_SYM, H5E_BADVALUE, FAIL, "returned group link count is not an integer");

    if (YAJL_GET_INTEGER(key_obj) < 0)
        FUNC_GOTO_ERROR(H5E_SYM, H5E_BADVALUE, FAIL, "group link count was negative");

    group_info->nlinks = (hsize_t)YAJL_GET_INTEGER(key_obj);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Group had %llu links in it\n\n", group_info->nlinks);
#endif

done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_get_group_info_callback() */
