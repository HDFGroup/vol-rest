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

/* Callback to iterate over objects given in HTTP response */
static herr_t RV_object_iter_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out);

/* Helper functions to work with a table of objects for object iteration */
static herr_t RV_build_object_table(char *HTTP_response, hbool_t is_recursive,
                                    int (*sort_func)(const void *, const void *),
                                    object_table_entry **object_table, size_t *num_entries,
                                    iter_data *object_iter_data, rv_hash_table_t *visited_link_table);

/* Function to go through each object in table and perform an operation */
static herr_t RV_traverse_object_table(object_table_entry *object_table,
                                       rv_hash_table_t *visited_object_table, size_t num_entries,
                                       iter_data *iter_data, const char *cur_object_rel_path);

static void RV_free_object_table(object_table_entry *object_table, size_t num_entries);

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

            if (H5I_INVALID_HID == loc_params->loc_data.loc_by_name.lapl_id)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, NULL, "invalid LAPL");

            htri_t search_ret;

            /* Retrieve the type of object being dealt with by querying the server */
            search_ret = RV_find_object_by_path(loc_obj, loc_params->loc_data.loc_by_name.name, &obj_type,
                                                NULL, NULL, NULL);
            if (!search_ret || search_ret < 0)
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PATH, NULL, "can't find object by name");

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
    const char  *base_URL          = NULL;
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

    if ((base_URL = loc_obj->domain->u.file.server_info.base_URL) == NULL)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "location object does not have valid server URL");

    switch (args->op_type) {
        case H5VL_OBJECT_GET_NAME: {
            size_t copy_size = 0;
            char  *name      = loc_obj->handle_path;

            if (!name)
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PATH, FAIL, "object has NULL name");

            /* Only return the name if the user provided an allocate buffer */
            if (args->args.get_name.buf) {
                /* Initialize entire buffer regardless of path size */
                memset(args->args.get_name.buf, 0, args->args.get_name.buf_size);

                /* If given an attribute, H5Iget_name returns the name of the object an attribute is attached
                 * to */
                if (loc_obj->obj_type == H5I_ATTR)
                    if ((name = loc_obj->u.attribute.parent_name) == NULL)
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "attribute parent has NULL name");

                copy_size = (strlen(name) < args->args.get_name.buf_size - 1)
                                ? strlen(name)
                                : args->args.get_name.buf_size - 1;
                strncpy(args->args.get_name.buf, name, copy_size);
            }

            if (args->args.get_name.name_len) {
                *args->args.get_name.name_len = strlen(name);
            }

        } break;
        case H5VL_OBJECT_GET_FILE:
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

                    if (H5I_INVALID_HID == loc_params->loc_data.loc_by_name.lapl_id)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid LAPL");

                    htri_t search_ret;
                    char   temp_URI[URI_MAX_LENGTH];

                    obj_type = H5I_UNINIT;

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Oget_info_by_name(): locating object by given path\n\n");
#endif

                    loc_info_out.URI = temp_URI;
                    /* loc_info_out.domain was copied at function start */

                    /* Locate group and set domain */
                    search_ret = RV_find_object_by_path(loc_obj, loc_params->loc_data.loc_by_name.name,
                                                        &obj_type, RV_copy_object_loc_info_callback,
                                                        &loc_obj->domain->u.file.server_info, &loc_info_out);
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

                    if (H5I_INVALID_HID == loc_params->loc_data.loc_by_idx.lapl_id)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid LAPL");

                    htri_t      search_ret;
                    char        temp_URI[URI_MAX_LENGTH];
                    const char *request_idx_type       = NULL;
                    const char *parent_obj_type_header = NULL;

                    obj_type = H5I_UNINIT;

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Oget_info_by_idx(): locating object by given path\n\n");
#endif

                    loc_info_out.URI = temp_URI;
                    /* loc_info_out.domain was copied at function start */

                    /* Locate group and set domain */
                    search_ret = RV_find_object_by_path(loc_obj, loc_params->loc_data.loc_by_name.name,
                                                        &obj_type, RV_copy_object_loc_info_callback,
                                                        &loc_obj->domain->u.file.server_info, &loc_info_out);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PATH, FAIL, "can't locate object by path");

                    if (obj_type != H5I_GROUP && obj_type != H5I_FILE)
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PATH, FAIL, "specified name did not lead to a group");

                    switch (loc_params->loc_data.loc_by_idx.idx_type) {
                        case (H5_INDEX_CRT_ORDER):
                            if (SERVER_VERSION_MATCHES_OR_EXCEEDS(loc_obj->domain->u.file.server_info.version,
                                                                  0, 8, 0)) {
                                request_idx_type = "&CreateOrder=1";
                            }
                            else {
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_UNSUPPORTED, FAIL,
                                                "indexing by creation order not supported by server versions "
                                                "before 0.8.0");
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

                    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_USERNAME,
                                                     loc_obj->domain->u.file.server_info.username))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL username: %s",
                                        curl_err_buf);
                    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_PASSWORD,
                                                     loc_obj->domain->u.file.server_info.password))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL password: %s",
                                        curl_err_buf);
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

                    if (0 > RV_parse_response(response_buffer.buffer,
                                              (void *)&loc_params->loc_data.loc_by_idx, &found_object_name,
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

                    search_ret = RV_find_object_by_path(loc_obj, found_object_name, &obj_type,
                                                        RV_copy_object_loc_info_callback,
                                                        &loc_obj->domain->u.file.server_info, &loc_info_out);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PATH, FAIL, "can't locate object by path");

                    if (RV_set_object_type_header(obj_type, &parent_obj_type_header) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL,
                                        "object at index not a group, datatype or dataset");

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

            if (CURLE_OK !=
                curl_easy_setopt(curl, CURLOPT_USERNAME, loc_obj->domain->u.file.server_info.username))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL username: %s", curl_err_buf);
            if (CURLE_OK !=
                curl_easy_setopt(curl, CURLOPT_PASSWORD, loc_obj->domain->u.file.server_info.password))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL password: %s", curl_err_buf);
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
    RV_free(loc_info_out.GCPL_base64);

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

    H5VL_loc_params_t *attr_loc_params  = NULL;
    H5I_type_t         iter_object_type = H5I_UNINIT;
    H5O_info2_t        oinfo;
    RV_object_t       *iter_object    = NULL;
    RV_object_t       *attr_object    = NULL;
    hid_t              iter_object_id = H5I_INVALID_HID;
    char               visit_by_name_URI[URI_MAX_LENGTH];
    char               request_url[URL_MAX_LENGTH];
    char              *host_header     = NULL;
    int                url_len         = 0;
    size_t             host_header_len = 0;

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
        case H5VL_OBJECT_VISIT: {
            const char *object_type_header = NULL;
            iter_data   object_iter_data;

            object_iter_data.index_type                   = args->args.visit.idx_type;
            object_iter_data.iter_order                   = args->args.visit.order;
            object_iter_data.oinfo_fields                 = args->args.visit.fields;
            object_iter_data.iter_function.object_iter_op = args->args.visit.op;
            object_iter_data.op_data                      = args->args.visit.op_data;
            object_iter_data.is_recursive                 = true;
            object_iter_data.idx_p                        = 0;

            if (loc_obj->obj_type == H5I_ATTR)
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, FAIL,
                                "H5Ovisit(_by_name) on an attribute is unsupported");

            if (!object_iter_data.iter_function.object_iter_op)
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "no object iteration function specified");

            switch (loc_params->type) {
                case (H5VL_OBJECT_BY_SELF): {

                    if (RV_set_object_type_header(loc_obj->obj_type, &object_type_header) < 0)
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "bad object type");

                    /* Since we already have the object, but still need an hid_t for it
                     * to pass to the user's object, we will just copy the current object, making sure to
                     * increment the ref. counts for the object's fields so that closing it at the end of
                     * this function does not close the fields themselves in the real object, such as a
                     * dataset's dataspace.
                     */

                    /* Increment refs for top-level file */
                    loc_obj->domain->u.file.ref_count++;

                    if ((iter_object = RV_calloc(sizeof(RV_object_t))) == NULL)
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTALLOC, FAIL,
                                        "couldn't allocate space for iteration object");

                    memcpy(iter_object, loc_obj, sizeof(RV_object_t));

                    if ((iter_object->handle_path = RV_malloc(strlen(loc_obj->handle_path) + 1)) == NULL)
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTALLOC, FAIL,
                                        "couldn't allocate space for iteration object path");

                    memcpy(iter_object->handle_path, loc_obj->handle_path, strlen(loc_obj->handle_path) + 1);

                    /* Increment refs for specific type */
                    switch (loc_obj->obj_type) {
                        case H5I_FILE:
                            /* Copy plists, filepath, and server info to new object */

                            if (H5I_INVALID_HID ==
                                (iter_object->u.file.fapl_id = H5Pcopy(loc_obj->u.file.fapl_id)))
                                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, FAIL, "can't copy FAPL");
                            if (H5I_INVALID_HID ==
                                (iter_object->u.file.fcpl_id = H5Pcopy(loc_obj->u.file.fcpl_id)))
                                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, FAIL, "can't copy FCPL");
                            if (NULL == (iter_object->u.file.filepath_name =
                                             RV_malloc(strlen(loc_obj->u.file.filepath_name) + 1)))
                                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL,
                                                "can't allocate space for copied filepath_name object");

                            strncpy(iter_object->u.file.filepath_name, loc_obj->u.file.filepath_name,
                                    strlen(loc_obj->u.file.filepath_name) + 1);

                            if ((iter_object->u.file.server_info.username =
                                     RV_malloc(strlen(loc_obj->u.file.server_info.username) + 1)) == NULL)
                                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL,
                                                "can't allocate space for copied username");

                            strncpy(iter_object->u.file.server_info.username,
                                    loc_obj->u.file.server_info.username,
                                    strlen(loc_obj->u.file.server_info.username) + 1);

                            if ((iter_object->u.file.server_info.password =
                                     RV_malloc(strlen(loc_obj->u.file.server_info.password) + 1)) == NULL)
                                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL,
                                                "can't allocate space for copied password");

                            strncpy(iter_object->u.file.server_info.password,
                                    loc_obj->u.file.server_info.password,
                                    strlen(loc_obj->u.file.server_info.password) + 1);

                            if ((iter_object->u.file.server_info.base_URL =
                                     RV_malloc(strlen(loc_obj->u.file.server_info.base_URL) + 1)) == NULL)
                                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL,
                                                "can't allocate space for copied URL");

                            strncpy(iter_object->u.file.server_info.base_URL,
                                    loc_obj->u.file.server_info.base_URL,
                                    strlen(loc_obj->u.file.server_info.base_URL) + 1);

                            /* This is a copy of the file, not a reference to the same memory */
                            loc_obj->domain->u.file.ref_count--;
                            iter_object->u.file.ref_count = 1;
                            iter_object_type              = H5I_FILE;
                            break;
                        case H5I_GROUP:
                            if (loc_obj->u.group.gcpl_id != H5P_GROUP_CREATE_DEFAULT) {
                                if (H5Iinc_ref(loc_obj->u.group.gcpl_id) < 0)
                                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL,
                                                    "can't increment field's ref. count for copy of "
                                                    "object");
                            }

                            iter_object_type = H5I_GROUP;
                            break;

                        case H5I_DATASET:

                            if (H5Iinc_ref(loc_obj->u.dataset.dtype_id) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL,
                                                "can't increment field's ref. count for copy of  "
                                                "dataset");
                            if (H5Iinc_ref(loc_obj->u.dataset.space_id) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL,
                                                "can't increment field's ref. count for copy of  "
                                                "dataset");
                            if (H5Iinc_ref(loc_obj->u.dataset.dapl_id) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL,
                                                "can't increment field's ref. count for copy of  "
                                                "dataset");
                            if (H5Iinc_ref(loc_obj->u.dataset.dcpl_id) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL,
                                                "can't increment field's ref. count for copy of  "
                                                "dataset");

                            iter_object_type = H5I_DATASET;
                            break;

                        case H5I_DATATYPE:
                            if (H5Iinc_ref(loc_obj->u.datatype.dtype_id) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL,
                                                "can't increment field's ref. count for copy of  "
                                                "datatype");
                            if (H5Iinc_ref(loc_obj->u.datatype.tcpl_id) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL,
                                                "can't increment field's ref. count for copy of  "
                                                "datatype");

                            iter_object_type = H5I_DATATYPE;
                            break;

                        case H5I_ATTR: {
                            FUNC_GOTO_ERROR(H5E_UNSUPPORTED, H5E_UNSUPPORTED, FAIL,
                                            "H5Ovisit on attribute is currently unsupported");
                            break;
                        } /* end H5Ovisit on H5I_ATTR case */

                        default:
                            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL,
                                            "invalid parent object type supplied for visit");
                            break;
                    } /* end switch for loc_obj->obj_type */

                    break;
                } /* end H5Ovisit H5VL_OBJECT_BY_SELF */

                case (H5VL_OBJECT_BY_NAME): {

                    if (H5I_INVALID_HID == loc_params->loc_data.loc_by_name.lapl_id)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid LAPL");

                    /* Make a request to figure out how to open iter object, set header string,
                     * and iter object type */
                    if (RV_find_object_by_path(loc_obj, loc_params->loc_data.loc_by_name.name,
                                               &iter_object_type, RV_copy_object_URI_callback, NULL,
                                               visit_by_name_URI) < 0) {

                        /* If object was not found by name, try to open it as an attribute */

                        if ((attr_loc_params = RV_calloc(sizeof(H5VL_loc_params_t))) == NULL)
                            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTALLOC, FAIL,
                                            "can't allocate memory for attribute loc params");

                        attr_loc_params->type = H5VL_OBJECT_BY_SELF;

                        if (NULL == (attr_object = RV_attr_open(loc_obj, attr_loc_params,
                                                                loc_params->loc_data.loc_by_name.name,
                                                                H5P_DEFAULT, H5P_DEFAULT, NULL)))

                            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL,
                                            "failed to get URI of visited object by name");

                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, FAIL,
                                        "H5Ovisit(_by_name) on attribute is currently unsupported");
                    }

                    if (RV_set_object_type_header(iter_object_type, &object_type_header) < 0)
                        FUNC_DONE_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL,
                                        "invalid object type provided to H5Ovisit_by_name");

                    switch (iter_object_type) {
                        case H5I_FILE:
                        case H5I_GROUP:
                            if (NULL == (iter_object = RV_group_open(loc_obj, loc_params,
                                                                     loc_params->loc_data.loc_by_name.name,
                                                                     H5P_DEFAULT, H5P_DEFAULT, NULL)))
                                FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTOPENOBJ, FAIL,
                                                "can't open object iteration group");

                            break;

                        case H5I_DATASET:
                            if (NULL == (iter_object = RV_dataset_open(loc_obj, loc_params,
                                                                       loc_params->loc_data.loc_by_name.name,
                                                                       H5P_DEFAULT, H5P_DEFAULT, NULL)))
                                FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTOPENOBJ, FAIL,
                                                "can't open object iteration dataset");

                            break;

                        case H5I_DATATYPE:
                            if (NULL == (iter_object = RV_datatype_open(loc_obj, loc_params,
                                                                        loc_params->loc_data.loc_by_name.name,
                                                                        H5P_DEFAULT, H5P_DEFAULT, NULL)))
                                FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTOPENOBJ, FAIL,
                                                "can't open object iteration dataset");

                            break;

                        case H5I_ATTR: {
                            /* The object where the attribute is attached will be iterated */
                            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, FAIL,
                                            "H5Ovisit on an attribute is unsupported");
                            break;
                        } /* end switch for attr parent type */

                        default:
                            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL,
                                            "invalid parent object type supplied for visit");
                            break;
                    }

                    break;
                } /* end H5Ovisit_by_name */
                case (H5VL_OBJECT_BY_IDX):
                case (H5VL_OBJECT_BY_TOKEN):
                    FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, FAIL, "invalid H5Ovisit type");
                    break;
            } /* end loc_params->type switch */

            /* To build object table, information about parent object will be needed */
            object_iter_data.iter_obj_parent = iter_object;

            if (url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/%s/%s",
                                   iter_object->domain->u.file.server_info.base_URL, object_type_header,
                                   object_iter_data.iter_obj_parent->URI) < 0)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                "H5Oiterate/visit request URL size exceeded maximum URL size");

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Registering hid_t for opened object iteration \n\n");
#endif

            /* Note: Because this might be the first call to register an ID for an object of iter_object_type,
             * it is possible that the H5X interface will be uninitialized at this point, which would lead
             * H5VLwrap_register to fail.
             * Therefore, we make a fake call to H5X_open to initialize the correct interface via the
             * FUNC_ENTER_API macro. */

            H5E_BEGIN_TRY
            {
                switch (iter_object_type) {

                    case (H5I_FILE):
                    case (H5I_GROUP):
                        H5Gopen2(H5I_INVALID_HID, NULL, H5P_DEFAULT);
                        break;

                    case (H5I_DATASET):
                        H5Dopen2(H5I_INVALID_HID, NULL, H5P_DEFAULT);
                        break;

                    case (H5I_DATATYPE):
                        H5Topen2(H5I_INVALID_HID, NULL, H5P_DEFAULT);
                        break;

                    default:
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, FAIL, "invalid H5Ovisit type");
                        break;
                }
            }
            H5E_END_TRY;

            /* Register an hid_t for the iteration object */
            if ((iter_object_id = H5VLwrap_register((void *)iter_object, iter_object_type)) < 0)
                FUNC_GOTO_ERROR(H5E_ID, H5E_CANTREGISTER, FAIL,
                                "can't create ID for object to be iterated over");
            object_iter_data.iter_obj_id = iter_object_id;

            /* Unlike H5Lvisit, H5Ovisit executes the provided callback on the directly specified object. */

            /* Make GET request to server */

            /* Setup the host header */
            host_header_len = strlen(object_iter_data.iter_obj_parent->domain->u.file.filepath_name) +
                              strlen(host_string) + 1;
            if (NULL == (host_header = (char *)RV_malloc(host_header_len)))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL,
                                "can't allocate space for request Host header");

            strcpy(host_header, host_string);

            curl_headers = curl_slist_append(
                curl_headers,
                strncat(host_header, object_iter_data.iter_obj_parent->domain->u.file.filepath_name,
                        host_header_len - strlen(host_string) - 1));

            /* Disable use of Expect: 100 Continue HTTP response */
            curl_headers = curl_slist_append(curl_headers, "Expect:");

            if (CURLE_OK !=
                curl_easy_setopt(curl, CURLOPT_USERNAME, loc_obj->domain->u.file.server_info.username))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL username: %s", curl_err_buf);
            if (CURLE_OK !=
                curl_easy_setopt(curl, CURLOPT_PASSWORD, loc_obj->domain->u.file.server_info.password))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL password: %s", curl_err_buf);
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf);
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP GET request: %s",
                                curl_err_buf);
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf);

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Retrieving all links in group using URL: %s\n\n", request_url);

            printf("   /**********************************\\\n");
            printf("-> | Making GET request to the server |\n");
            printf("   \\**********************************/\n\n");
#endif

            /* Do a first request to populate obj info in order to execute the callback on the top-level given
             * object */
            CURL_PERFORM(curl, H5E_LINK, H5E_CANTGET, FAIL);

            if (curl_headers) {
                curl_slist_free_all(curl_headers);
                curl_headers = NULL;
            }

            if (RV_parse_response(response_buffer.buffer, NULL, &oinfo, RV_get_object_info_callback) < 0)
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PARSEERROR, FAIL, "failed to get object info");

            herr_t callback_ret = SUCCEED;
            callback_ret        = object_iter_data.iter_function.object_iter_op(iter_object_id, ".", &oinfo,
                                                                                object_iter_data.op_data);

            if (callback_ret < 0) {
                FUNC_GOTO_ERROR(H5E_LINK, H5E_CALLBACK, callback_ret,
                                "H5Oiterate/H5Ovisit (_by_name) user callback failed for target object ");
            }
            else if (callback_ret > 0) {
                FUNC_GOTO_DONE(callback_ret);
            }

            /* Get recursion info */
            switch (iter_object_type) {
                case H5I_FILE:
                case H5I_GROUP:
                    if (url_len =
                            snprintf(request_url, URL_MAX_LENGTH, "%s/%s/%s%s",
                                     object_iter_data.iter_obj_parent->domain->u.file.server_info.base_URL,
                                     object_type_header, object_iter_data.iter_obj_parent->URI,
                                     (!strcmp(object_type_header, "groups") ? "/links" : "")) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                        "H5Oiterate/visit request URL size exceeded maximum URL size");

                    curl_headers = curl_slist_append(curl_headers, host_header);

                    /* Disable use of Expect: 100 Continue HTTP response */
                    curl_headers = curl_slist_append(curl_headers, "Expect:");

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

                    if (curl_headers) {
                        curl_slist_free_all(curl_headers);
                        curl_headers = NULL;
                    }

                    if (RV_parse_response(response_buffer.buffer, &object_iter_data, NULL,
                                          RV_object_iter_callback) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "can't iterate over links");
                    break;

                case H5I_DATASET:
                case H5I_DATATYPE:
                    /* No iteration */
                    break;

                default:
                    FUNC_GOTO_ERROR(H5E_OBJECT, H5E_UNSUPPORTED, FAIL, "invalid H5Ovisit type");
                    break;
            }
            break;
        } /* end H5Ovisit(_by_name)  */

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
    if (host_header)
        RV_free(host_header);

    RV_free(attr_loc_params);

    if (attr_object)
        RV_attr_close(attr_object, H5P_DEFAULT, NULL);

    if (iter_object_id != H5I_INVALID_HID) {
        switch (iter_object_type) {
            case H5I_FILE:
                if (H5Fclose(iter_object_id) < 0)
                    FUNC_DONE_ERROR(H5E_LINK, H5E_CANTCLOSEOBJ, FAIL, "can't close object visit group");
                break;
            case H5I_GROUP:
                if (H5Gclose(iter_object_id) < 0)
                    FUNC_DONE_ERROR(H5E_LINK, H5E_CANTCLOSEOBJ, FAIL, "can't close object visit group");
                break;
            case H5I_DATATYPE:
                if (H5Tclose(iter_object_id) < 0)
                    FUNC_DONE_ERROR(H5E_LINK, H5E_CANTCLOSEOBJ, FAIL, "can't close object visit datatype");
                break;
            case H5I_DATASET:
                if (H5Dclose(iter_object_id) < 0)
                    FUNC_DONE_ERROR(H5E_LINK, H5E_CANTCLOSEOBJ, FAIL, "can't close object visit dataset");
                break;
            default:
                break;
        }
    }
    else {
        /* If execution failed before the wrap, free the RV_object_t block directly*/
        if (iter_object) {
            if (args->op_type == H5VL_OBJECT_VISIT && loc_params->type == H5VL_OBJECT_BY_SELF) {
                RV_free(iter_object->handle_path);
            }
            RV_free(iter_object);
        }
    }

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

    /* Retrieve the object's class */
    switch (object_id[0]) {
        case 'd':
            obj_info->type = H5O_TYPE_DATASET;
            break;
        case 't':
            obj_info->type = H5O_TYPE_NAMED_DATATYPE;
            break;
        case 'g':
            obj_info->type = H5O_TYPE_GROUP;
            break;
        default:
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_BADVALUE, FAIL, "get object info called on invalid object type");
            break;
    }
done:
    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_get_object_info_callback() */

/*-------------------------------------------------------------------------
 * Function:    H5_rest_cmp_objects_by_creation_order_inc
 *
 * Purpose:     Qsort callback to sort objects by creation order; the objects
 *              will be sorted in increasing order of creation order.
 *
 * Return:      negative if the creation time of object1 is earlier than that
 *              of object2
 *              0 if the creation time of object1 and object2 are equal
 *              positive if the creation time of object1 is later than that
 *              of object2
 *
 * Programmer:  Matthew Larson
 *              May, 2023
 */
static int
H5_rest_cmp_objects_by_creation_order_inc(const void *object1, const void *object2)
{
    const object_table_entry *_object1 = (const object_table_entry *)object1;
    const object_table_entry *_object2 = (const object_table_entry *)object2;

    return ((_object1->crt_time > _object2->crt_time) - (_object1->crt_time < _object2->crt_time));
} /* end H5_rest_cmp_objects_by_creation_order_inc() */

/*-------------------------------------------------------------------------
 * Function:    RV_object_iter_callback
 *
 * Purpose:     A callback for RV_parse_response which will search an HTTP
 *              response for objects in a group and iterate through them,
 *              setting up an H5O_info2_t struct and calling the supplied
 *              callback function for each object. The callback_data_in
 *              parameter should be a iter_data struct *, containing all
 *              the data necessary for iteration, such as the callback
 *              function, iteration order, index type, etc.
 *
 *              Non-hard links are ignored.
 *
 *              If the same object is linked to multiple times, the
 *              callback function will only be executed on it once.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Matthew Larson
 *              May, 2023
 */
herr_t
RV_object_iter_callback(char *HTTP_response, void *callback_data_in, void *callback_data_out)
{
    object_table_entry *object_table             = NULL;
    rv_hash_table_t    *visited_link_table       = NULL;
    rv_hash_table_t    *visited_object_table     = NULL;
    iter_data          *object_iter_data         = (iter_data *)callback_data_in;
    size_t              object_table_num_entries = 0;
    herr_t              ret_value                = SUCCEED;
    char                URL[URL_MAX_LENGTH];

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Iterating recursively through objects according to server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL");
    if (!object_iter_data)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "object iteration data pointer was NULL");

    /* Because H5Ovisit is recursive, setup a hash table to keep track of visited links
     * so that cyclic links can be dealt with appropriately.
     */
    if (NULL == (visited_link_table = rv_hash_table_new(rv_hash_string, H5_rest_compare_string_keys)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL,
                        "can't allocate hash table for determining cyclic links");

    /* Since the JSON parse trees aren't persistent, the keys inserted into the visited link hash table
     * are RV_malloc()ed copies. Make sure to free these when freeing the table.
     */
    rv_hash_table_register_free_functions(visited_link_table, RV_free_visited_link_hash_table_key, NULL);

    /* Similarly, set up a hash table to keep track of which objects have had the callback executed on them.
     * Note that this needs to be a distinct table for cases where multiple links point at the same object.
     */
    if (NULL == (visited_object_table = rv_hash_table_new(rv_hash_string, H5_rest_compare_string_keys)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL,
                        "can't allocate hash table for determining repeat object links");

    rv_hash_table_register_free_functions(visited_object_table, RV_free_visited_link_hash_table_key, NULL);

    /* Build a table of all of the links in the given group */
    if (H5_INDEX_CRT_ORDER == object_iter_data->index_type) {
        /* This code assumes that links are returned in alphabetical order by default. If the user has
         * requested them by creation order, sort them this way while building the link table. If, in the
         * future, links are not returned in alphabetical order by default, this code should be changed to
         * reflect this.
         */
        if (RV_build_object_table(HTTP_response, true, H5_rest_cmp_objects_by_creation_order_inc,
                                  &object_table, &object_table_num_entries, object_iter_data,
                                  visited_link_table) < 0)
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTBUILDLINKTABLE, FAIL, "can't build link table");

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Object table sorted according to creation order\n\n");
#endif

    } /* end if */
    else {
        if (RV_build_object_table(HTTP_response, true, NULL, &object_table, &object_table_num_entries,
                                  object_iter_data, visited_link_table) < 0)
            FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTBUILDOBJECTTABLE, FAIL, "can't build object table");
    } /* end else */

    /* Begin iteration */
    if (object_table)
        if (RV_traverse_object_table(object_table, visited_object_table, object_table_num_entries,
                                     object_iter_data, NULL) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_OBJECTITERERROR, FAIL, "can't iterate over object table");

done:
    if (object_table)
        RV_free_object_table(object_table, object_table_num_entries);

    /* Free the visited link hash table if necessary */
    if (visited_link_table) {
        rv_hash_table_free(visited_link_table);
        visited_link_table = NULL;
    } /* end if */

    /* Free the visited object hash table if necessary */
    if (visited_object_table) {
        rv_hash_table_free(visited_object_table);
        visited_object_table = NULL;
    } /* end if */

    return ret_value;
} /* end RV_object_iter_callback */

/*-------------------------------------------------------------------------
 * Function:    RV_build_object_table
 *
 * Purpose:     Given an HTTP response that contains the information about
 *              all of the objects contained within a given group, this
 *              function builds a list of object_table_entry structs
 *              (defined near the top of this file), one for each object,
 *              which each contain the name of a link to the object,
 *              creation time, a link info H5L_info2_t struct,
 *              and an object info H5O_info2_t struct.
 *
 *              Each object_table_entry struct may additionally contain a
 *              pointer to another object table in the case that the link in
 *              question points to a subgroup of the parent group and a
 *              call to H5Ovisit has been made. H5Ovisit visits all the
 *              links under the given object and its subgroups, as opposed to
 *              H5Oiterate which only iterates over the objects in the given
 *              group.
 *
 *              This list is used during object iteration in order to supply
 *              the user's optional iteration callback function with all
 *              of the information it needs to process each object contained
 *              within a group (for H5Oiterate) or within a group and all
 *              of its subgroups (for H5Ovisit).
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Matthew Larson
 *              May, 2023
 */
herr_t
RV_build_object_table(char *HTTP_response, hbool_t is_recursive, int (*sort_func)(const void *, const void *),
                      object_table_entry **object_table, size_t *num_entries, iter_data *object_iter_data,
                      rv_hash_table_t *visited_link_table)
{
    object_table_entry *table      = NULL;
    yajl_val            parse_tree = NULL, key_obj;
    yajl_val            link_obj, link_field_obj;
    size_t              i, num_links;
    char               *HTTP_buffer  = HTTP_response;
    char               *visit_buffer = NULL;
    char               *link_section_start, *link_section_end;
    char               *url_encoded_link_name = NULL;
    char                request_url[URL_MAX_LENGTH];
    herr_t              ret_value   = SUCCEED;
    int                 url_len     = 0;
    H5I_type_t          obj_type    = H5I_UNINIT;
    char               *host_header = NULL;
    RV_object_t        *subgroup    = NULL;

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response was NULL");
    if (!object_table)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "link table pointer was NULL");
    if (!num_entries)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "link table num. entries pointer was NULL");
    if (is_recursive && !visited_link_table)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "visited link hash table was NULL");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Building table of objects %s\n\n", is_recursive ? "recursively" : "non-recursively");
#endif

    /* If this is a call to H5Ovisit, make a copy of the HTTP response since the
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
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL, "can't allocate space for object table");

    /* Find the beginning of the "links" section */
    if (NULL == (link_section_start = strstr(HTTP_buffer, "\"links\"")))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_PARSEERROR, FAIL,
                        "can't find \"links\" information section in HTTP response");

    /* For each link, grab its name and creation order, then find its corresponding JSON
     * subsection, place a NULL terminator at the end of it in order to "extract out" that
     * subsection, and pass it to the "get link info" callback function in order to fill
     * out a H5L_info2_t struct for the link. Then perform a get request to the server
     * to populate an H5O_info2_t struct for the object the link points to.
     */
    for (i = 0; i < num_links; i++) {
        char *link_name;

        link_obj = YAJL_GET_ARRAY(key_obj)->values[i];

        /* Get the current link's name */
        if (NULL == (link_field_obj = yajl_tree_get(link_obj, link_title_keys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "retrieval of link name failed");

        if (NULL == (link_name = YAJL_GET_STRING(link_field_obj)))
            FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "returned link name was NULL");

        if (strlen(link_name) + 1 > LINK_NAME_MAX_LENGTH)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL, "retrieved link name was too long");

        if (object_iter_data->iter_obj_parent->obj_type == H5I_FILE) {
            char abs_link_name[LINK_NAME_MAX_LENGTH];

            abs_link_name[0] = '/';
            abs_link_name[1] = '\0';
            strcat(abs_link_name, link_name);

            strncpy(table[i].link_name, abs_link_name, LINK_NAME_MAX_LENGTH);
        }
        else {
            strncpy(table[i].link_name, link_name, LINK_NAME_MAX_LENGTH);
        }

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

        /* Populate an H5O_info2_t struct for the object the link points to. */

        if (RV_find_object_by_path(object_iter_data->iter_obj_parent, link_name, &obj_type,
                                   RV_get_object_info_callback, NULL, &table[i].object_info) < 0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_PARSEERROR, FAIL, "can't parse object info while building table");

        /* Get the URI of the object the current link points to */
        switch (table[i].link_info.type) {
            case H5L_TYPE_HARD: {
                char  *object_URI = NULL;
                size_t id_len     = 0;

                if (NULL == (link_field_obj = yajl_tree_get(link_obj, object_id_keys, yajl_t_string)))
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL,
                                    "failed to parse object URI from hard link");

                if (NULL == (object_URI = YAJL_GET_STRING(link_field_obj)))
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_BADVALUE, FAIL,
                                    "object URI parsed from hard link was NULL");

                id_len = strlen(object_URI);

                if (id_len > URI_MAX_LENGTH - 1)
                    FUNC_DONE_ERROR(H5E_LINK, H5E_BADVALUE, FAIL,
                                    "parsed object URI exceeded maximum length!");

                memcpy(table[i].object_URI, object_URI, id_len + 1);

                break;
            }
            case H5L_TYPE_SOFT:
            case H5L_TYPE_EXTERNAL:
                /* For a symbolic link, get URI by path */
                if (RV_find_object_by_path(object_iter_data->iter_obj_parent, link_name, &obj_type,
                                           RV_copy_object_URI_callback, NULL, &table[i].object_URI) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL,
                                    "failed to get object info from link by path");

        } /* end switch on current link type */

        /*
         * If this is a call to H5Ovisit and the current link points to a group, hash the link object ID and
         * check to see if the key exists in the visited link hash table. If it does, this is a cyclic
         * link, so do not include it in the list of links. Otherwise, add it to the visited link hash
         * table and recursively process the group, building a link table for it as well.
         */

        table[i].subgroup.subgroup_object_table = NULL;
        if (is_recursive && (H5L_TYPE_HARD == table[i].link_info.type)) {
            char *link_collection;

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

                    if ((url_len =
                             snprintf(request_url, URL_MAX_LENGTH, "%s/groups/%s/links",
                                      object_iter_data->iter_obj_parent->domain->u.file.server_info.base_URL,
                                      url_encoded_link_name)) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                        "link GET request URL size exceeded maximum URL size");

                    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL request URL: %s",
                                        curl_err_buf);

                    /* Set up host header */
                    if (host_header) {
                        RV_free(host_header);
                        host_header = NULL;
                    }

                    if (curl_headers) {
                        curl_slist_free_all(curl_headers);
                        curl_headers = NULL;
                    }

                    size_t host_header_len =
                        strlen(object_iter_data->iter_obj_parent->domain->u.file.filepath_name) +
                        strlen(host_string) + 1;
                    if (NULL == (host_header = (char *)RV_malloc(host_header_len)))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL,
                                        "can't allocate space for request Host header");

                    strcpy(host_header, host_string);

                    curl_headers = curl_slist_append(
                        curl_headers,
                        strncat(host_header, object_iter_data->iter_obj_parent->domain->u.file.filepath_name,
                                host_header_len - strlen(host_string) - 1));

                    /* Disable use of Expect: 100 Continue HTTP response */
                    curl_headers = curl_slist_append(curl_headers, "Expect:");

                    if (CURLE_OK !=
                        curl_easy_setopt(
                            curl, CURLOPT_USERNAME,
                            object_iter_data->iter_obj_parent->domain->u.file.server_info.username))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL username: %s",
                                        curl_err_buf);
                    if (CURLE_OK !=
                        curl_easy_setopt(
                            curl, CURLOPT_PASSWORD,
                            object_iter_data->iter_obj_parent->domain->u.file.server_info.password))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL password: %s",
                                        curl_err_buf);
                    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s",
                                        curl_err_buf);
#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Retrieving all links in subgroup using URL: %s\n\n", request_url);

                    printf("   /**********************************\\\n");
                    printf("-> | Making GET request to the server |\n");
                    printf("   \\**********************************/\n\n");
#endif

                    CURL_PERFORM(curl, H5E_LINK, H5E_CANTGET, FAIL);

                    /* Use the group we are recursing into as the parent during the recursion */
                    if ((subgroup = RV_malloc(sizeof(RV_object_t))) == NULL)
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTALLOC, FAIL,
                                        "can't allocate memory for subgroup");

                    memcpy(subgroup->URI, table[i].object_URI, URI_MAX_LENGTH);
                    subgroup->domain          = object_iter_data->iter_obj_parent->domain;
                    subgroup->obj_type        = H5I_GROUP;
                    subgroup->u.group.gcpl_id = H5P_DEFAULT;
                    subgroup->u.group.gapl_id = H5P_DEFAULT;
                    if (RV_set_object_handle_path(link_name, object_iter_data->iter_obj_parent->handle_path,
                                                  &subgroup->handle_path) < 0)
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_PATH, FAIL, "can't set up object path");

                    object_iter_data->iter_obj_parent->domain->u.file.ref_count++;

                    iter_data subtable_iter_data;

                    memcpy(&subtable_iter_data, object_iter_data, sizeof(iter_data));

                    subtable_iter_data.iter_obj_parent = subgroup;

                    if (RV_build_object_table(
                            response_buffer.buffer, true, sort_func, &table[i].subgroup.subgroup_object_table,
                            &table[i].subgroup.num_entries, &subtable_iter_data, visited_link_table) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTBUILDLINKTABLE, FAIL,
                                        "can't build link table for subgroup '%s'", table[i].link_name);

                    if (url_encoded_link_name) {
                        curl_free(url_encoded_link_name);
                        url_encoded_link_name = NULL;
                    }

                    if (subgroup) {
                        RV_group_close(subgroup, H5P_DEFAULT, NULL);
                        subgroup = NULL;
                    }

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

        if (host_header) {
            RV_free(host_header);
            host_header = NULL;
        }
        if (curl_headers) {
            curl_slist_free_all(curl_headers);
            curl_headers = NULL;
        }
    } /* end for */

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Link table built\n\n");
#endif

    if (sort_func)
        qsort(table, num_links, sizeof(*table), sort_func);

done:
    if (ret_value >= 0) {
        if (object_table)
            *object_table = table;
        if (num_entries)
            *num_entries = num_links;
    } /* end if */

    if (ret_value < 0)
        RV_free(table);

    if (subgroup)
        RV_group_close(subgroup, H5P_DEFAULT, NULL);
    if (url_encoded_link_name)
        curl_free(url_encoded_link_name);
    if (parse_tree)
        yajl_tree_free(parse_tree);
    if (visit_buffer)
        RV_free(visit_buffer);
    if (host_header)
        RV_free(host_header);
    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    }

    return ret_value;
} /* end RV_build_object_table */

/*-------------------------------------------------------------------------
 * Function:    RV_free_object_table
 *
 * Purpose:     Helper function to free a built up object table, freeing its
 *              individual subgroup object tables as necessary
 *
 * Return:      Nothing
 *
 * Programmer:  Matthew Larson
 *              May, 2023
 */
static void
RV_free_object_table(object_table_entry *object_table, size_t num_entries)
{
    for (size_t i = 0; i < num_entries; i++) {
        if (object_table[i].subgroup.subgroup_object_table)
            RV_free_object_table(object_table[i].subgroup.subgroup_object_table,
                                 object_table[i].subgroup.num_entries);
    }

    RV_free(object_table);
} /* end RV_free_object_table*/

/*-------------------------------------------------------------------------
 * Function:    RV_traverse_object_table
 *
 * Purpose:     Helper function to actually iterate over an object
 *              table, calling the user's callback for each object
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Matthew Larson
 *              May, 2023
 */
static herr_t
RV_traverse_object_table(object_table_entry *object_table, rv_hash_table_t *visited_object_table,
                         size_t num_entries, iter_data *object_iter_data, const char *cur_object_rel_path)
{
    herr_t        ret_value = SUCCEED;
    static size_t depth     = 0;
    size_t        last_idx;
    herr_t        callback_ret;
    size_t        object_rel_path_len =
        (cur_object_rel_path ? strlen(cur_object_rel_path) : 0) + LINK_NAME_MAX_LENGTH + 2;
    char *object_rel_path = NULL;
    int   snprintf_ret    = 0;

    if (NULL == (object_rel_path = (char *)RV_malloc(object_rel_path_len)))
        FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTALLOC, FAIL,
                        "can't allocate space for link's relative pathname buffer");

    switch (object_iter_data->iter_order) {
        case H5_ITER_NATIVE:
        case H5_ITER_INC: {
#ifdef RV_CONNECTOR_DEBUG
            printf("-> Beginning iteration in increasing order\n\n");
#endif

            for (last_idx = (object_iter_data->idx_p ? *object_iter_data->idx_p : 0); last_idx < num_entries;
                 last_idx++) {
#ifdef RV_CONNECTOR_DEBUG
                printf("-> Link %zu name: %s\n", last_idx, object_table[last_idx].link_name);
                printf("-> Link %zu creation time: %f\n", last_idx, object_table[last_idx].crt_time);
                printf("-> Link %zu type: %s\n\n", last_idx,
                       link_class_to_string(object_table[last_idx].link_info.type));
#endif

                /* Form the link's relative path from the parent group by combining the current relative path
                 * with the link's name */
                if ((snprintf_ret =
                         snprintf(object_rel_path, object_rel_path_len, "%s%s%s",
                                  cur_object_rel_path ? cur_object_rel_path : "",
                                  cur_object_rel_path ? "/" : "", object_table[last_idx].link_name)) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                if ((size_t)snprintf_ret >= object_rel_path_len)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                    "link's relative path string size exceeded allocated buffer size");

#ifdef RV_CONNECTOR_DEBUG
                printf("-> Calling supplied callback function with relative link path %s\n\n",
                       object_rel_path);
#endif

                /* If object is an unvisited hard link, execute callback and add to visited table. */
                if ((object_table[last_idx].link_info.type == H5L_TYPE_HARD) &&
                    (RV_HASH_TABLE_NULL ==
                     rv_hash_table_lookup(visited_object_table, object_table[last_idx].object_URI))) {

                    callback_ret = object_iter_data->iter_function.object_iter_op(
                        object_iter_data->iter_obj_id, object_rel_path, &object_table[last_idx].object_info,
                        object_iter_data->op_data);
                    if (callback_ret < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CALLBACK, callback_ret,
                                        "H5Oiterate/H5Ovisit (_by_name) user callback failed for object '%s'",
                                        object_table[last_idx].link_name);
                    else if (callback_ret > 0)
                        FUNC_GOTO_DONE(callback_ret);

                    char *uri_hash_copy = NULL;

                    if (NULL == (uri_hash_copy = RV_malloc(strlen(object_table[last_idx].object_URI) + 1)))
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTALLOC, FAIL,
                                        "couldn't allocate memory for URI copy");

                    if (NULL == memcpy(uri_hash_copy, object_table[last_idx].object_URI,
                                       strlen(object_table[last_idx].object_URI)))
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "couldn't copy object URI");

                    uri_hash_copy[strlen(object_table[last_idx].object_URI)] = '\0';

                    if (!rv_hash_table_insert(visited_object_table, uri_hash_copy, uri_hash_copy))
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTINSERT, FAIL,
                                        "unable to insert key into visited link hash table");
                }

                /* If this is a group and H5Ovisit has been called, descend into the group */
                if (object_table[last_idx].subgroup.subgroup_object_table) {
#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Descending into subgroup '%s'\n\n", object_table[last_idx].link_name);
#endif

                    depth++;
                    if (RV_traverse_object_table(object_table[last_idx].subgroup.subgroup_object_table,
                                                 visited_object_table,
                                                 object_table[last_idx].subgroup.num_entries,
                                                 object_iter_data, object_rel_path) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_OBJECTITERERROR, FAIL,
                                        "can't iterate over links in subgroup '%s'",
                                        object_table[last_idx].link_name);
                    depth--;

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Exiting subgroup '%s'\n\n", object_table[last_idx].link_name);
#endif
                } /* end if */
                else {
                    char *last_slash = strrchr(object_rel_path, '/');

                    /* Truncate the relative path buffer by cutting off the trailing link name from the
                     * current path chain */
                    if (last_slash)
                        *last_slash = '\0';

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Relative link path after truncating trailing link name: %s\n\n",
                           object_rel_path);
#endif
                } /* end else */
            }     /* end for */

            break;
        } /* H5_ITER_NATIVE H5_ITER_INC */

        case H5_ITER_DEC: {
#ifdef RV_CONNECTOR_DEBUG
            printf("-> Beginning iteration in decreasing order\n\n");
#endif

            for (last_idx = (object_iter_data->idx_p ? *object_iter_data->idx_p : num_entries - 1);
                 last_idx >= 0; last_idx--) {
#ifdef RV_CONNECTOR_DEBUG
                printf("-> Link %zu name: %s\n", last_idx, object_table[last_idx].link_name);
                printf("-> Link %zu creation time: %f\n", last_idx, object_table[last_idx].crt_time);
                printf("-> Link %zu type: %s\n\n", last_idx,
                       link_class_to_string(object_table[last_idx].link_info.type));
#endif

                /* Form the link's relative path from the parent group by combining the current relative path
                 * with the link's name */
                if ((snprintf_ret =
                         snprintf(object_rel_path, object_rel_path_len, "%s%s%s",
                                  cur_object_rel_path ? cur_object_rel_path : "",
                                  cur_object_rel_path ? "/" : "", object_table[last_idx].link_name)) < 0)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL, "snprintf error");

                if ((size_t)snprintf_ret >= object_rel_path_len)
                    FUNC_GOTO_ERROR(H5E_LINK, H5E_SYSERRSTR, FAIL,
                                    "link's relative path string size exceeded allocated buffer size");

#ifdef RV_CONNECTOR_DEBUG
                printf("-> Calling supplied callback function with relative link path %s\n\n",
                       object_rel_path);
#endif

                /* If object is an unvisited hard link, execute callback and add to visited table. */
                if ((object_table[last_idx].link_info.type == H5L_TYPE_HARD) &&
                    (RV_HASH_TABLE_NULL ==
                     rv_hash_table_lookup(visited_object_table, object_table[last_idx].object_URI))) {

                    callback_ret = object_iter_data->iter_function.object_iter_op(
                        object_iter_data->iter_obj_id, object_rel_path, &object_table[last_idx].object_info,
                        object_iter_data->op_data);
                    if (callback_ret < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_CALLBACK, callback_ret,
                                        "H5Oiterate/H5Ovisit (_by_name) user callback failed for object '%s'",
                                        object_table[last_idx].link_name);
                    else if (callback_ret > 0)
                        FUNC_GOTO_DONE(callback_ret);

                    char *uri_hash_copy = NULL;

                    if (NULL == (uri_hash_copy = RV_malloc(strlen(object_table[last_idx].object_URI) + 1)))
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTALLOC, FAIL,
                                        "couldn't allocate memory for URI copy");

                    if (NULL == memcpy(uri_hash_copy, object_table[last_idx].object_URI,
                                       strlen(object_table[last_idx].object_URI) + 1))
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_SYSERRSTR, FAIL, "couldn't copy object URI");

                    uri_hash_copy[strlen(object_table[last_idx].object_URI)] = '\0';

                    if (!rv_hash_table_insert(visited_object_table, uri_hash_copy, uri_hash_copy))
                        FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTINSERT, FAIL,
                                        "unable to insert key into visited link hash table");
                }

                /* If this is a group and H5Ovisit has been called, descend into the group */
                if (object_table[last_idx].subgroup.subgroup_object_table) {
#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Descending into subgroup '%s'\n\n", object_table[last_idx].link_name);
#endif

                    depth++;
                    if (RV_traverse_object_table(object_table[last_idx].subgroup.subgroup_object_table,
                                                 visited_object_table,
                                                 object_table[last_idx].subgroup.num_entries,
                                                 object_iter_data, object_rel_path) < 0)
                        FUNC_GOTO_ERROR(H5E_LINK, H5E_OBJECTITERERROR, FAIL,
                                        "can't iterate over links in subgroup '%s'",
                                        object_table[last_idx].link_name);
                    depth--;

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Exiting subgroup '%s'\n\n", object_table[last_idx].link_name);
#endif
                } /* end if */
                else {
                    char *last_slash = strrchr(object_rel_path, '/');

                    /* Truncate the relative path buffer by cutting off the trailing link name from the
                     * current path chain */
                    if (last_slash)
                        *last_slash = '\0';

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Relative link path after truncating trailing link name: %s\n\n",
                           object_rel_path);
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
    if (object_iter_data->idx_p && (ret_value >= 0) && (depth == 0))
        *object_iter_data->idx_p = last_idx;

    if (object_rel_path)
        RV_free(object_rel_path);

    return ret_value;
} /* end RV_traverse_object_table */
