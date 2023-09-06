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
 * Implementations of the dataset callbacks for the HDF5 REST VOL connector.
 */

#include "rest_vol_dataset.h"

/* Set of callbacks for RV_parse_response() */
static herr_t RV_parse_dataset_creation_properties_callback(char *HTTP_response, void *callback_data_in,
                                                            void *callback_data_out);

/* Helper functions for creating a Dataset */
static herr_t RV_setup_dataset_create_request_body(void *parent_obj, const char *name, hid_t type_id,
                                                   hid_t space_id, hid_t lcpl_id, hid_t dcpl,
                                                   char  **create_request_body,
                                                   size_t *create_request_body_len);
static herr_t RV_convert_dataset_creation_properties_to_JSON(hid_t dcpl_id, char **creation_properties_body,
                                                             size_t *creation_properties_body_len);

/* Helper function to convert a selection within an HDF5 Dataspace into a JSON-format string */
static herr_t RV_convert_dataspace_selection_to_string(hid_t space_id, char **selection_string,
                                                       size_t *selection_string_len, hbool_t req_param);

/* Conversion function to convert one or more rest_obj_ref_t objects into a binary buffer for data transfer */
static herr_t RV_convert_obj_refs_to_buffer(const rv_obj_ref_t *ref_array, size_t ref_array_len,
                                            char **buf_out, size_t *buf_out_len);
static herr_t RV_convert_buffer_to_obj_refs(char *ref_buf, size_t ref_buf_len, rv_obj_ref_t **buf_out,
                                            size_t *buf_out_len);

/* H5Dscatter() callback for dataset reads */
static herr_t dataset_read_scatter_op(const void **src_buf, size_t *src_buf_bytes_used, void *op_data);

/* JSON keys to retrieve the various creation properties from a dataset */
const char *creation_properties_keys[]    = {"creationProperties", (const char *)0};
const char *alloc_time_keys[]             = {"allocTime", (const char *)0};
const char *creation_order_keys[]         = {"attributeCreationOrder", (const char *)0};
const char *attribute_phase_change_keys[] = {"attributePhaseChange", (const char *)0};
const char *fill_time_keys[]              = {"fillTime", (const char *)0};
const char *fill_value_keys[]             = {"fillValue", (const char *)0};
const char *filters_keys[]                = {"filters", (const char *)0};
const char *filter_class_keys[]           = {"class", (const char *)0};
const char *filter_ID_keys[]              = {"id", (const char *)0};
const char *layout_keys[]                 = {"layout", (const char *)0};
const char *track_times_keys[]            = {"trackTimes", (const char *)0};
const char *max_compact_keys[]            = {"maxCompact", (const char *)0};
const char *min_dense_keys[]              = {"minDense", (const char *)0};
const char *layout_class_keys[]           = {"class", (const char *)0};
const char *chunk_dims_keys[]             = {"dims", (const char *)0};
const char *external_storage_keys[]       = {"externalStorage", (const char *)0};

/* Defines for Dataset operations */
#define DATASET_CREATION_PROPERTIES_BODY_DEFAULT_SIZE 512
#define DATASET_CREATE_MAX_COMPACT_ATTRIBUTES_DEFAULT 8
#define DATASET_CREATE_MIN_DENSE_ATTRIBUTES_DEFAULT   6
#define OBJECT_REF_STRING_LEN                         48

/* Default sizes for strings formed when dealing with turning a
 * representation of an HDF5 dataspace and a selection within one into JSON
 */
#define DATASPACE_SELECTION_STRING_DEFAULT_SIZE 512
#define DATASPACE_MAX_RANK                      32

/* Defines for the use of the LZF and ScaleOffset filters */
#define LZF_FILTER_ID                                                                                        \
    32000 /* Avoid calling this 'H5Z_FILTER_LZF'; The HDF5 Library could potentially add 'H5Z_FILTER_LZF' in \
             the future */
#define H5Z_SCALEOFFSET_PARM_SCALETYPE   0 /* ScaleOffset filter "User" parameter for scale type */
#define H5Z_SCALEOFFSET_PARM_SCALEFACTOR 1 /* ScaleOffset filter "User" parameter for scale factor */

/* Default size for the buffer to allocate during base64-encoding if the caller
 * of RV_base64_encode supplies a 0-sized buffer.
 */
#define BASE64_ENCODE_DEFAULT_BUFFER_SIZE 33554432 /* 32MB */

/*-------------------------------------------------------------------------
 * Function:    RV_dataset_create
 *
 * Purpose:     Creates an HDF5 dataset by making the appropriate REST API
 *              call to the server and allocating an internal memory struct
 *              object for the dataset.
 *
 * Return:      Pointer to an RV_object_t struct corresponding to the
 *              newly-created dataset on success/NULL on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
void *
RV_dataset_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t lcpl_id,
                  hid_t type_id, hid_t space_id, hid_t dcpl_id, hid_t dapl_id, hid_t dxpl_id, void **req)
{
    RV_object_t *parent                  = (RV_object_t *)obj;
    RV_object_t *new_dataset             = NULL;
    curl_off_t   create_request_body_len = 0;
    size_t       host_header_len         = 0;
    size_t       path_size               = 0;
    size_t       path_len                = 0;
    char        *host_header             = NULL;
    char        *create_request_body     = NULL;
    char         request_url[URL_MAX_LENGTH];
    int          url_len   = 0;
    void        *ret_value = NULL;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received dataset create call with following parameters:\n");
    printf("     - H5Dcreate variant: %s\n", name ? "H5Dcreate2" : "H5Dcreate_anon");
    if (name)
        printf("     - Dataset's name: %s\n", name);
    printf("     - Dataset's parent object URI: %s\n", parent->URI);
    printf("     - Dataset's parent object type: %s\n", object_type_to_string(parent->obj_type));
    printf("     - Dataset's parent object domain path: %s\n", parent->domain->u.file.filepath_name);
    printf("     - Default DCPL? %s\n", (H5P_DATASET_CREATE_DEFAULT == dcpl_id) ? "yes" : "no");
    printf("     - Default DAPL? %s\n\n", (H5P_DATASET_ACCESS_DEFAULT == dapl_id) ? "yes" : "no");
#endif

    if (H5I_FILE != parent->obj_type && H5I_GROUP != parent->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object not a file or group");

    /* Check for write access */
    if (!(parent->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, NULL, "no write intent on file");

    /* Allocate and setup internal Dataset struct */
    if (NULL == (new_dataset = (RV_object_t *)RV_malloc(sizeof(*new_dataset))))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, NULL, "can't allocate space for dataset object");

    new_dataset->URI[0]             = '\0';
    new_dataset->obj_type           = H5I_DATASET;
    new_dataset->u.dataset.dtype_id = FAIL;
    new_dataset->u.dataset.space_id = FAIL;
    new_dataset->u.dataset.dapl_id  = FAIL;
    new_dataset->u.dataset.dcpl_id  = FAIL;

    new_dataset->domain = parent->domain;
    parent->domain->u.file.ref_count++;

    new_dataset->handle_path = NULL;

    if (RV_set_object_handle_path(name, parent->handle_path, &new_dataset->handle_path) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_PATH, NULL, "can't set up object path");

    /* Copy the DAPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * H5Dget_access_plist() will function correctly
     */
    if (H5P_DATASET_ACCESS_DEFAULT != dapl_id) {
        if ((new_dataset->u.dataset.dapl_id = H5Pcopy(dapl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy DAPL");
    } /* end if */
    else
        new_dataset->u.dataset.dapl_id = H5P_DATASET_ACCESS_DEFAULT;

    /* Copy the DCPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * H5Dget_create_plist() will function correctly
     */
    if (H5P_DATASET_CREATE_DEFAULT != dcpl_id) {
        if ((new_dataset->u.dataset.dcpl_id = H5Pcopy(dcpl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, NULL, "can't copy DCPL");
    } /* end if */
    else
        new_dataset->u.dataset.dcpl_id = H5P_DATASET_CREATE_DEFAULT;

    /* Form the request body to give the new Dataset its properties */
    {
        size_t tmp_len = 0;

        if (RV_setup_dataset_create_request_body(obj, name, type_id, space_id, lcpl_id, dcpl_id,
                                                 &create_request_body, &tmp_len) < 0)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCONVERT, NULL,
                            "can't convert dataset creation parameters to JSON");

        /* Check to make sure that the size of the create request HTTP body can safely be cast to a curl_off_t
         */
        if (sizeof(curl_off_t) < sizeof(size_t))
            ASSIGN_TO_SMALLER_SIZE(create_request_body_len, curl_off_t, tmp_len, size_t)
        else if (sizeof(curl_off_t) > sizeof(size_t))
            create_request_body_len = (curl_off_t)tmp_len;
        else
            ASSIGN_TO_SAME_SIZE_UNSIGNED_TO_SIGNED(create_request_body_len, curl_off_t, tmp_len, size_t)
    }

    /* Setup the host header */
    host_header_len = strlen(parent->domain->u.file.filepath_name) + strlen(host_string) + 1;
    if (NULL == (host_header = (char *)RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, NULL, "can't allocate space for request Host header");

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, parent->domain->u.file.filepath_name,
                                                           host_header_len - strlen(host_string) - 1));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    /* Instruct cURL that we are sending JSON */
    curl_headers = curl_slist_append(curl_headers, "Content-Type: application/json");

    /* Redirect cURL from the base URL to "/datasets" to create the dataset */
    if ((url_len = snprintf(request_url, URL_MAX_LENGTH, "%s/datasets", base_URL)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, NULL, "snprintf error");

    if (url_len >= URL_MAX_LENGTH)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, NULL,
                        "dataset create URL size exceeded maximum URL size");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Dataset creation request URL: %s\n\n", request_url);
#endif

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, NULL, "can't set cURL HTTP headers: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POST, 1))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, NULL, "can't set up cURL to make HTTP POST request: %s",
                        curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDS, create_request_body))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, NULL, "can't set cURL POST data: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, create_request_body_len))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, NULL, "can't set cURL POST data size: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, NULL, "can't set cURL request URL: %s", curl_err_buf);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Creating dataset\n\n");

    printf("   /***********************************\\\n");
    printf("-> | Making POST request to the server |\n");
    printf("   \\***********************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_DATASET, H5E_CANTCREATE, NULL);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Created dataset\n\n");
#endif

    /* Store the newly-created dataset's URI */
    if (RV_parse_response(response_buffer.buffer, NULL, new_dataset->URI, RV_copy_object_URI_callback) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCREATE, NULL, "can't parse new dataset's URI");

    if ((new_dataset->u.dataset.dtype_id = H5Tcopy(type_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCOPY, NULL, "failed to copy dataset's datatype");
    if ((new_dataset->u.dataset.space_id = H5Scopy(space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCOPY, NULL, "failed to copy dataset's dataspace");

    ret_value = (void *)new_dataset;

done:
#ifdef RV_CONNECTOR_DEBUG
    printf("-> Dataset create response buffer:\n%s\n\n", response_buffer.buffer);

    if (new_dataset && ret_value) {
        printf("-> New dataset's info:\n");
        printf("     - New dataset's URI: %s\n", new_dataset->URI);
        printf("     - New dataset's object type: %s\n", object_type_to_string(new_dataset->obj_type));
        printf("     - New dataset's domain path: %s\n\n", new_dataset->domain->u.file.filepath_name);
    } /* end if */
#endif

    if (create_request_body)
        RV_free(create_request_body);
    if (host_header)
        RV_free(host_header);

    /* Clean up allocated dataset object if there was an issue */
    if (new_dataset && !ret_value)
        if (RV_dataset_close(new_dataset, FAIL, NULL) < 0)
            FUNC_DONE_ERROR(H5E_DATASET, H5E_CANTCLOSEOBJ, NULL, "can't close dataset");

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    PRINT_ERROR_STACK;

    return ret_value;
} /* end RV_dataset_create() */

/*-------------------------------------------------------------------------
 * Function:    RV_dataset_open
 *
 * Purpose:     Opens an existing HDF5 dataset by retrieving its URI,
 *              dataspace and datatype info from the server and allocating
 *              an internal memory struct object for the dataset.
 *
 * Return:      Pointer to an RV_object_t struct corresponding to
 *              the opened dataset on success/NULL on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
void *
RV_dataset_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t dapl_id,
                hid_t dxpl_id, void **req)
{
    RV_object_t *parent   = (RV_object_t *)obj;
    RV_object_t *dataset  = NULL;
    H5I_type_t   obj_type = H5I_UNINIT;
    htri_t       search_ret;
    void        *ret_value = NULL;
    loc_info     loc_info_out;
    size_t       path_size = 0;
    size_t       path_len  = 0;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received dataset open call with following parameters:\n");
    printf("     - loc_id object's URI: %s\n", parent->URI);
    printf("     - loc_id object's type: %s\n", object_type_to_string(parent->obj_type));
    printf("     - loc_id object's domain path: %s\n", parent->domain->u.file.filepath_name);
    printf("     - Path to dataset: %s\n", name);
    printf("     - Default DAPL? %s\n\n", (H5P_DATASET_ACCESS_DEFAULT == dapl_id) ? "yes" : "no");
#endif

    if (H5I_FILE != parent->obj_type && H5I_GROUP != parent->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "parent object not a file or group");

    /* Allocate and setup internal Dataset struct */
    if (NULL == (dataset = (RV_object_t *)RV_malloc(sizeof(*dataset))))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, NULL, "can't allocate space for dataset object");

    dataset->URI[0]             = '\0';
    dataset->obj_type           = H5I_DATASET;
    dataset->u.dataset.dtype_id = FAIL;
    dataset->u.dataset.space_id = FAIL;
    dataset->u.dataset.dapl_id  = FAIL;
    dataset->u.dataset.dcpl_id  = FAIL;

    /* Copy information about file that the newly-created dataset is in */
    dataset->domain = parent->domain;
    parent->domain->u.file.ref_count++;

    dataset->handle_path = NULL;

    if (RV_set_object_handle_path(name, parent->handle_path, &dataset->handle_path) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_PATH, NULL, "can't set up object path");

    loc_info_out.URI         = dataset->URI;
    loc_info_out.domain      = dataset->domain;
    loc_info_out.GCPL_base64 = NULL;

    /* Locate dataset and set domain */
    search_ret = RV_find_object_by_path(parent, name, &obj_type, RV_copy_object_loc_info_callback, NULL,
                                        &loc_info_out);
    if (!search_ret || search_ret < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_PATH, NULL, "can't locate dataset by path");

    dataset->domain = loc_info_out.domain;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Found dataset by given path\n\n");
#endif

    /* Set up a Dataspace for the opened Dataset */
    if ((dataset->u.dataset.space_id = RV_parse_dataspace(response_buffer.buffer)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCONVERT, NULL,
                        "can't convert JSON to usable dataspace for dataset");

    /* Set up a Datatype for the opened Dataset */
    if ((dataset->u.dataset.dtype_id = RV_parse_datatype(response_buffer.buffer, TRUE)) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, NULL,
                        "can't convert JSON to usable datatype for dataset");

    /* Copy the DAPL if it wasn't H5P_DEFAULT, else set up a default one so that
     * H5Dget_access_plist() will function correctly
     */
    if (H5P_DATASET_ACCESS_DEFAULT != dapl_id) {
        if ((dataset->u.dataset.dapl_id = H5Pcopy(dapl_id)) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "can't copy DAPL");
    } /* end if */
    else
        dataset->u.dataset.dapl_id = H5P_DATASET_ACCESS_DEFAULT;

    /* Set up a DCPL for the dataset so that H5Dget_create_plist() will function correctly */
    if ((dataset->u.dataset.dcpl_id = H5Pcreate(H5P_DATASET_CREATE)) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "can't create DCPL for dataset");

    /* Set any necessary creation properties on the DCPL setup for the dataset */
    if (RV_parse_response(response_buffer.buffer, NULL, &dataset->u.dataset.dcpl_id,
                          RV_parse_dataset_creation_properties_callback) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCREATE, NULL,
                        "can't parse dataset's creation properties from JSON representation");

    ret_value = (void *)dataset;

done:
#ifdef RV_CONNECTOR_DEBUG
    printf("-> Dataset open response buffer:\n%s\n\n", response_buffer.buffer);

    if (dataset && ret_value) {
        printf("-> Dataset's info:\n");
        printf("     - Dataset's URI: %s\n", dataset->URI);
        printf("     - Dataset's object type: %s\n", object_type_to_string(dataset->obj_type));
        printf("     - Dataset's domain path: %s\n", dataset->domain->u.file.filepath_name);
        printf("     - Dataset's datatype class: %s\n\n",
               datatype_class_to_string(dataset->u.dataset.dtype_id));
    }
#endif

    /* Clean up allocated dataset object if there was an issue */
    if (dataset && !ret_value)
        if (RV_dataset_close(dataset, FAIL, NULL) < 0)
            FUNC_DONE_ERROR(H5E_DATASET, H5E_CANTCLOSEOBJ, NULL, "can't close dataset");

    PRINT_ERROR_STACK;

    return ret_value;
} /* end RV_dataset_open() */

/*-------------------------------------------------------------------------
 * Function:    RV_dataset_read
 *
 * Purpose:     Reads data from an HDF5 dataset according to the given
 *              memory dataspace by making the appropriate REST API call to
 *              the server.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
herr_t
RV_dataset_read(size_t count, void *dset[], hid_t mem_type_id[], hid_t mem_space_id[], hid_t file_space_id[],
                hid_t dxpl_id, void *buf[], void **req)
{
    H5S_sel_type sel_type = H5S_SEL_ALL;
    RV_object_t *dataset  = (RV_object_t *)dset[0];
    H5T_class_t  dtype_class;
    hssize_t     mem_select_npoints, file_select_npoints;
    hbool_t      is_transfer_binary = FALSE;
    htri_t       is_variable_str;
    size_t       read_data_size;
    size_t       selection_body_len = 0;
    size_t       host_header_len    = 0;
    char        *host_header        = NULL;
    char        *selection_body     = NULL;
    void        *obj_ref_buf        = NULL;
    char         request_url[URL_MAX_LENGTH];
    int          url_len   = 0;
    herr_t       ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received dataset read call with following parameters:\n");
    printf("     - Dataset's URI: %s\n", dataset->URI);
    printf("     - Dataset's object type: %s\n", object_type_to_string(dataset->obj_type));
    printf("     - Dataset's domain path: %s\n", dataset->domain->u.file.filepath_name);
    printf("     - Entire memory dataspace selected? %s\n", (mem_space_id[0] == H5S_ALL) ? "yes" : "no");
    printf("     - Entire file dataspace selected? %s\n", (file_space_id[0] == H5S_ALL) ? "yes" : "no");
    printf("     - Default DXPL? %s\n\n", (dxpl_id == H5P_DATASET_XFER_DEFAULT) ? "yes" : "no");
#endif

    if (count > 1)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_UNSUPPORTED, FAIL, "multiple datasets are unsupported");
    if (H5I_DATASET != dataset->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a dataset");
    if (!buf[0])
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "read buffer was NULL");

    /* Determine whether it's possible to send the data as a binary blob instead of a JSON array */
    if (H5T_NO_CLASS == (dtype_class = H5Tget_class(mem_type_id[0])))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid");

    if ((is_variable_str = H5Tis_variable_str(mem_type_id[0])) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid");

    /* Only perform a binary transfer for fixed-length datatype datasets with an
     * All or Hyperslab selection. Point selections are dealt with by POSTing the
     * point list as JSON in the request body.
     */
    is_transfer_binary = (H5T_VLEN != dtype_class) && !is_variable_str;

    /* Follow the semantics for the use of H5S_ALL */
    if (H5S_ALL == mem_space_id[0] && H5S_ALL == file_space_id[0]) {
        /* The file dataset's dataspace is used for the memory dataspace
         * and the selection within the memory dataspace is set to the
         * "all" selection. The selection within the file dataset's
         * dataspace is set to the "all" selection.
         */
        mem_space_id[0] = file_space_id[0] = dataset->u.dataset.space_id;
        H5Sselect_all(file_space_id[0]);
    } /* end if */
    else if (H5S_ALL == file_space_id[0]) {
        /* mem_space_id specifies the memory dataspace and the selection
         * within it. The selection within the file dataset's dataspace
         * is set to the "all" selection.
         */
        file_space_id[0] = dataset->u.dataset.space_id;
        H5Sselect_all(file_space_id[0]);
    } /* end if */
    else {
        /* The file dataset's dataspace is used for the memory dataspace
         * and the selection specified with file_space_id specifies the
         * selection within it. The combination of the file dataset's
         * dataspace and the selection from file_space_id is used for
         * memory also.
         */
        if (H5S_ALL == mem_space_id[0]) {
            mem_space_id[0] = dataset->u.dataset.space_id;

            /* Copy the selection from file_space_id into the mem_space_id. */
            if (H5Sselect_copy(mem_space_id[0], file_space_id[0]) < 0)
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCOPY, FAIL,
                                "can't copy selection from file space to memory space");
        } /* end if */

        /* Since the selection in the dataset's file dataspace is not set
         * to "all", convert the selection into JSON */

        /* Retrieve the selection type to choose how to format the dataspace selection */
        if (H5S_SEL_ERROR == (sel_type = H5Sget_select_type(file_space_id[0])))
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't get dataspace selection type");
        is_transfer_binary = is_transfer_binary && (H5S_SEL_POINTS != sel_type);

        if (RV_convert_dataspace_selection_to_string(file_space_id[0], &selection_body, &selection_body_len,
                                                     is_transfer_binary) < 0)
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCONVERT, FAIL,
                            "can't convert dataspace selection to string representation");
    } /* end else */

    /* Verify that the number of selected points matches */
    if ((mem_select_npoints = H5Sget_select_npoints(mem_space_id[0])) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "memory dataspace is invalid");
    if ((file_select_npoints = H5Sget_select_npoints(file_space_id[0])) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "file dataspace is invalid");
    if (mem_select_npoints != file_select_npoints)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL,
                        "memory selection num points != file selection num points");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> %lld points selected in file dataspace\n", file_select_npoints);
    printf("-> %lld points selected in memory dataspace\n\n", mem_select_npoints);
#endif

    /* Setup the host header */
    host_header_len = strlen(dataset->domain->u.file.filepath_name) + strlen(host_string) + 1;
    if (NULL == (host_header = (char *)RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header");

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, dataset->domain->u.file.filepath_name,
                                                           host_header_len - strlen(host_string) - 1));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    /* Instruct cURL on which type of transfer to perform, binary or JSON */
    curl_headers = curl_slist_append(curl_headers, is_transfer_binary ? "Accept: application/octet-stream"
                                                                      : "Accept: application/json");

    /* Redirect cURL from the base URL to "/datasets/<id>/value" to get the dataset data values */
    if ((url_len = snprintf(
             request_url, URL_MAX_LENGTH, "%s/datasets/%s/value%s%s", base_URL, dataset->URI,
             is_transfer_binary && selection_body && (H5S_SEL_POINTS != sel_type) ? "?select=" : "",
             is_transfer_binary && selection_body && (H5S_SEL_POINTS != sel_type) ? selection_body : "")) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error");

    if (url_len >= URL_MAX_LENGTH)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "dataset read URL size exceeded maximum URL size");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Dataset read URL: %s\n\n", request_url);
#endif

    /* If using a point selection, instruct cURL to perform a POST request
     * in order to post the point list. Otherwise, a simple GET request
     * can be made, where the selection body should have already been
     * added as a request parameter to the GET URL.
     */
    if (H5S_SEL_POINTS == sel_type) {
        curl_off_t post_len;

        /* As the dataspace-selection-to-string function is not designed to include the enclosing '{' and '}',
         * since returning just the selection string to the user makes more sense if they are including more
         * elements in their JSON, we have to wrap the selection body here before sending it off to cURL
         */

        /* Ensure we have enough space to add the enclosing '{' and '}' */
        if (NULL == (selection_body = (char *)RV_realloc(selection_body, selection_body_len + 3)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL,
                            "can't reallocate space for point selection body");

        /* Shift the whole string down by a byte */
        memmove(selection_body + 1, selection_body, selection_body_len + 1);

        /* Add in the braces */
        selection_body[0]                      = '{';
        selection_body[selection_body_len + 1] = '}';
        selection_body[selection_body_len + 2] = '\0';

        /* Check to make sure that the size of the selection HTTP body can safely be cast to a curl_off_t */
        if (sizeof(curl_off_t) < sizeof(size_t))
            ASSIGN_TO_SMALLER_SIZE(post_len, curl_off_t, selection_body_len + 2, size_t)
        else if (sizeof(curl_off_t) > sizeof(size_t))
            post_len = (curl_off_t)(selection_body_len + 2);
        else
            ASSIGN_TO_SAME_SIZE_UNSIGNED_TO_SIGNED(post_len, curl_off_t, selection_body_len + 2, size_t)

        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POST, 1))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP POST request: %s",
                            curl_err_buf);
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDS, selection_body))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL POST data: %s", curl_err_buf);
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, post_len))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL POST data size: %s",
                            curl_err_buf);

        curl_headers = curl_slist_append(curl_headers, "Content-Type: application/json");

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Setup cURL to POST point list for dataset read\n\n");
#endif
    } /* end if */
    else {
        if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP GET request: %s",
                            curl_err_buf);
    } /* end else */

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Reading dataset\n\n");

    printf("   /***************************************\\\n");
    printf("-> | Making GET/POST request to the server |\n");
    printf("   \\***************************************/\n\n");
#endif

    CURL_PERFORM(curl, H5E_DATASET, H5E_READERROR, FAIL);

    if ((H5T_REFERENCE != dtype_class) && (H5T_VLEN != dtype_class) && !is_variable_str) {
        size_t dtype_size;

        if (0 == (dtype_size = H5Tget_size(mem_type_id[0])))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid");

        /* Scatter the read data out to the supplied read buffer according to the mem_type_id
         * and mem_space_id given */
        read_data_size = (size_t)file_select_npoints * dtype_size;
        if (H5Dscatter(dataset_read_scatter_op, &read_data_size, mem_type_id[0], mem_space_id[0], buf[0]) < 0)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_READERROR, FAIL, "can't scatter data to read buffer");
    } /* end if */
    else {
        if (H5T_STD_REF_OBJ == mem_type_id[0]) {
            /* Convert the received binary buffer into a buffer of rest_obj_ref_t's */
            if (RV_convert_buffer_to_obj_refs(response_buffer.buffer, (size_t)file_select_npoints,
                                              (rv_obj_ref_t **)&obj_ref_buf, &read_data_size) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL,
                                "can't convert ref string/s to object ref array");

            memcpy(buf[0], obj_ref_buf, read_data_size);
        } /* end if */
    }     /* end else */

done:
#ifdef RV_CONNECTOR_DEBUG
    printf("-> Dataset read response buffer:\n%s\n\n", response_buffer.buffer);
#endif

    if (obj_ref_buf)
        RV_free(obj_ref_buf);
    if (host_header)
        RV_free(host_header);
    if (selection_body)
        RV_free(selection_body);

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    PRINT_ERROR_STACK;

    return ret_value;
} /* end RV_dataset_read() */

/*-------------------------------------------------------------------------
 * Function:    RV_dataset_write
 *
 * Purpose:     Writes data to an HDF5 dataset according to the given
 *              memory dataspace by making the appropriate REST API call to
 *              the server.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
herr_t
RV_dataset_write(size_t count, void *dset[], hid_t mem_type_id[], hid_t mem_space_id[], hid_t file_space_id[],
                 hid_t dxpl_id, const void *buf[], void **req)
{
    H5S_sel_type sel_type = H5S_SEL_ALL;
    RV_object_t *dataset  = (RV_object_t *)dset[0];
    upload_info  uinfo;
    H5T_class_t  dtype_class;
    curl_off_t   write_len;
    hssize_t     mem_select_npoints, file_select_npoints;
    hbool_t      is_transfer_binary = FALSE;
    htri_t       is_variable_str;
    size_t       host_header_len      = 0;
    size_t       write_body_len       = 0;
    size_t       selection_body_len   = 0;
    char        *selection_body       = NULL;
    char        *base64_encoded_value = NULL;
    char        *host_header          = NULL;
    char        *write_body           = NULL;
    char         request_url[URL_MAX_LENGTH];
    int          url_len   = 0;
    herr_t       ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received dataset write call with following parameters:\n");
    printf("     - Dataset's URI: %s\n", dataset->URI);
    printf("     - Dataset's object type: %s\n", object_type_to_string(dataset->obj_type));
    printf("     - Dataset's domain path: %s\n", dataset->domain->u.file.filepath_name);
    printf("     - Entire memory dataspace selected? %s\n", (mem_space_id[0] == H5S_ALL) ? "yes" : "no");
    printf("     - Entire file dataspace selected? %s\n", (file_space_id[0] == H5S_ALL) ? "yes" : "no");
    printf("     - Default DXPL? %s\n\n", (dxpl_id == H5P_DATASET_XFER_DEFAULT) ? "yes" : "no");
#endif

    if (count > 1)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_UNSUPPORTED, FAIL, "multiple datasets are unsupported");
    if (H5I_DATASET != dataset->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a dataset");
    if (!buf[0])
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "write buffer was NULL");

    /* Check for write access */
    if (!(dataset->domain->u.file.intent & H5F_ACC_RDWR))
        FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "no write intent on file");

    /* Determine whether it's possible to send the data as a binary blob instead of as JSON */
    if (H5T_NO_CLASS == (dtype_class = H5Tget_class(mem_type_id[0])))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid");

    if ((is_variable_str = H5Tis_variable_str(mem_type_id[0])) < 0)
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid");

    /* Only perform a binary transfer for fixed-length datatype datasets with an
     * All or Hyperslab selection. Point selections are dealt with by POSTing the
     * point list as JSON in the request body.
     */
    is_transfer_binary = (H5T_VLEN != dtype_class) && !is_variable_str;

    /* Follow the semantics for the use of H5S_ALL */
    if (H5S_ALL == mem_space_id[0] && H5S_ALL == file_space_id[0]) {
        /* The file dataset's dataspace is used for the memory dataspace
         * and the selection within the memory dataspace is set to the
         * "all" selection. The selection within the file dataset's
         * dataspace is set to the "all" selection.
         */
        mem_space_id[0] = file_space_id[0] = dataset->u.dataset.space_id;
        H5Sselect_all(file_space_id[0]);
    } /* end if */
    else if (H5S_ALL == file_space_id[0]) {
        /* mem_space_id specifies the memory dataspace and the selection
         * within it. The selection within the file dataset's dataspace
         * is set to the "all" selection.
         */
        file_space_id[0] = dataset->u.dataset.space_id;
        H5Sselect_all(file_space_id[0]);
    } /* end if */
    else {
        /* The file dataset's dataspace is used for the memory dataspace
         * and the selection specified with file_space_id specifies the
         * selection within it. The combination of the file dataset's
         * dataspace and the selection from file_space_id is used for
         * memory also.
         */
        if (H5S_ALL == mem_space_id[0]) {
            mem_space_id[0] = dataset->u.dataset.space_id;

            /* Copy the selection from file_space_id into the mem_space_id */
            if (H5Sselect_copy(mem_space_id[0], file_space_id[0]) < 0)
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCOPY, FAIL,
                                "can't copy selection from file space to memory space");
        } /* end if */

        /* Since the selection in the dataset's file dataspace is not set
         * to "all", convert the selection into JSON */

        /* Retrieve the selection type here for later use */
        if (H5S_SEL_ERROR == (sel_type = H5Sget_select_type(file_space_id[0])))
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't get dataspace selection type");
        is_transfer_binary = is_transfer_binary && (H5S_SEL_POINTS != sel_type);

        if (RV_convert_dataspace_selection_to_string(file_space_id[0], &selection_body, &selection_body_len,
                                                     is_transfer_binary) < 0)
            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTCONVERT, FAIL,
                            "can't convert dataspace selection to string representation");
    } /* end else */

    /* Verify that the number of selected points matches */
    if ((mem_select_npoints = H5Sget_select_npoints(mem_space_id[0])) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "memory dataspace is invalid");
    if ((file_select_npoints = H5Sget_select_npoints(file_space_id[0])) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "file dataspace is invalid");
    if (mem_select_npoints != file_select_npoints)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL,
                        "memory selection num points != file selection num points");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> %lld points selected in file dataspace\n", file_select_npoints);
    printf("-> %lld points selected in memory dataspace\n\n", mem_select_npoints);
#endif

    /* Setup the size of the data being transferred and the data buffer itself (for non-simple
     * types like object references or variable length types)
     */
    if ((H5T_REFERENCE != dtype_class) && (H5T_VLEN != dtype_class) && !is_variable_str) {
        size_t dtype_size;

        if (0 == (dtype_size = H5Tget_size(mem_type_id[0])))
            FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_BADVALUE, FAIL, "memory datatype is invalid");

        write_body_len = (size_t)file_select_npoints * dtype_size;
    } /* end if */
    else {
        if (H5T_STD_REF_OBJ == mem_type_id[0]) {
            /* Convert the buffer of rest_obj_ref_t's to a binary buffer */
            if (RV_convert_obj_refs_to_buffer((const rv_obj_ref_t *)buf[0], (size_t)file_select_npoints,
                                              &write_body, &write_body_len) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCONVERT, FAIL,
                                "can't convert object ref/s to ref string/s");
            buf[0] = write_body;
        } /* end if */
    }     /* end else */

    /* Setup the host header */
    host_header_len = strlen(dataset->domain->u.file.filepath_name) + strlen(host_string) + 1;
    if (NULL == (host_header = (char *)RV_malloc(host_header_len)))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL, "can't allocate space for request Host header");

    strcpy(host_header, host_string);

    curl_headers = curl_slist_append(curl_headers, strncat(host_header, dataset->domain->u.file.filepath_name,
                                                           host_header_len - strlen(host_string) - 1));

    /* Disable use of Expect: 100 Continue HTTP response */
    curl_headers = curl_slist_append(curl_headers, "Expect:");

    /* Instruct cURL on which type of transfer to perform, binary or JSON */
    curl_headers =
        curl_slist_append(curl_headers, is_transfer_binary ? "Content-Type: application/octet-stream"
                                                           : "Content-Type: application/json");

    /* Redirect cURL from the base URL to "/datasets/<id>/value" to write the value out */
    if ((url_len = snprintf(
             request_url, URL_MAX_LENGTH, "%s/datasets/%s/value%s%s", base_URL, dataset->URI,
             is_transfer_binary && selection_body && (H5S_SEL_POINTS != sel_type) ? "?select=" : "",
             is_transfer_binary && selection_body && (H5S_SEL_POINTS != sel_type) ? selection_body : "")) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error");

    if (url_len >= URL_MAX_LENGTH)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "dataset write URL size exceeded maximum URL size");

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Dataset write URL: %s\n\n", request_url);
#endif

    /* If using a point selection, instruct cURL to perform a POST request in order to post the
     * point list. Otherwise, a PUT request is made to the server.
     */
    if (H5S_SEL_POINTS == sel_type) {
        const char *const fmt_string = "{%s,\"value_base64\": \"%s\"}";
        size_t            value_body_len;
        int               bytes_printed;

        /* Since base64 encoding generally introduces 33% overhead for encoding,
         * go ahead and allocate a buffer 4/3 the size of the given write buffer
         * in order to try and avoid reallocations inside the encoding function.
         */
        value_body_len = (size_t)((4.0 / 3.0) * (double)write_body_len);
        if (NULL == (base64_encoded_value = RV_malloc(value_body_len)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL,
                            "can't allocate temporary buffer for base64-encoded write buffer");

        if (RV_base64_encode(buf[0], write_body_len, &base64_encoded_value, &value_body_len) < 0)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTENCODE, FAIL, "can't base64-encode write buffer");

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Base64-encoded data buffer: %s\n\n", base64_encoded_value);
#endif

        write_body_len = (strlen(fmt_string) - 4) + selection_body_len + value_body_len;
        if (NULL == (write_body = RV_malloc(write_body_len + 1)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL, "can't allocate space for write buffer");

        if ((bytes_printed = snprintf(write_body, write_body_len + 1, fmt_string, selection_body,
                                      base64_encoded_value)) < 0)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error");

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Write body: %s\n\n", write_body);
#endif

        if (bytes_printed >= write_body_len + 1)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL,
                            "point selection write buffer exceeded allocated buffer size");

        curl_headers = curl_slist_append(curl_headers, "Content-Type: application/json");

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Setup cURL to POST point list for dataset write\n\n");
#endif
    } /* end if */

    uinfo.buffer      = is_transfer_binary ? buf[0] : write_body;
    uinfo.buffer_size = write_body_len;
    uinfo.bytes_sent  = 0;

    /* Check to make sure that the size of the write body can safely be cast to a curl_off_t */
    if (sizeof(curl_off_t) < sizeof(size_t))
        ASSIGN_TO_SMALLER_SIZE(write_len, curl_off_t, write_body_len, size_t)
    else if (sizeof(curl_off_t) > sizeof(size_t))
        write_len = (curl_off_t)write_body_len;
    else
        ASSIGN_TO_SAME_SIZE_UNSIGNED_TO_SIGNED(write_len, curl_off_t, write_body_len, size_t)

    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_UPLOAD, 1))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set up cURL to make HTTP PUT request: %s",
                        curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_READDATA, &uinfo))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL PUT data: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, write_len))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL PUT data size: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s", curl_err_buf);
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL request URL: %s", curl_err_buf);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Writing dataset\n\n");

    printf("   /**********************************\\\n");
    printf("-> | Making PUT request to the server |\n");
    printf("   \\**********************************/\n\n");
#endif

    if (write_len > 0)
        CURL_PERFORM(curl, H5E_DATASET, H5E_WRITEERROR, FAIL);

done:
#ifdef RV_CONNECTOR_DEBUG
    printf("-> Dataset write response buffer:\n%s\n\n", response_buffer.buffer);
#endif

    if (base64_encoded_value)
        RV_free(base64_encoded_value);
    if (host_header)
        RV_free(host_header);
    if (write_body)
        RV_free(write_body);
    if (selection_body)
        RV_free(selection_body);

    /* Unset cURL UPLOAD option to ensure that future requests don't try to use PUT calls */
    if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_UPLOAD, 0))
        FUNC_DONE_ERROR(H5E_ATTR, H5E_CANTSET, FAIL, "can't unset cURL PUT option: %s", curl_err_buf);

    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    } /* end if */

    PRINT_ERROR_STACK;

    return ret_value;
} /* end RV_dataset_write() */

/*-------------------------------------------------------------------------
 * Function:    RV_dataset_get
 *
 * Purpose:     Performs a "GET" operation on an HDF5 dataset, such as
 *              calling the H5Dget_type routine.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
herr_t
RV_dataset_get(void *obj, H5VL_dataset_get_args_t *args, hid_t dxpl_id, void **req)
{
    RV_object_t *dset      = (RV_object_t *)obj;
    herr_t       ret_value = SUCCEED;

    size_t host_header_len = 0;
    char  *host_header     = NULL;
    char   request_url[URL_MAX_LENGTH];

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received dataset get call with following parameters:\n");
    printf("     - Dataset get call type: %s\n", dataset_get_type_to_string(args->op_type));
    printf("     - Dataset's URI: %s\n", dset->URI);
    printf("     - Dataset's object type: %s\n", object_type_to_string(dset->obj_type));
    printf("     - Dataset's domain path: %s\n\n", dset->domain->u.file.filepath_name);
#endif

    if (H5I_DATASET != dset->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a dataset");

    switch (args->op_type) {
        /* H5Dget_access_plist */
        case H5VL_DATASET_GET_DAPL: {
            hid_t *ret_id = &args->args.get_dapl.dapl_id;

            if ((*ret_id = H5Pcopy(dset->u.dataset.dapl_id)) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, FAIL, "can't copy Dataset DAPL");

            break;
        } /* H5VL_DATASET_GET_DAPL */

        /* H5Dget_create_plist */
        case H5VL_DATASET_GET_DCPL: {
            hid_t *ret_id = &args->args.get_dcpl.dcpl_id;

            if ((*ret_id = H5Pcopy(dset->u.dataset.dcpl_id)) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTCOPY, FAIL, "can't copy Dataset DCPL");

            break;
        } /* H5VL_DATASET_GET_DCPL */

        /* H5Dget_space */
        case H5VL_DATASET_GET_SPACE: {
            hid_t *ret_id = &args->args.get_space.space_id;

            if ((*ret_id = H5Scopy(dset->u.dataset.space_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't get dataspace of dataset");

            break;
        } /* H5VL_DATASET_GET_SPACE */

        /* H5Dget_space_status */
        case H5VL_DATASET_GET_SPACE_STATUS:
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_UNSUPPORTED, FAIL, "H5Dget_space_status is unsupported");
            break;

        /* H5Dget_storage_size */
        case H5VL_DATASET_GET_STORAGE_SIZE:

            /* Make GET request to dataset with 'verbose' parameter for HSDS. */
            snprintf(request_url, URL_MAX_LENGTH, "%s%s%s%s", base_URL, "/datasets/", dset->URI,
                     "?verbose=1");

            /* Setup the host header */
            host_header_len = strlen(dset->domain->u.file.filepath_name) + strlen(host_string) + 1;
            if (NULL == (host_header = (char *)RV_malloc(host_header_len)))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL,
                                "can't allocate space for request Host header");

            strcpy(host_header, host_string);

            curl_headers =
                curl_slist_append(curl_headers, strncat(host_header, dset->domain->u.file.filepath_name,
                                                        host_header_len - strlen(host_string) - 1));

            /* Disable use of Expect: 100 Continue HTTP response */
            curl_headers = curl_slist_append(curl_headers, "Expect:");

            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL HTTP headers: %s",
                                curl_err_buf);
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_HTTPGET, 1))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL,
                                "can't set up cURL to make HTTP GET request: %s", curl_err_buf);
            if (CURLE_OK != curl_easy_setopt(curl, CURLOPT_URL, request_url))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set cURL request URL: %s",
                                curl_err_buf);

            CURL_PERFORM(curl, H5E_DATASET, H5E_CANTGET, FAIL);

            if (RV_parse_allocated_size_callback(response_buffer.buffer, NULL,
                                                 args->args.get_storage_size.storage_size) < 0)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_PARSEERROR, FAIL,
                                "can't get allocated size from server response");

            break;

        /* H5Dget_type */
        case H5VL_DATASET_GET_TYPE: {
            hid_t *ret_id = &args->args.get_type.type_id;

            if ((*ret_id = H5Tcopy(dset->u.dataset.dtype_id)) < 0)
                FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTCOPY, FAIL, "can't copy dataset's datatype");

            break;
        } /* H5VL_DATASET_GET_TYPE */

        default:
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL,
                            "can't get this type of information from dataset");
    } /* end switch */

done:
    if (curl_headers) {
        curl_slist_free_all(curl_headers);
        curl_headers = NULL;
    }

    RV_free(host_header);

    PRINT_ERROR_STACK;

    return ret_value;
} /* end RV_dataset_get() */

/*-------------------------------------------------------------------------
 * Function:    RV_dataset_specific
 *
 * Purpose:     Performs a connector-specific operation on an HDF5 dataset,
 *              such as calling the H5Dset_extent routine.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
herr_t
RV_dataset_specific(void *obj, H5VL_dataset_specific_args_t *args, hid_t dxpl_id, void **req)
{
    RV_object_t *dset      = (RV_object_t *)obj;
    herr_t       ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received dataset-specific call with following parameters:\n");
    printf("     - Dataset-specific call type: %s\n", dataset_specific_type_to_string(args->op_type));
    printf("     - Dataset's URI: %s\n", dset->URI);
    printf("     - Dataset's object type: %s\n", object_type_to_string(dset->obj_type));
    printf("     - Dataset's domain path: %s\n\n", dset->domain->u.file.filepath_name);
#endif

    if (H5I_DATASET != dset->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a dataset");

    switch (args->op_type) {
        /* H5Dset_extent */
        case H5VL_DATASET_SET_EXTENT:
            /* Check for write access */
            if (!(dset->domain->u.file.intent & H5F_ACC_RDWR))
                FUNC_GOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "no write intent on file");

            FUNC_GOTO_ERROR(H5E_DATASET, H5E_UNSUPPORTED, FAIL, "H5Dset_extent is unsupported");
            break;

        /* H5Dflush */
        case H5VL_DATASET_FLUSH:
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_UNSUPPORTED, FAIL, "H5Dflush is unsupported");
            break;

        /* H5Drefresh */
        case H5VL_DATASET_REFRESH:
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_UNSUPPORTED, FAIL, "H5Drefresh is unsupported");
            break;

        default:
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "unknown dataset operation");
    } /* end switch */

done:
    PRINT_ERROR_STACK;

    return ret_value;
} /* end RV_dataset_specific() */

/*-------------------------------------------------------------------------
 * Function:    RV_dataset_close
 *
 * Purpose:     Closes an HDF5 dataset by freeing the memory allocated for
 *              its internal memory struct object. There is no interaction
 *              with the server, whose state is unchanged.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
herr_t
RV_dataset_close(void *dset, hid_t dxpl_id, void **req)
{
    RV_object_t *_dset     = (RV_object_t *)dset;
    herr_t       ret_value = SUCCEED;

    if (!_dset)
        FUNC_GOTO_DONE(SUCCEED);

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Received dataset close call with following parameters:\n");
    printf("     - Dataset's URI: %s\n", _dset->URI);
    printf("     - Dataset's object type: %s\n", object_type_to_string(_dset->obj_type));
    if (_dset->domain && _dset->domain->u.file.filepath_name)
        printf("     - Dataset's domain path: %s\n", _dset->domain->u.file.filepath_name);
    printf("\n");
#endif

    if (H5I_DATASET != _dset->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a dataset");

    if (_dset->u.dataset.dtype_id >= 0 && H5Tclose(_dset->u.dataset.dtype_id) < 0)
        FUNC_DONE_ERROR(H5E_DATATYPE, H5E_CANTCLOSEOBJ, FAIL, "can't close dataset's datatype");

    if (_dset->u.dataset.space_id >= 0 && H5Sclose(_dset->u.dataset.space_id) < 0)
        FUNC_DONE_ERROR(H5E_DATASPACE, H5E_CANTCLOSEOBJ, FAIL, "can't close dataset's dataspace");

    if (_dset->u.dataset.dapl_id >= 0) {
        if (_dset->u.dataset.dapl_id != H5P_DATASET_ACCESS_DEFAULT && H5Pclose(_dset->u.dataset.dapl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close DAPL");
    } /* end if */
    if (_dset->u.dataset.dcpl_id >= 0) {
        if (_dset->u.dataset.dcpl_id != H5P_DATASET_CREATE_DEFAULT && H5Pclose(_dset->u.dataset.dcpl_id) < 0)
            FUNC_DONE_ERROR(H5E_PLIST, H5E_CANTCLOSEOBJ, FAIL, "can't close DCPL");
    } /* end if */

    if (RV_file_close(_dset->domain, H5P_DEFAULT, NULL)) {
        FUNC_DONE_ERROR(H5E_FILE, H5E_CANTCLOSEFILE, FAIL, "can't close file");
    }

    RV_free(_dset->handle_path);
    RV_free(_dset);
    _dset = NULL;

done:
    PRINT_ERROR_STACK;

    return ret_value;
} /* end RV_dataset_close() */

/*-------------------------------------------------------------------------
 * Function:    RV_parse_dataset_creation_properties_callback
 *
 * Purpose:     A callback for RV_parse_response which will search
 *              an HTTP response for the creation properties of a dataset
 *              and set those properties on a DCPL given as input. This
 *              callback is used to help H5Dopen() correctly setup a DCPL
 *              for a dataset that has been "opened" from the server. When
 *              this happens, a default DCPL is created for the dataset,
 *              but does not immediately have any properties set on it.
 *
 *              Without this callback, if a client were to call H5Dopen(),
 *              then call H5Pget_chunk() (or similar) on the Dataset's
 *              contained DCPL, it would result in an error because the
 *              library does not have the chunking information associated
 *              with the DCPL yet. Therefore, this VOL connector has to
 *              handle this case by retrieving all of the creation
 *              properties of a dataset from the server and manually set
 *              each one of the relevant creation properties on the DCPL.
 *
 *              Note that this is unnecessary when H5Pget_chunk() or
 *              similar is called directly after calling H5Dcreate()
 *              without closing the dataset. This is because the user's
 *              supplied DCPL (which would already have the properties set
 *              on it) is copied into the Dataset's in-memory struct
 *              representation for future use.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              November, 2017
 */
static herr_t
RV_parse_dataset_creation_properties_callback(char *HTTP_response, void *callback_data_in,
                                              void *callback_data_out)
{
    yajl_val parse_tree = NULL, creation_properties_obj, key_obj;
    hid_t   *DCPL       = (hid_t *)callback_data_out;
    herr_t   ret_value  = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Retrieving dataset's creation properties from server's HTTP response\n\n");
#endif

    if (!HTTP_response)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "HTTP response buffer was NULL");
    if (!DCPL)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "DCPL pointer was NULL");

    if (NULL == (parse_tree = yajl_tree_parse(HTTP_response, NULL, 0)))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_PARSEERROR, FAIL, "parsing JSON failed");

    /* Retrieve the creationProperties object */
    if (NULL ==
        (creation_properties_obj = yajl_tree_get(parse_tree, creation_properties_keys, yajl_t_object)))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "retrieval of creationProperties object failed");

    /********************************************************************************************
     *                                                                                          *
     *                              Space Allocation Time Section                               *
     *                                                                                          *
     * Determine the status of the space allocation time (default, early, late, incremental)    *
     * and set this on the DCPL                                                                 *
     *                                                                                          *
     ********************************************************************************************/
    if ((key_obj = yajl_tree_get(creation_properties_obj, alloc_time_keys, yajl_t_string))) {
        H5D_alloc_time_t alloc_time;
        char            *alloc_time_string;

        if (NULL == (alloc_time_string = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "space allocation time string was NULL");

        if (!strcmp(alloc_time_string, "H5D_ALLOC_TIME_EARLY")) {
            alloc_time = H5D_ALLOC_TIME_EARLY;

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Setting AllocTime H5D_ALLOC_TIME_EARLY on DCPL\n");
#endif
        } /* end if */
        else if (!strcmp(alloc_time_string, "H5D_ALLOC_TIME_INCR")) {
            alloc_time = H5D_ALLOC_TIME_INCR;

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Setting AllocTime H5D_ALLOC_TIME_INCR on DCPL\n");
#endif
        } /* end else if */
        else if (!strcmp(alloc_time_string, "H5D_ALLOC_TIME_LATE")) {
            alloc_time = H5D_ALLOC_TIME_LATE;

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Setting AllocTime H5D_ALLOC_TIME_LATE on DCPL\n");
#endif
        } /* end else if */
        else {
            alloc_time = H5D_ALLOC_TIME_DEFAULT;

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Setting AllocTime H5D_ALLOC_TIME_DEFAULT on DCPL\n");
#endif
        } /* end else */

        if (H5Pset_alloc_time(*DCPL, alloc_time) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "can't set space allocation time property on DCPL");
    } /* end if */

    /********************************************************************************************
     *                                                                                          *
     *                             Attribute Creation Order Section                             *
     *                                                                                          *
     * Determine the status of attribute creation order (tracked, tracked + indexed or neither) *
     * and set this on the DCPL                                                                 *
     *                                                                                          *
     ********************************************************************************************/
    if ((key_obj = yajl_tree_get(creation_properties_obj, creation_order_keys, yajl_t_string))) {
        unsigned crt_order_flags = 0x0;
        char    *crt_order_string;

        if (NULL == (crt_order_string = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "attribute creation order string was NULL");

        if (!strcmp(crt_order_string, "H5P_CRT_ORDER_INDEXED")) {
            crt_order_flags = H5P_CRT_ORDER_INDEXED | H5P_CRT_ORDER_TRACKED;

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Setting attribute creation order H5P_CRT_ORDER_INDEXED + H5P_CRT_ORDER_TRACKED on "
                   "DCPL\n");
#endif
        } /* end if */
        else {
            crt_order_flags = H5P_CRT_ORDER_TRACKED;

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Setting attribute creation order H5P_CRT_ORDER_TRACKED on DCPL\n");
#endif
        } /* end else */

        if (H5Pset_attr_creation_order(*DCPL, crt_order_flags) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL,
                            "can't set attribute creation order property on DCPL");
    } /* end if */

    /****************************************************************************
     *                                                                          *
     *                 Attribute Phase Change Threshold Section                 *
     *                                                                          *
     * Determine the phase change values for attribute storage and set these on *
     * the DCPL                                                                 *
     *                                                                          *
     ****************************************************************************/
    if ((key_obj = yajl_tree_get(creation_properties_obj, attribute_phase_change_keys, yajl_t_object))) {
        unsigned minDense   = DATASET_CREATE_MIN_DENSE_ATTRIBUTES_DEFAULT;
        unsigned maxCompact = DATASET_CREATE_MAX_COMPACT_ATTRIBUTES_DEFAULT;
        yajl_val sub_obj;

        if (NULL == (sub_obj = yajl_tree_get(key_obj, max_compact_keys, yajl_t_number)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL,
                            "retrieval of maxCompact attribute phase change value failed");

        if (!YAJL_IS_INTEGER(sub_obj))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL,
                            "return maxCompact attribute phase change value is not an integer");

        if (YAJL_GET_INTEGER(sub_obj) >= 0)
            maxCompact = (unsigned)YAJL_GET_INTEGER(sub_obj);

        if (NULL == (sub_obj = yajl_tree_get(key_obj, min_dense_keys, yajl_t_number)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL,
                            "retrieval of minDense attribute phase change value failed");

        if (!YAJL_IS_INTEGER(sub_obj))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL,
                            "returned minDense attribute phase change value is not an integer");

        if (YAJL_GET_INTEGER(sub_obj) >= 0)
            minDense = (unsigned)YAJL_GET_INTEGER(sub_obj);

        if (minDense != DATASET_CREATE_MIN_DENSE_ATTRIBUTES_DEFAULT ||
            maxCompact != DATASET_CREATE_MAX_COMPACT_ATTRIBUTES_DEFAULT) {
#ifdef RV_CONNECTOR_DEBUG
            printf("-> Setting attribute phase change values: [ minDense: %u, maxCompact: %u ] on DCPL\n",
                   minDense, maxCompact);
#endif

            if (H5Pset_attr_phase_change(*DCPL, maxCompact, minDense) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL,
                                "can't set attribute phase change values property on DCPL");
        } /* end if */
    }     /* end if */

    /**********************************************************
     *                                                        *
     *                    Fill Time Section                   *
     *                                                        *
     * Determine the fill time value and set this on the DCPL *
     *                                                        *
     **********************************************************/
    if ((key_obj = yajl_tree_get(creation_properties_obj, fill_time_keys, yajl_t_string))) {
        H5D_fill_time_t fill_time;
        char           *fill_time_str;

        if (NULL == (fill_time_str = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "fill time string was NULL");

        if (!strcmp(fill_time_str, "H5D_FILL_TIME_ALLOC")) {
            fill_time = H5D_FILL_TIME_ALLOC;

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Setting fill time H5D_FILL_TIME_ALLOC on DCPL\n");
#endif
        } /* end else if */
        else if (!strcmp(fill_time_str, "H5D_FILL_TIME_NEVER")) {
            fill_time = H5D_FILL_TIME_NEVER;

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Setting fill time H5D_FILL_TIME_NEVER on DCPL\n");
#endif
        } /* end else if */
        else {
            fill_time = H5D_FILL_TIME_IFSET;

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Setting fill time H5D_FILL_TIME_IFSET on DCPL\n");
#endif
        } /* end else */

        if (H5Pset_fill_time(*DCPL, fill_time) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "can't set fill time property on DCPL");
    } /* end if */

    /******************************************************************************
     *                                                                            *
     *                             Fill Value Section                             *
     *                                                                            *
     * Determine the fill value status for the Dataset and set this on the DCPL   *
     *                                                                            *
     ******************************************************************************/
    if ((key_obj = yajl_tree_get(creation_properties_obj, fill_value_keys, yajl_t_any))) {
        /* TODO: Until fill value support is implemented, just push an error to the stack but continue ahead
         */
        FUNC_DONE_ERROR(H5E_DATASET, H5E_UNSUPPORTED, SUCCEED,
                        "warning: dataset fill values are unsupported");
    } /* end if */

    /***************************************************************
     *                                                             *
     *                       Filters Section                       *
     *                                                             *
     * Determine the filters that have been added to the Dataset   *
     * and set this on the DCPL                                    *
     *                                                             *
     ***************************************************************/
    if ((key_obj = yajl_tree_get(creation_properties_obj, filters_keys, yajl_t_array))) {
        size_t i;

        /* Grab the relevant information from each filter and set them on the DCPL in turn. */
        for (i = 0; i < YAJL_GET_ARRAY(key_obj)->len; i++) {
            yajl_val  filter_obj = YAJL_GET_ARRAY(key_obj)->values[i];
            yajl_val  filter_field;
            char     *filter_class;
            long long filter_ID;

            if (NULL == (filter_field = yajl_tree_get(filter_obj, filter_class_keys, yajl_t_string)))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "retrieval of filter class failed");

            if (NULL == (filter_class = YAJL_GET_STRING(filter_field)))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "filter class string was NULL");

            if (NULL == (filter_field = yajl_tree_get(filter_obj, filter_ID_keys, yajl_t_number)))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "retrieval of filter ID failed");

            if (!YAJL_IS_INTEGER(filter_field))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "returned filter ID is not an integer");

            filter_ID = YAJL_GET_INTEGER(filter_field);

            switch (filter_ID) {
                case H5Z_FILTER_DEFLATE: {
                    const char *deflate_level_keys[] = {"level", (const char *)0};
                    long long   deflate_level;

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Discovered filter class H5Z_FILTER_DEFLATE in JSON response; setting deflate "
                           "filter on DCPL\n");
#endif

                    /* Quick sanity check; push an error to the stack on failure, but don't fail this function
                     */
                    if (strcmp(filter_class, "H5Z_FILTER_DEFLATE"))
                        FUNC_DONE_ERROR(H5E_DATASET, H5E_BADVALUE, SUCCEED,
                                        "warning: filter class '%s' does not match H5Z_FILTER_DEFLATE; DCPL "
                                        "should not be trusted",
                                        filter_class);

                    /* Grab the level of compression */
                    if (NULL == (filter_field = yajl_tree_get(filter_obj, deflate_level_keys, yajl_t_number)))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL,
                                        "retrieval of deflate filter compression level value failed");

                    if (!YAJL_IS_INTEGER(filter_field))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL,
                                        "returned deflate filter compression level is not an integer");

                    deflate_level = YAJL_GET_INTEGER(filter_field);
                    if (deflate_level < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL,
                                        "deflate filter compression level invalid (level < 0)");

                    if (H5Pset_deflate(*DCPL, (unsigned)deflate_level) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set deflate filter on DCPL");

                    break;
                }

                case H5Z_FILTER_SHUFFLE: {
#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Discovered filter class H5Z_FILTER_SHUFFLE in JSON response; setting shuffle "
                           "filter on DCPL\n");
#endif

                    /* Quick sanity check; push an error to the stack on failure, but don't fail this function
                     */
                    if (strcmp(filter_class, "H5Z_FILTER_SHUFFLE"))
                        FUNC_DONE_ERROR(H5E_DATASET, H5E_BADVALUE, SUCCEED,
                                        "warning: filter class '%s' does not match H5Z_FILTER_SHUFFLE; DCPL "
                                        "should not be trusted",
                                        filter_class);

                    if (H5Pset_shuffle(*DCPL) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set shuffle filter on DCPL");

                    break;
                }

                case H5Z_FILTER_FLETCHER32: {
#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Discovered filter class H5Z_FILTER_FLETCHER32 in JSON response; setting "
                           "fletcher32 filter on DCPL\n");
#endif

                    /* Quick sanity check; push an error to the stack on failure, but don't fail this function
                     */
                    if (strcmp(filter_class, "H5Z_FILTER_FLETCHER32"))
                        FUNC_DONE_ERROR(H5E_DATASET, H5E_BADVALUE, SUCCEED,
                                        "warning: filter class '%s' does not match H5Z_FILTER_FLETCHER32; "
                                        "DCPL should not be trusted",
                                        filter_class);

                    if (H5Pset_fletcher32(*DCPL) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL,
                                        "can't set fletcher32 filter on DCPL");

                    break;
                }

                case H5Z_FILTER_SZIP: {
                    const char *szip_option_mask_keys[] = {"coding", (const char *)0};
                    const char *szip_ppb_keys[]         = {"pixelsPerBlock", (const char *)0};
                    char       *szip_option_mask;
                    long long   szip_ppb;

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Discovered filter class H5Z_FILTER_SZIP in JSON response; setting SZIP filter "
                           "on DCPL\n");
#endif

                    /* Quick sanity check; push an error to the stack on failure, but don't fail this function
                     */
                    if (strcmp(filter_class, "H5Z_FILTER_SZIP"))
                        FUNC_DONE_ERROR(H5E_DATASET, H5E_BADVALUE, SUCCEED,
                                        "warning: filter class '%s' does not match H5Z_FILTER_SZIP; DCPL "
                                        "should not be trusted",
                                        filter_class);

                    /* Retrieve the value of the SZIP option mask */
                    if (NULL ==
                        (filter_field = yajl_tree_get(filter_obj, szip_option_mask_keys, yajl_t_string)))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL,
                                        "retrieval of SZIP option mask failed");

                    if (NULL == (szip_option_mask = YAJL_GET_STRING(filter_field)))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "SZIP option mask string was NULL");

                    if (strcmp(szip_option_mask, "H5_SZIP_EC_OPTION_MASK") &&
                        strcmp(szip_option_mask, "H5_SZIP_NN_OPTION_MASK")) {
                        /* Push an error to the stack, but don't fail this function */
                        FUNC_DONE_ERROR(H5E_DATASET, H5E_BADVALUE, SUCCEED,
                                        "invalid SZIP option mask value '%s'", szip_option_mask);
                        continue;
                    }

                    /* Retrieve the value of the SZIP "pixels per block" option */
                    if (NULL == (filter_field = yajl_tree_get(filter_obj, szip_ppb_keys, yajl_t_number)))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL,
                                        "retrieval of SZIP pixels per block option failed");

                    if (!YAJL_IS_INTEGER(filter_field))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL,
                                        "returned SZIP pixels per block option value is not an integer");

                    szip_ppb = YAJL_GET_INTEGER(filter_field);
                    if (szip_ppb < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL,
                                        "invalid SZIP pixels per block option value (PPB < 0)");

                    if (H5Pset_szip(*DCPL,
                                    (!strcmp(szip_option_mask, "H5_SZIP_EC_OPTION_MASK")
                                         ? H5_SZIP_EC_OPTION_MASK
                                         : H5_SZIP_NN_OPTION_MASK),
                                    (unsigned)szip_ppb) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set SZIP filter on DCPL");

                    break;
                }

                case H5Z_FILTER_NBIT: {
#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Discovered filter class H5Z_FILTER_NBIT in JSON response; setting N-Bit "
                           "filter on DCPL\n");
#endif

                    /* Quick sanity check; push an error to the stack on failure, but don't fail this function
                     */
                    if (strcmp(filter_class, "H5Z_FILTER_NBIT"))
                        FUNC_DONE_ERROR(H5E_DATASET, H5E_BADVALUE, SUCCEED,
                                        "warning: filter class '%s' does not match H5Z_FILTER_NBIT; DCPL "
                                        "should not be trusted",
                                        filter_class);

                    if (H5Pset_nbit(*DCPL) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set N-Bit filter on DCPL");

                    break;
                }

                case H5Z_FILTER_SCALEOFFSET: {
                    H5Z_SO_scale_type_t scale_type;
                    const char         *scale_type_keys[]   = {"scaleType", (const char *)0};
                    const char         *scale_offset_keys[] = {"scaleOffset", (const char *)0};
                    long long           scale_offset;
                    char               *scale_type_str;

#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Discovered filter class H5Z_FILTER_SCALEOFFSET in JSON response; setting "
                           "scale-offset filter on DCPL\n");
#endif

                    /* Quick sanity check; push an error to the stack on failure, but don't fail this function
                     */
                    if (strcmp(filter_class, "H5Z_FILTER_SCALEOFFSET"))
                        FUNC_DONE_ERROR(H5E_DATASET, H5E_BADVALUE, SUCCEED,
                                        "warning: filter class '%s' does not match H5Z_FILTER_SCALEOFFSET; "
                                        "DCPL should not be trusted",
                                        filter_class);

                    /* Retrieve the scale type */
                    if (NULL == (filter_field = yajl_tree_get(filter_obj, scale_type_keys, yajl_t_string)))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "retrieval of scale type failed");

                    if (NULL == (scale_type_str = YAJL_GET_STRING(filter_field)))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "scale type string was NULL");

                    if (!strcmp(scale_type_str, "H5Z_SO_FLOAT_DSCALE"))
                        scale_type = H5Z_SO_FLOAT_DSCALE;
                    else if (!strcmp(scale_type_str, "H5Z_SO_FLOAT_ESCALE"))
                        scale_type = H5Z_SO_FLOAT_ESCALE;
                    else if (!strcmp(scale_type_str, "H5Z_SO_INT"))
                        scale_type = H5Z_SO_INT;
                    else {
                        FUNC_DONE_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "invalid scale type '%s'",
                                        scale_type_str);
                        continue;
                    }

                    /* Retrieve the scale offset value */
                    if (NULL == (filter_field = yajl_tree_get(filter_obj, scale_offset_keys, yajl_t_number)))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL,
                                        "retrieval of scale offset value failed");

                    if (!YAJL_IS_INTEGER(filter_field))
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL,
                                        "returned scale offset value is not an integer");

                    scale_offset = YAJL_GET_INTEGER(filter_field);

                    if (H5Pset_scaleoffset(*DCPL, scale_type, (int)scale_offset) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL,
                                        "can't set scale-offset filter on DCPL");

                    break;
                }

                case LZF_FILTER_ID: {
#ifdef RV_CONNECTOR_DEBUG
                    printf("-> Discovered filter class H5Z_FILTER_LZF in JSON response; setting LZF filter "
                           "on DCPL\n");
#endif

                    /* Quick sanity check; push an error to the stack on failure, but don't fail this function
                     */
                    if (strcmp(filter_class, "H5Z_FILTER_LZF"))
                        FUNC_DONE_ERROR(H5E_DATASET, H5E_BADVALUE, SUCCEED,
                                        "warning: filter class '%s' does not match H5Z_FILTER_LZF; DCPL "
                                        "should not be trusted",
                                        filter_class);

                    /* Note that it may be more appropriate to set the LZF filter as mandatory here, but for
                     * now optional is used */
                    if (H5Pset_filter(*DCPL, LZF_FILTER_ID, H5Z_FLAG_OPTIONAL, 0, NULL) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set LZF filter on DCPL");

                    break;
                }

                    /* TODO: support for other/user-defined filters */

                default:
                    /* Push error to stack; but don't fail this function */
                    FUNC_DONE_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL,
                                    "warning: invalid filter with class '%s' and ID '%lld' on DCPL",
                                    filter_class, filter_ID);
            }

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Filter %zu:\n", i);
            printf("->   Class: %s\n", filter_class);
            printf("->   ID: %lld\n", filter_ID);
#endif
        } /* end for */
    }     /* end if */

    /****************************************************************************
     *                                                                          *
     *                                Layout Section                            *
     *                                                                          *
     * Determine the layout information of the Dataset and set this on the DCPL *
     *                                                                          *
     ****************************************************************************/
    if ((key_obj = yajl_tree_get(creation_properties_obj, layout_keys, yajl_t_object))) {
        yajl_val sub_obj;
        size_t   i;
        char    *layout_class;

        if (NULL == (sub_obj = yajl_tree_get(key_obj, layout_class_keys, yajl_t_string)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "retrieval of layout class property failed");

        if (NULL == (layout_class = YAJL_GET_STRING(sub_obj)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "layout class string was NULL");

        if (!strcmp(layout_class, "H5D_CHUNKED")) {
            yajl_val chunk_dims_obj;
            hsize_t  chunk_dims[DATASPACE_MAX_RANK];

            if (NULL == (chunk_dims_obj = yajl_tree_get(key_obj, chunk_dims_keys, yajl_t_array)))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "retrieval of chunk dimensionality failed");

            for (i = 0; i < YAJL_GET_ARRAY(chunk_dims_obj)->len; i++) {
                long long val;

                if (!YAJL_IS_INTEGER(YAJL_GET_ARRAY(chunk_dims_obj)->values[i]))
                    FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL,
                                    "one of the chunk dimension sizes was not an integer");

                if ((val = YAJL_GET_INTEGER(YAJL_GET_ARRAY(chunk_dims_obj)->values[i])) < 0)
                    FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL,
                                    "one of the chunk dimension sizes was negative");

                chunk_dims[i] = (hsize_t)val;
            } /* end for */

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Setting chunked layout on DCPL\n");
            printf("-> Chunk dims: [ ");
            for (i = 0; i < YAJL_GET_ARRAY(chunk_dims_obj)->len; i++) {
                if (i > 0)
                    printf(", ");
                printf("%llu", chunk_dims[i]);
            }
            printf(" ]\n");
#endif

            if (H5Pset_chunk(*DCPL, (int)YAJL_GET_ARRAY(chunk_dims_obj)->len, chunk_dims) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "can't set chunked storage layout on DCPL");
        } /* end if */
        else if (!strcmp(layout_class, "H5D_CONTIGUOUS")) {
            /* Check to see if there is any external storage information */
            if (yajl_tree_get(key_obj, external_storage_keys, yajl_t_array)) {
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_UNSUPPORTED, FAIL,
                                "dataset external file storage is unsupported");
            } /* end if */

#ifdef RV_CONNECTOR_DEBUG
            printf("-> Setting contiguous layout on DCPL\n");
#endif

            if (H5Pset_layout(*DCPL, H5D_CONTIGUOUS) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "can't set contiguous storage layout on DCPL");
        } /* end if */
        else if (!strcmp(layout_class, "H5D_COMPACT")) {
#ifdef RV_CONNECTOR_DEBUG
            printf("-> Setting compact layout on DCPL\n");
#endif

            if (H5Pset_layout(*DCPL, H5D_COMPACT) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "can't set compact storage layout on DCPL");
        } /* end else */
    }     /* end if */

    /*************************************************************************
     *                                                                       *
     *                       Object Time Tracking Section                    *
     *                                                                       *
     * Determine the status of object time tracking and set this on the DCPL *
     *                                                                       *
     *************************************************************************/
    if ((key_obj = yajl_tree_get(creation_properties_obj, track_times_keys, yajl_t_string))) {
        hbool_t track_times = false;
        char   *track_times_str;

        if (NULL == (track_times_str = YAJL_GET_STRING(key_obj)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "track times string was NULL");

        track_times = !strcmp(track_times_str, "true");

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Setting track times: %s on DCPL\n", track_times ? "true" : "false");
#endif

        if (H5Pset_obj_track_times(*DCPL, track_times) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "can't set track object times property on DCPL");
    } /* end if */

done:
#ifdef RV_CONNECTOR_DEBUG
    printf("\n");
#endif

    if (parse_tree)
        yajl_tree_free(parse_tree);

    return ret_value;
} /* end RV_parse_dataset_creation_properties_callback() */

/*-------------------------------------------------------------------------
 * Function:    RV_convert_dataset_creation_properties_to_JSON
 *
 * Purpose:     Given a DCPL during a dataset create operation, converts
 *              all of the Dataset Creation Properties, such as the
 *              filters used, Chunk Dimensionality, fill time/value, etc.,
 *              into JSON to be used during the Dataset create request.
 *              The string buffer handed back by this function must be
 *              freed by the caller, else memory will be leaked.
 *
 * Return:      Non-negative on success, negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
RV_convert_dataset_creation_properties_to_JSON(hid_t dcpl, char **creation_properties_body,
                                               size_t *creation_properties_body_len)
{
    const char *const leading_string = "\"creationProperties\": {";
    H5D_alloc_time_t  alloc_time;
    ptrdiff_t         buf_ptrdiff;
    size_t            leading_string_len = strlen(leading_string);
    size_t            bytes_to_print     = 0;
    size_t            out_string_len;
    char             *chunk_dims_string = NULL;
    char             *out_string        = NULL;
    char *out_string_curr_pos; /* The "current position" pointer used to print to the appropriate place
                                  in the buffer and not overwrite important leading data */
    int    bytes_printed = 0;
    herr_t ret_value     = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Converting dataset creation properties from DCPL to JSON\n\n");
#endif

    if (!creation_properties_body)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL,
                        "invalid NULL pointer for dataset creation properties string buffer");

    out_string_len = DATASET_CREATION_PROPERTIES_BODY_DEFAULT_SIZE;
    if (NULL == (out_string = (char *)RV_malloc(out_string_len)))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL,
                        "can't allocate space for dataset creation properties string buffer");

    /* Keep track of the current position in the resulting string so everything
     * gets added smoothly
     */
    out_string_curr_pos = out_string;

    /* Make sure the buffer is at least large enough to hold the leading string */
    CHECKED_REALLOC(out_string, out_string_len, leading_string_len + 1, out_string_curr_pos, H5E_DATASET,
                    FAIL);

    /* Add the leading string */
    strncpy(out_string, leading_string, out_string_len);
    out_string_curr_pos += leading_string_len;

    /* Note: At least one creation property needs to be guaranteed to be printed out in the resulting
     * output string so that each additional property can be safely appended to the string with a leading
     * comma to separate it from the other properties. Without the guarantee of at least one printed out
     * property, the result can be a missing or hanging comma in the string, depending on the combinations
     * of set/unset properties, which may result in server request errors. In this case, simply the Dataset
     * space allocation time property is chosen to always be printed to the resulting string.
     */
    if (H5Pget_alloc_time(dcpl, &alloc_time) < 0)
        FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't retrieve alloc time property");

    /* Check whether the buffer needs to be grown */
    bytes_to_print = strlen("\"allocTime\": \"H5D_ALLOC_TIME_DEFAULT\"") + 1;

    buf_ptrdiff = out_string_curr_pos - out_string;
    if (buf_ptrdiff < 0)
        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                        "unsafe cast: dataset creation properties buffer pointer difference was negative - "
                        "this should not happen!");

    CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print, out_string_curr_pos,
                    H5E_DATASET, FAIL);

    switch (alloc_time) {
        case H5D_ALLOC_TIME_DEFAULT: {
            const char *const default_alloc_time = "\"allocTime\": \"H5D_ALLOC_TIME_DEFAULT\"";
            size_t            default_alloc_len  = strlen(default_alloc_time);

            strncat(out_string_curr_pos, default_alloc_time, default_alloc_len);
            out_string_curr_pos += default_alloc_len;
            break;
        } /* H5D_ALLOC_TIME_DEFAULT */

        case H5D_ALLOC_TIME_EARLY: {
            const char *const early_alloc_time = "\"allocTime\": \"H5D_ALLOC_TIME_EARLY\"";
            size_t            early_alloc_len  = strlen(early_alloc_time);

            strncat(out_string_curr_pos, early_alloc_time, early_alloc_len);
            out_string_curr_pos += early_alloc_len;
            break;
        } /* H5D_ALLOC_TIME_EARLY */

        case H5D_ALLOC_TIME_LATE: {
            const char *const late_alloc_time = "\"allocTime\": \"H5D_ALLOC_TIME_LATE\"";
            size_t            late_alloc_len  = strlen(late_alloc_time);

            strncat(out_string_curr_pos, late_alloc_time, late_alloc_len);
            out_string_curr_pos += late_alloc_len;
            break;
        } /* H5D_ALLOC_TIME_LATE */

        case H5D_ALLOC_TIME_INCR: {
            const char *const incr_alloc_time = "\"allocTime\": \"H5D_ALLOC_TIME_INCR\"";
            size_t            incr_alloc_len  = strlen(incr_alloc_time);

            strncat(out_string_curr_pos, incr_alloc_time, incr_alloc_len);
            out_string_curr_pos += incr_alloc_len;
            break;
        } /* H5D_ALLOC_TIME_INCR */

        case H5D_ALLOC_TIME_ERROR:
        default:
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "invalid dataset space alloc time");
    } /* end switch */

    /********************************************************************************************
     *                                                                                          *
     *                             Attribute Creation Order Section                             *
     *                                                                                          *
     * Determine the status of attribute creation order (tracked, tracked + indexed or neither) *
     * and append its string representation                                                     *
     *                                                                                          *
     ********************************************************************************************/
    {
        unsigned crt_order_flags;

        if (H5Pget_attr_creation_order(dcpl, &crt_order_flags) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't retrieve attribute creation order property");

        if (0 != crt_order_flags) {
            const char *const fmt_string = ", \"attributeCreationOrder\": \"H5P_CRT_ORDER_%s\"";

            /* Check whether the buffer needs to be grown */
            bytes_to_print = strlen(fmt_string) + strlen("INDEXED") + 1;

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                "unsafe cast: dataset creation properties buffer pointer difference was "
                                "negative - this should not happen!");

            CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                            out_string_curr_pos, H5E_DATASET, FAIL);

            if ((bytes_printed = snprintf(
                     out_string_curr_pos, out_string_len - (size_t)buf_ptrdiff, fmt_string,
                     (H5P_CRT_ORDER_INDEXED | H5P_CRT_ORDER_TRACKED) == crt_order_flags ? "INDEXED"
                                                                                        : "TRACKED")) < 0)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error");

            if ((size_t)bytes_printed >= out_string_len - (size_t)buf_ptrdiff)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL,
                                "dataset creation order property string size exceeded allocated buffer size");

            out_string_curr_pos += bytes_printed;
        } /* end if */
    }

    /****************************************************************************
     *                                                                          *
     *                 Attribute Phase Change Threshold Section                 *
     *                                                                          *
     * Determine the phase change values for attribute storage and append their *
     * string representations                                                   *
     *                                                                          *
     ****************************************************************************/
    {
        unsigned max_compact, min_dense;

        if (H5Pget_attr_phase_change(dcpl, &max_compact, &min_dense) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't retrieve attribute phase change property");

        if (DATASET_CREATE_MAX_COMPACT_ATTRIBUTES_DEFAULT != max_compact ||
            DATASET_CREATE_MIN_DENSE_ATTRIBUTES_DEFAULT != min_dense) {
            const char *const fmt_string = ", \"attributePhaseChange\": {"
                                           "\"maxCompact\": %u, "
                                           "\"minDense\": %u"
                                           "}";

            /* Check whether the buffer needs to be grown */
            bytes_to_print = strlen(fmt_string) + (2 * MAX_NUM_LENGTH) + 1;

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                "unsafe cast: dataset creation properties buffer pointer difference was "
                                "negative - this should not happen!");

            CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                            out_string_curr_pos, H5E_DATASET, FAIL);

            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t)buf_ptrdiff,
                                          fmt_string, max_compact, min_dense)) < 0)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error");

            if ((size_t)bytes_printed >= out_string_len - (size_t)buf_ptrdiff)
                FUNC_GOTO_ERROR(
                    H5E_DATASET, H5E_SYSERRSTR, FAIL,
                    "dataset attribute phase change property string size exceeded allocated buffer size");

            out_string_curr_pos += bytes_printed;
        } /* end if */
    }

    /**********************************************************************
     *                                                                    *
     *                         Fill Time Section                          *
     *                                                                    *
     * Determine the fill time value and append its string representation *
     *                                                                    *
     **********************************************************************/
    {
        H5D_fill_time_t fill_time;

        if (H5Pget_fill_time(dcpl, &fill_time) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't retrieve fill time property");

        if (H5D_FILL_TIME_IFSET != fill_time) {
            const char *const fmt_string = ", \"fillTime\": \"H5D_FILL_TIME_%s\"";

            /* Check whether the buffer needs to be grown */
            bytes_to_print = strlen(fmt_string) + strlen("ALLOC") + 1;

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                "unsafe cast: dataset creation properties buffer pointer difference was "
                                "negative - this should not happen!");

            CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                            out_string_curr_pos, H5E_DATASET, FAIL);

            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t)buf_ptrdiff,
                                          fmt_string, H5D_FILL_TIME_ALLOC == fill_time ? "ALLOC" : "NEVER")) <
                0)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error");

            if ((size_t)bytes_printed >= out_string_len - (size_t)buf_ptrdiff)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL,
                                "dataset fill time property string size exceeded allocated buffer size");

            out_string_curr_pos += bytes_printed;
        } /* end if */
    }

    /******************************************************************
     *                                                                *
     *                      Fill Value Section                        *
     *                                                                *
     * Determine the fill value status for the Dataset and append its *
     * string representation if it is specified                       *
     *                                                                *
     ******************************************************************/
    {
        H5D_fill_value_t fill_status;

        if (H5Pfill_value_defined(dcpl, &fill_status) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't retrieve the \"fill value defined\" status");

        if (H5D_FILL_VALUE_DEFAULT != fill_status) {
            if (H5D_FILL_VALUE_UNDEFINED == fill_status) {
                const char *const null_value     = ", \"fillValue\": null";
                size_t            null_value_len = strlen(null_value);

                /* Check whether the buffer needs to be grown */
                bytes_to_print = null_value_len + 1;

                buf_ptrdiff = out_string_curr_pos - out_string;
                if (buf_ptrdiff < 0)
                    FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                    "unsafe cast: dataset creation properties buffer pointer difference was "
                                    "negative - this should not happen!");

                CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                                out_string_curr_pos, H5E_DATASET, FAIL);

                strncat(out_string_curr_pos, null_value, null_value_len);
                out_string_curr_pos += null_value_len;
            } /* end if */
            else {
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_UNSUPPORTED, FAIL, "dataset fill values are unsupported");
            } /* end else */
        }     /* end if */
    }

    /***************************************************************
     *                                                             *
     *                       Filters Section                       *
     *                                                             *
     * Determine the filters to be added to the Dataset and append *
     * their string representations                                *
     *                                                             *
     ***************************************************************/
    {
        int nfilters;

        if ((nfilters = H5Pget_nfilters(dcpl))) {
            const char *const filters_string = ", \"filters\": [ ";
            H5Z_filter_t      filter_id;
            unsigned          filter_config;
            unsigned          flags;
            unsigned          cd_values[FILTER_MAX_CD_VALUES];
            size_t            filters_string_len = strlen(filters_string);
            size_t            cd_nelmts          = FILTER_MAX_CD_VALUES;
            size_t            filter_namelen     = FILTER_NAME_MAX_LENGTH;
            size_t            i;
            char              filter_name[FILTER_NAME_MAX_LENGTH];

            /* Check whether the buffer needs to be grown */
            bytes_to_print = filters_string_len + 1;

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                "unsafe cast: dataset creation properties buffer pointer difference was "
                                "negative - this should not happen!");

            CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                            out_string_curr_pos, H5E_DATASET, FAIL);

            strncat(out_string_curr_pos, filters_string, filters_string_len);
            out_string_curr_pos += filters_string_len;

            for (i = 0; i < (size_t)nfilters; i++) {
                /* Reset the value of cd_nelmts to make sure all of the filter's CD values are retrieved
                 * correctly */
                cd_nelmts = FILTER_MAX_CD_VALUES;

                switch ((filter_id = H5Pget_filter2(dcpl, (unsigned)i, &flags, &cd_nelmts, cd_values,
                                                    filter_namelen, filter_name, &filter_config))) {
                    case H5Z_FILTER_DEFLATE: {
                        const char *const fmt_string = "{"
                                                       "\"class\": \"H5Z_FILTER_DEFLATE\","
                                                       "\"id\": %d,"
                                                       "\"level\": %u"
                                                       "}";

                        /* Check whether the buffer needs to be grown */
                        bytes_to_print = strlen(fmt_string) + (2 * MAX_NUM_LENGTH) + 1;

                        buf_ptrdiff = out_string_curr_pos - out_string;
                        if (buf_ptrdiff < 0)
                            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                            "unsafe cast: dataset creation properties buffer pointer "
                                            "difference was negative - this should not happen!");

                        CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                                        out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed =
                                 snprintf(out_string_curr_pos, out_string_len - (size_t)buf_ptrdiff,
                                          fmt_string, H5Z_FILTER_DEFLATE, cd_values[0])) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error");

                        if ((size_t)bytes_printed >= out_string_len - (size_t)buf_ptrdiff)
                            FUNC_GOTO_ERROR(
                                H5E_DATASET, H5E_SYSERRSTR, FAIL,
                                "dataset deflate filter property string size exceeded allocated buffer size");

                        out_string_curr_pos += bytes_printed;
                        break;
                    } /* H5Z_FILTER_DEFLATE */

                    case H5Z_FILTER_SHUFFLE: {
                        const char *const fmt_string = "{"
                                                       "\"class\": \"H5Z_FILTER_SHUFFLE\","
                                                       "\"id\": %d"
                                                       "}";

                        /* Check whether the buffer needs to be grown */
                        bytes_to_print = strlen(fmt_string) + MAX_NUM_LENGTH + 1;

                        buf_ptrdiff = out_string_curr_pos - out_string;
                        if (buf_ptrdiff < 0)
                            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                            "unsafe cast: dataset creation properties buffer pointer "
                                            "difference was negative - this should not happen!");

                        CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                                        out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed =
                                 snprintf(out_string_curr_pos, out_string_len - (size_t)buf_ptrdiff,
                                          fmt_string, H5Z_FILTER_SHUFFLE)) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error");

                        if ((size_t)bytes_printed >= out_string_len - (size_t)buf_ptrdiff)
                            FUNC_GOTO_ERROR(
                                H5E_DATASET, H5E_SYSERRSTR, FAIL,
                                "dataset shuffle filter property string size exceeded allocated buffer size");

                        out_string_curr_pos += bytes_printed;
                        break;
                    } /* H5Z_FILTER_SHUFFLE */

                    case H5Z_FILTER_FLETCHER32: {
                        const char *const fmt_string = "{"
                                                       "\"class\": \"H5Z_FILTER_FLETCHER32\","
                                                       "\"id\": %d"
                                                       "}";

                        /* Check whether the buffer needs to be grown */
                        bytes_to_print = strlen(fmt_string) + MAX_NUM_LENGTH + 1;

                        buf_ptrdiff = out_string_curr_pos - out_string;
                        if (buf_ptrdiff < 0)
                            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                            "unsafe cast: dataset creation properties buffer pointer "
                                            "difference was negative - this should not happen!");

                        CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                                        out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed =
                                 snprintf(out_string_curr_pos, out_string_len - (size_t)buf_ptrdiff,
                                          fmt_string, H5Z_FILTER_FLETCHER32)) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error");

                        if ((size_t)bytes_printed >= out_string_len - (size_t)buf_ptrdiff)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL,
                                            "dataset fletcher32 filter property string size exceeded "
                                            "allocated buffer size");

                        out_string_curr_pos += bytes_printed;
                        break;
                    } /* H5Z_FILTER_FLETCHER32 */

                    case H5Z_FILTER_SZIP: {
                        const char       *szip_option_mask;
                        const char *const fmt_string = "{"
                                                       "\"class\": \"H5Z_FILTER_SZIP\","
                                                       "\"id\": %d,"
                                                       "\"bitsPerPixel\": %u,"
                                                       "\"coding\": \"%s\","
                                                       "\"pixelsPerBlock\": %u,"
                                                       "\"pixelsPerScanline\": %u"
                                                       "}";

                        switch (cd_values[H5Z_SZIP_PARM_MASK]) {
                            case H5_SZIP_EC_OPTION_MASK:
                                szip_option_mask = "H5_SZIP_EC_OPTION_MASK";
                                break;

                            case H5_SZIP_NN_OPTION_MASK:
                                szip_option_mask = "H5_SZIP_NN_OPTION_MASK";
                                break;

                            default:
#ifdef RV_CONNECTOR_DEBUG
                                printf(
                                    "-> Unable to add SZIP filter to DCPL - unsupported mask value specified "
                                    "(not H5_SZIP_EC_OPTION_MASK or H5_SZIP_NN_OPTION_MASK)\n\n");
#endif

                                if (flags & H5Z_FLAG_OPTIONAL)
                                    continue;
                                else
                                    FUNC_GOTO_ERROR(
                                        H5E_DATASET, H5E_CANTSET, FAIL,
                                        "can't set SZIP filter on DCPL - unsupported mask value specified "
                                        "(not H5_SZIP_EC_OPTION_MASK or H5_SZIP_NN_OPTION_MASK)");

                                break;
                        } /* end switch */

                        /* Check whether the buffer needs to be grown */
                        bytes_to_print =
                            strlen(fmt_string) + (4 * MAX_NUM_LENGTH) + strlen(szip_option_mask) + 1;

                        buf_ptrdiff = out_string_curr_pos - out_string;
                        if (buf_ptrdiff < 0)
                            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                            "unsafe cast: dataset creation properties buffer pointer "
                                            "difference was negative - this should not happen!");

                        CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                                        out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed =
                                 snprintf(out_string_curr_pos, out_string_len - (size_t)buf_ptrdiff,
                                          fmt_string, H5Z_FILTER_SZIP, cd_values[H5Z_SZIP_PARM_BPP],
                                          cd_values[H5Z_SZIP_PARM_MASK] == H5_SZIP_EC_OPTION_MASK
                                              ? "H5_SZIP_EC_OPTION_MASK"
                                              : "H5_SZIP_NN_OPTION_MASK",
                                          cd_values[H5Z_SZIP_PARM_PPB], cd_values[H5Z_SZIP_PARM_PPS])) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error");

                        if ((size_t)bytes_printed >= out_string_len - (size_t)buf_ptrdiff)
                            FUNC_GOTO_ERROR(
                                H5E_DATASET, H5E_SYSERRSTR, FAIL,
                                "dataset szip filter property string size exceeded allocated buffer size");

                        out_string_curr_pos += bytes_printed;
                        break;
                    } /* H5Z_FILTER_SZIP */

                    case H5Z_FILTER_NBIT: {
                        const char *const fmt_string = "{"
                                                       "\"class\": \"H5Z_FILTER_NBIT\","
                                                       "\"id\": %d"
                                                       "}";

                        /* Check whether the buffer needs to be grown */
                        bytes_to_print = strlen(fmt_string) + MAX_NUM_LENGTH + 1;

                        buf_ptrdiff = out_string_curr_pos - out_string;
                        if (buf_ptrdiff < 0)
                            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                            "unsafe cast: dataset creation properties buffer pointer "
                                            "difference was negative - this should not happen!");

                        CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                                        out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed =
                                 snprintf(out_string_curr_pos, out_string_len - (size_t)buf_ptrdiff,
                                          fmt_string, H5Z_FILTER_NBIT)) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error");

                        if ((size_t)bytes_printed >= out_string_len - (size_t)buf_ptrdiff)
                            FUNC_GOTO_ERROR(
                                H5E_DATASET, H5E_SYSERRSTR, FAIL,
                                "dataset nbit filter property string size exceeded allocated buffer size");

                        out_string_curr_pos += bytes_printed;
                        break;
                    } /* H5Z_FILTER_NBIT */

                    case H5Z_FILTER_SCALEOFFSET: {
                        const char       *scaleType;
                        const char *const fmt_string = "{"
                                                       "\"class\": \"H5Z_FILTER_SCALEOFFSET\","
                                                       "\"id\": %d,"
                                                       "\"scaleType\": \"%s\","
                                                       "\"scaleOffset\": %u"
                                                       "}";

                        switch (cd_values[H5Z_SCALEOFFSET_PARM_SCALETYPE]) {
                            case H5Z_SO_FLOAT_DSCALE:
                                scaleType = "H5Z_SO_FLOAT_DSCALE";
                                break;

                            case H5Z_SO_FLOAT_ESCALE:
                                scaleType = "H5Z_SO_FLOAT_ESCALE";
                                break;

                            case H5Z_SO_INT:
                                scaleType = "H5Z_FLOAT_SO_INT";
                                break;

                            default:
#ifdef RV_CONNECTOR_DEBUG
                                printf("-> Unable to add ScaleOffset filter to DCPL - unsupported scale type "
                                       "specified (not H5Z_SO_FLOAT_DSCALE, H5Z_SO_FLOAT_ESCALE or "
                                       "H5Z_SO_INT)\n\n");
#endif

                                if (flags & H5Z_FLAG_OPTIONAL)
                                    continue;
                                else
                                    FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL,
                                                    "can't set ScaleOffset filter on DCPL - unsupported "
                                                    "scale type specified (not H5Z_SO_FLOAT_DSCALE, "
                                                    "H5Z_SO_FLOAT_ESCALE or H5Z_SO_INT)");

                                break;
                        } /* end switch */

                        /* Check whether the buffer needs to be grown */
                        bytes_to_print = strlen(fmt_string) + (2 * MAX_NUM_LENGTH) + strlen(scaleType) + 1;

                        buf_ptrdiff = out_string_curr_pos - out_string;
                        if (buf_ptrdiff < 0)
                            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                            "unsafe cast: dataset creation properties buffer pointer "
                                            "difference was negative - this should not happen!");

                        CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                                        out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed =
                                 snprintf(out_string_curr_pos, out_string_len - (size_t)buf_ptrdiff,
                                          fmt_string, H5Z_FILTER_SCALEOFFSET, scaleType,
                                          cd_values[H5Z_SCALEOFFSET_PARM_SCALEFACTOR])) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error");

                        if ((size_t)bytes_printed >= out_string_len - (size_t)buf_ptrdiff)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL,
                                            "dataset scaleoffset filter property string size exceeded "
                                            "allocated buffer size");

                        out_string_curr_pos += bytes_printed;
                        break;
                    } /* H5Z_FILTER_SCALEOFFSET */

                    case LZF_FILTER_ID: /* LZF Filter */
                    {
                        const char *const fmt_string = "{"
                                                       "\"class\": \"H5Z_FILTER_LZF\","
                                                       "\"id\": %d"
                                                       "}";

                        /* Check whether the buffer needs to be grown */
                        bytes_to_print = strlen(fmt_string) + MAX_NUM_LENGTH + 1;

                        buf_ptrdiff = out_string_curr_pos - out_string;
                        if (buf_ptrdiff < 0)
                            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                            "unsafe cast: dataset creation properties buffer pointer "
                                            "difference was negative - this should not happen!");

                        CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                                        out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed =
                                 snprintf(out_string_curr_pos, out_string_len - (size_t)buf_ptrdiff,
                                          fmt_string, LZF_FILTER_ID)) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error");

                        if ((size_t)bytes_printed >= out_string_len - (size_t)buf_ptrdiff)
                            FUNC_GOTO_ERROR(
                                H5E_DATASET, H5E_SYSERRSTR, FAIL,
                                "dataset lzf filter property string size exceeded allocated buffer size");

                        out_string_curr_pos += bytes_printed;
                        break;
                    } /* LZF_FILTER_ID */

                    case H5Z_FILTER_ERROR: {
#ifdef RV_CONNECTOR_DEBUG
                        printf("-> Unknown filter specified for filter %zu - not adding to DCPL\n\n", i);
#endif

                        if (flags & H5Z_FLAG_OPTIONAL)
                            continue;
                        else
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "invalid filter specified");

                        break;
                    } /* H5Z_FILTER_ERROR */

                    default: /* User-defined filter */
                    {
                        char             *parameters = NULL;
                        const char *const fmt_string = "{"
                                                       "\"class\": \"H5Z_FILTER_USER\","
                                                       "\"id\": %d,"
                                                       "\"parameters\": %s"
                                                       "}";

                        if (filter_id < 0) {
                            if (flags & H5Z_FLAG_OPTIONAL)
                                continue;
                            else
                                FUNC_GOTO_ERROR(
                                    H5E_DATASET, H5E_CANTSET, FAIL,
                                    "Unable to set filter on DCPL - invalid filter specified for filter %zu",
                                    i);
                        } /* end if */

                        /* Retrieve all of the parameters for the user-defined filter */

                        /* Check whether the buffer needs to be grown */
                        bytes_to_print = strlen(fmt_string) + MAX_NUM_LENGTH + strlen(parameters) + 1;

                        buf_ptrdiff = out_string_curr_pos - out_string;
                        if (buf_ptrdiff < 0)
                            FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                            "unsafe cast: dataset creation properties buffer pointer "
                                            "difference was negative - this should not happen!");

                        CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                                        out_string_curr_pos, H5E_DATASET, FAIL);

                        if ((bytes_printed =
                                 snprintf(out_string_curr_pos, out_string_len - (size_t)buf_ptrdiff,
                                          fmt_string, filter_id, parameters)) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error");

                        if ((size_t)bytes_printed >= out_string_len - (size_t)buf_ptrdiff)
                            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL,
                                            "dataset user-defined filter property string size exceeded "
                                            "allocated buffer size");

                        out_string_curr_pos += bytes_printed;
                        break;
                    } /* User-defined filter */
                }     /* end switch */

                /* TODO: When the addition of an optional filter fails, it should use the continue statement
                 * to allow this loop to continue instead of throwing an error stack and failing the whole
                 * function. However, when this happens, a trailing comma may be left behind if the optional
                 * filter was the last one to be added. The resulting JSON may look like:
                 *
                 * [{filter},{filter},{filter},]
                 *
                 * and this currently will cause the server to return a 500 error.
                 */
                if (i < nfilters - 1) {
                    buf_ptrdiff = out_string_curr_pos - out_string;
                    if (buf_ptrdiff < 0)
                        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                        "unsafe cast: dataset creation properties buffer pointer difference "
                                        "was negative - this should not happen!");

                    CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + 1, out_string_curr_pos,
                                    H5E_DATASET, FAIL);

                    strcat(out_string_curr_pos++, ",");
                } /* end if */
            }     /* end for */

            /* Make sure to add a closing ']' to close the 'filters' section */
            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                "unsafe cast: dataset creation properties buffer pointer difference was "
                                "negative - this should not happen!");

            CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + 1, out_string_curr_pos,
                            H5E_DATASET, FAIL);

            strcat(out_string_curr_pos++, "]");
        } /* end if */
    }

    /****************************************************************************************
     *                                                                                      *
     *                                    Layout Section                                    *
     *                                                                                      *
     * Determine the layout information of the Dataset and append its string representation *
     *                                                                                      *
     ****************************************************************************************/
    switch (H5Pget_layout(dcpl)) {
        case H5D_COMPACT: {
            const char *const compact_layout_str     = ", \"layout\": {"
                                                       "\"class\": \"H5D_COMPACT\""
                                                       "}";
            size_t            compact_layout_str_len = strlen(compact_layout_str);

            /* Check whether the buffer needs to be grown */
            bytes_to_print = compact_layout_str_len + 1;

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                "unsafe cast: dataset creation properties buffer pointer difference was "
                                "negative - this should not happen!");

            CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                            out_string_curr_pos, H5E_DATASET, FAIL);

            strncat(out_string_curr_pos, compact_layout_str, compact_layout_str_len);
            out_string_curr_pos += compact_layout_str_len;
            break;
        } /* H5D_COMPACT */

        case H5D_CONTIGUOUS: {
            const char *const contiguous_layout_str = ", \"layout\": {\"class\": \"H5D_CONTIGUOUS\"";
            int               external_file_count;

            /* Check whether the buffer needs to be grown */
            bytes_to_print = strlen(contiguous_layout_str);

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                "unsafe cast: dataset creation properties buffer pointer difference was "
                                "negative - this should not happen!");

            CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                            out_string_curr_pos, H5E_DATASET, FAIL);

            /* Append the "contiguous layout" string */
            strncat(out_string_curr_pos, contiguous_layout_str, bytes_to_print);
            out_string_curr_pos += bytes_to_print;

            /* Determine if there are external files for the dataset */
            if ((external_file_count = H5Pget_external_count(dcpl)) < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_CANTGET, FAIL, "can't retrieve external file count");

            if (external_file_count > 0) {
                size_t            i;
                const char *const external_storage_str = ", externalStorage: [";
                const char *const external_file_str    = "%s{"
                                                         "\"name\": %s,"
                                                         "\"offset\": " OFF_T_SPECIFIER ","
                                                         "\"size\": %llu"
                                                         "}";

                /* Check whether the buffer needs to be grown */
                bytes_to_print += strlen(external_storage_str);

                buf_ptrdiff = out_string_curr_pos - out_string;
                if (buf_ptrdiff < 0)
                    FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                    "unsafe cast: dataset creation properties buffer pointer difference was "
                                    "negative - this should not happen!");

                CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                                out_string_curr_pos, H5E_DATASET, FAIL);

                /* Append the "external storage" string */
                strncat(out_string_curr_pos, external_storage_str, bytes_to_print);
                out_string_curr_pos += bytes_to_print;

                /* Append an entry for each of the external files */
                for (i = 0; i < (size_t)external_file_count; i++) {
                    hsize_t file_size;
                    off_t   file_offset;
                    char    file_name[EXTERNAL_FILE_NAME_MAX_LENGTH];

                    if (H5Pget_external(dcpl, (unsigned)i, (size_t)EXTERNAL_FILE_NAME_MAX_LENGTH, file_name,
                                        &file_offset, &file_size) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL,
                                        "can't get information for external file %zu from DCPL", i);

                    /* Ensure that the file name buffer is NULL-terminated */
                    file_name[EXTERNAL_FILE_NAME_MAX_LENGTH - 1] = '\0';

                    bytes_to_print += strlen(external_file_str) + strlen(file_name) + (2 * MAX_NUM_LENGTH) +
                                      (i > 0 ? 1 : 0) - 8;

                    /* Check whether the buffer needs to be grown */
                    buf_ptrdiff = out_string_curr_pos - out_string;
                    if (buf_ptrdiff < 0)
                        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                        "unsafe cast: dataset creation properties buffer pointer difference "
                                        "was negative - this should not happen!");

                    CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                                    out_string_curr_pos, H5E_DATASET, FAIL);

                    if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t)buf_ptrdiff,
                                                  external_file_str, (i > 0) ? "," : "", file_name,
                                                  OFF_T_CAST file_offset, file_size)) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error");

                    if ((size_t)bytes_printed >= out_string_len - (size_t)buf_ptrdiff)
                        FUNC_GOTO_ERROR(
                            H5E_DATASET, H5E_SYSERRSTR, FAIL,
                            "dataset external file list property string size exceeded allocated buffer size");

                    out_string_curr_pos += bytes_printed;
                } /* end for */

                /* Make sure to add a closing ']' to close the external file section */
                buf_ptrdiff = out_string_curr_pos - out_string;
                if (buf_ptrdiff < 0)
                    FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                    "unsafe cast: dataset creation properties buffer pointer difference was "
                                    "negative - this should not happen!");

                CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + 1, out_string_curr_pos,
                                H5E_DATASET, FAIL);

                strcat(out_string_curr_pos++, "]");
            } /* end if */

            /* Make sure to add a closing '}' to close the 'layout' section */
            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                "unsafe cast: dataset creation properties buffer pointer difference was "
                                "negative - this should not happen!");

            CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + 1, out_string_curr_pos,
                            H5E_DATASET, FAIL);

            strcat(out_string_curr_pos++, "}");

            break;
        } /* H5D_CONTIGUOUS */

        case H5D_CHUNKED: {
            hsize_t           chunk_dims[H5S_MAX_RANK + 1];
            size_t            i;
            char             *chunk_dims_string_curr_pos;
            int               ndims;
            const char *const fmt_string = ", \"layout\": {"
                                           "\"class\": \"H5D_CHUNKED\","
                                           "\"dims\": %s"
                                           "}";

            if ((ndims = H5Pget_chunk(dcpl, H5S_MAX_RANK + 1, chunk_dims)) < 0)
                FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't retrieve dataset chunk dimensionality");

            if (!ndims)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "no chunk dimensionality specified");

            if (NULL ==
                (chunk_dims_string = (char *)RV_malloc((size_t)((ndims * MAX_NUM_LENGTH) + ndims + 3))))
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL,
                                "can't allocate space for chunk dimensionality string");
            chunk_dims_string_curr_pos  = chunk_dims_string;
            *chunk_dims_string_curr_pos = '\0';

            strcat(chunk_dims_string_curr_pos++, "[");

            for (i = 0; i < (size_t)ndims; i++) {
                if ((bytes_printed = snprintf(chunk_dims_string_curr_pos, MAX_NUM_LENGTH, "%s%" PRIuHSIZE,
                                              i > 0 ? "," : "", chunk_dims[i])) < 0)
                    FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error");

                if (bytes_printed >= MAX_NUM_LENGTH)
                    FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL,
                                    "chunk 'dimension size' string size exceeded maximum number string size");

                chunk_dims_string_curr_pos += bytes_printed;
            } /* end for */

            strcat(chunk_dims_string_curr_pos++, "]");

            /* Check whether the buffer needs to be grown */
            bytes_to_print = strlen(fmt_string) + strlen(chunk_dims_string) + 1;

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                "unsafe cast: dataset creation properties buffer pointer difference was "
                                "negative - this should not happen!");

            CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                            out_string_curr_pos, H5E_DATASET, FAIL);

            if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t)buf_ptrdiff,
                                          fmt_string, chunk_dims_string)) < 0)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error");

            if ((size_t)bytes_printed >= out_string_len - (size_t)buf_ptrdiff)
                FUNC_GOTO_ERROR(
                    H5E_DATASET, H5E_SYSERRSTR, FAIL,
                    "dataset chunk dimensionality property string size exceeded allocated buffer size");

            out_string_curr_pos += bytes_printed;
            break;
        } /* H5D_CHUNKED */

        case H5D_VIRTUAL:
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_UNSUPPORTED, FAIL, "unsupported dataset layout: Virtual");
            break;

        case H5D_LAYOUT_ERROR:
        case H5D_NLAYOUTS:
        default:
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't retrieve dataset layout property");
    } /* end switch */

    /*************************************************************************************
     *                                                                                   *
     *                            Object Time Tracking Section                           *
     *                                                                                   *
     * Determine the status of object time tracking and append its string representation *
     *                                                                                   *
     *************************************************************************************/
    {
        hbool_t track_times;

        if (H5Pget_obj_track_times(dcpl, &track_times) < 0)
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't retrieve object time tracking property");

        if (track_times) {
            const char *const track_times_true     = ", \"trackTimes\": \"true\"";
            size_t            track_times_true_len = strlen(track_times_true);

            /* Check whether the buffer needs to be grown */
            bytes_to_print = track_times_true_len + 1;

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                "unsafe cast: dataset creation properties buffer pointer difference was "
                                "negative - this should not happen!");

            CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                            out_string_curr_pos, H5E_DATASET, FAIL);

            strncat(out_string_curr_pos, track_times_true, track_times_true_len);
            out_string_curr_pos += track_times_true_len;
        } /* end if */
        else {
            const char *const track_times_false     = ", \"trackTimes\": \"false\"";
            size_t            track_times_false_len = strlen(track_times_false);

            /* Check whether the buffer needs to be grown */
            bytes_to_print = track_times_false_len + 1;

            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                "unsafe cast: dataset creation properties buffer pointer difference was "
                                "negative - this should not happen!");

            CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + bytes_to_print,
                            out_string_curr_pos, H5E_DATASET, FAIL);

            strncat(out_string_curr_pos, track_times_false, track_times_false_len);
            out_string_curr_pos += track_times_false_len;
        } /* end else */
    }

    /* Make sure to add a closing '}' to close the creationProperties section */
    buf_ptrdiff = out_string_curr_pos - out_string;
    if (buf_ptrdiff < 0)
        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                        "unsafe cast: dataset creation properties buffer pointer difference was negative - "
                        "this should not happen!");

    CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + 1, out_string_curr_pos, H5E_DATASET,
                    FAIL);

    strcat(out_string_curr_pos++, "}");

done:
    if (ret_value >= 0) {
        *creation_properties_body = out_string;
        if (creation_properties_body_len) {
            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_DONE_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                "unsafe cast: dataset creation properties buffer pointer difference was "
                                "negative - this should not happen!");
            else
                *creation_properties_body_len = (size_t)buf_ptrdiff;
        } /* end if */

#ifdef RV_CONNECTOR_DEBUG
        printf("-> DCPL JSON representation:\n%s\n\n", out_string);
#endif
    } /* end if */
    else {
        if (out_string)
            RV_free(out_string);
    } /* end else */

    if (chunk_dims_string)
        RV_free(chunk_dims_string);

    return ret_value;
} /* end RV_convert_dataset_creation_properties_to_JSON() */

/*-------------------------------------------------------------------------
 * Function:    RV_setup_dataset_create_request_body
 *
 * Purpose:     Given a DCPL during a dataset create operation, converts
 *              the datatype and shape of a dataset into JSON, then
 *              combines these with a JSON-ified list of the Dataset
 *              Creation Properties, as well as an optional JSON-formatted
 *              link string to link the Dataset into the file structure,
 *              into one large string of JSON to be used as the request
 *              body during the Dataset create operation. The string
 *              buffer returned by this function must be freed by the
 *              caller, else memory will be leaked.
 *
 * Return:      Non-negative on success, negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
RV_setup_dataset_create_request_body(void *parent_obj, const char *name, hid_t type_id, hid_t space_id,
                                     hid_t lcpl_id, hid_t dcpl, char **create_request_body,
                                     size_t *create_request_body_len)
{
    RV_object_t *pobj                         = (RV_object_t *)parent_obj;
    size_t       creation_properties_body_len = 0;
    size_t       create_request_nalloc        = 0;
    size_t       datatype_body_len            = 0;
    size_t       link_body_nalloc             = 0;
    char        *datatype_body                = NULL;
    char        *out_string                   = NULL;
    char        *shape_body                   = NULL;
    char        *maxdims_body                 = NULL;
    char        *creation_properties_body     = NULL;
    char        *link_body                    = NULL;
    char        *path_dirname                 = NULL;
    int          create_request_len           = 0;
    int          link_body_len                = 0;
    herr_t       ret_value                    = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Setting up dataset creation request\n\n");
#endif

    if (H5I_FILE != pobj->obj_type && H5I_GROUP != pobj->obj_type)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "parent object not a file or group");
    if (!create_request_body)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "dataset create request output buffer was NULL");

    /* Form the Datatype portion of the Dataset create request */
    if (RV_convert_datatype_to_JSON(type_id, &datatype_body, &datatype_body_len, FALSE) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCONVERT, FAIL,
                        "can't convert dataset's datatype to JSON representation");

    /* If the Dataspace of the Dataset was not specified as H5P_DEFAULT, parse it. */
    if (H5P_DEFAULT != space_id)
        if (RV_convert_dataspace_shape_to_JSON(space_id, &shape_body, &maxdims_body) < 0)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCREATE, FAIL,
                            "can't convert dataset's dataspace to JSON representation");

    /* If the DCPL was not specified as H5P_DEFAULT, form the Dataset Creation Properties portion of the
     * Dataset create request */
    if (H5P_DATASET_CREATE_DEFAULT != dcpl) {
        if ((H5Pget_layout(dcpl) == H5D_CONTIGUOUS) &&
            !(SERVER_VERSION_MATCHES_OR_EXCEEDS(pobj->domain->u.file.server_version, 0, 8, 0)))
            FUNC_GOTO_ERROR(H5E_PLIST, H5E_UNSUPPORTED, FAIL,
                            "layout H5D_CONTIGUOUS is unsupported for server versions before 0.8.0");

        if (RV_convert_dataset_creation_properties_to_JSON(dcpl, &creation_properties_body,
                                                           &creation_properties_body_len) < 0)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTCONVERT, FAIL,
                            "can't convert Dataset Creation Properties to JSON representation");
    }
    /* If this isn't an H5Dcreate_anon call, create a link for the Dataset to
     * link it into the file structure */
    if (name) {
        hbool_t           empty_dirname;
        char              target_URI[URI_MAX_LENGTH];
        const char *const link_basename    = H5_rest_basename(name);
        const char *const link_body_format = "\"link\": {"
                                             "\"id\": \"%s\", "
                                             "\"name\": \"%s\""
                                             "}";

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Creating JSON link for dataset\n\n");
#endif

        /* In case the user specified a path which contains multiple groups on the way to the
         * one which the dataset will ultimately be linked under, extract out the path to the
         * final group in the chain */
        if (NULL == (path_dirname = H5_rest_dirname(name)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_BADVALUE, FAIL, "invalid pathname for dataset link");
        empty_dirname = !strcmp(path_dirname, "");

        /* If the path to the final group in the chain wasn't empty, get the URI of the final
         * group in order to correctly link the dataset into the file structure. Otherwise,
         * the supplied parent group is the one housing the dataset, so just use its URI.
         */
        if (!empty_dirname) {
            H5I_type_t obj_type = H5I_GROUP;
            htri_t     search_ret;

            search_ret = RV_find_object_by_path(pobj, path_dirname, &obj_type, RV_copy_object_URI_callback,
                                                NULL, target_URI);
            if (!search_ret || search_ret < 0)
                FUNC_GOTO_ERROR(H5E_DATASET, H5E_PATH, FAIL, "can't locate target for dataset link");
        } /* end if */

        link_body_nalloc = strlen(link_body_format) + strlen(link_basename) +
                           (empty_dirname ? strlen(pobj->URI) : strlen(target_URI)) + 1;
        if (NULL == (link_body = (char *)RV_malloc(link_body_nalloc)))
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL, "can't allocate space for dataset link body");

        /* Form the Dataset Creation Link portion of the Dataset create request using the above format
         * specifier and the corresponding arguments */
        if ((link_body_len = snprintf(link_body, link_body_nalloc, link_body_format,
                                      empty_dirname ? pobj->URI : target_URI, link_basename)) < 0)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error");

        if ((size_t)link_body_len >= link_body_nalloc)
            FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL,
                            "dataset link create request body size exceeded allocated buffer size");
    } /* end if */

    create_request_nalloc = datatype_body_len + (shape_body ? strlen(shape_body) + 2 : 0) +
                            (maxdims_body ? strlen(maxdims_body) + 2 : 0) +
                            (creation_properties_body ? creation_properties_body_len + 2 : 0) +
                            (link_body ? (size_t)link_body_len + 2 : 0) + 3;

    if (NULL == (out_string = (char *)RV_malloc(create_request_nalloc)))
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_CANTALLOC, FAIL,
                        "can't allocate space for dataset creation request body");

    if ((create_request_len = snprintf(
             out_string, create_request_nalloc, "{%s%s%s%s%s%s%s%s%s}",
             datatype_body,                /* Add the required Dataset Datatype description */
             shape_body ? ", " : "",       /* Add separator for Dataset shape section, if specified */
             shape_body ? shape_body : "", /* Add the Dataset Shape description, if specified */
             maxdims_body ? ", " : "",     /* Add separator for Max Dims section, if specified */
             maxdims_body ? maxdims_body
                          : "", /* Add the Dataset Maximum Dimension Size section, if specified */
             creation_properties_body
                 ? ", "
                 : "", /* Add separator for Dataset Creation properties section, if specified */
             creation_properties_body ? creation_properties_body
                                      : "", /* Add the Dataset Creation properties section, if specified */
             link_body ? ", " : "",         /* Add separator for Link Creation section, if specified */
             link_body ? link_body : "")    /* Add the Link Creation section, if specified */
         ) < 0)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL, "snprintf error");

    if ((size_t)create_request_len >= create_request_nalloc)
        FUNC_GOTO_ERROR(H5E_DATASET, H5E_SYSERRSTR, FAIL,
                        "dataset create request body size exceeded allocated buffer size");

done:
#ifdef RV_CONNECTOR_DEBUG
    printf("\n");
#endif

    if (ret_value >= 0) {
        *create_request_body = out_string;
        if (create_request_body_len)
            *create_request_body_len = (size_t)create_request_len;

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Dataset creation request JSON:\n%s\n\n", out_string);
#endif
    } /* end if */
    else {
        if (out_string)
            RV_free(out_string);
    } /* end else */

    if (link_body)
        RV_free(link_body);
    if (path_dirname)
        RV_free(path_dirname);
    if (creation_properties_body)
        RV_free(creation_properties_body);
    if (maxdims_body)
        RV_free(maxdims_body);
    if (shape_body)
        RV_free(shape_body);
    if (datatype_body)
        RV_free(datatype_body);

    return ret_value;
} /* end RV_setup_dataset_create_request_body() */

/*-------------------------------------------------------------------------
 * Function:    RV_convert_dataspace_selection_to_string
 *
 * Purpose:     Given an HDF5 dataspace, formats the selection within the
 *              dataspace into either a JSON-based or purely string-based
 *              representation, depending on whether req_param is specified
 *              as FALSE or TRUE, respectively. This is used during dataset
 *              reads/writes in order to make a correct REST API call to
 *              the server for reading/writing a dataset by hyperslabs or
 *              point selections. The string buffer handed back by this
 *              function by the caller, else memory will be leaked.
 *
 *              When req_param is specified as TRUE, the selection is
 *              formatted purely as a string which can be included as a
 *              request parameter in the URL of a dataset write request,
 *              which is useful when doing a binary transfer of the data,
 *              since JSON can't be included in the request body in that
 *              case.
 *
 *              When req_param is specified as FALSE, the seleciton is
 *              formatted as JSON so that it can be included in the request
 *              body of a dataset read/write. This form is primarily used
 *              for point selections and hyperslab selections where the
 *              datatype of the dataset is variable-length.
 *
 * Return:      Non-negative on success, negative on failure
 *
 * Programmer:  Jordan Henderson
 *              March, 2017
 */
static herr_t
RV_convert_dataspace_selection_to_string(hid_t space_id, char **selection_string,
                                         size_t *selection_string_len, hbool_t req_param)
{
    ptrdiff_t buf_ptrdiff;
    hsize_t  *point_list = NULL;
    hsize_t  *start      = NULL;
    hsize_t  *stride     = NULL;
    hsize_t  *count      = NULL;
    hsize_t  *block      = NULL;
    size_t    i;
    size_t    out_string_len;
    char     *out_string = NULL;
    char     *out_string_curr_pos;
    char     *start_body    = NULL;
    char     *stop_body     = NULL;
    char     *step_body     = NULL;
    int       bytes_printed = 0;
    int       ndims;
    herr_t    ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Converting selection within dataspace to JSON\n\n");
#endif

    if (!selection_string)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "dataspace selection string was NULL");

    out_string_len = DATASPACE_SELECTION_STRING_DEFAULT_SIZE;
    if (NULL == (out_string = (char *)RV_malloc(out_string_len)))
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL,
                        "can't allocate space for dataspace selection string");

    out_string_curr_pos = out_string;

    /* Ensure that the buffer is NUL-terminated */
    *out_string_curr_pos = '\0';

    if (H5I_DATASPACE != H5Iget_type(space_id))
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "not a dataspace");

    if ((ndims = H5Sget_simple_extent_ndims(space_id)) < 0)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't retrieve dataspace dimensionality");
    if (!ndims)
        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "0-dimension dataspace specified");

    if (req_param) {
        /* Format the selection in a manner such that it can be used as a request parameter in
         * an HTTP request. This is primarily the format used when the datatype of the Dataset
         * being written to/read from is a fixed-length datatype. In this case, the server can
         * support a purely binary data transfer, in which case the selection information has
         * to be sent as a request parameter instead of in the request body.
         */
        switch (H5Sget_select_type(space_id)) {
            case H5S_SEL_ALL:
            case H5S_SEL_NONE:
                break;

            case H5S_SEL_POINTS:
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_UNSUPPORTED, FAIL,
                                "point selections are unsupported as a HTTP request parameter");
                break;

            case H5S_SEL_HYPERSLABS: {
#ifdef RV_CONNECTOR_DEBUG
                printf("-> Hyperslab selection\n\n");
#endif

                /* Format the hyperslab selection according to the 'select' request/query parameter.
                 * This is composed of N triplets, one for each dimension of the dataspace, and looks like:
                 *
                 * [X:Y:Z, X:Y:Z, ...]
                 *
                 * where X is the starting coordinate of the selection, Y is the ending coordinate of
                 * the selection, and Z is the stride of the selection in that dimension.
                 */
                if (NULL == (start = (hsize_t *)RV_malloc((size_t)ndims * sizeof(*start))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL,
                                    "can't allocate space for hyperslab selection 'start' values");
                if (NULL == (stride = (hsize_t *)RV_malloc((size_t)ndims * sizeof(*stride))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL,
                                    "can't allocate space for hyperslab selection 'stride' values");
                if (NULL == (count = (hsize_t *)RV_malloc((size_t)ndims * sizeof(*count))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL,
                                    "can't allocate space for hyperslab selection 'count' values");
                if (NULL == (block = (hsize_t *)RV_malloc((size_t)ndims * sizeof(*block))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL,
                                    "can't allocate space for hyperslab selection 'block' values");

                if (H5Sget_regular_hyperslab(space_id, start, stride, count, block) < 0)
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL,
                                    "can't get regular hyperslab selection");

                strcat(out_string_curr_pos++, "[");

                /* Append a tuple for each dimension of the dataspace */
                for (i = 0; i < (size_t)ndims; i++) {
                    buf_ptrdiff = out_string_curr_pos - out_string;
                    if (buf_ptrdiff < 0)
                        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                        "unsafe cast: dataspace buffer pointer difference was negative - "
                                        "this should not happen!");

                    size_t out_string_new_len = (size_t)buf_ptrdiff + (3 * MAX_NUM_LENGTH) + 4;

                    CHECKED_REALLOC(out_string, out_string_len, out_string_new_len, out_string_curr_pos,
                                    H5E_DATASPACE, FAIL);

                    if ((bytes_printed = snprintf(
                             out_string_curr_pos, out_string_new_len,
                             "%s%" PRIuHSIZE ":%" PRIuHSIZE ":%" PRIuHSIZE, i > 0 ? "," : "", start[i],
                             (start[i] + (stride[i] * (count[i] - 1)) + (block[i] - 1) + 1),
                             (stride[i] / block[i]))) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "snprintf error");

                    out_string_curr_pos += bytes_printed;
                } /* end for */

                buf_ptrdiff = out_string_curr_pos - out_string;
                if (buf_ptrdiff < 0)
                    FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                    "unsafe cast: dataspace buffer pointer difference was negative - this "
                                    "should not happen!");

                CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + 2, out_string_curr_pos,
                                H5E_DATASPACE, FAIL);

                strcat(out_string_curr_pos++, "]");

                break;
            } /* H5S_SEL_HYPERSLABS */

            case H5S_SEL_ERROR:
            case H5S_SEL_N:
            default:
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "invalid selection type");
        } /* end switch */
    }     /* end if */
    else {
        /* Format the selection as JSON so that it can be sent in the request body of an HTTP
         * request. This is primarily the format used when the datatype of the Dataset being
         * written to/read from is a variable-length datatype. In this case, the server cannot
         * support a purely binary data transfer, and the selection information as well as the
         * data has to be sent as JSON in the request body.
         */
        switch (H5Sget_select_type(space_id)) {
            case H5S_SEL_ALL:
            case H5S_SEL_NONE:
                break;

            case H5S_SEL_POINTS: {
                const char *const points_str = "\"points\": [";
                hssize_t          num_points;
                size_t            points_strlen = strlen(points_str);

#ifdef RV_CONNECTOR_DEBUG
                printf("-> Point selection\n\n");
#endif

                if ((num_points = H5Sget_select_npoints(space_id)) < 0)
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't get number of selected points");

                if (NULL ==
                    (point_list = (hsize_t *)RV_malloc((size_t)(ndims * num_points) * sizeof(*point_list))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate point list buffer");

                if (H5Sget_select_elem_pointlist(space_id, 0, (hsize_t)num_points, point_list) < 0)
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "can't retrieve point list");

                CHECKED_REALLOC(out_string, out_string_len, points_strlen + 1, out_string_curr_pos,
                                H5E_DATASPACE, FAIL);

                strcat(out_string_curr_pos, points_str);
                out_string_curr_pos += strlen(points_str);

                for (i = 0; i < (hsize_t)num_points; i++) {
                    size_t j;

                    /* Check whether the buffer needs to grow to accommodate the next point */
                    buf_ptrdiff = out_string_curr_pos - out_string;
                    if (buf_ptrdiff < 0)
                        FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                        "unsafe cast: dataspace buffer pointer difference was negative - "
                                        "this should not happen!");

                    size_t out_string_new_len =
                        (size_t)buf_ptrdiff +
                        ((size_t)((ndims * MAX_NUM_LENGTH) + (ndims) + (ndims > 1 ? 3 : 1)));

                    CHECKED_REALLOC(out_string, out_string_len, out_string_new_len, out_string_curr_pos,
                                    H5E_DATASPACE, FAIL);

                    /* Add the delimiter between individual points */
                    if (i > 0)
                        strcat(out_string_curr_pos++, ",");

                    /* Add starting bracket for the next point, if applicable */
                    if (ndims > 1)
                        strcat(out_string_curr_pos++, "[");

                    for (j = 0; j < (size_t)ndims; j++) {
                        if ((bytes_printed = snprintf(
                                 out_string_curr_pos, out_string_new_len - (size_t)buf_ptrdiff,
                                 "%s%" PRIuHSIZE, j > 0 ? "," : "", point_list[(i * (size_t)ndims) + j])) < 0)
                            FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "snprintf error");

                        out_string_curr_pos += bytes_printed;
                    } /* end for */

                    /* Enclose the current point in brackets */
                    if (ndims > 1)
                        strcat(out_string_curr_pos++, "]");
                } /* end for */

                buf_ptrdiff = out_string_curr_pos - out_string;
                if (buf_ptrdiff < 0)
                    FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                    "unsafe cast: dataspace buffer pointer difference was negative - this "
                                    "should not happen!");

                CHECKED_REALLOC(out_string, out_string_len, (size_t)buf_ptrdiff + 2, out_string_curr_pos,
                                H5E_DATASPACE, FAIL);

                strcat(out_string_curr_pos++, "]");

                break;
            } /* H5S_SEL_POINTS */

            case H5S_SEL_HYPERSLABS: {
                /* Format the hyperslab selection according to the 'start', 'stop' and 'step' keys
                 * in a JSON request body. This looks like:
                 *
                 * "start": X, X, ...,
                 * "stop": Y, Y, ...,
                 * "step": Z, Z, ...
                 */
                char             *start_body_curr_pos, *stop_body_curr_pos, *step_body_curr_pos;
                const char *const slab_format = "\"start\": %s,"
                                                "\"stop\": %s,"
                                                "\"step\": %s";

#ifdef RV_CONNECTOR_DEBUG
                printf("-> Hyperslab selection\n\n");
#endif

                if (NULL == (start = (hsize_t *)RV_malloc((size_t)ndims * sizeof(*start))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL,
                                    "can't allocate space for hyperslab selection 'start' values");
                if (NULL == (stride = (hsize_t *)RV_malloc((size_t)ndims * sizeof(*stride))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL,
                                    "can't allocate space for hyperslab selection 'stride' values");
                if (NULL == (count = (hsize_t *)RV_malloc((size_t)ndims * sizeof(*count))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL,
                                    "can't allocate space for hyperslab selection 'count' values");
                if (NULL == (block = (hsize_t *)RV_malloc((size_t)ndims * sizeof(*block))))
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL,
                                    "can't allocate space for hyperslab selection 'block' values");

                size_t body_size = (size_t)ndims * MAX_NUM_LENGTH + (size_t)ndims;

                if (NULL == (start_body = (char *)RV_calloc(body_size)))
                    FUNC_GOTO_ERROR(
                        H5E_DATASPACE, H5E_CANTALLOC, FAIL,
                        "can't allocate space for hyperslab selection 'start' values string representation");
                start_body_curr_pos = start_body;

                if (NULL == (stop_body = (char *)RV_calloc(body_size)))
                    FUNC_GOTO_ERROR(
                        H5E_DATASPACE, H5E_CANTALLOC, FAIL,
                        "can't allocate space for hyperslab selection 'stop' values string representation");
                stop_body_curr_pos = stop_body;

                if (NULL == (step_body = (char *)RV_calloc(body_size)))
                    FUNC_GOTO_ERROR(
                        H5E_DATASPACE, H5E_CANTALLOC, FAIL,
                        "can't allocate space for hyperslab selection 'step' values string representation");
                step_body_curr_pos = step_body;

                strcat(start_body_curr_pos++, "[");
                strcat(stop_body_curr_pos++, "[");
                strcat(stop_body_curr_pos++, "[");

                if (H5Sget_regular_hyperslab(space_id, start, stride, count, block) < 0)
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL,
                                    "can't get regular hyperslab selection");

                for (i = 0; i < (size_t)ndims; i++) {
                    if ((bytes_printed = snprintf(start_body_curr_pos,
                                                  body_size - (size_t)(start_body_curr_pos - start_body),
                                                  "%s%" PRIuHSIZE, (i > 0 ? "," : ""), start[i])) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "snprintf error");
                    start_body_curr_pos += bytes_printed;

                    if ((bytes_printed = snprintf(
                             stop_body_curr_pos, body_size - (size_t)(stop_body_curr_pos - stop_body),
                             "%s%" PRIuHSIZE, (i > 0 ? "," : ""),
                             (start[i] + (stride[i] * (count[i] - 1)) + (block[i] - 1) + 1))) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "snprintf error");
                    stop_body_curr_pos += bytes_printed;

                    if ((bytes_printed = snprintf(
                             step_body_curr_pos, body_size - (size_t)(step_body_curr_pos - step_body),
                             "%s%" PRIuHSIZE, (i > 0 ? "," : ""), (stride[i] / block[i]))) < 0)
                        FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "snprintf error");
                    step_body_curr_pos += bytes_printed;
                } /* end for */

                strcat(start_body_curr_pos, "]");
                strcat(stop_body_curr_pos, "]");
                strcat(step_body_curr_pos, "]");

                buf_ptrdiff = out_string_curr_pos - out_string;
                if (buf_ptrdiff < 0)
                    FUNC_GOTO_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                    "unsafe cast: dataspace buffer pointer difference was negative - this "
                                    "should not happen!");

                CHECKED_REALLOC(out_string, out_string_len,
                                (size_t)buf_ptrdiff + strlen(start_body) + strlen(stop_body) +
                                    strlen(step_body) + 1,
                                out_string_curr_pos, H5E_DATASPACE, FAIL);

                if ((bytes_printed = snprintf(out_string_curr_pos, out_string_len - (size_t)buf_ptrdiff,
                                              slab_format, start_body, stop_body, step_body)) < 0)
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL, "snprintf error");

                if ((size_t)bytes_printed >= out_string_len - (size_t)buf_ptrdiff)
                    FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_SYSERRSTR, FAIL,
                                    "dataspace string size exceeded allocated buffer size");

                out_string_curr_pos += bytes_printed;

                break;
            } /* H5S_SEL_HYPERSLABS */

            case H5S_SEL_ERROR:
            case H5S_SEL_N:
            default:
                FUNC_GOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "invalid selection type");
        } /* end switch(H5Sget_select_type()) */
    }     /* end else */

done:
    if (ret_value >= 0) {
        *selection_string = out_string;
        if (selection_string_len) {
            buf_ptrdiff = out_string_curr_pos - out_string;
            if (buf_ptrdiff < 0)
                FUNC_DONE_ERROR(H5E_INTERNAL, H5E_BADVALUE, FAIL,
                                "unsafe cast: dataspace buffer pointer difference was negative - this should "
                                "not happen!");
            else
                *selection_string_len = (size_t)buf_ptrdiff;
        } /* end if */

#ifdef RV_CONNECTOR_DEBUG
        printf("-> Dataspace selection JSON representation:\n%s\n\n", out_string);
#endif
    } /* end if */
    else {
        if (out_string)
            RV_free(out_string);
    } /* end else */

    if (step_body)
        RV_free(step_body);
    if (stop_body)
        RV_free(stop_body);
    if (start_body)
        RV_free(start_body);
    if (block)
        RV_free(block);
    if (count)
        RV_free(count);
    if (stride)
        RV_free(stride);
    if (start)
        RV_free(start);
    if (point_list)
        RV_free(point_list);

    return ret_value;
} /* end RV_convert_dataspace_selection_to_string() */

/*-------------------------------------------------------------------------
 * Function:    RV_convert_obj_refs_to_buffer
 *
 * Purpose:     Given an array of rv_obj_ref_t structs, as well as the
 *              array's size, this function converts the array of object
 *              references into a binary buffer of object reference
 *              strings, which can then be transferred to the server.
 *
 *              Note that HDF Kita expects each element of an object
 *              reference typed dataset to be a 48-byte string, which
 *              should be enough to hold the URI of the referenced object,
 *              as well as a prefixed string corresponding to the type of
 *              the referenced object, e.g. an object reference to a group
 *              may look like
 *              "groups/g-7e538c7e-d9dd-11e7-b940-0242ac110009".
 *
 *              Therefore, this function allocates a buffer of size
 *              (48 * number of elements in object reference array) bytes
 *              and continues to append strings until the end of the array
 *              is reached. If a string is less than 48 bytes in length,
 *              the bytes following the string's NUL terminator may be junk,
 *              but the server should be smart enough to handle this case.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static herr_t
RV_convert_obj_refs_to_buffer(const rv_obj_ref_t *ref_array, size_t ref_array_len, char **buf_out,
                              size_t *buf_out_len)
{
    const char *const prefix_table[] = {"groups", "datatypes", "datasets"};
    size_t            i;
    size_t            prefix_index;
    size_t            out_len = 0;
    char             *out     = NULL;
    char             *out_curr_pos;
    int               ref_string_len = 0;
    herr_t            ret_value      = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Converting object ref. array to binary buffer\n\n");
#endif

    if (!ref_array)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "reference array pointer was NULL");
    if (!buf_out)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "output buffer was NULL");
    if (!buf_out_len)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "output buffer size pointer was NULL");
    if (!ref_array_len)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid reference array length specified");

    out_len = ref_array_len * OBJECT_REF_STRING_LEN;
    if (NULL == (out = (char *)RV_malloc(out_len)))
        FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_CANTALLOC, FAIL,
                        "can't allocate space for object reference string buffer");
    out_curr_pos = out;

    for (i = 0; i < ref_array_len; i++) {
        memset(out_curr_pos, 0, OBJECT_REF_STRING_LEN);

        if (!strcmp(ref_array[i].ref_obj_URI, "")) {
            out_curr_pos += OBJECT_REF_STRING_LEN;

            continue;
        } /* end if */

        switch (ref_array[i].ref_obj_type) {
            case H5I_FILE:
            case H5I_GROUP:
                prefix_index = 0;
                break;

            case H5I_DATATYPE:
                prefix_index = 1;
                break;

            case H5I_DATASET:
                prefix_index = 2;
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
                FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_BADVALUE, FAIL, "invalid ref obj. type");
        } /* end switch */

        if ((ref_string_len = snprintf(out_curr_pos, OBJECT_REF_STRING_LEN, "%s/%s",
                                       prefix_table[prefix_index], ref_array[i].ref_obj_URI)) < 0)
            FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_SYSERRSTR, FAIL, "snprintf error");

        if (ref_string_len >= OBJECT_REF_STRING_LEN + 1)
            FUNC_GOTO_ERROR(H5E_REFERENCE, H5E_SYSERRSTR, FAIL,
                            "object reference string size exceeded maximum reference string size");

        out_curr_pos += OBJECT_REF_STRING_LEN;
    } /* end for */

done:
    if (ret_value >= 0) {
        *buf_out     = out;
        *buf_out_len = out_len;

#ifdef RV_CONNECTOR_DEBUG
        for (i = 0; i < ref_array_len; i++) {
            printf("-> Ref_array[%zu]: %s\n", i, (out + (i * OBJECT_REF_STRING_LEN)));
        } /* end for */
        printf("\n");
#endif
    } /* end if */
    else {
        if (out)
            RV_free(out);
    } /* end else */

    return ret_value;
} /* end RV_convert_obj_refs_to_buffer() */

/*-------------------------------------------------------------------------
 * Function:    RV_convert_buffer_to_obj_refs
 *
 * Purpose:     Given a binary buffer of object reference strings, this
 *              function converts the binary buffer into a buffer of
 *              rv_obj_ref_t's which is then placed in the parameter
 *              buf_out.
 *
 *              Note that on the user's side, the buffer is expected to
 *              be an array of rv_obj_ref_t's, each of which has three
 *              fields to be populated. The first field is the reference
 *              type field, which gets set to H5R_OBJECT. The second is
 *              the URI of the object which is referenced and the final
 *              field is the type of the object which is referenced. This
 *              function is responsible for making sure each of those
 *              fields in each struct is setup correctly.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
static herr_t
RV_convert_buffer_to_obj_refs(char *ref_buf, size_t ref_buf_len, rv_obj_ref_t **buf_out, size_t *buf_out_len)
{
    rv_obj_ref_t *out = NULL;
    size_t        i;
    size_t        out_len   = 0;
    herr_t        ret_value = SUCCEED;

#ifdef RV_CONNECTOR_DEBUG
    printf("-> Converting binary buffer to ref. array\n\n");
#endif

    if (!ref_buf)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "reference string buffer was NULL");
    if (!buf_out)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "output buffer was NULL");
    if (!buf_out_len)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "output buffer size pointer was NULL");
    if (!ref_buf_len)
        FUNC_GOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid reference buffer size specified");

    out_len = ref_buf_len * sizeof(rv_obj_ref_t);
    if (NULL == (out = (rv_obj_ref_t *)RV_malloc(out_len)))
        FUNC_GOTO_ERROR(H5E_DATATYPE, H5E_CANTALLOC, FAIL, "can't allocate space for object reference array");

    for (i = 0; i < ref_buf_len; i++) {
        char *URI_start;

        out[i].ref_type = H5R_OBJECT;

        /* As the URI received from the server will have a string
         * prefix like "groups/", "datatypes/" or "datasets/", skip
         * past the prefix in order to get to the real URI.
         */
        URI_start = ref_buf + (i * OBJECT_REF_STRING_LEN);
        while (*URI_start && *URI_start != '/')
            URI_start++;

        /* Handle empty ref data */
        if (!*URI_start) {
            out[i].ref_obj_URI[0] = '\0';
            continue;
        } /* end if */

        URI_start++;

        strncpy(out[i].ref_obj_URI, URI_start, OBJECT_REF_STRING_LEN);

        /* Since the first character of the server's object URIs denotes
         * the type of the object, e.g. 'g' denotes a group object,
         * capture this here.
         */
        if ('g' == *URI_start) {
            out[i].ref_obj_type = H5I_GROUP;
        } /* end if */
        else if ('t' == *URI_start) {
            out[i].ref_obj_type = H5I_DATATYPE;
        } /* end else if */
        else if ('d' == *URI_start) {
            out[i].ref_obj_type = H5I_DATASET;
        } /* end else if */
        else {
            out[i].ref_obj_type = H5I_BADID;
        } /* end else */
    }     /* end for */

done:
    if (ret_value >= 0) {
        *buf_out     = out;
        *buf_out_len = out_len;

#ifdef RV_CONNECTOR_DEBUG
        for (i = 0; i < ref_buf_len; i++) {
            printf("-> Ref_array[%zu]: %s\n", i, out[i].ref_obj_URI);
        } /* end for */
        printf("\n");
#endif
    } /* end if */
    else {
        if (out)
            RV_free(out);
    } /* end else */

    return ret_value;
} /* end RV_convert_buffer_to_obj_refs() */

/*-------------------------------------------------------------------------
 * Function:    dataset_read_scatter_op
 *
 * Purpose:     Callback for H5Dscatter() to scatter the read data into the
 *              supplied buffer
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Jordan Henderson
 *              July, 2017
 */
static herr_t
dataset_read_scatter_op(const void **src_buf, size_t *src_buf_bytes_used, void *op_data)
{
    *src_buf            = response_buffer.buffer;
    *src_buf_bytes_used = *((size_t *)op_data);

    return 0;
} /* end dataset_read_scatter_op() */
