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
 * Implementations of the object callbacks for the HDF5 REST VOL connector.
 */

#include "rest_vol_object.h"

/* Set of callbacks for RV_parse_response() */
static herr_t RV_get_object_info_callback(char *HTTP_response, void *callback_data_in,
                                          void *callback_data_out);

/* JSON keys to retrieve relevant information for H5Oget_info */
const char *attribute_count_keys[] = {"attributeCount", (const char *)0};
const char *hrefs_keys[]           = {"hrefs", (const char *)0};

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
void *
RV_object_open(void *obj, const H5VL_loc_params_t *loc_params, H5I_type_t *opened_type, hid_t dxpl_id,
               void **req)
{
    RV_object_t *loc_obj  = (RV_object_t *)obj;
    H5I_type_t   obj_type = H5I_UNINIT;
    hid_t        lapl_id;
    void        *ret_value = NULL;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received object open call with following parameters:\n");
    if (H5VL_OBJECT_BY_NAME == loc_params->type) {
        printf("     - H5Oopen variant: H5Oopen\n");
    } /* end if */
    else if (H5VL_OBJECT_BY_IDX == loc_params->type) {
        printf("     - H5Oopen variant: H5Oopen_by_idx\n");
    } /* end else if */

    if (loc_params->loc_data.loc_by_name.name)
        printf("     - Path to object: %s\n", loc_params->loc_data.loc_by_name.name);
    printf("     - loc_id object's URI: %s\n", loc_obj->URI);
    printf("     - loc_id object's type: %s\n", object_type_to_string(loc_obj->obj_type));
    printf("     - loc_id object's domain path: %s\n\n", loc_obj->domain->u.file.filepath_name);
#endif

    if (H5I_FILE != loc_obj->obj_type && H5I_GROUP != loc_obj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object not a file or group");

    switch (loc_params->type) {
        /* H5Oopen */
        case H5VL_OBJECT_BY_NAME: {
            htri_t search_ret;

            /* Retrieve the type of object being dealt with by querying the server */
            search_ret = RV_find_object_by_path(loc_obj, loc_params->loc_data.loc_by_name.name, &obj_type,
                                                NULL, NULL, NULL);
            if (!search_ret || search_ret < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_PATH, NULL, "can't find object by name");

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Found object by given path\n\n");
#endif

            break;
        } /* H5VL_OBJECT_BY_NAME */

        /* H5Oopen_by_idx */
        case H5VL_OBJECT_BY_IDX: {
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, NULL, "H5Oopen_by_idx is unsupported");
            break;
        } /* H5VL_OBJECT_BY_IDX */

        case H5VL_OBJECT_BY_TOKEN:
        case H5VL_OBJECT_BY_SELF:
        default:
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, NULL, "invalid loc_params type");
    } /* end switch */

    /* Call the appropriate RV_*_open call based upon the object type */
    switch (obj_type) {
        case H5I_DATATYPE:
#ifdef RV_CONNECTOR_DEBUG
            printf("-> Opening datatype\n\n");
#endif

            /* Setup the correct lapl_id. Note that if H5P_DEFAULT was specified for the LAPL in the
             * H5Oopen(_by_name) call, HDF5 will actually pass H5P_LINK_ACCESS_DEFAULT down to this layer */
            if (H5VL_OBJECT_BY_NAME == loc_params->type) {
                lapl_id = (loc_params->loc_data.loc_by_name.lapl_id != H5P_LINK_ACCESS_DEFAULT)
                              ? loc_params->loc_data.loc_by_name.lapl_id
                              : H5P_DATATYPE_ACCESS_DEFAULT;
            } /* end if */
            else if (H5VL_OBJECT_BY_IDX == loc_params->type) {
                lapl_id = (loc_params->loc_data.loc_by_idx.lapl_id != H5P_LINK_ACCESS_DEFAULT)
                              ? loc_params->loc_data.loc_by_idx.lapl_id
                              : H5P_DATATYPE_ACCESS_DEFAULT;
            } /* end else if */
            else
                lapl_id = H5P_DATATYPE_ACCESS_DEFAULT;

            if (NULL ==
                (ret_value = RV_datatype_open(loc_obj, loc_params, loc_params->loc_data.loc_by_name.name,
                                              lapl_id, dxpl_id, req)))
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTOPENOBJ, NULL, "can't open datatype");
            break;

        case H5I_DATASET:
#ifdef RV_CONNECTOR_DEBUG
            printf("-> Opening dataset\n\n");
#endif

            /* Setup the correct lapl_id. Note that if H5P_DEFAULT was specified for the LAPL in the
             * H5Oopen(_by_name) call, HDF5 will actually pass H5P_LINK_ACCESS_DEFAULT down to this layer */
            if (H5VL_OBJECT_BY_NAME == loc_params->type) {
                lapl_id = (loc_params->loc_data.loc_by_name.lapl_id != H5P_LINK_ACCESS_DEFAULT)
                              ? loc_params->loc_data.loc_by_name.lapl_id
                              : H5P_DATASET_ACCESS_DEFAULT;
            } /* end if */
            else if (H5VL_OBJECT_BY_IDX == loc_params->type) {
                lapl_id = (loc_params->loc_data.loc_by_idx.lapl_id != H5P_LINK_ACCESS_DEFAULT)
                              ? loc_params->loc_data.loc_by_idx.lapl_id
                              : H5P_DATASET_ACCESS_DEFAULT;
            } /* end else if */
            else
                lapl_id = H5P_DATASET_ACCESS_DEFAULT;

            if (NULL ==
                (ret_value = RV_dataset_open(loc_obj, loc_params, loc_params->loc_data.loc_by_name.name,
                                             lapl_id, dxpl_id, req)))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTOPENOBJ, NULL, "can't open dataset");
            break;

        case H5I_GROUP:
#ifdef RV_CONNECTOR_DEBUG
            printf("-> Opening group\n\n");
#endif

            /* Setup the correct lapl_id. Note that if H5P_DEFAULT was specified for the LAPL in the
             * H5Oopen(_by_name) call, HDF5 will actually pass H5P_LINK_ACCESS_DEFAULT down to this layer */
            if (H5VL_OBJECT_BY_NAME == loc_params->type) {
                lapl_id = (loc_params->loc_data.loc_by_name.lapl_id != H5P_LINK_ACCESS_DEFAULT)
                              ? loc_params->loc_data.loc_by_name.lapl_id
                              : H5P_GROUP_ACCESS_DEFAULT;
            } /* end if */
            else if (H5VL_OBJECT_BY_IDX == loc_params->type) {
                lapl_id = (loc_params->loc_data.loc_by_idx.lapl_id != H5P_LINK_ACCESS_DEFAULT)
                              ? loc_params->loc_data.loc_by_idx.lapl_id
                              : H5P_GROUP_ACCESS_DEFAULT;
            } /* end else if */
            else
                lapl_id = H5P_GROUP_ACCESS_DEFAULT;

            if (NULL == (ret_value = RV_group_open(loc_obj, loc_params, loc_params->loc_data.loc_by_name.name,
                                                   lapl_id, dxpl_id, req)))
                FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTOPENOBJ, NULL, "can't open group");
            break;

        case H5I_ATTR:
        case H5I_UNINIT:
        case H5I_BADID:
        case H5I_FILE:
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
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTOPENOBJ, NULL, "invalid object type");
    } /* end switch */

    if (opened_type)
        *opened_type = obj_type;

done:
    PRINT_ERROR_STACK;

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
herr_t
RV_object_copy(void *src_obj, const H5VL_loc_params_t *loc_params1, const char *src_name, void *dst_obj,
               const H5VL_loc_params_t *loc_params2, const char *dst_name, hid_t ocpypl_id, hid_t lcpl_id,
               hid_t dxpl_id, void **req)
{
    herr_t ret_value = SUCCEED;

    FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, FAIL, "H5Ocopy is unsupported");

done:
    PRINT_ERROR_STACK;

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
herr_t
RV_object_get(void *obj, const H5VL_loc_params_t *loc_params, H5VL_object_get_args_t *args, hid_t dxpl_id,
              void **req)
{
    RV_object_t *loc_obj         = (RV_object_t *)obj;
    size_t       host_header_len = 0;
    char        *host_header     = NULL;
    char         request_url[URL_MAX_LENGTH];
    char        *found_object_name = NULL;
    int          url_len           = 0;
    herr_t       ret_value         = SUCCEED;
    loc_info     loc_info_out;

    loc_info_out.GCPL_base64 = NULL;
    loc_info_out.domain      = loc_obj->domain;
    loc_obj->domain->u.file.ref_count++;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received object get call with following parameters:\n");
    printf("     - Object get call type: %s\n", object_get_type_to_string(args->op_type));
    printf("     - loc_id object's URI: %s\n", loc_obj->URI);
    printf("     - loc_id object's type: %s\n", object_type_to_string(loc_obj->obj_type));
    printf("     - loc_id object's domain path: %s\n\n", loc_obj->domain->u.file.filepath_name);
#endif

    if (H5I_FILE != loc_obj->obj_type && H5I_GROUP != loc_obj->obj_type &&
        H5I_DATATYPE != loc_obj->obj_type && H5I_DATASET != loc_obj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a file, group, dataset or committed datatype");

    switch (args->op_type) {
        case H5VL_OBJECT_GET_FILE:
        case H5VL_OBJECT_GET_NAME:
        case H5VL_OBJECT_GET_TYPE:
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, FAIL, "unsupported object operation");
            break;

        case H5VL_OBJECT_GET_INFO: {
            H5O_info2_t *obj_info = args->args.get_info.oinfo;
            unsigned     fields   = args->args.get_info.fields;
            H5I_type_t   obj_type;

            switch (loc_params->type) {
                /* H5Oget_info */
                case H5VL_OBJECT_BY_SELF: {
                    obj_type = loc_obj->obj_type;

                    /* Redirect cURL from the base URL to
                     * "/groups/<id>", "/datasets/<id>" or "/datatypes/<id>",
                     * depending on the type of the object. Also set the
                     * object's type in the H5O_info2_t struct.
                     */
                    switch (obj_type) {
                        case H5I_FILE:
                        case H5I_GROUP: {
                            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s", base_URL,
                                                    loc_obj->URI)) < 0)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "snprintf error");

                            if (url_len >= URL_MAX_LENGTH)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL,
                                                "H5Oget_info request URL size exceeded maximum URL size");

                            break;
                        } /* H5I_FILE H5I_GROUP */

                        case H5I_DATATYPE: {
                            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s", base_URL,
                                                    loc_obj->URI)) < 0)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "snprintf error");

                            if (url_len >= URL_MAX_LENGTH)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL,
                                                "H5Oget_info request URL size exceeded maximum URL size");

                            break;
                        } /* H5I_DATATYPE */

                        case H5I_DATASET: {
                            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s", base_URL,
                                                    loc_obj->URI)) < 0)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "snprintf error");

                            if (url_len >= URL_MAX_LENGTH)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL,
                                                "H5Oget_info request URL size exceeded maximum URL size");

                            break;
                        } /* H5I_DATASET */

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
                            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL,
                                            "loc_id object is not a group, datatype or dataset");
                    } /* end switch */

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Oget_info(): Object type: %s\n\n", object_type_to_string(obj_type));
#endif

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Oget_info_by_name */
                case H5VL_OBJECT_BY_NAME: {
                    htri_t search_ret;
                    char   temp_URI[URI_MAX_LENGTH];

                    obj_type = H5I_UNINIT;

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Oget_info_by_name(): locating object by given path\n\n");
#endif

                    loc_info_out.URI = temp_URI;
                    /* loc_info_out.domain was copied at function start */

                    /* Locate group and set domain */
                    search_ret =
                        RV_find_object_by_path(loc_obj, loc_params->loc_data.loc_by_name.name, &obj_type,
                                               RV_copy_object_loc_info_callback, NULL, &loc_info_out);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PATH, FAIL, "can't locate object by path");

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Oget_info_by_name(): found object by given path\n");
                    printf("-> H5Oget_info_by_name(): object's URI: %s\n", temp_URI);
                    printf("-> H5Oget_info_by_name(): object's type: %s\n\n",
                           object_type_to_string(obj_type));
#endif

                    /* Redirect cURL from the base URL to
                     * "/groups/<id>", "/datasets/<id>" or "/datatypes/<id>",
                     * depending on the type of the object. Also set the
                     * object's type in the H5O_info2_t struct.
                     */
                    switch (obj_type) {
                        case H5I_FILE:
                        case H5I_GROUP: {
                            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s", base_URL,
                                                    temp_URI)) < 0)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "snprintf error");

                            if (url_len >= URL_MAX_LENGTH)
                                FUNC_GOTO_ERROR(
                                    H5E_OBJECT, H5E_SYSERRSTR, FAIL,
                                    "H5Oget_info_by_name request URL size exceeded maximum URL size");

                            break;
                        } /* H5I_FILE H5I_GROUP */

                        case H5I_DATATYPE: {
                            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datatypes/%s", base_URL,
                                                    temp_URI)) < 0)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "snprintf error");

                            if (url_len >= URL_MAX_LENGTH)
                                FUNC_GOTO_ERROR(
                                    H5E_OBJECT, H5E_SYSERRSTR, FAIL,
                                    "H5Oget_info_by_name request URL size exceeded maximum URL size");

                            break;
                        } /* H5I_DATATYPE */

                        case H5I_DATASET: {
                            if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datasets/%s", base_URL,
                                                    temp_URI)) < 0)
                                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "snprintf error");

                            if (url_len >= URL_MAX_LENGTH)
                                FUNC_GOTO_ERROR(
                                    H5E_OBJECT, H5E_SYSERRSTR, FAIL,
                                    "H5Oget_info_by_name request URL size exceeded maximum URL size");

                            break;
                        } /* H5I_DATASET */

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
                            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL,
                                            "loc_id object is not a group, datatype or dataset");
                    } /* end switch */

                    break;
                } /* H5VL_OBJECT_BY_NAME */

                /* H5Oget_info_by_idx */
                case H5VL_OBJECT_BY_IDX: {
                    htri_t            search_ret;
                    char              temp_URI[URI_MAX_LENGTH];
                    const char       *request_idx_type       = NULL;
                    const char       *parent_obj_type_header = NULL;

                    obj_type = H5I_UNINIT;

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Oget_info_by_idx(): locating object by given path\n\n");
#endif

                    loc_info_out.URI = temp_URI;
                    /* loc_info_out.domain was copied at function start */

                    /* Locate group and set domain */
                    search_ret =
                        RV_find_object_by_path(loc_obj, loc_params->loc_data.loc_by_name.name, &obj_type,
                                               RV_copy_object_loc_info_callback, NULL, &loc_info_out);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PATH, FAIL, "can't locate object by path");

                    if (obj_type != H5I_GROUP && obj_type != H5I_FILE)
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PATH, FAIL, "specified name did not lead to a group");

                    switch (loc_params->loc_data.loc_by_idx.idx_type) {
                        case (H5_INDEX_CRT_ORDER):
                            if (SERVER_VERSION_MATCHES_OR_EXCEEDS(loc_obj->domain->u.file.server_version, 0,
                                                                  7, 3)) {
                                request_idx_type = "&CreateOrder=1";
                            }
                            else {
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_UNSUPPORTED, NULL,
                                                "indexing by creation order not supported by server versions "
                                                "before 0.7.3");
                            }

                            break;
                        case (H5_INDEX_NAME):
                            request_idx_type = "";
                            break;
                        case (H5_INDEX_N):
                        case (H5_INDEX_UNKNOWN):
                        default:
                            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL,
                                            "unsupported index type specified");
                            break;
                    }

                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_PATH, FAIL, "can't locate parent object");

                    /* Setup the host header */
                    host_header_len = strlen(loc_obj->domain->u.file.filepath_name) + strlen(host_string) + 1;
                    if (NULL == (host_header = (char *)RV_malloc(host_header_len)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL,
                                        "can't allocate space for request Host header");

                    strcpy(host_header, host_string);

                    curl_headers = curl_slist_append(
                        curl_headers, strncat(host_header, loc_obj->domain->u.file.filepath_name,
                                              host_header_len - strlen(host_string) - 1));

                    /* Disable use of Expect: 100 Continue HTTP response */
                    curl_headers = curl_slist_append(curl_headers, "Expect:");

                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/links?%s", base_URL,
                                            temp_URI, request_idx_type)) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                        "attribute open URL exceeded maximum URL size");

                    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s",
                                        curl_err_buf);
                    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL,
                                        "can't set up cURL to make HTTP GET request: %s", curl_err_buf);
                    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL request URL: %s",
                                        curl_err_buf);

                    CURL_PERFORM(curl, H5E_LINK, H5E_CANTGET, FAIL);

                    if (0 > RV_parse_response(response_buffer.buffer, (void *)&loc_params->loc_data.loc_by_idx, &found_object_name,
                                              RV_copy_link_name_by_index))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_PARSEERROR, FAIL, "failed to retrieve link names");

                    if (host_header) {
                        RV_free(host_header);
                        host_header = NULL;
                    }

                    if (curl_headers) {
                        curl_slist_free_all(curl_headers);
                        curl_headers = NULL;
                    } /* end if */

                    /* Use name of link to get object URI for final request */

                    if (loc_info_out.GCPL_base64) {
                        RV_free(loc_info_out.GCPL_base64);
                        loc_info_out.GCPL_base64 = NULL;
                    }

                    search_ret =
                        RV_find_object_by_path(loc_obj, found_object_name, &obj_type,
                                               RV_copy_object_loc_info_callback, NULL, &loc_info_out);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PATH, FAIL, "can't locate object by path");

                    switch (obj_type) {
                        case H5I_FILE:
                        case H5I_GROUP:
                            parent_obj_type_header = "groups";
                            break;
                        case H5I_DATATYPE:
                            parent_obj_type_header = "datatypes";
                            break;
                        case H5I_DATASET:
                            parent_obj_type_header = "datasets";
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
                                            "object at index not a group, datatype or dataset");
                    }

                    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/%s/%s", base_URL,
                                            parent_obj_type_header, loc_info_out.URI)) < 0)
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL,
                                        "H5Oget_info_by_name request URL size exceeded maximum URL size");

                    break;
                } /* H5VL_OBJECT_BY_IDX */

                case H5VL_OBJECT_BY_TOKEN:
                default:
                    FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "invalid loc_params type");
            } /* end switch */

            /* Make a GET request to the server to retrieve the number of attributes attached to the object */

            /* Setup the host header */
            host_header_len = strlen(loc_info_out.domain->u.file.filepath_name) + strlen(host_string) + 1;
            if (NULL == (host_header = (char *)RV_malloc(host_header_len)))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTALLOC, FAIL,
                                "can't allocate space for request Host header");

            strcpy(host_header, host_string);

            curl_headers = curl_slist_append(curl_headers,
                                             strncat(host_header, loc_info_out.domain->u.file.filepath_name,
                                                     host_header_len - strlen(host_string) - 1));

            /* Disable use of Expect: 100 Continue HTTP response */
            curl_headers = curl_slist_append(curl_headers, "Expect:");

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s",
                                curl_err_buf);
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTSET, FAIL,
                                "can't set up cURL to make HTTP GET request: %s", curl_err_buf);
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTSET, FAIL, "can't set cURL request URL: %s",
                                curl_err_buf);

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Retrieving object info using URL: %s\n\n", request_url);

            printf("   /**********************************\\\n");
            printf("-> | Making GET request to the server |\n");
            printf("   \\**********************************/\n\n");
#endif

            CURL_PERFORM(curl, H5E_OBJECT, H5E_CANTGET, FAIL);

            /* Retrieve the attribute count for the object */
            if (RV_parse_response(response_buffer.buffer, NULL, obj_info, RV_get_object_info_callback) < 0)
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTGET, FAIL, "can't get object info");

            /* Set the type of the object */
            if (H5I_GROUP == obj_type)
                obj_info->type = H5O_TYPE_GROUP;
            else if (H5I_DATATYPE == obj_type)
                obj_info->type = H5O_TYPE_NAMED_DATATYPE;
            else if (H5I_DATASET == obj_type)
                obj_info->type = H5O_TYPE_DATASET;
            else
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL,
                                "object type is not group, datatype or dataset");

            break;
        } /* H5VL_OBJECT_GET_INFO */

        default:
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "unknown object operation");
    } /* end switch */

done:
    if (host_header)
        RV_free(host_header);

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    if (found_object_name) {
        RV_free(found_object_name);
        found_object_name = NULL;
    }

    RV_file_close(loc_info_out.domain, H5P_DEFAULT, NULL);
    free(loc_info_out.GCPL_base64);

    PRINT_ERROR_STACK;

    return ret_value;
} /* end RV_object_get() */

/*-------------------------------------------------------------------------
 * Function:    RV_object_specific
 *
 * Purpose:     Performs a connector-specific operation on an HDF5 object,
 *              such as calling the H5Ovisit routine.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
herr_t
RV_object_specific(void *obj, const H5VL_loc_params_t *loc_params, H5VL_object_specific_args_t *args,
                   hid_t dxpl_id, void **req)
{
    RV_object_t *loc_obj   = (RV_object_t *)obj;
    herr_t       ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received object-specific call with following parameters:\n");
    printf("     - Object-specific call type: %s\n", object_specific_type_to_string(args->op_type));
    printf("     - loc_id object's URI: %s\n", loc_obj->URI);
    printf("     - loc_id object's type: %s\n", object_type_to_string(loc_obj->obj_type));
    printf("     - loc_id object's domain path: %s\n\n", loc_obj->domain->u.file.filepath_name);
#endif

    switch (args->op_type) {
        /* H5Oincr/decr_refcount */
        case H5VL_OBJECT_CHANGE_REF_COUNT:
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, FAIL,
                            "H5Oincr_refcount and H5Odecr_refcount are unsupported");
            break;

        /* H5Oexists_by_name */
        case H5VL_OBJECT_EXISTS:
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, FAIL, "H5Oexists_by_name is unsupported");
            break;

        /* Object lookup for references */
        case H5VL_OBJECT_LOOKUP:
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, FAIL, "object lookup is unsupported");
            break;

        /* H5Ovisit(_by_name) */
        case H5VL_OBJECT_VISIT:
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, FAIL,
                            "H5Ovisit and H5Ovisit_by_name are unsupported");
            break;

        /* H5Oflush */
        case H5VL_OBJECT_FLUSH:
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, FAIL, "H5Oflush is unsupported");
            break;

        /* H5Orefresh */
        case H5VL_OBJECT_REFRESH:
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, FAIL, "H5Orefresh is unsupported");
            break;

        default:
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "unknown object operation");
    } /* end switch */

done:
    PRINT_ERROR_STACK;

    return ret_value;
} /* end RV_object_specific() */

/*-------------------------------------------------------------------------
 * Function:    RV_get_object_info_callback
 *
 * Purpose:     A callback for RV_parse_response which will search
 *              an HTTP response for info about an object and copy that
 *              info into the callback_data_out parameter, which should be
 *              a H5O_info2_t *. This callback is used to help H5Oget_info;
 *              currently only the file number, object address and number
 *              of attributes fields are filled out in the H5O_info2_t
 *              struct. All other fields are cleared and should not be
 *              relied upon.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              November, 2017
 */
static herr_t
RV_get_object_info_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out)
{
    H5O_info2_t *obj_info   = (H5O_info2_t *)callback_data_out;
    yajl_val     parse_tree = NULL, key_obj;
    size_t       i;
    char        *object_id, *domain_path = NULL;
    herr_t       ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Retrieving object's info from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL");
    if (!obj_info)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "object info pointer was NULL");

    memset(obj_info, 0, sizeof(*obj_info));

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "parsing JSON failed");

    /*
     * Fill out the fileno and addr fields with somewhat faked data, as these fields are used
     * in other places to verify that two objects are different. The domain path is hashed
     * and converted to an unsigned long for the fileno field and the object's UUID string
     * is hashed to an haddr_t for the addr field.
     */

    if (NULL == (key_obj = yajl_tree_get(parse_tree, hrefs_keys, yajl_t_array)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTGET, FAIL, "retrieval of object HREFs failed");

    /* Find the "home" href that corresponds to the object's domain path */
    for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++) {
        yajl_val href_obj = YAJL_GET_ARRAY(key_obj)->values[i];
        size_t   j;

        if (!YAJL_IS_OBJECT(href_obj))
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "HREFs array value is not an object");

        for (j = 0; j < YAJL_GET_OBJECT(href_obj)->len; j++) {
            char *key_val;

            if (NULL == (key_val = YAJL_GET_STRING(YAJL_GET_OBJECT(href_obj)->values[j])))
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "HREF object key value was NULL");

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
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTGET, FAIL,
                        "unable to determine a value for object info file number field");

    obj_info->fileno = (unsigned long)rv_hash_string(domain_path);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Object's file number: %lu\n", (unsigned long)obj_info->fileno);
#endif

    if (NULL == (key_obj = yajl_tree_get(parse_tree, object_id_keys, yajl_t_string)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTGET, FAIL, "retrieval of object ID failed");

    if (NULL == (object_id = YAJL_GET_STRING(key_obj)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "object ID string was NULL");

    /* TODO */
    obj_info->token = H5O_TOKEN_UNDEF;

#ifdef RV_CONNECTOR_DEBUG
    /* TODO: representation of object token */
#endif

    /* Retrieve the object's attribute count */
    if (NULL == (key_obj = yajl_tree_get(parse_tree, attribute_count_keys, yajl_t_number)))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTGET, FAIL, "retrieval of object attribute count failed");

    if (!YAJL_IS_INTEGER(key_obj))
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "returned object attribute count is not an integer");

    if (YAJL_GET_INTEGER(key_obj) < 0)
        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "returned object attribute count was negative");

    obj_info->num_attrs = (hsize_t)YAJL_GET_INTEGER(key_obj);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Object had %llu attributes attached to it\n\n", obj_info->num_attrs);
#endif

done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_get_object_info_callback() */
