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
 * Implementations of the attribute callbacks for the HDF5 REST VOL connector.
 */

#include "rest_vol_attr.h"

/* Set of callbacks for RV_parse_response() */
static herr_t RV_get_attr_info_callback(char *HTTP_response, const void *callback_data_in,
                                        void *callback_data_out);
static herr_t RV_attr_iter_callback(char *HTTP_response, const void *callback_data_in,
                                    void *callback_data_out);

/* Helper functions to work with a table of attributes for attribute iteration */
static herr_t RV_build_attr_table(char *HTTP_response, hbool_t                                     sort,
                                  int (*sort_func)(const void *, const void *), attr_table_entry **attr_table,
                                  size_t *num_entries);
static herr_t RV_traverse_attr_table(attr_table_entry *attr_table, size_t num_entries,
                                     const iter_data *iter_data);

/* Qsort callback to sort attributes by creation order */
static int cmp_attributes_by_creation_order(const void *attr1, const void *attr2);

/* JSON keys to retrieve all of the information from an object when doing attribute iteration */
const char *attr_name_keys[]          = {"name", (const char *)0};
const char *attr_creation_time_keys[] = {"created", (const char *)0};

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
void *
RV_attr_create(void *obj, const H5VL_loc_params_t *loc_params, const char *attr_name, hid_t type_id,
               hid_t space_id, hid_t acpl_id, hid_t aapl_id, hid_t dxpl_id, void **req)
{
    RV_object_t *parent        = (RV_object_t *)obj;
    RV_object_t *new_attribute = NULL;
    upload_info  uinfo;
    size_t       create_request_nalloc = 0;
    size_t       datatype_body_len     = 0;
    size_t       attr_name_len         = 0;
    size_t       path_size             = 0;
    size_t       path_len              = 0;
    htri_t       search_ret;
    char        *create_request_body = NULL;
    char        *datatype_body       = NULL;
    char        *shape_body          = NULL;
    char         request_endpoint[URL_MAX_LENGTH];
    const char  *parent_obj_type_header  = NULL;
    char        *url_encoded_attr_name   = NULL;
    int          create_request_body_len = 0;
    int          url_len                 = 0;
    void        *ret_value               = NULL;
    long         http_response           = -1;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received attribute create call with following parameters:\n");

    if (H5VL_OBJECT_BY_NAME == loc_params->type) {
        printf("     - H5Acreate variant: H5Acreate_by_name\n");
        printf("     - loc_id object's URI: %s\n", parent->URI);
        printf("     - loc_id object's type: %s\n", object_type_to_string(parent->obj_type));
        printf("     - loc_id object's domain path: %s\n", parent->domain->u.file.filepath_name);
        printf("     - Path to object that attribute is to be attached to: %s\n",
               loc_params->loc_data.loc_by_name.name);
    } /* end if */
    else {
        printf("     - H5Acreate variant: H5Acreate2\n");
        printf("     - New attribute's parent object URI: %s\n", parent->URI);
        printf("     - New attribute's parent object type: %s\n", object_type_to_string(parent->obj_type));
        printf("     - New attribute's parent object domain path: %s\n",
               parent->domain->u.file.filepath_name);
    } /* end else */

    printf("     - New attribute's name: %s\n", attr_name);
    printf("     - Default ACPL? %s\n\n", (H5P_ATTRIBUTE_CREATE_DEFAULT == acpl_id) ? "yes" : "no");
#endif

    if (H5I_FILE != parent->obj_type && H5I_GROUP != parent->obj_type && H5I_DATATYPE != parent->obj_type &&
        H5I_DATASET != parent->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object not a file, group, datatype or dataset");

    if (!parent->domain->u.file.server_info.base_URL)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object does not have valid server URL");

    /* Check for write access */
    if (!(parent->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, NULL, "no write intent on file");

    if (aapl_id == H5I_INVALID_HID)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "invalid AAPL");

    /* Allocate and setup internal Attribute struct */
    if (NULL == (new_attribute = (RV_object_t *)RV_malloc(sizeof(*new_attribute))))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL, "can't allocate space for attribute object");

    new_attribute->URI[0]                = '\0';
    new_attribute->obj_type              = H5I_ATTR;
    new_attribute->u.attribute.dtype_id  = FAIL;
    new_attribute->u.attribute.space_id  = FAIL;
    new_attribute->u.attribute.aapl_id   = FAIL;
    new_attribute->u.attribute.acpl_id   = FAIL;
    new_attribute->u.attribute.attr_name = NULL;

    new_attribute->domain = parent->domain;
    parent->domain->u.file.ref_count++;

    new_attribute->handle_path = NULL;

    if (RV_set_object_handle_path(attr_name, parent->handle_path, &new_attribute->handle_path) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PATH, NULL, "can't set up object path");

    new_attribute->u.attribute.parent_name = NULL;

    if (parent->handle_path) {
        if ((new_attribute->u.attribute.parent_name = RV_malloc(strlen(parent->handle_path) + 1)) == NULL)
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL, "can't allocate space for attribute parent name");

        strncpy(new_attribute->u.attribute.parent_name, parent->handle_path, strlen(parent->handle_path) + 1);
    }

    /* If this is a call to H5Acreate_by_name, locate the real parent object */
    if (H5VL_OBJECT_BY_NAME == loc_params->type) {

        if (H5I_INVALID_HID == loc_params->loc_data.loc_by_name.lapl_id)
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, NULL, "invalid LAPL");

        new_attribute->u.attribute.parent_obj_type = H5I_UNINIT;

        search_ret = RV_find_object_by_path(
            parent, loc_params->loc_data.loc_by_name.name, &new_attribute->u.attribute.parent_obj_type,
            RV_copy_object_URI_callback, NULL, new_attribute->u.attribute.parent_obj_URI);
        if (!search_ret || search_ret < 0)
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_PATH, NULL,
                            "can't locate object that attribute is to be attached to");

#ifdef RV_CONNECTOR_DEBUG
        printf("-> H5Acreate_by_name(): found attribute's parent object by given path\n");
        printf("-> H5Acreate_by_name(): new attribute's parent object URI: %s\n",
               new_attribute->u.attribute.parent_obj_URI);
        printf("-> H5Acreate_by_name(): new attribute's parent object type: %s\n\n",
               object_type_to_string(new_attribute->u.attribute.parent_obj_type));
#endif
    } /* end if */
    else {
        if (H5VL_OBJECT_BY_IDX == loc_params->type) {

            if (H5I_INVALID_HID == loc_params->loc_data.loc_by_idx.lapl_id)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, NULL, "invalid LAPL");
        }

        new_attribute->u.attribute.parent_obj_type = parent->obj_type;
        strncpy(new_attribute->u.attribute.parent_obj_URI, parent->URI, URI_MAX_LENGTH);
    } /* end else */

    /* See HSDS#223 */
    if ((H5I_DATATYPE == new_attribute->u.attribute.parent_obj_type) &&
        !(SERVER_VERSION_MATCHES_OR_EXCEEDS(parent->domain->u.file.server_info.version, 0, 8, 0)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_UNSUPPORTED, NULL,
                        "server versions before 0.8.0 cannot properly create attributes on datatypes");

    /* Copy the AAPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * attribute access property lists functions will function correctly
     */
    if (H5P_ATTRIBUTE_ACCESS_DEFAULT != aapl_id) {
        if ((new_attribute->u.attribute.aapl_id = H5Pcopy(aapl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy AAPL");
    } /* end if */
    else
        new_attribute->u.attribute.aapl_id = H5P_ATTRIBUTE_ACCESS_DEFAULT;

    /* Copy the ACPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * H5Aget_create_plist() will function correctly
     */
    if (H5P_ATTRIBUTE_CREATE_DEFAULT != acpl_id) {
        if ((new_attribute->u.attribute.acpl_id = H5Pcopy(acpl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy ACPL");
    } /* end if */
    else
        new_attribute->u.attribute.acpl_id = H5P_ATTRIBUTE_CREATE_DEFAULT;

    /* Copy the datatype and dataspace IDs into the internal struct for the Attribute */
    if ((new_attribute->u.attribute.dtype_id = H5Tcopy(type_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCOPY, NULL, "failed to copy attribute's datatype");
    if ((new_attribute->u.attribute.space_id = H5Scopy(space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCOPY, NULL, "failed to copy attribute's dataspace");

    /* Copy the attribute's name */
    attr_name_len = strlen(attr_name);
    if (NULL == (new_attribute->u.attribute.attr_name = (char *)RV_malloc(attr_name_len + 1)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL, "can't allocate space for copy of attribute's name");
    memcpy(new_attribute->u.attribute.attr_name, attr_name, attr_name_len);
    new_attribute->u.attribute.attr_name[attr_name_len] = '\0';

    /* Form the request body to give the new Attribute its properties */

    /* Form the Datatype portion of the Attribute create request */
    if (RV_convert_datatype_to_JSON(type_id, &datatype_body, &datatype_body_len, FALSE,
                                    parent->domain->u.file.server_info.version) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, NULL,
                        "can't convert attribute's datatype to JSON representation");

    /* If the Dataspace of the Attribute was specified, convert it to JSON. Otherwise, use defaults */
    if (H5P_DEFAULT != space_id)
        if (RV_convert_dataspace_shape_to_JSON(space_id, &shape_body, NULL) < 0)
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCONVERT, NULL,
                            "can't convert attribute's dataspace to JSON representation");

    create_request_nalloc = strlen(datatype_body) + (shape_body ? strlen(shape_body) : 0) + 4;
    if (NULL == (create_request_body = (char *)RV_malloc(create_request_nalloc)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL,
                        "can't allocate space for attribute create request body");

    if ((create_request_body_len =
             snprintf(create_request_body, create_request_nalloc, "{%s%s%s}", datatype_body,
                      shape_body ? "," : "", shape_body ? shape_body : "")) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL, "snprintf error");

    if ((size_t)create_request_body_len >= create_request_nalloc)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL,
                        "attribute create request body size exceeded allocated buffer size");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Attribute create request JSON:\n%s\n\n", create_request_body);
#endif

    /* URL-encode the attribute name to ensure that the resulting URL for the creation
     * operation contains no illegal characters
     */
    if (NULL == (url_encoded_attr_name = curl_easy_escape(curl, attr_name, (int)attr_name_len)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTENCODE, NULL, "can't URL-encode attribute name");

    /* Redirect cURL from the base URL to
     * "/groups/<id>/attributes/<attr name>",
     * "/datatypes/<id>/attributes/<attr name>"
     * or
     * "/datasets/<id>/attributes/<attr name>",
     * depending on the type of the object the attribute is being attached to. */
    if (RV_set_object_type_header(new_attribute->u.attribute.parent_obj_type, &parent_obj_type_header) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, NULL, "parent object not a group, datatype or dataset");

    if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/%s/%s/attributes/%s", parent_obj_type_header,
                            new_attribute->u.attribute.parent_obj_URI, url_encoded_attr_name)) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL, "snprintf error");

    if (url_len >= URL_MAX_LENGTH)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL, "attribute create URL exceeded maximum URL size");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> URL for attribute creation request: %s\n\n", request_endpoint);
#endif

    uinfo.buffer      = create_request_body;
    uinfo.buffer_size = (size_t)create_request_body_len;
    uinfo.bytes_sent  = 0;

    http_response = RV_curl_put(curl, &new_attribute->domain->u.file.server_info, request_endpoint,
                                new_attribute->domain->u.file.filepath_name, &uinfo, CONTENT_TYPE_JSON);
    if (!HTTP_SUCCESS(http_response))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTCREATE, NULL, "can't create attribute");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Created attribute\n\n");
#endif

    if (rv_hash_table_insert(RV_type_info_array_g[H5I_ATTR]->table, (char *)new_attribute->URI,
                             (char *)new_attribute) == 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL, "Failed to add attribute to type info array");

    ret_value = (void *)new_attribute;

done:
#ifdef RV_CONNECTOR_DEBUG
    printf("-> Attribute create response buffer:\n%s\n\n", response_buffer.buffer);

    if (new_attribute && ret_value) {
        printf("-> New attribute's info:\n");
        printf("     - New attribute's object type: %s\n", object_type_to_string(new_attribute->obj_type));
        printf("     - New attribute's domain path: %s\n", new_attribute->domain->u.file.filepath_name);
        printf("     - New attribute's name: %s\n", new_attribute->u.attribute.attr_name);
        printf("     - New attribute's datatype class: %s\n\n",
               datatype_class_to_string(new_attribute->u.attribute.dtype_id));
    } /* end if */
#endif

    if (create_request_body)
        RV_free(create_request_body);
    if (datatype_body)
        RV_free(datatype_body);
    if (shape_body)
        RV_free(shape_body);
    if (url_encoded_attr_name)
        curl_free(url_encoded_attr_name);

    /* Clean up allocated attribute object if there was an issue */
    if (new_attribute && !ret_value)
        if (RV_attr_close(new_attribute, FAIL, NULL) < 0)
            FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTCLOSEOBJ, NULL, "can't close attribute");

    PRINT_ERROR_STACK;

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
void *
RV_attr_open(void *obj, const H5VL_loc_params_t *loc_params, const char *attr_name, hid_t aapl_id,
             hid_t dxpl_id, void **req)
{
    RV_object_t *parent          = (RV_object_t *)obj;
    RV_object_t *attribute       = NULL;
    size_t       attr_name_len   = 0;
    size_t       path_size       = 0;
    size_t       path_len        = 0;
    char        *found_attr_name = NULL;
    char         request_endpoint[URL_MAX_LENGTH];
    char        *url_encoded_attr_name  = NULL;
    const char  *parent_obj_type_header = NULL;
    int          url_len                = 0;
    void        *ret_value              = NULL;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received attribute open call with following parameters:\n");

    if (H5VL_OBJECT_BY_NAME == loc_params->type) {
        printf("     - H5Aopen variant: H5Aopen_by_name\n");
        printf("     - loc_id object's URI: %s\n", parent->URI);
        printf("     - loc_id object's type: %s\n", object_type_to_string(parent->obj_type));
        printf("     - loc_id object's domain path: %s\n", parent->domain->u.file.filepath_name);
        printf("     - Path to object that attribute is attached to: %s\n",
               loc_params->loc_data.loc_by_name.name);
    } /* end if */
    else if (H5VL_OBJECT_BY_IDX == loc_params->type) {
        printf("     - H5Aopen variant: H5Aopen_by_idx\n");
    } /* end else if */
    else {
        printf("     - H5Aopen variant: H5Aopen\n");
        printf("     - Attribute's parent object URI: %s\n", parent->URI);
        printf("     - Attribute's parent object type: %s\n", object_type_to_string(parent->obj_type));
        printf("     - Attribute's parent object domain path: %s\n", parent->domain->u.file.filepath_name);
    } /* end else */

    if (attr_name)
        printf("     - Attribute's name: %s\n", attr_name);
    printf("\n");
#endif

    if (H5I_FILE != parent->obj_type && H5I_GROUP != parent->obj_type && H5I_DATATYPE != parent->obj_type &&
        H5I_DATASET != parent->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object not a file, group, datatype or dataset");

    if (!parent->domain->u.file.server_info.base_URL)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object does not have valid server URL");

    if (aapl_id == H5I_INVALID_HID)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "invalid AAPL");

    /* Allocate and setup internal Attribute struct */
    if (NULL == (attribute = (RV_object_t *)RV_malloc(sizeof(*attribute))))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL, "can't allocate space for attribute object");

    attribute->URI[0]                      = '\0';
    attribute->obj_type                    = H5I_ATTR;
    attribute->u.attribute.dtype_id        = FAIL;
    attribute->u.attribute.space_id        = FAIL;
    attribute->u.attribute.aapl_id         = FAIL;
    attribute->u.attribute.acpl_id         = FAIL;
    attribute->u.attribute.attr_name       = NULL;
    attribute->u.attribute.parent_obj_type = H5I_UNINIT;

    attribute->domain = parent->domain;
    parent->domain->u.file.ref_count++;

    attribute->handle_path = NULL;

    if (RV_set_object_handle_path(attr_name, parent->handle_path, &attribute->handle_path) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PATH, NULL, "can't set up object path");

    attribute->u.attribute.parent_name = NULL;

    if (parent->handle_path) {
        if ((attribute->u.attribute.parent_name = RV_malloc(strlen(parent->handle_path) + 1)) == NULL)
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL, "can't allocate space for attribute parent name");

        strncpy(attribute->u.attribute.parent_name, parent->handle_path, strlen(parent->handle_path) + 1);
    }

    /* Set the parent object's type and URI in the attribute's appropriate fields */
    switch (loc_params->type) {
        /* H5Aopen */
        case H5VL_OBJECT_BY_SELF: {
            attribute->u.attribute.parent_obj_type = parent->obj_type;
            strncpy(attribute->u.attribute.parent_obj_URI, parent->URI, URI_MAX_LENGTH);
            break;
        } /* H5VL_OBJECT_BY_SELF */

        /* H5Aopen_by_name */
        case H5VL_OBJECT_BY_NAME: {

            if (H5I_INVALID_HID == loc_params->loc_data.loc_by_name.lapl_id)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, NULL, "invalid LAPL");

            htri_t search_ret;

            /* If this is a call to H5Aopen_by_name, locate the real object that the attribute
             * is attached to by searching the given path
             */

            attribute->u.attribute.parent_obj_type = H5I_UNINIT;

            /* External links to attributes are not supported, so there is no need to use
             * a callback that checks for a different domain */
            search_ret = RV_find_object_by_path(
                parent, loc_params->loc_data.loc_by_name.name, &attribute->u.attribute.parent_obj_type,
                RV_copy_object_URI_callback, NULL, attribute->u.attribute.parent_obj_URI);
            if (!search_ret || search_ret < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_PATH, NULL,
                                "can't locate object that attribute is attached to");

#ifdef RV_CONNECTOR_DEBUG
            printf("-> H5Aopen_by_name(): found attribute's parent object by given path\n");
            printf("-> H5Aopen_by_name(): attribute's parent object URI: %s\n",
                   attribute->u.attribute.parent_obj_URI);
            printf("-> H5Aopen_by_name(): attribute's parent object type: %s\n\n",
                   object_type_to_string(attribute->u.attribute.parent_obj_type));
#endif

            break;
        } /* H5VL_OBJECT_BY_NAME */

        /* H5Aopen_by_idx */
        case H5VL_OBJECT_BY_IDX: {

            if (H5I_INVALID_HID == loc_params->loc_data.loc_by_idx.lapl_id)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, NULL, "invalid LAPL");

            htri_t search_ret;

            const char *request_idx_type = NULL;

            switch (loc_params->loc_data.loc_by_idx.idx_type) {
                case (H5_INDEX_CRT_ORDER):
                    if (SERVER_VERSION_MATCHES_OR_EXCEEDS(parent->domain->u.file.server_info.version, 0, 8,
                                                          0)) {
                        request_idx_type = "&CreateOrder=1";
                    }
                    else {
                        FUNC_GOTO_ERROR(
                            H5E_ATTR, H5E_UNSUPPORTED, NULL,
                            "indexing by creation order not supported by server versions before 0.8.0");
                    }

                    break;
                case (H5_INDEX_NAME):
                    request_idx_type = "";
                    break;
                case (H5_INDEX_N):
                case (H5_INDEX_UNKNOWN):
                default:
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL, "unsupported index type specified");
                    break;
            }

            /* Make additional request to server to determine attribute name by index */
            if (!strcmp(loc_params->loc_data.loc_by_idx.name, ".")) {
                attribute->u.attribute.parent_obj_type = parent->obj_type;
                strncpy(attribute->u.attribute.parent_obj_URI, parent->URI, URI_MAX_LENGTH);
            }
            else {
                search_ret = RV_find_object_by_path(
                    parent, loc_params->loc_data.loc_by_idx.name, &attribute->u.attribute.parent_obj_type,
                    RV_copy_object_URI_callback, NULL, attribute->u.attribute.parent_obj_URI);

                if (!search_ret || search_ret < 0)
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_PATH, NULL,
                                    "can't locate object that attribute is attached to");
            }

            /* Redirect cURL from the base URL to
             * "/groups/<id>/attributes/<attr name>",
             * "/datatypes/<id>/attributes/<attr name>"
             * or
             * "/datasets/<id>/attributes/<attr name>",
             * depending on the type of the object the attribute is attached to. */
            if (RV_set_object_type_header(attribute->u.attribute.parent_obj_type, &parent_obj_type_header) <
                0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, NULL,
                                "parent object not a group, datatype or dataset");

            if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/%s/%s?%s&include_attrs=1",
                                    parent_obj_type_header, attribute->u.attribute.parent_obj_URI,
                                    request_idx_type)) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL, "snprintf error");

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL,
                                "attribute open URL exceeded maximum URL size");

            if (RV_curl_get(curl, &attribute->domain->u.file.server_info, request_endpoint,
                            attribute->domain->u.file.filepath_name, CONTENT_TYPE_JSON) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, NULL, "can't get attribute");

            if (0 > RV_parse_response(response_buffer.buffer, (const void *)&loc_params->loc_data.loc_by_idx,
                                      &found_attr_name, RV_copy_attribute_name_by_index))
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_PARSEERROR, NULL, "failed to retrieve attribute names");

            if (url_encoded_attr_name) {
                curl_free(url_encoded_attr_name);
                url_encoded_attr_name = NULL;
            }

            break;
        } /* H5VL_OBJECT_BY_IDX */

        case H5VL_OBJECT_BY_TOKEN:
        default:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, NULL, "invalid loc_params type");
    } /* end switch */

    /* Make a GET request to the server to retrieve information about the attribute */

    /* URL-encode the attribute name to ensure that the resulting URL for the open
     * operation contains no illegal characters
     */
    const char *target_attr_name = found_attr_name ? (const char *)found_attr_name : attr_name;

    attr_name_len = strlen(target_attr_name);
    if (NULL == (url_encoded_attr_name = curl_easy_escape(curl, target_attr_name, (int)attr_name_len)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTENCODE, NULL, "can't URL-encode attribute name");

    /* Redirect cURL from the base URL to
     * "/groups/<id>/attributes/<attr name>",
     * "/datatypes/<id>/attributes/<attr name>"
     * or
     * "/datasets/<id>/attributes/<attr name>",
     * depending on the type of the object the attribute is attached to. */
    if (RV_set_object_type_header(attribute->u.attribute.parent_obj_type, &parent_obj_type_header) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, NULL, "parent object not a group, datatype or dataset");

    if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/%s/%s/attributes/%s", parent_obj_type_header,
                            attribute->u.attribute.parent_obj_URI, url_encoded_attr_name)) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL, "snprintf error");

    if (url_len >= URL_MAX_LENGTH)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, NULL, "attribute open URL exceeded maximum URL size");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> URL for attribute open request: %s\n\n", request_endpoint);
#endif

    if (RV_curl_get(curl, &attribute->domain->u.file.server_info, request_endpoint,
                    attribute->domain->u.file.filepath_name, CONTENT_TYPE_JSON) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, NULL, "can't get attribute");

    /* Set up a Dataspace for the opened Attribute */
    if ((attribute->u.attribute.space_id = RV_parse_dataspace(response_buffer.buffer)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCONVERT, NULL,
                        "can't convert JSON into usable dataspace for attribute");

    /* Set up a Datatype for the opened Attribute */
    if ((attribute->u.attribute.dtype_id = RV_parse_datatype(response_buffer.buffer, TRUE)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, NULL,
                        "can't convert JSON into usable datatype for attribute");

    /* Copy the attribute's name */
    if (NULL == (attribute->u.attribute.attr_name = (char *)RV_malloc(attr_name_len + 1)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL, "can't allocate space for copy of attribute's name");
    memcpy(attribute->u.attribute.attr_name, target_attr_name, attr_name_len);
    attribute->u.attribute.attr_name[attr_name_len] = '\0';

    /* Copy the AAPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * attribute access property list functions will function correctly
     */
    if (H5P_ATTRIBUTE_ACCESS_DEFAULT != aapl_id) {
        if ((attribute->u.attribute.aapl_id = H5Pcopy(aapl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy AAPL");
    } /* end if */
    else
        attribute->u.attribute.aapl_id = H5P_ATTRIBUTE_ACCESS_DEFAULT;

    /* Set up an ACPL for the attribute so that H5Aget_create_plist() will function correctly */
    /* XXX: Set any properties necessary */
    if ((attribute->u.attribute.acpl_id = H5Pcreate(H5P_ATTRIBUTE_CREATE)) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "can't create ACPL for attribute");

    if (rv_hash_table_insert(RV_type_info_array_g[H5I_ATTR]->table, (char *)attribute->URI,
                             (char *)attribute) == 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, NULL, "Failed to add attribute to type info array");

    ret_value = (void *)attribute;

done:
#ifdef RV_CONNECTOR_DEBUG
    printf("-> Attribute open response buffer:\n%s\n\n", response_buffer.buffer);

    if (attribute && ret_value) {
        printf("-> Attribute's info:\n");
        printf("     - Attribute's object type: %s\n", object_type_to_string(attribute->obj_type));
        printf("     - Attribute's domain path: %s\n", attribute->domain->u.file.filepath_name);
        printf("     - Attribute's name: %s\n", attribute->u.attribute.attr_name);
        printf("     - Attribute's datatype class: %s\n\n",
               datatype_class_to_string(attribute->u.attribute.dtype_id));
    } /* end if */
#endif

    if (url_encoded_attr_name)
        curl_free(url_encoded_attr_name);

    /* Clean up allocated attribute object if there was an issue */
    if (attribute && !ret_value)
        if (RV_attr_close(attribute, FAIL, NULL) < 0)
            FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTCLOSEOBJ, NULL, "can't close attribute");

    if (found_attr_name)
        RV_free(found_attr_name);

    PRINT_ERROR_STACK;

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
herr_t
RV_attr_read(void *attr, hid_t dtype_id, void *buf, hid_t dxpl_id, void **req)
{
    RV_object_t *attribute = (RV_object_t *)attr;
    H5T_class_t  dtype_class;
    hssize_t     file_select_npoints;
    hbool_t      is_transfer_binary = FALSE;
    htri_t       is_variable_str;
    size_t       dtype_size;
    char        *url_encoded_attr_name = NULL;
    char         request_endpoint[URL_MAX_LENGTH];
    const char  *parent_obj_type_header = NULL;
    int          url_len                = 0;
    herr_t       ret_value              = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received attribute read call with following parameters:\n");
    printf("     - Attribute's object type: %s\n", object_type_to_string(attribute->obj_type));
    if (H5I_ATTR == attribute->obj_type && attribute->u.attribute.attr_name)
        printf("     - Attribute's name: %s\n", attribute->u.attribute.attr_name);
    printf("     - Attribute's domain path: %s\n\n", attribute->domain->u.file.filepath_name);
#endif

    if (H5I_ATTR != attribute->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not an attribute");
    if (!buf)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "read buffer was NULL");

    if (!attribute->domain->u.file.server_info.base_URL)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "attribute does not have valid server URL");

    /* Determine whether it's possible to receive the data as a binary blob instead of as JSON */
    if (H5T_NO_CLASS == (dtype_class = H5Tget_class(dtype_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid");

    if ((is_variable_str = H5Tis_variable_str(dtype_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid");

    is_transfer_binary = (H5T_VLEN != dtype_class) && !is_variable_str;

    if ((file_select_npoints = H5Sget_select_npoints(attribute->u.attribute.space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "attribute's dataspace is invalid");

    if (0 == (dtype_size = H5Tget_size(dtype_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> %lld points selected for attribute read\n", file_select_npoints);
    printf("-> Attribute's datatype size: %zu\n\n", dtype_size);
#endif

    /* Instruct cURL on which type of transfer to perform, binary or JSON */
    content_type_t content_type = is_transfer_binary ? CONTENT_TYPE_OCTET_STREAM : CONTENT_TYPE_JSON;

    /* URL-encode the attribute name to ensure that the resulting URL for the read
     * operation contains no illegal characters
     */
    if (NULL == (url_encoded_attr_name = curl_easy_escape(curl, attribute->u.attribute.attr_name, 0)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTENCODE, FAIL, "can't URL-encode attribute name");

    /* Redirect cURL from the base URL to
     * "/groups/<id>/attributes/<attr name>/value",
     * "/datatypes/<id>/attributes/<attr name>/value"
     * or
     * "/datasets/<id>/attributes/<attr name>/value",
     * depending on the type of the object the attribute is attached to. */
    if (RV_set_object_type_header(attribute->u.attribute.parent_obj_type, &parent_obj_type_header) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "parent object not a group, datatype or dataset");

    if ((url_len =
             snprintf(request_endpoint, URL_MAX_LENGTH, "/%s/%s/attributes/%s/value", parent_obj_type_header,
                      attribute->u.attribute.parent_obj_URI, url_encoded_attr_name)) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error");

    if (url_len >= URL_MAX_LENGTH)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "attribute read URL exceeded maximum URL size");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> URL for attribute read request: %s\n\n", request_endpoint);
#endif

    if (RV_curl_get(curl, &attribute->domain->u.file.server_info, request_endpoint,
                    attribute->domain->u.file.filepath_name, content_type) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_READERROR, FAIL, "can't read from attribute");

    memcpy(buf, response_buffer.buffer, (size_t)file_select_npoints * dtype_size);

done:
#ifdef RV_CONNECTOR_DEBUG
    printf("-> Attribute read response buffer:\n%s\n\n", response_buffer.buffer);
#endif

    if (url_encoded_attr_name)
        curl_free(url_encoded_attr_name);

    PRINT_ERROR_STACK;

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
herr_t
RV_attr_write(void *attr, hid_t dtype_id, const void *buf, hid_t dxpl_id, void **req)
{
    RV_object_t *attribute = (RV_object_t *)attr;
    H5T_class_t  dtype_class;
    upload_info  uinfo;
    curl_off_t   write_len;
    hssize_t     file_select_npoints;
    htri_t       is_variable_str    = -1;
    hbool_t      is_transfer_binary = FALSE;
    size_t       dtype_size;
    size_t       write_body_len        = 0;
    char        *url_encoded_attr_name = NULL;
    char         request_endpoint[URL_MAX_LENGTH];
    const char  *parent_obj_type_header = NULL;
    int          url_len                = 0;
    long         http_response;
    herr_t       ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received attribute write call with following parameters:\n");
    printf("     - Attribute's object type: %s\n", object_type_to_string(attribute->obj_type));
    if (H5I_ATTR == attribute->obj_type && attribute->u.attribute.attr_name)
        printf("     - Attribute's name: %s\n", attribute->u.attribute.attr_name);
    printf("     - Attribute's domain path: %s\n\n", attribute->domain->u.file.filepath_name);
#endif

    if (H5I_ATTR != attribute->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not an attribute");
    if (!buf)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "write buffer was NULL");
    if (!attribute->domain->u.file.server_info.base_URL)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "attribute does not have valid server URL");

    /* Check for write access */
    if (!(attribute->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "no write intent on file");

    /* Determine whether it's possible to send the data as a binary blob instead of as JSON */
    if (H5T_NO_CLASS == (dtype_class = H5Tget_class(dtype_id)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid");

    if ((is_variable_str = H5Tis_variable_str(dtype_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid");

    is_transfer_binary = (H5T_VLEN != dtype_class) && !is_variable_str;

    if ((file_select_npoints = H5Sget_select_npoints(attribute->u.attribute.space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "attribute's dataspace is invalid");

    if (0 == (dtype_size = H5Tget_size(dtype_id)))
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "memory datatype is invalid");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> %lld points selected for attribute write\n", file_select_npoints);
    printf("-> Attribute's datatype size: %zu\n\n", dtype_size);
#endif

    write_body_len = (size_t)file_select_npoints * dtype_size;

    /* URL-encode the attribute name to ensure that the resulting URL for the write
     * operation contains no illegal characters
     */
    if (NULL == (url_encoded_attr_name = curl_easy_escape(curl, attribute->u.attribute.attr_name, 0)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTENCODE, FAIL, "can't URL-encode attribute name");

    /* Redirect cURL from the base URL to
     * "/groups/<id>/attributes/<attr name>/value",
     * "/datatypes/<id>/attributes/<attr name>/value"
     * or
     * "/datasets/<id>/attributes/<attr name>/value",
     * depending on the type of the object the attribute is attached to. */
    if (RV_set_object_type_header(attribute->u.attribute.parent_obj_type, &parent_obj_type_header) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "parent object not a group, datatype or dataset");

    if ((url_len =
             snprintf(request_endpoint, URL_MAX_LENGTH, "/%s/%s/attributes/%s/value", parent_obj_type_header,
                      attribute->u.attribute.parent_obj_URI, url_encoded_attr_name)) < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error");

    if (url_len >= URL_MAX_LENGTH)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "attribute write URL exceeded maximum URL size");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> URL for attribute write request: %s\n\n", request_endpoint);
#endif

    /* Check to make sure that the size of the write body can safely be cast to a curl_off_t */
    if (sizeof(curl_off_t) < sizeof(size_t))
        ASSIGN_TO_SMALLER_SIZE(write_len, curl_off_t, write_body_len, size_t)
    else if (sizeof(curl_off_t) > sizeof(size_t))
        write_len = (curl_off_t)write_body_len;
    else
        ASSIGN_TO_SAME_SIZE_UNSIGNED_TO_SIGNED(write_len, curl_off_t, write_body_len, size_t)

    uinfo.buffer      = buf;
    uinfo.buffer_size = write_body_len;
    uinfo.bytes_sent  = 0;

    // TODO
    /* Clear response buffer */
    memset(response_buffer.buffer, 0, response_buffer.buffer_size);

    http_response = RV_curl_put(curl, &attribute->domain->u.file.server_info, request_endpoint,
                                attribute->domain->u.file.filepath_name, &uinfo,
                                (is_transfer_binary ? CONTENT_TYPE_OCTET_STREAM : CONTENT_TYPE_JSON));

    if (!HTTP_SUCCESS(http_response))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_WRITEERROR, FAIL, "can't write to attribute");

done:
#ifdef RV_CONNECTOR_DEBUG
    printf("-> Attribute write response buffer:\n%s\n\n", response_buffer.buffer);
#endif

    if (url_encoded_attr_name)
        curl_free(url_encoded_attr_name);

    PRINT_ERROR_STACK;

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
herr_t
RV_attr_get(void *obj, H5VL_attr_get_args_t *args, hid_t dxpl_id, void **req)
{
    RV_object_t *loc_obj = (RV_object_t *)obj;
    char         request_endpoint[URL_MAX_LENGTH];
    char        *url_encoded_attr_name  = NULL;
    char        *found_attr_name        = NULL;
    int          url_len                = 0;
    const char  *parent_obj_type_header = NULL;
    const char  *request_idx_type       = NULL;
    herr_t       ret_value              = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received attribute get call with following parameters:\n");
    printf("     - Attribute get call type: %s\n\n", attr_get_type_to_string(args->op_type));
#endif

    if (H5I_ATTR != loc_obj->obj_type && H5I_FILE != loc_obj->obj_type && H5I_GROUP != loc_obj->obj_type &&
        H5I_DATATYPE != loc_obj->obj_type && H5I_DATASET != loc_obj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                        "parent object not an attribute, file, group, datatype or dataset");

    if (!loc_obj->domain->u.file.server_info.base_URL)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "location object does not have valid server URL");

    switch (args->op_type) {
        /* H5Aget_create_plist */
        case H5VL_ATTR_GET_ACPL: {
            hid_t *ret_id = &args->args.get_acpl.acpl_id;

            if ((*ret_id = H5Pcopy(loc_obj->u.attribute.acpl_id)) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, FAIL, "can't copy attribute ACPL");

            break;
        } /* H5VL_ATTR_GET_ACPL */

        /* H5Aget_info (_by_name/_by_idx) */
        case H5VL_ATTR_GET_INFO: {
            H5VL_loc_params_t *loc_params = &args->args.get_info.loc_params;
            H5A_info_t        *attr_info  = args->args.get_info.ainfo;

            switch (loc_params->type) {
                /* H5Aget_info */
                case H5VL_OBJECT_BY_SELF: {
#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Aget_info(): Attribute's parent object URI: %s\n",
                           loc_obj->u.attribute.parent_obj_URI);
                    printf("-> H5Aget_info(): Attribute's parent object type: %s\n\n",
                           object_type_to_string(loc_obj->u.attribute.parent_obj_type));
#endif

                    /* URL-encode the attribute name to ensure that the resulting URL for the creation
                     * operation contains no illegal characters
                     */
                    if (NULL ==
                        (url_encoded_attr_name = curl_easy_escape(curl, loc_obj->u.attribute.attr_name, 0)))
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTENCODE, FAIL, "can't URL-encode attribute name");

                    if (RV_set_object_type_header(loc_obj->u.attribute.parent_obj_type,
                                                  &parent_obj_type_header) < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL,
                                        "can't get path header from parent object type");

                    if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/%s/%s/attributes/%s",
                                            parent_obj_type_header, loc_obj->u.attribute.parent_obj_URI,
                                            url_encoded_attr_name)) < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL,
                                        "H5Aget_info request URL exceeded maximum URL size");
                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Aget_info_by_name */
                case H5VL_OBJECT_BY_NAME: {

                    if (H5I_INVALID_HID == loc_params->loc_data.loc_by_name.lapl_id)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid LAPL");

                    const char *attr_name       = args->args.get_info.attr_name;
                    H5I_type_t  parent_obj_type = H5I_UNINIT;
                    htri_t      search_ret;
                    char        parent_obj_URI[URI_MAX_LENGTH];

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Aget_info_by_name(): loc_id object's URI: %s\n", loc_obj->URI);
                    printf("-> H5Aget_info_by_name(): loc_id object type: %s\n",
                           object_type_to_string(loc_obj->obj_type));
                    printf("-> H5Aget_info_by_name(): Path to object that attribute is attached to: %s\n\n",
                           loc_params->loc_data.loc_by_name.name);
#endif

                    /* Retrieve the type and URI of the object that the attribute is attached to */
                    search_ret = RV_find_object_by_path(loc_obj, loc_params->loc_data.loc_by_name.name,
                                                        &parent_obj_type, RV_copy_object_URI_callback, NULL,
                                                        parent_obj_URI);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PATH, FAIL, "can't find parent object by name");

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Aget_info_by_name(): found attribute's parent object by given path\n");
                    printf("-> H5Aget_info_by_name(): attribute's parent object URI: %s\n", parent_obj_URI);
                    printf("-> H5Aget_info_by_name(): attribute's parent object type: %s\n\n",
                           object_type_to_string(parent_obj_type));
#endif

                    /* URL-encode the attribute name to ensure that the resulting URL for the creation
                     * operation contains no illegal characters
                     */
                    if (NULL == (url_encoded_attr_name = curl_easy_escape(curl, attr_name, 0)))
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTENCODE, FAIL, "can't URL-encode attribute name");

                    if (RV_set_object_type_header(parent_obj_type, &parent_obj_type_header) < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL,
                                        "can't get path header from parent object type");

                    if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/%s/%s/attributes/%s",
                                            parent_obj_type_header, parent_obj_URI, url_encoded_attr_name)) <
                        0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL,
                                        "H5Aget_info_by_name request URL exceeded maximum URL size");

                    break;
                } /* H5VL_OBJECT_BY_NAME */

                /* H5Aget_info_by_idx */
                case H5VL_OBJECT_BY_IDX: {

                    if (H5I_INVALID_HID == loc_params->loc_data.loc_by_idx.lapl_id)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid LAPL");

                    H5I_type_t parent_obj_type = H5I_UNINIT;
                    htri_t     search_ret;
                    char       parent_obj_URI[URI_MAX_LENGTH];

                    /* Retrieve the type and URI of the object that the attribute is attached to */
                    search_ret = RV_find_object_by_path(loc_obj, loc_params->loc_data.loc_by_name.name,
                                                        &parent_obj_type, RV_copy_object_URI_callback, NULL,
                                                        parent_obj_URI);

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
                            FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, FAIL,
                                            "unsupported index type specified");
                            break;
                    }

                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PATH, FAIL,
                                        "can't locate object that attribute is attached to");

                    /* Redirect cURL from the base URL to
                     * "/groups/<id>/attributes/<attr name>",
                     * "/datatypes/<id>/attributes/<attr name>"
                     * or
                     * "/datasets/<id>/attributes/<attr name>",
                     * depending on the type of the object the attribute is attached to. */
                    if (RV_set_object_type_header(parent_obj_type, &parent_obj_type_header) < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL,
                                        "can't get path header from parent object type");

                    if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/%s/%s?%s&include_attrs=1",
                                            parent_obj_type_header, parent_obj_URI, request_idx_type)) < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL,
                                        "attribute open URL exceeded maximum URL size");

                    if (RV_curl_get(curl, &loc_obj->domain->u.file.server_info, request_endpoint,
                                    loc_obj->domain->u.file.filepath_name, CONTENT_TYPE_JSON) < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "can't get attribute");

                    if (0 > RV_parse_response(response_buffer.buffer, &loc_params->loc_data.loc_by_idx,
                                              &found_attr_name, RV_copy_attribute_name_by_index))
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PARSEERROR, FAIL, "failed to retrieve attribute names");

                    if (url_encoded_attr_name) {
                        curl_free(url_encoded_attr_name);
                        url_encoded_attr_name = NULL;
                    }

                    if (NULL == (url_encoded_attr_name = curl_easy_escape(curl, found_attr_name, 0)))
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTENCODE, FAIL, "can't URL-encode attribute name");

                    if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/%s/%s/attributes/%s",
                                            parent_obj_type_header, parent_obj_URI, url_encoded_attr_name)) <
                        0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error");

                    break;
                } /* H5VL_OBJECT_BY_IDX */

                case H5VL_OBJECT_BY_TOKEN:
                default:
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid loc_params type");
            } /* end switch */

            /* Make a GET request to the server to retrieve the attribute's info */
            if (RV_curl_get(curl, &loc_obj->domain->u.file.server_info, request_endpoint,
                            loc_obj->domain->u.file.filepath_name, CONTENT_TYPE_JSON) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "can't get attribute");

            /* Retrieve the attribute's info */
            if (RV_parse_response(response_buffer.buffer, NULL, attr_info, RV_get_attr_info_callback) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "can't get attribute info");

            break;
        } /* H5VL_ATTR_GET_INFO */

        /* H5Aget_name (_by_idx) */
        case H5VL_ATTR_GET_NAME: {
            H5VL_loc_params_t *loc_params    = &args->args.get_name.loc_params;
            size_t             name_buf_size = args->args.get_name.buf_size;
            char              *name_buf      = args->args.get_name.buf;
            size_t            *ret_size      = args->args.get_name.attr_name_len;

            switch (loc_params->type) {
                /* H5Aget_name */
                case H5VL_OBJECT_BY_SELF: {
#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Aget_name(): Attribute's parent object URI: %s\n",
                           loc_obj->u.attribute.parent_obj_URI);
                    printf("-> H5Aget_name(): Attribute's parent object type: %s\n\n",
                           object_type_to_string(loc_obj->u.attribute.parent_obj_type));
#endif

                    *ret_size = (size_t)strlen(loc_obj->u.attribute.attr_name);

                    if (name_buf && name_buf_size) {
                        strncpy(name_buf, loc_obj->u.attribute.attr_name, name_buf_size - 1);
                        name_buf[name_buf_size - 1] = '\0';
                    } /* end if */

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Aget_name_by_idx */
                case H5VL_OBJECT_BY_IDX: {

                    if (H5I_INVALID_HID == loc_params->loc_data.loc_by_idx.lapl_id)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid LAPL");

                    H5I_type_t parent_obj_type = H5I_UNINIT;
                    htri_t     search_ret;
                    char       parent_obj_URI[URI_MAX_LENGTH];

                    /* Retrieve the type and URI of the object that the attribute is attached to */
                    search_ret = RV_find_object_by_path(loc_obj, loc_params->loc_data.loc_by_idx.name,
                                                        &parent_obj_type, RV_copy_object_URI_callback, NULL,
                                                        parent_obj_URI);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PATH, FAIL, "can't find parent object by name");

                    if (H5I_ATTR == loc_obj->obj_type)
                        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                                        "argument to H5Aget_name_by_idx should not be an attribute");

                    switch (loc_params->loc_data.loc_by_idx.idx_type) {
                        case (H5_INDEX_CRT_ORDER):
                            if (SERVER_VERSION_MATCHES_OR_EXCEEDS(loc_obj->domain->u.file.server_info.version,
                                                                  0, 7, 3)) {
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
                            FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, FAIL,
                                            "unsupported index type specified");
                            break;
                    }

                    /* Redirect cURL from the base URL to
                     * "/groups/<id>/attributes/<attr name>",
                     * "/datatypes/<id>/attributes/<attr name>"
                     * or
                     * "/datasets/<id>/attributes/<attr name>",
                     * depending on the type of the object the attribute is attached to. */
                    if (RV_set_object_type_header(parent_obj_type, &parent_obj_type_header) < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL,
                                        "parent object not a group, datatype or dataset");

                    if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/%s/%s?%s&include_attrs=1",
                                            parent_obj_type_header, parent_obj_URI, request_idx_type)) < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if (url_len >= URL_MAX_LENGTH)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL,
                                        "attribute open URL exceeded maximum URL size");

                    if (RV_curl_get(curl, &loc_obj->domain->u.file.server_info, request_endpoint,
                                    loc_obj->domain->u.file.filepath_name, CONTENT_TYPE_JSON) < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "can't get attribute");

                    if (0 > RV_parse_response(response_buffer.buffer, &loc_params->loc_data.loc_by_idx,
                                              &found_attr_name, RV_copy_attribute_name_by_index))
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PARSEERROR, FAIL, "failed to retrieve attribute names");

                    *ret_size = (size_t)strlen(found_attr_name);

                    if (name_buf && name_buf_size) {
                        strncpy(name_buf, found_attr_name, name_buf_size - 1);
                        name_buf[name_buf_size - 1] = '\0';
                    } /* end if */

                    if (url_encoded_attr_name) {
                        curl_free(url_encoded_attr_name);
                        url_encoded_attr_name = NULL;
                    }

                    break;
                } /* H5VL_OBJECT_BY_IDX */

                case H5VL_OBJECT_BY_TOKEN:
                case H5VL_OBJECT_BY_NAME:
                default:
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid loc_params type");
            }

            break;
        } /* H5VL_ATTR_GET_NAME */

        /* H5Aget_space */
        case H5VL_ATTR_GET_SPACE: {
            hid_t *ret_id = &args->args.get_space.space_id;

            if ((*ret_id = H5Scopy(loc_obj->u.attribute.space_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCOPY, FAIL, "can't copy attribute's dataspace");

            break;
        } /* H5VL_ATTR_GET_SPACE */

        /* H5Aget_storage_size */
        case H5VL_ATTR_GET_STORAGE_SIZE:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_UNSUPPORTED, FAIL, "H5Aget_storage_size is unsupported");
            break;

        /* H5Aget_type */
        case H5VL_ATTR_GET_TYPE: {
            hid_t *ret_id = &args->args.get_type.type_id;

            if ((*ret_id = H5Tcopy(loc_obj->u.attribute.dtype_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCOPY, FAIL, "can't copy attribute's datatype");

            break;
        } /* H5VL_ATTR_GET_TYPE */

        default:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "can't get this type of information from attribute");
    } /* end switch */

done:
    if (url_encoded_attr_name)
        curl_free(url_encoded_attr_name);

    if (found_attr_name) {
        RV_free(found_attr_name);
        found_attr_name = NULL;
    }

    PRINT_ERROR_STACK;

    return ret_value;
} /* end RV_attr_get() */

/*-------------------------------------------------------------------------
 * Function:    RV_attr_specific
 *
 * Purpose:     Performs a connector-specific operation on an HDF5 attribute,
 *              such as calling H5Adelete
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              November, 2017
 */
herr_t
RV_attr_specific(void *obj, const H5VL_loc_params_t *loc_params, H5VL_attr_specific_args_t *args,
                 hid_t dxpl_id, void **req)
{
    RV_object_t              *loc_obj             = (RV_object_t *)obj;
    RV_object_t              *attr_iter_obj_typed = NULL;
    RV_object_t              *attr                = NULL;
    RV_object_t              *renamed_attr        = NULL;
    RV_object_t              *attr_parent         = NULL;
    H5I_type_t                parent_obj_type     = H5I_UNINIT;
    size_t                    elem_size           = 0;
    H5VL_attr_get_args_t      attr_get_args;
    H5VL_loc_params_t         attr_open_loc_params;
    H5VL_loc_params_t         attr_delete_loc_params;
    H5VL_attr_specific_args_t attr_delete_args;
    hssize_t                  num_elems               = 0;
    size_t                    attr_name_to_delete_len = 0;
    hid_t                     attr_iter_object_id     = H5I_INVALID_HID;
    hid_t                     space_id                = H5I_INVALID_HID;
    hid_t                     type_id                 = H5I_INVALID_HID;
    void                     *buf                     = NULL;
    void                     *attr_iter_object        = NULL;
    char                      parent_URI[URI_MAX_LENGTH];
    char                     *obj_URI;
    char                      temp_URI[URI_MAX_LENGTH];
    char                      request_endpoint[URL_MAX_LENGTH];
    char                      attr_name_to_delete[ATTRIBUTE_NAME_MAX_LENGTH];
    char                     *url_encoded_attr_name  = NULL;
    const char               *parent_obj_type_header = NULL;
    int                       url_len                = 0;
    herr_t                    ret_value              = SUCCEED;
    long                      http_response;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received attribute-specific call with following parameters:\n");
    printf("     - Attribute-specific call type: %s\n\n", attr_specific_type_to_string(args->op_type));
#endif

    if (H5I_FILE != loc_obj->obj_type && H5I_GROUP != loc_obj->obj_type &&
        H5I_DATATYPE != loc_obj->obj_type && H5I_DATASET != loc_obj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "parent object not a file, group, datatype or dataset");

    switch (args->op_type) {
        /* H5Adelete (_by_name/_by_idx) */
        case H5VL_ATTR_DELETE_BY_IDX: {

            if (H5I_INVALID_HID == loc_params->loc_data.loc_by_name.lapl_id)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid LAPL");

            attr_get_args.op_type                                        = H5VL_ATTR_GET_NAME;
            attr_get_args.args.get_name.loc_params.type                  = H5VL_OBJECT_BY_IDX;
            attr_get_args.args.get_name.loc_params.loc_data.loc_by_idx.n = args->args.delete_by_idx.n;
            attr_get_args.args.get_name.loc_params.loc_data.loc_by_idx.idx_type =
                args->args.delete_by_idx.idx_type;
            attr_get_args.args.get_name.loc_params.loc_data.loc_by_idx.order = args->args.delete_by_idx.order;
            attr_get_args.args.get_name.loc_params.loc_data.loc_by_idx.lapl_id = H5P_DEFAULT;
            attr_get_args.args.get_name.loc_params.loc_data.loc_by_idx.name =
                loc_params->loc_data.loc_by_name.name;

            attr_get_args.args.get_name.buf_size      = ATTRIBUTE_NAME_MAX_LENGTH;
            attr_get_args.args.get_name.buf           = attr_name_to_delete;
            attr_get_args.args.get_name.attr_name_len = &attr_name_to_delete_len;

            if (RV_attr_get(obj, &attr_get_args, dxpl_id, req) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "can't get name of attribute by index");

            /* URL-encode the attribute name so that the resulting URL for the
             * attribute delete operation doesn't contain any illegal characters
             */
            if (NULL == (url_encoded_attr_name = curl_easy_escape(curl, attr_name_to_delete, 0)))
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTENCODE, FAIL, "can't URL-encode attribute name");

            /* Retrieve type of attribute's parent object */
            if (RV_find_object_by_path(loc_obj, loc_params->loc_data.loc_by_name.name, &parent_obj_type,
                                       RV_copy_object_URI_callback, NULL, temp_URI) < 0)
                FUNC_GOTO_ERROR(H5E_OBJECT, H5E_CANTFIND, FAIL, "unable to retrieve attribute parent object");

            /* Redirect cURL from the base URL to
             * "/groups/<id>/attributes/<attr name>",
             * "/datatypes/<id>/attributes/<attr name>"
             * or
             * "/datasets/<id>/attributes/<attr name>,
             * depending on the type of the object the attribute is attached to. */
            if (RV_set_object_type_header(parent_obj_type, (const char **)&parent_obj_type_header) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL,
                                "parent object not a group, datatype or dataset");

            if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/%s/%s/attributes/%s",
                                    parent_obj_type_header, temp_URI, url_encoded_attr_name)) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error");

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL,
                                "H5Adelete(_by_name) request URL exceeded maximum URL size");

            http_response =
                RV_curl_delete(curl, &loc_obj->domain->u.file.server_info, (const char *)request_endpoint,
                               (const char *)loc_obj->domain->u.file.filepath_name);

            if (!HTTP_SUCCESS(http_response))
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTREMOVE, FAIL, "can't delete attribute");

            break;
        }

        case H5VL_ATTR_DELETE: {
            const char *attr_name = NULL;

            /* Check for write access */
            if (!(loc_obj->domain->u.file.intent & H5F_ACC_RDWR))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "no write intent on file");

            switch (loc_params->type) {
                /* H5Adelete */
                case H5VL_OBJECT_BY_SELF: {
                    attr_name       = args->args.del.name;
                    obj_URI         = loc_obj->URI;
                    parent_obj_type = loc_obj->obj_type;

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Adelete(): Attribute's name: %s\n", attr_name);
                    printf("-> H5Adelete(): Attribute's parent object URI: %s\n", loc_obj->URI);
                    printf("-> H5Adelete(): Attribute's parent object type: %s\n\n",
                           object_type_to_string(parent_obj_type));
#endif

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Adelete_by_name */
                case H5VL_OBJECT_BY_NAME: {

                    if (H5I_INVALID_HID == loc_params->loc_data.loc_by_name.lapl_id)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid LAPL");

                    htri_t search_ret;

                    attr_name = args->args.del.name;

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Adelete_by_name(): loc_id object type: %s\n",
                           object_type_to_string(loc_obj->obj_type));
                    printf("-> H5Adelete_by_name(): Path to object that attribute is attached to: %s\n\n",
                           loc_params->loc_data.loc_by_name.name);
#endif

                    search_ret =
                        RV_find_object_by_path(loc_obj, loc_params->loc_data.loc_by_name.name,
                                               &parent_obj_type, RV_copy_object_URI_callback, NULL, temp_URI);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PATH, FAIL,
                                        "can't locate object that attribute is attached to");

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Adelete_by_name(): found attribute's parent object by given path\n");
                    printf("-> H5Adelete_by_name(): attribute's parent object URI: %s\n", temp_URI);
                    printf("-> H5Adelete_by_name(): attribute's parent object type: %s\n\n",
                           object_type_to_string(parent_obj_type));
#endif

                    obj_URI = temp_URI;

                    break;
                } /* H5VL_OBJECT_BY_NAME */

                /* H5Adelete_by_idx */
                case H5VL_OBJECT_BY_IDX: {
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_UNSUPPORTED, FAIL,
                                    "invalid location parameters - this message should not appear!");
                    break;
                } /* H5VL_OBJECT_BY_IDX */

                case H5VL_OBJECT_BY_TOKEN:
                default:
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid loc_params type");
            } /* end switch */

            /* URL-encode the attribute name so that the resulting URL for the
             * attribute delete operation doesn't contain any illegal characters
             */
            if (NULL == (url_encoded_attr_name = curl_easy_escape(curl, attr_name, 0)))
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTENCODE, FAIL, "can't URL-encode attribute name");

            /* Redirect cURL from the base URL to
             * "/groups/<id>/attributes/<attr name>",
             * "/datatypes/<id>/attributes/<attr name>"
             * or
             * "/datasets/<id>/attributes/<attr name>,
             * depending on the type of the object the attribute is attached to. */
            if (RV_set_object_type_header(parent_obj_type, &parent_obj_type_header) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL,
                                "parent object not a group, datatype or dataset");

            if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/%s/%s/attributes/%s",
                                    parent_obj_type_header, obj_URI, url_encoded_attr_name)) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error");

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL,
                                "H5Adelete(_by_name) request URL exceeded maximum URL size");

            http_response =
                RV_curl_delete(curl, &loc_obj->domain->u.file.server_info, (const char *)request_endpoint,
                               (const char *)loc_obj->domain->u.file.filepath_name);

            if (!HTTP_SUCCESS(http_response))
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTREMOVE, FAIL, "can't delete attribute");

            break;
        } /* H5VL_ATTR_DELETE */

        /* H5Aexists (_by_name) */
        case H5VL_ATTR_EXISTS: {
            const char *attr_name = args->args.exists.name;
            hbool_t    *ret       = args->args.exists.exists;

            switch (loc_params->type) {
                /* H5Aexists */
                case H5VL_OBJECT_BY_SELF: {
                    obj_URI         = loc_obj->URI;
                    parent_obj_type = loc_obj->obj_type;

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Aexists(): Attribute's parent object URI: %s\n", loc_obj->URI);
                    printf("-> H5Aexists(): Attribute's parent object type: %s\n\n",
                           object_type_to_string(parent_obj_type));
#endif

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Aexists_by_name */
                case H5VL_OBJECT_BY_NAME: {

                    if (H5I_INVALID_HID == loc_params->loc_data.loc_by_name.lapl_id)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid LAPL");

                    htri_t search_ret;

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Aexists_by_name(): loc_id object type: %s\n",
                           object_type_to_string(loc_obj->obj_type));
                    printf("-> H5Aexists_by_name(): Path to object that attribute is attached to: %s\n\n",
                           loc_params->loc_data.loc_by_name.name);
#endif

                    search_ret =
                        RV_find_object_by_path(loc_obj, loc_params->loc_data.loc_by_name.name,
                                               &parent_obj_type, RV_copy_object_URI_callback, NULL, temp_URI);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PATH, FAIL,
                                        "can't locate object that attribute is attached to");

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Aexists_by_name(): found attribute's parent object by given path\n");
                    printf("-> H5Aexists_by_name(): attribute's parent object URI: %s\n", temp_URI);
                    printf("-> H5Aexists_by_name(): attribute's parent object type: %s\n\n",
                           object_type_to_string(parent_obj_type));
#endif

                    obj_URI = temp_URI;

                    break;
                } /* H5VL_OBJECT_BY_NAME */

                case H5VL_OBJECT_BY_IDX:
                case H5VL_OBJECT_BY_TOKEN:
                default:
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid loc_params types");
            } /* end switch */

            /* URL-encode the attribute name so that the resulting URL for the
             * attribute delete operation doesn't contain any illegal characters
             */
            if (NULL == (url_encoded_attr_name = curl_easy_escape(curl, attr_name, 0)))
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTENCODE, FAIL, "can't URL-encode attribute name");

            /* Redirect cURL from the base URL to
             * "/groups/<id>/attributes/<attr name>",
             * "/datatypes/<id>/attributes/<attr name>"
             * or
             * "/datasets/<id>/attributes/<attr name>,
             * depending on the type of the object the attribute is attached to. */
            if (RV_set_object_type_header(parent_obj_type, &parent_obj_type_header) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL,
                                "parent object not a group, datatype or dataset");

            if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/%s/%s/attributes/%s",
                                    parent_obj_type_header, obj_URI, url_encoded_attr_name)) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error");

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL,
                                "H5Aexists(_by_name) request URL exceeded maximum URL size");

            http_response = RV_curl_get(curl, &loc_obj->domain->u.file.server_info, request_endpoint,
                                        loc_obj->domain->u.file.filepath_name, CONTENT_TYPE_JSON);

            if (HTTP_SUCCESS(http_response))
                *ret = TRUE;
            else if (HTTP_CLIENT_ERROR(http_response))
                *ret = FALSE;
            else
                HANDLE_RESPONSE(http_response, H5E_ATTR, H5E_CANTGET, FAIL);

            break;
        } /* H5VL_ATTR_EXISTS */

        /* H5Aiterate (_by_name) */
        case H5VL_ATTR_ITER: {
            iter_data attr_iter_data;

            attr_iter_data.is_recursive               = FALSE;
            attr_iter_data.index_type                 = args->args.iterate.idx_type;
            attr_iter_data.iter_order                 = args->args.iterate.order;
            attr_iter_data.idx_p                      = args->args.iterate.idx;
            attr_iter_data.iter_function.attr_iter_op = args->args.iterate.op;
            attr_iter_data.op_data                    = args->args.iterate.op_data;

            if (!attr_iter_data.iter_function.attr_iter_op)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_ATTRITERERROR, FAIL,
                                "no attribute iteration function specified");

            switch (loc_params->type) {
                /* H5Aiterate2 */
                case H5VL_OBJECT_BY_SELF: {
                    obj_URI         = loc_obj->URI;
                    parent_obj_type = loc_obj->obj_type;

                    if (NULL == (attr_iter_object = RV_malloc(sizeof(RV_object_t))))
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, FAIL,
                                        "can't allocate copy of attribute's parent object");

                    memcpy(attr_iter_object, loc_obj, sizeof(RV_object_t));

                    attr_iter_obj_typed = (RV_object_t *)attr_iter_object;

                    /* Since we already have the attribute's parent object, but still need an hid_t for it
                     * to pass to the user's object, we will just copy the current object, making sure to
                     * increment the ref. counts for the object's fields so that closing it at the end of
                     * this function does not close the fields themselves in the real object, such as a
                     * dataset's dataspace.
                     */

                    /* Increment refs for top-level file */
                    if (parent_obj_type == H5I_FILE || parent_obj_type == H5I_GROUP ||
                        parent_obj_type == H5I_DATASET || parent_obj_type == H5I_DATATYPE) {

                        loc_obj->domain->u.file.ref_count++;
                    }

                    /* Increment refs for specific type */

                    RV_object_t *attr_iter_obj = (RV_object_t *)attr_iter_object;

                    if ((attr_iter_obj->handle_path = RV_malloc(strlen(loc_obj->handle_path) + 1)) == NULL)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, FAIL,
                                        "can't allocate space for copy of object path");

                    strncpy(attr_iter_obj->handle_path, loc_obj->handle_path,
                            strlen(loc_obj->handle_path) + 1);

                    switch (parent_obj_type) {
                        case H5I_FILE:
                            /* Copy plists, filepath, and server info to new object */

                            /* FAPL */
                            if (loc_obj->u.file.fapl_id != H5P_FILE_ACCESS_DEFAULT) {
                                if (H5I_INVALID_HID ==
                                    (attr_iter_obj->u.file.fapl_id = H5Pcopy(loc_obj->u.file.fapl_id)))
                                    FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, FAIL, "can't copy FAPL");
                            }
                            else
                                attr_iter_obj->u.file.fapl_id = H5P_FILE_ACCESS_DEFAULT;

                            /* FCPL */
                            if (loc_obj->u.file.fcpl_id != H5P_FILE_CREATE_DEFAULT) {
                                if (H5I_INVALID_HID ==
                                    (attr_iter_obj->u.file.fcpl_id = H5Pcopy(loc_obj->u.file.fcpl_id)))
                                    FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, FAIL, "can't copy FCPL");
                            }
                            else
                                attr_iter_obj->u.file.fcpl_id = H5P_FILE_CREATE_DEFAULT;

                            /* Filepath */
                            if (NULL == (attr_iter_obj->u.file.filepath_name =
                                             RV_malloc(strlen(loc_obj->u.file.filepath_name) + 1)))
                                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL,
                                                "can't allocate space for copied filepath");

                            strncpy(attr_iter_obj->u.file.filepath_name, loc_obj->u.file.filepath_name,
                                    strlen(loc_obj->u.file.filepath_name) + 1);

                            if ((attr_iter_obj->u.file.server_info.username =
                                     RV_malloc(strlen(loc_obj->u.file.server_info.username) + 1)) == NULL)
                                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL,
                                                "can't allocate space for copied username");

                            strncpy(attr_iter_obj->u.file.server_info.username,
                                    loc_obj->u.file.server_info.username,
                                    strlen(loc_obj->u.file.server_info.username) + 1);

                            if ((attr_iter_obj->u.file.server_info.password =
                                     RV_malloc(strlen(loc_obj->u.file.server_info.password) + 1)) == NULL)
                                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL,
                                                "can't allocate space for copied password");

                            strncpy(attr_iter_obj->u.file.server_info.password,
                                    loc_obj->u.file.server_info.password,
                                    strlen(loc_obj->u.file.server_info.password) + 1);

                            if ((attr_iter_obj->u.file.server_info.base_URL =
                                     RV_malloc(strlen(loc_obj->u.file.server_info.base_URL) + 1)) == NULL)
                                FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL,
                                                "can't allocate space for copied URL");

                            strncpy(attr_iter_obj->u.file.server_info.base_URL,
                                    loc_obj->u.file.server_info.base_URL,
                                    strlen(loc_obj->u.file.server_info.base_URL) + 1);

                            /* This is a copy of the file, not a reference to the same memory */
                            loc_obj->domain->u.file.ref_count--;
                            break;
                        case H5I_GROUP:

                            /* GCPL */
                            if (loc_obj->u.group.gcpl_id != H5P_GROUP_CREATE_DEFAULT) {
                                if (H5Iinc_ref(loc_obj->u.group.gcpl_id) < 0)
                                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL,
                                                    "can't increment field's ref. count for copy of "
                                                    "attribute's parent group");
                            }

                            /* GAPL */
                            if (loc_obj->u.group.gapl_id != H5P_GROUP_ACCESS_DEFAULT) {
                                if (H5Iinc_ref(loc_obj->u.group.gapl_id) < 0)
                                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL,
                                                    "can't increment field's ref. count for copy of "
                                                    "attribute's parent group");
                            }

                            break;

                        case H5I_DATATYPE:

                            /* Datatype */
                            if (H5Iinc_ref(loc_obj->u.datatype.dtype_id) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL,
                                                "can't increment field's ref. count for copy of attribute's "
                                                "parent datatype");

                            /* TCPL */
                            if (loc_obj->u.datatype.tcpl_id != H5P_DATATYPE_CREATE_DEFAULT)
                                if (H5Iinc_ref(loc_obj->u.datatype.tcpl_id) < 0)
                                    FUNC_GOTO_ERROR(
                                        H5E_ATTR, H5E_CANTINC, FAIL,
                                        "can't increment field's ref. count for copy of attribute's "
                                        "parent datatype");

                            /* TAPL */
                            if (loc_obj->u.datatype.tapl_id != H5P_DATATYPE_ACCESS_DEFAULT)
                                if (H5Iinc_ref(loc_obj->u.datatype.tapl_id) < 0)
                                    FUNC_GOTO_ERROR(
                                        H5E_ATTR, H5E_CANTINC, FAIL,
                                        "can't increment field's ref. count for copy of attribute's "
                                        "parent datatype");
                            break;

                        case H5I_DATASET:

                            if (H5Iinc_ref(loc_obj->u.dataset.dtype_id) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL,
                                                "can't increment field's ref. count for copy of attribute's "
                                                "parent dataset");
                            if (H5Iinc_ref(loc_obj->u.dataset.space_id) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL,
                                                "can't increment field's ref. count for copy of attribute's "
                                                "parent dataset");
                            if (H5Iinc_ref(loc_obj->u.dataset.dapl_id) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL,
                                                "can't increment field's ref. count for copy of attribute's "
                                                "parent dataset");
                            if (H5Iinc_ref(loc_obj->u.dataset.dcpl_id) < 0)
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTINC, FAIL,
                                                "can't increment field's ref. count for copy of attribute's "
                                                "parent dataset");
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
                            FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL,
                                            "parent object not a file, group, datatype or dataset");
                    } /* end switch */

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Aiterate2(): Attribute's parent object URI: %s\n", loc_obj->URI);
                    printf("-> H5Aiterate2(): Attribute's parent object type: %s\n\n",
                           object_type_to_string(parent_obj_type));
#endif

                    break;
                } /* H5VL_OBJECT_BY_SELF */

                /* H5Aiterate_by_name */
                case H5VL_OBJECT_BY_NAME: {

                    if (H5I_INVALID_HID == loc_params->loc_data.loc_by_name.lapl_id)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid LAPL");

                    htri_t search_ret;

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Aiterate_by_name(): loc_id object type: %s\n",
                           object_type_to_string(loc_obj->obj_type));
                    printf("-> H5Aiterate_by_name(): Path to object that attribute is attached to: %s\n\n",
                           loc_params->loc_data.loc_by_name.name);
#endif

                    search_ret =
                        RV_find_object_by_path(loc_obj, loc_params->loc_data.loc_by_name.name,
                                               &parent_obj_type, RV_copy_object_URI_callback, NULL, temp_URI);
                    if (!search_ret || search_ret < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PATH, FAIL,
                                        "can't locate object that attribute is attached to");

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> H5Aiterate_by_name(): found attribute's parent object by given path\n");
                    printf("-> H5Aiterate_by_name(): attribute's parent object URI: %s\n", temp_URI);
                    printf("-> H5Aiterate_by_name(): attribute's parent object type: %s\n\n",
                           object_type_to_string(parent_obj_type));

                    printf("-> Opening attribute's parent object to generate an hid_t and work around VOL "
                           "layer\n\n");
#endif

                    /* Since the VOL layer doesn't directly pass down the parent object's ID for the
                     * attribute, explicitly open the object here so that a valid hid_t can be passed to the
                     * user's attribute iteration callback. In the case of H5Aiterate, we are already passed
                     * the attribute's parent object, so we just generate a second ID for it instead of
                     * needing to open it explicitly.
                     */
                    switch (parent_obj_type) {
                        case H5I_FILE:
                        case H5I_GROUP:
                            if (NULL == (attr_iter_object = RV_group_open(
                                             loc_obj, loc_params, loc_params->loc_data.loc_by_name.name,
                                             H5P_DEFAULT, H5P_DEFAULT, NULL)))
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTOPENOBJ, FAIL,
                                                "can't open attribute's parent group");
                            break;

                        case H5I_DATATYPE:
                            if (NULL == (attr_iter_object = RV_datatype_open(
                                             loc_obj, loc_params, loc_params->loc_data.loc_by_name.name,
                                             H5P_DEFAULT, H5P_DEFAULT, NULL)))
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTOPENOBJ, FAIL,
                                                "can't open attribute's parent datatype");
                            break;

                        case H5I_DATASET:
                            if (NULL == (attr_iter_object = RV_dataset_open(
                                             loc_obj, loc_params, loc_params->loc_data.loc_by_name.name,
                                             H5P_DEFAULT, H5P_DEFAULT, NULL)))
                                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTOPENOBJ, FAIL,
                                                "can't open attribute's parent dataset");
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
                            FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL,
                                            "parent object not a file, group, datatype or dataset");
                    } /* end switch */

                    obj_URI = temp_URI;

                    break;
                } /* H5VL_OBJECT_BY_NAME */

                case H5VL_OBJECT_BY_IDX:
                case H5VL_OBJECT_BY_TOKEN:
                default:
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid loc_params type");
            } /* end switch */

            /* Redirect cURL from the base URL to
             * "/groups/<id>/attributes",
             * "/datatypes/<id>/attributes"
             * or
             * "/datasets/<id>/attributes",
             * depending on the type of the object the attribute is attached to. */
            if (RV_set_object_type_header(parent_obj_type, &parent_obj_type_header) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL,
                                "parent object not a group, datatype or dataset");

            if ((url_len = snprintf(request_endpoint, URL_MAX_LENGTH, "/%s/%s/attributes",
                                    parent_obj_type_header, obj_URI)) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL, "snprintf error");

            if (url_len >= URL_MAX_LENGTH)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_SYSERRSTR, FAIL,
                                "H5Aiterate(_by_name) request URL exceeded maximum URL size");

            /* Register an hid_t for the attribute's parent object */

            /* In order to appease H5VLwrap_register(), ensure that the proper interface is initialized before
             * calling it, just as in the code for link iteration.
             */
            if (H5I_FILE == parent_obj_type || H5I_GROUP == parent_obj_type) {
                H5E_BEGIN_TRY
                {
                    H5Gopen2(H5I_INVALID_HID, NULL, H5P_DEFAULT);
                }
                H5E_END_TRY;
            } /* end if */
            else if (H5I_DATATYPE == parent_obj_type) {
                H5E_BEGIN_TRY
                {
                    H5Topen2(H5I_INVALID_HID, NULL, H5P_DEFAULT);
                }
                H5E_END_TRY;
            } /* end else if */
            else {
                H5E_BEGIN_TRY
                {
                    H5Dopen2(H5I_INVALID_HID, NULL, H5P_DEFAULT);
                }
                H5E_END_TRY;
            } /* end else */

            if ((attr_iter_object_id = H5VLwrap_register(attr_iter_object, parent_obj_type)) < 0)
                FUNC_GOTO_ERROR(H5E_ID, H5E_CANTREGISTER, FAIL,
                                "can't create ID for parent object for attribute iteration");

            attr_iter_data.iter_obj_id = attr_iter_object_id;

            /* Make a GET request to the server to retrieve all of the attributes attached to the given object
             */
            if (RV_curl_get(curl, &loc_obj->domain->u.file.server_info, request_endpoint,
                            loc_obj->domain->u.file.filepath_name, CONTENT_TYPE_JSON) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "can't get attribute");

            if (RV_parse_response(response_buffer.buffer, &attr_iter_data, NULL, RV_attr_iter_callback) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "can't iterate over attributes");

            break;
        } /* H5VL_ATTR_ITER */

        /* H5Arename (_by_name) */
        case H5VL_ATTR_RENAME: {
            /* Open original attribute */

            switch (loc_params->type) {
                /* H5rename */
                case H5VL_OBJECT_BY_SELF: {
                    parent_obj_type = loc_obj->obj_type;
                    break;
                }

                /* H5Arename_by_name */
                case H5VL_OBJECT_BY_NAME: {
                    if (loc_params->loc_data.loc_by_name.lapl_id == H5I_INVALID_HID)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid LAPL");

                    if (RV_find_object_by_path(loc_obj, loc_params->loc_data.loc_by_name.name,
                                               &parent_obj_type, RV_copy_object_URI_callback, NULL,
                                               parent_URI) < 0)
                        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PATH, FAIL,
                                        "can't find object attribute is attached to");

                    /* Open parent object of attribute */
                    switch (parent_obj_type) {
                        case H5I_FILE:
                        case H5I_GROUP:
                            if ((attr_parent = (RV_object_t *)RV_group_open(
                                     obj, loc_params, loc_params->loc_data.loc_by_name.name, H5P_DEFAULT,
                                     H5P_DEFAULT, NULL)) == NULL)
                                FUNC_GOTO_ERROR(H5E_SYM, H5E_CANTOPENOBJ, FAIL, "can't open parent group");
                            break;

                        case H5I_DATASET:
                            if ((attr_parent = (RV_object_t *)RV_dataset_open(
                                     obj, loc_params, loc_params->loc_data.loc_by_name.name, H5P_DEFAULT,
                                     H5P_DEFAULT, NULL)) == NULL)
                                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTOPENOBJ, FAIL,
                                                "can't open parent group");
                            break;

                        case H5I_DATATYPE:
                            if ((attr_parent = (RV_object_t *)RV_datatype_open(
                                     obj, loc_params, loc_params->loc_data.loc_by_name.name, H5P_DEFAULT,
                                     H5P_DEFAULT, NULL)) == NULL)
                                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTOPENOBJ, FAIL,
                                                "can't open parent group");
                            break;

                        default:
                            FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL,
                                            "attribute's parent object is not group, dataset, or datatype");
                            break;
                    }

                    break;
                }

                case H5VL_OBJECT_BY_TOKEN:
                case H5VL_OBJECT_BY_IDX:
                default: {
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "invalid loc_params type");
                } break;
            }

            /* Open original attribute */
            attr_open_loc_params.type = H5VL_OBJECT_BY_SELF;
            attr_open_loc_params.obj_type =
                (loc_params->type == H5VL_OBJECT_BY_SELF) ? loc_obj->obj_type : parent_obj_type;

            if ((attr = (RV_object_t *)RV_attr_open(
                     (loc_params->type == H5VL_OBJECT_BY_SELF) ? obj : (void *)attr_parent,
                     (const H5VL_loc_params_t *)&attr_open_loc_params, args->args.rename.old_name,
                     H5P_DEFAULT, H5P_DEFAULT, NULL)) == NULL)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTOPENOBJ, FAIL, "can't open attribute");

            /* Create copy of attribute with same name */
            if ((renamed_attr = RV_attr_create(
                     (loc_params->type == H5VL_OBJECT_BY_SELF) ? obj : (void *)attr_parent,
                     (const H5VL_loc_params_t *)&attr_open_loc_params, args->args.rename.new_name,
                     attr->u.attribute.dtype_id, attr->u.attribute.space_id, attr->u.attribute.acpl_id,
                     attr->u.attribute.aapl_id, H5P_DEFAULT, NULL)) == NULL)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTCREATE, FAIL, "can't create renamed attribute");

            /* Write original data to copy of attribute */
            if ((num_elems = H5Sget_simple_extent_npoints(attr->u.attribute.space_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL,
                                "can't get number of elements in dataspace");

            if ((elem_size = H5Tget_size(attr->u.attribute.dtype_id)) == 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTGET, FAIL, "can't get size of datatype");

            /* Allocate buffer for attr read */
            if ((buf = RV_calloc((size_t)num_elems * elem_size)) == NULL)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, FAIL, "can't allocate space for attribute read");

            if (RV_attr_read((void *)attr, attr->u.attribute.dtype_id, buf, H5P_DEFAULT, NULL) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_READERROR, FAIL, "can't read attribute");

            if (RV_attr_write((void *)renamed_attr, attr->u.attribute.dtype_id, (const void *)buf,
                              H5P_DEFAULT, NULL) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_WRITEERROR, FAIL, "can't write to attribute");

            /* Close original attribute */
            if (RV_attr_close((void *)attr, H5P_DEFAULT, NULL) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTCLOSEOBJ, FAIL, "can't close attribute");

            attr = NULL;

            /* Delete original attribute */
            attr_delete_loc_params.obj_type = H5I_ATTR;
            attr_delete_loc_params.type     = H5VL_OBJECT_BY_SELF;

            attr_delete_args.op_type       = H5VL_ATTR_DELETE;
            attr_delete_args.args.del.name = args->args.rename.old_name;

            if (RV_attr_specific((loc_params->type == H5VL_OBJECT_BY_SELF) ? obj : (void *)attr_parent,
                                 (const H5VL_loc_params_t *)&attr_delete_loc_params,
                                 (H5VL_attr_specific_args_t *)&attr_delete_args, H5P_DEFAULT, NULL) < 0)
                FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTDELETE, FAIL, "can't delete attr with old name");

            break;
        }
        default:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "unknown attribute operation");

    } /* end switch */

done:
    if (attr_iter_object_id >= 0) {
        if (H5I_FILE == parent_obj_type) {
            if (H5Fclose(attr_iter_object_id) < 0)
                FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTCLOSEOBJ, FAIL,
                                "can't close attribute iteration parent file");
        }
        else if (H5I_GROUP == parent_obj_type) {
            if (H5Gclose(attr_iter_object_id) < 0)
                FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTCLOSEOBJ, FAIL,
                                "can't close attribute iteration parent group");
        }
        else if (H5I_DATATYPE == parent_obj_type) {
            if (H5Tclose(attr_iter_object_id) < 0)
                FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTCLOSEOBJ, FAIL,
                                "can't close attribute iteration parent datatype");
        }
        else if (H5I_DATASET == parent_obj_type) {
            if (H5Dclose(attr_iter_object_id) < 0)
                FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTCLOSEOBJ, FAIL,
                                "can't close attribute iteration parent dataset");
        } /* end else if */
        else
            FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTCLOSEOBJ, FAIL, "invalid attribute parent object type");
    } /* end if */

    if (attr)
        if (RV_attr_close(attr, H5P_DEFAULT, NULL) < 0)
            FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTCLOSEOBJ, FAIL, "can't close attribute");

    if (renamed_attr)
        if (RV_attr_close(renamed_attr, H5P_DEFAULT, NULL) < 0)
            FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTCLOSEOBJ, FAIL, "can't close attribute");

    if (attr_parent)
        switch (parent_obj_type) {
            case H5I_FILE:
            case H5I_GROUP:
                if (RV_group_close((void *)attr_parent, H5P_DEFAULT, NULL) < 0)
                    FUNC_DONE_ERROR(H5E_SYM, H5E_CANTCLOSEOBJ, FAIL, "can't close parent group");
                break;

            case H5I_DATASET:
                if (RV_dataset_close((void *)attr_parent, H5P_DEFAULT, NULL) < 0)
                    FUNC_DONE_ERROR(H5E_DATASET, H5E_CANTCLOSEOBJ, FAIL, "can't close parent dataset");
                break;

            case H5I_DATATYPE:
                if (RV_datatype_close((void *)attr_parent, H5P_DEFAULT, NULL) < 0)
                    FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, FAIL, "can't close parent datatype");

            default:
                FUNC_DONE_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL,
                                "attribute's parent object is not group, dataset, or datatype");
                break;
        }

    if (url_encoded_attr_name)
        curl_free(url_encoded_attr_name);

    if (buf)
        RV_free(buf);

    PRINT_ERROR_STACK;

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
herr_t
RV_attr_close(void *attr, hid_t dxpl_id, void **req)
{
    RV_object_t *_attr     = (RV_object_t *)attr;
    herr_t       ret_value = SUCCEED;

    if (!_attr)
        FUNC_GOTO_DONE(SUCCEED);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received attribute close call with following parameters:\n");
    printf("     - Attribute's object type: %s\n", object_type_to_string(_attr->obj_type));
    if (H5I_ATTR == _attr->obj_type && _attr->u.attribute.attr_name)
        printf("     - Attribute's name: %s\n", _attr->u.attribute.attr_name);
    if (_attr->domain && _attr->domain->u.file.filepath_name)
        printf("     - Attribute's domain path: %s\n", _attr->domain->u.file.filepath_name);
    printf("\n");
#endif

    if (H5I_ATTR != _attr->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not an attribute");

    if (_attr->u.attribute.attr_name) {
        RV_free(_attr->u.attribute.attr_name);
        _attr->u.attribute.attr_name = NULL;
    }

    if (_attr->u.attribute.dtype_id >= 0 && H5Tclose(_attr->u.attribute.dtype_id) < 0)
        FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, FAIL, "can't close attribute's datatype");
    if (_attr->u.attribute.space_id >= 0 && H5Sclose(_attr->u.attribute.space_id) < 0)
        FUNC_DONE_ERROR(H5E_DATASPACE, H5E_CANTCLOSEOBJ, FAIL, "can't close attribute's dataspace");

    if (_attr->u.attribute.aapl_id >= 0) {
        if (_attr->u.attribute.aapl_id != H5P_ATTRIBUTE_ACCESS_DEFAULT &&
            H5Pclose(_attr->u.attribute.aapl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close AAPL");
    } /* end if */
    if (_attr->u.attribute.acpl_id >= 0) {
        if (_attr->u.attribute.acpl_id != H5P_ATTRIBUTE_CREATE_DEFAULT &&
            H5Pclose(_attr->u.attribute.acpl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close ACPL");
    } /* end if */

    if (RV_type_info_array_g[H5I_ATTR])
        rv_hash_table_remove(RV_type_info_array_g[H5I_ATTR]->table, (char *)_attr->URI);

    if (RV_file_close(_attr->domain, H5P_DEFAULT, NULL) < 0)
        FUNC_GOTO_ERROR(H5E_FILE, H5E_CANTCLOSEOBJ, FAIL, "couldn't close attr domain");

    RV_free(_attr->u.attribute.parent_name);
    RV_free(_attr->handle_path);
    RV_free(_attr);
    _attr = NULL;

done:
    PRINT_ERROR_STACK;

    return ret_value;
} /* end RV_attr_close() */

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
RV_get_attr_info_callback(char *HTTP_response, const void *callback_data_in, void *callback_data_out)
{
    H5A_info_t *attr_info = (H5A_info_t *)callback_data_out;
    herr_t      ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Retrieving attribute info from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL");
    if (!attr_info)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "attribute info pointer was NULL");

    memset(attr_info, 0, sizeof(*attr_info));

done:
    return ret_value;
} /* end RV_get_attr_info_callback() */

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
RV_attr_iter_callback(char *HTTP_response, const void *callback_data_in, void *callback_data_out)
{
    attr_table_entry *attr_table     = NULL;
    const iter_data  *attr_iter_data = (const iter_data *)callback_data_in;
    size_t            attr_table_num_entries;
    herr_t            ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Iterating through attributes according to server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL");
    if (!attr_iter_data)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "attribute iteration data pointer was NULL");

    /* Build a table of all of the attributes attached to the given object */
    if (H5_INDEX_CRT_ORDER == attr_iter_data->index_type) {
        /* This code assumes that attributes are returned in alphabetical order by default. If the user has
         * requested them by creation order, sort them this way while building the attribute table. If, in the
         * future, attributes are not returned in alphabetical order by default, this code should be changed
         * to reflect this.
         */
        if (RV_build_attr_table(HTTP_response, TRUE, cmp_attributes_by_creation_order, &attr_table,
                                &attr_table_num_entries) < 0)
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTBUILDATTRTABLE, FAIL, "can't build attribute table");

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Attribute table sorted according to creation order\n\n");
#endif
    } /* end if */
    else {
        if (RV_build_attr_table(HTTP_response, FALSE, NULL, &attr_table, &attr_table_num_entries) < 0)
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTBUILDATTRTABLE, FAIL, "can't build attribute table");
    } /* end else */

    /* Begin iteration */
    if (attr_table)
        if (RV_traverse_attr_table(attr_table, attr_table_num_entries, attr_iter_data) < 0)
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_ATTRITERERROR, FAIL, "can't iterate over attribute table");

done:
    if (attr_table)
        RV_free(attr_table);

    return ret_value;
} /* end RV_attr_iter_callback() */

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
    attr_table_entry *table      = NULL;
    yajl_val          parse_tree = NULL, key_obj;
    yajl_val          attr_obj, attr_field_obj;
    size_t            i, num_attributes;
    char             *attribute_section_start, *attribute_section_end;
    herr_t            ret_value = SUCCEED;

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response was NULL");
    if (!attr_table)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "attr table pointer was NULL");
    if (!num_entries)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "attr table num. entries pointer was NULL");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Building table of attributes\n\n");
#endif

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PARSEERROR, FAIL, "parsing JSON failed");

    if (NULL == (key_obj = yajl_tree_get(parse_tree, attributes_keys, yajl_t_array)))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "retrieval of attributes object failed");

    num_attributes = YAJL_GET_ARRAY(key_obj)->len;
    if (num_attributes < 0)
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "number of attributes attached to object was negative");

    /* If this object has no attributes, just finish */
    if (!num_attributes)
        FUNC_GOTO_DONE(SUCCEED);

    if (NULL == (table = RV_malloc(num_attributes * sizeof(*table))))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTALLOC, FAIL, "can't allocate space for attribute table");

    /* Find the beginning of the "attributes" section */
    if (NULL == (attribute_section_start = strstr(HTTP_response, "\"attributes\"")))
        FUNC_GOTO_ERROR(H5E_ATTR, H5E_PARSEERROR, FAIL,
                        "can't find \"attributes\" information section in HTTP response");

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
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "retrieval of attribute name failed");

        if (NULL == (attr_name = YAJL_GET_STRING(attr_field_obj)))
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "returned attribute name was NULL");

        strncpy(table[i].attr_name, attr_name, ATTRIBUTE_NAME_MAX_LENGTH);

        /* Get the current attribute's creation time */
        if (NULL == (attr_field_obj = yajl_tree_get(attr_obj, attr_creation_time_keys, yajl_t_number)))
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_CANTGET, FAIL, "retrieval of attribute creation time failed");

        if (!YAJL_IS_DOUBLE(attr_field_obj))
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "returned attribute creation time is not a double");

        table[i].crt_time = YAJL_GET_DOUBLE(attr_field_obj);

        /* Process the JSON for the current attribute and fill out a H5A_info_t struct for it */

        /* Find the beginning and end of the JSON section for this attribute */
        if (NULL == (attribute_section_start = strstr(attribute_section_start, "{")))
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_PARSEERROR, FAIL,
                            "can't find start of current attribute's JSON section");

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
        if (RV_parse_response(attribute_section_start, NULL, &table[i].attr_info, RV_get_attr_info_callback) <
            0)
            FUNC_GOTO_ERROR(H5E_LINK, H5E_CANTGET, FAIL, "couldn't get link info");

        /* Continue on to the next attribute subsection */
        attribute_section_start = attribute_section_end + 1;
    } /* end for */

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Attribute table built\n\n");
#endif

    if (sort)
        qsort(table, num_attributes, sizeof(*table), sort_func);

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
RV_traverse_attr_table(attr_table_entry *attr_table, size_t num_entries, const iter_data *attr_iter_data)
{
    size_t last_idx;
    herr_t callback_ret;
    herr_t ret_value = SUCCEED;

    switch (attr_iter_data->iter_order) {
        case H5_ITER_NATIVE:
        case H5_ITER_INC: {
#ifdef RV_CONNECTOR_DEBUG
            printf("-> Beginning iteration in increasing order\n\n");
#endif

            for (last_idx = (attr_iter_data->idx_p ? *attr_iter_data->idx_p : 0); last_idx < num_entries;
                 last_idx++) {
#ifdef RV_CONNECTOR_DEBUG
                printf("-> Attribute %zu name: %s\n", last_idx, attr_table[last_idx].attr_name);
                printf("-> Attribute %zu creation time: %f\n", last_idx, attr_table[last_idx].crt_time);
                printf("-> Attribute %zu data size: %llu\n\n", last_idx,
                       attr_table[last_idx].attr_info.data_size);

                printf("-> Calling supplied callback function\n\n");
#endif

                /* Call the user's callback */
                callback_ret = attr_iter_data->iter_function.attr_iter_op(
                    attr_iter_data->iter_obj_id, attr_table[last_idx].attr_name,
                    &attr_table[last_idx].attr_info, attr_iter_data->op_data);
                if (callback_ret < 0)
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_CALLBACK, callback_ret,
                                    "H5Aiterate (_by_name) user callback failed for attribute '%s'",
                                    attr_table[last_idx].attr_name);
                else if (callback_ret > 0)
                    FUNC_GOTO_DONE(callback_ret);
            } /* end for */

            break;
        } /* H5_ITER_NATIVE H5_ITER_INC */

        case H5_ITER_DEC: {
#ifdef RV_CONNECTOR_DEBUG
            printf("-> Beginning iteration in decreasing order\n\n");
#endif

            for (last_idx = (attr_iter_data->idx_p ? *attr_iter_data->idx_p : num_entries - 1); last_idx >= 0;
                 last_idx--) {
#ifdef RV_CONNECTOR_DEBUG
                printf("-> Attribute %zu name: %s\n", last_idx, attr_table[last_idx].attr_name);
                printf("-> Attribute %zu creation time: %f\n", last_idx, attr_table[last_idx].crt_time);
                printf("-> Attribute %zu data size: %llu\n\n", last_idx,
                       attr_table[last_idx].attr_info.data_size);

                printf("-> Calling supplied callback function\n\n");
#endif

                /* Call the user's callback */
                callback_ret = attr_iter_data->iter_function.attr_iter_op(
                    attr_iter_data->iter_obj_id, attr_table[last_idx].attr_name,
                    &attr_table[last_idx].attr_info, attr_iter_data->op_data);
                if (callback_ret < 0)
                    FUNC_GOTO_ERROR(H5E_ATTR, H5E_CALLBACK, callback_ret,
                                    "H5Aiterate (_by_name) user callback failed for attribute '%s'",
                                    attr_table[last_idx].attr_name);
                else if (callback_ret > 0)
                    FUNC_GOTO_DONE(callback_ret);

                if (last_idx == 0)
                    break;
            } /* end for */

            break;
        } /* H5_ITER_DEC */

        case H5_ITER_UNKNOWN:
        case H5_ITER_N:
        default:
            FUNC_GOTO_ERROR(H5E_ATTR, H5E_BADVALUE, FAIL, "unknown attribute iteration order");
    } /* end switch */

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Attribute iteration finished\n\n");
#endif

done:
    return ret_value;
} /* end RV_traverse_attr_table() */

/*-------------------------------------------------------------------------
 * Function:    cmp_attributes_by_creation_order
 *
 * Purpose:     Qsort callback to sort attributes by creation order when
 *              doing attribute iteration
 *
 * Return:      negative if the creation time of attr1 is earlier than that
 *              of attr2
 *              0 if the creation time of attr1 and attr2 are equal
 *              positive if the creation time of attr1 is later than that
 *              of attr2
 *
 * Programmer:  Jordan Henderson
 *              January, 2018
 */
static int
cmp_attributes_by_creation_order(const void *attr1, const void *attr2)
{
    const attr_table_entry *_attr1 = (const attr_table_entry *)attr1;
    const attr_table_entry *_attr2 = (const attr_table_entry *)attr2;

    return ((_attr1->crt_time > _attr2->crt_time) - (_attr1->crt_time < _attr2->crt_time));
} /* end cmp_attributes_by_creation_order() */
