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
 * Implementations of the links callbacks for the HDF5 REST VOL connector.
 */

#include "rest_vol_link.h"

/* Version of external link format -- borrowed from H5Lexternal.c */
#define H5L_EXT_VERSION 0

/* Valid flags for external links -- borrowed from H5Lexternal.c */
#define H5L_EXT_FLAGS_ALL 0

/* Set of callbacks for RV_parse_response() */
static herr_t RV_get_link_name_by_idx_callback(char *HTTP_response, const void *callback_data_in,
                                               void *callback_data_out);
static herr_t RV_link_iter_callback(char *HTTP_response, const void *callback_data_in,
                                    void *callback_data_out);

/* Helper functions to work with a table of links for link iteration */
static herr_t RV_build_link_table(char *HTTP_response, hbool_t is_recursive,
                                  int (*sort_func)(const void *, const void *), link_table_entry **link_table,
                                  size_t *num_entries, rv_hash_table_t *visited_link_table,
                                  RV_object_t *loc_obj);
static void   RV_free_link_table(link_table_entry *link_table, size_t num_entries);
static herr_t RV_traverse_link_table(link_table_entry *link_table, size_t num_entries,
                                     const iter_data *iter_data, const char *cur_link_rel_path);

/* Qsort callbacks to sort links by name or creation order */
static int H5_rest_cmp_links_by_creation_order_inc(const void *link1, const void *link2);
static int H5_rest_cmp_links_by_creation_order_dec(const void *link1, const void *link2);
static int H5_rest_cmp_links_by_name_inc(const void *link1, const void *link2);
static int H5_rest_cmp_links_by_name_dec(const void *link1, const void *link2);

/* JSON keys to retrieve the value of a soft or external link */
const char *link_path_keys[]    = {"link", "h5path", (const char *)0};
const char *link_path_keys2[]   = {"h5path", (const char *)0};
const char *link_domain_keys[]  = {"link", "h5domain", (const char *)0};
const char *link_domain_keys2[] = {"h5domain", (const char *)0};

/* JSON keys to retrieve the collection that a hard link belongs to
 * (the type of object it points to), "groups", "datasets" or "datatypes"
 */
const char *link_collection_keys[] = {"link", "collection", (const char *)0};

/*
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
herr_t
RV_link_create(H5VL_link_create_args_t *args, void *obj, const H5VL_loc_params_t *loc_params, hid_t lcpl_id,
               hid_t lapl_id, hid_t dxpl_id, void **req)
{
    H5VL_loc_params_t *hard_link_target_obj_loc_params = NULL;
    RV_object_t       *new_link_loc_obj                = (RV_object_t *)obj;
    upload_info        uinfo;
    size_t             create_request_nalloc = 0;
    size_t             escaped_link_size     = 0;
    void              *hard_link_target_obj;
    char              *create_request_body = NULL;
    char               request_endpoint[URL_MAX_LENGTH];
    char              *url_encoded_link_name   = NULL;
    char              *escaped_link_name       = NULL;
    int                create_request_body_len = 0;
    int                url_len                 = 0;
    long               http_response;
    herr_t             ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received link create call with following parameters:\n");
    printf("     - Link Name: %s\n", loc_params->loc_data.loc_by_name.name);
    printf("     - Link Type: %s\n", link_create_type_to_string(args->op_type));
    if (new_link_loc_obj) {
        printf("     - Link loc_obj's URI: %s\n", new_link_loc_obj->URI);
        printf("     - Link loc_obj's type: %s\n", object_type_to_string(new_link_loc_obj->obj_type));
        printf("     - Link loc_obj's domain path: %s\n", new_link_loc_obj->domain->u.file.filepath_name);
    } /* end if */
    printf("     - Default LCPL? %s\n", (H5P_LINK_CREATE_DEFAULT == lcpl_id) ? "yes" : "no");
    printf("     - Default LAPL? %s\n\n", (H5P_LINK_ACCESS_DEFAULT == lapl_id) ? "yes" : "no");
#endif
    if (lcpl_id == H5I_INVALID_HID)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid LCPL");

    if (loc_params->type == H5VL_OBJECT_BY_NAME &&
        H5I_INVALID_HID == loc_params->loc_data.loc_by_name.lapl_id)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid LAPL");

    if (loc_params->type == H5VL_OBJECT_BY_IDX && H5I_INVALID_HID == loc_params->loc_data.loc_by_idx.lapl_id)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid LAPL");

    /* Since the usage of the H5L_SAME_LOC macro for hard link creation may cause new_link_loc_obj to
     * be NULL, do some special-case handling for the Hard Link creation case
     */
    if (H5VL_LINK_CREATE_HARD == args->op_type) {
        /* Pre-fetch the target object's relevant information in the case of hard link creation */
        hard_link_target_obj = args->args.hard.curr_obj ? args->args.hard.curr_obj : new_link_loc_obj;
        hard_link_target_obj_loc_params = &args->args.hard.curr_loc_params;

        /* If link_loc_new_obj was NULL, H5L_SAME_LOC was specified as the new link's loc_id.
         * In this case, we use the target object's location as the new link's location.
         */
        if (!new_link_loc_obj)
            new_link_loc_obj = (RV_object_t *)hard_link_target_obj;
    } /* end if */

    if (!new_link_loc_obj)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "link location object is NULL");

    /* Validate loc_id and check for write access on the file */
    if (H5I_FILE != new_link_loc_obj->obj_type && H5I_GROUP != new_link_loc_obj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "link location object not a file or group");
    if (!loc_params->loc_data.loc_by_name.name)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "link name data was NULL");

    if (!(new_link_loc_obj->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "no write intent on file");

    /* If link name will be sent in request body, JSON escape it */
    if (SERVER_VERSION_SUPPORTS_LONG_NAMES(new_link_loc_obj->domain->u.file.server_info.version) &&
        loc_params->loc_data.loc_by_name.name) {
        if (RV_JSON_escape_string(loc_params->loc_data.loc_by_name.name, escaped_link_name,
                                  &escaped_link_size) < 0)
            FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "can't get size of JSON escaped link name");

        if ((escaped_link_name = RV_malloc(escaped_link_size)) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate space for escaped link name");

        if (RV_JSON_escape_string(loc_params->loc_data.loc_by_name.name, escaped_link_name,
                                  &escaped_link_size) < 0)
            FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "can't JSON escape link name");
    }

    switch (args->op_type) {
        /* H5Lcreate_hard */
        case H5VL_LINK_CREATE_HARD: {
            htri_t search_ret;
            char   temp_URI[URI_MAX_LENGTH];
            char  *target_URI;

            /* Since the special-case handling above for hard link creation should have already fetched the
             * target object for the hard link, proceed forward.
             */

            /* Check to make sure that a hard link is being created in the same file as
             * the target object
             */
            if (strcmp(new_link_loc_obj->domain->u.file.filepath_name,
                       ((RV_object_t *)hard_link_target_obj)->domain->u.file.filepath_name))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTCREATE, FAIL,
                                "can't create soft or hard link to object outside of the current file");

            switch (hard_link_target_obj_loc_params->type) {
                /* H5Olink */
                case H5VL_OBJECT_BY_SELF: {
                    target_URI = ((RV_object_t *)hard_link_target_obj)->URI;
                    break;
                } /* H5VL_OBJECT_BY_SELF */

                case H5VL_OBJECT_BY_NAME: {

                    H5I_type_t obj_type = H5I_UNINIT;

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Locating hard link's target object\n\n");
#endif

                    search_ret =
                        RV_find_object_by_path((RV_object_t *)hard_link_target_obj,
                                               hard_link_target_obj_loc_params->loc_data.loc_by_name.name,
                                               &obj_type, RV_copy_object_URI_callback, NULL, temp_URI);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_PATH, FAIL, "can't locate link target object");

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Found hard link's target object by given path\n\n");
#endif

                    target_URI = temp_URI;

                    break;
                } /* H5VL_OBJECT_BY_NAME */

                case H5VL_OBJECT_BY_IDX:
                case H5VL_OBJECT_BY_TOKEN:
                default:
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "invalid loc_params type");
            } /* end switch */

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Hard link target object's URI: %s\n\n", target_URI);
#endif

            {
                const char *const fmt_string_no_title = "{\"id\": \"%s\"}";
                const char *const fmt_string_title    = "{\"links\": {\"%s\": {\"id\": \"%s\"}}}";

                /* Form the request body to create the Link */
                if (SERVER_VERSION_SUPPORTS_LONG_NAMES(
                        new_link_loc_obj->domain->u.file.server_info.version) &&
                    loc_params->loc_data.loc_by_name.name) {
                    /* Include escaped link name in body */
                    create_request_nalloc =
                        (strlen(fmt_string_title) - 4) + strlen(target_URI) + strlen(escaped_link_name) + 1;

                    if (NULL == (create_request_body = (char *)RV_malloc(create_request_nalloc)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL,
                                        "can't allocate space for link create request body");

                    if ((create_request_body_len =
                             snprintf(create_request_body, create_request_nalloc, fmt_string_title,
                                      escaped_link_name, target_URI)) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if ((size_t)create_request_body_len >= create_request_nalloc)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                        "link create request body size buffer size");
                }
                else {
                    /* Body only contains target id */
                    create_request_nalloc = (strlen(fmt_string_no_title) - 2) + strlen(target_URI) + 1;

                    if (NULL == (create_request_body = (char *)RV_malloc(create_request_nalloc)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL,
                                        "can't allocate space for link create request body");

                    if ((create_request_body_len = snprintf(create_request_body, create_request_nalloc,
                                                            fmt_string_no_title, target_URI)) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if ((size_t)create_request_body_len >= create_request_nalloc)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                        "link create request body size exceeded allocated buffer size");
                }
            }

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Hard link create request JSON:\n%s\n\n", create_request_body);
#endif

            break;
        } /* H5VL_LINK_CREATE_HARD */

        /* H5Lcreate_soft */
        case H5VL_LINK_CREATE_SOFT: {

            const char *link_target = args->args.soft.target;

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Soft link target: %s\n\n", link_target);
#endif

            {
                const char *const fmt_string_no_title = "{\"h5path\": \"%s\"}";
                const char *const fmt_string_title    = "{\"links\": {\"%s\": {\"h5path\": \"%s\"}}}";

                /* Form the request body to create the Link */
                if (SERVER_VERSION_SUPPORTS_LONG_NAMES(
                        new_link_loc_obj->domain->u.file.server_info.version) &&
                    loc_params->loc_data.loc_by_name.name) {
                    /* Body contains link title */
                    create_request_nalloc =
                        (strlen(fmt_string_title) - 4) + strlen(link_target) + strlen(escaped_link_name) + 1;

                    if (NULL == (create_request_body = (char *)RV_malloc(create_request_nalloc)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL,
                                        "can't allocate space for link create request body");

                    if ((create_request_body_len =
                             snprintf(create_request_body, create_request_nalloc, fmt_string_title,
                                      escaped_link_name, link_target)) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if ((size_t)create_request_body_len >= create_request_nalloc)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                        "link create request body size exceeded allocated buffer size");
                }
                else {
                    /* Body only contains h5path */
                    create_request_nalloc = (strlen(fmt_string_no_title) - 2) + strlen(link_target) + 1;

                    if (NULL == (create_request_body = (char *)RV_malloc(create_request_nalloc)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL,
                                        "can't allocate space for link create request body");

                    if ((create_request_body_len = snprintf(create_request_body, create_request_nalloc,
                                                            fmt_string_no_title, link_target)) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if ((size_t)create_request_body_len >= create_request_nalloc)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                        "link create request body size exceeded allocated buffer size");
                }
            }

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Soft link create request JSON:\n%s\n\n", create_request_body);
#endif

            break;
        } /* H5VL_LINK_CREATE_SOFT */

        /* H5Lcreate_external and H5Lcreate_ud */
        case H5VL_LINK_CREATE_UD: {
            H5L_type_t  link_type      = args->args.ud.type;
            const void *udata_buf      = args->args.ud.buf;
            size_t      udata_buf_size = args->args.ud.buf_size;
            const char *file_path, *link_target;
            unsigned    elink_flags;

            if (H5L_TYPE_EXTERNAL != link_type)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_UNSUPPORTED, FAIL, "unsupported link type");

            if (H5Lunpack_elink_val(udata_buf, udata_buf_size, &elink_flags, &file_path, &link_target) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't unpack contents of external link buffer");

#ifdef RV_CONNECTOR_DEBUG
            printf("-> External link's domain path: %s\n", file_path);
            printf("-> External link's link target: %s\n\n", link_target);
#endif

            {
                const char *const fmt_string_no_title = "{\"h5domain\": \"%s\", \"h5path\": \"%s\"}";
                const char *const fmt_string_title =
                    "{\"links\": {\"%s\": {\"h5domain\": \"%s\", \"h5path\": \"%s\"}}}";

                /* Form the request body to create the Link */
                if (SERVER_VERSION_SUPPORTS_LONG_NAMES(
                        new_link_loc_obj->domain->u.file.server_info.version) &&
                    loc_params->loc_data.loc_by_name.name) {
                    /* Body contains link name */
                    create_request_nalloc = (strlen(fmt_string_title) - 6) + strlen(file_path) +
                                            strlen(link_target) + strlen(escaped_link_name) + 1;

                    if (NULL == (create_request_body = (char *)RV_malloc(create_request_nalloc)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL,
                                        "can't allocate space for link create request body");

                    if ((create_request_body_len =
                             snprintf(create_request_body, create_request_nalloc, fmt_string_title,
                                      escaped_link_name, file_path, link_target)) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if ((size_t)create_request_body_len >= create_request_nalloc)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                        "link create request body size exceeded allocated buffer size");
                }
                else {
                    /* Body does not contain link name */
                    create_request_nalloc =
                        (strlen(fmt_string_no_title) - 4) + strlen(file_path) + strlen(link_target) + 1;
                    if (NULL == (create_request_body = (char *)RV_malloc(create_request_nalloc)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL,
                                        "can't allocate space for link create request body");

                    if ((create_request_body_len = snprintf(create_request_body, create_request_nalloc,
                                                            fmt_string_no_title, file_path, link_target)) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if ((size_t)create_request_body_len >= create_request_nalloc)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                        "link create request body size exceeded allocated buffer size");
                }
            }

#ifdef RV_CONNECTOR_DEBUG
            printf("-> External link create request JSON:\n%s\n\n", create_request_body);
#endif

            break;
        } /* H5VL_LINK_CREATE_UD */

        default:
            FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "Invalid link create type");
    } /* end switch */

    if (SERVER_VERSION_SUPPORTS_LONG_NAMES(new_link_loc_obj->domain->u.file.server_info.version) &&
        loc_params->loc_data.loc_by_name.name) {
        /* Redirect cURL from the base URL to "/groups/<id>/links" to create the link */
        if ((url_len =
                 snprintf(request_endpoint, URL_MAX_LENGTH, "/groups/%s/links", new_link_loc_obj->URI)) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");
    }
    else {
        /* URL-encode the name of the link to ensure that the resulting URL for the link
         * creation operation doesn't contain any illegal characters
         */
        if (NULL == (url_encoded_link_name =
                         curl_easy_escape(curl, H5_rest_basename(loc_params->loc_data.loc_by_name.name), 0)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode link name");

        /* Redirect cURL from the base URL to "/groups/<id>/links/<name>" to create the link */
        if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/groups/%s/links/%s",
                                new_link_loc_obj->URI, url_encoded_link_name)) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");
    }

    if (url_len >= URL_MAX_LENGTH)
        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "link create URL size exceeded maximum URL size");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Link create request URL: %s\n\n", request_endpoint);
#endif

    uinfo.buffer      = create_request_body;
    uinfo.buffer_size = (size_t)create_request_body_len;
    uinfo.bytes_sent  = 0;

    http_response = RV_curl_put(curl, &new_link_loc_obj->domain->u.file.server_info, request_endpoint,
                                new_link_loc_obj->domain->u.file.filepath_name, &uinfo, CONTENT_TYPE_JSON);

    if (!HTTP_SUCCESS(http_response))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTCREATE, FAIL, "can't create link");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Created link\n\n");
#endif

done:
#ifdef RV_CONNECTOR_DEBUG
    printf("-> Link create response buffer:\n%s\n\n", response_buffer.buffer);
#endif

    if (create_request_body)
        RV_free(create_request_body);
    if (url_encoded_link_name)
        curl_free(url_encoded_link_name);
    if (escaped_link_name)
        RV_free(escaped_link_name);

    PRINT_ERROR_STACK;

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
herr_t
RV_link_copy(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj,
             const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req)
{
    herr_t ret_value = SUCCEED;

    FUNC_GOTO_ERROR(H5E_LINK, H5E_UNSUPPORTED, FAIL, "H5Lcopy is unsupported");

done:
    PRINT_ERROR_STACK;

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
herr_t
RV_link_move(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj,
             const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req)
{
    herr_t ret_value = SUCCEED;

    FUNC_GOTO_ERROR(H5E_LINK, H5E_UNSUPPORTED, FAIL, "H5Lmove is unsupported");

done:
    PRINT_ERROR_STACK;

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
herr_t
RV_link_get(void *obj, const H5VL_loc_params_t *loc_params, H5VL_link_get_args_t *args, hid_t dxpl_id,
            void **req)
{
    RV_object_t *loc_obj = (RV_object_t *)obj;
    hbool_t      empty_dirname;
    char        *link_dir_name         = NULL;
    char        *url_encoded_link_name = NULL;
    char         temp_URI[URI_MAX_LENGTH];
    char         request_endpoint[URL_MAX_LENGTH];
    int          url_len   = 0;
    herr_t       ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received link get call with following parameters:\n");
    printf("     - Link get call type: %s\n", link_get_type_to_string(args->op_type));
    printf("     - Link loc_obj's URI: %s\n", loc_obj->URI);
    printf("     - Link loc_obj's object type: %s\n", object_type_to_string(loc_obj->obj_type));
    printf("     - Link loc_obj's domain path: %s\n\n", loc_obj->domain->u.file.filepath_name);
#endif

    switch (args->op_type) {
        /* H5Lget_info */
        case H5VL_LINK_GET_INFO: {
            H5L_info2_t *link_info = args->args.get_info.linfo;

            switch (loc_params->type) {
                /* H5Lget_info */
                case H5VL_OBJECT_BY_NAME: {

                    if (H5I_INVALID_HID == loc_params->loc_data.loc_by_name.lapl_id)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid LAPL");

                    /* In case the user specified a path which contains any groups on the way to the
                     * link in question, extract out the path to the final group in the chain */
                    if (NULL == (link_dir_name = H5_rest_dirname(loc_params->loc_data.loc_by_name.name)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't get path dirname");
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
                            FUNC_GOTO_ERROR(H5E_SYM, H5E_PATH, FAIL, "can't locate parent group");

#ifdef RV_CONNECTOR_DEBUG
                        printf("-> H5Lget_info(): Found link's parent object by given path\n");
                        printf("-> H5Lget_info(): link's parent object URI: %s\n", temp_URI);
                        printf("-> H5Lget_info(): link's parent object type: %s\n\n",
                               object_type_to_string(obj_type));
#endif
                    } /* end if */

                    /* URL-encode the name of the link to ensure that the resulting URL for the get
                     * link info operation doesn't contain any illegal characters
                     */
                    if (NULL == (url_encoded_link_name = curl_easy_escape(
                                     curl, H5_rest_basename(loc_params->loc_data.loc_by_name.name), 0)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode link name");

                    if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/groups/%s/links/%s",
                                            empty_dirname ? loc_obj->URI : temp_URI, url_encoded_link_name)) <
                        0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                        "H5Lget_info request URL size exceeded maximum URL size");

                    break;
                } /* H5VL_OBJECT_BY_NAME */

                /* H5Lget_info_by_idx */
                case H5VL_OBJECT_BY_IDX: {
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_UNSUPPORTED, FAIL, "H5Lget_info_by_idx is unsupported");
                    break;
                } /* H5VL_OBJECT_BY_IDX */

                case H5VL_OBJECT_BY_SELF:
                case H5VL_OBJECT_BY_TOKEN:
                default:
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "invalid loc_params type");
            } /* end switch */

            /* Make a GET request to the server to retrieve the number of attributes attached to the object */
            if (RV_curl_get(curl, &loc_obj->domain->u.file.server_info, request_endpoint,
                            loc_obj->domain->u.file.filepath_name, CONTENT_TYPE_JSON) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't get link");

            /* Retrieve the link info */
            if (RV_parse_response(response_buffer.buffer, NULL, link_info, RV_get_link_info_callback) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't retrieve link info");

            break;
        } /* H5VL_LINK_GET_INFO */

        /* H5Lget_name_by_idx */
        case H5VL_LINK_GET_NAME: {
            link_name_by_idx_data link_name_data;
            H5I_type_t            obj_type = H5I_GROUP;
            iter_data             by_idx_data;
            htri_t                search_ret;
            char                 *link_name_buf      = args->args.get_name.name;
            size_t                link_name_buf_size = args->args.get_name.name_size;
            size_t                idx_p              = 0;
            size_t               *ret_size           = args->args.get_name.name_len;

            /*
             * NOTE: The current implementation of this function does not do any sort of caching.
             * On each call, the index of all links in the specified group is built up and sorted according
             * to the order specified. Then, the nth link's name is retrieved and everything is torn down.
             * If wanting to retrieve the name of every link in a given group, it will currently be much
             * more efficient to use H5Literate instead.
             */

            /*
             * Setup information needed for determining the order to sort the index by.
             */
            by_idx_data.is_recursive               = FALSE;
            by_idx_data.index_type                 = loc_params->loc_data.loc_by_idx.idx_type;
            by_idx_data.iter_order                 = loc_params->loc_data.loc_by_idx.order;
            by_idx_data.iter_function.link_iter_op = NULL;
            by_idx_data.op_data                    = NULL;
            by_idx_data.iter_obj_parent            = loc_obj;
            idx_p                                  = loc_params->loc_data.loc_by_idx.n;
            by_idx_data.idx_p                      = &idx_p;

            /*
             * Setup information to be passed back from link name retrieval callback
             */
            link_name_data.link_name     = link_name_buf;
            link_name_data.link_name_len = link_name_buf_size;

            /*
             * Locate group
             */
            search_ret = RV_find_object_by_path(loc_obj, loc_params->loc_data.loc_by_idx.name, &obj_type,
                                                RV_copy_object_URI_callback, NULL, temp_URI);
            if (!search_ret || search_ret < 0)
                FUNC_GOTO_ERROR(H5E_SYM, H5E_PATH, FAIL, "can't locate group");

            if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/groups/%s/links", temp_URI)) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                "H5Lget_name_by_idx request URL size exceeded maximum URL size");

            /* Make a GET request to the server to retrieve all of the links in the given group */
            if (RV_curl_get(curl, &loc_obj->domain->u.file.server_info, request_endpoint,
                            loc_obj->domain->u.file.filepath_name, CONTENT_TYPE_JSON) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't get link");

            if (RV_parse_response(response_buffer.buffer, &by_idx_data, &link_name_data,
                                  RV_get_link_name_by_idx_callback) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't retrieve link name by index");

            *ret_size = (size_t)link_name_data.link_name_len;

            break;
        } /* H5VL_LINK_GET_NAME */

        /* H5Lget_val */
        case H5VL_LINK_GET_VAL: {
            void  *out_buf  = args->args.get_val.buf;
            size_t buf_size = args->args.get_val.buf_size;

            switch (loc_params->type) {
                /* H5Lget_val */
                case H5VL_OBJECT_BY_NAME: {

                    if (H5I_INVALID_HID == loc_params->loc_data.loc_by_name.lapl_id)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid LAPL");

                    /* In case the user specified a path which contains any groups on the way to the
                     * link in question, extract out the path to the final group in the chain */
                    if (NULL == (link_dir_name = H5_rest_dirname(loc_params->loc_data.loc_by_name.name)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't get path dirname");
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
                            FUNC_GOTO_ERROR(H5E_SYM, H5E_PATH, FAIL, "can't locate parent group");

#ifdef RV_CONNECTOR_DEBUG
                        printf("-> H5Lget_val(): Found link's parent object by given path\n");
                        printf("-> H5Lget_val(): link's parent object URI: %s\n", temp_URI);
                        printf("-> H5Lget_val(): link's parent object type: %s\n\n",
                               object_type_to_string(obj_type));
#endif
                    } /* end if */

                    /* URL-encode the name of the link to ensure that the resulting URL for the get
                     * link value operation doesn't contain any illegal characters
                     */
                    if (NULL == (url_encoded_link_name = curl_easy_escape(
                                     curl, H5_rest_basename(loc_params->loc_data.loc_by_name.name), 0)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode link name");

                    if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/groups/%s/links/%s",
                                            empty_dirname ? loc_obj->URI : temp_URI, url_encoded_link_name)) <
                        0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                        "H5Lget_val request URL size exceeded maximum URL size");

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Lget_val_by_idx */
                case H5VL_OBJECT_BY_IDX: {
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_UNSUPPORTED, FAIL, "H5Lget_val_by_idx is unsupported");
                    break;
                } /* H5VL_OBJECT_BY_IDX */

                case H5VL_OBJECT_BY_SELF:
                case H5VL_OBJECT_BY_TOKEN:
                default:
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "invalid loc_params type");
            } /* end switch */

            /* Make a GET request to the server to retrieve the number of attributes attached to the object */
            if (RV_curl_get(curl, &loc_obj->domain->u.file.server_info, request_endpoint,
                            loc_obj->domain->u.file.filepath_name, CONTENT_TYPE_JSON) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't get link");

            /* Retrieve the link value */
            get_link_val_out get_link_val_args;
            get_link_val_args.in_buf_size = &buf_size;
            get_link_val_args.buf         = out_buf;

            if (RV_parse_response(response_buffer.buffer, NULL, &get_link_val_args,
                                  RV_get_link_val_callback) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't retrieve link value");

            break;
        } /* H5VL_LINK_GET_VAL */

        default:
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't get this type of information from link");
    } /* end switch */

done:
#ifdef RV_CONNECTOR_DEBUG
    printf("-> Link get response buffer:\n%s\n\n", response_buffer.buffer);
#endif

    if (url_encoded_link_name)
        curl_free(url_encoded_link_name);
    if (link_dir_name)
        RV_free(link_dir_name);

    PRINT_ERROR_STACK;

    return ret_value;
} /* end RV_link_get() */

/*-------------------------------------------------------------------------
 * Function:    RV_link_specific
 *
 * Purpose:     Performs a connector-specific operation on an HDF5 link, such
 *              as calling the H5Ldelete routine.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
herr_t
RV_link_specific(void *obj, const H5VL_loc_params_t *loc_params, H5VL_link_specific_args_t *args,
                 hid_t dxpl_id, void **req)
{
    RV_object_t *loc_obj = (RV_object_t *)obj;
    hbool_t      empty_dirname;
    size_t       escaped_link_size      = 0;
    int          request_body_len       = 0;
    hid_t        link_iter_group_id     = H5I_INVALID_HID;
    void        *link_iter_group_object = NULL;
    char        *link_path_dirname      = NULL;
    char         temp_URI[URI_MAX_LENGTH];
    char         request_endpoint[URL_MAX_LENGTH];
    char        *url_encoded_link_name = NULL;
    char        *escaped_link_name     = NULL;
    char        *request_body          = NULL;
    int          url_len               = 0;
    long         http_response;
    herr_t       ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received link-specific call with following parameters:\n");
    printf("     - Link-specific call type: %s\n", link_specific_type_to_string(args->op_type));
    printf("     - Link loc_obj's URI: %s\n", loc_obj->URI);
    printf("     - Link loc_obj's object type: %s\n", object_type_to_string(loc_obj->obj_type));
    printf("     - Link loc_obj's domain path: %s\n\n", loc_obj->domain->u.file.filepath_name);
#endif

    if (H5I_FILE != loc_obj->obj_type && H5I_GROUP != loc_obj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "parent object not a file or group");

    switch (args->op_type) {
        /* H5Ldelete */
        case H5VL_LINK_DELETE: {
            switch (loc_params->type) {
                /* H5Ldelete */
                case H5VL_OBJECT_BY_NAME: {

                    if (H5I_INVALID_HID == loc_params->loc_data.loc_by_name.lapl_id)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid LAPL");

                    /* In case the user specified a path which contains multiple groups on the way to the
                     * link in question, extract out the path to the final group in the chain */
                    if (NULL == (link_path_dirname = H5_rest_dirname(loc_params->loc_data.loc_by_name.name)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "invalid pathname for link");
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
                            FUNC_GOTO_ERROR(H5E_LINK, H5E_PATH, FAIL, "can't locate parent group for link");
                    } /* end if */

                    /* URL-encode the link name so that the resulting URL for the link delete
                     * operation doesn't contain any illegal characters
                     */
                    if (NULL == (url_encoded_link_name = curl_easy_escape(
                                     curl, H5_rest_basename(loc_params->loc_data.loc_by_name.name), 0)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode link name");

                    /* Redirect cURL from the base URL to "/groups/<id>/links/<name>" to delete link */
                    if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/groups/%s/links/%s",
                                            empty_dirname ? loc_obj->URI : temp_URI, url_encoded_link_name)) <
                        0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                        "H5Ldelete request URL size exceeded maximum URL size");

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Ldelete_by_idx */
                case H5VL_OBJECT_BY_IDX: {
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_UNSUPPORTED, FAIL, "H5Ldelete_by_idx is unsupported");
                    break;
                } /* H5VL_OBJECT_BY_IDX */

                case H5VL_OBJECT_BY_SELF:
                case H5VL_OBJECT_BY_TOKEN:
                default:
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "invalid loc_params type");
            } /* end switch */

            http_response = RV_curl_delete(curl, &loc_obj->domain->u.file.server_info, request_endpoint,
                                           (const char *)loc_obj->domain->u.file.filepath_name);

            if (!HTTP_SUCCESS(http_response))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTREMOVE, FAIL, "can't delete link");

            break;
        } /* H5VL_LINK_DELETE */

        /* H5Lexists */
        case H5VL_LINK_EXISTS: {
            hbool_t *ret = args->args.exists.exists;

            /* In case the user specified a path which contains multiple groups on the way to the
             * link in question, extract out the path to the final group in the chain */
            if (NULL == (link_path_dirname = H5_rest_dirname(loc_params->loc_data.loc_by_name.name)))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "invalid pathname for link");
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
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_PATH, FAIL, "can't locate parent group for link");
            } /* end if */

            /* Setup cURL to make the request */
            if (SERVER_VERSION_SUPPORTS_LONG_NAMES(loc_obj->domain->u.file.server_info.version)) {
                /* Send link name in body of POST request */
                const char *fmt_string = "{\"titles\": [\"%s\"]}";
                int         bytes_printed;

                /* JSON escape link name */
                if (RV_JSON_escape_string(H5_rest_basename(loc_params->loc_data.loc_by_name.name),
                                          escaped_link_name, &escaped_link_size) < 0)
                    FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "can't get size of JSON escaped link name");

                if ((escaped_link_name = RV_malloc(escaped_link_size)) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL,
                                    "can't allocate space for escaped link name");

                if (RV_JSON_escape_string(H5_rest_basename(loc_params->loc_data.loc_by_name.name),
                                          escaped_link_name, &escaped_link_size) < 0)
                    FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "can't JSON escape link name");

                request_body_len = (int)(strlen(fmt_string) - 2 + strlen(escaped_link_name) + 1);

                if ((request_body = RV_malloc((size_t)request_body_len)) == NULL)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL,
                                    "can't allocate space for link query body");

                if ((bytes_printed =
                         snprintf(request_body, (size_t)request_body_len, fmt_string, escaped_link_name)) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                if (bytes_printed >= request_body_len)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                    "request body size exceeded allocated buffer size");

                if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/groups/%s/links",
                                        empty_dirname ? loc_obj->URI : temp_URI)) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

#ifdef RV_CONNECTOR_DEBUG
                printf("-> Checking for existence of link using endpoint: %s\n\n", request_endpoint);
#endif

                if ((http_response = RV_curl_post(curl, &loc_obj->domain->u.file.server_info,
                                                  request_endpoint, loc_obj->domain->u.file.filepath_name,
                                                  request_body, (size_t)bytes_printed, CONTENT_TYPE_JSON)) <
                    0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL,
                                    "internal failure while making POST request to server");
            }
            else {
                /* URL-encode the link name so that the resulting URL for the link GET
                 * operation doesn't contain any illegal characters
                 */
                if (NULL == (url_encoded_link_name = curl_easy_escape(
                                 curl, H5_rest_basename(loc_params->loc_data.loc_by_name.name), 0)))
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode link name");

                if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/groups/%s/links/%s",
                                        empty_dirname ? loc_obj->URI : temp_URI, url_encoded_link_name)) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

#ifdef RV_CONNECTOR_DEBUG
                printf("-> Checking for existence of link using endpoint: %s\n\n", request_endpoint);
#endif
                if ((http_response = RV_curl_get(curl, &loc_obj->domain->u.file.server_info, request_endpoint,
                                                 loc_obj->domain->u.file.filepath_name, CONTENT_TYPE_JSON)) <
                    0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL,
                                    "internal failure while making GET request to server");
            }

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                "H5Lexists request URL size exceeded maximum URL size");

            if (HTTP_CLIENT_ERROR(http_response) && http_response != 404 && http_response != 410)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "malformed client request: response code %zu\n",
                                http_response);

            if (HTTP_SERVER_ERROR(http_response))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "internal server failure: response code %zu\n",
                                http_response);

            *ret = HTTP_SUCCESS(http_response);

            break;
        } /* H5VL_LINK_EXISTS */

        /* H5Literate/visit (_by_name) */
        case H5VL_LINK_ITER: {
            iter_data link_iter_data;

            link_iter_data.is_recursive               = args->args.iterate.recursive;
            link_iter_data.index_type                 = args->args.iterate.idx_type;
            link_iter_data.iter_order                 = args->args.iterate.order;
            link_iter_data.idx_p                      = args->args.iterate.idx_p;
            link_iter_data.iter_function.link_iter_op = args->args.iterate.op;
            link_iter_data.op_data                    = args->args.iterate.op_data;
            link_iter_data.iter_obj_parent            = loc_obj;

            if (!link_iter_data.iter_function.link_iter_op)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_LINKITERERROR, FAIL, "no link iteration function specified");

            switch (loc_params->type) {
                /* H5Literate/H5Lvisit */
                case H5VL_OBJECT_BY_SELF: {
#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Opening group for link iteration to generate an hid_t and work around VOL "
                           "layer\n\n");
#endif

                    /* Since the VOL doesn't directly pass down the group's hid_t, explicitly open the group
                     * here so that a valid hid_t can be passed to the user's link iteration callback.
                     */
                    if (NULL == (link_iter_group_object =
                                     RV_group_open(loc_obj, loc_params, ".", H5P_DEFAULT, H5P_DEFAULT, NULL)))
                        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTOPENOBJ, FAIL, "can't open link iteration group");

                    if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/groups/%s/links",
                                            loc_obj->URI)) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                        "H5Literate/visit request URL size exceeded maximum URL size");

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Literate_by_name/H5Lvisit_by_name */
                case H5VL_OBJECT_BY_NAME: {

                    if (H5I_INVALID_HID == loc_params->loc_data.loc_by_name.lapl_id)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid LAPL");

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Opening group for link iteration to generate an hid_t and work around VOL "
                           "layer\n\n");
#endif

                    /* Since the VOL doesn't directly pass down the group's hid_t, explicitly open the group
                     * here so that a valid hid_t can be passed to the user's link iteration callback.
                     */
                    if (NULL == (link_iter_group_object =
                                     RV_group_open(loc_obj, loc_params, loc_params->loc_data.loc_by_name.name,
                                                   H5P_DEFAULT, H5P_DEFAULT, NULL)))
                        FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTOPENOBJ, FAIL, "can't open link iteration group");

                    if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/groups/%s/links",
                                            ((RV_object_t *)link_iter_group_object)->URI)) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(
                            H5E_LINK, H5E_SYSERRSTR, FAIL,
                            "H5Literate/visit_by_name request URL size exceeded maximum URL size");

                    break;
                } /* H5VL_OBJECT_BY_NAME */

                case H5VL_OBJECT_BY_IDX:
                case H5VL_OBJECT_BY_TOKEN:
                default:
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "invalid loc_params type");
            } /* end switch */

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Registering hid_t for opened group\n\n");
#endif

            /* Note: The case of handling the group ID is awkward as it is, but is made even more
             * awkward by the fact that this might be the first call to register an ID for an object
             * of type H5I_GROUP. Since the group was opened using a VOL-internal routine and was not
             * able to go through the public API call H5Gopen2(), this means that it is possible for
             * the H5G interface to be uninitialized at this point in time, which will cause the below
             * H5VLwrap_register() call to fail. Therefore, we have to make a fake call to H5Gopen2()
             * to make sure that the H5G interface is initialized. The call will of course fail, but
             * the FUNC_ENTER_API macro should ensure that the H5G interface is initialized.
             */
            H5E_BEGIN_TRY
            {
                H5Gopen2(H5I_INVALID_HID, NULL, H5P_DEFAULT);
            }
            H5E_END_TRY;

            /* Register an hid_t for the group object */
            if ((link_iter_group_id = H5VLwrap_register(link_iter_group_object, H5I_GROUP)) < 0)
                FUNC_GOTO_ERROR(H5E_ID, H5E_CANTREGISTER, FAIL,
                                "can't create ID for group to be iterated over");
            link_iter_data.iter_obj_id = link_iter_group_id;

            /* Make a GET request to the server to retrieve all of the links in the given group */
            if (RV_curl_get(curl, &loc_obj->domain->u.file.server_info, request_endpoint,
                            loc_obj->domain->u.file.filepath_name, CONTENT_TYPE_JSON) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't get link");

            if (RV_parse_response(response_buffer.buffer, &link_iter_data, NULL, RV_link_iter_callback) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't iterate over links");

            break;
        } /* H5VL_LINK_ITER */

        default:
            FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "unknown link operation");
            ;
    } /* end switch */

done:
    if (link_path_dirname)
        RV_free(link_path_dirname);

    if (link_iter_group_id >= 0)
        if (H5Gclose(link_iter_group_id) < 0)
            FUNC_DONE_ERROR(H5E_LINK, H5E_CANTCLOSEOBJ, FAIL, "can't close link iteration group");

    /* Free the escaped portion of the URL */
    if (url_encoded_link_name)
        curl_free(url_encoded_link_name);

    if (escaped_link_name)
        RV_free(escaped_link_name);
    if (request_body)
        RV_free(request_body);

    PRINT_ERROR_STACK;

    return ret_value;
} /* end RV_link_specific() */

/*-------------------------------------------------------------------------
 * Function:    RV_get_link_info_callback
 *
 * Purpose:     A callback for RV_parse_response which will search an HTTP
 *              response for information about a link, such as the link
 *              type, and copy that info into the callback_data_out
 *              parameter, which should be a H5L_info2_t *. This callback
 *              is used specifically for H5Lget_info (_by_idx). Currently
 *              only the link class, and for soft, external and
 *              user-defined links, the link value, is returned by this
 *              function. All other information in the H5L_info2_t struct is
 *              initialized to 0.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
herr_t
RV_get_link_info_callback(char *HTTP_response, const void *callback_data_in, void *callback_data_out)
{
    H5L_info2_t *link_info  = (H5L_info2_t *)callback_data_out;
    yajl_val     parse_tree = NULL, key_obj;
    char        *parsed_string;
    herr_t       ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Retrieving link's info from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL");
    if (!link_info)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "link info pointer was NULL");

    memset(link_info, 0, sizeof(H5L_info2_t));

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_PARSEERROR, FAIL, "parsing JSON failed");

    /* Retrieve the link's class */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, link_class_keys, yajl_t_string))) {
        if (NULL == (key_obj = yajl_tree_get(parse_tree, link_class_keys2, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "retrieval of object parent collection failed");
    }

    if (!YAJL_IS_STRING(key_obj))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "returned object parent collection is not a string");

    if (NULL == (parsed_string = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "object parent collection string was NULL");

    if (!strcmp(parsed_string, "H5L_TYPE_HARD"))
        link_info->type = H5L_TYPE_HARD;
    else if (!strcmp(parsed_string, "H5L_TYPE_SOFT"))
        link_info->type = H5L_TYPE_SOFT;
    else if (!strcmp(parsed_string, "H5L_TYPE_EXTERNAL"))
        link_info->type = H5L_TYPE_EXTERNAL;
    else
        FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "invalid link class");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Retrieved link's class: %s\n\n", link_class_to_string(link_info->type));
#endif

    /* If this is not a hard link, determine the value for the 'val_size' field corresponding
     * to the size of a soft, external or user-defined link's value, including the NULL terminator
     */
    if (strcmp(parsed_string, "H5L_TYPE_HARD")) {
        get_link_val_out get_link_val_args;
        get_link_val_args.in_buf_size = &link_info->u.val_size;
        get_link_val_args.buf         = NULL;

        if (RV_parse_response(HTTP_response, NULL, &get_link_val_args, RV_get_link_val_callback) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't retrieve link value size");

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Retrieved link's value size: %zu\n\n", link_info->u.val_size);
#endif
    } /* end if */
    else {
        /* TODO */
        link_info->u.token = H5O_TOKEN_UNDEF;
    }

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
 *              the link's value in an H5L_info2_t struct's 'val_size'
 *              field, and also by H5Lget_val (_by_idx) to actually
 *              retrieve the link's value.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
herr_t
RV_get_link_val_callback(char *HTTP_response, const void *callback_data_in, void *callback_data_out)
{
    yajl_val          parse_tree        = NULL, key_obj;
    get_link_val_out *get_link_val_args = (get_link_val_out *)callback_data_out;
    size_t           *in_buf_size       = get_link_val_args->in_buf_size;
    char             *link_path;
    char             *link_class;
    char             *out_buf   = get_link_val_args->buf;
    herr_t            ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Retrieving link's value from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL");
    if (!in_buf_size)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "buffer size pointer was NULL");

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_PARSEERROR, FAIL, "parsing JSON failed");

    /* Retrieve the link's class */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, link_class_keys, yajl_t_string))) {
        if (NULL == (key_obj = yajl_tree_get(parse_tree, link_class_keys2, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "retrieval of link class failed");
    }

    if (!YAJL_IS_STRING(key_obj))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "returned link class is not a string");

    if (NULL == (link_class = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "link class was NULL");

    if (!strcmp(link_class, "H5L_TYPE_HARD"))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "H5Lget_val should not be called for hard links");

    /* Retrieve the link's value */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, link_path_keys, yajl_t_string))) {
        if (NULL == (key_obj = yajl_tree_get(parse_tree, link_path_keys2, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "retrieval of link value failed");
    }

    if (!YAJL_IS_STRING(key_obj))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "returned link value is not a string");

    if (NULL == (link_path = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "link value was NULL");

    if (!strcmp(link_class, "H5L_TYPE_SOFT")) {
        if ((!*in_buf_size) || (*in_buf_size < 0)) {
            /* If the buffer size was specified as non-positive, simply set the size that
             * the buffer needs to be to contain the link value, which should just be
             * large enough to contain the link's target path
             */
            *in_buf_size = strlen(link_path) + 1;

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Returning size of soft link's value\n\n");
#endif
        } /* end if */
        else {
            if (out_buf)
                strncpy(out_buf, link_path, *in_buf_size);

            /* Ensure that the buffer is NULL-terminated */
            out_buf[*in_buf_size - 1] = '\0';

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Returning soft link's value\n\n");
#endif
        } /* end else */
    }     /* end if */
    else {
        yajl_val link_domain_obj;
        char    *link_domain;

        if (NULL == (link_domain_obj = yajl_tree_get(parse_tree, link_domain_keys, yajl_t_string))) {
            if (NULL == (link_domain_obj = yajl_tree_get(parse_tree, link_domain_keys2, yajl_t_string)))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "retrieval of external link domain failed");
        }

        if (!YAJL_IS_STRING(link_domain_obj))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "returned external link domain is not a string");

        if (NULL == (link_domain = YAJL_GET_STRING(link_domain_obj)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "link domain was NULL");

        /* Process external links; user-defined links are currently unsupported */
        if ((!*in_buf_size) || (*in_buf_size < 0)) {
            /* If the buffer size was specified as non-positive, simply set the size that
             * the buffer needs to be to contain the link value, which should contain
             * the link's flags, target file and target path in the case of external links
             */
            *in_buf_size = 1 + (strlen(link_domain) + 1) + (strlen(link_path) + 1);

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Returning size of external link's value\n\n");
#endif
        } /* end if */
        else {
            uint8_t *p = (uint8_t *)out_buf;

            if (p) {
                /* Pack an external link's version, flags, target object and target file into a
                 * single buffer for later unpacking with H5Lunpack_elink_val(). Note that the
                 * implementation for unpacking the external link buffer may change in the future
                 * and thus this implementation for packing it up will have to change as well.
                 */

                /* First pack the link version and flags into the output buffer */
                *p++ = (H5L_EXT_VERSION << 4) | H5L_EXT_FLAGS_ALL;

                /* Next copy the external link's target filename into the link value buffer */
                strncpy((char *)p, link_domain, *in_buf_size - 1);
                p += strlen(link_domain) + 1;

                /* Finally comes the external link's target path */
                strncpy((char *)p, link_path, (*in_buf_size - 1) - (strlen(link_domain) + 1));

#ifdef RV_CONNECTOR_DEBUG
                printf("-> Returning external link's value\n\n");
#endif
            } /* end if */
        }     /* end else */
    }         /* end else */

done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_get_link_val_callback() */

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
herr_t
RV_get_link_obj_type_callback(char *HTTP_response, const void *callback_data_in, void *callback_data_out)
{
    H5I_type_t *obj_type   = (H5I_type_t *)callback_data_out;
    yajl_val    parse_tree = NULL, key_obj;
    char       *parsed_string;
    herr_t      ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Retrieving object's type from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL");
    if (!obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "object type pointer was NULL");

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "parsing JSON failed");

    /* To handle the awkward case of soft and external links, which do not have the link
     * collection element, first check for the link class field and short circuit if it
     * is found not to be equal to "H5L_TYPE_HARD"
     */
    if (NULL != (key_obj = yajl_tree_get(parse_tree, link_class_keys, yajl_t_string))) {
        char *link_type;

        if (NULL == (link_type = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "link type string was NULL");

        if (strcmp(link_type, "H5L_TYPE_HARD"))
            FUNC_GOTO_DONE(SUCCEED);
    } /* end if */

    /* Retrieve the object's type */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, link_collection_keys, yajl_t_string)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTGET, FAIL, "retrieval of object parent collection failed");

    if (!YAJL_IS_STRING(key_obj))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "returned object parent collection is not a string");

    if (NULL == (parsed_string = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "object parent collection string was NULL");

    if (!strcmp(parsed_string, "groups"))
        *obj_type = H5I_GROUP;
    else if (!strcmp(parsed_string, "datasets"))
        *obj_type = H5I_DATASET;
    else if (!strcmp(parsed_string, "datatypes"))
        *obj_type = H5I_DATATYPE;
    else
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "invalid object type");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Retrieved object's type: %s\n\n", object_type_to_string(*obj_type));
#endif

done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_get_link_obj_type_callback() */

/*-------------------------------------------------------------------------
 * Function:    RV_get_link_name_by_idx_callback
 *
 * Purpose:     A callback for RV_parse_response which will search an HTTP
 *              response for all the links in a group, and do one of two
 *              things, based on the value of the buffer size given through
 *              the callback_data_in parameter, as well as whether the
 *              buffer given through the callback_data_in parameter is
 *              NULL or non-NULL.
 *
 *              If the buffer specified is NULL, the size of the name of
 *              the link specified by the given index number will be
 *              returned.
 *
 *              If the buffer specified is non-NULL and the buffer size
 *              specified is positive, the name of the link specified by
 *              the given index number will be copied into the buffer
 *              given. Up to n characters will be copied, where n is the
 *              specified size of the buffer. This function makes sure
 *              to NULL terminate the buffer given.
 *
 *              This callback is used by H5Lget_name_by_idx to do all of
 *              its necessary processing.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              November, 2018
 */
static herr_t
RV_get_link_name_by_idx_callback(char *HTTP_response, const void *callback_data_in, void *callback_data_out)
{
    link_name_by_idx_data *link_name_data = (link_name_by_idx_data *)callback_data_out;
    link_table_entry      *link_table     = NULL;
    const iter_data       *by_idx_data    = (const iter_data *)callback_data_in;
    size_t                 link_table_num_entries;
    int (*link_table_sort_func)(const void *, const void *);
    herr_t ret_value = SUCCEED;

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL");
    if (!by_idx_data)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "link index order data pointer was NULL");
    if (!by_idx_data->idx_p)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "link index number pointer was NULL");
    if (!link_name_data)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "link name data pointer was NULL");

    /*
     * Setup the appropriate sorting function
     */
    if (H5_INDEX_NAME == by_idx_data->index_type) {
        if (H5_ITER_INC == by_idx_data->iter_order || H5_ITER_NATIVE == by_idx_data->iter_order)
            link_table_sort_func = H5_rest_cmp_links_by_name_inc;
        else
            link_table_sort_func = H5_rest_cmp_links_by_name_dec;
    } /* end if */
    else {
        if (H5_ITER_INC == by_idx_data->iter_order || H5_ITER_NATIVE == by_idx_data->iter_order)
            link_table_sort_func = H5_rest_cmp_links_by_creation_order_inc;
        else
            link_table_sort_func = H5_rest_cmp_links_by_creation_order_dec;
    } /* end else */

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Building link table and sorting by %s in %s order\n\n",
           (H5_INDEX_NAME == by_idx_data->index_type) ? "link name" : "link creation order",
           (H5_ITER_INC == by_idx_data->iter_order || H5_ITER_NATIVE == by_idx_data->iter_order)
               ? "increasing"
               : "decreasing");
#endif

    if (RV_build_link_table(HTTP_response, by_idx_data->is_recursive, link_table_sort_func, &link_table,
                            &link_table_num_entries, NULL, by_idx_data->iter_obj_parent) < 0)
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTBUILDLINKTABLE, FAIL, "can't build link table");

    /* Check to make sure the index given is within bounds */
    if (*by_idx_data->idx_p >= link_table_num_entries)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "link index number larger than number of links");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Retrieving link name of link at index %PRIuHSIZE\n\n", *by_idx_data->idx_p);
#endif

    /* Retrieve the nth link name */
    {
        link_table_entry selected_link_entry = link_table[*by_idx_data->idx_p];

        /* If a buffer of the appropriate size has already been allocated, copy the link name back */
        if (link_name_data->link_name && link_name_data->link_name_len) {
            strncpy(link_name_data->link_name, link_table[*by_idx_data->idx_p].link_name,
                    link_name_data->link_name_len);
            link_name_data->link_name[link_name_data->link_name_len - 1] = '\0';

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Link name was '%s'\n\n", link_name_data->link_name);
#endif
        } /* end if */

        /* Set the link name length in case the function call is trying to find this out */
        link_name_data->link_name_len = strlen(selected_link_entry.link_name);

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Returning link name length of %" PRIuHSIZE "\n\n", link_name_data->link_name_len);
#endif
    }

done:
    if (link_table)
        RV_free_link_table(link_table, link_table_num_entries);

    return ret_value;
} /* end RV_get_link_name_by_idx_callback() */

/*-------------------------------------------------------------------------
 * Function:    RV_link_iter_callback
 *
 * Purpose:     A callback for RV_parse_response which will search an HTTP
 *              response for links in a group and iterate through them,
 *              setting up a H5L_info2_t struct and calling the supplied
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
RV_link_iter_callback(char *HTTP_response, const void *callback_data_in, void *callback_data_out)
{
    link_table_entry *link_table         = NULL;
    rv_hash_table_t  *visited_link_table = NULL;
    const iter_data  *link_iter_data     = (const iter_data *)callback_data_in;
    size_t            link_table_num_entries;
    herr_t            ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Iterating %s through links according to server's HTTP response\n\n",
           link_iter_data->is_recursive ? "recursively" : "non-recursively");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL");
    if (!link_iter_data)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "link iteration data pointer was NULL");

    /* If this is a call to H5Lvisit, setup a hash table to keep track of visited links
     * so that cyclic links can be dealt with appropriately.
     */
    if (link_iter_data->is_recursive) {
        if (NULL == (visited_link_table = rv_hash_table_new(rv_hash_string, H5_rest_compare_string_keys)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL,
                            "can't allocate hash table for determining cyclic links");

        /* Since the JSON parse trees aren't persistent, the keys inserted into the visited link hash table
         * are RV_malloc()ed copies. Make sure to free these when freeing the table.
         */
        rv_hash_table_register_free_functions(visited_link_table, RV_free_visited_link_hash_table_key, NULL);
    } /* end if */

    /* Build a table of all of the links in the given group */
    if (H5_INDEX_CRT_ORDER == link_iter_data->index_type) {
        /* This code assumes that links are returned in alphabetical order by default. If the user has
         * requested them by creation order, sort them this way while building the link table. If, in the
         * future, links are not returned in alphabetical order by default, this code should be changed to
         * reflect this.
         */
        if (RV_build_link_table(HTTP_response, link_iter_data->is_recursive,
                                H5_rest_cmp_links_by_creation_order_inc, &link_table, &link_table_num_entries,
                                visited_link_table, link_iter_data->iter_obj_parent) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTBUILDLINKTABLE, FAIL, "can't build link table");

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Link table sorted according to link creation order\n\n");
#endif
    } /* end if */
    else {
        if (RV_build_link_table(HTTP_response, link_iter_data->is_recursive, NULL, &link_table,
                                &link_table_num_entries, visited_link_table,
                                link_iter_data->iter_obj_parent) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTBUILDLINKTABLE, FAIL, "can't build link table");
    } /* end else */

    /* Begin iteration */
    if (link_table)
        if (RV_traverse_link_table(link_table, link_table_num_entries, link_iter_data, NULL) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_LINKITERERROR, FAIL, "can't iterate over link table");

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
 * Function:    RV_build_link_table
 *
 * Purpose:     Given an HTTP response that contains the information about
 *              all of the links contained within a given group, this
 *              function builds a list of link_table_entry structs
 *              (defined near the top of this file), one for each link,
 *              which each contain a link's name, creation time and a link
 *              info H5L_info2_t struct.
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
                    link_table_entry **link_table, size_t *num_entries, rv_hash_table_t *visited_link_table,
                    RV_object_t *loc_obj)
{
    link_table_entry *table      = NULL;
    yajl_val          parse_tree = NULL, key_obj;
    yajl_val          link_obj, link_field_obj;
    size_t            i, num_links;
    char             *HTTP_buffer  = HTTP_response;
    char             *visit_buffer = NULL;
    char             *link_section_start, *link_section_end;
    char             *url_encoded_link_name = NULL;
    char              request_endpoint[URL_MAX_LENGTH];
    herr_t            ret_value = SUCCEED;

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response was NULL");
    if (!link_table)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "link table pointer was NULL");
    if (!num_entries)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "link table num. entries pointer was NULL");
    if (is_recursive && !visited_link_table)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "visited link hash table was NULL");

#ifdef RV_CONNECTOR_DEBUG
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
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate temporary buffer for H5Lvisit");

        memcpy(visit_buffer, HTTP_response, buffer_len);
        visit_buffer[buffer_len] = '\0';

        HTTP_buffer = visit_buffer;
    } /* end if */

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_buffer, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_PARSEERROR, FAIL, "parsing JSON failed");

    if (NULL == (key_obj = yajl_tree_get(parse_tree, links_keys, yajl_t_array)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "retrieval of links object failed");

    num_links = YAJL_GET_ARRAY(key_obj)->len;
    if (num_links < 0)
        FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "number of links in group was negative");

    /* If this group has no links, leave its sub-table alone */
    if (!num_links)
        FUNC_GOTO_DONE(SUCCEED);

    /* Build a table of link information for each link so that we can sort in order
     * of link creation if needed and can also work in decreasing order if desired
     */
    if (NULL == (table = RV_malloc(num_links * sizeof(*table))))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate space for link table");

    /* Find the beginning of the "links" section */
    if (NULL == (link_section_start = strstr(HTTP_buffer, "\"links\"")))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_PARSEERROR, FAIL,
                        "can't find \"links\" information section in HTTP response");

    /* For each link, grab its name and creation order, then find its corresponding JSON
     * subsection, place a NULL terminator at the end of it in order to "extract out" that
     * subsection, and pass it to the "get link info" callback function in order to fill
     * out a H5L_info2_t struct for the link.
     */
    for (i = 0; i < num_links; i++) {
        char *link_name;

        link_obj = YAJL_GET_ARRAY(key_obj)->values[i];

        /* Get the current link's name */
        if (NULL == (link_field_obj = yajl_tree_get(link_obj, link_title_keys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "retrieval of link name failed");

        if (NULL == (link_name = YAJL_GET_STRING(link_field_obj)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "returned link name was NULL");

        strncpy(table[i].link_name, link_name, LINK_NAME_MAX_LENGTH);

        /* Get the current link's creation time */
        if (NULL == (link_field_obj = yajl_tree_get(link_obj, link_creation_time_keys, yajl_t_number)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "retrieval of link creation time failed");

        if (!YAJL_IS_DOUBLE(link_field_obj))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "returned link creation time is not a double");

        table[i].crt_time = YAJL_GET_DOUBLE(link_field_obj);

        /* Process the JSON for the current link and fill out a H5L_info2_t struct for it */

        /* Find the beginning and end of the JSON section for this link */
        if (NULL == (link_section_start = strstr(link_section_start, "{")))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_PARSEERROR, FAIL,
                            "can't find start of current link's JSON section");

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

        /* Fill out a H5L_info2_t struct for this link */
        if (RV_parse_response(link_section_start, NULL, &table[i].link_info, RV_get_link_info_callback) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "couldn't get link info");

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
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "retrieval of link collection failed");

            if (NULL == (link_collection = YAJL_GET_STRING(link_field_obj)))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "returned link collection was NULL");

            if (!strcmp(link_collection, "groups")) {
                char *link_id;

                /* Retrieve the ID of the current link */
                if (NULL == (link_field_obj = yajl_tree_get(link_obj, object_id_keys, yajl_t_string)))
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "retrieval of link ID failed");

                if (NULL == (link_id = YAJL_GET_STRING(link_field_obj)))
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "returned link ID was NULL");

                /* Check if this link has been visited already before processing it */
                if (RV_HASH_TABLE_NULL == rv_hash_table_lookup(visited_link_table, link_id)) {
                    size_t link_id_len = strlen(link_id);
                    char  *link_id_copy;

                    /* Make a copy of the key and add it to the hash table to prevent future cyclic links from
                     * being visited */
                    if (NULL == (link_id_copy = RV_malloc(link_id_len + 1)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL,
                                        "unable to allocate space for key in visited link hash table");

                    strncpy(link_id_copy, link_id, link_id_len);
                    link_id_copy[link_id_len] = '\0';

                    if (!rv_hash_table_insert(visited_link_table, link_id_copy, link_id_copy))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTINSERT, FAIL,
                                        "unable to insert key into visited link hash table");

                    /* Make a GET request to the server to retrieve all of the links in the subgroup */

                    /* URL-encode the name of the link to ensure that the resulting URL for the link
                     * iteration operation doesn't contain any illegal characters
                     */
                    if (NULL == (url_encoded_link_name = curl_easy_escape(
                                     curl, H5_rest_basename(YAJL_GET_STRING(link_field_obj)), 0)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTENCODE, FAIL, "can't URL-encode link name");

                    if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/groups/%s/links",
                                            url_encoded_link_name)) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                        "link GET request URL size exceeded maximum URL size");

                    if (RV_curl_get(curl, &loc_obj->domain->u.file.server_info, request_endpoint,
                                    loc_obj->domain->u.file.filepath_name, CONTENT_TYPE_JSON) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't get link");

                    if (RV_build_link_table(response_buffer.buffer, is_recursive, sort_func,
                                            &table[i].subgroup.subgroup_link_table,
                                            &table[i].subgroup.num_entries, visited_link_table, loc_obj) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTBUILDLINKTABLE, FAIL,
                                        "can't build link table for subgroup '%s'", table[i].link_name);

                    curl_free(url_encoded_link_name);
                    url_encoded_link_name = NULL;
                } /* end if */
#ifdef RV_CONNECTOR_DEBUG
                else {
                    printf("-> Cyclic link detected; not following into subgroup\n\n");
                } /* end else */
#endif
            } /* end if */
        }     /* end if */

        /* Continue on to the next link subsection */
        link_section_start = link_section_end + 1;
    } /* end for */

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Link table built\n\n");
#endif

    if (sort_func)
        qsort(table, num_links, sizeof(*table), sort_func);

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
            RV_free_link_table(link_table[i].subgroup.subgroup_link_table,
                               link_table[i].subgroup.num_entries);
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
RV_traverse_link_table(link_table_entry *link_table, size_t num_entries, const iter_data *link_iter_data,
                       const char *cur_link_rel_path)
{
    static size_t depth = 0;
    size_t        last_idx;
    herr_t        callback_ret;
    size_t link_rel_path_len = (cur_link_rel_path ? strlen(cur_link_rel_path) : 0) + LINK_NAME_MAX_LENGTH + 2;
    char  *link_rel_path     = NULL;
    int    snprintf_ret      = 0;
    herr_t ret_value         = SUCCEED;

    if (NULL == (link_rel_path = (char *)RV_malloc(link_rel_path_len)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL,
                        "can't allocate space for link's relative pathname buffer");

    switch (link_iter_data->iter_order) {
        case H5_ITER_NATIVE:
        case H5_ITER_INC: {
#ifdef RV_CONNECTOR_DEBUG
            printf("-> Beginning iteration in increasing order\n\n");
#endif

            for (last_idx = (link_iter_data->idx_p ? *link_iter_data->idx_p : 0); last_idx < num_entries;
                 last_idx++) {
#ifdef RV_CONNECTOR_DEBUG
                printf("-> Link %zu name: %s\n", last_idx, link_table[last_idx].link_name);
                printf("-> Link %zu creation time: %f\n", last_idx, link_table[last_idx].crt_time);
                printf("-> Link %zu type: %s\n\n", last_idx,
                       link_class_to_string(link_table[last_idx].link_info.type));
#endif

                /* Form the link's relative path from the parent group by combining the current relative path
                 * with the link's name */
                if ((snprintf_ret = snprintf(link_rel_path, link_rel_path_len, "%s%s%s",
                                             cur_link_rel_path ? cur_link_rel_path : "",
                                             cur_link_rel_path ? "/" : "", link_table[last_idx].link_name)) <
                    0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                if ((size_t)snprintf_ret >= link_rel_path_len)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                    "link's relative path string size exceeded allocated buffer size");

#ifdef RV_CONNECTOR_DEBUG
                printf("-> Calling supplied callback function with relative link path %s\n\n", link_rel_path);
#endif

                /* Call the user's callback */
                callback_ret = link_iter_data->iter_function.link_iter_op(
                    link_iter_data->iter_obj_id, link_rel_path, &link_table[last_idx].link_info,
                    link_iter_data->op_data);
                if (callback_ret < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CALLBACK, callback_ret,
                                    "H5Literate/H5Lvisit (_by_name) user callback failed for link '%s'",
                                    link_table[last_idx].link_name);
                else if (callback_ret > 0)
                    FUNC_GOTO_DONE(callback_ret);

                /* If this is a group and H5Lvisit has been called, descend into the group */
                if (link_table[last_idx].subgroup.subgroup_link_table) {
#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Descending into subgroup '%s'\n\n", link_table[last_idx].link_name);
#endif

                    depth++;
                    if (RV_traverse_link_table(link_table[last_idx].subgroup.subgroup_link_table,
                                               link_table[last_idx].subgroup.num_entries, link_iter_data,
                                               link_rel_path) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_LINKITERERROR, FAIL,
                                        "can't iterate over links in subgroup '%s'",
                                        link_table[last_idx].link_name);
                    depth--;

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Exiting subgroup '%s'\n\n", link_table[last_idx].link_name);
#endif
                } /* end if */
                else {
                    char *last_slash = strrchr(link_rel_path, '/');

                    /* Truncate the relative path buffer by cutting off the trailing link name from the
                     * current path chain */
                    if (last_slash)
                        *last_slash = '\0';

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Relative link path after truncating trailing link name: %s\n\n",
                           link_rel_path);
#endif
                } /* end else */
            }     /* end for */

            break;
        } /* H5_ITER_NATIVE H5_ITER_INC */

        case H5_ITER_DEC: {
#ifdef RV_CONNECTOR_DEBUG
            printf("-> Beginning iteration in decreasing order\n\n");
#endif

            for (last_idx = (link_iter_data->idx_p ? *link_iter_data->idx_p : num_entries - 1); last_idx >= 0;
                 last_idx--) {
#ifdef RV_CONNECTOR_DEBUG
                printf("-> Link %zu name: %s\n", last_idx, link_table[last_idx].link_name);
                printf("-> Link %zu creation time: %f\n", last_idx, link_table[last_idx].crt_time);
                printf("-> Link %zu type: %s\n\n", last_idx,
                       link_class_to_string(link_table[last_idx].link_info.type));
#endif

                /* Form the link's relative path from the parent group by combining the current relative path
                 * with the link's name */
                if ((snprintf_ret = snprintf(link_rel_path, link_rel_path_len, "%s%s%s",
                                             cur_link_rel_path ? cur_link_rel_path : "",
                                             cur_link_rel_path ? "/" : "", link_table[last_idx].link_name)) <
                    0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                if ((size_t)snprintf_ret >= link_rel_path_len)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                    "link's relative path string size exceeded allocated buffer size");

#ifdef RV_CONNECTOR_DEBUG
                printf("-> Calling supplied callback function with relative link path %s\n\n", link_rel_path);
#endif

                /* Call the user's callback */
                callback_ret = link_iter_data->iter_function.link_iter_op(
                    link_iter_data->iter_obj_id, link_rel_path, &link_table[last_idx].link_info,
                    link_iter_data->op_data);
                if (callback_ret < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CALLBACK, callback_ret,
                                    "H5Literate/H5Lvisit (_by_name) user callback failed for link '%s'",
                                    link_table[last_idx].link_name);
                else if (callback_ret > 0)
                    FUNC_GOTO_DONE(callback_ret);

                /* If this is a group and H5Lvisit has been called, descend into the group */
                if (link_table[last_idx].subgroup.subgroup_link_table) {
#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Descending into subgroup '%s'\n\n", link_table[last_idx].link_name);
#endif

                    depth++;
                    if (RV_traverse_link_table(link_table[last_idx].subgroup.subgroup_link_table,
                                               link_table[last_idx].subgroup.num_entries, link_iter_data,
                                               link_rel_path) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_LINKITERERROR, FAIL,
                                        "can't iterate over links in subgroup '%s'",
                                        link_table[last_idx].link_name);
                    depth--;

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Exiting subgroup '%s'\n\n", link_table[last_idx].link_name);
#endif
                } /* end if */
                else {
                    char *last_slash = strrchr(link_rel_path, '/');

                    /* Truncate the relative path buffer by cutting off the trailing link name from the
                     * current path chain */
                    if (last_slash)
                        *last_slash = '\0';

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Relative link path after truncating trailing link name: %s\n\n",
                           link_rel_path);
#endif
                } /* end else */

                if (last_idx == 0)
                    break;
            } /* end for */

            break;
        } /* H5_ITER_DEC */

        case H5_ITER_UNKNOWN:
        case H5_ITER_N:
        default:
            FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "unknown link iteration order");
    } /* end switch */

#ifdef RV_CONNECTOR_DEBUG
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
 * Function:    H5_rest_cmp_links_by_creation_order_inc
 *
 * Purpose:     Qsort callback to sort links by creation order; the links
 *              will be sorted in increasing order of creation order.
 *
 * Return:      negative if the creation time of link1 is earlier than that
 *              of link2
 *              0 if the creation time of link1 and link2 are equal
 *              positive if the creation time of link1 is later than that
 *              of link2
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static int
H5_rest_cmp_links_by_creation_order_inc(const void *link1, const void *link2)
{
    const link_table_entry *_link1 = (const link_table_entry *)link1;
    const link_table_entry *_link2 = (const link_table_entry *)link2;

    return ((_link1->crt_time > _link2->crt_time) - (_link1->crt_time < _link2->crt_time));
} /* end H5_rest_cmp_links_by_creation_order_inc() */

/*-------------------------------------------------------------------------
 * Function:    H5_rest_cmp_links_by_creation_order_dec
 *
 * Purpose:     Qsort callback to sort links by creation order; the links
 *              will be sorted in decreasing order of creation order.
 *
 * Return:      negative if the creation time of link1 is later than that
 *              of link2
 *              0 if the creation time of link1 and link2 are equal
 *              positive if the creation time of link1 is earlier than that
 *              of link2
 *
 * Programmer:  Jordan Henderson
 *              November, 2018
 */
static int
H5_rest_cmp_links_by_creation_order_dec(const void *link1, const void *link2)
{
    const link_table_entry *_link1 = (const link_table_entry *)link1;
    const link_table_entry *_link2 = (const link_table_entry *)link2;

    return ((_link1->crt_time < _link2->crt_time) - (_link1->crt_time > _link2->crt_time));
} /* end H5_rest_cmp_links_by_creation_order_dec() */

/*-------------------------------------------------------------------------
 * Function:    H5_rest_cmp_links_by_name_inc
 *
 * Purpose:     Qsort callback to sort links by name; the links will be
 *              sorted in increasing order of name.
 *
 * Return:      negative if the name of link1 comes earlier alphabetically
 *              than that of link2
 *              0 if the name of link1 and link2 are alphabetically equal
 *              positive if the name of link1 comes later alphabetically
 *              than that of link2
 *
 * Programmer:  Jordan Henderson
 *              November, 2018
 */
static int
H5_rest_cmp_links_by_name_inc(const void *link1, const void *link2)
{
    const link_table_entry *_link1 = (const link_table_entry *)link1;
    const link_table_entry *_link2 = (const link_table_entry *)link2;

    return strncmp(_link1->link_name, _link2->link_name, LINK_NAME_MAX_LENGTH);
} /* end H5_rest_cmp_links_by_name_inc() */

/*-------------------------------------------------------------------------
 * Function:    H5_rest_cmp_links_by_name_dec
 *
 * Purpose:     Qsort callback to sort links by name; the links will be
 *              sorted in decreasing order of name.
 *
 * Return:      negative if the name of link1 comes later alphabetically
 *              than that of link2
 *              0 if the name of link1 and link2 are alphabetically equal
 *              positive if the name of link1 comes earlier alphabetically
 *              than that of link2
 *
 * Programmer:  Jordan Henderson
 *              November, 2018
 */
static int
H5_rest_cmp_links_by_name_dec(const void *link1, const void *link2)
{
    const link_table_entry *_link1 = (const link_table_entry *)link1;
    const link_table_entry *_link2 = (const link_table_entry *)link2;

    return (-1) * strncmp(_link1->link_name, _link2->link_name, LINK_NAME_MAX_LENGTH);
} /* end H5_rest_cmp_links_by_name_dec() */
