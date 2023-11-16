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
 *              March 2017
 *
 * Purpose: Tests the REST VOL connector
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hdf5.h"

#include "../src/util/rest_vol_err.h"
#include "rest_vol_public.h"
#include "rest_vol_config.h"

#define TEST_DIR_PREFIX "/home"
#define TEST_FILE_NAME  "test_file"

char *username;

/* The name of the file that all of the tests will operate on */
#define FILENAME_MAX_LENGTH 1024
char filename[FILENAME_MAX_LENGTH];

#define ARRAY_LENGTH(array) sizeof(array) / sizeof(array[0])

/* Comment out to allow large tests (e.g. ones that allocate
 * gigabytes of memory) to run
 */
#define NO_LARGE_TESTS

/* The maximum level of recursion that the generate_random_datatype()
 * function should go down to, before being forced to choose a base type
 * in order to not cause a stack overflow
 */
#define RECURSION_MAX_DEPTH 3

/* The maximum number of members allowed in an HDF5 compound type,
 * for ease of development
 */
#define COMPOUND_TYPE_MAX_MEMBERS 4

/* The maximum number and size of the dimensions of an HDF5 array
 * datatype
 */
#define ARRAY_TYPE_MAX_DIMS 4

/* The maximum size of an HDF5 string datatype, as created by the
 * generate_random_datatype() function
 */
#define STRING_TYPE_MAX_SIZE 1024

/* The maximum number of members and the maximum size of those
 * members' names for an HDF5 enum type
 */
#define ENUM_TYPE_MAX_MEMBER_NAME_LENGTH 256
#define ENUM_TYPE_MAX_MEMBERS            16

/* The maximum size of a dimension in an HDF5 dataspace as allowed
 * for this testing suite so as not to try to create too large
 * of a dataspace/datatype */
#define MAX_DIM_SIZE 16

/* The names of a set of container groups which hold objects
 * created by each of the different types of tests
 */
#define GROUP_TEST_GROUP_NAME         "group_tests"
#define ATTRIBUTE_TEST_GROUP_NAME     "attribute_tests"
#define DATASET_TEST_GROUP_NAME       "dataset_tests"
#define DATATYPE_TEST_GROUP_NAME      "datatype_tests"
#define LINK_TEST_GROUP_NAME          "link_tests"
#define OBJECT_TEST_GROUP_NAME        "object_tests"
#define MISCELLANEOUS_TEST_GROUP_NAME "miscellaneous_tests"

/*****************************************************
 *                                                   *
 *             Plugin File test defines              *
 *                                                   *
 *****************************************************/

#define FILE_INTENT_TEST_DATASETNAME "/test_dset"
#define FILE_INTENT_TEST_DSET_RANK   2
#define FILE_INTENT_TEST_FILENAME    "intent_test_file"

#define NONEXISTENT_FILENAME "nonexistent_file"

#define FILE_PROPERTY_LIST_TEST_FNAME1 "property_list_test_file1"
#define FILE_PROPERTY_LIST_TEST_FNAME2 "property_list_test_file2"

/*****************************************************
 *                                                   *
 *             Plugin Group test defines             *
 *                                                   *
 *****************************************************/

#define GROUP_CREATE_INVALID_LOC_ID_GNAME "/test_group"

#define GROUP_CREATE_UNDER_ROOT_GNAME "/group_under_root"

#define GROUP_CREATE_UNDER_GROUP_REL_GNAME "group_under_group2"

#define GROUP_CREATE_ANONYMOUS_GROUP_NAME "anon_group"

#define NONEXISTENT_GROUP_TEST_GNAME "/nonexistent_group"

#define GROUP_PROPERTY_LIST_TEST_GROUP_NAME1 "property_list_test_group1"
#define GROUP_PROPERTY_LIST_TEST_GROUP_NAME2 "property_list_test_group2"
#define GROUP_PROPERTY_LIST_TEST_DUMMY_VAL   100

/*****************************************************
 *                                                   *
 *           Plugin Attribute test defines           *
 *                                                   *
 *****************************************************/

#define ATTRIBUTE_CREATE_ON_ROOT_SPACE_RANK 2
#define ATTRIBUTE_CREATE_ON_ROOT_ATTR_NAME  "attr_on_root"
#define ATTRIBUTE_CREATE_ON_ROOT_ATTR_NAME2 "attr_on_root2"

#define ATTRIBUTE_CREATE_ON_DATASET_DSET_SPACE_RANK 2
#define ATTRIBUTE_CREATE_ON_DATASET_ATTR_SPACE_RANK 2
#define ATTRIBUTE_CREATE_ON_DATASET_DSET_NAME       "dataset_with_attr"
#define ATTRIBUTE_CREATE_ON_DATASET_ATTR_NAME       "attr_on_dataset"
#define ATTRIBUTE_CREATE_ON_DATASET_ATTR_NAME2      "attr_on_dataset2"

#define ATTRIBUTE_CREATE_ON_DATATYPE_SPACE_RANK 2
#define ATTRIBUTE_CREATE_ON_DATATYPE_DTYPE_NAME "datatype_with_attr"
#define ATTRIBUTE_CREATE_ON_DATATYPE_ATTR_NAME  "attr_on_datatype"
#define ATTRIBUTE_CREATE_ON_DATATYPE_ATTR_NAME2 "attr_on_datatype2"

#define ATTRIBUTE_CREATE_NULL_DATASPACE_TEST_SUBGROUP_NAME "attr_with_null_space_test"
#define ATTRIBUTE_CREATE_NULL_DATASPACE_TEST_ATTR_NAME     "attr_with_null_space"

#define ATTRIBUTE_CREATE_SCALAR_DATASPACE_TEST_SUBGROUP_NAME "attr_with_scalar_space_test"
#define ATTRIBUTE_CREATE_SCALAR_DATASPACE_TEST_ATTR_NAME     "attr_with_scalar_space"

#define ATTRIBUTE_GET_INFO_TEST_SPACE_RANK 2
#define ATTRIBUTE_GET_INFO_TEST_ATTR_NAME  "get_info_test_attr"

#define ATTRIBUTE_GET_SPACE_TYPE_TEST_SPACE_RANK 2
#define ATTRIBUTE_GET_SPACE_TYPE_TEST_ATTR_NAME  "get_space_type_test_attr"

#define ATTRIBUTE_GET_NAME_TEST_ATTRIBUTE_NAME "retrieve_attr_name_test"
#define ATTRIBUTE_GET_NAME_TEST_SPACE_RANK     2

#define ATTRIBUTE_CREATE_WITH_SPACE_IN_NAME_SPACE_RANK 2
#define ATTRIBUTE_CREATE_WITH_SPACE_IN_NAME_ATTR_NAME  "attr with space in name"

#define ATTRIBUTE_DELETION_TEST_SPACE_RANK 2
#define ATTRIBUTE_DELETION_TEST_ATTR_NAME  "attr_to_be_deleted"

#define ATTRIBUTE_WRITE_TEST_ATTR_DTYPE_SIZE sizeof(int)
#define ATTRIBUTE_WRITE_TEST_ATTR_DTYPE      H5T_NATIVE_INT
#define ATTRIBUTE_WRITE_TEST_SPACE_RANK      2
#define ATTRIBUTE_WRITE_TEST_ATTR_NAME       "write_test_attr"

#define ATTRIBUTE_READ_TEST_ATTR_DTYPE_SIZE sizeof(int)
#define ATTRIBUTE_READ_TEST_ATTR_DTYPE      H5T_NATIVE_INT
#define ATTRIBUTE_READ_TEST_SPACE_RANK      2
#define ATTRIBUTE_READ_TEST_ATTR_NAME       "read_test_attr"

#define ATTRIBUTE_GET_NUM_ATTRS_TEST_ATTRIBUTE_NAME "get_num_attrs_test_attribute"
#define ATTRIBUTE_GET_NUM_ATTRS_TEST_SPACE_RANK     2

#define ATTRIBUTE_ITERATE_TEST_DSET_SPACE_RANK 2
#define ATTRIBUTE_ITERATE_TEST_ATTR_SPACE_RANK 2
#define ATTRIBUTE_ITERATE_TEST_SUBGROUP_NAME   "attribute_iterate_test"
#define ATTRIBUTE_ITERATE_TEST_DSET_NAME       "attribute_iterate_dset"
#define ATTRIBUTE_ITERATE_TEST_ATTR_NAME       "iter_attr1"
#define ATTRIBUTE_ITERATE_TEST_ATTR_NAME2      "iter_attr2"
#define ATTRIBUTE_ITERATE_TEST_ATTR_NAME3      "iter_attr3"
#define ATTRIBUTE_ITERATE_TEST_ATTR_NAME4      "iter_attr4"

#define ATTRIBUTE_ITERATE_TEST_0_ATTRIBUTES_DSET_SPACE_RANK 2
#define ATTRIBUTE_ITERATE_TEST_0_ATTRIBUTES_SUBGROUP_NAME   "attribute_iterate_test_0_attributes"
#define ATTRIBUTE_ITERATE_TEST_0_ATTRIBUTES_DSET_NAME       "attribute_iterate_dset"

#define ATTRIBUTE_UNUSED_APIS_TEST_SPACE_RANK 2
#define ATTRIBUTE_UNUSED_APIS_TEST_ATTR_NAME  "unused_apis_attr"

#define ATTRIBUTE_PROPERTY_LIST_TEST_ATTRIBUTE_NAME1 "property_list_test_attribute1"
#define ATTRIBUTE_PROPERTY_LIST_TEST_ATTRIBUTE_NAME2 "property_list_test_attribute2"
#define ATTRIBUTE_PROPERTY_LIST_TEST_SUBGROUP_NAME   "attribute_property_list_test_group"
#define ATTRIBUTE_PROPERTY_LIST_TEST_SPACE_RANK      2

/*****************************************************
 *                                                   *
 *            Plugin Dataset test defines            *
 *                                                   *
 *****************************************************/

#define DATASET_CREATE_UNDER_ROOT_DSET_NAME  "/dset_under_root"
#define DATASET_CREATE_UNDER_ROOT_SPACE_RANK 2

#define DATASET_CREATE_ANONYMOUS_DATASET_NAME "anon_dset"
#define DATASET_CREATE_ANONYMOUS_SPACE_RANK   2

#define DATASET_CREATE_UNDER_EXISTING_SPACE_RANK 2
#define DATASET_CREATE_UNDER_EXISTING_DSET_NAME  "nested_dset"

#define DATASET_CREATE_NULL_DATASPACE_TEST_SUBGROUP_NAME "dataset_with_null_space_test"
#define DATASET_CREATE_NULL_DATASPACE_TEST_DSET_NAME     "dataset_with_null_space"

#define DATASET_CREATE_SCALAR_DATASPACE_TEST_SUBGROUP_NAME "dataset_with_scalar_space_test"
#define DATASET_CREATE_SCALAR_DATASPACE_TEST_DSET_NAME     "dataset_with_scalar_space"

/* Defines for testing the connector's ability to parse different types
 * of Datatypes for Dataset creation
 */
#define DATASET_PREDEFINED_TYPE_TEST_SPACE_RANK    2
#define DATASET_PREDEFINED_TYPE_TEST_BASE_NAME     "predefined_type_dset"
#define DATASET_PREDEFINED_TYPE_TEST_SUBGROUP_NAME "predefined_type_dataset_test"

#define DATASET_STRING_TYPE_TEST_STRING_LENGTH 40
#define DATASET_STRING_TYPE_TEST_SPACE_RANK    2
#define DATASET_STRING_TYPE_TEST_DSET_NAME1    "fixed_length_string_dset"
#define DATASET_STRING_TYPE_TEST_DSET_NAME2    "variable_length_string_dset"
#define DATASET_STRING_TYPE_TEST_SUBGROUP_NAME "string_type_dataset_test"

#define DATASET_ENUM_TYPE_TEST_VAL_BASE_NAME "INDEX"
#define DATASET_ENUM_TYPE_TEST_SUBGROUP_NAME "enum_type_dataset_test"
#define DATASET_ENUM_TYPE_TEST_NUM_MEMBERS   16
#define DATASET_ENUM_TYPE_TEST_SPACE_RANK    2
#define DATASET_ENUM_TYPE_TEST_DSET_NAME1    "enum_native_dset"
#define DATASET_ENUM_TYPE_TEST_DSET_NAME2    "enum_non_native_dset"

#define DATASET_ARRAY_TYPE_TEST_SUBGROUP_NAME "array_type_dataset_test"
#define DATASET_ARRAY_TYPE_TEST_DSET_NAME1    "array_type_test1"
#define DATASET_ARRAY_TYPE_TEST_DSET_NAME2    "array_type_test2"
#if 0
#define DATASET_ARRAY_TYPE_TEST_DSET_NAME3 "array_type_test3"
#endif
#define DATASET_ARRAY_TYPE_TEST_SPACE_RANK 2
#define DATASET_ARRAY_TYPE_TEST_RANK1      2
#define DATASET_ARRAY_TYPE_TEST_RANK2      2
#if 0
#define DATASET_ARRAY_TYPE_TEST_RANK3 2
#endif

#define DATASET_COMPOUND_TYPE_TEST_SUBGROUP_NAME "compound_type_dataset_test"
#define DATASET_COMPOUND_TYPE_TEST_DSET_NAME     "compound_type_test"
#define DATASET_COMPOUND_TYPE_TEST_MAX_SUBTYPES  10
#define DATASET_COMPOUND_TYPE_TEST_MAX_PASSES    5
#define DATASET_COMPOUND_TYPE_TEST_DSET_RANK     2

/* Defines for testing the connector's ability to parse different
 * Dataset shapes for creation
 */
#define DATASET_SHAPE_TEST_DSET_BASE_NAME "dataset_shape_test"
#define DATASET_SHAPE_TEST_SUBGROUP_NAME  "dataset_shape_test"
#define DATASET_SHAPE_TEST_NUM_ITERATIONS 5
#define DATASET_SHAPE_TEST_MAX_DIMS       32

#define DATASET_CREATION_PROPERTIES_TEST_TRACK_TIMES_YES_DSET_NAME "track_times_true_test"
#define DATASET_CREATION_PROPERTIES_TEST_TRACK_TIMES_NO_DSET_NAME  "track_times_false_test"
#define DATASET_CREATION_PROPERTIES_TEST_PHASE_CHANGE_DSET_NAME    "attr_phase_change_test"
#define DATASET_CREATION_PROPERTIES_TEST_ALLOC_TIMES_BASE_NAME     "alloc_time_test"
#define DATASET_CREATION_PROPERTIES_TEST_FILL_TIMES_BASE_NAME      "fill_times_test"
#define DATASET_CREATION_PROPERTIES_TEST_CRT_ORDER_BASE_NAME       "creation_order_test"
#define DATASET_CREATION_PROPERTIES_TEST_LAYOUTS_BASE_NAME         "layout_test"
#define DATASET_CREATION_PROPERTIES_TEST_FILTERS_DSET_NAME         "filters_test"
#define DATASET_CREATION_PROPERTIES_TEST_GROUP_NAME                "creation_properties_test"
#define DATASET_CREATION_PROPERTIES_TEST_CHUNK_DIM_RANK            DATASET_CREATION_PROPERTIES_TEST_SHAPE_RANK
#define DATASET_CREATION_PROPERTIES_TEST_MAX_COMPACT               12
#define DATASET_CREATION_PROPERTIES_TEST_MIN_DENSE                 8
#define DATASET_CREATION_PROPERTIES_TEST_SHAPE_RANK                3

#define DATASET_SMALL_WRITE_TEST_ALL_DSET_SPACE_RANK 3
#define DATASET_SMALL_WRITE_TEST_ALL_DSET_DTYPESIZE  sizeof(int)
#define DATASET_SMALL_WRITE_TEST_ALL_DSET_DTYPE      H5T_NATIVE_INT
#define DATASET_SMALL_WRITE_TEST_ALL_DSET_NAME       "dataset_write_small_all"

#define DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK 3
#define DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_DTYPESIZE  sizeof(int)
#define DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_DTYPE      H5T_NATIVE_INT
#define DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_NAME       "dataset_write_small_hyperslab"

#define DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_SPACE_RANK 3
#define DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_DTYPESIZE  sizeof(int)
#define DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_DTYPE      H5T_NATIVE_INT
#define DATASET_SMALL_WRITE_TEST_POINT_SELECTION_NUM_POINTS      10
#define DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_NAME       "dataset_write_small_point_selection"

#ifndef NO_LARGE_TESTS
#define DATASET_LARGE_WRITE_TEST_ALL_DSET_SPACE_RANK 3
#define DATASET_LARGE_WRITE_TEST_ALL_DSET_DTYPESIZE  sizeof(int)
#define DATASET_LARGE_WRITE_TEST_ALL_DSET_DTYPE      H5T_NATIVE_INT
#define DATASET_LARGE_WRITE_TEST_ALL_DSET_NAME       "dataset_write_large_all"

#define DATASET_LARGE_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK 3
#define DATASET_LARGE_WRITE_TEST_HYPERSLAB_DSET_DTYPESIZE  sizeof(int)
#define DATASET_LARGE_WRITE_TEST_HYPERSLAB_DSET_DTYPE      H5T_NATIVE_INT
#define DATASET_LARGE_WRITE_TEST_HYPERSLAB_DSET_NAME       "dataset_write_large_hyperslab"

#define DATASET_LARGE_WRITE_TEST_POINT_SELECTION_DSET_SPACE_RANK 3
#define DATASET_LARGE_WRITE_TEST_POINT_SELECTION_DSET_DTYPESIZE  sizeof(int)
#define DATASET_LARGE_WRITE_TEST_POINT_SELECTION_DSET_DTYPE      H5T_NATIVE_INT
#define DATASET_LARGE_WRITE_TEST_POINT_SELECTION_DSET_NAME       "dataset_write_large_point_selection"
#endif

#define DATASET_SMALL_READ_TEST_ALL_DSET_SPACE_RANK 3
#define DATASET_SMALL_READ_TEST_ALL_DSET_DTYPESIZE  sizeof(int)
#define DATASET_SMALL_READ_TEST_ALL_DSET_DTYPE      H5T_NATIVE_INT
#define DATASET_SMALL_READ_TEST_ALL_DSET_NAME       "dataset_read_small_all"

#define DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_SPACE_RANK 3
#define DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_DTYPESIZE  sizeof(int)
#define DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_DTYPE      H5T_NATIVE_INT
#define DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_NAME       "dataset_read_small_hyperslab"

#define DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_SPACE_RANK 3
#define DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_DTYPESIZE  sizeof(int)
#define DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_DTYPE      H5T_NATIVE_INT
#define DATASET_SMALL_READ_TEST_POINT_SELECTION_NUM_POINTS      10
#define DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_NAME       "dataset_read_small_point_selection"

#ifndef NO_LARGE_TESTS
#define DATASET_LARGE_READ_TEST_ALL_DSET_SPACE_RANK 3
#define DATASET_LARGE_READ_TEST_ALL_DSET_DTYPESIZE  sizeof(int)
#define DATASET_LARGE_READ_TEST_ALL_DSET_DTYPE      H5T_NATIVE_INT
#define DATASET_LARGE_READ_TEST_ALL_DSET_NAME       "dataset_read_large_all"

#define DATASET_LARGE_READ_TEST_HYPERSLAB_DSET_SPACE_RANK 3
#define DATASET_LARGE_READ_TEST_HYPERSLAB_DSET_DTYPESIZE  sizeof(int)
#define DATASET_LARGE_READ_TEST_HYPERSLAB_DSET_DTYPE      H5T_NATIVE_INT
#define DATASET_LARGE_READ_TEST_HYPERSLAB_DSET_NAME       "dataset_read_large_hyperslab"

#define DATASET_LARGE_READ_TEST_POINT_SELECTION_DSET_SPACE_RANK 3
#define DATASET_LARGE_READ_TEST_POINT_SELECTION_DSET_DTYPESIZE  sizeof(int)
#define DATASET_LARGE_READ_TEST_POINT_SELECTION_DSET_DTYPE      H5T_NATIVE_INT
#define DATASET_LARGE_READ_TEST_POINT_SELECTION_DSET_NAME       "dataset_read_large_point_selection"
#endif

#define DATASET_DATA_VERIFY_WRITE_TEST_DSET_SPACE_RANK 3
#define DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE  sizeof(int)
#define DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPE      H5T_NATIVE_INT
#define DATASET_DATA_VERIFY_WRITE_TEST_NUM_POINTS      10
#define DATASET_DATA_VERIFY_WRITE_TEST_DSET_NAME       "dataset_data_verification"

#define DATASET_SET_EXTENT_TEST_SPACE_RANK 2
#define DATASET_SET_EXTENT_TEST_DSET_NAME  "set_extent_test_dset"

#define DATASET_UNUSED_APIS_TEST_SPACE_RANK 2
#define DATASET_UNUSED_APIS_TEST_DSET_NAME  "unused_apis_dset"

#define DATASET_PROPERTY_LIST_TEST_SUBGROUP_NAME "dataset_property_list_test_group"
#define DATASET_PROPERTY_LIST_TEST_SPACE_RANK    2
#define DATASET_PROPERTY_LIST_TEST_DSET_NAME1    "property_list_test_dataset1"
#define DATASET_PROPERTY_LIST_TEST_DSET_NAME2    "property_list_test_dataset2"
#define DATASET_PROPERTY_LIST_TEST_DSET_NAME3    "property_list_test_dataset3"
#define DATASET_PROPERTY_LIST_TEST_DSET_NAME4    "property_list_test_dataset4"

/*****************************************************
 *                                                   *
 *           Plugin Datatype test defines            *
 *                                                   *
 *****************************************************/

#define DATATYPE_CREATE_TEST_DATASET_DIMS 2

#define DATATYPE_CREATE_TEST_TYPE_NAME "test_type"

#define DATATYPE_CREATE_ANONYMOUS_TYPE_NAME "anon_type"

#define DATASET_CREATE_WITH_DATATYPE_TEST_DATASET_DIMS 2
#define DATASET_CREATE_WITH_DATATYPE_TEST_TYPE_NAME    "committed_type_test_dtype1"
#define DATASET_CREATE_WITH_DATATYPE_TEST_DSET_NAME    "committed_type_test_dset"

#define ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_SPACE_RANK 2
#define ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_DTYPE_NAME "committed_type_test_dtype2"
#define ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_ATTR_NAME  "committed_type_test_attr"

#define DATATYPE_DELETE_TEST_DTYPE_NAME "delete_test_dtype"

#define DATATYPE_PROPERTY_LIST_TEST_SUBGROUP_NAME  "datatype_property_list_test_group"
#define DATATYPE_PROPERTY_LIST_TEST_DATATYPE_NAME1 "property_list_test_datatype1"
#define DATATYPE_PROPERTY_LIST_TEST_DATATYPE_NAME2 "property_list_test_datatype2"

/*****************************************************
 *                                                   *
 *             Plugin Link test defines              *
 *                                                   *
 *****************************************************/

#define HARD_LINK_TEST_LINK_NAME "test_link"

#define H5L_SAME_LOC_TEST_DSET_SPACE_RANK 2
#define H5L_SAME_LOC_TEST_GROUP_NAME      "h5l_same_loc_test_group"
#define H5L_SAME_LOC_TEST_LINK_NAME1      "h5l_same_loc_test_link1"
#define H5L_SAME_LOC_TEST_LINK_NAME2      "h5l_same_loc_test_link2"
#define H5L_SAME_LOC_TEST_DSET_NAME       "h5l_same_loc_test_dset"

#define SOFT_LINK_EXISTING_RELATIVE_TEST_DSET_SPACE_RANK 2
#define SOFT_LINK_EXISTING_RELATIVE_TEST_SUBGROUP_NAME   "soft_link_to_existing_relative_path_test"
#define SOFT_LINK_EXISTING_RELATIVE_TEST_DSET_NAME       "dset"
#define SOFT_LINK_EXISTING_RELATIVE_TEST_LINK_NAME       "soft_link_to_existing_relative_path"

#define SOFT_LINK_EXISTING_ABSOLUTE_TEST_SUBGROUP_NAME "soft_link_to_existing_absolute_path_test"
#define SOFT_LINK_EXISTING_ABSOLUTE_TEST_LINK_NAME     "soft_link_to_existing_absolute_path"

#define SOFT_LINK_DANGLING_RELATIVE_TEST_DSET_SPACE_RANK 2
#define SOFT_LINK_DANGLING_RELATIVE_TEST_SUBGROUP_NAME   "soft_link_dangling_relative_path_test"
#define SOFT_LINK_DANGLING_RELATIVE_TEST_DSET_NAME       "dset"
#define SOFT_LINK_DANGLING_RELATIVE_TEST_LINK_NAME       "soft_link_dangling_relative_path"

#define SOFT_LINK_DANGLING_ABSOLUTE_TEST_DSET_SPACE_RANK 2
#define SOFT_LINK_DANGLING_ABSOLUTE_TEST_SUBGROUP_NAME   "soft_link_dangling_absolute_path_test"
#define SOFT_LINK_DANGLING_ABSOLUTE_TEST_DSET_NAME       "dset"
#define SOFT_LINK_DANGLING_ABSOLUTE_TEST_LINK_NAME       "soft_link_dangling_absolute_path"

#define EXTERNAL_LINK_TEST_SUBGROUP_NAME "external_link_test"
#define EXTERNAL_LINK_TEST_FILE_NAME     "ext_link_file"
#define EXTERNAL_LINK_TEST_LINK_NAME     "ext_link"

#define EXTERNAL_LINK_TEST_DANGLING_DSET_SPACE_RANK 2
#define EXTERNAL_LINK_TEST_DANGLING_SUBGROUP_NAME   "external_link_dangling_test"
#define EXTERNAL_LINK_TEST_DANGLING_LINK_NAME       "dangling_ext_link"
#define EXTERNAL_LINK_TEST_DANGLING_DSET_NAME       "external_dataset"

#define UD_LINK_TEST_UDATA_MAX_SIZE 256
#define UD_LINK_TEST_LINK_NAME      "ud_link"

#define LINK_DELETE_TEST_DSET_SPACE_RANK     2
#define LINK_DELETE_TEST_EXTERNAL_LINK_NAME  "external_link"
#define LINK_DELETE_TEST_EXTERNAL_LINK_NAME2 "external_link2"
#define LINK_DELETE_TEST_SOFT_LINK_NAME      "soft_link"
#define LINK_DELETE_TEST_SOFT_LINK_NAME2     "soft_link2"
#define LINK_DELETE_TEST_SUBGROUP_NAME       "link_delete_test"
#define LINK_DELETE_TEST_DSET_NAME1          "link_delete_test_dset1"
#define LINK_DELETE_TEST_DSET_NAME2          "link_delete_test_dset2"

#define COPY_LINK_TEST_SOFT_LINK_TARGET_PATH                                                                 \
    "/" LINK_TEST_GROUP_NAME "/" COPY_LINK_TEST_GROUP_NAME "/" COPY_LINK_TEST_DSET_NAME
#define COPY_LINK_TEST_HARD_LINK_COPY_NAME "hard_link_to_dset_copy"
#define COPY_LINK_TEST_SOFT_LINK_COPY_NAME "soft_link_to_dset_copy"
#define COPY_LINK_TEST_HARD_LINK_NAME      "hard_link_to_dset"
#define COPY_LINK_TEST_SOFT_LINK_NAME      "soft_link_to_dset"
#define COPY_LINK_TEST_GROUP_NAME          "link_copy_test_group"
#define COPY_LINK_TEST_DSET_NAME           "link_copy_test_dset"
#define COPY_LINK_TEST_DSET_SPACE_RANK     2

#define MOVE_LINK_TEST_SOFT_LINK_TARGET_PATH                                                                 \
    "/" LINK_TEST_GROUP_NAME "/" MOVE_LINK_TEST_GROUP_NAME "/" MOVE_LINK_TEST_DSET_NAME
#define MOVE_LINK_TEST_HARD_LINK_NAME  "hard_link_to_dset"
#define MOVE_LINK_TEST_SOFT_LINK_NAME  "soft_link_to_dset"
#define MOVE_LINK_TEST_GROUP_NAME      "link_move_test_group"
#define MOVE_LINK_TEST_DSET_NAME       "link_move_test_dset"
#define MOVE_LINK_TEST_DSET_SPACE_RANK 2

#define GET_LINK_INFO_TEST_DSET_SPACE_RANK 2
#define GET_LINK_INFO_TEST_SUBGROUP_NAME   "get_link_info_test"
#define GET_LINK_INFO_TEST_SOFT_LINK_NAME  "soft_link"
#define GET_LINK_INFO_TEST_EXT_LINK_NAME   "ext_link"
#define GET_LINK_INFO_TEST_DSET_NAME       "get_link_info_dset"

#define GET_LINK_NAME_BY_IDX_TEST_MAX_LINK_NAME_LENGTH 256
#define GET_LINK_NAME_BY_IDX_TEST_DSET_SPACE_RANK      2
#define GET_LINK_NAME_BY_IDX_TEST_SUBGROUP_NAME        "get_link_name_by_idx_test"
#define GET_LINK_NAME_BY_IDX_TEST_DSET_NAME            "get_link_name_by_idx_dset"
#define GET_LINK_NAME_BY_IDX_TEST_NUM_LINKS            10
#define GET_LINK_NAME_BY_IDX_TEST_FIRST_LINK_IDX       4
#define GET_LINK_NAME_BY_IDX_TEST_FIRST_LINK_NAME      "link4"
#define GET_LINK_NAME_BY_IDX_TEST_SECOND_LINK_IDX      2
#define GET_LINK_NAME_BY_IDX_TEST_SECOND_LINK_NAME     "link7"
#define GET_LINK_NAME_BY_IDX_TEST_THIRD_LINK_IDX       8
#define GET_LINK_NAME_BY_IDX_TEST_THIRD_LINK_NAME      "link1"
#define GET_LINK_NAME_BY_IDX_TEST_FOURTH_LINK_IDX      2
#define GET_LINK_NAME_BY_IDX_TEST_FOURTH_LINK_NAME     "link2"

#define GET_LINK_VAL_TEST_SUBGROUP_NAME  "get_link_val_test"
#define GET_LINK_VAL_TEST_SOFT_LINK_NAME "soft_link"
#define GET_LINK_VAL_TEST_EXT_LINK_NAME  "ext_link"

#define LINK_ITER_TEST_DSET_SPACE_RANK 2
#define LINK_ITER_TEST_HARD_LINK_NAME  "link_iter_test_dset"
#define LINK_ITER_TEST_SOFT_LINK_NAME  "soft_link1"
#define LINK_ITER_TEST_EXT_LINK_NAME   "ext_link1"
#define LINK_ITER_TEST_SUBGROUP_NAME   "link_iter_test"
#define LINK_ITER_TEST_NUM_LINKS       3

#define LINK_ITER_TEST_0_LINKS_SUBGROUP_NAME "link_iter_test_0_links"

#define LINK_VISIT_TEST_NO_CYCLE_DSET_SPACE_RANK 2
#define LINK_VISIT_TEST_NO_CYCLE_DSET_NAME       "dset"
#define LINK_VISIT_TEST_NO_CYCLE_SUBGROUP_NAME   "link_visit_test_no_cycles"
#define LINK_VISIT_TEST_NO_CYCLE_SUBGROUP_NAME2  "link_visit_subgroup1"
#define LINK_VISIT_TEST_NO_CYCLE_SUBGROUP_NAME3  "link_visit_subgroup2"
#define LINK_VISIT_TEST_NO_CYCLE_LINK_NAME1      "hard_link1"
#define LINK_VISIT_TEST_NO_CYCLE_LINK_NAME2      "soft_link1"
#define LINK_VISIT_TEST_NO_CYCLE_LINK_NAME3      "ext_link1"
#define LINK_VISIT_TEST_NO_CYCLE_LINK_NAME4      "hard_link2"

#define LINK_VISIT_TEST_CYCLE_SUBGROUP_NAME  "link_visit_test_cycles"
#define LINK_VISIT_TEST_CYCLE_SUBGROUP_NAME2 "link_visit_subgroup1"
#define LINK_VISIT_TEST_CYCLE_SUBGROUP_NAME3 "link_visit_subgroup2"
#define LINK_VISIT_TEST_CYCLE_LINK_NAME1     "hard_link1"
#define LINK_VISIT_TEST_CYCLE_LINK_NAME2     "soft_link1"
#define LINK_VISIT_TEST_CYCLE_LINK_NAME3     "ext_link1"
#define LINK_VISIT_TEST_CYCLE_LINK_NAME4     "hard_link2"

#define LINK_VISIT_TEST_0_LINKS_SUBGROUP_NAME  "link_visit_test_0_links"
#define LINK_VISIT_TEST_0_LINKS_SUBGROUP_NAME2 "link_visit_test_0_links_subgroup1"
#define LINK_VISIT_TEST_0_LINKS_SUBGROUP_NAME3 "link_visit_test_0_links_subgroup2"

/*****************************************************
 *                                                   *
 *            Plugin Object test defines             *
 *                                                   *
 *****************************************************/

#define GENERIC_DATASET_OPEN_TEST_SPACE_RANK 2
#define GENERIC_DATASET_OPEN_TEST_DSET_NAME  "generic_dataset_open_test"

#define GENERIC_GROUP_OPEN_TEST_GROUP_NAME "generic_group_open_test"

#define GENERIC_DATATYPE_OPEN_TEST_TYPE_NAME "generic_datatype_open_test"

#define OBJECT_EXISTS_TEST_DSET_SPACE_RANK 2
#define OBJECT_EXISTS_TEST_SUBGROUP_NAME   "h5o_exists_by_name_test"
#define OBJECT_EXISTS_TEST_DTYPE_NAME      "h5o_exists_by_name_dtype"
#define OBJECT_EXISTS_TEST_DSET_NAME       "h5o_exists_by_name_dset"

#define OBJECT_COPY_TEST_SUBGROUP_NAME "object_copy_test"
#define OBJECT_COPY_TEST_SPACE_RANK    2
#define OBJECT_COPY_TEST_DSET_DTYPE    H5T_NATIVE_INT
#define OBJECT_COPY_TEST_DSET_NAME     "dset"
#define OBJECT_COPY_TEST_DSET_NAME2    "dset_copy"

#define H5O_CLOSE_TEST_SPACE_RANK 2
#define H5O_CLOSE_TEST_DSET_NAME  "h5o_close_test_dset"
#define H5O_CLOSE_TEST_TYPE_NAME  "h5o_close_test_type"

#define OBJ_REF_GET_TYPE_TEST_SUBGROUP_NAME "obj_ref_get_obj_type_test"
#define OBJ_REF_GET_TYPE_TEST_DSET_NAME     "ref_dset"
#define OBJ_REF_GET_TYPE_TEST_TYPE_NAME     "ref_dtype"
#define OBJ_REF_GET_TYPE_TEST_SPACE_RANK    2

#define OBJ_REF_DATASET_WRITE_TEST_SUBGROUP_NAME "obj_ref_write_test"
#define OBJ_REF_DATASET_WRITE_TEST_REF_DSET_NAME "ref_dset"
#define OBJ_REF_DATASET_WRITE_TEST_REF_TYPE_NAME "ref_dtype"
#define OBJ_REF_DATASET_WRITE_TEST_SPACE_RANK    1
#define OBJ_REF_DATASET_WRITE_TEST_DSET_NAME     "obj_ref_dset"

#define OBJ_REF_DATASET_READ_TEST_SUBGROUP_NAME "obj_ref_read_test"
#define OBJ_REF_DATASET_READ_TEST_REF_DSET_NAME "ref_dset"
#define OBJ_REF_DATASET_READ_TEST_REF_TYPE_NAME "ref_dtype"
#define OBJ_REF_DATASET_READ_TEST_SPACE_RANK    1
#define OBJ_REF_DATASET_READ_TEST_DSET_NAME     "obj_ref_dset"

#define OBJ_REF_DATASET_EMPTY_WRITE_TEST_SUBGROUP_NAME "obj_ref_empty_write_test"
#define OBJ_REF_DATASET_EMPTY_WRITE_TEST_SPACE_RANK    1
#define OBJ_REF_DATASET_EMPTY_WRITE_TEST_DSET_NAME     "obj_ref_dset"

/*****************************************************
 *                                                   *
 *         Plugin Miscellaneous test defines         *
 *                                                   *
 *****************************************************/

#define OPEN_LINK_WITHOUT_SLASH_DSET_SPACE_RANK 2
#define OPEN_LINK_WITHOUT_SLASH_DSET_NAME       "link_without_slash_test_dset"

#define OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_CONTAINER_GROUP_NAME "absolute_path_test_container_group"
#define OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_SUBGROUP_NAME        "absolute_path_test_subgroup"
#define OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DTYPE_NAME           "absolute_path_test_dtype"
#define OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DSET_NAME            "absolute_path_test_dset"
#define OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DSET_SPACE_RANK      3

#define ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME "absolute_vs_relative_test_container_group"
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET1_NAME           "absolute_vs_relative_test_dset1"
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET2_NAME           "absolute_vs_relative_test_dset2"
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET3_NAME           "absolute_vs_relative_test_dset3"
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET4_NAME           "absolute_vs_relative_test_dset4"
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET5_NAME           "absolute_vs_relative_test_dset5"
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET6_NAME           "absolute_vs_relative_test_dset6"
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET_SPACE_RANK      3

#define URL_ENCODING_TEST_SPACE_RANK 2
#define URL_ENCODING_TEST_GROUP_NAME "url_encoding_group !*'():@&=+$,?#[]-.<>\\\\^`{}|~"
#define URL_ENCODING_TEST_DSET_NAME  "url_encoding_dset !*'():@&=+$,?#[]-.<>\\\\^`{}|~"
#define URL_ENCODING_TEST_ATTR_NAME  "url_encoding_attr !*'():@&=+$,?#[]-.<>\\\\^`{}|~"

#define COMPOUND_WITH_SYMBOLS_IN_MEMBER_NAMES_TEST_SUBGROUP_NAME                                             \
    "compound_type_with_symbols_in_member_names_test"
#define COMPOUND_WITH_SYMBOLS_IN_MEMBER_NAMES_TEST_NUM_SUBTYPES 9
#define COMPOUND_WITH_SYMBOLS_IN_MEMBER_NAMES_TEST_DSET_RANK    2
#define COMPOUND_WITH_SYMBOLS_IN_MEMBER_NAMES_TEST_DSET_NAME    "dset"

/* Connector initialization/termination test */
static int test_setup_connector(void);

/* File interface tests */
static int test_create_file(void);
static int test_get_file_info(void);
static int test_nonexistent_file(void);
static int test_get_file_intent(void);
static int test_get_file_name(void);
static int test_file_reopen(void);
static int test_unused_file_API_calls(void);
static int test_file_property_lists(void);

/* Group interface tests */
static int test_create_group_invalid_loc_id(void);
static int test_create_group_under_root(void);
static int test_create_group_under_existing_group(void);
static int test_create_anonymous_group(void);
static int test_get_group_info(void);
static int test_nonexistent_group(void);
static int test_unused_group_API_calls(void);
static int test_group_property_lists(void);

/* Attribute interface tests */
static int test_create_attribute_on_root(void);
static int test_create_attribute_on_dataset(void);
static int test_create_attribute_on_datatype(void);
static int test_create_attribute_with_null_space(void);
static int test_create_attribute_with_scalar_space(void);
static int test_get_attribute_info(void);
static int test_get_attribute_space_and_type(void);
static int test_get_attribute_name(void);
static int test_create_attribute_with_space_in_name(void);
static int test_delete_attribute(void);
static int test_write_attribute(void);
static int test_read_attribute(void);
static int test_get_number_attributes(void);
static int test_attribute_iterate(void);
static int test_attribute_iterate_0_attributes(void);
static int test_unused_attribute_API_calls(void);
static int test_attribute_property_lists(void);

/* Dataset interface tests */
static int test_create_dataset_under_root(void);
static int test_create_anonymous_dataset(void);
static int test_create_dataset_under_existing_group(void);
static int test_create_dataset_null_space(void);
static int test_create_dataset_scalar_space(void);
static int test_create_dataset_predefined_types(void);
static int test_create_dataset_string_types(void);
static int test_create_dataset_compound_types(void);
static int test_create_dataset_enum_types(void);
static int test_create_dataset_array_types(void);
static int test_create_dataset_shapes(void);
static int test_create_dataset_creation_properties(void);
static int test_write_dataset_small_all(void);
static int test_write_dataset_small_hyperslab(void);
static int test_write_dataset_small_point_selection(void);
#ifndef NO_LARGE_TESTS
static int test_write_dataset_large_all(void);
static int test_write_dataset_large_hyperslab(void);
static int test_write_dataset_large_point_selection(void);
#endif
static int test_read_dataset_small_all(void);
static int test_read_dataset_small_hyperslab(void);
static int test_read_dataset_small_point_selection(void);
#ifndef NO_LARGE_TESTS
static int test_read_dataset_large_all(void);
static int test_read_dataset_large_hyperslab(void);
static int test_read_dataset_large_point_selection(void);
#endif
static int test_write_dataset_data_verification(void);
static int test_dataset_set_extent(void);
static int test_unused_dataset_API_calls(void);
static int test_dataset_property_lists(void);

/* Committed Datatype interface tests */
static int test_create_committed_datatype(void);
static int test_create_anonymous_committed_datatype(void);
static int test_create_dataset_with_committed_type(void);
static int test_create_attribute_with_committed_type(void);
static int test_delete_committed_type(void);
static int test_unused_datatype_API_calls(void);
static int test_datatype_property_lists(void);

/* Link interface tests */
static int test_create_hard_link(void);
static int test_create_hard_link_same_loc(void);
static int test_create_soft_link_existing_relative(void);
static int test_create_soft_link_existing_absolute(void);
static int test_create_soft_link_dangling_relative(void);
static int test_create_soft_link_dangling_absolute(void);
static int test_create_external_link(void);
static int test_create_dangling_external_link(void);
static int test_create_user_defined_link(void);
static int test_delete_link(void);
static int test_copy_link(void);
static int test_move_link(void);
static int test_get_link_info(void);
static int test_get_link_name_by_index(void);
static int test_get_link_val(void);
static int test_link_iterate(void);
static int test_link_iterate_0_links(void);
static int test_link_visit(void);
static int test_link_visit_cycles(void);
static int test_link_visit_0_links(void);
static int test_unused_link_API_calls(void);

/* Object interface tests */
static int test_open_dataset_generically(void);
static int test_open_group_generically(void);
static int test_open_datatype_generically(void);
static int test_object_exists(void);
static int test_incr_decr_refcount(void);
static int test_h5o_copy(void);
static int test_h5o_close(void);
static int test_object_visit(void);
static int test_create_obj_ref(void);
static int test_dereference_reference(void);
static int test_get_ref_type(void);
static int test_get_ref_name(void);
static int test_get_region(void);
static int test_write_dataset_w_obj_refs(void);
static int test_read_dataset_w_obj_refs(void);
static int test_write_dataset_w_obj_refs_empty_data(void);
static int test_unused_object_API_calls(void);

/* Miscellaneous tests to check edge cases */
static int test_open_link_without_leading_slash(void);
static int test_object_creation_by_absolute_path(void);
static int test_absolute_vs_relative_path(void);
static int test_url_encoding(void);
static int test_symbols_in_compound_field_name(void);
static int test_double_init_free(void);

static herr_t attr_iter_callback1(hid_t location_id, const char *attr_name, const H5A_info_t *ainfo,
                                  void *op_data);
static herr_t attr_iter_callback2(hid_t location_id, const char *attr_name, const H5A_info_t *ainfo,
                                  void *op_data);

static herr_t link_iter_callback1(hid_t group_id, const char *name, const H5L_info2_t *info, void *op_data);
static herr_t link_iter_callback2(hid_t group_id, const char *name, const H5L_info2_t *info, void *op_data);
static herr_t link_iter_callback3(hid_t group_id, const char *name, const H5L_info2_t *info, void *op_data);

static herr_t link_visit_callback1(hid_t group_id, const char *name, const H5L_info2_t *info, void *op_data);
static herr_t link_visit_callback2(hid_t group_id, const char *name, const H5L_info2_t *info, void *op_data);
static herr_t link_visit_callback3(hid_t group_id, const char *name, const H5L_info2_t *info, void *op_data);

static herr_t object_visit_callback(hid_t o_id, const char *name, const H5O_info2_t *object_info,
                                    void *op_data);

static hid_t generate_random_datatype(H5T_class_t parent_class);

static int (*setup_tests[])(void) = {test_setup_connector, NULL};

static int (*file_tests[])(void) = {
    test_create_file,           test_get_file_info,       test_nonexistent_file,
    test_get_file_intent,       test_get_file_name,       test_file_reopen,
    test_unused_file_API_calls, test_file_property_lists, NULL};

static int (*group_tests[])(void) = {
    test_create_group_invalid_loc_id, test_create_group_under_root, test_create_group_under_existing_group,
    test_create_anonymous_group,      test_get_group_info,          test_nonexistent_group,
    test_unused_group_API_calls,      test_group_property_lists,    NULL};

static int (*attribute_tests[])(void) = {test_create_attribute_on_root,
                                         test_create_attribute_on_dataset,
                                         test_create_attribute_on_datatype,
                                         test_create_attribute_with_null_space,
                                         test_create_attribute_with_scalar_space,
                                         test_get_attribute_info,
                                         test_get_attribute_space_and_type,
                                         test_get_attribute_name,
                                         test_create_attribute_with_space_in_name,
                                         test_delete_attribute,
                                         test_write_attribute,
                                         test_read_attribute,
                                         test_get_number_attributes,
                                         test_attribute_iterate,
                                         test_attribute_iterate_0_attributes,
                                         test_unused_attribute_API_calls,
                                         test_attribute_property_lists,
                                         NULL};

static int (*dataset_tests[])(void) = {test_create_dataset_under_root,
                                       test_create_anonymous_dataset,
                                       test_create_dataset_under_existing_group,
                                       test_create_dataset_null_space,
                                       test_create_dataset_scalar_space,
                                       test_create_dataset_predefined_types,
                                       test_create_dataset_string_types,
                                       test_create_dataset_compound_types,
                                       test_create_dataset_enum_types,
                                       test_create_dataset_array_types,
                                       test_create_dataset_shapes,
                                       test_create_dataset_creation_properties,
                                       test_write_dataset_small_all,
                                       test_write_dataset_small_hyperslab,
                                       test_write_dataset_small_point_selection,
#ifndef NO_LARGE_TESTS
                                       test_write_dataset_large_all,
                                       test_write_dataset_large_hyperslab,
                                       test_write_dataset_large_point_selection,
#endif
                                       test_read_dataset_small_all,
                                       test_read_dataset_small_hyperslab,
                                       test_read_dataset_small_point_selection,
#ifndef NO_LARGE_TESTS
                                       test_read_dataset_large_all,
                                       test_read_dataset_large_hyperslab,
                                       test_read_dataset_large_point_selection,
#endif
                                       test_write_dataset_data_verification,
                                       test_dataset_set_extent,
                                       test_unused_dataset_API_calls,
                                       test_dataset_property_lists,
                                       NULL};

static int (*type_tests[])(void) = {test_create_committed_datatype,
                                    test_create_anonymous_committed_datatype,
                                    test_create_dataset_with_committed_type,
                                    test_create_attribute_with_committed_type,
                                    test_delete_committed_type,
                                    test_unused_datatype_API_calls,
                                    test_datatype_property_lists,
                                    NULL};

static int (*link_tests[])(void) = {test_create_hard_link,
                                    test_create_hard_link_same_loc,
                                    test_create_soft_link_existing_relative,
                                    test_create_soft_link_existing_absolute,
                                    test_create_soft_link_dangling_relative,
                                    test_create_soft_link_dangling_absolute,
                                    test_create_external_link,
                                    test_create_dangling_external_link,
                                    test_create_user_defined_link,
                                    test_delete_link,
                                    test_copy_link,
                                    test_move_link,
                                    test_get_link_info,
                                    test_get_link_name_by_index,
                                    test_get_link_val,
                                    test_link_iterate,
                                    test_link_iterate_0_links,
                                    test_link_visit,
                                    test_link_visit_cycles,
                                    test_link_visit_0_links,
                                    test_unused_link_API_calls,
                                    NULL};

static int (*object_tests[])(void) = {test_open_dataset_generically,
                                      test_open_group_generically,
                                      test_open_datatype_generically,
                                      test_object_exists,
                                      test_incr_decr_refcount,
                                      test_h5o_copy,
                                      test_h5o_close,
                                      test_object_visit,
                                      test_create_obj_ref,
                                      test_dereference_reference,
                                      test_get_ref_type,
                                      test_get_ref_name,
                                      test_get_region,
                                      test_write_dataset_w_obj_refs,
                                      test_read_dataset_w_obj_refs,
                                      test_write_dataset_w_obj_refs_empty_data,
                                      test_unused_object_API_calls,
                                      NULL};

static int (*misc_tests[])(void) = {test_open_link_without_leading_slash,
                                    test_object_creation_by_absolute_path,
                                    test_absolute_vs_relative_path,
                                    test_url_encoding,
                                    test_symbols_in_compound_field_name,
                                    test_double_init_free,
                                    NULL};

static int (**tests[])(void) = {
    setup_tests, file_tests, group_tests,  attribute_tests, dataset_tests,
    link_tests,  type_tests, object_tests, misc_tests,
};
/*****************************************************
 *                                                   *
 *      Plugin initialization/termination tests      *
 *                                                   *
 *****************************************************/

static int
test_setup_connector(void)
{
    hid_t fapl_id = -1;

    TESTING("connector setup");

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Pclose(fapl_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

/*****************************************************
 *                                                   *
 *                 Plugin File tests                 *
 *                                                   *
 *****************************************************/

static int
test_create_file(void)
{
    hid_t file_id = -1, fapl = -1;
    hid_t group_id = -1;

    TESTING("create file")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl) < 0)
        TEST_ERROR

    if ((file_id = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, fapl)) < 0) {
        H5_FAILED();
        printf("    couldn't create file\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Setting up container groups\n");
#endif

    /* Setup container groups for the different classes of tests */
    if ((group_id = H5Gcreate2(file_id, GROUP_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group for group tests\n");
        goto error;
    }

    if (H5Gclose(group_id) < 0)
        TEST_ERROR

    if ((group_id = H5Gcreate2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) <
        0) {
        H5_FAILED();
        printf("    couldn't create group for attribute tests\n");
        goto error;
    }

    if (H5Gclose(group_id) < 0)
        TEST_ERROR

    if ((group_id = H5Gcreate2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) <
        0) {
        H5_FAILED();
        printf("    couldn't create group for dataset tests\n");
        goto error;
    }

    if (H5Gclose(group_id) < 0)
        TEST_ERROR

    if ((group_id = H5Gcreate2(file_id, DATATYPE_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) <
        0) {
        H5_FAILED();
        printf("    couldn't create group for datatype tests\n");
        goto error;
    }

    if (H5Gclose(group_id) < 0)
        TEST_ERROR

    if ((group_id = H5Gcreate2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group for link tests\n");
        goto error;
    }

    if (H5Gclose(group_id) < 0)
        TEST_ERROR

    if ((group_id = H5Gcreate2(file_id, OBJECT_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group for object tests\n");
        goto error;
    }

    if (H5Gclose(group_id) < 0)
        TEST_ERROR

    if ((group_id =
             H5Gcreate2(file_id, MISCELLANEOUS_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group for miscellaneous tests\n");
        goto error;
    }

    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Pclose(fapl) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(group_id);
        H5Pclose(fapl);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_get_file_info(void)
{
    H5F_info2_t file_info;
    hid_t       file_id = -1, fapl_id = -1;

    TESTING("retrieve file info")
    SKIPPED()
    return 0;

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving file info\n");
#endif

    if (H5Fget_info2(file_id, &file_info) < 0) {
        H5_FAILED();
        printf("    couldn't get file info\n");
        goto error;
    }

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_nonexistent_file(void)
{
    hid_t file_id = -1, fapl_id = -1;
    char  test_filename[FILENAME_MAX_LENGTH];

    TESTING("failure for opening non-existent file")

    snprintf(test_filename, FILENAME_MAX_LENGTH, "%s/%s/%s", TEST_DIR_PREFIX, username, NONEXISTENT_FILENAME);

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to open non-existent file\n");
#endif

    H5E_BEGIN_TRY
    {
        if ((file_id = H5Fopen(test_filename, H5F_ACC_RDWR, fapl_id)) >= 0) {
            H5_FAILED();
            printf("    non-existent file was opened!\n");
            goto error;
        }
    }
    H5E_END_TRY;

#ifdef RV_CONNECTOR_DEBUG
    puts("File open call successfully failed for non-existent file\n");
#endif

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Pclose(fapl_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_get_file_intent(void)
{
    unsigned file_intent;
    hsize_t  space_dims[FILE_INTENT_TEST_DSET_RANK];
    size_t   i;
    hid_t    file_id = -1, fapl_id = -1;
    hid_t    dset_id    = -1;
    hid_t    dset_dtype = -1;
    hid_t    space_id   = -1;
    char     test_filename[FILENAME_MAX_LENGTH];

    TESTING("retrieve file intent")

    snprintf(test_filename, FILENAME_MAX_LENGTH, "%s/%s/%s", TEST_DIR_PREFIX, username,
             FILE_INTENT_TEST_FILENAME);

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    /* Test that file intent works correctly for file create */
    if ((file_id = H5Fcreate(test_filename, H5F_ACC_TRUNC, H5P_DEFAULT, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't create file\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Checking to make sure H5F_ACC_TRUNC works correctly\n");
#endif

    if (H5Fget_intent(file_id, &file_intent) < 0)
        TEST_ERROR

    if (H5F_ACC_RDWR != file_intent) {
        H5_FAILED();
        printf("    received incorrect file intent\n");
        goto error;
    }

    if (H5Fclose(file_id) < 0)
        TEST_ERROR

    /* Test that file intent works correctly for file open */
    if ((file_id = H5Fopen(filename, H5F_ACC_RDONLY, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Checking to make sure H5F_ACC_RDONLY works correctly\n");
#endif

    if (H5Fget_intent(file_id, &file_intent) < 0)
        TEST_ERROR

    if (H5F_ACC_RDONLY != file_intent) {
        H5_FAILED();
        printf("    received incorrect file intent\n");
        goto error;
    }

    for (i = 0; i < FILE_INTENT_TEST_DSET_RANK; i++)
        space_dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((space_id = H5Screate_simple(FILE_INTENT_TEST_DSET_RANK, space_dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Checking to make sure we can't create an object when H5F_ACC_RDONLY is specified\n");
#endif

    /* Ensure that no objects can be created when a file is opened in read-only mode */
    H5E_BEGIN_TRY
    {
        if ((dset_id = H5Dcreate2(file_id, FILE_INTENT_TEST_DATASETNAME, dset_dtype, space_id, H5P_DEFAULT,
                                  H5P_DEFAULT, H5P_DEFAULT)) >= 0) {
            H5_FAILED();
            printf("    read-only file was modified!\n");
            goto error;
        }
    }
    H5E_END_TRY;

    if (H5Fclose(file_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Checking to make sure H5F_ACC_RDWR works correctly\n");
#endif

    if (H5Fget_intent(file_id, &file_intent) < 0)
        TEST_ERROR

    if (H5F_ACC_RDWR != file_intent) {
        H5_FAILED();
        printf("    received incorrect file intent\n");
        goto error;
    }

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(space_id);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_get_file_name(void)
{
    ssize_t file_name_buf_len = 0;
    char   *file_name_buf     = NULL;
    hid_t   file_id = -1, fapl_id = -1;

    TESTING("get file name with H5Fget_name")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving size of file name\n");
#endif

    /* Retrieve the size of the file name */
    if ((file_name_buf_len = H5Fget_name(file_id, NULL, 0)) < 0)
        TEST_ERROR

    /* Allocate buffer for file name */
    if (NULL == (file_name_buf = (char *)malloc((size_t)file_name_buf_len + 1)))
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving file name\n");
#endif

    /* Retrieve the actual file name */
    if (H5Fget_name(file_id, file_name_buf, (size_t)file_name_buf_len + 1) < 0)
        TEST_ERROR

    if (file_name_buf) {
        free(file_name_buf);
        file_name_buf = NULL;
    }

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        if (file_name_buf)
            free(file_name_buf);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_file_reopen(void)
{
    hid_t file_id = -1, file_id2 = -1, fapl_id = -1;

    TESTING("re-open file w/ H5Freopen")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Re-opening file\n");
#endif

    if ((file_id2 = H5Freopen(file_id)) < 0) {
        H5_FAILED();
        printf("    couldn't re-open file\n");
        goto error;
    }

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id2) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5Fclose(file_id2);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_unused_file_API_calls(void)
{
    hid_t file_id = -1, fapl_id = -1;

    TESTING("unused File API calls")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Testing that all of the unused file API calls don't cause application issues\n");
#endif

    H5E_BEGIN_TRY
    {
        H5AC_cache_config_t mdc_config = {0};
        hsize_t             filesize;
        double              mdc_hit_rate;
        size_t              file_image_buf_len = 0;
        hid_t               obj_id;
        void               *file_handle;

        if (H5Fmount(file_id, "/", file_id, H5P_DEFAULT) >= 0)
            TEST_ERROR
        if (H5Funmount(file_id, "/") >= 0)
            TEST_ERROR
        if (H5Fclear_elink_file_cache(file_id) >= 0)
            TEST_ERROR
        if (H5Fget_file_image(file_id, NULL, file_image_buf_len) >= 0)
            TEST_ERROR
        if (H5Fget_free_sections(file_id, H5FD_MEM_DEFAULT, 0, NULL) >= 0)
            TEST_ERROR
        if (H5Fget_freespace(file_id) >= 0)
            TEST_ERROR
        if (H5Fget_mdc_config(file_id, &mdc_config) >= 0)
            TEST_ERROR
        if (H5Fget_mdc_hit_rate(file_id, &mdc_hit_rate) >= 0)
            TEST_ERROR
        if (H5Fget_mdc_size(file_id, NULL, NULL, NULL, NULL) >= 0)
            TEST_ERROR
        if (H5Fget_filesize(file_id, &filesize) >= 0)
            TEST_ERROR
        if (H5Fget_vfd_handle(file_id, fapl_id, &file_handle) >= 0)
            TEST_ERROR
        if (H5Freset_mdc_hit_rate_stats(file_id) >= 0)
            TEST_ERROR
        if (H5Fset_mdc_config(file_id, &mdc_config) >= 0)
            TEST_ERROR
    }
    H5E_END_TRY;

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_file_property_lists(void)
{
    hid_t file_id1 = -1, file_id2 = -1, fapl_id = -1;
    hid_t fcpl_id1 = -1, fcpl_id2 = -1;
    hid_t fapl_id1 = -1, fapl_id2 = -1;
    char  test_filename1[FILENAME_MAX_LENGTH], test_filename2[FILENAME_MAX_LENGTH];

    TESTING("file property list operations")

    snprintf(test_filename1, FILENAME_MAX_LENGTH, "%s/%s/%s", TEST_DIR_PREFIX, username,
             FILE_PROPERTY_LIST_TEST_FNAME1);
    snprintf(test_filename2, FILENAME_MAX_LENGTH, "%s/%s/%s", TEST_DIR_PREFIX, username,
             FILE_PROPERTY_LIST_TEST_FNAME2);

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((fcpl_id1 = H5Pcreate(H5P_FILE_CREATE)) < 0) {
        H5_FAILED();
        printf("    couldn't create FCPL\n");
        goto error;
    }

    if ((file_id1 = H5Fcreate(test_filename1, H5F_ACC_TRUNC, fcpl_id1, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't create file\n");
        goto error;
    }

    if ((file_id2 = H5Fcreate(test_filename2, H5F_ACC_TRUNC, H5P_DEFAULT, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't create file\n");
        goto error;
    }

    if (H5Pclose(fcpl_id1) < 0)
        TEST_ERROR

    /* Try to receive copies of the two property lists, one which has the property set and one which does not
     */
    if ((fcpl_id1 = H5Fget_create_plist(file_id1)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if ((fcpl_id2 = H5Fget_create_plist(file_id2)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    /* Ensure that property list 1 has the property set and property list 2 does not */

    /* Due to the nature of needing to supply a FAPL with the REST VOL having been set on it to the
     * H5Fcreate() call, we cannot exactly test using H5P_DEFAULT as the FAPL for one of the create calls in
     * this test. However, the use of H5Fget_create_plist() will still be used to check that the FAPL is
     * correct after both creating and opening a file
     */
    if ((fapl_id1 = H5Fget_access_plist(file_id1)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if ((fapl_id2 = H5Fget_access_plist(file_id2)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    /* Now close the property lists and files and see if we can still retrieve copies of
     * the property lists upon opening (instead of creating) a file
     */
    if (H5Pclose(fcpl_id1) < 0)
        TEST_ERROR
    if (H5Pclose(fcpl_id2) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id1) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id2) < 0)
        TEST_ERROR
    if (H5Fclose(file_id1) < 0)
        TEST_ERROR
    if (H5Fclose(file_id2) < 0)
        TEST_ERROR

    if ((file_id1 = H5Fopen(test_filename1, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((file_id2 = H5Fopen(test_filename2, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((fcpl_id1 = H5Fget_create_plist(file_id1)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if ((fcpl_id2 = H5Fget_create_plist(file_id2)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if ((fapl_id1 = H5Fget_access_plist(file_id1)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if ((fapl_id2 = H5Fget_access_plist(file_id2)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    /* For completeness' sake, check to make sure the REST VOL is set on each of the FAPLs */

    if (H5Pclose(fcpl_id1) < 0)
        TEST_ERROR
    if (H5Pclose(fcpl_id2) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id1) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id2) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id1) < 0)
        TEST_ERROR
    if (H5Fclose(file_id2) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Pclose(fcpl_id1);
        H5Pclose(fcpl_id2);
        H5Pclose(fapl_id1);
        H5Pclose(fapl_id2);
        H5Pclose(fapl_id);
        H5Fclose(file_id1);
        H5Fclose(file_id2);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

/*****************************************************
 *                                                   *
 *                Plugin Group tests                 *
 *                                                   *
 *****************************************************/

static int
test_create_group_invalid_loc_id(void)
{
    hid_t file_id = -1, fapl_id = -1;
    hid_t group_id = -1;

    TESTING("create group with invalid loc_id")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Trying to create a group with an invalid loc_id\n");
#endif

    H5E_BEGIN_TRY
    {
        if ((group_id = H5Gcreate2(file_id, GROUP_CREATE_INVALID_LOC_ID_GNAME, H5P_DEFAULT, H5P_DEFAULT,
                                   H5P_DEFAULT)) >= 0) {
            H5_FAILED();
            printf("    created group in invalid loc_id!\n");
            goto error;
        }
    }
    H5E_END_TRY;

#ifdef RV_CONNECTOR_DEBUG
    puts("Group create call successfully failed with invalid loc_id\n");
#endif

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Pclose(fapl_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_group_under_root(void)
{
    hid_t file_id = -1, group_id = -1, fapl_id = -1;

    TESTING("create group under root group")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating group under root group\n");
#endif

    /* Create the group under the root group of the file */
    if ((group_id =
             H5Gcreate2(file_id, GROUP_CREATE_UNDER_ROOT_GNAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group\n");
        goto error;
    }

    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(group_id);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_group_under_existing_group(void)
{
    hid_t file_id         = -1;
    hid_t parent_group_id = -1, new_group_id = -1;
    hid_t fapl_id = -1;

    TESTING("create group under existing group using relative path")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    /* Open the already-existing parent group in the file */
    if ((parent_group_id = H5Gopen2(file_id, GROUP_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open group\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating group under non-root group\n");
#endif

    /* Create a new Group under the already-existing parent Group using a relative path */
    if ((new_group_id = H5Gcreate2(parent_group_id, GROUP_CREATE_UNDER_GROUP_REL_GNAME, H5P_DEFAULT,
                                   H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group using relative path\n");
        goto error;
    }

    if (H5Gclose(parent_group_id) < 0)
        TEST_ERROR
    if (H5Gclose(new_group_id) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(new_group_id);
        H5Gclose(parent_group_id);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_anonymous_group(void)
{
    hid_t file_id         = -1;
    hid_t container_group = -1, new_group_id = -1;
    hid_t fapl_id = -1;

    TESTING("create anonymous group")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, GROUP_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open group\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating anonymous group\n");
#endif

    if ((new_group_id = H5Gcreate_anon(file_id, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create anonymous group\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Linking anonymous group into file structure\n");
#endif

    if (H5Olink(new_group_id, container_group, GROUP_CREATE_ANONYMOUS_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT) <
        0) {
        H5_FAILED();
        printf("    couldn't link anonymous group into file structure\n");
        goto error;
    }

    if (H5Gclose(new_group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(new_group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_get_group_info(void)
{
    H5G_info_t group_info;
    hid_t      file_id = -1, fapl_id = -1;

    TESTING("retrieve group info")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving group info with H5Gget_info\n");
#endif

    if (H5Gget_info(file_id, &group_info) < 0) {
        H5_FAILED();
        printf("    couldn't get group info\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving group info with H5Gget_info_by_name\n");
#endif

    if (H5Gget_info_by_name(file_id, "/", &group_info, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't get group info by name\n");
        goto error;
    }

    H5E_BEGIN_TRY
    {
#ifdef RV_CONNECTOR_DEBUG
        puts("Retrieving group info with H5Gget_info_by_idx\n");
#endif

        if (H5Gget_info_by_idx(file_id, "/", H5_INDEX_NAME, H5_ITER_INC, 0, &group_info, H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded!\n");
            goto error;
        }
    }
    H5E_END_TRY;

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_nonexistent_group(void)
{
    hid_t file_id = -1, group_id = -1, fapl_id = -1;

    TESTING("failure for opening nonexistent group")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to open a non-existent group\n");
#endif

    H5E_BEGIN_TRY
    {
        if ((group_id = H5Gopen2(file_id, NONEXISTENT_GROUP_TEST_GNAME, H5P_DEFAULT)) >= 0) {
            H5_FAILED();
            printf("    opened non-existent group!\n");
            goto error;
        }
    }
    H5E_END_TRY;

#ifdef RV_CONNECTOR_DEBUG
    puts("Group open call successfully failed for non-existent group\n");
#endif

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_unused_group_API_calls(void)
{
    TESTING("unused group API calls")

    /* None currently that aren't planned to be used */
#ifdef RV_CONNECTOR_DEBUG
    puts("Currently no APIs to test here\n");
#endif

    SKIPPED();

    return 0;
}

static int
test_group_property_lists(void)
{
    size_t dummy_prop_val = GROUP_PROPERTY_LIST_TEST_DUMMY_VAL;
    hid_t  file_id = -1, fapl_id = -1;
    hid_t  container_group = -1;
    hid_t  group_id1 = -1, group_id2 = -1;
    hid_t  gcpl_id1 = -1, gcpl_id2 = -1;

    TESTING("group property list operations")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, GROUP_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((gcpl_id1 = H5Pcreate(H5P_GROUP_CREATE)) < 0) {
        H5_FAILED();
        printf("    couldn't create GCPL\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Setting property on GCPL\n");
#endif

    if (H5Pset_local_heap_size_hint(gcpl_id1, dummy_prop_val) < 0) {
        H5_FAILED();
        printf("    couldn't set property on GCPL\n");
        goto error;
    }

    /* Create the group in the file */
    if ((group_id1 = H5Gcreate2(container_group, GROUP_PROPERTY_LIST_TEST_GROUP_NAME1, H5P_DEFAULT, gcpl_id1,
                                H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group\n");
        goto error;
    }

    /* Create the second group using H5P_DEFAULT for the GCPL */
    if ((group_id2 = H5Gcreate2(container_group, GROUP_PROPERTY_LIST_TEST_GROUP_NAME2, H5P_DEFAULT,
                                H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group\n");
        goto error;
    }

    if (H5Pclose(gcpl_id1) < 0)
        TEST_ERROR

    /* Try to retrieve copies of the two property lists, one which has the property set and one which does not
     */
    if ((gcpl_id1 = H5Gget_create_plist(group_id1)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if ((gcpl_id2 = H5Gget_create_plist(group_id2)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    /* Ensure that property list 1 has the property set and property list 2 does not */
    dummy_prop_val = 0;

    if (H5Pget_local_heap_size_hint(gcpl_id1, &dummy_prop_val) < 0) {
        H5_FAILED();
        printf("    couldn't retrieve GCPL property value\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Checking that property value is retrieved correctly\n");
#endif

    if (dummy_prop_val != GROUP_PROPERTY_LIST_TEST_DUMMY_VAL) {
        H5_FAILED();
        printf("    GCPL property value was incorrect\n");
        goto error;
    }

    dummy_prop_val = 0;

    if (H5Pget_local_heap_size_hint(gcpl_id2, &dummy_prop_val) < 0) {
        H5_FAILED();
        printf("    couldn't retrieve GCPL property value\n");
        goto error;
    }

    if (dummy_prop_val == GROUP_PROPERTY_LIST_TEST_DUMMY_VAL) {
        H5_FAILED();
        printf("    GCPL property value was set!\n");
        goto error;
    }

    /* Now close the property lists and groups and see if we can still retrieve copies of
     * the property lists upon opening (instead of creating) a group
     */
    if (H5Pclose(gcpl_id1) < 0)
        TEST_ERROR
    if (H5Pclose(gcpl_id2) < 0)
        TEST_ERROR
    if (H5Gclose(group_id1) < 0)
        TEST_ERROR
    if (H5Gclose(group_id2) < 0)
        TEST_ERROR

    if ((group_id1 = H5Gopen2(container_group, GROUP_PROPERTY_LIST_TEST_GROUP_NAME1, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open group\n");
        goto error;
    }

    if ((group_id2 = H5Gopen2(container_group, GROUP_PROPERTY_LIST_TEST_GROUP_NAME2, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open group\n");
        goto error;
    }

    if ((gcpl_id1 = H5Gget_create_plist(group_id1)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if ((gcpl_id2 = H5Gget_create_plist(group_id2)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if (H5Pclose(gcpl_id1) < 0)
        TEST_ERROR
    if (H5Pclose(gcpl_id2) < 0)
        TEST_ERROR
    if (H5Gclose(group_id1) < 0)
        TEST_ERROR
    if (H5Gclose(group_id2) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Pclose(gcpl_id1);
        H5Pclose(gcpl_id2);
        H5Gclose(group_id1);
        H5Gclose(group_id2);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

/*****************************************************
 *                                                   *
 *              Plugin Attribute tests               *
 *                                                   *
 *****************************************************/

static int
test_create_attribute_on_root(void)
{
    hsize_t dims[ATTRIBUTE_CREATE_ON_ROOT_SPACE_RANK];
    size_t  i;
    htri_t  attr_exists;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   attr_id = -1, attr_id2 = -1;
    hid_t   attr_dtype1 = -1, attr_dtype2 = -1;
    hid_t   space_id = -1;

    TESTING("create, open and close attribute on root group")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    for (i = 0; i < ATTRIBUTE_CREATE_ON_ROOT_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((space_id = H5Screate_simple(ATTRIBUTE_CREATE_ON_ROOT_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_dtype1 = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR
    if ((attr_dtype2 = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating attribute on root group with H5Acreate2\n");
#endif

    if ((attr_id = H5Acreate2(file_id, ATTRIBUTE_CREATE_ON_ROOT_ATTR_NAME, attr_dtype1, space_id, H5P_DEFAULT,
                              H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating attribute on root group with H5Acreate_by_name\n");
#endif

    if ((attr_id2 = H5Acreate_by_name(file_id, "/", ATTRIBUTE_CREATE_ON_ROOT_ATTR_NAME2, attr_dtype2,
                                      space_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute on object by name\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that the attributes exist\n");
#endif

    /* Verify the attributes have been created */
    if ((attr_exists = H5Aexists(file_id, ATTRIBUTE_CREATE_ON_ROOT_ATTR_NAME)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

    if ((attr_exists = H5Aexists(file_id, ATTRIBUTE_CREATE_ON_ROOT_ATTR_NAME2)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

    if ((attr_exists = H5Aexists_by_name(file_id, "/", ATTRIBUTE_CREATE_ON_ROOT_ATTR_NAME, H5P_DEFAULT)) <
        0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists by H5Aexists_by_name\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

    if ((attr_exists = H5Aexists_by_name(file_id, "/", ATTRIBUTE_CREATE_ON_ROOT_ATTR_NAME2, H5P_DEFAULT)) <
        0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists by H5Aexists_by_name\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

    /* Now close the attributes and verify we can open them */
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id2) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to open the attributes with H5Aopen\n");
#endif

    if ((attr_id = H5Aopen(file_id, ATTRIBUTE_CREATE_ON_ROOT_ATTR_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute\n");
        goto error;
    }

    if ((attr_id2 = H5Aopen(file_id, ATTRIBUTE_CREATE_ON_ROOT_ATTR_NAME2, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute\n");
        goto error;
    }

    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id2) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to open the attributes with H5Aopen_by_name\n");
#endif

    if ((attr_id = H5Aopen_by_name(file_id, "/", ATTRIBUTE_CREATE_ON_ROOT_ATTR_NAME, H5P_DEFAULT,
                                   H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute by name\n");
        goto error;
    }

    if ((attr_id2 = H5Aopen_by_name(file_id, "/", ATTRIBUTE_CREATE_ON_ROOT_ATTR_NAME2, H5P_DEFAULT,
                                    H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute by name\n");
        goto error;
    }

    H5E_BEGIN_TRY
    {
        if (H5Aclose(attr_id) < 0)
            TEST_ERROR
        if (H5Aclose(attr_id2) < 0)
            TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
        puts("Attempting to open the attributes with H5Aopen_by_idx\n");
#endif

        if ((attr_id =
                 H5Aopen_by_idx(file_id, "/", H5_INDEX_NAME, H5_ITER_INC, 0, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    failed to open attribute by index!\n");
            goto error;
        }

        if ((attr_id2 =
                 H5Aopen_by_idx(file_id, "/", H5_INDEX_NAME, H5_ITER_INC, 0, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    failed to open attribute by index!\n");
            goto error;
        }
    }
    H5E_END_TRY;

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Tclose(attr_dtype1) < 0)
        TEST_ERROR
    if (H5Tclose(attr_dtype2) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id2) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(space_id);
        H5Tclose(attr_dtype1);
        H5Tclose(attr_dtype2);
        H5Aclose(attr_id);
        H5Aclose(attr_id2);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_attribute_on_dataset(void)
{
    hsize_t dset_dims[ATTRIBUTE_CREATE_ON_DATASET_DSET_SPACE_RANK];
    hsize_t attr_dims[ATTRIBUTE_CREATE_ON_DATASET_ATTR_SPACE_RANK];
    size_t  i;
    htri_t  attr_exists;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   dset_id         = -1;
    hid_t   attr_id = -1, attr_id2 = -1;
    hid_t   attr_dtype1 = -1, attr_dtype2 = -1;
    hid_t   dset_dtype    = -1;
    hid_t   dset_space_id = -1;
    hid_t   attr_space_id = -1;

    TESTING("create attribute on dataset")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    for (i = 0; i < ATTRIBUTE_CREATE_ON_DATASET_DSET_SPACE_RANK; i++)
        dset_dims[i] = (hsize_t)rand() % MAX_DIM_SIZE + 1;
    for (i = 0; i < ATTRIBUTE_CREATE_ON_DATASET_ATTR_SPACE_RANK; i++)
        attr_dims[i] = (hsize_t)rand() % MAX_DIM_SIZE + 1;

    if ((dset_space_id = H5Screate_simple(ATTRIBUTE_CREATE_ON_DATASET_DSET_SPACE_RANK, dset_dims, NULL)) < 0)
        TEST_ERROR
    if ((attr_space_id = H5Screate_simple(ATTRIBUTE_CREATE_ON_DATASET_ATTR_SPACE_RANK, attr_dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR
    if ((attr_dtype1 = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR
    if ((attr_dtype2 = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, ATTRIBUTE_CREATE_ON_DATASET_DSET_NAME, dset_dtype,
                              dset_space_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating attribute on dataset with H5Acreate2\n");
#endif

    if ((attr_id = H5Acreate2(dset_id, ATTRIBUTE_CREATE_ON_DATASET_ATTR_NAME, attr_dtype1, attr_space_id,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating attribute on dataset with H5Acreate_by_name\n");
#endif

    if ((attr_id2 = H5Acreate_by_name(file_id,
                                      "/" ATTRIBUTE_TEST_GROUP_NAME "/" ATTRIBUTE_CREATE_ON_DATASET_DSET_NAME,
                                      ATTRIBUTE_CREATE_ON_DATASET_ATTR_NAME2, attr_dtype2, attr_space_id,
                                      H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute on object by name\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that the attributes exist\n");
#endif

    /* Verify the attributes have been created */
    if ((attr_exists = H5Aexists(dset_id, ATTRIBUTE_CREATE_ON_DATASET_ATTR_NAME)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

    if ((attr_exists = H5Aexists(dset_id, ATTRIBUTE_CREATE_ON_DATASET_ATTR_NAME2)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

    /* Now close the attributes and verify we can open them */
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id2) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to open the attributes with H5Aopen\n");
#endif

    if ((attr_id = H5Aopen(dset_id, ATTRIBUTE_CREATE_ON_DATASET_ATTR_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute\n");
        goto error;
    }

    if ((attr_id2 = H5Aopen(dset_id, ATTRIBUTE_CREATE_ON_DATASET_ATTR_NAME2, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute\n");
        goto error;
    }

    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id2) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to open the attributes with H5Aopen_by_name\n");
#endif

    if ((attr_id =
             H5Aopen_by_name(file_id, "/" ATTRIBUTE_TEST_GROUP_NAME "/" ATTRIBUTE_CREATE_ON_DATASET_DSET_NAME,
                             ATTRIBUTE_CREATE_ON_DATASET_ATTR_NAME, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute by name\n");
        goto error;
    }

    if ((attr_id2 =
             H5Aopen_by_name(file_id, "/" ATTRIBUTE_TEST_GROUP_NAME "/" ATTRIBUTE_CREATE_ON_DATASET_DSET_NAME,
                             ATTRIBUTE_CREATE_ON_DATASET_ATTR_NAME2, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute by name\n");
        goto error;
    }

    H5E_BEGIN_TRY
    {
        if (H5Aclose(attr_id) < 0)
            TEST_ERROR
        if (H5Aclose(attr_id2) < 0)
            TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
        puts("Attempting to open the attributes with H5Aopen_by_idx\n");
#endif

        if ((attr_id = H5Aopen_by_idx(file_id,
                                      "/" ATTRIBUTE_TEST_GROUP_NAME "/" ATTRIBUTE_CREATE_ON_DATASET_DSET_NAME,
                                      H5_INDEX_NAME, H5_ITER_INC, 0, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    failed to open attribute by index!\n");
            goto error;
        }

        if ((attr_id2 = H5Aopen_by_idx(
                 file_id, "/" ATTRIBUTE_TEST_GROUP_NAME "/" ATTRIBUTE_CREATE_ON_DATASET_DSET_NAME,
                 H5_INDEX_NAME, H5_ITER_INC, 0, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    failed to open attribute by index!\n");
            goto error;
        }
    }
    H5E_END_TRY;

    if (H5Sclose(dset_space_id) < 0)
        TEST_ERROR
    if (H5Sclose(attr_space_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Tclose(attr_dtype1) < 0)
        TEST_ERROR
    if (H5Tclose(attr_dtype2) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id2) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(dset_space_id);
        H5Sclose(attr_space_id);
        H5Tclose(dset_dtype);
        H5Tclose(attr_dtype1);
        H5Tclose(attr_dtype2);
        H5Dclose(dset_id);
        H5Aclose(attr_id);
        H5Aclose(attr_id2);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_attribute_on_datatype(void)
{
    hsize_t dims[ATTRIBUTE_CREATE_ON_DATATYPE_SPACE_RANK];
    size_t  i;
    htri_t  attr_exists;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   type_id         = -1;
    hid_t   attr_id = -1, attr_id2 = -1;
    hid_t   attr_dtype1 = -1, attr_dtype2 = -1;
    hid_t   space_id = -1;

    TESTING("create attribute on committed datatype")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((type_id = generate_random_datatype(H5T_NO_CLASS)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if (H5Tcommit2(container_group, ATTRIBUTE_CREATE_ON_DATATYPE_DTYPE_NAME, type_id, H5P_DEFAULT,
                   H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't commit datatype\n");
        goto error;
    }

    {
        /* Temporary workaround for now since H5Tcommit2 doesn't return something publicly usable
         * for a VOL object */
        if (H5Tclose(type_id) < 0)
            TEST_ERROR

        if ((type_id = H5Topen2(container_group, ATTRIBUTE_CREATE_ON_DATATYPE_DTYPE_NAME, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't open committed datatype\n");
            goto error;
        }
    }

    for (i = 0; i < ATTRIBUTE_CREATE_ON_DATATYPE_SPACE_RANK; i++)
        dims[i] = (hsize_t)rand() % MAX_DIM_SIZE + 1;

    if ((space_id = H5Screate_simple(ATTRIBUTE_CREATE_ON_DATATYPE_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_dtype1 = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR
    if ((attr_dtype2 = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating attribute on datatype with H5Acreate2\n");
#endif

    if ((attr_id = H5Acreate2(type_id, ATTRIBUTE_CREATE_ON_DATATYPE_ATTR_NAME, attr_dtype1, space_id,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating attribute on datatype with H5Acreate_by_name\n");
#endif

    if ((attr_id2 = H5Acreate_by_name(
             file_id, "/" ATTRIBUTE_TEST_GROUP_NAME "/" ATTRIBUTE_CREATE_ON_DATATYPE_DTYPE_NAME,
             ATTRIBUTE_CREATE_ON_DATATYPE_ATTR_NAME2, attr_dtype2, space_id, H5P_DEFAULT, H5P_DEFAULT,
             H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute on datatype by name\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that the attributes exist\n");
#endif

    /* Verify the attributes have been created */
    if ((attr_exists = H5Aexists(type_id, ATTRIBUTE_CREATE_ON_DATATYPE_ATTR_NAME)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

    if ((attr_exists = H5Aexists(type_id, ATTRIBUTE_CREATE_ON_DATATYPE_ATTR_NAME2)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

    /* Now close the attributes and verify we can open them */
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id2) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to open the attributes with H5Aopen\n");
#endif

    if ((attr_id = H5Aopen(type_id, ATTRIBUTE_CREATE_ON_DATATYPE_ATTR_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute\n");
        goto error;
    }

    if ((attr_id2 = H5Aopen(type_id, ATTRIBUTE_CREATE_ON_DATATYPE_ATTR_NAME2, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute\n");
        goto error;
    }

    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id2) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to open the attributes with H5Aopen_by_name\n");
#endif

    if ((attr_id = H5Aopen_by_name(file_id,
                                   "/" ATTRIBUTE_TEST_GROUP_NAME "/" ATTRIBUTE_CREATE_ON_DATATYPE_DTYPE_NAME,
                                   ATTRIBUTE_CREATE_ON_DATATYPE_ATTR_NAME, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute by name\n");
        goto error;
    }

    if ((attr_id2 = H5Aopen_by_name(file_id,
                                    "/" ATTRIBUTE_TEST_GROUP_NAME "/" ATTRIBUTE_CREATE_ON_DATATYPE_DTYPE_NAME,
                                    ATTRIBUTE_CREATE_ON_DATATYPE_ATTR_NAME2, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute by name\n");
        goto error;
    }

    H5E_BEGIN_TRY
    {
        if (H5Aclose(attr_id) < 0)
            TEST_ERROR
        if (H5Aclose(attr_id2) < 0)
            TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
        puts("Attempting to open the attributes with H5Aopen_by_idx\n");
#endif

        if ((attr_id =
                 H5Aopen_by_idx(type_id, ".", H5_INDEX_NAME, H5_ITER_INC, 0, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    failed to open attribute by index!\n");
            goto error;
        }

        if ((attr_id2 =
                 H5Aopen_by_idx(type_id, ".", H5_INDEX_NAME, H5_ITER_INC, 0, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    failed to open attribute by index!\n");
            goto error;
        }
    }
    H5E_END_TRY;

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Tclose(attr_dtype1) < 0)
        TEST_ERROR
    if (H5Tclose(attr_dtype2) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id2) < 0)
        TEST_ERROR
    if (H5Tclose(type_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(space_id);
        H5Tclose(attr_dtype1);
        H5Tclose(attr_dtype2);
        H5Aclose(attr_id);
        H5Aclose(attr_id2);
        H5Tclose(type_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_attribute_with_null_space(void)
{
    htri_t attr_exists;
    hid_t  file_id = -1, fapl_id = -1;
    hid_t  container_group = -1, group_id = -1;
    hid_t  attr_id    = -1;
    hid_t  attr_dtype = -1;
    hid_t  space_id   = -1;

    TESTING("create attribute with NULL dataspace")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, ATTRIBUTE_CREATE_NULL_DATASPACE_TEST_SUBGROUP_NAME,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

    if ((space_id = H5Screate(H5S_NULL)) < 0)
        TEST_ERROR

    if ((attr_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    printf("Creating attribute with NULL dataspace\n");
#endif

    if ((attr_id = H5Acreate2(group_id, ATTRIBUTE_CREATE_NULL_DATASPACE_TEST_ATTR_NAME, attr_dtype, space_id,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    /* Verify the attribute has been created */
    if ((attr_exists = H5Aexists(group_id, ATTRIBUTE_CREATE_NULL_DATASPACE_TEST_ATTR_NAME)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

    if (H5Aclose(attr_id) < 0)
        TEST_ERROR

    if ((attr_id = H5Aopen(group_id, ATTRIBUTE_CREATE_NULL_DATASPACE_TEST_ATTR_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute\n");
        goto error;
    }

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Tclose(attr_dtype) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(space_id);
        H5Tclose(attr_dtype);
        H5Aclose(attr_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_attribute_with_scalar_space(void)
{
    htri_t attr_exists;
    hid_t  file_id = -1, fapl_id = -1;
    hid_t  container_group = -1, group_id = -1;
    hid_t  attr_id    = -1;
    hid_t  attr_dtype = -1;
    hid_t  space_id   = -1;

    TESTING("create attribute with SCALAR dataspace")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, ATTRIBUTE_CREATE_SCALAR_DATASPACE_TEST_SUBGROUP_NAME,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

    if ((space_id = H5Screate(H5S_SCALAR)) < 0)
        TEST_ERROR

    if ((attr_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    printf("Creating attribute with SCALAR dataspace\n");
#endif

    if ((attr_id = H5Acreate2(group_id, ATTRIBUTE_CREATE_SCALAR_DATASPACE_TEST_ATTR_NAME, attr_dtype,
                              space_id, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    /* Verify the attribute has been created */
    if ((attr_exists = H5Aexists(group_id, ATTRIBUTE_CREATE_SCALAR_DATASPACE_TEST_ATTR_NAME)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

    if (H5Aclose(attr_id) < 0)
        TEST_ERROR

    if ((attr_id = H5Aopen(group_id, ATTRIBUTE_CREATE_SCALAR_DATASPACE_TEST_ATTR_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute\n");
        goto error;
    }

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Tclose(attr_dtype) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(space_id);
        H5Tclose(attr_dtype);
        H5Aclose(attr_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_get_attribute_info(void)
{
    H5A_info_t attr_info;
    hsize_t    dims[ATTRIBUTE_GET_INFO_TEST_SPACE_RANK];
    size_t     i;
    htri_t     attr_exists;
    hid_t      file_id = -1, fapl_id = -1;
    hid_t      container_group = -1;
    hid_t      attr_id         = -1;
    hid_t      attr_dtype      = -1;
    hid_t      space_id        = -1;

    TESTING("retrieve attribute info")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    for (i = 0; i < ATTRIBUTE_GET_INFO_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((space_id = H5Screate_simple(ATTRIBUTE_GET_INFO_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    if ((attr_id = H5Acreate2(container_group, ATTRIBUTE_GET_INFO_TEST_ATTR_NAME, attr_dtype, space_id,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    /* Verify the attribute has been created */
    if ((attr_exists = H5Aexists(container_group, ATTRIBUTE_GET_INFO_TEST_ATTR_NAME)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving attribute's info with H5Aget_info\n");
#endif

    if (H5Aget_info(attr_id, &attr_info) < 0) {
        H5_FAILED();
        printf("    couldn't get attribute info\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving attribute's info with H5Aget_info_by_name\n");
#endif

    if (H5Aget_info_by_name(container_group, ".", ATTRIBUTE_GET_INFO_TEST_ATTR_NAME, &attr_info,
                            H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't get attribute info by name\n");
        goto error;
    }

    H5E_BEGIN_TRY
    {
#ifdef RV_CONNECTOR_DEBUG
        puts("Retrieving attribute's info with H5Aget_info_by_idx\n");
#endif

        if (H5Aget_info_by_idx(container_group, "/", H5_INDEX_NAME, H5_ITER_INC, 0, &attr_info, H5P_DEFAULT) <
            0) {
            H5_FAILED();
            printf("    failed to open attribute by index!\n");
            goto error;
        }
    }
    H5E_END_TRY;

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Tclose(attr_dtype) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(space_id);
        H5Tclose(attr_dtype);
        H5Aclose(attr_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_get_attribute_space_and_type(void)
{
    hsize_t attr_dims[ATTRIBUTE_GET_SPACE_TYPE_TEST_SPACE_RANK];
    size_t  i;
    htri_t  attr_exists;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   attr_id         = -1;
    hid_t   attr_dtype      = -1;
    hid_t   attr_space_id   = -1;
    hid_t   tmp_type_id     = -1;
    hid_t   tmp_space_id    = -1;

    TESTING("retrieve attribute dataspace and datatype")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    for (i = 0; i < ATTRIBUTE_GET_SPACE_TYPE_TEST_SPACE_RANK; i++)
        attr_dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((attr_space_id = H5Screate_simple(ATTRIBUTE_GET_SPACE_TYPE_TEST_SPACE_RANK, attr_dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    if ((attr_id = H5Acreate2(container_group, ATTRIBUTE_GET_SPACE_TYPE_TEST_ATTR_NAME, attr_dtype,
                              attr_space_id, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    /* Verify the attribute has been created */
    if ((attr_exists = H5Aexists(container_group, ATTRIBUTE_GET_SPACE_TYPE_TEST_ATTR_NAME)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving attribute's datatype\n");
#endif

    /* Retrieve the attribute's datatype and dataspace and verify them */
    if ((tmp_type_id = H5Aget_type(attr_id)) < 0) {
        H5_FAILED();
        printf("    couldn't retrieve attribute's datatype\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving attribute's dataspace\n");
#endif

    if ((tmp_space_id = H5Aget_space(attr_id)) < 0) {
        H5_FAILED();
        printf("    couldn't retrieve attribute's dataspace\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Checking to make sure the attribute's datatype and dataspace match what was provided at creation "
         "time\n");
#endif

    {
        hsize_t space_dims[ATTRIBUTE_GET_SPACE_TYPE_TEST_SPACE_RANK];
        htri_t  types_equal = H5Tequal(tmp_type_id, attr_dtype);

        if (types_equal < 0) {
            H5_FAILED();
            printf("    datatype was invalid\n");
            goto error;
        }

        if (!types_equal) {
            H5_FAILED();
            printf("    attribute's datatype did not match\n");
            goto error;
        }

        if (H5Sget_simple_extent_dims(tmp_space_id, space_dims, NULL) < 0)
            TEST_ERROR

        for (i = 0; i < ATTRIBUTE_GET_SPACE_TYPE_TEST_SPACE_RANK; i++)
            if (space_dims[i] != attr_dims[i]) {
                H5_FAILED();
                printf("    dataspace dims didn't match\n");
                goto error;
            }
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that the previous checks hold true after closing and re-opening the attribute\n");
#endif

    /* Now close the attribute and verify that this still works after opening an
     * attribute instead of creating it
     */
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Tclose(tmp_type_id) < 0)
        TEST_ERROR
    if (H5Sclose(tmp_space_id) < 0)
        TEST_ERROR

    if ((attr_id = H5Aopen(container_group, ATTRIBUTE_GET_SPACE_TYPE_TEST_ATTR_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute\n");
        goto error;
    }

    if ((tmp_type_id = H5Aget_type(attr_id)) < 0) {
        H5_FAILED();
        printf("    couldn't retrieve attribute's datatype\n");
        goto error;
    }

    if ((tmp_space_id = H5Aget_space(attr_id)) < 0) {
        H5_FAILED();
        printf("    couldn't retrieve attribute's dataspace\n");
        goto error;
    }

    {
        hsize_t space_dims[ATTRIBUTE_GET_SPACE_TYPE_TEST_SPACE_RANK];
        htri_t  types_equal = H5Tequal(tmp_type_id, attr_dtype);

        if (types_equal < 0) {
            H5_FAILED();
            printf("    datatype was invalid\n");
            goto error;
        }

        /*
         * Disabled for now, as there seem to be issues with HDF5 comparing
         * certain datatypes
         */
#if 0
        if (!types_equal) {
            H5_FAILED();
            printf("    attribute's datatype did not match\n");
            goto error;
        }
#endif

        if (H5Sget_simple_extent_dims(tmp_space_id, space_dims, NULL) < 0)
            TEST_ERROR

        for (i = 0; i < ATTRIBUTE_GET_SPACE_TYPE_TEST_SPACE_RANK; i++)
            if (space_dims[i] != attr_dims[i]) {
                H5_FAILED();
                printf("    dataspace dims didn't match\n");
                goto error;
            }
    }

    if (H5Sclose(tmp_space_id) < 0)
        TEST_ERROR
    if (H5Sclose(attr_space_id) < 0)
        TEST_ERROR
    if (H5Tclose(tmp_type_id) < 0)
        TEST_ERROR
    if (H5Tclose(attr_dtype) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(tmp_space_id);
        H5Sclose(attr_space_id);
        H5Tclose(tmp_type_id);
        H5Tclose(attr_dtype);
        H5Aclose(attr_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_get_attribute_name(void)
{
    hsize_t dims[ATTRIBUTE_GET_NAME_TEST_SPACE_RANK];
    ssize_t name_buf_size;
    size_t  i;
    htri_t  attr_exists;
    char   *name_buf = NULL;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   attr_id         = -1;
    hid_t   attr_dtype      = -1;
    hid_t   space_id        = -1;

    TESTING("retrieve attribute name")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    for (i = 0; i < ATTRIBUTE_GET_NAME_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((space_id = H5Screate_simple(ATTRIBUTE_GET_NAME_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    if ((attr_id = H5Acreate2(container_group, ATTRIBUTE_GET_NAME_TEST_ATTRIBUTE_NAME, attr_dtype, space_id,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    /* Verify the attribute has been created */
    if ((attr_exists = H5Aexists(container_group, ATTRIBUTE_GET_NAME_TEST_ATTRIBUTE_NAME)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving size of attribute's name\n");
#endif

    /* Retrieve the name buffer size */
    if ((name_buf_size = H5Aget_name(attr_id, 0, NULL)) < 0) {
        H5_FAILED();
        printf("    couldn't retrieve name buf size\n");
        goto error;
    }

    if (NULL == (name_buf = (char *)malloc((size_t)name_buf_size + 1)))
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving attribute's name\n");
#endif

    if (H5Aget_name(attr_id, (size_t)name_buf_size + 1, name_buf) < 0) {
        H5_FAILED();
        printf("    couldn't retrieve attribute name\n");
    }

    if (strcmp(name_buf, ATTRIBUTE_GET_NAME_TEST_ATTRIBUTE_NAME)) {
        H5_FAILED();
        printf("    retrieved attribute name didn't match\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that this still works after closing and re-opening the attribute\n");
#endif

    /* Now close the attribute and verify that we can still retrieve the attribute's name after
     * opening (instead of creating) it
     */
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR

    if ((attr_id = H5Aopen(container_group, ATTRIBUTE_GET_NAME_TEST_ATTRIBUTE_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute\n");
        goto error;
    }

    if (H5Aget_name(attr_id, (size_t)name_buf_size + 1, name_buf) < 0) {
        H5_FAILED();
        printf("    couldn't retrieve attribute name\n");
        goto error;
    }

    if (strcmp(name_buf, ATTRIBUTE_GET_NAME_TEST_ATTRIBUTE_NAME)) {
        H5_FAILED();
        printf("    attribute name didn't match\n");
        goto error;
    }

    H5E_BEGIN_TRY
    {
        if (H5Aget_name_by_idx(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5_INDEX_NAME, H5_ITER_INC, 0, name_buf,
                               (size_t)name_buf_size + 1, H5P_DEFAULT) < 0) {
            H5_FAILED();
            printf("    failed to open attribute by index!\n");
            goto error;
        }
    }
    H5E_END_TRY;

    if (name_buf) {
        free(name_buf);
        name_buf = NULL;
    }

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Tclose(attr_dtype) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        if (name_buf)
            free(name_buf);
        H5Sclose(space_id);
        H5Tclose(attr_dtype);
        H5Aclose(attr_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_attribute_with_space_in_name(void)
{
    hsize_t dims[ATTRIBUTE_CREATE_WITH_SPACE_IN_NAME_SPACE_RANK];
    size_t  i;
    htri_t  attr_exists;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   attr_id         = -1;
    hid_t   attr_dtype      = -1;
    hid_t   space_id        = -1;

    TESTING("create attribute with a space in its name")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    for (i = 0; i < ATTRIBUTE_CREATE_WITH_SPACE_IN_NAME_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((space_id = H5Screate_simple(ATTRIBUTE_CREATE_WITH_SPACE_IN_NAME_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to create an attribute with a space in its name\n");
#endif

    if ((attr_id = H5Acreate2(container_group, ATTRIBUTE_CREATE_WITH_SPACE_IN_NAME_ATTR_NAME, attr_dtype,
                              space_id, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    /* Verify the attribute has been created */
    if ((attr_exists = H5Aexists(container_group, ATTRIBUTE_CREATE_WITH_SPACE_IN_NAME_ATTR_NAME)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Tclose(attr_dtype) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(space_id);
        H5Tclose(attr_dtype);
        H5Aclose(attr_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_delete_attribute(void)
{
    hsize_t dims[ATTRIBUTE_DELETION_TEST_SPACE_RANK];
    size_t  i;
    htri_t  attr_exists;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   attr_id         = -1;
    hid_t   attr_dtype      = -1;
    hid_t   space_id        = -1;

    TESTING("delete an attribute")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    for (i = 0; i < ATTRIBUTE_DELETION_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((space_id = H5Screate_simple(ATTRIBUTE_DELETION_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    /* Test H5Adelete */
    if ((attr_id = H5Acreate2(container_group, ATTRIBUTE_DELETION_TEST_ATTR_NAME, attr_dtype, space_id,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    /* Verify the attribute has been created */
    if ((attr_exists = H5Aexists(container_group, ATTRIBUTE_DELETION_TEST_ATTR_NAME)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute didn't exists\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to delete attribute with H5Adelete\n");
#endif

    /* Delete the attribute */
    if (H5Adelete(container_group, ATTRIBUTE_DELETION_TEST_ATTR_NAME) < 0) {
        H5_FAILED();
        printf("    failed to delete attribute\n");
        goto error;
    }

    /* Verify the attribute has been deleted */
    if ((attr_exists = H5Aexists(container_group, ATTRIBUTE_DELETION_TEST_ATTR_NAME)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (attr_exists) {
        H5_FAILED();
        printf("    attribute exists!\n");
        goto error;
    }

    if (H5Aclose(attr_id) < 0)
        TEST_ERROR

    /* Test H5Adelete_by_name */
    if ((attr_id = H5Acreate2(container_group, ATTRIBUTE_DELETION_TEST_ATTR_NAME, attr_dtype, space_id,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    /* Verify the attribute has been created */
    if ((attr_exists = H5Aexists(container_group, ATTRIBUTE_DELETION_TEST_ATTR_NAME)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute didn't exists\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to delete attribute with H5Adelete_by_name\n");
#endif

    /* Delete the attribute */
    if (H5Adelete_by_name(file_id, ATTRIBUTE_TEST_GROUP_NAME, ATTRIBUTE_DELETION_TEST_ATTR_NAME,
                          H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    failed to delete attribute\n");
        goto error;
    }

    /* Verify the attribute has been deleted */
    if ((attr_exists = H5Aexists(container_group, ATTRIBUTE_DELETION_TEST_ATTR_NAME)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (attr_exists) {
        H5_FAILED();
        printf("    attribute exists!\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to delete attribute with H5Adelete_by_idx\n");
#endif

    if ((attr_id = H5Acreate2(container_group, ATTRIBUTE_DELETION_TEST_ATTR_NAME, attr_dtype, space_id,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    if (H5Adelete_by_idx(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5_INDEX_CRT_ORDER, H5_ITER_DEC, 0,
                         H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Adelete_by_idx failed!\n");
        goto error;
    }

    /* Verify the attribute has been deleted */
    if ((attr_exists = H5Aexists(container_group, ATTRIBUTE_DELETION_TEST_ATTR_NAME)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (attr_exists) {
        H5_FAILED();
        printf("    attribute exists!\n");
        goto error;
    }

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Tclose(attr_dtype) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(space_id);
        H5Tclose(attr_dtype);
        H5Aclose(attr_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_write_attribute(void)
{
    hsize_t dims[ATTRIBUTE_WRITE_TEST_SPACE_RANK];
    size_t  i, data_size;
    htri_t  attr_exists;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   attr_id         = -1;
    hid_t   space_id        = -1;
    void   *data            = NULL;

    TESTING("write data to an attribute")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    for (i = 0; i < ATTRIBUTE_WRITE_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((space_id = H5Screate_simple(ATTRIBUTE_WRITE_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_id = H5Acreate2(container_group, ATTRIBUTE_WRITE_TEST_ATTR_NAME,
                              ATTRIBUTE_WRITE_TEST_ATTR_DTYPE, space_id, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    /* Verify the attribute has been created */
    if ((attr_exists = H5Aexists(container_group, ATTRIBUTE_WRITE_TEST_ATTR_NAME)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

    for (i = 0, data_size = 1; i < ATTRIBUTE_WRITE_TEST_SPACE_RANK; i++)
        data_size *= dims[i];
    data_size *= ATTRIBUTE_WRITE_TEST_ATTR_DTYPE_SIZE;

    if (NULL == (data = malloc(data_size)))
        TEST_ERROR

    for (i = 0; i < data_size / ATTRIBUTE_WRITE_TEST_ATTR_DTYPE_SIZE; i++)
        ((int *)data)[i] = (int)i;

#ifdef RV_CONNECTOR_DEBUG
    puts("Writing to the attribute\n");
#endif

    if (H5Awrite(attr_id, ATTRIBUTE_WRITE_TEST_ATTR_DTYPE, data) < 0) {
        H5_FAILED();
        printf("    couldn't write to attribute\n");
        goto error;
    }

    if (data) {
        free(data);
        data = NULL;
    }

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(space_id);
        H5Aclose(attr_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_read_attribute(void)
{
    hsize_t dims[ATTRIBUTE_READ_TEST_SPACE_RANK];
    size_t  i, data_size;
    htri_t  attr_exists;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   attr_id         = -1;
    hid_t   space_id        = -1;
    void   *data            = NULL;
    void   *read_buf        = NULL;

    TESTING("read data from an attribute")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    for (i = 0; i < ATTRIBUTE_READ_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((space_id = H5Screate_simple(ATTRIBUTE_READ_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_id = H5Acreate2(container_group, ATTRIBUTE_READ_TEST_ATTR_NAME, ATTRIBUTE_READ_TEST_ATTR_DTYPE,
                              space_id, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    /* Verify the attribute has been created */
    if ((attr_exists = H5Aexists(container_group, ATTRIBUTE_READ_TEST_ATTR_NAME)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

    for (i = 0, data_size = 1; i < ATTRIBUTE_READ_TEST_SPACE_RANK; i++)
        data_size *= dims[i];
    data_size *= ATTRIBUTE_READ_TEST_ATTR_DTYPE_SIZE;

    if (NULL == (data = malloc(data_size)))
        TEST_ERROR
    if (NULL == (read_buf = calloc(1, data_size)))
        TEST_ERROR

    for (i = 0; i < data_size / ATTRIBUTE_READ_TEST_ATTR_DTYPE_SIZE; i++)
        ((int *)data)[i] = (int)i;

#ifdef RV_CONNECTOR_DEBUG
    puts("Writing to the attribute\n");
#endif

    if (H5Awrite(attr_id, ATTRIBUTE_READ_TEST_ATTR_DTYPE, data) < 0) {
        H5_FAILED();
        printf("    couldn't write to attribute\n");
        goto error;
    }

    if (data) {
        free(data);
        data = NULL;
    }

    if (H5Aclose(attr_id) < 0)
        TEST_ERROR

    if ((attr_id = H5Aopen(container_group, ATTRIBUTE_READ_TEST_ATTR_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Reading from the attribute\n");
#endif

    if (H5Aread(attr_id, ATTRIBUTE_READ_TEST_ATTR_DTYPE, read_buf) < 0) {
        H5_FAILED();
        printf("    couldn't read from attribute\n");
        goto error;
    }

    for (i = 0; i < data_size / ATTRIBUTE_READ_TEST_ATTR_DTYPE_SIZE; i++)
        if (((int *)read_buf)[i] != (int)i) {
            H5_FAILED();
            printf("    data verification failed\n");
            goto error;
        }

    if (read_buf) {
        free(read_buf);
        read_buf = NULL;
    }

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        if (data)
            free(data);
        if (read_buf)
            free(read_buf);
        H5Sclose(space_id);
        H5Aclose(attr_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_get_number_attributes(void)
{
    H5O_info2_t obj_info;
    hsize_t     dims[ATTRIBUTE_GET_NUM_ATTRS_TEST_SPACE_RANK];
    size_t      i;
    htri_t      attr_exists;
    hid_t       file_id = -1, fapl_id = -1;
    hid_t       container_group = -1;
    hid_t       attr_id         = -1;
    hid_t       attr_dtype      = -1;
    hid_t       space_id        = -1;

    TESTING("retrieve the number of attributes on an object")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    for (i = 0; i < ATTRIBUTE_GET_NUM_ATTRS_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((space_id = H5Screate_simple(ATTRIBUTE_GET_NUM_ATTRS_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    if ((attr_id = H5Acreate2(container_group, ATTRIBUTE_GET_NUM_ATTRS_TEST_ATTRIBUTE_NAME, attr_dtype,
                              space_id, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    /* Verify the attribute has been created */
    if ((attr_exists = H5Aexists(container_group, ATTRIBUTE_GET_NUM_ATTRS_TEST_ATTRIBUTE_NAME)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to retrieve the number of attributes on a group with H5Oget_info\n");
#endif

    /* Now get the number of attributes from the group */
    if (H5Oget_info3(container_group, &obj_info, H5O_INFO_ALL) < 0) {
        H5_FAILED();
        printf("    couldn't retrieve root group info\n");
        goto error;
    }

    if (obj_info.num_attrs < 1) {
        H5_FAILED();
        printf("    invalid number of attributes received\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to retrieve the number of attributes on a group with H5Oget_info_by_name\n");
#endif

    if (H5Oget_info_by_name3(file_id, "/" ATTRIBUTE_TEST_GROUP_NAME, &obj_info, H5O_INFO_ALL, H5P_DEFAULT) <
        0) {
        H5_FAILED();
        printf("    couldn't retrieve root group info\n");
        goto error;
    }

    if (obj_info.num_attrs < 1) {
        H5_FAILED();
        printf("    invalid number of attributes received\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to retrieve the number of attributes on a group with H5Oget_info_by_idx\n");
#endif

    H5E_BEGIN_TRY
    {
        if (H5Oget_info_by_idx3(file_id, "/" ATTRIBUTE_TEST_GROUP_NAME, H5_INDEX_NAME, H5_ITER_INC, 0,
                                &obj_info, H5O_INFO_ALL, H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded!\n");
            goto error;
        }

#if 0
        if (obj_info.num_attrs < 1) {
            H5_FAILED();
            printf("    invalid number of attributes received\n");
            goto error;
        }
#endif
    }
    H5E_END_TRY;

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Tclose(attr_dtype) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(space_id);
        H5Tclose(attr_dtype);
        H5Aclose(attr_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_attribute_iterate(void)
{
    hsize_t dset_dims[ATTRIBUTE_ITERATE_TEST_DSET_SPACE_RANK];
    hsize_t attr_dims[ATTRIBUTE_ITERATE_TEST_ATTR_SPACE_RANK];
    size_t  i;
    htri_t  attr_exists;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id = -1;
    hid_t   attr_id = -1, attr_id2 = -1, attr_id3 = -1, attr_id4 = -1;
    hid_t   dset_dtype    = -1;
    hid_t   attr_dtype    = -1;
    hid_t   dset_space_id = -1;
    hid_t   attr_space_id = -1;

    TESTING("attribute iteration")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, ATTRIBUTE_ITERATE_TEST_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR
    if ((attr_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    for (i = 0; i < ATTRIBUTE_ITERATE_TEST_DSET_SPACE_RANK; i++)
        dset_dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);
    for (i = 0; i < ATTRIBUTE_ITERATE_TEST_ATTR_SPACE_RANK; i++)
        attr_dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((dset_space_id = H5Screate_simple(ATTRIBUTE_ITERATE_TEST_DSET_SPACE_RANK, dset_dims, NULL)) < 0)
        TEST_ERROR
    if ((attr_space_id = H5Screate_simple(ATTRIBUTE_ITERATE_TEST_ATTR_SPACE_RANK, attr_dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, ATTRIBUTE_ITERATE_TEST_DSET_NAME, dset_dtype, dset_space_id,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    printf("Creating attributes on dataset\n");
#endif

    if ((attr_id = H5Acreate2(dset_id, ATTRIBUTE_ITERATE_TEST_ATTR_NAME, attr_dtype, attr_space_id,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    if ((attr_id2 = H5Acreate2(dset_id, ATTRIBUTE_ITERATE_TEST_ATTR_NAME2, attr_dtype, attr_space_id,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    if ((attr_id3 = H5Acreate2(dset_id, ATTRIBUTE_ITERATE_TEST_ATTR_NAME3, attr_dtype, attr_space_id,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    if ((attr_id4 = H5Acreate2(dset_id, ATTRIBUTE_ITERATE_TEST_ATTR_NAME4, attr_dtype, attr_space_id,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    printf("Verifying that the attributes exist\n");
#endif

    /* Verify the attributes have been created */
    if ((attr_exists = H5Aexists(dset_id, ATTRIBUTE_ITERATE_TEST_ATTR_NAME)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

    if ((attr_exists = H5Aexists(dset_id, ATTRIBUTE_ITERATE_TEST_ATTR_NAME2)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

    if ((attr_exists = H5Aexists(dset_id, ATTRIBUTE_ITERATE_TEST_ATTR_NAME3)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

    if ((attr_exists = H5Aexists(dset_id, ATTRIBUTE_ITERATE_TEST_ATTR_NAME4)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    printf("Iterating over attributes by attribute name in increasing order with H5Aiterate2\n");
#endif

    /* Test basic attribute iteration capability using both index types and both index orders */
    if (H5Aiterate2(dset_id, H5_INDEX_NAME, H5_ITER_INC, NULL, attr_iter_callback1, NULL) < 0) {
        H5_FAILED();
        printf("    H5Aiterate2 by index type name in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    printf("Iterating over attributes by attribute name in decreasing order with H5Aiterate2\n");
#endif

    if (H5Aiterate2(dset_id, H5_INDEX_NAME, H5_ITER_DEC, NULL, attr_iter_callback1, NULL) < 0) {
        H5_FAILED();
        printf("    H5Aiterate2 by index type name in decreasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    printf("Iterating over attributes by creation order in increasing order with H5Aiterate2\n");
#endif

    if (H5Aiterate2(dset_id, H5_INDEX_CRT_ORDER, H5_ITER_INC, NULL, attr_iter_callback1, NULL) < 0) {
        H5_FAILED();
        printf("    H5Aiterate2 by index type creation order in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    printf("Iterating over attributes by creation order in decreasing order with H5Aiterate2\n");
#endif

    if (H5Aiterate2(dset_id, H5_INDEX_CRT_ORDER, H5_ITER_DEC, NULL, attr_iter_callback1, NULL) < 0) {
        H5_FAILED();
        printf("    H5Aiterate2 by index type creation order in decreasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    printf("Iterating over attributes by attribute name in increasing order with H5Aiterate_by_name\n");
#endif

    if (H5Aiterate_by_name(file_id,
                           "/" ATTRIBUTE_TEST_GROUP_NAME "/" ATTRIBUTE_ITERATE_TEST_SUBGROUP_NAME
                           "/" ATTRIBUTE_ITERATE_TEST_DSET_NAME,
                           H5_INDEX_NAME, H5_ITER_INC, NULL, attr_iter_callback1, NULL, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Aiterate_by_name by index type name in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    printf("Iterating over attributes by attribute name in decreasing order with H5Aiterate_by_name\n");
#endif

    if (H5Aiterate_by_name(file_id,
                           "/" ATTRIBUTE_TEST_GROUP_NAME "/" ATTRIBUTE_ITERATE_TEST_SUBGROUP_NAME
                           "/" ATTRIBUTE_ITERATE_TEST_DSET_NAME,
                           H5_INDEX_NAME, H5_ITER_DEC, NULL, attr_iter_callback1, NULL, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Aiterate_by_name by index type name in decreasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    printf("Iterating over attributes by creation order in increasing order with H5Aiterate_by_name\n");
#endif

    if (H5Aiterate_by_name(file_id,
                           "/" ATTRIBUTE_TEST_GROUP_NAME "/" ATTRIBUTE_ITERATE_TEST_SUBGROUP_NAME
                           "/" ATTRIBUTE_ITERATE_TEST_DSET_NAME,
                           H5_INDEX_CRT_ORDER, H5_ITER_INC, NULL, attr_iter_callback1, NULL,
                           H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Aiterate_by_name by index type creation order in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    printf("Iterating over attributes by creation order in decreasing order with H5Aiterate_by_name\n");
#endif

    if (H5Aiterate_by_name(file_id,
                           "/" ATTRIBUTE_TEST_GROUP_NAME "/" ATTRIBUTE_ITERATE_TEST_SUBGROUP_NAME
                           "/" ATTRIBUTE_ITERATE_TEST_DSET_NAME,
                           H5_INDEX_CRT_ORDER, H5_ITER_DEC, NULL, attr_iter_callback1, NULL,
                           H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Aiterate_by_name by index type creation order in decreasing order failed\n");
        goto error;
    }

    /* XXX: Test the H5Aiterate index-saving capabilities */

    if (H5Sclose(dset_space_id) < 0)
        TEST_ERROR
    if (H5Sclose(attr_space_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Tclose(attr_dtype) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id2) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id3) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id4) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(dset_space_id);
        H5Sclose(attr_space_id);
        H5Tclose(dset_dtype);
        H5Tclose(attr_dtype);
        H5Aclose(attr_id);
        H5Aclose(attr_id2);
        H5Aclose(attr_id3);
        H5Aclose(attr_id4);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
    }
    H5E_END_TRY;

    return 1;
}

static int
test_attribute_iterate_0_attributes(void)
{
    hsize_t dset_dims[ATTRIBUTE_ITERATE_TEST_0_ATTRIBUTES_DSET_SPACE_RANK];
    size_t  i;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id       = -1;
    hid_t   dset_dtype    = -1;
    hid_t   dset_space_id = -1;

    TESTING("attribute iteration on object with 0 attributes")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, ATTRIBUTE_ITERATE_TEST_0_ATTRIBUTES_SUBGROUP_NAME,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    for (i = 0; i < ATTRIBUTE_ITERATE_TEST_0_ATTRIBUTES_DSET_SPACE_RANK; i++)
        dset_dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((dset_space_id =
             H5Screate_simple(ATTRIBUTE_ITERATE_TEST_0_ATTRIBUTES_DSET_SPACE_RANK, dset_dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, ATTRIBUTE_ITERATE_TEST_0_ATTRIBUTES_DSET_NAME, dset_dtype,
                              dset_space_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if (H5Aiterate2(dset_id, H5_INDEX_NAME, H5_ITER_INC, NULL, attr_iter_callback2, NULL) < 0) {
        H5_FAILED();
        printf("    H5Aiterate2 by index type name in increasing order failed\n");
        goto error;
    }

    if (H5Sclose(dset_space_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(dset_space_id);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_unused_attribute_API_calls(void)
{
    hsize_t attr_dims[ATTRIBUTE_UNUSED_APIS_TEST_SPACE_RANK];
    size_t  i;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   attr_id         = -1;
    hid_t   attr_dtype      = -1;
    hid_t   attr_space_id   = -1;

    TESTING("unused attribute API calls")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    for (i = 0; i < ATTRIBUTE_UNUSED_APIS_TEST_SPACE_RANK; i++)
        attr_dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((attr_space_id = H5Screate_simple(ATTRIBUTE_UNUSED_APIS_TEST_SPACE_RANK, attr_dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    if ((attr_id = H5Acreate2(container_group, ATTRIBUTE_UNUSED_APIS_TEST_ATTR_NAME, attr_dtype,
                              attr_space_id, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Testing that all of the unused attribute API calls don't cause application issues\n");
#endif

    H5E_BEGIN_TRY
    {
        if (H5Aget_storage_size(attr_id) > 0)
            TEST_ERROR
    }
    H5E_END_TRY;

    if (H5Sclose(attr_space_id) < 0)
        TEST_ERROR
    if (H5Tclose(attr_dtype) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(attr_space_id);
        H5Tclose(attr_dtype);
        H5Aclose(attr_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_attribute_property_lists(void)
{
    H5T_cset_t encoding = H5T_CSET_UTF8;
    hsize_t    dims[ATTRIBUTE_PROPERTY_LIST_TEST_SPACE_RANK];
    size_t     i;
    htri_t     attr_exists;
    hid_t      file_id = -1, fapl_id = -1;
    hid_t      container_group = -1, group_id = -1;
    hid_t      attr_id1 = -1, attr_id2 = -1;
    hid_t      attr_dtype1 = -1, attr_dtype2 = -1;
    hid_t      acpl_id1 = -1, acpl_id2 = -1;
    hid_t      space_id = -1;

    TESTING("attribute property list operations")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, ATTRIBUTE_PROPERTY_LIST_TEST_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    for (i = 0; i < ATTRIBUTE_PROPERTY_LIST_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((space_id = H5Screate_simple(ATTRIBUTE_PROPERTY_LIST_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_dtype1 = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR
    if ((attr_dtype2 = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    if ((acpl_id1 = H5Pcreate(H5P_ATTRIBUTE_CREATE)) < 0) {
        H5_FAILED();
        printf("    couldn't create ACPL\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Setting property on ACPL\n");
#endif

    if (H5Pset_char_encoding(acpl_id1, encoding) < 0) {
        H5_FAILED();
        printf("    couldn't set ACPL property value\n");
        goto error;
    }

    if ((attr_id1 = H5Acreate2(group_id, ATTRIBUTE_PROPERTY_LIST_TEST_ATTRIBUTE_NAME1, attr_dtype1, space_id,
                               acpl_id1, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    if ((attr_id2 = H5Acreate2(group_id, ATTRIBUTE_PROPERTY_LIST_TEST_ATTRIBUTE_NAME2, attr_dtype2, space_id,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    if (H5Pclose(acpl_id1) < 0)
        TEST_ERROR

    /* Verify the attributes have been created */
    if ((attr_exists = H5Aexists(group_id, ATTRIBUTE_PROPERTY_LIST_TEST_ATTRIBUTE_NAME1)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

    if ((attr_exists = H5Aexists(group_id, ATTRIBUTE_PROPERTY_LIST_TEST_ATTRIBUTE_NAME2)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

    /* Try to retrieve copies of the two property lists, one which ahs the property set and one which does not
     */
    if ((acpl_id1 = H5Aget_create_plist(attr_id1)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if ((acpl_id2 = H5Aget_create_plist(attr_id2)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    /* Ensure that property list 1 has the property list set and property list 2 does not */
    encoding = H5T_CSET_ERROR;

    if (H5Pget_char_encoding(acpl_id1, &encoding) < 0) {
        H5_FAILED();
        printf("    couldn't retrieve ACPL property value\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Checking that property set on ACPL was retrieved correctly\n");
#endif

    if (H5T_CSET_UTF8 != encoding) {
        H5_FAILED();
        printf("   ACPL property value was incorrect\n");
        goto error;
    }

    encoding = H5T_CSET_ERROR;

    if (H5Pget_char_encoding(acpl_id2, &encoding) < 0) {
        H5_FAILED();
        printf("    couldn't retrieve ACPL property value\n");
        goto error;
    }

    if (H5T_CSET_UTF8 == encoding) {
        H5_FAILED();
        printf("    ACPL property value was set!\n");
        goto error;
    }

    /* Now close the property lists and attribute and see if we can still retrieve copies of
     * the property lists upon opening (instead of creating) an attribute
     */
    if (H5Pclose(acpl_id1) < 0)
        TEST_ERROR
    if (H5Pclose(acpl_id2) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id1) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id2) < 0)
        TEST_ERROR

    if ((attr_id1 = H5Aopen(group_id, ATTRIBUTE_PROPERTY_LIST_TEST_ATTRIBUTE_NAME1, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute\n");
        goto error;
    }

    if ((attr_id2 = H5Aopen(group_id, ATTRIBUTE_PROPERTY_LIST_TEST_ATTRIBUTE_NAME2, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute\n");
        goto error;
    }

    if ((acpl_id1 = H5Aget_create_plist(attr_id1)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if ((acpl_id2 = H5Aget_create_plist(attr_id2)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if (H5Pclose(acpl_id1) < 0)
        TEST_ERROR
    if (H5Pclose(acpl_id2) < 0)
        TEST_ERROR
    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Tclose(attr_dtype1) < 0)
        TEST_ERROR
    if (H5Tclose(attr_dtype2) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id1) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id2) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Pclose(acpl_id1);
        H5Pclose(acpl_id2);
        H5Sclose(space_id);
        H5Tclose(attr_dtype1);
        H5Tclose(attr_dtype2);
        H5Aclose(attr_id1);
        H5Aclose(attr_id2);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

/*****************************************************
 *                                                   *
 *               Plugin Dataset tests                *
 *                                                   *
 *****************************************************/

static int
test_create_dataset_under_root(void)
{
    hsize_t dims[DATASET_CREATE_UNDER_ROOT_SPACE_RANK];
    size_t  i;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   dset_id    = -1;
    hid_t   dset_dtype = -1;
    hid_t   fspace_id  = -1;

    TESTING("create dataset under root group")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    for (i = 0; i < DATASET_CREATE_UNDER_ROOT_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((fspace_id = H5Screate_simple(DATASET_CREATE_UNDER_ROOT_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating a dataset under the root group\n");
#endif

    /* Create the Dataset under the root group of the file */
    if ((dset_id = H5Dcreate2(file_id, DATASET_CREATE_UNDER_ROOT_DSET_NAME, dset_dtype, fspace_id,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(fspace_id);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_anonymous_dataset(void)
{
    hsize_t dims[DATASET_CREATE_ANONYMOUS_SPACE_RANK];
    size_t  i;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   dset_id         = -1;
    hid_t   dset_dtype      = -1;
    hid_t   fspace_id       = -1;

    TESTING("create anonymous dataset")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    for (i = 0; i < DATASET_CREATE_ANONYMOUS_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((fspace_id = H5Screate_simple(DATASET_CREATE_ANONYMOUS_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating an anonymous dataset\n");
#endif

    if ((dset_id = H5Dcreate_anon(container_group, dset_dtype, fspace_id, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Linking the anonymous dataset into the file structure\n");
#endif

    if (H5Olink(dset_id, container_group, DATASET_CREATE_ANONYMOUS_DATASET_NAME, H5P_DEFAULT, H5P_DEFAULT) <
        0) {
        H5_FAILED();
        printf("    couldn't link anonymous dataset into file structure\n");
        goto error;
    }

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(fspace_id);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_dataset_under_existing_group(void)
{
    hsize_t dims[DATASET_CREATE_UNDER_EXISTING_SPACE_RANK];
    size_t  i;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   group_id   = -1;
    hid_t   dset_id    = -1;
    hid_t   dset_dtype = -1;
    hid_t   fspace_id  = -1;

    TESTING("create dataset under existing group")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((group_id = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open group\n");
        goto error;
    }

    for (i = 0; i < DATASET_CREATE_UNDER_EXISTING_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((fspace_id = H5Screate_simple(DATASET_CREATE_UNDER_EXISTING_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating dataset under non-root group\n");
#endif

    if ((dset_id = H5Dcreate2(group_id, DATASET_CREATE_UNDER_EXISTING_DSET_NAME, dset_dtype, fspace_id,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(fspace_id);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_dataset_null_space(void)
{
    hid_t file_id = -1, fapl_id = -1;
    hid_t container_group = -1, group_id = -1;
    hid_t dset_id    = -1;
    hid_t dset_dtype = -1;
    hid_t fspace_id  = -1;

    TESTING("create dataset with a NULL dataspace")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, DATASET_CREATE_NULL_DATASPACE_TEST_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

    if ((fspace_id = H5Screate(H5S_NULL)) < 0)
        TEST_ERROR

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    printf("Creating dataset with NULL dataspace\n");
#endif

    if ((dset_id = H5Dcreate2(group_id, DATASET_CREATE_NULL_DATASPACE_TEST_DSET_NAME, dset_dtype, fspace_id,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if (H5Dclose(dset_id) < 0)
        TEST_ERROR

    if ((dset_id = H5Dopen2(group_id, DATASET_CREATE_NULL_DATASPACE_TEST_DSET_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset\n");
        goto error;
    }

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(fspace_id);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_dataset_scalar_space(void)
{
    hid_t file_id = -1, fapl_id = -1;
    hid_t container_group = -1, group_id = -1;
    hid_t dset_id    = -1;
    hid_t dset_dtype = -1;
    hid_t fspace_id  = -1;

    TESTING("create dataset with a SCALAR dataspace")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, DATASET_CREATE_SCALAR_DATASPACE_TEST_SUBGROUP_NAME,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

    if ((fspace_id = H5Screate(H5S_SCALAR)) < 0)
        TEST_ERROR

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    printf("Creating dataset with SCALAR dataspace\n");
#endif

    if ((dset_id = H5Dcreate2(group_id, DATASET_CREATE_SCALAR_DATASPACE_TEST_DSET_NAME, dset_dtype, fspace_id,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if (H5Dclose(dset_id) < 0)
        TEST_ERROR

    if ((dset_id = H5Dopen2(group_id, DATASET_CREATE_SCALAR_DATASPACE_TEST_DSET_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset\n");
        goto error;
    }

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(fspace_id);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_dataset_predefined_types(void)
{
    size_t i;
    hid_t  file_id = -1, fapl_id = -1;
    hid_t  container_group = -1, group_id = -1;
    hid_t  fspace_id                    = -1;
    hid_t  dset_id                      = -1;
    hid_t  predefined_type_test_table[] = {H5T_STD_U8LE,   H5T_STD_U8BE,   H5T_STD_I8LE,   H5T_STD_I8BE,
                                          H5T_STD_U16LE,  H5T_STD_U16BE,  H5T_STD_I16LE,  H5T_STD_I16BE,
                                          H5T_STD_U32LE,  H5T_STD_U32BE,  H5T_STD_I32LE,  H5T_STD_I32BE,
                                          H5T_STD_U64LE,  H5T_STD_U64BE,  H5T_STD_I64LE,  H5T_STD_I64BE,
                                          H5T_IEEE_F32LE, H5T_IEEE_F32BE, H5T_IEEE_F64LE, H5T_IEEE_F64BE};

    TESTING("dataset creation w/ predefined datatypes")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, DATASET_PREDEFINED_TYPE_TEST_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create sub-container group\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating datasets with the different predefined integer/floating-point datatypes\n");
#endif

    for (i = 0; i < ARRAY_LENGTH(predefined_type_test_table); i++) {
        hsize_t dims[DATASET_PREDEFINED_TYPE_TEST_SPACE_RANK];
        size_t  j;
        char    name[100];

        for (j = 0; j < DATASET_PREDEFINED_TYPE_TEST_SPACE_RANK; j++)
            dims[j] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

        if ((fspace_id = H5Screate_simple(DATASET_PREDEFINED_TYPE_TEST_SPACE_RANK, dims, NULL)) < 0)
            TEST_ERROR

        sprintf(name, "%s%zu", DATASET_PREDEFINED_TYPE_TEST_BASE_NAME, i);

        if ((dset_id = H5Dcreate2(group_id, name, predefined_type_test_table[i], fspace_id, H5P_DEFAULT,
                                  H5P_DEFAULT, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't create dataset\n");
            goto error;
        }

        if (H5Sclose(fspace_id) < 0)
            TEST_ERROR
        if (H5Dclose(dset_id) < 0)
            TEST_ERROR

        if ((dset_id = H5Dopen2(group_id, name, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    failed to open dataset\n");
            goto error;
        }

        if (H5Dclose(dset_id) < 0)
            TEST_ERROR
    }

    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_dataset_string_types(void)
{
    hsize_t dims[DATASET_STRING_TYPE_TEST_SPACE_RANK];
    size_t  i;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id_fixed = -1, dset_id_variable = -1;
    hid_t   type_id_fixed = -1, type_id_variable = -1;
    hid_t   fspace_id = -1;

    TESTING("dataset creation w/ string types")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, DATASET_STRING_TYPE_TEST_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    if ((type_id_fixed = H5Tcreate(H5T_STRING, DATASET_STRING_TYPE_TEST_STRING_LENGTH)) < 0) {
        H5_FAILED();
        printf("    couldn't create fixed-length string type\n");
        goto error;
    }

    if ((type_id_variable = H5Tcreate(H5T_STRING, H5T_VARIABLE)) < 0) {
        H5_FAILED();
        printf("    couldn't create variable-length string type\n");
        goto error;
    }

    for (i = 0; i < DATASET_STRING_TYPE_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((fspace_id = H5Screate_simple(DATASET_STRING_TYPE_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating dataset with fixed-length string datatype\n");
#endif

    if ((dset_id_fixed = H5Dcreate2(group_id, DATASET_STRING_TYPE_TEST_DSET_NAME1, type_id_fixed, fspace_id,
                                    H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create fixed-length string dataset\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating dataset with variable-length string datatype\n");
#endif

    if ((dset_id_variable = H5Dcreate2(group_id, DATASET_STRING_TYPE_TEST_DSET_NAME2, type_id_variable,
                                       fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create variable-length string dataset\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to re-open the datasets\n");
#endif

    if (H5Dclose(dset_id_fixed) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id_variable) < 0)
        TEST_ERROR

    if ((dset_id_fixed = H5Dopen2(group_id, DATASET_STRING_TYPE_TEST_DSET_NAME1, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    failed to open dataset\n");
        goto error;
    }

    if ((dset_id_variable = H5Dopen2(group_id, DATASET_STRING_TYPE_TEST_DSET_NAME2, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    failed to opend dataset\n");
        goto error;
    }

    if (H5Tclose(type_id_fixed) < 0)
        TEST_ERROR
    if (H5Tclose(type_id_variable) < 0)
        TEST_ERROR
    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id_fixed) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id_variable) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Tclose(type_id_fixed);
        H5Tclose(type_id_variable);
        H5Sclose(fspace_id);
        H5Dclose(dset_id_fixed);
        H5Dclose(dset_id_variable);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_dataset_compound_types(void)
{
    hsize_t dims[DATASET_COMPOUND_TYPE_TEST_DSET_RANK];
    size_t  i, j;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   compound_type = -1;
    hid_t   dset_id       = -1;
    hid_t   fspace_id     = -1;
    hid_t   type_pool[DATASET_COMPOUND_TYPE_TEST_MAX_SUBTYPES];
    int     num_passes;

    TESTING("dataset creation w/ compound datatypes")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, DATASET_COMPOUND_TYPE_TEST_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    for (i = 0; i < DATASET_COMPOUND_TYPE_TEST_DSET_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((fspace_id = H5Screate_simple(DATASET_COMPOUND_TYPE_TEST_DSET_RANK, dims, NULL)) < 0)
        TEST_ERROR

    num_passes = (rand() % DATASET_COMPOUND_TYPE_TEST_MAX_PASSES) + 1;

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating datasets with a variety of randomly-generated compound datatypes\n");
#endif

    for (i = 0; i < (size_t)num_passes; i++) {
        size_t num_subtypes;
        size_t compound_size = 0;
        size_t next_offset   = 0;
        char   dset_name[256];

        num_subtypes = (size_t)(rand() % DATASET_COMPOUND_TYPE_TEST_MAX_SUBTYPES) + 1;

        for (j = 0; j < num_subtypes; j++)
            type_pool[j] = -1;

        if ((compound_type = H5Tcreate(H5T_COMPOUND, 1)) < 0) {
            H5_FAILED();
            printf("    couldn't create compound datatype\n");
            goto error;
        }

        /* Start adding subtypes to the compound type */
        for (j = 0; j < num_subtypes; j++) {
            size_t member_size;
            char   member_name[256];

            snprintf(member_name, 256, "member%zu", j);

            if ((type_pool[j] = generate_random_datatype(H5T_NO_CLASS)) < 0) {
                H5_FAILED();
                printf("    couldn't create compound datatype member %zu\n", j);
                goto error;
            }

            if ((member_size = H5Tget_size(type_pool[j])) < 0) {
                H5_FAILED();
                printf("    couldn't get compound member %zu size\n", j);
                goto error;
            }

            compound_size += member_size;

            if (H5Tset_size(compound_type, compound_size) < 0)
                TEST_ERROR

            if (H5Tinsert(compound_type, member_name, next_offset, type_pool[j]) < 0)
                TEST_ERROR

            next_offset += member_size;
        }

        if (H5Tpack(compound_type) < 0)
            TEST_ERROR

        snprintf(dset_name, sizeof(dset_name), "%s%zu", DATASET_COMPOUND_TYPE_TEST_DSET_NAME, i);

        if ((dset_id = H5Dcreate2(group_id, dset_name, compound_type, fspace_id, H5P_DEFAULT, H5P_DEFAULT,
                                  H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't create dataset\n");
            goto error;
        }

        if (H5Dclose(dset_id) < 0)
            TEST_ERROR

        if ((dset_id = H5Dopen2(group_id, dset_name, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    failed to open dataset\n");
            goto error;
        }

        for (j = 0; j < num_subtypes; j++)
            if (type_pool[j] >= 0 && H5Tclose(type_pool[j]) < 0)
                TEST_ERROR
        if (H5Tclose(compound_type) < 0)
            TEST_ERROR
        if (H5Dclose(dset_id) < 0)
            TEST_ERROR
    }

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        for (i = 0; i < DATASET_COMPOUND_TYPE_TEST_MAX_SUBTYPES; i++)
            H5Tclose(type_pool[i]);
        H5Tclose(compound_type);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_dataset_enum_types(void)
{
    hsize_t     dims[DATASET_ENUM_TYPE_TEST_SPACE_RANK];
    size_t      i;
    hid_t       file_id = -1, fapl_id = -1;
    hid_t       container_group = -1, group_id = -1;
    hid_t       dset_id_native = -1, dset_id_non_native = -1;
    hid_t       fspace_id   = -1;
    hid_t       enum_native = -1, enum_non_native = -1;
    const char *enum_type_test_table[] = {"RED",    "GREEN",  "BLUE",   "BLACK", "WHITE",
                                          "PURPLE", "ORANGE", "YELLOW", "BROWN"};

    TESTING("dataset creation w/ enum types")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, DATASET_ENUM_TYPE_TEST_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    if ((enum_native = H5Tcreate(H5T_ENUM, sizeof(int))) < 0) {
        H5_FAILED();
        printf("    couldn't create native enum type\n");
        goto error;
    }

    for (i = 0; i < ARRAY_LENGTH(enum_type_test_table); i++)
        if (H5Tenum_insert(enum_native, enum_type_test_table[i], &i) < 0)
            TEST_ERROR

    if ((enum_non_native = H5Tenum_create(H5T_STD_U32LE)) < 0) {
        H5_FAILED();
        printf("    couldn't create non-native enum type\n");
        goto error;
    }

    for (i = 0; i < DATASET_ENUM_TYPE_TEST_NUM_MEMBERS; i++) {
        char val_name[15];

        sprintf(val_name, "%s%zu", DATASET_ENUM_TYPE_TEST_VAL_BASE_NAME, i);

        if (H5Tenum_insert(enum_non_native, val_name, &i) < 0)
            TEST_ERROR
    }

    for (i = 0; i < DATASET_ENUM_TYPE_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((fspace_id = H5Screate_simple(DATASET_ENUM_TYPE_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating dataset with native enum datatype\n");
#endif

    if ((dset_id_native = H5Dcreate2(group_id, DATASET_ENUM_TYPE_TEST_DSET_NAME1, enum_native, fspace_id,
                                     H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create native enum dataset\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating dataset with non-native enum datatype\n");
#endif

    if ((dset_id_non_native = H5Dcreate2(group_id, DATASET_ENUM_TYPE_TEST_DSET_NAME2, enum_non_native,
                                         fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create non-native enum dataset\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to re-open the datasets\n");
#endif

    if (H5Dclose(dset_id_native) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id_non_native) < 0)
        TEST_ERROR

    if ((dset_id_native = H5Dopen2(group_id, DATASET_ENUM_TYPE_TEST_DSET_NAME1, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    failed to open dataset\n");
        goto error;
    }

    if ((dset_id_non_native = H5Dopen2(group_id, DATASET_ENUM_TYPE_TEST_DSET_NAME2, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    failed to open dataset\n");
        goto error;
    }

    if (H5Tclose(enum_native) < 0)
        TEST_ERROR
    if (H5Tclose(enum_non_native) < 0)
        TEST_ERROR
    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id_native) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id_non_native) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Tclose(enum_native);
        H5Tclose(enum_non_native);
        H5Sclose(fspace_id);
        H5Dclose(dset_id_native);
        H5Dclose(dset_id_non_native);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_dataset_array_types(void)
{
    hsize_t dset_dims[DATASET_ARRAY_TYPE_TEST_SPACE_RANK];
    hsize_t array_dims1[DATASET_ARRAY_TYPE_TEST_RANK1];
    hsize_t array_dims2[DATASET_ARRAY_TYPE_TEST_RANK2];
#if 0
    hsize_t array_dims3[DATASET_ARRAY_TYPE_TEST_RANK3];
#endif
    size_t i;
    hid_t  file_id = -1, fapl_id = -1;
    hid_t  container_group = -1, group_id = -1;
    hid_t  dset_id1 = -1, dset_id2 = -1;
    hid_t  fspace_id      = -1;
    hid_t  array_type_id1 = -1, array_type_id2 = -1;
    hid_t  array_base_type_id1 = -1, array_base_type_id2 = -1;
#if 0
    hid_t   array_base_type_id3 = -1;
    hid_t   array_type_id3 = -1;
    hid_t   nested_type_id = -1;
    hid_t   dset_id3 = -1;
#endif
    hid_t non_predefined_type_id = -1;

    TESTING("dataset creation w/ array types")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, DATASET_ARRAY_TYPE_TEST_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    /* Test creation of array with some different types */
    for (i = 0; i < DATASET_ARRAY_TYPE_TEST_RANK1; i++)
        array_dims1[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((array_base_type_id1 = generate_random_datatype(H5T_ARRAY)) < 0)
        TEST_ERROR

    if ((array_type_id1 = H5Tarray_create(array_base_type_id1, DATASET_ARRAY_TYPE_TEST_RANK1, array_dims1)) <
        0) {
        H5_FAILED();
        printf("    couldn't create predefined integer array type\n");
        goto error;
    }

    for (i = 0; i < DATASET_ARRAY_TYPE_TEST_RANK2; i++)
        array_dims2[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((array_base_type_id2 = generate_random_datatype(H5T_ARRAY)) < 0)
        TEST_ERROR

    if ((array_type_id2 = H5Tarray_create(array_base_type_id2, DATASET_ARRAY_TYPE_TEST_RANK2, array_dims2)) <
        0) {
        H5_FAILED();
        printf("    couldn't create predefined floating-point array type\n");
        goto error;
    }

#if 0
    /* Test nested arrays */
    for (i = 0; i < DATASET_ARRAY_TYPE_TEST_RANK4; i++)
        array_dims3[i] = (hsize_t) (rand() % MAX_DIM_SIZE + 1);

    if ((array_base_type_id3 = generate_random_datatype(H5T_ARRAY)) < 0)
        TEST_ERROR

    if ((nested_type_id = H5Tarray_create(array_base_type_id3, DATASET_ARRAY_TYPE_TEST_RANK3, array_dims3)) < 0) {
        H5_FAILED();
        printf("    couldn't create nested array base type\n");
        goto error;
    }

    if ((array_type_id3 = H5Tarray_create(nested_type_id, DATASET_ARRAY_TYPE_TEST_RANK3, array_dims3)) < 0) {
        H5_FAILED();
        printf("    couldn't create nested array type\n");
        goto error;
    }
#endif

    for (i = 0; i < DATASET_ARRAY_TYPE_TEST_SPACE_RANK; i++)
        dset_dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((fspace_id = H5Screate_simple(DATASET_ARRAY_TYPE_TEST_SPACE_RANK, dset_dims, NULL)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating datasets with variet of randomly-generated array datatypes\n");
#endif

    if ((dset_id1 = H5Dcreate2(group_id, DATASET_ARRAY_TYPE_TEST_DSET_NAME1, array_type_id1, fspace_id,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create array type dataset\n");
        goto error;
    }

    if ((dset_id2 = H5Dcreate2(group_id, DATASET_ARRAY_TYPE_TEST_DSET_NAME2, array_type_id2, fspace_id,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create array type dataset\n");
        goto error;
    }

#if 0
    if ((dset_id3 = H5Dcreate2(group_id, DATASET_ARRAY_TYPE_TEST_DSET_NAME3, array_type_id3, fspace_id,
            H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create nested array type dataset\n");
        goto error;
    }
#endif

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to re-open the datasets\n");
#endif

    if (H5Dclose(dset_id1) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id2) < 0)
        TEST_ERROR
#if 0
    if (H5Dclose(dset_id3) < 0)
        TEST_ERROR
#endif

    if ((dset_id1 = H5Dopen2(group_id, DATASET_ARRAY_TYPE_TEST_DSET_NAME1, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    failed to open dataset\n");
        goto error;
    }

    if ((dset_id2 = H5Dopen2(group_id, DATASET_ARRAY_TYPE_TEST_DSET_NAME2, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    failed to open dataset\n");
        goto error;
    }

#if 0
    if ((dset_id3 = H5Dopen2(group_id, DATASET_ARRAY_TYPE_TEST_DSET_NAME3, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    failed to open dataset\n");
        goto error;
    }
#endif

    if (H5Tclose(array_type_id1) < 0)
        TEST_ERROR
    if (H5Tclose(array_type_id2) < 0)
        TEST_ERROR
#if 0
    if (H5Tclose(array_type_id3) < 0)
        TEST_ERROR
    if (H5Tclose(nested_type_id) < 0)
        TEST_ERROR
#endif
    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id1) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id2) < 0)
        TEST_ERROR
#if 0
    if (H5Dclose(dset_id3) < 0)
        TEST_ERROR
#endif
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Tclose(array_type_id1);
        H5Tclose(array_type_id2);
#if 0
        H5Tclose(array_type_id3);
        H5Tclose(nested_type_id);
#endif
        H5Tclose(non_predefined_type_id);
        H5Sclose(fspace_id);
        H5Dclose(dset_id1);
        H5Dclose(dset_id2);
#if 0
        H5Dclose(dset_id3);
#endif
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_dataset_shapes(void)
{
    hsize_t *dims = NULL;
    size_t   i;
    hid_t    file_id = -1, fapl_id = -1;
    hid_t    container_group = -1, group_id = -1;
    hid_t    dset_id = -1, space_id = -1;
    hid_t    dset_dtype = -1;

    TESTING("dataset creation w/ random dimension sizes")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, DATASET_SHAPE_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT,
                               H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating datasets with a variety of randomly-generated dataspace shapes\n");
#endif

    for (i = 0; i < DATASET_SHAPE_TEST_NUM_ITERATIONS; i++) {
        size_t j;
        char   name[100];
        int    ndims = rand() % DATASET_SHAPE_TEST_MAX_DIMS + 1;

        if (NULL == (dims = (hsize_t *)malloc((size_t)ndims * sizeof(*dims)))) {
            H5_FAILED();
            printf("    couldn't allocate space for dataspace dimensions\n");
            goto error;
        }

        for (j = 0; j < (size_t)ndims; j++)
            dims[j] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

        if ((space_id = H5Screate_simple(ndims, dims, NULL)) < 0) {
            H5_FAILED();
            printf("    couldn't create dataspace\n");
            goto error;
        }

        sprintf(name, "%s%zu", DATASET_SHAPE_TEST_DSET_BASE_NAME, i + 1);

        if ((dset_id = H5Dcreate2(group_id, name, dset_dtype, space_id, H5P_DEFAULT, H5P_DEFAULT,
                                  H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't create dataset\n");
            goto error;
        }

        if (dims) {
            free(dims);
            dims = NULL;
        }

        if (H5Sclose(space_id) < 0)
            TEST_ERROR
        if (H5Dclose(dset_id) < 0)
            TEST_ERROR
    }

    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        if (dims)
            free(dims);
        H5Sclose(space_id);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_dataset_creation_properties(void)
{
    hsize_t dims[DATASET_CREATION_PROPERTIES_TEST_SHAPE_RANK];
    size_t  i;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id = -1, dcpl_id = -1;
    hid_t   dset_dtype = -1;
    hid_t   fspace_id  = -1;

    TESTING("dataset creation properties")
    SKIPPED()
    return 0;

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, DATASET_CREATION_PROPERTIES_TEST_GROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group\n");
        goto error;
    }

    for (i = 0; i < DATASET_CREATION_PROPERTIES_TEST_SHAPE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((fspace_id = H5Screate_simple(DATASET_CREATION_PROPERTIES_TEST_SHAPE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating a variety of datasets with different creation properties\n");
#endif

    /* Test the alloc time property */
    {
        H5D_alloc_time_t alloc_times[] = {H5D_ALLOC_TIME_DEFAULT, H5D_ALLOC_TIME_EARLY, H5D_ALLOC_TIME_INCR,
                                          H5D_ALLOC_TIME_LATE};

#ifdef RV_CONNECTOR_DEBUG
        puts("Testing the alloc time property\n");
#endif

        if ((dcpl_id = H5Pcreate(H5P_DATASET_CREATE)) < 0)
            TEST_ERROR

        for (i = 0; i < ARRAY_LENGTH(alloc_times); i++) {
            char name[100];

            if (H5Pset_alloc_time(dcpl_id, alloc_times[i]) < 0)
                TEST_ERROR

            sprintf(name, "%s%zu", DATASET_CREATION_PROPERTIES_TEST_ALLOC_TIMES_BASE_NAME, i);

            if ((dset_id = H5Dcreate2(group_id, name, dset_dtype, fspace_id, H5P_DEFAULT, dcpl_id,
                                      H5P_DEFAULT)) < 0) {
                H5_FAILED();
                printf("    couldn't create dataset\n");
                goto error;
            }

            if (H5Dclose(dset_id) < 0)
                TEST_ERROR

            if ((dset_id = H5Dopen2(group_id, name, H5P_DEFAULT)) < 0) {
                H5_FAILED();
                printf("    couldn't open dataset\n");
                goto error;
            }

            if (H5Dclose(dset_id) < 0)
                TEST_ERROR
        }

        if (H5Pclose(dcpl_id) < 0)
            TEST_ERROR
    }

    /* Test the attribute creation order property */
    {
        unsigned creation_orders[] = {H5P_CRT_ORDER_TRACKED, H5P_CRT_ORDER_TRACKED | H5P_CRT_ORDER_INDEXED};

#ifdef RV_CONNECTOR_DEBUG
        puts("Testing the attribute creation order property\n");
#endif

        if ((dcpl_id = H5Pcreate(H5P_DATASET_CREATE)) < 0)
            TEST_ERROR

        for (i = 0; i < ARRAY_LENGTH(creation_orders); i++) {
            char name[100];

            if (H5Pset_attr_creation_order(dcpl_id, creation_orders[i]) < 0)
                TEST_ERROR

            sprintf(name, "%s%zu", DATASET_CREATION_PROPERTIES_TEST_CRT_ORDER_BASE_NAME, i);

            if ((dset_id = H5Dcreate2(group_id, name, dset_dtype, fspace_id, H5P_DEFAULT, dcpl_id,
                                      H5P_DEFAULT)) < 0) {
                H5_FAILED();
                printf("    couldn't create dataset\n");
                goto error;
            }

            if (H5Dclose(dset_id) < 0)
                TEST_ERROR

            if ((dset_id = H5Dopen2(group_id, name, H5P_DEFAULT)) < 0) {
                H5_FAILED();
                printf("    couldn't open dataset\n");
                goto error;
            }

            if (H5Dclose(dset_id) < 0)
                TEST_ERROR
        }

        if (H5Pclose(dcpl_id) < 0)
            TEST_ERROR
    }

    /* Test the attribute phase change property */
    {
#ifdef RV_CONNECTOR_DEBUG
        puts("Testing the attribute phase change property\n");
#endif

        if ((dcpl_id = H5Pcreate(H5P_DATASET_CREATE)) < 0)
            TEST_ERROR

        if (H5Pset_attr_phase_change(dcpl_id, DATASET_CREATION_PROPERTIES_TEST_MAX_COMPACT,
                                     DATASET_CREATION_PROPERTIES_TEST_MIN_DENSE) < 0)
            TEST_ERROR

        if ((dset_id = H5Dcreate2(group_id, DATASET_CREATION_PROPERTIES_TEST_PHASE_CHANGE_DSET_NAME,
                                  dset_dtype, fspace_id, H5P_DEFAULT, dcpl_id, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't create dataset\n");
            goto error;
        }

        if (H5Dclose(dset_id) < 0)
            TEST_ERROR

        if ((dset_id = H5Dopen2(group_id, DATASET_CREATION_PROPERTIES_TEST_PHASE_CHANGE_DSET_NAME,
                                H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't open dataset\n");
            goto error;
        }

        if (H5Dclose(dset_id) < 0)
            TEST_ERROR
        if (H5Pclose(dcpl_id) < 0)
            TEST_ERROR
    }

    /* Test the fill time property */
    {
        H5D_fill_time_t fill_times[] = {H5D_FILL_TIME_IFSET, H5D_FILL_TIME_ALLOC, H5D_FILL_TIME_NEVER};

#ifdef RV_CONNECTOR_DEBUG
        puts("Testing the fill time property\n");
#endif

        if ((dcpl_id = H5Pcreate(H5P_DATASET_CREATE)) < 0)
            TEST_ERROR

        for (i = 0; i < ARRAY_LENGTH(fill_times); i++) {
            char name[100];

            if (H5Pset_fill_time(dcpl_id, fill_times[i]) < 0)
                TEST_ERROR

            sprintf(name, "%s%zu", DATASET_CREATION_PROPERTIES_TEST_FILL_TIMES_BASE_NAME, i);

            if ((dset_id = H5Dcreate2(group_id, name, dset_dtype, fspace_id, H5P_DEFAULT, dcpl_id,
                                      H5P_DEFAULT)) < 0) {
                H5_FAILED();
                printf("    couldn't create dataset\n");
                goto error;
            }

            if (H5Dclose(dset_id) < 0)
                TEST_ERROR

            if ((dset_id = H5Dopen2(group_id, name, H5P_DEFAULT)) < 0) {
                H5_FAILED();
                printf("    couldn't open dataset\n");
                goto error;
            }

            if (H5Dclose(dset_id) < 0)
                TEST_ERROR
        }

        if (H5Pclose(dcpl_id) < 0)
            TEST_ERROR
    }

    /* TODO: Test the fill value property */
    {

    }

    {
#ifdef RV_CONNECTOR_DEBUG
        puts("Testing dataset filters\n");
#endif

        if ((dcpl_id = H5Pcreate(H5P_DATASET_CREATE)) < 0)
            TEST_ERROR

        /* Set all of the available filters on the DCPL */
        if (H5Pset_deflate(dcpl_id, 7) < 0)
            TEST_ERROR
        if (H5Pset_shuffle(dcpl_id) < 0)
            TEST_ERROR
        if (H5Pset_fletcher32(dcpl_id) < 0)
            TEST_ERROR
        if (H5Pset_nbit(dcpl_id) < 0)
            TEST_ERROR
        if (H5Pset_scaleoffset(dcpl_id, H5Z_SO_FLOAT_ESCALE, 2) < 0)
            TEST_ERROR

        if ((dset_id = H5Dcreate2(group_id, DATASET_CREATION_PROPERTIES_TEST_FILTERS_DSET_NAME, dset_dtype,
                                  fspace_id, H5P_DEFAULT, dcpl_id, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't create dataset\n");
            goto error;
        }

        if (H5Dclose(dset_id) < 0)
            TEST_ERROR

        if ((dset_id = H5Dopen2(group_id, DATASET_CREATION_PROPERTIES_TEST_FILTERS_DSET_NAME, H5P_DEFAULT)) <
            0) {
            H5_FAILED();
            printf("    couldn't open dataset\n");
            goto error;
        }

        if (H5Dclose(dset_id) < 0)
            TEST_ERROR
        if (H5Pclose(dcpl_id) < 0)
            TEST_ERROR
    }

    /* Test the storage layout property */
    {
        H5D_layout_t layouts[] = {H5D_COMPACT, H5D_CONTIGUOUS, H5D_CHUNKED};

#ifdef RV_CONNECTOR_DEBUG
        puts("Testing the storage layout property\n");
#endif

        if ((dcpl_id = H5Pcreate(H5P_DATASET_CREATE)) < 0)
            TEST_ERROR

        for (i = 0; i < ARRAY_LENGTH(layouts); i++) {
            char name[100];

            if (H5Pset_layout(dcpl_id, layouts[i]) < 0)
                TEST_ERROR

            if (H5D_CHUNKED == layouts[i]) {
                hsize_t chunk_dims[DATASET_CREATION_PROPERTIES_TEST_CHUNK_DIM_RANK];
                size_t  j;

                for (j = 0; j < DATASET_CREATION_PROPERTIES_TEST_CHUNK_DIM_RANK; j++)
                    chunk_dims[j] = (hsize_t)(rand() % (int)dims[j] + 1);

                if (H5Pset_chunk(dcpl_id, DATASET_CREATION_PROPERTIES_TEST_CHUNK_DIM_RANK, chunk_dims) < 0)
                    TEST_ERROR
            }

            sprintf(name, "%s%zu", DATASET_CREATION_PROPERTIES_TEST_LAYOUTS_BASE_NAME, i);

            if ((dset_id = H5Dcreate2(group_id, name, dset_dtype, fspace_id, H5P_DEFAULT, dcpl_id,
                                      H5P_DEFAULT)) < 0) {
                H5_FAILED();
                printf("    couldn't create dataset\n");
                goto error;
            }

            if (H5Dclose(dset_id) < 0)
                TEST_ERROR

            if ((dset_id = H5Dopen2(group_id, name, H5P_DEFAULT)) < 0) {
                H5_FAILED();
                printf("    couldn't open dataset\n");
                goto error;
            }

            if (H5Dclose(dset_id) < 0)
                TEST_ERROR
        }

        if (H5Pclose(dcpl_id) < 0)
            TEST_ERROR
    }

    /* Test the "track object times" property */
    {
#ifdef RV_CONNECTOR_DEBUG
        puts("Testing the object time tracking property\n");
#endif

        if ((dcpl_id = H5Pcreate(H5P_DATASET_CREATE)) < 0)
            TEST_ERROR

        if (H5Pset_obj_track_times(dcpl_id, true) < 0)
            TEST_ERROR

        if ((dset_id = H5Dcreate2(group_id, DATASET_CREATION_PROPERTIES_TEST_TRACK_TIMES_YES_DSET_NAME,
                                  dset_dtype, fspace_id, H5P_DEFAULT, dcpl_id, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't create dataset\n");
            goto error;
        }

        if (H5Dclose(dset_id) < 0)
            TEST_ERROR

        if ((dset_id = H5Dopen2(group_id, DATASET_CREATION_PROPERTIES_TEST_TRACK_TIMES_YES_DSET_NAME,
                                H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't open dataset\n");
            goto error;
        }

        if (H5Dclose(dset_id) < 0)
            TEST_ERROR

        if (H5Pset_obj_track_times(dcpl_id, false) < 0)
            TEST_ERROR

        if ((dset_id = H5Dcreate2(group_id, DATASET_CREATION_PROPERTIES_TEST_TRACK_TIMES_NO_DSET_NAME,
                                  dset_dtype, fspace_id, H5P_DEFAULT, dcpl_id, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't create dataset\n");
            goto error;
        }

        if (H5Dclose(dset_id) < 0)
            TEST_ERROR

        if ((dset_id = H5Dopen2(group_id, DATASET_CREATION_PROPERTIES_TEST_TRACK_TIMES_NO_DSET_NAME,
                                H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't open dataset\n");
            goto error;
        }

        if (H5Dclose(dset_id) < 0)
            TEST_ERROR
        if (H5Pclose(dcpl_id) < 0)
            TEST_ERROR
    }

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(fspace_id);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Pclose(dcpl_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_write_dataset_small_all(void)
{
    hssize_t space_npoints;
    hsize_t  dims[DATASET_SMALL_WRITE_TEST_ALL_DSET_SPACE_RANK] = {10, 5, 3};
    size_t   i;
    hid_t    file_id = -1, fapl_id = -1;
    hid_t    container_group = -1;
    hid_t    dset_id         = -1;
    hid_t    fspace_id       = -1;
    void    *data            = NULL;

    TESTING("small write to dataset w/ H5S_ALL")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((fspace_id = H5Screate_simple(DATASET_SMALL_WRITE_TEST_ALL_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_SMALL_WRITE_TEST_ALL_DSET_NAME,
                              DATASET_SMALL_WRITE_TEST_ALL_DSET_DTYPE, fspace_id, H5P_DEFAULT, H5P_DEFAULT,
                              H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    /* Close the dataset and dataspace to ensure that retrieval of file space ID is working */
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR;
    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR

    if ((dset_id = H5Dopen2(file_id, "/" DATASET_TEST_GROUP_NAME "/" DATASET_SMALL_WRITE_TEST_ALL_DSET_NAME,
                            H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset\n");
        goto error;
    }

    if ((fspace_id = H5Dget_space(dset_id)) < 0) {
        H5_FAILED();
        printf("    couldn't get dataset dataspace\n");
        goto error;
    }

    if ((space_npoints = H5Sget_simple_extent_npoints(fspace_id)) < 0) {
        H5_FAILED();
        printf("    couldn't get dataspace num points\n");
        goto error;
    }

    if (NULL == (data = malloc((hsize_t)space_npoints * DATASET_SMALL_WRITE_TEST_ALL_DSET_DTYPESIZE)))
        TEST_ERROR

    for (i = 0; i < (hsize_t)space_npoints; i++)
        ((int *)data)[i] = (int)i;

#ifdef RV_CONNECTOR_DEBUG
    puts("Writing to entire dataset with a small amount of data\n");
#endif

    if (H5Dwrite(dset_id, DATASET_SMALL_WRITE_TEST_ALL_DSET_DTYPE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data) < 0) {
        H5_FAILED();
        printf("    couldn't write to dataset\n");
        goto error;
    }

    if (data) {
        free(data);
        data = NULL;
    }

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        if (data)
            free(data);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_write_dataset_small_hyperslab(void)
{
    hsize_t start[DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t stride[DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t count[DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t block[DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t dims[DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK] = {10, 5, 3};
    size_t  i, data_size;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   dset_id         = -1;
    hid_t   mspace_id = -1, fspace_id = -1;
    void   *data = NULL;

    TESTING("small write to dataset w/ hyperslab")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((fspace_id = H5Screate_simple(DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR
    if ((mspace_id = H5Screate_simple(DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK - 1, dims, NULL)) <
        0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_NAME,
                              DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_DTYPE, fspace_id, H5P_DEFAULT,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    for (i = 0, data_size = 1; i < DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK - 1; i++)
        data_size *= dims[i];
    data_size *= DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_DTYPESIZE;

    if (NULL == (data = malloc(data_size)))
        TEST_ERROR

    for (i = 0; i < data_size / DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_DTYPESIZE; i++)
        ((int *)data)[i] = (int)i;

    for (i = 0; i < DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK; i++) {
        start[i]  = 0;
        stride[i] = 1;
        count[i]  = dims[i];
        block[i]  = 1;
    }

    count[2] = 1;

    if (H5Sselect_hyperslab(fspace_id, H5S_SELECT_SET, start, stride, count, block) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Writing small amount of data to dataset using a hyperslab selection\n");
#endif

    if (H5Dwrite(dset_id, DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_DTYPE, mspace_id, fspace_id, H5P_DEFAULT,
                 data) < 0) {
        H5_FAILED();
        printf("    couldn't write to dataset\n");
        goto error;
    }

    if (data) {
        free(data);
        data = NULL;
    }

    if (H5Sclose(mspace_id) < 0)
        TEST_ERROR
    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        if (data)
            free(data);
        H5Sclose(mspace_id);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_write_dataset_small_point_selection(void)
{
    hsize_t points[DATASET_SMALL_WRITE_TEST_POINT_SELECTION_NUM_POINTS *
                   DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_SPACE_RANK];
    hsize_t dims[DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_SPACE_RANK] = {10, 10, 10};
    hsize_t mdims[1];
    size_t  i, data_size;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   dset_id         = -1;
    hid_t   fspace_id       = -1;
    hid_t   mspace_id       = -1;
    void   *data            = NULL;

    TESTING("small write to dataset w/ point selection")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((fspace_id = H5Screate_simple(DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_SPACE_RANK, dims, NULL)) <
        0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_NAME,
                              DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_DTYPE, fspace_id, H5P_DEFAULT,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    data_size = DATASET_SMALL_WRITE_TEST_POINT_SELECTION_NUM_POINTS *
                DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_DTYPESIZE;

    if (NULL == (data = malloc(data_size)))
        TEST_ERROR

    mdims[0] = DATASET_SMALL_WRITE_TEST_POINT_SELECTION_NUM_POINTS;
    if ((mspace_id = H5Screate_simple(1, mdims, NULL)) < 0)
        TEST_ERROR
    for (i = 0; i < data_size / DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_DTYPESIZE; i++)
        ((int *)data)[i] = (int)i;

    for (i = 0; i < DATASET_SMALL_WRITE_TEST_POINT_SELECTION_NUM_POINTS; i++) {
        size_t j;

        for (j = 0; j < DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_SPACE_RANK; j++)
            points[(i * DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_SPACE_RANK) + j] = i;
    }

    if (H5Sselect_elements(fspace_id, H5S_SELECT_SET, DATASET_SMALL_WRITE_TEST_POINT_SELECTION_NUM_POINTS,
                           points) < 0) {
        H5_FAILED();
        printf("    couldn't select points\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Writing a small amount of data to dataset using a point selection\n");
#endif

    if (H5Dwrite(dset_id, DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_DTYPE, mspace_id, fspace_id,
                 H5P_DEFAULT, data) < 0) {
        H5_FAILED();
        printf("    couldn't write to dataset\n");
        goto error;
    }

    if (data) {
        free(data);
        data = NULL;
    }

    if (H5Sclose(mspace_id) < 0)
        TEST_ERROR
    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        if (data)
            free(data);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

#ifndef NO_LARGE_TESTS
static int
test_write_dataset_large_all(void)
{
    hssize_t space_npoints;
    hsize_t  dims[DATASET_LARGE_WRITE_TEST_ALL_DSET_SPACE_RANK] = {600, 600, 600};
    size_t   i, data_size;
    hid_t    file_id = -1, fapl_id = -1;
    hid_t    container_group = -1;
    hid_t    dset_id         = -1;
    hid_t    fspace_id       = -1;
    void    *data            = NULL;

    TESTING("write to large dataset w/ H5S_ALL")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((fspace_id = H5Screate_simple(DATASET_LARGE_WRITE_TEST_ALL_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_LARGE_WRITE_TEST_ALL_DSET_NAME,
                              DATASET_LARGE_WRITE_TEST_ALL_DSET_DTYPE, fspace_id, H5P_DEFAULT, H5P_DEFAULT,
                              H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    /* Close the dataset and dataspace to ensure that retrieval of file space ID is working */
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR

    if ((dset_id = H5Dopen2(file_id, "/" DATASET_TEST_GROUP_NAME "/" DATASET_LARGE_WRITE_TEST_ALL_DSET_NAME,
                            H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset\n");
        goto error;
    }

    if ((fspace_id = H5Dget_space(dset_id)) < 0) {
        H5_FAILED();
        printf("    couldn't get dataset dataspace\n");
        goto error;
    }

    if ((space_npoints = H5Sget_simple_extent_npoints(fspace_id)) < 0) {
        H5_FAILED();
        printf("    couldn't get dataspace num points\n");
        goto error;
    }

    if (NULL == (data = malloc((hsize_t)space_npoints * DATASET_LARGE_WRITE_TEST_ALL_DSET_DTYPESIZE)))
        TEST_ERROR

    for (i = 0; i < (hsize_t)space_npoints; i++)
        ((int *)data)[i] = (int)i;

#ifdef RV_CONNECTOR_DEBUG
    puts("Writing to entire dataset with a large amount of data\n");
#endif

    if (H5Dwrite(dset_id, DATASET_LARGE_WRITE_TEST_ALL_DSET_DTYPE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data) < 0) {
        H5_FAILED();
        printf("    couldn't write to dataset\n");
        goto error;
    }

    if (data) {
        free(data);
        data = NULL;
    }

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_write_dataset_large_hyperslab(void)
{
    hsize_t start[DATASET_LARGE_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t stride[DATASET_LARGE_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t count[DATASET_LARGE_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t block[DATASET_LARGE_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t dims[DATASET_LARGE_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK] = {600, 600, 600};
    size_t  i, data_size;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   dset_id         = -1;
    hid_t   mspace_id = -1, fspace_id = -1;
    void   *data = NULL;

    TESTING("write to large dataset w/ hyperslab selection")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((fspace_id = H5Screate_simple(DATASET_LARGE_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR
    if ((mspace_id = H5Screate_simple(DATASET_LARGE_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_LARGE_WRITE_TEST_HYPERSLAB_DSET_NAME,
                              DATASET_LARGE_WRITE_TEST_HYPERSLAB_DSET_DTYPE, fspace_id, H5P_DEFAULT,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    for (i = 0, data_size = 1; i < DATASET_LARGE_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK; i++)
        data_size *= dims[i];
    data_size *= DATASET_LARGE_WRITE_TEST_HYPERSLAB_DSET_DTYPESIZE;

    if (NULL == (data = malloc(data_size)))
        TEST_ERROR

    for (i = 0; i < data_size / DATASET_LARGE_WRITE_TEST_HYPERSLAB_DSET_DTYPESIZE; i++)
        ((int *)data)[i] = (int)i;

    for (i = 0; i < DATASET_LARGE_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK; i++) {
        start[i]  = 0;
        stride[i] = 1;
        count[i]  = dims[i];
        block[i]  = 1;
    }

    if (H5Sselect_hyperslab(fspace_id, H5S_SELECT_SET, start, stride, count, block) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Writing large amount of data to dataset using a hyperslab selection\n");
#endif

    if (H5Dwrite(dset_id, DATASET_LARGE_WRITE_TEST_HYPERSLAB_DSET_DTYPE, mspace_id, fspace_id, H5P_DEFAULT,
                 data) < 0) {
        H5_FAILED();
        printf("    couldn't write to dataset\n");
        goto error;
    }

    if (data) {
        free(data);
        data = NULL;
    }

    if (H5Sclose(mspace_id) < 0)
        TEST_ERROR
    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        if (data)
            free(data);
        H5Sclose(mspace_id);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_write_dataset_large_point_selection(void)
{
    TESTING("write to large dataset w/ point selection")

    SKIPPED();

    return 0;

error:
    return 1;
}
#endif

static int
test_read_dataset_small_all(void)
{
    hsize_t dims[DATASET_SMALL_READ_TEST_ALL_DSET_SPACE_RANK] = {10, 5, 3};
    size_t  i, data_size;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   dset_id         = -1;
    hid_t   fspace_id       = -1;
    void   *read_buf        = NULL;

    TESTING("small read from dataset w/ H5S_ALL")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((fspace_id = H5Screate_simple(DATASET_SMALL_READ_TEST_ALL_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_SMALL_READ_TEST_ALL_DSET_NAME,
                              DATASET_SMALL_READ_TEST_ALL_DSET_DTYPE, fspace_id, H5P_DEFAULT, H5P_DEFAULT,
                              H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    for (i = 0, data_size = 1; i < DATASET_SMALL_READ_TEST_ALL_DSET_SPACE_RANK; i++)
        data_size *= dims[i];
    data_size *= DATASET_SMALL_READ_TEST_ALL_DSET_DTYPESIZE;

    if (NULL == (read_buf = malloc(data_size)))
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Reading entirety of small dataset\n");
#endif

    if (H5Dread(dset_id, DATASET_SMALL_READ_TEST_ALL_DSET_DTYPE, H5S_ALL, H5S_ALL, H5P_DEFAULT, read_buf) <
        0) {
        H5_FAILED();
        printf("    couldn't read from dataset\n");
        goto error;
    }

    if (read_buf) {
        free(read_buf);
        read_buf = NULL;
    }

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        if (read_buf)
            free(read_buf);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_read_dataset_small_hyperslab(void)
{
    hsize_t start[DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t stride[DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t count[DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t block[DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t dims[DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_SPACE_RANK] = {10, 5, 3};
    size_t  i, data_size;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   dset_id         = -1;
    hid_t   mspace_id = -1, fspace_id = -1;
    void   *read_buf = NULL;

    TESTING("small read from dataset w/ hyperslab")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((fspace_id = H5Screate_simple(DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR
    if ((mspace_id = H5Screate_simple(DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_SPACE_RANK - 1, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_NAME,
                              DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_DTYPE, fspace_id, H5P_DEFAULT,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    for (i = 0; i < DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_SPACE_RANK; i++) {
        start[i]  = 0;
        stride[i] = 1;
        count[i]  = dims[i];
        block[i]  = 1;
    }

    count[2] = 1;

    if (H5Sselect_hyperslab(fspace_id, H5S_SELECT_SET, start, stride, count, block) < 0)
        TEST_ERROR

    for (i = 0, data_size = 1; i < DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_SPACE_RANK - 1; i++)
        data_size *= dims[i];
    data_size *= DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_DTYPESIZE;

    if (NULL == (read_buf = malloc(data_size)))
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Reading portion of small dataset using hyperslab selection\n");
#endif

    if (H5Dread(dset_id, DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_DTYPE, mspace_id, fspace_id, H5P_DEFAULT,
                read_buf) < 0) {
        H5_FAILED();
        printf("    couldn't read from dataset\n");
        goto error;
    }

    if (read_buf) {
        free(read_buf);
        read_buf = NULL;
    }

    if (H5Sclose(mspace_id) < 0)
        TEST_ERROR
    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        if (read_buf)
            free(read_buf);
        H5Sclose(mspace_id);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_read_dataset_small_point_selection(void)
{
    hsize_t points[DATASET_SMALL_READ_TEST_POINT_SELECTION_NUM_POINTS *
                   DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_SPACE_RANK];
    hsize_t dims[DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_SPACE_RANK] = {10, 10, 10};
    hsize_t mspace_dims[] = {DATASET_SMALL_READ_TEST_POINT_SELECTION_NUM_POINTS};
    size_t  i, data_size;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   dset_id         = -1;
    hid_t   fspace_id       = -1;
    hid_t   mspace_id       = -1;
    void   *data            = NULL;

    TESTING("small read from dataset w/ point selection")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((fspace_id = H5Screate_simple(DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_SPACE_RANK, dims, NULL)) <
        0)
        TEST_ERROR
    if ((mspace_id = H5Screate_simple(1, mspace_dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_NAME,
                              DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_DTYPE, fspace_id, H5P_DEFAULT,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    data_size = DATASET_SMALL_READ_TEST_POINT_SELECTION_NUM_POINTS *
                DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_DTYPESIZE;

    if (NULL == (data = malloc(data_size)))
        TEST_ERROR

    for (i = 0; i < DATASET_SMALL_READ_TEST_POINT_SELECTION_NUM_POINTS; i++) {
        size_t j;

        for (j = 0; j < DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_SPACE_RANK; j++)
            points[(i * DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_SPACE_RANK) + j] = i;
    }

    if (H5Sselect_elements(fspace_id, H5S_SELECT_SET, DATASET_SMALL_READ_TEST_POINT_SELECTION_NUM_POINTS,
                           points) < 0) {
        H5_FAILED();
        printf("    couldn't select points\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Reading portion of small dataset using a point selection\n");
#endif

    if (H5Dread(dset_id, DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_DTYPE, mspace_id, fspace_id,
                H5P_DEFAULT, data) < 0) {
        H5_FAILED();
        printf("    couldn't read from dataset\n");
        goto error;
    }

    if (data) {
        free(data);
        data = NULL;
    }

    if (H5Sclose(mspace_id) < 0)
        TEST_ERROR
    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        if (data)
            free(data);
        H5Sclose(mspace_id);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

#ifndef NO_LARGE_TESTS
static int
test_read_dataset_large_all(void)
{
    hsize_t dims[DATASET_LARGE_READ_TEST_ALL_DSET_SPACE_RANK] = {600, 600, 600};
    size_t  i, data_size;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   dset_id         = -1;
    hid_t   fspace_id       = -1;
    void   *read_buf        = NULL;

    TESTING("read from large dataset w/ H5S_ALL")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((fspace_id = H5Screate_simple(DATASET_LARGE_READ_TEST_ALL_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_LARGE_READ_TEST_ALL_DSET_NAME,
                              DATASET_LARGE_READ_TEST_ALL_DSET_DTYPE, fspace_id, H5P_DEFAULT, H5P_DEFAULT,
                              H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    for (i = 0, data_size = 1; i < DATASET_LARGE_READ_TEST_ALL_DSET_SPACE_RANK; i++)
        data_size *= dims[i];
    data_size *= DATASET_LARGE_READ_TEST_ALL_DSET_DTYPESIZE;

    if (NULL == (read_buf = malloc(data_size)))
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Reading entirety of large dataset\n");
#endif

    if (H5Dread(dset_id, DATASET_LARGE_READ_TEST_ALL_DSET_DTYPE, H5S_ALL, H5S_ALL, H5P_DEFAULT, read_buf) <
        0) {
        H5_FAILED();
        printf("    couldn't read from dataset\n");
        goto error;
    }

    if (read_buf) {
        free(read_buf);
        read_buf = NULL;
    }

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        if (read_buf)
            free(read_buf);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_read_dataset_large_hyperslab(void)
{
    hsize_t start[DATASET_LARGE_READ_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t stride[DATASET_LARGE_READ_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t count[DATASET_LARGE_READ_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t block[DATASET_LARGE_READ_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t dims[DATASET_LARGE_READ_TEST_HYPERSLAB_DSET_SPACE_RANK] = {600, 600, 600};
    size_t  i, data_size;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   dset_id         = -1;
    hid_t   mspace_id = -1, fspace_id = -1;
    void   *read_buf = NULL;

    TESTING("read from large dataset w/ hyperslab selection")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((fspace_id = H5Screate_simple(DATASET_LARGE_READ_TEST_HYPERSLAB_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR
    if ((mspace_id = H5Screate_simple(DATASET_LARGE_READ_TEST_HYPERSLAB_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_LARGE_READ_TEST_HYPERSLAB_DSET_NAME,
                              DATASET_LARGE_READ_TEST_HYPERSLAB_DSET_DTYPE, fspace_id, H5P_DEFAULT,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    for (i = 0; i < DATASET_LARGE_READ_TEST_HYPERSLAB_DSET_SPACE_RANK; i++) {
        start[i]  = 0;
        stride[i] = 1;
        count[i]  = dims[i];
        block[i]  = 1;
    }

    if (H5Sselect_hyperslab(fspace_id, H5S_SELECT_SET, start, stride, count, block) < 0)
        TEST_ERROR

    for (i = 0, data_size = 1; i < DATASET_LARGE_READ_TEST_HYPERSLAB_DSET_SPACE_RANK; i++)
        data_size *= dims[i];
    data_size *= DATASET_LARGE_READ_TEST_HYPERSLAB_DSET_DTYPESIZE;

    if (NULL == (read_buf = malloc(data_size)))
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Reading portion of large dataset using hyperslab selection\n");
#endif

    if (H5Dread(dset_id, DATASET_LARGE_READ_TEST_HYPERSLAB_DSET_DTYPE, mspace_id, fspace_id, H5P_DEFAULT,
                read_buf) < 0) {
        H5_FAILED();
        printf("    couldn't read from dataset\n");
        goto error;
    }

    if (read_buf) {
        free(read_buf);
        read_buf = NULL;
    }

    if (H5Sclose(mspace_id) < 0)
        TEST_ERROR
    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(mspace_id);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_read_dataset_large_point_selection(void)
{
    hsize_t *points                                                        = NULL;
    hsize_t  dims[DATASET_LARGE_READ_TEST_POINT_SELECTION_DSET_SPACE_RANK] = {600, 600, 600};
    hid_t    file_id = -1, fapl_id = -1;
    hid_t    container_group = -1;
    hid_t    dset_id         = -1;
    hid_t    fspace_id       = -1;
    void    *data            = NULL;

    TESTING("read from large dataset w/ point selection")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((fspace_id = H5Screate_simple(DATASET_LARGE_READ_TEST_POINT_SELECTION_DSET_SPACE_RANK, dims, NULL)) <
        0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_LARGE_READ_TEST_POINT_SELECTION_DSET_NAME,
                              DATASET_LARGE_READ_TEST_POINT_SELECTION_DSET_DTYPE, fspace_id, H5P_DEFAULT,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    size_t num_elems = 1;

    for (size_t i = 0; i < DATASET_LARGE_READ_TEST_POINT_SELECTION_DSET_SPACE_RANK; i++)
        num_elems *= dims[i];

    if (NULL == (data = calloc(num_elems, DATASET_LARGE_READ_TEST_POINT_SELECTION_DSET_DTYPESIZE)))
        TEST_ERROR
    if (NULL == (points = calloc(3 * num_elems, sizeof(hsize_t))))
        TEST_ERROR

    /* Select the entire dataspace */
    for (size_t i = 0; i < num_elems; i += 3) {
        points[(i * DATASET_LARGE_READ_TEST_POINT_SELECTION_DSET_SPACE_RANK)] =
            (i % (dims[0] * dims[1])) % dims[1];
        points[(i * DATASET_LARGE_READ_TEST_POINT_SELECTION_DSET_SPACE_RANK) + 1] =
            (i % (dims[0] * dims[1])) / dims[0];
        points[(i * DATASET_LARGE_READ_TEST_POINT_SELECTION_DSET_SPACE_RANK) + 2] = (i / (dims[0] * dims[1]));
    }

    if (H5Sselect_elements(fspace_id, H5S_SELECT_SET, num_elems, points) < 0) {
        H5_FAILED();
        printf("    couldn't select points\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Reading portion of large dataset using a point selection\n");
#endif

    if (H5Dread(dset_id, DATASET_LARGE_READ_TEST_POINT_SELECTION_DSET_DTYPE, H5S_ALL, fspace_id, H5P_DEFAULT,
                data) < 0) {
        H5_FAILED();
        printf("    couldn't read from dataset\n");
        goto error;
    }

    if (data) {
        free(data);
        data = NULL;
    }

    if (points) {
        free(points);
        points = NULL;
    }

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        if (data)
            free(data);
        if (points)
            free(points);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}
#endif

static int
test_write_dataset_data_verification(void)
{
    hssize_t space_npoints;
    hsize_t  dims[DATASET_DATA_VERIFY_WRITE_TEST_DSET_SPACE_RANK] = {10, 10, 10};
    hsize_t  mdims[2];
    hsize_t  start[DATASET_DATA_VERIFY_WRITE_TEST_DSET_SPACE_RANK];
    hsize_t  stride[DATASET_DATA_VERIFY_WRITE_TEST_DSET_SPACE_RANK];
    hsize_t  count[DATASET_DATA_VERIFY_WRITE_TEST_DSET_SPACE_RANK];
    hsize_t  block[DATASET_DATA_VERIFY_WRITE_TEST_DSET_SPACE_RANK];
    hsize_t
           points[DATASET_DATA_VERIFY_WRITE_TEST_NUM_POINTS * DATASET_DATA_VERIFY_WRITE_TEST_DSET_SPACE_RANK];
    size_t i, data_size;
    hid_t  file_id = -1, fapl_id = -1;
    hid_t  container_group = -1;
    hid_t  dset_id         = -1;
    hid_t  fspace_id       = -1;
    hid_t  mspace_id       = -1;
    void  *data            = NULL;
    void  *write_buf       = NULL;
    void  *read_buf        = NULL;

    TESTING("verification of dataset data after write then read")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((fspace_id = H5Screate_simple(DATASET_DATA_VERIFY_WRITE_TEST_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_DATA_VERIFY_WRITE_TEST_DSET_NAME,
                              DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPE, fspace_id, H5P_DEFAULT, H5P_DEFAULT,
                              H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Writing to dataset using H5S_ALL\n");
#endif

    for (i = 0, data_size = 1; i < DATASET_DATA_VERIFY_WRITE_TEST_DSET_SPACE_RANK; i++)
        data_size *= dims[i];
    data_size *= DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE;

    if (NULL == (data = malloc(data_size)))
        TEST_ERROR

    for (i = 0; i < data_size / DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE; i++)
        ((int *)data)[i] = (int)i;

    if (H5Dwrite(dset_id, DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data) <
        0) {
        H5_FAILED();
        printf("    couldn't write to dataset\n");
        goto error;
    }

    if (data) {
        free(data);
        data = NULL;
    }

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR

    if ((dset_id = H5Dopen2(file_id, "/" DATASET_TEST_GROUP_NAME "/" DATASET_DATA_VERIFY_WRITE_TEST_DSET_NAME,
                            H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset\n");
        goto error;
    }

    if ((fspace_id = H5Dget_space(dset_id)) < 0) {
        H5_FAILED();
        printf("    couldn't get dataset dataspace\n");
        goto error;
    }

    if ((space_npoints = H5Sget_simple_extent_npoints(fspace_id)) < 0) {
        H5_FAILED();
        printf("    couldn't get dataspace num points\n");
        goto error;
    }

    if (NULL == (data = malloc((hsize_t)space_npoints * DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE)))
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that the data that comes back is correct after writing to entire dataset\n");
#endif

    if (H5Dread(dset_id, DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data) <
        0) {
        H5_FAILED();
        printf("    couldn't read from dataset\n");
        goto error;
    }

    for (i = 0; i < (hsize_t)space_npoints; i++)
        if (((int *)data)[i] != (int)i) {
            H5_FAILED();
            printf("    ALL selection data verification failed\n");
            goto error;
        }

    if (data) {
        free(data);
        data = NULL;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Writing to dataset using hyperslab selection - contiguous\n");
#endif

    data_size = dims[1] * 2 * DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE;

    if (NULL == (write_buf = malloc(data_size)))
        TEST_ERROR

    for (i = 0; i < data_size / DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE; i++)
        ((int *)write_buf)[i] = 56;

    for (i = 0, data_size = 1; i < DATASET_DATA_VERIFY_WRITE_TEST_DSET_SPACE_RANK; i++)
        data_size *= dims[i];
    data_size *= DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE;

    if (NULL == (data = malloc(data_size)))
        TEST_ERROR

    if (H5Dread(dset_id, DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data) <
        0) {
        H5_FAILED();
        printf("    couldn't read from dataset\n");
        goto error;
    }

    for (i = 0; i < 2; i++) {
        size_t j;

        for (j = 0; j < dims[1]; j++)
            ((int *)data)[(i * dims[1] * dims[2]) + (j * dims[2])] = 56;
    }

    /* Write to first two rows of dataset */
    mdims[0]  = dims[1] * 2;
    start[0]  = 0;
    stride[0] = 1;
    count[0]  = dims[1] * 2;
    block[0]  = 1;
    if ((mspace_id = H5Screate_simple(1, mdims, NULL)) < 0)
        TEST_ERROR
    if (H5Sselect_hyperslab(mspace_id, H5S_SELECT_SET, start, stride, count, block) < 0)
        TEST_ERROR

    start[0] = start[1] = start[2] = 0;
    stride[0] = stride[1] = stride[2] = 1;
    count[0]                          = 2;
    count[1]                          = dims[1];
    count[2]                          = 1;
    block[0] = block[1] = block[2] = 1;
    if (H5Sselect_hyperslab(fspace_id, H5S_SELECT_SET, start, stride, count, block) < 0)
        TEST_ERROR

    if (H5Dwrite(dset_id, DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPE, mspace_id, fspace_id, H5P_DEFAULT,
                 write_buf) < 0) {
        H5_FAILED();
        printf("    couldn't write to dataset\n");
        goto error;
    }

    if (H5Sclose(mspace_id) < 0)
        TEST_ERROR
    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR

    if ((dset_id = H5Dopen2(file_id, "/" DATASET_TEST_GROUP_NAME "/" DATASET_DATA_VERIFY_WRITE_TEST_DSET_NAME,
                            H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset\n");
        goto error;
    }

    if ((fspace_id = H5Dget_space(dset_id)) < 0) {
        H5_FAILED();
        printf("    couldn't get dataset dataspace\n");
        goto error;
    }

    if ((space_npoints = H5Sget_simple_extent_npoints(fspace_id)) < 0) {
        H5_FAILED();
        printf("    couldn't get dataspace num points\n");
        goto error;
    }

    if (NULL == (read_buf = malloc((hsize_t)space_npoints * DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE)))
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that the data that comes back is correct after writing to the dataset using a hyperslab "
         "selection\n");
#endif

    if (H5Dread(dset_id, DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPE, H5S_ALL, H5S_ALL, H5P_DEFAULT, read_buf) <
        0) {
        H5_FAILED();
        printf("    couldn't read from dataset\n");
        goto error;
    }

    if (memcmp(data, read_buf, data_size)) {
        H5_FAILED();
        printf("    hyperslab selection data (contiguous) verification failed\n");
        goto error;
    }

    if (data) {
        free(data);
        data = NULL;
    }

    if (write_buf) {
        free(write_buf);
        write_buf = NULL;
    }

    if (read_buf) {
        free(read_buf);
        read_buf = NULL;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Writing to dataset using hyperslab selection - contiguous - non-zero offset\n");
#endif

    data_size = dims[1] * 2 * DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE;

    if (NULL == (write_buf = malloc(data_size)))
        TEST_ERROR

    for (i = 0; i < dims[1]; i++)
        ((int *)write_buf)[i] = 68;
    for (i = dims[1]; i < data_size / DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE; i++)
        ((int *)write_buf)[i] = 67;

    for (i = 0, data_size = 1; i < DATASET_DATA_VERIFY_WRITE_TEST_DSET_SPACE_RANK; i++)
        data_size *= dims[i];
    data_size *= DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE;

    if (NULL == (data = malloc(data_size)))
        TEST_ERROR

    if (H5Dread(dset_id, DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data) <
        0) {
        H5_FAILED();
        printf("    couldn't read from dataset\n");
        goto error;
    }

    for (i = 2; i < 3; i++) {
        size_t j;

        for (j = 0; j < dims[1]; j++)
            ((int *)data)[(i * dims[1] * dims[2]) + (j * dims[2])] = 67;
    }

    /* Write to third row of dataset */
    mdims[0]  = dims[1] * 2;
    start[0]  = dims[1];
    stride[0] = 1;
    count[0]  = dims[1];
    block[0]  = 1;
    if ((mspace_id = H5Screate_simple(1, mdims, NULL)) < 0)
        TEST_ERROR
    if (H5Sselect_hyperslab(mspace_id, H5S_SELECT_SET, start, stride, count, block) < 0)
        TEST_ERROR

    start[0] = 2;
    start[1] = start[2] = 0;
    stride[0] = stride[1] = stride[2] = 1;
    count[0]                          = 1;
    count[1]                          = dims[1];
    count[2]                          = 1;
    block[0] = block[1] = block[2] = 1;
    if (H5Sselect_hyperslab(fspace_id, H5S_SELECT_SET, start, stride, count, block) < 0)
        TEST_ERROR

    if (H5Dwrite(dset_id, DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPE, mspace_id, fspace_id, H5P_DEFAULT,
                 write_buf) < 0) {
        H5_FAILED();
        printf("    couldn't write to dataset\n");
        goto error;
    }

    if (H5Sclose(mspace_id) < 0)
        TEST_ERROR
    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR

    if ((dset_id = H5Dopen2(file_id, "/" DATASET_TEST_GROUP_NAME "/" DATASET_DATA_VERIFY_WRITE_TEST_DSET_NAME,
                            H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset\n");
        goto error;
    }

    if ((fspace_id = H5Dget_space(dset_id)) < 0) {
        H5_FAILED();
        printf("    couldn't get dataset dataspace\n");
        goto error;
    }

    if ((space_npoints = H5Sget_simple_extent_npoints(fspace_id)) < 0) {
        H5_FAILED();
        printf("    couldn't get dataspace num points\n");
        goto error;
    }

    if (NULL == (read_buf = malloc((hsize_t)space_npoints * DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE)))
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that the data that comes back is correct after writing to the dataset using a hyperslab "
         "selection\n");
#endif

    if (H5Dread(dset_id, DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPE, H5S_ALL, H5S_ALL, H5P_DEFAULT, read_buf) <
        0) {
        H5_FAILED();
        printf("    couldn't read from dataset\n");
        goto error;
    }

    if (memcmp(data, read_buf, data_size)) {
        H5_FAILED();
        printf("    hyperslab selection data (contiguous) verification failed\n");
        goto error;
    }

    if (data) {
        free(data);
        data = NULL;
    }

    if (write_buf) {
        free(write_buf);
        write_buf = NULL;
    }

    if (read_buf) {
        free(read_buf);
        read_buf = NULL;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Writing to dataset using hyperslab selection - non-contiguous\n");
#endif

    data_size = dims[1] * 2 * DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE;

    if (NULL == (write_buf = malloc(data_size)))
        TEST_ERROR

    for (i = 0; i < data_size / DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE; i = i + 2)
        ((int *)write_buf)[i] = 78;
    for (i = 1; i < data_size / DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE; i = i + 2)
        ((int *)write_buf)[i] = 79;

    for (i = 0, data_size = 1; i < DATASET_DATA_VERIFY_WRITE_TEST_DSET_SPACE_RANK; i++)
        data_size *= dims[i];
    data_size *= DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE;

    if (NULL == (data = malloc(data_size)))
        TEST_ERROR

    if (H5Dread(dset_id, DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data) <
        0) {
        H5_FAILED();
        printf("    couldn't read from dataset\n");
        goto error;
    }

    for (i = 3; i < 4; i++) {
        size_t j;

        for (j = 0; j < dims[1]; j++)
            ((int *)data)[(i * dims[1] * dims[2]) + (j * dims[2])] = 78;
    }

    /* Write to fourth row of dataset */
    mdims[0] = dims[1];
    mdims[1] = 2;
    start[0] = start[1] = 0;
    stride[0] = stride[1] = 1;
    count[0]              = dims[1];
    count[1]              = 1;
    block[0] = block[1] = 1;
    if ((mspace_id = H5Screate_simple(2, mdims, NULL)) < 0)
        TEST_ERROR
    if (H5Sselect_hyperslab(mspace_id, H5S_SELECT_SET, start, stride, count, block) < 0)
        TEST_ERROR

    start[0] = 3;
    start[1] = start[2] = 0;
    stride[0] = stride[1] = stride[2] = 1;
    count[0]                          = 1;
    count[1]                          = dims[1];
    count[2]                          = 1;
    block[0] = block[1] = block[2] = 1;
    if (H5Sselect_hyperslab(fspace_id, H5S_SELECT_SET, start, stride, count, block) < 0)
        TEST_ERROR

    if (H5Dwrite(dset_id, DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPE, mspace_id, fspace_id, H5P_DEFAULT,
                 write_buf) < 0) {
        H5_FAILED();
        printf("    couldn't write to dataset\n");
        goto error;
    }

    if (H5Sclose(mspace_id) < 0)
        TEST_ERROR
    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR

    if ((dset_id = H5Dopen2(file_id, "/" DATASET_TEST_GROUP_NAME "/" DATASET_DATA_VERIFY_WRITE_TEST_DSET_NAME,
                            H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset\n");
        goto error;
    }

    if ((fspace_id = H5Dget_space(dset_id)) < 0) {
        H5_FAILED();
        printf("    couldn't get dataset dataspace\n");
        goto error;
    }

    if ((space_npoints = H5Sget_simple_extent_npoints(fspace_id)) < 0) {
        H5_FAILED();
        printf("    couldn't get dataspace num points\n");
        goto error;
    }

    if (NULL == (read_buf = malloc((hsize_t)space_npoints * DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE)))
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that the data that comes back is correct after writing to the dataset using a hyperslab "
         "selection\n");
#endif

    if (H5Dread(dset_id, DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPE, H5S_ALL, H5S_ALL, H5P_DEFAULT, read_buf) <
        0) {
        H5_FAILED();
        printf("    couldn't read from dataset\n");
        goto error;
    }

    if (memcmp(data, read_buf, data_size)) {
        H5_FAILED();
        printf("    hyperslab selection data (non-contiguous) verification failed\n");
        goto error;
    }

    if (data) {
        free(data);
        data = NULL;
    }

    if (write_buf) {
        free(write_buf);
        write_buf = NULL;
    }

    if (read_buf) {
        free(read_buf);
        read_buf = NULL;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Writing to dataset using point selection\n");
#endif

    data_size = DATASET_DATA_VERIFY_WRITE_TEST_NUM_POINTS * DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE;

    if (NULL == (write_buf = malloc(data_size)))
        TEST_ERROR

    for (i = 0; i < data_size / DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE; i++)
        ((int *)write_buf)[i] = 13;

    for (i = 0, data_size = 1; i < DATASET_DATA_VERIFY_WRITE_TEST_DSET_SPACE_RANK; i++)
        data_size *= dims[i];
    data_size *= DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE;

    if (NULL == (data = malloc(data_size)))
        TEST_ERROR

    if (H5Dread(dset_id, DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data) <
        0) {
        H5_FAILED();
        printf("    couldn't read from dataset\n");
        goto error;
    }

    for (i = 0; i < dims[0]; i++) {
        size_t j;

        for (j = 0; j < dims[1]; j++) {
            size_t k;

            for (k = 0; k < dims[2]; k++) {
                if (i == j && j == k)
                    ((int *)data)[(i * dims[1] * dims[2]) + (j * dims[2]) + k] = 13;
            }
        }
    }

    /* Select a series of 10 points in the dataset */
    mdims[0] = DATASET_DATA_VERIFY_WRITE_TEST_NUM_POINTS;
    if ((mspace_id = H5Screate_simple(1, mdims, NULL)) < 0)
        TEST_ERROR
    for (i = 0; i < DATASET_DATA_VERIFY_WRITE_TEST_NUM_POINTS; i++) {
        points[i] = i;
    }
    if (H5Sselect_elements(mspace_id, H5S_SELECT_SET, DATASET_DATA_VERIFY_WRITE_TEST_NUM_POINTS, points) < 0)
        TEST_ERROR

    for (i = 0; i < DATASET_DATA_VERIFY_WRITE_TEST_NUM_POINTS; i++) {
        size_t j;

        for (j = 0; j < DATASET_DATA_VERIFY_WRITE_TEST_DSET_SPACE_RANK; j++)
            points[(i * DATASET_DATA_VERIFY_WRITE_TEST_DSET_SPACE_RANK) + j] = i;
    }

    if (H5Sselect_elements(fspace_id, H5S_SELECT_SET, DATASET_DATA_VERIFY_WRITE_TEST_NUM_POINTS, points) < 0)
        TEST_ERROR

    if (H5Dwrite(dset_id, DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPE, mspace_id, fspace_id, H5P_DEFAULT,
                 write_buf) < 0) {
        H5_FAILED();
        printf("    couldn't write to dataset\n");
        goto error;
    }

    if (H5Sclose(mspace_id) < 0)
        TEST_ERROR
    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR

    if ((dset_id = H5Dopen2(file_id, "/" DATASET_TEST_GROUP_NAME "/" DATASET_DATA_VERIFY_WRITE_TEST_DSET_NAME,
                            H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset\n");
        goto error;
    }

    if ((fspace_id = H5Dget_space(dset_id)) < 0) {
        H5_FAILED();
        printf("    couldn't get dataset dataspace\n");
        goto error;
    }

    if ((space_npoints = H5Sget_simple_extent_npoints(fspace_id)) < 0) {
        H5_FAILED();
        printf("    couldn't get dataspace num points\n");
        goto error;
    }

    if (NULL == (read_buf = malloc((hsize_t)space_npoints * DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE)))
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that the data that comes back is correct after writing to dataset using point "
         "selection\n");
#endif

    if (H5Dread(dset_id, DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPE, H5S_ALL, H5S_ALL, H5P_DEFAULT, read_buf) <
        0) {
        H5_FAILED();
        printf("    couldn't read from dataset\n");
        goto error;
    }

    if (memcmp(data, read_buf, data_size)) {
        H5_FAILED();
        printf("    point selection data verification failed\n");
        goto error;
    }

    if (data) {
        free(data);
        data = NULL;
    }

    if (write_buf) {
        free(write_buf);
        write_buf = NULL;
    }

    if (read_buf) {
        free(read_buf);
        read_buf = NULL;
    }

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        if (data)
            free(data);
        if (write_buf)
            free(write_buf);
        if (read_buf)
            free(read_buf);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_dataset_set_extent(void)
{
    hsize_t dims[DATASET_SET_EXTENT_TEST_SPACE_RANK];
    hsize_t new_dims[DATASET_SET_EXTENT_TEST_SPACE_RANK];
    size_t  i;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   dset_id         = -1;
    hid_t   dset_dtype      = -1;
    hid_t   fspace_id       = -1;

    TESTING("set dataset extent")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    for (i = 0; i < DATASET_SET_EXTENT_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);
    for (i = 0; i < DATASET_SET_EXTENT_TEST_SPACE_RANK; i++)
        new_dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((fspace_id = H5Screate_simple(DATASET_SET_EXTENT_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_SET_EXTENT_TEST_DSET_NAME, dset_dtype, fspace_id,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Testing use of H5Dset_extent to change dataset's extent\n");
#endif

    H5E_BEGIN_TRY
    {
        if (H5Dset_extent(dset_id, new_dims) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded!\n");
            goto error;
        }
    }
    H5E_END_TRY;

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(fspace_id);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_unused_dataset_API_calls(void)
{
    H5D_space_status_t allocation;
    hsize_t            dims[DATASET_UNUSED_APIS_TEST_SPACE_RANK];
    size_t             i;
    hid_t              file_id = -1, fapl_id = -1;
    hid_t              container_group = -1;
    hid_t              dset_id         = -1;
    hid_t              dset_dtype      = -1;
    hid_t              fspace_id       = -1;

    TESTING("unused dataset API calls")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    for (i = 0; i < DATASET_UNUSED_APIS_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((fspace_id = H5Screate_simple(DATASET_UNUSED_APIS_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_UNUSED_APIS_TEST_DSET_NAME, dset_dtype, fspace_id,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Testing that all of the unused dataset API calls don't cause application issues\n");
#endif

    H5E_BEGIN_TRY
    {
        if (H5Dget_storage_size(dset_id) > 0)
            TEST_ERROR
        if (H5Dget_space_status(dset_id, &allocation) > 0)
            TEST_ERROR
        if (H5Dget_offset(dset_id) != HADDR_UNDEF)
            TEST_ERROR
    }
    H5E_END_TRY;

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(fspace_id);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_dataset_property_lists(void)
{
    const char *path_prefix = "/test_prefix";
    hsize_t     dims[DATASET_PROPERTY_LIST_TEST_SPACE_RANK];
    hsize_t     chunk_dims[DATASET_PROPERTY_LIST_TEST_SPACE_RANK];
    size_t      i;
    hid_t       file_id = -1, fapl_id = -1;
    hid_t       container_group = -1, group_id = -1;
    hid_t       dset_id1 = -1, dset_id2 = -1, dset_id3 = -1, dset_id4 = -1;
    hid_t       dcpl_id1 = -1, dcpl_id2 = -1;
    hid_t       dapl_id1 = -1, dapl_id2 = -1;
    hid_t       dset_dtype1 = -1, dset_dtype2 = -1, dset_dtype3 = -1, dset_dtype4 = -1;
    hid_t       space_id   = -1;
    char       *tmp_prefix = NULL;

    TESTING("dataset property list operations")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, DATASET_PROPERTY_LIST_TEST_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    for (i = 0; i < DATASET_PROPERTY_LIST_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);
    for (i = 0; i < DATASET_PROPERTY_LIST_TEST_SPACE_RANK; i++)
        chunk_dims[i] = (hsize_t)(rand() % (int)dims[i] + 1);

    if ((space_id = H5Screate_simple(DATASET_PROPERTY_LIST_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_dtype1 = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR
    if ((dset_dtype2 = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR
    if ((dset_dtype3 = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR
    if ((dset_dtype4 = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    if ((dcpl_id1 = H5Pcreate(H5P_DATASET_CREATE)) < 0) {
        H5_FAILED();
        printf("    couldn't create DCPL\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Setting property on DCPL\n");
#endif

    if (H5Pset_chunk(dcpl_id1, DATASET_PROPERTY_LIST_TEST_SPACE_RANK, chunk_dims) < 0) {
        H5_FAILED();
        printf("    couldn't set DCPL property\n");
        goto error;
    }

    if ((dset_id1 = H5Dcreate2(group_id, DATASET_PROPERTY_LIST_TEST_DSET_NAME1, dset_dtype1, space_id,
                               H5P_DEFAULT, dcpl_id1, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if ((dset_id2 = H5Dcreate2(group_id, DATASET_PROPERTY_LIST_TEST_DSET_NAME2, dset_dtype2, space_id,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if (H5Pclose(dcpl_id1) < 0)
        TEST_ERROR

    /* Try to receive copies of the two property lists, one which has the property set and one which does not
     */
    if ((dcpl_id1 = H5Dget_create_plist(dset_id1)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if ((dcpl_id2 = H5Dget_create_plist(dset_id2)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    /* Ensure that property list 1 has the property set and property list 2 does not */
    {
        hsize_t tmp_chunk_dims[DATASET_PROPERTY_LIST_TEST_SPACE_RANK];

        memset(tmp_chunk_dims, 0, sizeof(tmp_chunk_dims));

        if (H5Pget_chunk(dcpl_id1, DATASET_PROPERTY_LIST_TEST_SPACE_RANK, tmp_chunk_dims) < 0) {
            H5_FAILED();
            printf("    couldn't get DCPL property value\n");
            goto error;
        }

#ifdef RV_CONNECTOR_DEBUG
        puts("Ensuring that the property on the DCPL was received back correctly\n");
#endif

        for (i = 0; i < DATASET_PROPERTY_LIST_TEST_SPACE_RANK; i++)
            if (tmp_chunk_dims[i] != chunk_dims[i]) {
                H5_FAILED();
                printf("    DCPL property values were incorrect\n");
                goto error;
            }

        H5E_BEGIN_TRY
        {
            if (H5Pget_chunk(dcpl_id2, DATASET_PROPERTY_LIST_TEST_SPACE_RANK, tmp_chunk_dims) >= 0) {
                H5_FAILED();
                printf("    property list 2 shouldn't have had chunk dimensionality set (not a chunked "
                       "layout)\n");
                goto error;
            }
        }
        H5E_END_TRY;
    }

    if ((dapl_id1 = H5Pcreate(H5P_DATASET_ACCESS)) < 0) {
        H5_FAILED();
        printf("    couldn't create DAPL\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Setting property on DAPL\n");
#endif

    if (H5Pset_efile_prefix(dapl_id1, path_prefix) < 0) {
        H5_FAILED();
        printf("    couldn't set DAPL property\n");
        goto error;
    }

    if ((dset_id3 = H5Dcreate2(group_id, DATASET_PROPERTY_LIST_TEST_DSET_NAME3, dset_dtype3, space_id,
                               H5P_DEFAULT, H5P_DEFAULT, dapl_id1)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if ((dset_id4 = H5Dcreate2(group_id, DATASET_PROPERTY_LIST_TEST_DSET_NAME4, dset_dtype4, space_id,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if (H5Pclose(dapl_id1) < 0)
        TEST_ERROR

    /* Try to receive copies of the two property lists, one which has the property set and one which does not
     */
    if ((dapl_id1 = H5Dget_access_plist(dset_id3)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if ((dapl_id2 = H5Dget_access_plist(dset_id4)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    /* Ensure that property list 1 has the property set and property list 2 does not */
    {
        ssize_t buf_size = 0;

#ifdef RV_CONNECTOR_DEBUG
        puts("Ensuring that the property on the DAPL was received back correctly\n");
#endif

        if ((buf_size = H5Pget_efile_prefix(dapl_id1, NULL, 0)) < 0) {
            H5_FAILED();
            printf("    couldn't retrieve size for property value buffer\n");
            goto error;
        }

        if (NULL == (tmp_prefix = (char *)calloc(1, (size_t)buf_size + 1)))
            TEST_ERROR

        if (H5Pget_efile_prefix(dapl_id1, tmp_prefix, (size_t)buf_size + 1) < 0) {
            H5_FAILED();
            printf("    couldn't retrieve property list value\n");
            goto error;
        }

        if (strcmp(tmp_prefix, path_prefix)) {
            H5_FAILED();
            printf("    DAPL values were incorrect!\n");
            goto error;
        }

        memset(tmp_prefix, 0, (size_t)buf_size + 1);

        if (H5Pget_efile_prefix(dapl_id2, tmp_prefix, (size_t)buf_size) < 0) {
            H5_FAILED();
            printf("    couldn't retrieve property list value\n");
            goto error;
        }

        if (!strcmp(tmp_prefix, path_prefix)) {
            H5_FAILED();
            printf("    DAPL property value was set!\n");
            goto error;
        }
    }

    /* Now close the property lists and datasets and see if we can still retrieve copies of
     * the property lists upon opening (instead of creating) a dataset
     */
    if (H5Pclose(dcpl_id1) < 0)
        TEST_ERROR
    if (H5Pclose(dcpl_id2) < 0)
        TEST_ERROR
    if (H5Pclose(dapl_id1) < 0)
        TEST_ERROR
    if (H5Pclose(dapl_id2) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id1) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id2) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id3) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id4) < 0)
        TEST_ERROR

    if ((dset_id1 = H5Dopen2(group_id, DATASET_PROPERTY_LIST_TEST_DSET_NAME1, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset\n");
        goto error;
    }

    if ((dset_id2 = H5Dopen2(group_id, DATASET_PROPERTY_LIST_TEST_DSET_NAME2, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset\n");
        goto error;
    }

    if ((dset_id3 = H5Dopen2(group_id, DATASET_PROPERTY_LIST_TEST_DSET_NAME3, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset\n");
        goto error;
    }

    if ((dset_id4 = H5Dopen2(group_id, DATASET_PROPERTY_LIST_TEST_DSET_NAME4, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset\n");
        goto error;
    }

    if ((dcpl_id1 = H5Dget_create_plist(dset_id1)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if ((dcpl_id2 = H5Dget_create_plist(dset_id2)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if ((dapl_id1 = H5Dget_access_plist(dset_id3)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if ((dapl_id2 = H5Dget_create_plist(dset_id4)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if (tmp_prefix) {
        free(tmp_prefix);
        tmp_prefix = NULL;
    }

    if (H5Pclose(dcpl_id1) < 0)
        TEST_ERROR
    if (H5Pclose(dcpl_id2) < 0)
        TEST_ERROR
    if (H5Pclose(dapl_id1) < 0)
        TEST_ERROR
    if (H5Pclose(dapl_id2) < 0)
        TEST_ERROR
    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype1) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype2) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype3) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype4) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id1) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id2) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id3) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id4) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        free(tmp_prefix);
        H5Pclose(dcpl_id1);
        H5Pclose(dcpl_id2);
        H5Pclose(dapl_id1);
        H5Pclose(dapl_id2);
        H5Sclose(space_id);
        H5Tclose(dset_dtype1);
        H5Tclose(dset_dtype2);
        H5Tclose(dset_dtype3);
        H5Tclose(dset_dtype4);
        H5Dclose(dset_id1);
        H5Dclose(dset_id2);
        H5Dclose(dset_id3);
        H5Dclose(dset_id4);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

/*****************************************************
 *                                                   *
 *          Plugin Committed Datatype tests          *
 *                                                   *
 *****************************************************/

static int
test_create_committed_datatype(void)
{
    hid_t file_id = -1, fapl_id = -1;
    hid_t container_group = -1;
    hid_t type_id         = -1;

    TESTING("creation of committed datatype")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATATYPE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((type_id = generate_random_datatype(H5T_NO_CLASS)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Committing datatype\n");
#endif

    if (H5Tcommit2(container_group, DATATYPE_CREATE_TEST_TYPE_NAME, type_id, H5P_DEFAULT, H5P_DEFAULT,
                   H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't commit datatype\n");
        goto error;
    }

    if (H5Tclose(type_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Tclose(type_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_anonymous_committed_datatype(void)
{
    hid_t file_id = -1, fapl_id = -1;
    hid_t container_group = -1;
    hid_t type_id         = -1;

    TESTING("creation of anonymous committed datatype")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATATYPE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((type_id = generate_random_datatype(H5T_NO_CLASS)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Committing anonymous datatype\n");
#endif

    if (H5Tcommit_anon(container_group, type_id, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't commit anonymous datatype\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Linking anonymous datatype into file structure\n");
#endif

    if (H5Olink(type_id, container_group, DATATYPE_CREATE_ANONYMOUS_TYPE_NAME, H5P_DEFAULT, H5P_DEFAULT) <
        0) {
        H5_FAILED();
        printf("    couldn't link anonymous datatype into file structure\n");
        goto error;
    }

    if (H5Tclose(type_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Tclose(type_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_dataset_with_committed_type(void)
{
    hsize_t dims[DATASET_CREATE_WITH_DATATYPE_TEST_DATASET_DIMS];
    size_t  i;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   dset_id         = -1;
    hid_t   type_id         = -1;
    hid_t   fspace_id       = -1;

    TESTING("dataset creation w/ committed datatype")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATATYPE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((type_id = generate_random_datatype(H5T_NO_CLASS)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if (H5Tcommit2(container_group, DATASET_CREATE_WITH_DATATYPE_TEST_TYPE_NAME, type_id, H5P_DEFAULT,
                   H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't commit datatype\n");
        goto error;
    }

    if (H5Tclose(type_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATATYPE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((type_id = H5Topen2(container_group, DATASET_CREATE_WITH_DATATYPE_TEST_TYPE_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open committed datatype\n");
        goto error;
    }

    for (i = 0; i < DATATYPE_CREATE_TEST_DATASET_DIMS; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((fspace_id = H5Screate_simple(DATATYPE_CREATE_TEST_DATASET_DIMS, dims, NULL)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating dataset with a committed type\n");
#endif

    if ((dset_id = H5Dcreate2(container_group, DATASET_CREATE_WITH_DATATYPE_TEST_DSET_NAME, type_id,
                              fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset using variable-length string committed datatype\n");
        goto error;
    }

    if (H5Dclose(dset_id) < 0)
        TEST_ERROR

    if ((dset_id = H5Dopen2(container_group, DATASET_CREATE_WITH_DATATYPE_TEST_DSET_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    failed to open dataset\n");
        goto error;
    }

    if (H5Tclose(type_id) < 0)
        TEST_ERROR
    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Tclose(type_id);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_attribute_with_committed_type(void)
{
    hsize_t dims[ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_SPACE_RANK];
    size_t  i;
    htri_t  attr_exists;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   attr_id         = -1;
    hid_t   type_id         = -1;
    hid_t   space_id        = -1;

    TESTING("attribute creation w/ committed datatype")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATATYPE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((type_id = generate_random_datatype(H5T_NO_CLASS)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if (H5Tcommit2(container_group, ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_DTYPE_NAME, type_id, H5P_DEFAULT,
                   H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't commit datatype\n");
        goto error;
    }

    if (H5Tclose(type_id) < 0)
        TEST_ERROR

    if ((type_id = H5Topen2(container_group, ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_DTYPE_NAME, H5P_DEFAULT)) <
        0) {
        H5_FAILED();
        printf("    couldn't open committed datatype\n");
        goto error;
    }

    for (i = 0; i < ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((space_id = H5Screate_simple(ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating attribute with a committed type\n");
#endif

    if ((attr_id = H5Acreate2(container_group, ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_ATTR_NAME, type_id,
                              space_id, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    /* Verify the attribute has been created */
    if ((attr_exists = H5Aexists(container_group, ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_ATTR_NAME)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

    if (H5Tclose(type_id) < 0)
        TEST_ERROR
    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Tclose(type_id);
        H5Sclose(space_id);
        H5Aclose(attr_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_delete_committed_type(void)
{
    htri_t type_exists;
    hid_t  file_id = -1, fapl_id = -1;
    hid_t  container_group = -1;
    hid_t  type_id         = -1;

    TESTING("delete committed datatype")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATATYPE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((type_id = generate_random_datatype(H5T_NO_CLASS)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if (H5Tcommit2(container_group, DATATYPE_DELETE_TEST_DTYPE_NAME, type_id, H5P_DEFAULT, H5P_DEFAULT,
                   H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't commit datatype\n");
        goto error;
    }

    if ((type_exists = H5Lexists(container_group, DATATYPE_DELETE_TEST_DTYPE_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if datatype exists\n");
        goto error;
    }

    if (!type_exists) {
        H5_FAILED();
        printf("    datatype didn't exist\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Deleting committed type with H5Ldelete\n");
#endif

    if (H5Ldelete(container_group, DATATYPE_DELETE_TEST_DTYPE_NAME, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't delete datatype\n");
        goto error;
    }

    if ((type_exists = H5Lexists(container_group, DATATYPE_DELETE_TEST_DTYPE_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if datatype exists\n");
        goto error;
    }

    if (type_exists) {
        H5_FAILED();
        printf("    datatype exists\n");
        goto error;
    }

    if (H5Tclose(type_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Tclose(type_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_unused_datatype_API_calls(void)
{
    TESTING("unused datatype API calls")

    /* None currently that aren't planned to be used */
#ifdef RV_CONNECTOR_DEBUG
    puts("Currently no API calls to test here\n");
#endif

    SKIPPED();

    return 0;
}

static int
test_datatype_property_lists(void)
{
    hid_t file_id = -1, fapl_id = -1;
    hid_t container_group = -1, group_id = -1;
    hid_t type_id1 = -1, type_id2 = -1;
    hid_t tcpl_id1 = -1, tcpl_id2 = -1;

    TESTING("datatype property list operations")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATATYPE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, DATATYPE_PROPERTY_LIST_TEST_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    if ((type_id1 = generate_random_datatype(H5T_NO_CLASS)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if ((type_id2 = generate_random_datatype(H5T_NO_CLASS)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if ((tcpl_id1 = H5Pcreate(H5P_DATATYPE_CREATE)) < 0) {
        H5_FAILED();
        printf("    couldn't create TCPL\n");
        goto error;
    }

    /* Currently no TCPL routines are defined */

    if (H5Tcommit2(group_id, DATATYPE_PROPERTY_LIST_TEST_DATATYPE_NAME1, type_id1, H5P_DEFAULT, tcpl_id1,
                   H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't commit datatype\n");
        goto error;
    }

    if (H5Tcommit2(group_id, DATATYPE_PROPERTY_LIST_TEST_DATATYPE_NAME2, type_id2, H5P_DEFAULT, H5P_DEFAULT,
                   H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't commit datatype\n");
        goto error;
    }

    if (H5Pclose(tcpl_id1) < 0)
        TEST_ERROR

    /* Try to receive copies for the two property lists */
    if ((tcpl_id1 = H5Tget_create_plist(type_id1)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if ((tcpl_id2 = H5Tget_create_plist(type_id2)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    /* Now close the property lists and datatypes and see if we can still retrieve copies of
     * the property lists upon opening (instead of creating) a datatype
     */
    if (H5Pclose(tcpl_id1) < 0)
        TEST_ERROR
    if (H5Pclose(tcpl_id2) < 0)
        TEST_ERROR
    if (H5Tclose(type_id1) < 0)
        TEST_ERROR
    if (H5Tclose(type_id2) < 0)
        TEST_ERROR

    if ((type_id1 = H5Topen2(group_id, DATATYPE_PROPERTY_LIST_TEST_DATATYPE_NAME1, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open datatype\n");
        goto error;
    }

    if ((type_id2 = H5Topen2(group_id, DATATYPE_PROPERTY_LIST_TEST_DATATYPE_NAME2, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open datatype\n");
        goto error;
    }

    if ((tcpl_id1 = H5Tget_create_plist(type_id1)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if ((tcpl_id2 = H5Tget_create_plist(type_id2)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if (H5Pclose(tcpl_id1) < 0)
        TEST_ERROR
    if (H5Pclose(tcpl_id2) < 0)
        TEST_ERROR
    if (H5Tclose(type_id1) < 0)
        TEST_ERROR
    if (H5Tclose(type_id2) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Pclose(tcpl_id1);
        H5Pclose(tcpl_id2);
        H5Tclose(type_id1);
        H5Tclose(type_id2);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

/*****************************************************
 *                                                   *
 *                Plugin Link tests                  *
 *                                                   *
 *****************************************************/

static int
test_create_hard_link(void)
{
    htri_t link_exists;
    hid_t  file_id = -1, fapl_id = -1;
    hid_t  container_group = -1;

    TESTING("create hard link")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating a hard link\n");
#endif

    if (H5Lcreate_hard(file_id, "/" DATASET_TEST_GROUP_NAME "/" DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_NAME,
                       container_group, HARD_LINK_TEST_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create hard link\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that the link exists\n");
#endif

    /* Verify the link has been created */
    if ((link_exists = H5Lexists(container_group, HARD_LINK_TEST_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link did not exist\n");
        goto error;
    }

    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

/*
 * The following two tests ensure that behavior is correct when using
 * the H5L_SAME_LOC macro for H5Lcreate_hard().
 */
static int
test_create_hard_link_same_loc(void)
{
    hsize_t dims[H5L_SAME_LOC_TEST_DSET_SPACE_RANK];
    size_t  i;
    htri_t  link_exists;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id    = -1;
    hid_t   dset_dtype = -1;
    hid_t   space_id   = -1;

    TESTING("create hard link with H5L_SAME_LOC")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, H5L_SAME_LOC_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT,
                               H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group\n");
        goto error;
    }

    memset(dims, 0, sizeof(dims));
    for (i = 0; i < H5L_SAME_LOC_TEST_DSET_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((space_id = H5Screate_simple(H5L_SAME_LOC_TEST_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, H5L_SAME_LOC_TEST_DSET_NAME, dset_dtype, space_id, H5P_DEFAULT,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

/* Library functionality for this part of the test is broken */
#ifdef RV_CONNECTOR_DEBUG
    puts("Calling H5Lcreate_hard with H5L_SAME_LOC as first parameter\n");
#endif

    if (H5Lcreate_hard(H5L_SAME_LOC, H5L_SAME_LOC_TEST_DSET_NAME, group_id, H5L_SAME_LOC_TEST_LINK_NAME1,
                       H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create first link\n");
        goto error;
    }

    /* Verify the link has been created */
    if ((link_exists = H5Lexists(group_id, H5L_SAME_LOC_TEST_LINK_NAME1, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link did not exist\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Calling H5Lcreate_hard with H5L_SAME_LOC as second parameter\n");
#endif

    if (H5Lcreate_hard(group_id, H5L_SAME_LOC_TEST_DSET_NAME, H5L_SAME_LOC, H5L_SAME_LOC_TEST_LINK_NAME2,
                       H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create second link\n");
        goto error;
    }

    /* Verify the link has been created */
    if ((link_exists = H5Lexists(group_id, H5L_SAME_LOC_TEST_LINK_NAME2, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link did not exist\n");
        goto error;
    }

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(space_id);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_soft_link_existing_relative(void)
{
    hsize_t dims[SOFT_LINK_EXISTING_RELATIVE_TEST_DSET_SPACE_RANK];
    size_t  i;
    htri_t  link_exists;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id     = -1;
    hid_t   dset_dtype  = -1;
    hid_t   dset_dspace = -1;

    TESTING("create soft link to existing object by relative path")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, SOFT_LINK_EXISTING_RELATIVE_TEST_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    for (i = 0; i < SOFT_LINK_EXISTING_RELATIVE_TEST_DSET_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((dset_dspace = H5Screate_simple(SOFT_LINK_EXISTING_RELATIVE_TEST_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, SOFT_LINK_EXISTING_RELATIVE_TEST_DSET_NAME, dset_dtype, dset_dspace,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if (H5Dclose(dset_id) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating soft link with relative path value to an existing object\n");
#endif

    if (H5Lcreate_soft(SOFT_LINK_EXISTING_RELATIVE_TEST_DSET_NAME, group_id,
                       SOFT_LINK_EXISTING_RELATIVE_TEST_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create soft link\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that the link exists\n");
#endif

    /* Verify the link has been created */
    if ((link_exists = H5Lexists(group_id, SOFT_LINK_EXISTING_RELATIVE_TEST_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link did not exist\n");
        goto error;
    }

    if ((dset_id = H5Dopen2(group_id, SOFT_LINK_EXISTING_RELATIVE_TEST_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset through the soft link\n");
        goto error;
    }

    if (H5Sclose(dset_dspace) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(dset_dspace);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_soft_link_existing_absolute(void)
{
    htri_t link_exists;
    hid_t  file_id = -1, fapl_id = -1;
    hid_t  container_group = -1, group_id = -1, root_id = -1;

    TESTING("create soft link to existing object by absolute path")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, SOFT_LINK_EXISTING_ABSOLUTE_TEST_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating a soft link with absolute path value to an existing object\n");
#endif

    if (H5Lcreate_soft("/", group_id, SOFT_LINK_EXISTING_ABSOLUTE_TEST_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) <
        0) {
        H5_FAILED();
        printf("    couldn't create soft link\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that the link exists\n");
#endif

    /* Verify the link has been created */
    if ((link_exists = H5Lexists(file_id,
                                 "/" LINK_TEST_GROUP_NAME "/" SOFT_LINK_EXISTING_ABSOLUTE_TEST_SUBGROUP_NAME
                                 "/" SOFT_LINK_EXISTING_ABSOLUTE_TEST_LINK_NAME,
                                 H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link did not exist\n");
        goto error;
    }

    if ((root_id = H5Gopen2(group_id, SOFT_LINK_EXISTING_ABSOLUTE_TEST_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open object pointed to by soft link\n");
        goto error;
    }

    if (H5Gclose(root_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(root_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_soft_link_dangling_relative(void)
{
    hsize_t dims[SOFT_LINK_DANGLING_RELATIVE_TEST_DSET_SPACE_RANK];
    size_t  i;
    htri_t  link_exists;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id     = -1;
    hid_t   dset_dtype  = -1;
    hid_t   dset_dspace = -1;

    TESTING("create dangling soft link to object by relative path")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, SOFT_LINK_DANGLING_RELATIVE_TEST_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating a dangling soft link with relative path value\n");
#endif

    if (H5Lcreate_soft(SOFT_LINK_DANGLING_RELATIVE_TEST_DSET_NAME, group_id,
                       SOFT_LINK_DANGLING_RELATIVE_TEST_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create soft link\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that the link exists\n");
#endif

    /* Verify the link has been created */
    if ((link_exists = H5Lexists(group_id, SOFT_LINK_DANGLING_RELATIVE_TEST_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link did not exist\n");
        goto error;
    }

    H5E_BEGIN_TRY
    {
        if (H5Dopen2(group_id, SOFT_LINK_DANGLING_RELATIVE_TEST_LINK_NAME, H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    opened target of dangling link!\n");
            goto error;
        }
    }
    H5E_END_TRY;

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    for (i = 0; i < SOFT_LINK_DANGLING_RELATIVE_TEST_DSET_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((dset_dspace = H5Screate_simple(SOFT_LINK_DANGLING_RELATIVE_TEST_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, SOFT_LINK_DANGLING_RELATIVE_TEST_DSET_NAME, dset_dtype, dset_dspace,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if (H5Dclose(dset_id) < 0)
        TEST_ERROR

    if ((dset_id = H5Dopen2(group_id, SOFT_LINK_DANGLING_RELATIVE_TEST_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset pointed to by soft link\n");
        goto error;
    }

    if (H5Sclose(dset_dspace) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(dset_dspace);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_soft_link_dangling_absolute(void)
{
    hsize_t dims[SOFT_LINK_DANGLING_ABSOLUTE_TEST_DSET_SPACE_RANK];
    size_t  i;
    htri_t  link_exists;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id     = -1;
    hid_t   dset_dtype  = -1;
    hid_t   dset_dspace = -1;

    TESTING("create dangling soft link to object by absolute path")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, SOFT_LINK_DANGLING_ABSOLUTE_TEST_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating dangling soft link with absolute path value\n");
#endif

    if (H5Lcreate_soft("/" LINK_TEST_GROUP_NAME "/" SOFT_LINK_DANGLING_ABSOLUTE_TEST_SUBGROUP_NAME
                       "/" SOFT_LINK_DANGLING_ABSOLUTE_TEST_DSET_NAME,
                       group_id, SOFT_LINK_DANGLING_ABSOLUTE_TEST_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create soft link\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that the link exists\n");
#endif

    /* Verify the link has been created */
    if ((link_exists = H5Lexists(group_id, SOFT_LINK_DANGLING_ABSOLUTE_TEST_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link did not exist\n");
        goto error;
    }

    H5E_BEGIN_TRY
    {
        if (H5Dopen2(group_id, SOFT_LINK_DANGLING_ABSOLUTE_TEST_LINK_NAME, H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    opened target of dangling link!\n");
            goto error;
        }
    }
    H5E_END_TRY;

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    for (i = 0; i < SOFT_LINK_DANGLING_ABSOLUTE_TEST_DSET_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((dset_dspace = H5Screate_simple(SOFT_LINK_DANGLING_ABSOLUTE_TEST_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, SOFT_LINK_DANGLING_ABSOLUTE_TEST_DSET_NAME, dset_dtype, dset_dspace,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if (H5Dclose(dset_id) < 0)
        TEST_ERROR

    if ((dset_id = H5Dopen2(group_id, SOFT_LINK_DANGLING_ABSOLUTE_TEST_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset pointed to by soft link\n");
        goto error;
    }

    if (H5Sclose(dset_dspace) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(dset_dspace);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_external_link(void)
{
    htri_t link_exists;
    hid_t  file_id = -1, fapl_id = -1;
    hid_t  container_group = -1, group_id = -1;
    hid_t  root_id = -1;
    char   ext_link_filename[FILENAME_MAX_LENGTH];

    TESTING("create external link to existing object")

    snprintf(ext_link_filename, FILENAME_MAX_LENGTH, "%s/%s/%s", TEST_DIR_PREFIX, username,
             EXTERNAL_LINK_TEST_FILE_NAME);

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fcreate(ext_link_filename, H5F_ACC_TRUNC, H5P_DEFAULT, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't create file for external link to reference\n");
        goto error;
    }

    if (H5Fclose(file_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, EXTERNAL_LINK_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT,
                               H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating an external link to root group of other file\n");
#endif

    if (H5Lcreate_external(ext_link_filename, "/", group_id, EXTERNAL_LINK_TEST_LINK_NAME, H5P_DEFAULT,
                           H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create external link\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that the link exists\n");
#endif

    /* Verify the link has been created */
    if ((link_exists = H5Lexists(group_id, EXTERNAL_LINK_TEST_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link did not exist\n");
        goto error;
    }

    if ((root_id = H5Gopen2(group_id, EXTERNAL_LINK_TEST_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open root group of other file using external link\n");
        goto error;
    }

    if (H5Gclose(root_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(root_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_dangling_external_link(void)
{
    hsize_t dims[EXTERNAL_LINK_TEST_DANGLING_DSET_SPACE_RANK];
    size_t  i;
    htri_t  link_exists;
    hid_t   file_id = -1, ext_file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id     = -1;
    hid_t   dset_dtype  = -1;
    hid_t   dset_dspace = -1;
    char    ext_link_filename[FILENAME_MAX_LENGTH];

    TESTING("create dangling external link")

    snprintf(ext_link_filename, FILENAME_MAX_LENGTH, "%s/%s/%s", TEST_DIR_PREFIX, username,
             EXTERNAL_LINK_TEST_FILE_NAME);

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((ext_file_id = H5Fcreate(ext_link_filename, H5F_ACC_TRUNC, H5P_DEFAULT, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't create file for external link to reference\n");
        goto error;
    }

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, EXTERNAL_LINK_TEST_DANGLING_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating a dangling external link to a dataset in other file\n");
#endif

    if (H5Lcreate_external(ext_link_filename, "/" EXTERNAL_LINK_TEST_DANGLING_DSET_NAME, group_id,
                           EXTERNAL_LINK_TEST_DANGLING_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create dangling external link\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that the link exists\n");
#endif

    /* Verify the link has been created */
    if ((link_exists = H5Lexists(group_id, EXTERNAL_LINK_TEST_DANGLING_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link did not exist\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to open non-existent dataset using dangling external link\n");
#endif

    H5E_BEGIN_TRY
    {
        if (H5Dopen2(group_id, EXTERNAL_LINK_TEST_DANGLING_LINK_NAME, H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    opened non-existent dataset in other file using dangling external link!\n");
            goto error;
        }
    }
    H5E_END_TRY;

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    for (i = 0; i < EXTERNAL_LINK_TEST_DANGLING_DSET_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((dset_dspace = H5Screate_simple(EXTERNAL_LINK_TEST_DANGLING_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating target dataset for dangling external link\n");
#endif

    if ((dset_id = H5Dcreate2(ext_file_id, EXTERNAL_LINK_TEST_DANGLING_DSET_NAME, dset_dtype, dset_dspace,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset in external file\n");
        goto error;
    }

    if (H5Dclose(dset_id) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Re-attempting to open dataset using external link\n");
#endif

    if ((dset_id = H5Dopen2(group_id, EXTERNAL_LINK_TEST_DANGLING_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset in external file\n");
        goto error;
    }

    if (H5Sclose(dset_dspace) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5Fclose(ext_file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(dset_dspace);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5Fclose(ext_file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_user_defined_link(void)
{
    ssize_t udata_size;
    htri_t  link_exists;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    char    udata[UD_LINK_TEST_UDATA_MAX_SIZE];

    TESTING("create user-defined link")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((udata_size = snprintf(udata, UD_LINK_TEST_UDATA_MAX_SIZE, "udata")) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating user-defined link\n");
#endif

    H5E_BEGIN_TRY
    {
        if (H5Lcreate_ud(container_group, UD_LINK_TEST_LINK_NAME, H5L_TYPE_HARD, udata, (size_t)udata_size,
                         H5P_DEFAULT, H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded\n");
            goto error;
        }
    }
    H5E_END_TRY;

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that the link exists\n");
#endif

    /* Verify the link has been created */
    if ((link_exists = H5Lexists(container_group, UD_LINK_TEST_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (link_exists) {
        H5_FAILED();
        printf("    link existed!\n");
        goto error;
    }

    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_delete_link(void)
{
    hsize_t dims[LINK_DELETE_TEST_DSET_SPACE_RANK];
    size_t  i;
    htri_t  link_exists;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id = -1, dset_id2 = -1;
    hid_t   dset_dtype  = -1;
    hid_t   dset_dspace = -1;
    char    ext_link_filename[FILENAME_MAX_LENGTH];

    TESTING("delete link")

    snprintf(ext_link_filename, FILENAME_MAX_LENGTH, "%s/%s/%s", TEST_DIR_PREFIX, username,
             EXTERNAL_LINK_TEST_FILE_NAME);

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, LINK_DELETE_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT,
                               H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    for (i = 0; i < LINK_DELETE_TEST_DSET_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((dset_dspace = H5Screate_simple(LINK_DELETE_TEST_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, LINK_DELETE_TEST_DSET_NAME1, dset_dtype, dset_dspace, H5P_DEFAULT,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create first hard link\n");
        goto error;
    }

    if ((dset_id2 = H5Dcreate2(group_id, LINK_DELETE_TEST_DSET_NAME2, dset_dtype, dset_dspace, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create second hard link\n");
        goto error;
    }

    if (H5Lcreate_soft("/" LINK_TEST_GROUP_NAME "/" LINK_DELETE_TEST_SUBGROUP_NAME
                       "/" LINK_DELETE_TEST_DSET_NAME1,
                       group_id, LINK_DELETE_TEST_SOFT_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create first soft link\n");
        goto error;
    }

    if (H5Lcreate_soft("/" LINK_TEST_GROUP_NAME "/" LINK_DELETE_TEST_SUBGROUP_NAME
                       "/" LINK_DELETE_TEST_DSET_NAME2,
                       group_id, LINK_DELETE_TEST_SOFT_LINK_NAME2, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create second soft link\n");
        goto error;
    }

    if (H5Lcreate_external(ext_link_filename, "/", group_id, LINK_DELETE_TEST_EXTERNAL_LINK_NAME, H5P_DEFAULT,
                           H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create first external link\n");
        goto error;
    }

    if (H5Lcreate_external(ext_link_filename, "/", group_id, LINK_DELETE_TEST_EXTERNAL_LINK_NAME2,
                           H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create second external link\n");
        goto error;
    }

    /* Verify the links have been created */
    if ((link_exists = H5Lexists(group_id, LINK_DELETE_TEST_DSET_NAME1, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if first hard link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    first hard link did not exist\n");
        goto error;
    }

    if ((link_exists = H5Lexists(group_id, LINK_DELETE_TEST_DSET_NAME2, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if second hard link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    second hard link did not exist\n");
        goto error;
    }

    if ((link_exists = H5Lexists(group_id, LINK_DELETE_TEST_SOFT_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if first soft link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    first soft link did not exist\n");
        goto error;
    }

    if ((link_exists = H5Lexists(group_id, LINK_DELETE_TEST_SOFT_LINK_NAME2, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if second soft link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    second soft link did not exist\n");
        goto error;
    }

    if ((link_exists = H5Lexists(group_id, LINK_DELETE_TEST_EXTERNAL_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if first external link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    first external link did not exist\n");
        goto error;
    }

    if ((link_exists = H5Lexists(group_id, LINK_DELETE_TEST_EXTERNAL_LINK_NAME2, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if second external link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    second external link did not exist\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Deleting links with H5Ldelete\n");
#endif

    if (H5Ldelete(group_id, LINK_DELETE_TEST_DSET_NAME1, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't delete hard link using H5Ldelete\n");
        goto error;
    }

    if (H5Ldelete(group_id, LINK_DELETE_TEST_SOFT_LINK_NAME, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't delete soft link using H5Ldelete\n");
        goto error;
    }

    if (H5Ldelete(group_id, LINK_DELETE_TEST_EXTERNAL_LINK_NAME, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't delete external link using H5Ldelete\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Deleting links with H5Ldelete_by_idx\n");
#endif

    H5E_BEGIN_TRY
    {
        if (H5Ldelete_by_idx(group_id, ".", H5_INDEX_NAME, H5_ITER_INC, 0, H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded!\n");
            goto error;
        }

        if (H5Ldelete_by_idx(group_id, ".", H5_INDEX_NAME, H5_ITER_INC, 0, H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded!\n");
            goto error;
        }

        if (H5Ldelete_by_idx(group_id, ".", H5_INDEX_NAME, H5_ITER_INC, 0, H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded!\n");
            goto error;
        }
    }
    H5E_END_TRY;

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that all links have been deleted\n");
#endif

    /* Verify that all links have been deleted */
    if ((link_exists = H5Lexists(group_id, LINK_DELETE_TEST_DSET_NAME1, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if first hard link exists\n");
        goto error;
    }

    if (link_exists) {
        H5_FAILED();
        printf("    first hard link exists!\n");
        goto error;
    }

    if ((link_exists = H5Lexists(group_id, LINK_DELETE_TEST_DSET_NAME2, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if second hard link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    second hard link did not exist!\n");
        goto error;
    }

    if ((link_exists = H5Lexists(group_id, LINK_DELETE_TEST_SOFT_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if first soft link exists\n");
        goto error;
    }

    if (link_exists) {
        H5_FAILED();
        printf("    first soft link exists!\n");
        goto error;
    }

    if ((link_exists = H5Lexists(group_id, LINK_DELETE_TEST_SOFT_LINK_NAME2, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if second soft link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    second soft link did not exist!\n");
        goto error;
    }

    if ((link_exists = H5Lexists(group_id, LINK_DELETE_TEST_EXTERNAL_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if first external link exists\n");
        goto error;
    }

    if (link_exists) {
        H5_FAILED();
        printf("    first external link exists!\n");
        goto error;
    }

    if ((link_exists = H5Lexists(group_id, LINK_DELETE_TEST_EXTERNAL_LINK_NAME2, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if second external link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    second external link did not exist!\n");
        goto error;
    }

    if (H5Sclose(dset_dspace) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id2) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(dset_dspace);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Dclose(dset_id2);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_copy_link(void)
{
    hsize_t dims[COPY_LINK_TEST_DSET_SPACE_RANK];
    size_t  i;
    htri_t  link_exists;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id    = -1;
    hid_t   dset_dtype = -1;
    hid_t   space_id   = -1;

    TESTING("copy a link")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, COPY_LINK_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT,
                               H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group\n");
        goto error;
    }

    for (i = 0; i < COPY_LINK_TEST_DSET_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((space_id = H5Screate_simple(COPY_LINK_TEST_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, COPY_LINK_TEST_DSET_NAME, dset_dtype, space_id, H5P_DEFAULT,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    /* Try to copy a hard link */
    if (H5Lcreate_hard(group_id, COPY_LINK_TEST_DSET_NAME, group_id, COPY_LINK_TEST_HARD_LINK_NAME,
                       H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create hard link\n");
        goto error;
    }

    /* Verify the link has been created */
    if ((link_exists = H5Lexists(group_id, COPY_LINK_TEST_HARD_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if hard link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    hard link did not exist\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to copy a hard link to another location\n");
#endif

    /* Copy the link */
    H5E_BEGIN_TRY
    {
        if (H5Lcopy(group_id, COPY_LINK_TEST_HARD_LINK_NAME, group_id, COPY_LINK_TEST_HARD_LINK_COPY_NAME,
                    H5P_DEFAULT, H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded\n");
            goto error;
        }
    }
    H5E_END_TRY;

#if 0
    /* Verify the link has been copied */
    if ((link_exists = H5Lexists(group_id, COPY_LINK_TEST_HARD_LINK_COPY_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if hard link copy exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    hard link copy did not exist\n");
        goto error;
    }
#endif

    /* Try to copy a soft link */
    if (H5Lcreate_soft(COPY_LINK_TEST_SOFT_LINK_TARGET_PATH, group_id, COPY_LINK_TEST_SOFT_LINK_NAME,
                       H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create soft link\n");
        goto error;
    }

    /* Verify the link has been created */
    if ((link_exists = H5Lexists(group_id, COPY_LINK_TEST_SOFT_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if soft link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    soft link did not exist\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to copy a soft link to another location\n");
#endif

    /* Copy the link */
    H5E_BEGIN_TRY
    {
        if (H5Lcopy(group_id, COPY_LINK_TEST_SOFT_LINK_NAME, group_id, COPY_LINK_TEST_SOFT_LINK_COPY_NAME,
                    H5P_DEFAULT, H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded\n");
            goto error;
        }
    }
    H5E_END_TRY;

#if 0
    /* Verify the link has been copied */
    if ((link_exists = H5Lexists(group_id, COPY_LINK_TEST_SOFT_LINK_COPY_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if soft link copy exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    soft link copy did not exist\n");
        goto error;
    }
#endif

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(space_id);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_move_link(void)
{
    hsize_t dims[MOVE_LINK_TEST_DSET_SPACE_RANK];
    size_t  i;
    htri_t  link_exists;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id    = -1;
    hid_t   dset_dtype = -1;
    hid_t   space_id   = -1;

    TESTING("move a link")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, MOVE_LINK_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT,
                               H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group\n");
        goto error;
    }

    for (i = 0; i < MOVE_LINK_TEST_DSET_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((space_id = H5Screate_simple(MOVE_LINK_TEST_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, MOVE_LINK_TEST_DSET_NAME, dset_dtype, space_id, H5P_DEFAULT,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    /* Try to move a hard link */
    if (H5Lcreate_hard(group_id, MOVE_LINK_TEST_DSET_NAME, file_id, MOVE_LINK_TEST_HARD_LINK_NAME,
                       H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create hard link\n");
        goto error;
    }

    /* Verify the link has been created */
    if ((link_exists = H5Lexists(file_id, MOVE_LINK_TEST_HARD_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if hard link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    hard link did not exist\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to move a hard link to another location\n");
#endif

    /* Move the link */
    H5E_BEGIN_TRY
    {
        if (H5Lmove(file_id, MOVE_LINK_TEST_HARD_LINK_NAME, group_id, MOVE_LINK_TEST_HARD_LINK_NAME,
                    H5P_DEFAULT, H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded\n");
            goto error;
        }
    }
    H5E_END_TRY;

#if 0
    /* Verify the link has been moved */
    if ((link_exists = H5Lexists(group_id, MOVE_LINK_TEST_HARD_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if hard link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    hard link did not exist\n");
        goto error;
    }

    /* Verify the old link is gone */
    if ((link_exists = H5Lexists(file_id, MOVE_LINK_TEST_HARD_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if old hard link exists\n");
        goto error;
    }

    if (link_exists) {
        H5_FAILED();
        printf("    old hard link exists\n");
        goto error;
    }
#endif

    /* Try to move a soft link */
    if (H5Lcreate_soft(MOVE_LINK_TEST_SOFT_LINK_TARGET_PATH, file_id, MOVE_LINK_TEST_SOFT_LINK_NAME,
                       H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create soft link\n");
        goto error;
    }

    /* Verify the link has been created */
    if ((link_exists = H5Lexists(file_id, MOVE_LINK_TEST_SOFT_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if soft link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    soft link did not exist\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to move a soft link to another location\n");
#endif

    /* Move the link */
    H5E_BEGIN_TRY
    {
        if (H5Lmove(file_id, MOVE_LINK_TEST_SOFT_LINK_NAME, group_id, MOVE_LINK_TEST_SOFT_LINK_NAME,
                    H5P_DEFAULT, H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded\n");
            goto error;
        }
    }
    H5E_END_TRY;

#if 0
    /* Verify the link has been moved */
    if ((link_exists = H5Lexists(group_id, MOVE_LINK_TEST_SOFT_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if soft link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    soft link did not exist\n");
        goto error;
    }

    /* Verify the old link is gone */
    if ((link_exists = H5Lexists(file_id, MOVE_LINK_TEST_SOFT_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if old soft link exists\n");
        goto error;
    }

    if (link_exists) {
        H5_FAILED();
        printf("    old soft link exists\n");
        goto error;
    }
#endif

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(space_id);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_get_link_info(void)
{
    H5L_info2_t link_info;
    hsize_t     dims[GET_LINK_INFO_TEST_DSET_SPACE_RANK];
    size_t      i;
    htri_t      link_exists;
    hid_t       file_id = -1, fapl_id = -1;
    hid_t       container_group = -1, group_id = -1;
    hid_t       dset_id     = -1;
    hid_t       dset_dtype  = -1;
    hid_t       dset_dspace = -1;
    char        ext_link_filename[FILENAME_MAX_LENGTH];

    TESTING("get link info")

    snprintf(ext_link_filename, FILENAME_MAX_LENGTH, "%s/%s/%s", TEST_DIR_PREFIX, username,
             EXTERNAL_LINK_TEST_FILE_NAME);

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, GET_LINK_INFO_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT,
                               H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    for (i = 0; i < GET_LINK_INFO_TEST_DSET_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((dset_dspace = H5Screate_simple(GET_LINK_INFO_TEST_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, GET_LINK_INFO_TEST_DSET_NAME, dset_dtype, dset_dspace, H5P_DEFAULT,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if (H5Lcreate_soft("/" LINK_TEST_GROUP_NAME "/" GET_LINK_INFO_TEST_SUBGROUP_NAME
                       "/" GET_LINK_INFO_TEST_DSET_NAME,
                       group_id, GET_LINK_INFO_TEST_SOFT_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create soft link\n");
        goto error;
    }

    if (H5Lcreate_external(ext_link_filename, "/", group_id, GET_LINK_INFO_TEST_EXT_LINK_NAME, H5P_DEFAULT,
                           H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create external link\n");
        goto error;
    }

    /* Verify the links have been created */
    if ((link_exists = H5Lexists(group_id, GET_LINK_INFO_TEST_DSET_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if hard link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    hard link did not exist\n");
        goto error;
    }

    if ((link_exists = H5Lexists(group_id, GET_LINK_INFO_TEST_SOFT_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if soft link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    soft link did not exist\n");
        goto error;
    }

    if ((link_exists = H5Lexists(group_id, GET_LINK_INFO_TEST_EXT_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if external link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    external link did not exist\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving hard link info with H5Lget_info\n");
#endif

    memset(&link_info, 0, sizeof(link_info));

    if (H5Lget_info2(group_id, GET_LINK_INFO_TEST_DSET_NAME, &link_info, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't get hard link info\n");
        goto error;
    }

    if (link_info.type != H5L_TYPE_HARD) {
        H5_FAILED();
        printf("    incorrect link type returned\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving soft link info with H5Lget_info\n");
#endif

    memset(&link_info, 0, sizeof(link_info));

    if (H5Lget_info2(file_id,
                     "/" LINK_TEST_GROUP_NAME "/" GET_LINK_INFO_TEST_SUBGROUP_NAME
                     "/" GET_LINK_INFO_TEST_SOFT_LINK_NAME,
                     &link_info, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't get soft link info\n");
        goto error;
    }

    if (link_info.type != H5L_TYPE_SOFT) {
        H5_FAILED();
        printf("    incorrect link type returned\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving external link info with H5Lget_info\n");
#endif

    memset(&link_info, 0, sizeof(link_info));

    if (H5Lget_info2(group_id, GET_LINK_INFO_TEST_EXT_LINK_NAME, &link_info, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't get external link info\n");
        goto error;
    }

    if (link_info.type != H5L_TYPE_EXTERNAL) {
        H5_FAILED();
        printf("    incorrect link type returned\n");
        goto error;
    }

    H5E_BEGIN_TRY
    {
#ifdef RV_CONNECTOR_DEBUG
        puts("Retrieving hard link info with H5Lget_info_by_idx\n");
#endif

        memset(&link_info, 0, sizeof(link_info));

        if (H5Lget_info_by_idx2(group_id, ".", H5_INDEX_NAME, H5_ITER_INC, 0, &link_info, H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded!\n");
            goto error;
        }

#if 0
        if (link_info.type != H5L_TYPE_HARD) {
            H5_FAILED();
            printf("    incorrect link type returned\n");
            goto error;
        }
#endif

#ifdef RV_CONNECTOR_DEBUG
        puts("Retrieving soft link info with H5Lget_info_by_idx\n");
#endif

        memset(&link_info, 0, sizeof(link_info));

        if (H5Lget_info_by_idx2(file_id, "/" LINK_TEST_GROUP_NAME "/" GET_LINK_INFO_TEST_SUBGROUP_NAME,
                                H5_INDEX_CRT_ORDER, H5_ITER_DEC, 1, &link_info, H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded!\n");
            goto error;
        }

#if 0
        if (link_info.type != H5L_TYPE_SOFT) {
            H5_FAILED();
            printf("    incorrect link type returned\n");
            goto error;
        }
#endif

#ifdef RV_CONNECTOR_DEBUG
        puts("Retrieving external link info with H5Lget_info_by_idx\n");
#endif

        memset(&link_info, 0, sizeof(link_info));

        if (H5Lget_info_by_idx2(group_id, ".", H5_INDEX_NAME, H5_ITER_DEC, 2, &link_info, H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded!\n");
            goto error;
        }

#if 0
        if (link_info.type != H5L_TYPE_EXTERNAL) {
            H5_FAILED();
            printf("    incorrect link type returned\n");
            goto error;
        }
#endif
    }
    H5E_END_TRY;

    if (H5Sclose(dset_dspace) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(dset_dspace);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_get_link_name_by_index(void)
{
    hsize_t dims[GET_LINK_NAME_BY_IDX_TEST_DSET_SPACE_RANK];
    ssize_t ret;
    size_t  i;
    htri_t  link_exists;
    size_t  link_name_buf_size = 0;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id       = -1;
    hid_t   dset_dtype    = -1;
    hid_t   dset_dspace   = -1;
    char   *link_name_buf = NULL;

    TESTING("get link name by index")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, GET_LINK_NAME_BY_IDX_TEST_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    for (i = 0; i < GET_LINK_NAME_BY_IDX_TEST_DSET_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((dset_dspace = H5Screate_simple(GET_LINK_NAME_BY_IDX_TEST_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, GET_LINK_NAME_BY_IDX_TEST_DSET_NAME, dset_dtype, dset_dspace,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    /* Verify that the hard link exists */
    if ((link_exists = H5Lexists(group_id, GET_LINK_NAME_BY_IDX_TEST_DSET_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link '%s' did not exist\n", GET_LINK_NAME_BY_IDX_TEST_DSET_NAME);
        goto error;
    }

    /* Verify that retrieving the link name by index works with hard links; the dataset hard link
     * should be the only link in the group currently.
     */

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving link name of hard link to dataset\n");
    puts("Retrieving size of link name\n");
#endif

    if ((ret = H5Lget_name_by_idx(group_id, ".", H5_INDEX_NAME, H5_ITER_INC, 0, NULL, link_name_buf_size,
                                  H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    unable to retrieve link name size\n");
        goto error;
    }

    link_name_buf_size = (size_t)ret;
    if (NULL == (link_name_buf = (char *)calloc(1, link_name_buf_size + 1)))
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving link name\n");
#endif

    if (H5Lget_name_by_idx(group_id, ".", H5_INDEX_NAME, H5_ITER_INC, 0, link_name_buf,
                           link_name_buf_size + 1, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    unable to retrieve link name\n");
        goto error;
    }

    if (strcmp(link_name_buf, GET_LINK_NAME_BY_IDX_TEST_DSET_NAME)) {
        H5_FAILED();
        printf("    link name '%s' did not match '%s'\n", link_name_buf, GET_LINK_NAME_BY_IDX_TEST_DSET_NAME);
        goto error;
    }

    if (link_name_buf) {
        free(link_name_buf);
        link_name_buf = NULL;
    }

    /*
     * Delete the dataset hard link so the next tests won't be thrown off
     */
    if (H5Ldelete(group_id, GET_LINK_NAME_BY_IDX_TEST_DSET_NAME, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't delete hard link\n");
        goto error;
    }

    /* Create 10 soft links for testing with; create links backwards to make sure sorting by creation order
     * doesn't look the same as sorting by link name */
    for (i = GET_LINK_NAME_BY_IDX_TEST_NUM_LINKS - 1; i >= 0; i--) {
        char temp_link_name[GET_LINK_NAME_BY_IDX_TEST_MAX_LINK_NAME_LENGTH];

        snprintf(temp_link_name, GET_LINK_NAME_BY_IDX_TEST_MAX_LINK_NAME_LENGTH, "link%zu", i);

        if (H5Lcreate_soft("/" LINK_TEST_GROUP_NAME "/" GET_LINK_NAME_BY_IDX_TEST_SUBGROUP_NAME, group_id,
                           temp_link_name, H5P_DEFAULT, H5P_DEFAULT) < 0) {
            H5_FAILED();
            printf("    failed to create soft link '%s'\n", temp_link_name);
            goto error;
        }

        if ((link_exists = H5Lexists(group_id, temp_link_name, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't determine if link exists\n");
            goto error;
        }

        if (!link_exists) {
            H5_FAILED();
            printf("    link '%s' did not exist\n", temp_link_name);
            goto error;
        }

        if (i == 0)
            break;
    }

    /*
     * A variety of ways of reaching the same group are used below in order to test the
     * capability of the function to correctly find the specified group by ID and path.
     */

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving link name by index number; sorted by link name in increasing order\n");
    puts("Retrieving size of link name\n");
#endif

    if ((ret = H5Lget_name_by_idx(group_id, ".", H5_INDEX_NAME, H5_ITER_INC,
                                  GET_LINK_NAME_BY_IDX_TEST_FIRST_LINK_IDX, NULL, link_name_buf_size,
                                  H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    unable to retrieve link name size\n");
        goto error;
    }

    link_name_buf_size = (size_t)ret;
    if (NULL == (link_name_buf = (char *)calloc(1, link_name_buf_size + 1)))
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving link name\n");
#endif

    if (H5Lget_name_by_idx(group_id, ".", H5_INDEX_NAME, H5_ITER_INC,
                           GET_LINK_NAME_BY_IDX_TEST_FIRST_LINK_IDX, link_name_buf, link_name_buf_size + 1,
                           H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    unable to retrieve link name\n");
        goto error;
    }

    if (strcmp(link_name_buf, GET_LINK_NAME_BY_IDX_TEST_FIRST_LINK_NAME)) {
        H5_FAILED();
        printf("    link name '%s' did not match '%s'\n", link_name_buf,
               GET_LINK_NAME_BY_IDX_TEST_FIRST_LINK_NAME);
        goto error;
    }

    if (link_name_buf) {
        free(link_name_buf);
        link_name_buf = NULL;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving link name by index number; sorted by link name in decreasing order\n");
    puts("Retrieving size of link name\n");
#endif

    if ((ret =
             H5Lget_name_by_idx(file_id, "/" LINK_TEST_GROUP_NAME "/" GET_LINK_NAME_BY_IDX_TEST_SUBGROUP_NAME,
                                H5_INDEX_NAME, H5_ITER_DEC, GET_LINK_NAME_BY_IDX_TEST_SECOND_LINK_IDX, NULL,
                                link_name_buf_size, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    unable to retrieve link name size\n");
        goto error;
    }

    link_name_buf_size = (size_t)ret;
    if (NULL == (link_name_buf = (char *)calloc(1, link_name_buf_size + 1)))
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving link name\n");
#endif

    if (H5Lget_name_by_idx(file_id, "/" LINK_TEST_GROUP_NAME "/" GET_LINK_NAME_BY_IDX_TEST_SUBGROUP_NAME,
                           H5_INDEX_NAME, H5_ITER_DEC, GET_LINK_NAME_BY_IDX_TEST_SECOND_LINK_IDX,
                           link_name_buf, link_name_buf_size + 1, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    unable to retrieve link name\n");
        goto error;
    }

    if (strcmp(link_name_buf, GET_LINK_NAME_BY_IDX_TEST_SECOND_LINK_NAME)) {
        H5_FAILED();
        printf("    link name '%s' did not match '%s'\n", link_name_buf,
               GET_LINK_NAME_BY_IDX_TEST_SECOND_LINK_NAME);
        goto error;
    }

    if (link_name_buf) {
        free(link_name_buf);
        link_name_buf = NULL;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving link name by index number; sorted by link creation order in increasing order\n");
    puts("Retrieving size of link name\n");
#endif

    if ((ret = H5Lget_name_by_idx(container_group, GET_LINK_NAME_BY_IDX_TEST_SUBGROUP_NAME,
                                  H5_INDEX_CRT_ORDER, H5_ITER_INC, GET_LINK_NAME_BY_IDX_TEST_THIRD_LINK_IDX,
                                  NULL, link_name_buf_size, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    unable to retrieve link name size\n");
        goto error;
    }

    link_name_buf_size = (size_t)ret;
    if (NULL == (link_name_buf = (char *)calloc(1, link_name_buf_size + 1)))
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving link name\n");
#endif

    if (H5Lget_name_by_idx(container_group, GET_LINK_NAME_BY_IDX_TEST_SUBGROUP_NAME, H5_INDEX_CRT_ORDER,
                           H5_ITER_INC, GET_LINK_NAME_BY_IDX_TEST_THIRD_LINK_IDX, link_name_buf,
                           link_name_buf_size + 1, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    unable to retrieve link name\n");
        goto error;
    }

    if (strcmp(link_name_buf, GET_LINK_NAME_BY_IDX_TEST_THIRD_LINK_NAME)) {
        H5_FAILED();
        printf("    link name '%s' did not match '%s'\n", link_name_buf,
               GET_LINK_NAME_BY_IDX_TEST_THIRD_LINK_NAME);
        goto error;
    }

    if (link_name_buf) {
        free(link_name_buf);
        link_name_buf = NULL;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving link name by index number; sorted by link creation order in decreasing order\n");
    puts("Retrieving size of link name\n");
#endif

    if ((ret = H5Lget_name_by_idx(group_id,
                                  "/" LINK_TEST_GROUP_NAME "/" GET_LINK_NAME_BY_IDX_TEST_SUBGROUP_NAME,
                                  H5_INDEX_CRT_ORDER, H5_ITER_DEC, GET_LINK_NAME_BY_IDX_TEST_FOURTH_LINK_IDX,
                                  NULL, link_name_buf_size, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    unable to retrieve link name size\n");
        goto error;
    }

    link_name_buf_size = (size_t)ret;
    if (NULL == (link_name_buf = (char *)calloc(1, link_name_buf_size + 1)))
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving link name\n");
#endif

    if (H5Lget_name_by_idx(group_id, "/" LINK_TEST_GROUP_NAME "/" GET_LINK_NAME_BY_IDX_TEST_SUBGROUP_NAME,
                           H5_INDEX_CRT_ORDER, H5_ITER_DEC, GET_LINK_NAME_BY_IDX_TEST_FOURTH_LINK_IDX,
                           link_name_buf, link_name_buf_size + 1, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    unable to retrieve link name\n");
        goto error;
    }

    if (strcmp(link_name_buf, GET_LINK_NAME_BY_IDX_TEST_FOURTH_LINK_NAME)) {
        H5_FAILED();
        printf("    link name '%s' did not match '%s'\n", link_name_buf,
               GET_LINK_NAME_BY_IDX_TEST_FOURTH_LINK_NAME);
        goto error;
    }

    if (link_name_buf) {
        free(link_name_buf);
        link_name_buf = NULL;
    }

    /* Check to make sure that using an index number beyond the number of links will fail */
    H5E_BEGIN_TRY
    {
        link_name_buf_size = 256;
        if (NULL == (link_name_buf = (char *)calloc(1, link_name_buf_size + 1)))
            TEST_ERROR

        if (H5Lget_name_by_idx(group_id, ".", H5_INDEX_NAME, H5_ITER_INC, GET_LINK_NAME_BY_IDX_TEST_NUM_LINKS,
                               link_name_buf, link_name_buf_size, H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    using an index number beyond the number of links didn't fail!\n");
            goto error;
        }

        if (link_name_buf) {
            free(link_name_buf);
            link_name_buf = NULL;
        }
    }
    H5E_END_TRY;

    if (H5Sclose(dset_dspace) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        if (link_name_buf)
            free(link_name_buf);
        H5Sclose(dset_dspace);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_get_link_val(void)
{
    H5L_info2_t link_info;
    const char *ext_link_filepath;
    const char *ext_link_val;
    unsigned    ext_link_flags;
    htri_t      link_exists;
    size_t      link_val_buf_size;
    char       *link_val_buf = NULL;
    hid_t       file_id = -1, fapl_id = -1;
    hid_t       container_group = -1, group_id = -1;
    char        ext_link_filename[FILENAME_MAX_LENGTH];

    TESTING("get link value")

    snprintf(ext_link_filename, FILENAME_MAX_LENGTH, "%s/%s/%s", TEST_DIR_PREFIX, username,
             EXTERNAL_LINK_TEST_FILE_NAME);

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, GET_LINK_VAL_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT,
                               H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

    if (H5Lcreate_soft("/" LINK_TEST_GROUP_NAME "/" GET_LINK_VAL_TEST_SUBGROUP_NAME, group_id,
                       GET_LINK_VAL_TEST_SOFT_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create soft link\n");
        goto error;
    }

    if (H5Lcreate_external(ext_link_filename, "/", group_id, GET_LINK_VAL_TEST_EXT_LINK_NAME, H5P_DEFAULT,
                           H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create external link\n");
        goto error;
    }

    /* Verify the links have been created */
    if ((link_exists = H5Lexists(group_id, GET_LINK_VAL_TEST_SOFT_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link did not exist\n");
        goto error;
    }

    if ((link_exists = H5Lexists(group_id, GET_LINK_VAL_TEST_EXT_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if external link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    external link did not exist\n");
        goto error;
    }

    memset(&link_info, 0, sizeof(link_info));

    if (H5Lget_info2(group_id, GET_LINK_VAL_TEST_SOFT_LINK_NAME, &link_info, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't get soft link info\n");
        goto error;
    }

    if (link_info.type != H5L_TYPE_SOFT) {
        H5_FAILED();
        printf("    incorrect link type returned\n");
        goto error;
    }

    link_val_buf_size = link_info.u.val_size;
    if (NULL == (link_val_buf = (char *)malloc(link_val_buf_size)))
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving value of soft link with H5Lget_val\n");
#endif

    if (H5Lget_val(group_id, GET_LINK_VAL_TEST_SOFT_LINK_NAME, link_val_buf, link_val_buf_size, H5P_DEFAULT) <
        0) {
        H5_FAILED();
        printf("    couldn't get soft link val\n");
        goto error;
    }

    if (strcmp(link_val_buf, "/" LINK_TEST_GROUP_NAME "/" GET_LINK_VAL_TEST_SUBGROUP_NAME)) {
        H5_FAILED();
        printf("    soft link value did not match\n");
        goto error;
    }

    memset(&link_info, 0, sizeof(link_info));

    if (H5Lget_info2(group_id, GET_LINK_VAL_TEST_EXT_LINK_NAME, &link_info, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't get external link info\n");
        goto error;
    }

    if (link_info.type != H5L_TYPE_EXTERNAL) {
        H5_FAILED();
        printf("    incorrect link type returned\n");
        goto error;
    }

    if (link_info.u.val_size > link_val_buf_size) {
        char *tmp_realloc;

        link_val_buf_size *= 2;

        if (NULL == (tmp_realloc = (char *)realloc(link_val_buf, link_val_buf_size)))
            TEST_ERROR
        link_val_buf = tmp_realloc;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving value of external link with H5Lget_val\n");
#endif

    if (H5Lget_val(group_id, GET_LINK_VAL_TEST_EXT_LINK_NAME, link_val_buf, link_val_buf_size, H5P_DEFAULT) <
        0) {
        H5_FAILED();
        printf("    couldn't get external link val\n");
        goto error;
    }

    if (H5Lunpack_elink_val(link_val_buf, link_val_buf_size, &ext_link_flags, &ext_link_filepath,
                            &ext_link_val) < 0) {
        H5_FAILED();
        printf("    couldn't unpack external link value buffer\n");
        goto error;
    }

    if (strcmp(ext_link_filepath, ext_link_filename)) {
        H5_FAILED();
        printf("    external link target file did not match\n");
        goto error;
    }

    if (strcmp(ext_link_val, "/")) {
        H5_FAILED();
        printf("    external link value did not match\n");
        goto error;
    }

    H5E_BEGIN_TRY
    {
        memset(&link_info, 0, sizeof(link_info));

        if (H5Lget_info2(group_id, GET_LINK_VAL_TEST_SOFT_LINK_NAME, &link_info, H5P_DEFAULT) < 0) {
            H5_FAILED();
            printf("    couldn't get soft link info\n");
            goto error;
        }

        if (link_info.type != H5L_TYPE_SOFT) {
            H5_FAILED();
            printf("    incorrect link type returned\n");
            goto error;
        }

        if (link_info.u.val_size > link_val_buf_size) {
            char *tmp_realloc;

            link_val_buf_size *= 2;

            if (NULL == (tmp_realloc = (char *)realloc(link_val_buf, link_val_buf_size)))
                TEST_ERROR
            link_val_buf = tmp_realloc;
        }

#ifdef RV_CONNECTOR_DEBUG
        puts("Retrieving value of soft link with H5Lget_val_by_idx\n");
#endif

        if (H5Lget_val_by_idx(group_id, ".", H5_INDEX_CRT_ORDER, H5_ITER_INC, 0, link_val_buf,
                              link_val_buf_size, H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded!\n");
            goto error;
        }

#if 0
        if (strcmp(link_val_buf, "/" LINK_TEST_GROUP_NAME "/" GET_LINK_VAL_TEST_SUBGROUP_NAME)) {
            H5_FAILED();
            printf("    soft link value did not match\n");
            goto error;
        }
#endif

        memset(&link_info, 0, sizeof(link_info));

        if (H5Lget_info2(group_id, GET_LINK_VAL_TEST_EXT_LINK_NAME, &link_info, H5P_DEFAULT) < 0) {
            H5_FAILED();
            printf("    couldn't get external link info\n");
            goto error;
        }

        if (link_info.type != H5L_TYPE_EXTERNAL) {
            H5_FAILED();
            printf("    incorrect link type returned\n");
            goto error;
        }

        if (link_info.u.val_size > link_val_buf_size) {
            char *tmp_realloc;

            link_val_buf_size *= 2;

            if (NULL == (tmp_realloc = (char *)realloc(link_val_buf, link_val_buf_size)))
                TEST_ERROR
            link_val_buf = tmp_realloc;
        }

#ifdef RV_CONNECTOR_DEBUG
        puts("Retrieving value of external link with H5Lget_val_by_idx\n");
#endif

        if (H5Lget_val_by_idx(group_id, ".", H5_INDEX_CRT_ORDER, H5_ITER_INC, 0, link_val_buf,
                              link_val_buf_size, H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded!\n");
            goto error;
        }

#if 0
        if (H5Lunpack_elink_val(link_val_buf, link_val_buf_size, &ext_link_flags, &ext_link_filename, &ext_link_val) < 0) {
            H5_FAILED();
            printf("    couldn't unpack external link value buffer\n");
            goto error;
        }

        if (strcmp(ext_link_filename, ext_link_filename)) {
            H5_FAILED();
            printf("    external link target file did not match\n");
            goto error;
        }

        if (strcmp(ext_link_val, "/")) {
            H5_FAILED();
            printf("    external link value did not match\n");
            goto error;
        }
#endif
    }
    H5E_END_TRY;

    if (link_val_buf) {
        free(link_val_buf);
        link_val_buf = NULL;
    }

    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        if (link_val_buf)
            free(link_val_buf);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_link_iterate(void)
{
    hsize_t dims[LINK_ITER_TEST_DSET_SPACE_RANK];
    hsize_t saved_idx = 0;
    size_t  i;
    htri_t  link_exists;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id     = -1;
    hid_t   dset_dtype  = -1;
    hid_t   dset_dspace = -1;
    int     halted      = 0;
    char    ext_link_filename[FILENAME_MAX_LENGTH];

    TESTING("link iteration")

    snprintf(ext_link_filename, FILENAME_MAX_LENGTH, "%s/%s/%s", TEST_DIR_PREFIX, username,
             EXTERNAL_LINK_TEST_FILE_NAME);

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, LINK_ITER_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT,
                               H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    for (i = 0; i < LINK_ITER_TEST_DSET_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((dset_dspace = H5Screate_simple(LINK_ITER_TEST_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, LINK_ITER_TEST_HARD_LINK_NAME, dset_dtype, dset_dspace, H5P_DEFAULT,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create hard link\n");
        goto error;
    }

    if (H5Lcreate_soft("/" LINK_TEST_GROUP_NAME "/" LINK_ITER_TEST_SUBGROUP_NAME
                       "/" LINK_ITER_TEST_HARD_LINK_NAME,
                       group_id, LINK_ITER_TEST_SOFT_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create soft link\n");
        goto error;
    }

    if (H5Lcreate_external(ext_link_filename, "/", group_id, LINK_ITER_TEST_EXT_LINK_NAME, H5P_DEFAULT,
                           H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create external link\n");
        goto error;
    }

    /* Verify the links have been created */
    if ((link_exists = H5Lexists(group_id, LINK_ITER_TEST_HARD_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link 1 did not exist\n");
        goto error;
    }

    if ((link_exists = H5Lexists(group_id, LINK_ITER_TEST_SOFT_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link 2 did not exist\n");
        goto error;
    }

    if ((link_exists = H5Lexists(group_id, LINK_ITER_TEST_EXT_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link 3 did not exist\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Iterating over links by link name in increasing order with H5Literate\n");
#endif

    /* Test basic link iteration capability using both index types and both index orders */
    if (H5Literate(group_id, H5_INDEX_NAME, H5_ITER_INC, NULL, link_iter_callback1, NULL) < 0) {
        H5_FAILED();
        printf("    H5Literate by index type name in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Iterating over links by link name in decreasing order with H5Literate\n");
#endif

    if (H5Literate(group_id, H5_INDEX_NAME, H5_ITER_DEC, NULL, link_iter_callback1, NULL) < 0) {
        H5_FAILED();
        printf("    H5Literate by index type name in decreasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Iterating over links by link creation order in increasing order with H5Literate\n");
#endif

    if (H5Literate(group_id, H5_INDEX_CRT_ORDER, H5_ITER_INC, NULL, link_iter_callback1, NULL) < 0) {
        H5_FAILED();
        printf("    H5Literate by index type creation order in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Iterating over links by link creation order in decreasing order with H5Literate\n");
#endif

    if (H5Literate(group_id, H5_INDEX_CRT_ORDER, H5_ITER_DEC, NULL, link_iter_callback1, NULL) < 0) {
        H5_FAILED();
        printf("    H5Literate by index type creation order in decreasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Iterating over links by link name in increasing order with H5Literate_by_name\n");
#endif

    if (H5Literate_by_name(file_id, "/" LINK_TEST_GROUP_NAME "/" LINK_ITER_TEST_SUBGROUP_NAME, H5_INDEX_NAME,
                           H5_ITER_INC, NULL, link_iter_callback1, NULL, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Literate_by_name by index type name in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Iterating over links by link name in decreasing order with H5Literate_by_name\n");
#endif

    if (H5Literate_by_name(file_id, "/" LINK_TEST_GROUP_NAME "/" LINK_ITER_TEST_SUBGROUP_NAME, H5_INDEX_NAME,
                           H5_ITER_DEC, NULL, link_iter_callback1, NULL, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Literate_by_name by index type name in decreasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Iterating over links by link creation order in increasing order with H5Literate_by_name\n");
#endif

    if (H5Literate_by_name(file_id, "/" LINK_TEST_GROUP_NAME "/" LINK_ITER_TEST_SUBGROUP_NAME,
                           H5_INDEX_CRT_ORDER, H5_ITER_INC, NULL, link_iter_callback1, NULL,
                           H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Literate_by_name by index type creation order in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Iterating over links by link creation order in decreasing order with H5Literate_by_name\n");
#endif

    if (H5Literate_by_name(file_id, "/" LINK_TEST_GROUP_NAME "/" LINK_ITER_TEST_SUBGROUP_NAME,
                           H5_INDEX_CRT_ORDER, H5_ITER_DEC, NULL, link_iter_callback1, NULL,
                           H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Literate_by_name by index type creation order in decreasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Testing H5Literate's index-saving capability in increasing iteration order\n");
#endif

    /* Test the H5Literate index-saving capabilities */
    if (H5Literate(group_id, H5_INDEX_CRT_ORDER, H5_ITER_INC, &saved_idx, link_iter_callback2, &halted) < 0) {
        H5_FAILED();
        printf("    H5Literate index-saving capability test failed\n");
        goto error;
    }

    if (saved_idx != 2) {
        H5_FAILED();
        printf("    saved index after iteration was wrong\n");
        goto error;
    }

    if (H5Literate(group_id, H5_INDEX_CRT_ORDER, H5_ITER_INC, &saved_idx, link_iter_callback2, &halted) < 0) {
        H5_FAILED();
        printf("    couldn't finish iterating\n");
        goto error;
    }

    saved_idx = LINK_ITER_TEST_NUM_LINKS - 1;
    halted    = 0;

#ifdef RV_CONNECTOR_DEBUG
    puts("Testing H5Literate's index-saving capability in decreasing iteration order\n");
#endif

    if (H5Literate(group_id, H5_INDEX_CRT_ORDER, H5_ITER_DEC, &saved_idx, link_iter_callback2, &halted) < 0) {
        H5_FAILED();
        printf("    H5Literate index-saving capability test failed\n");
        goto error;
    }

    if (saved_idx != 2) {
        H5_FAILED();
        printf("    saved index after iteration was wrong\n");
        goto error;
    }

    if (H5Literate(group_id, H5_INDEX_CRT_ORDER, H5_ITER_DEC, &saved_idx, link_iter_callback2, &halted) < 0) {
        H5_FAILED();
        printf("    couldn't finish iterating\n");
        goto error;
    }

    if (H5Sclose(dset_dspace) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(dset_dspace);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_link_iterate_0_links(void)
{
    hid_t file_id = -1, fapl_id = -1;
    hid_t container_group = -1, group_id = -1;

    TESTING("link iteration on group with 0 links")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, LINK_ITER_TEST_0_LINKS_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Iterating over links by link name in increasing order with H5Literate\n");
#endif

    /* Test basic link iteration capability using both index types and both index orders */
    if (H5Literate(group_id, H5_INDEX_NAME, H5_ITER_INC, NULL, link_iter_callback3, NULL) < 0) {
        H5_FAILED();
        printf("    H5Literate by index type name in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Iterating over links by link name in decreasing order with H5Literate\n");
#endif

    if (H5Literate(group_id, H5_INDEX_NAME, H5_ITER_DEC, NULL, link_iter_callback3, NULL) < 0) {
        H5_FAILED();
        printf("    H5Literate by index type name in decreasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Iterating over links by link creation order in increasing order with H5Literate\n");
#endif

    if (H5Literate(group_id, H5_INDEX_CRT_ORDER, H5_ITER_INC, NULL, link_iter_callback3, NULL) < 0) {
        H5_FAILED();
        printf("    H5Literate by index type creation order in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Iterating over links by link creation order in decreasing order with H5Literate\n");
#endif

    if (H5Literate(group_id, H5_INDEX_CRT_ORDER, H5_ITER_DEC, NULL, link_iter_callback3, NULL) < 0) {
        H5_FAILED();
        printf("    H5Literate by index type creation order in decreasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Iterating over links by link name in increasing order with H5Literate_by_name\n");
#endif

    if (H5Literate_by_name(file_id, "/" LINK_TEST_GROUP_NAME "/" LINK_ITER_TEST_0_LINKS_SUBGROUP_NAME,
                           H5_INDEX_NAME, H5_ITER_INC, NULL, link_iter_callback3, NULL, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Literate_by_name by index type name in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Iterating over links by link name in decreasing order with H5Literate_by_name\n");
#endif

    if (H5Literate_by_name(file_id, "/" LINK_TEST_GROUP_NAME "/" LINK_ITER_TEST_0_LINKS_SUBGROUP_NAME,
                           H5_INDEX_NAME, H5_ITER_DEC, NULL, link_iter_callback3, NULL, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Literate_by_name by index type name in decreasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Iterating over links by link creation order in increasing order with H5Literate_by_name\n");
#endif

    if (H5Literate_by_name(file_id, "/" LINK_TEST_GROUP_NAME "/" LINK_ITER_TEST_0_LINKS_SUBGROUP_NAME,
                           H5_INDEX_CRT_ORDER, H5_ITER_INC, NULL, link_iter_callback3, NULL,
                           H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Literate_by_name by index type creation order in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Iterating over links by link creation order in decreasing order with H5Literate_by_name\n");
#endif

    if (H5Literate_by_name(file_id, "/" LINK_TEST_GROUP_NAME "/" LINK_ITER_TEST_0_LINKS_SUBGROUP_NAME,
                           H5_INDEX_CRT_ORDER, H5_ITER_DEC, NULL, link_iter_callback3, NULL,
                           H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Literate_by_name by index type creation order in decreasing order failed\n");
        goto error;
    }

    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_link_visit(void)
{
    hsize_t dims[LINK_VISIT_TEST_NO_CYCLE_DSET_SPACE_RANK];
    size_t  i;
    htri_t  link_exists;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   subgroup1 = -1, subgroup2 = -1;
    hid_t   dset_id    = -1;
    hid_t   dset_dtype = -1;
    hid_t   fspace_id  = -1;
    char    ext_link_filename[FILENAME_MAX_LENGTH];

    TESTING("link visit without cycles")

    snprintf(ext_link_filename, FILENAME_MAX_LENGTH, "%s/%s/%s", TEST_DIR_PREFIX, username,
             EXTERNAL_LINK_TEST_FILE_NAME);

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, LINK_VISIT_TEST_NO_CYCLE_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

    if ((subgroup1 = H5Gcreate2(group_id, LINK_VISIT_TEST_NO_CYCLE_SUBGROUP_NAME2, H5P_DEFAULT, H5P_DEFAULT,
                                H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create first subgroup\n");
        goto error;
    }

    if ((subgroup2 = H5Gcreate2(group_id, LINK_VISIT_TEST_NO_CYCLE_SUBGROUP_NAME3, H5P_DEFAULT, H5P_DEFAULT,
                                H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create second subgroup\n");
        goto error;
    }

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    for (i = 0; i < LINK_VISIT_TEST_NO_CYCLE_DSET_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((fspace_id = H5Screate_simple(LINK_VISIT_TEST_NO_CYCLE_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(subgroup1, LINK_VISIT_TEST_NO_CYCLE_DSET_NAME, dset_dtype, fspace_id,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create first dataset\n");
    }

    if (H5Dclose(dset_id) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(subgroup2, LINK_VISIT_TEST_NO_CYCLE_DSET_NAME, dset_dtype, fspace_id,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create second dataset\n");
    }

    if (H5Dclose(dset_id) < 0)
        TEST_ERROR

    if (H5Lcreate_hard(subgroup1, LINK_VISIT_TEST_NO_CYCLE_DSET_NAME, subgroup1,
                       LINK_VISIT_TEST_NO_CYCLE_LINK_NAME1, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create first hard link\n");
        goto error;
    }

    if (H5Lcreate_soft("/" LINK_TEST_GROUP_NAME "/" LINK_VISIT_TEST_NO_CYCLE_SUBGROUP_NAME
                       "/" LINK_VISIT_TEST_NO_CYCLE_SUBGROUP_NAME2 "/" LINK_VISIT_TEST_NO_CYCLE_DSET_NAME,
                       subgroup1, LINK_VISIT_TEST_NO_CYCLE_LINK_NAME2, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create soft link\n");
        goto error;
    }

    if (H5Lcreate_external(ext_link_filename, "/", subgroup2, LINK_VISIT_TEST_NO_CYCLE_LINK_NAME3,
                           H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create external link\n");
        goto error;
    }

    if (H5Lcreate_hard(subgroup2, LINK_VISIT_TEST_NO_CYCLE_DSET_NAME, subgroup2,
                       LINK_VISIT_TEST_NO_CYCLE_LINK_NAME4, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create second hard link\n");
        goto error;
    }

    /* Verify the links have been created */
    if ((link_exists = H5Lexists(subgroup1, LINK_VISIT_TEST_NO_CYCLE_LINK_NAME1, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if first link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link 1 did not exist\n");
        goto error;
    }

    if ((link_exists = H5Lexists(subgroup1, LINK_VISIT_TEST_NO_CYCLE_LINK_NAME2, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if second link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link 2 did not exist\n");
        goto error;
    }

    if ((link_exists = H5Lexists(subgroup2, LINK_VISIT_TEST_NO_CYCLE_LINK_NAME3, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if third link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link 3 did not exist\n");
        goto error;
    }

    if ((link_exists = H5Lexists(subgroup2, LINK_VISIT_TEST_NO_CYCLE_LINK_NAME4, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if fourth link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link 4 did not exist\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link name in increasing order with H5Lvisit\n");
#endif

    if (H5Lvisit(group_id, H5_INDEX_NAME, H5_ITER_INC, link_visit_callback1, NULL) < 0) {
        H5_FAILED();
        printf("    H5Lvisit by index type name in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link name in decreasing order with H5Lvisit\n");
#endif

    if (H5Lvisit(group_id, H5_INDEX_NAME, H5_ITER_DEC, link_visit_callback1, NULL) < 0) {
        H5_FAILED();
        printf("    H5Lvisit by index type name in decreasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link creation order in increasing order with H5Lvisit\n");
#endif

    if (H5Lvisit(group_id, H5_INDEX_CRT_ORDER, H5_ITER_INC, link_visit_callback1, NULL) < 0) {
        H5_FAILED();
        printf("    H5Lvisit by index type creation order in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link creation order in decreasing order with H5Lvisit\n");
#endif

    if (H5Lvisit(group_id, H5_INDEX_CRT_ORDER, H5_ITER_DEC, link_visit_callback1, NULL) < 0) {
        H5_FAILED();
        printf("    H5Lvisit by index type creation order in decreasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link name in increasing order with H5Lvisit_by_name\n");
#endif

    if (H5Lvisit_by_name(file_id, "/" LINK_TEST_GROUP_NAME "/" LINK_VISIT_TEST_NO_CYCLE_SUBGROUP_NAME,
                         H5_INDEX_NAME, H5_ITER_INC, link_visit_callback1, NULL, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Lvisit_by_name by index type name in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link name in decreasing order with H5Lvisit_by_name\n");
#endif

    if (H5Lvisit_by_name(file_id, "/" LINK_TEST_GROUP_NAME "/" LINK_VISIT_TEST_NO_CYCLE_SUBGROUP_NAME,
                         H5_INDEX_NAME, H5_ITER_DEC, link_visit_callback1, NULL, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Lvisit_by_name by index type name in decreasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link creation order in increasing order with "
         "H5Lvisit_by_name\n");
#endif

    if (H5Lvisit_by_name(file_id, "/" LINK_TEST_GROUP_NAME "/" LINK_VISIT_TEST_NO_CYCLE_SUBGROUP_NAME,
                         H5_INDEX_CRT_ORDER, H5_ITER_INC, link_visit_callback1, NULL, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Lvisit_by_name by index type creation order in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link creation order in decreasing order with "
         "H5Lvisit_by_name\n");
#endif

    if (H5Lvisit_by_name(file_id, "/" LINK_TEST_GROUP_NAME "/" LINK_VISIT_TEST_NO_CYCLE_SUBGROUP_NAME,
                         H5_INDEX_CRT_ORDER, H5_ITER_DEC, link_visit_callback1, NULL, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Lvisit_by_name by index type creation order in decreasing order failed\n");
        goto error;
    }

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Gclose(subgroup1) < 0)
        TEST_ERROR
    if (H5Gclose(subgroup2) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(fspace_id);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(subgroup1);
        H5Gclose(subgroup2);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_link_visit_cycles(void)
{
    htri_t link_exists;
    hid_t  file_id = -1, fapl_id = -1;
    hid_t  container_group = -1, group_id = -1;
    hid_t  subgroup1 = -1, subgroup2 = -1;
    char   ext_link_filename[FILENAME_MAX_LENGTH];

    TESTING("link visit with cycles")

    snprintf(ext_link_filename, FILENAME_MAX_LENGTH, "%s/%s/%s", TEST_DIR_PREFIX, username,
             EXTERNAL_LINK_TEST_FILE_NAME);

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, LINK_VISIT_TEST_CYCLE_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT,
                               H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

    if ((subgroup1 = H5Gcreate2(group_id, LINK_VISIT_TEST_CYCLE_SUBGROUP_NAME2, H5P_DEFAULT, H5P_DEFAULT,
                                H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create first subgroup\n");
        goto error;
    }

    if ((subgroup2 = H5Gcreate2(group_id, LINK_VISIT_TEST_CYCLE_SUBGROUP_NAME3, H5P_DEFAULT, H5P_DEFAULT,
                                H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create second subgroup\n");
        goto error;
    }

    if (H5Lcreate_hard(group_id, LINK_VISIT_TEST_CYCLE_SUBGROUP_NAME2, subgroup1,
                       LINK_VISIT_TEST_CYCLE_LINK_NAME1, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create first hard link\n");
        goto error;
    }

    if (H5Lcreate_soft("/" LINK_TEST_GROUP_NAME "/" LINK_VISIT_TEST_CYCLE_SUBGROUP_NAME
                       "/" LINK_VISIT_TEST_CYCLE_SUBGROUP_NAME2,
                       subgroup1, LINK_VISIT_TEST_CYCLE_LINK_NAME2, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create soft link\n");
        goto error;
    }

    if (H5Lcreate_external(ext_link_filename, "/", subgroup2, LINK_VISIT_TEST_CYCLE_LINK_NAME3, H5P_DEFAULT,
                           H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create external link\n");
        goto error;
    }

    if (H5Lcreate_hard(group_id, LINK_VISIT_TEST_CYCLE_SUBGROUP_NAME3, subgroup2,
                       LINK_VISIT_TEST_CYCLE_LINK_NAME4, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create second hard link\n");
        goto error;
    }

    /* Verify the links have been created */
    if ((link_exists = H5Lexists(subgroup1, LINK_VISIT_TEST_CYCLE_LINK_NAME1, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if first link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link 1 did not exist\n");
        goto error;
    }

    if ((link_exists = H5Lexists(subgroup1, LINK_VISIT_TEST_CYCLE_LINK_NAME2, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if second link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link 2 did not exist\n");
        goto error;
    }

    if ((link_exists = H5Lexists(subgroup2, LINK_VISIT_TEST_CYCLE_LINK_NAME3, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if third link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link 3 did not exist\n");
        goto error;
    }

    if ((link_exists = H5Lexists(subgroup2, LINK_VISIT_TEST_CYCLE_LINK_NAME4, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if fourth link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link 4 did not exist\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link name in increasing order with H5Lvisit\n");
#endif

    if (H5Lvisit(group_id, H5_INDEX_NAME, H5_ITER_INC, link_visit_callback2, NULL) < 0) {
        H5_FAILED();
        printf("    H5Lvisit by index type name in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link name in decreasing order with H5Lvisit\n");
#endif

    if (H5Lvisit(group_id, H5_INDEX_NAME, H5_ITER_DEC, link_visit_callback2, NULL) < 0) {
        H5_FAILED();
        printf("    H5Lvisit by index type name in decreasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link creation order in increasing order with H5Lvisit\n");
#endif

    if (H5Lvisit(group_id, H5_INDEX_CRT_ORDER, H5_ITER_INC, link_visit_callback2, NULL) < 0) {
        H5_FAILED();
        printf("    H5Lvisit by index type creation order in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link creation order in decreasing order with H5Lvisit\n");
#endif

    if (H5Lvisit(group_id, H5_INDEX_CRT_ORDER, H5_ITER_DEC, link_visit_callback2, NULL) < 0) {
        H5_FAILED();
        printf("    H5Lvisit by index type creation order in decreasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link name in increasing order with H5Lvisit_by_name\n");
#endif

    if (H5Lvisit_by_name(file_id, "/" LINK_TEST_GROUP_NAME "/" LINK_VISIT_TEST_CYCLE_SUBGROUP_NAME,
                         H5_INDEX_NAME, H5_ITER_INC, link_visit_callback2, NULL, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Lvisit_by_name by index type name in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link name in decreasing order with H5Lvisit_by_name\n");
#endif

    if (H5Lvisit_by_name(file_id, "/" LINK_TEST_GROUP_NAME "/" LINK_VISIT_TEST_CYCLE_SUBGROUP_NAME,
                         H5_INDEX_NAME, H5_ITER_DEC, link_visit_callback2, NULL, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Lvisit_by_name by index type name in decreasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link creation order in increasing order with "
         "H5Lvisit_by_name\n");
#endif

    if (H5Lvisit_by_name(file_id, "/" LINK_TEST_GROUP_NAME "/" LINK_VISIT_TEST_CYCLE_SUBGROUP_NAME,
                         H5_INDEX_CRT_ORDER, H5_ITER_INC, link_visit_callback2, NULL, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Lvisit_by_name by index type creation order in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link creation order in decreasing order with "
         "H5Lvisit_by_name\n");
#endif

    if (H5Lvisit_by_name(file_id, "/" LINK_TEST_GROUP_NAME "/" LINK_VISIT_TEST_CYCLE_SUBGROUP_NAME,
                         H5_INDEX_CRT_ORDER, H5_ITER_DEC, link_visit_callback2, NULL, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Lvisit_by_name by index type creation order in decreasing order failed\n");
        goto error;
    }

    if (H5Gclose(subgroup1) < 0)
        TEST_ERROR
    if (H5Gclose(subgroup2) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(subgroup1);
        H5Gclose(subgroup2);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_link_visit_0_links(void)
{
    hid_t file_id = -1, fapl_id = -1;
    hid_t container_group = -1, group_id = -1;
    hid_t subgroup1 = -1, subgroup2 = -1;

    TESTING("link visit on group with subgroups containing 0 links")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, LINK_VISIT_TEST_0_LINKS_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

    if ((subgroup1 = H5Gcreate2(group_id, LINK_VISIT_TEST_0_LINKS_SUBGROUP_NAME2, H5P_DEFAULT, H5P_DEFAULT,
                                H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create first subgroup\n");
        goto error;
    }

    if ((subgroup2 = H5Gcreate2(group_id, LINK_VISIT_TEST_0_LINKS_SUBGROUP_NAME3, H5P_DEFAULT, H5P_DEFAULT,
                                H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create second subgroup\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link name in increasing order with H5Lvisit\n");
#endif

    if (H5Lvisit(group_id, H5_INDEX_NAME, H5_ITER_INC, link_visit_callback3, NULL) < 0) {
        H5_FAILED();
        printf("    H5Lvisit by index type name in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link name in decreasing order with H5Lvisit\n");
#endif

    if (H5Lvisit(group_id, H5_INDEX_NAME, H5_ITER_DEC, link_visit_callback3, NULL) < 0) {
        H5_FAILED();
        printf("    H5Lvisit by index type name in decreasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link creation order in increasing order with H5Lvisit\n");
#endif

    if (H5Lvisit(group_id, H5_INDEX_CRT_ORDER, H5_ITER_INC, link_visit_callback3, NULL) < 0) {
        H5_FAILED();
        printf("    H5Lvisit by index type creation order in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link creation order in decreasing order with H5Lvisit\n");
#endif

    if (H5Lvisit(group_id, H5_INDEX_CRT_ORDER, H5_ITER_DEC, link_visit_callback3, NULL) < 0) {
        H5_FAILED();
        printf("    H5Lvisit by index type creation order in decreasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link name in increasing order with H5Lvisit_by_name\n");
#endif

    if (H5Lvisit_by_name(file_id, "/" LINK_TEST_GROUP_NAME "/" LINK_VISIT_TEST_0_LINKS_SUBGROUP_NAME,
                         H5_INDEX_NAME, H5_ITER_INC, link_visit_callback3, NULL, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Lvisit_by_name by index type name in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link name in decreasing order with H5Lvisit_by_name\n");
#endif

    if (H5Lvisit_by_name(file_id, "/" LINK_TEST_GROUP_NAME "/" LINK_VISIT_TEST_0_LINKS_SUBGROUP_NAME,
                         H5_INDEX_NAME, H5_ITER_DEC, link_visit_callback3, NULL, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Lvisit_by_name by index type name in decreasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link creation order in increasing order with "
         "H5Lvisit_by_name\n");
#endif

    if (H5Lvisit_by_name(file_id, "/" LINK_TEST_GROUP_NAME "/" LINK_VISIT_TEST_0_LINKS_SUBGROUP_NAME,
                         H5_INDEX_CRT_ORDER, H5_ITER_INC, link_visit_callback3, NULL, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Lvisit_by_name by index type creation order in increasing order failed\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Recursively iterating over links by link creation order in decreasing order with "
         "H5Lvisit_by_name\n");
#endif

    if (H5Lvisit_by_name(file_id, "/" LINK_TEST_GROUP_NAME "/" LINK_VISIT_TEST_0_LINKS_SUBGROUP_NAME,
                         H5_INDEX_CRT_ORDER, H5_ITER_DEC, link_visit_callback3, NULL, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    H5Lvisit_by_name by index type creation order in decreasing order failed\n");
        goto error;
    }

    if (H5Gclose(subgroup1) < 0)
        TEST_ERROR
    if (H5Gclose(subgroup2) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(subgroup1);
        H5Gclose(subgroup2);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_unused_link_API_calls(void)
{
    TESTING("unused link API calls")

    /* None currently that aren't planned to be used */
#ifdef RV_CONNECTOR_DEBUG
    puts("Currently no API calls to test here\n");
#endif

    SKIPPED();

    return 0;
}

/*****************************************************
 *                                                   *
 *           Plugin Object Interface tests           *
 *                                                   *
 *****************************************************/

static int
test_open_dataset_generically(void)
{
    hsize_t dims[GENERIC_DATASET_OPEN_TEST_SPACE_RANK];
    size_t  i;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   dset_id         = -1;
    hid_t   dset_dtype      = -1;
    hid_t   fspace_id       = -1;

    TESTING("open dataset generically w/ H5Oopen()")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, OBJECT_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    for (i = 0; i < GENERIC_DATASET_OPEN_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((fspace_id = H5Screate_simple(GENERIC_DATASET_OPEN_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, GENERIC_DATASET_OPEN_TEST_DSET_NAME, dset_dtype, fspace_id,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if (H5Dclose(dset_id) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Opening dataset with H5Oopen\n");
#endif

    if ((dset_id = H5Oopen(file_id, "/" OBJECT_TEST_GROUP_NAME "/" GENERIC_DATASET_OPEN_TEST_DSET_NAME,
                           H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset with H5Oopen()\n");
        goto error;
    }

    H5E_BEGIN_TRY
    {
#if 0
        if (H5Dclose(dset_id) < 0)
            TEST_ERROR
#endif

#ifdef RV_CONNECTOR_DEBUG
        puts("Opening dataset with H5Oopen_by_idx\n");
#endif

        if ((/*dset_id = */ H5Oopen_by_idx(file_id, "/" OBJECT_TEST_GROUP_NAME, H5_INDEX_NAME, H5_ITER_INC, 0,
                                           H5P_DEFAULT)) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded!\n");
            goto error;
        }

#if 0
        if (H5Dclose(dset_id) < 0)
            TEST_ERROR
#endif

#ifdef RV_CONNECTOR_DEBUG
        puts("Opening dataset with H5Oopen_by_addr\n");
#endif

        if ((/*dset_id = */ H5Oopen_by_addr(file_id, 0)) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded!\n");
            goto error;
        }
    }
    H5E_END_TRY;

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(fspace_id);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_open_group_generically(void)
{
    hid_t file_id = -1, fapl_id = -1;
    hid_t container_group = -1;
    hid_t group_id        = -1;

    TESTING("open group generically w/ H5Oopen()")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, OBJECT_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, GENERIC_GROUP_OPEN_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT,
                               H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group\n");
        goto error;
    }

    if (H5Gclose(group_id) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Opening group with H5Oopen\n");
#endif

    if ((group_id = H5Oopen(file_id, "/" OBJECT_TEST_GROUP_NAME "/" GENERIC_GROUP_OPEN_TEST_GROUP_NAME,
                            H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open group with H5Oopen()\n");
        goto error;
    }

    H5E_BEGIN_TRY
    {
#if 0
        if (H5Gclose(group_id) < 0)
            TEST_ERROR
#endif

#ifdef RV_CONNECTOR_DEBUG
        puts("Opening group with H5Oopen_by_idx\n");
#endif

        if ((/*group_id = */ H5Oopen_by_idx(file_id, "/" OBJECT_TEST_GROUP_NAME, H5_INDEX_NAME, H5_ITER_INC,
                                            0, H5P_DEFAULT)) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded!\n");
            goto error;
        }

#if 0
        if (H5Gclose(group_id) < 0)
            TEST_ERROR
#endif

#ifdef RV_CONNECTOR_DEBUG
        puts("Opening group with H5Oopen_by_addr\n");
#endif

        if ((/*group_id = */ H5Oopen_by_addr(file_id, 0)) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded!\n");
            goto error;
        }
    }
    H5E_END_TRY;

    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_open_datatype_generically(void)
{
    hid_t file_id = -1, fapl_id = -1;
    hid_t container_group = -1;
    hid_t type_id         = -1;

    TESTING("open datatype generically w/ H5Oopen()")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, OBJECT_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((type_id = generate_random_datatype(H5T_NO_CLASS)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if (H5Tcommit2(container_group, GENERIC_DATATYPE_OPEN_TEST_TYPE_NAME, type_id, H5P_DEFAULT, H5P_DEFAULT,
                   H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't commit datatype\n");
        goto error;
    }

    if (H5Tclose(type_id) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Opening datatype with H5Oopen\n");
#endif

    if ((type_id = H5Oopen(file_id, "/" OBJECT_TEST_GROUP_NAME "/" GENERIC_DATATYPE_OPEN_TEST_TYPE_NAME,
                           H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open datatype generically w/ H5Oopen()\n");
        goto error;
    }

    H5E_BEGIN_TRY
    {
#if 0
        if (H5Tclose(type_id) < 0)
            TEST_ERROR
#endif

#ifdef RV_CONNECTOR_DEBUG
        puts("Opening datatype with H5Oopen_by_idx\n");
#endif

        if ((/*type_id = */ H5Oopen_by_idx(file_id, "/" OBJECT_TEST_GROUP_NAME, H5_INDEX_NAME, H5_ITER_INC, 0,
                                           H5P_DEFAULT)) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded!\n");
            goto error;
        }

#if 0
        if (H5Tclose(type_id) < 0)
            TEST_ERROR
#endif

#ifdef RV_CONNECTOR_DEBUG
        puts("Opening datatype with H5Oopen_by_addr\n");
#endif

        if ((/*type_id = */ H5Oopen_by_addr(file_id, 0)) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded!\n");
            goto error;
        }
    }
    H5E_END_TRY;

    if (H5Tclose(type_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Tclose(type_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_object_exists(void)
{
    hsize_t dims[OBJECT_EXISTS_TEST_DSET_SPACE_RANK];
    size_t  i;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id    = -1;
    hid_t   dtype_id   = -1;
    hid_t   fspace_id  = -1;
    hid_t   dset_dtype = -1;

    TESTING("object exists by name")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, OBJECT_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, OBJECT_EXISTS_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT,
                               H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

    if ((dtype_id = generate_random_datatype(H5T_NO_CLASS)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
    }

    if (H5Tcommit2(group_id, OBJECT_EXISTS_TEST_DTYPE_NAME, dtype_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT) <
        0) {
        H5_FAILED();
        printf("    couldn't commit datatype\n");
        goto error;
    }

    for (i = 0; i < OBJECT_EXISTS_TEST_DSET_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((fspace_id = H5Screate_simple(OBJECT_EXISTS_TEST_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, OBJECT_EXISTS_TEST_DSET_NAME, dset_dtype, fspace_id, H5P_DEFAULT,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    H5E_BEGIN_TRY
    {
        /* H5Oexists_by_name for hard links should always succeed. */

        /* Check if the group exists */
        if (H5Oexists_by_name(file_id, OBJECT_TEST_GROUP_NAME "/" OBJECT_EXISTS_TEST_SUBGROUP_NAME,
                              H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded!\n");
            goto error;
        }

        /* Check if the datatype exists */
        if (H5Oexists_by_name(file_id,
                              OBJECT_TEST_GROUP_NAME "/" OBJECT_EXISTS_TEST_SUBGROUP_NAME
                                                     "/" OBJECT_EXISTS_TEST_DTYPE_NAME,
                              H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded!\n");
            goto error;
        }

        /* Check if the dataset exists */
        if (H5Oexists_by_name(file_id,
                              OBJECT_TEST_GROUP_NAME "/" OBJECT_EXISTS_TEST_SUBGROUP_NAME
                                                     "/" OBJECT_EXISTS_TEST_DSET_NAME,
                              H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded!\n");
            goto error;
        }

        /* Checking for a soft link may fail if the link doesn't resolve. */
    }
    H5E_END_TRY;

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Tclose(dtype_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(fspace_id);
        H5Tclose(dset_dtype);
        H5Tclose(dtype_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_incr_decr_refcount(void)
{
    hid_t file_id = -1, fapl_id = -1;

    TESTING("H5Oincr/decr_refcount")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Testing unsupported APIs H5Oincr/decr_refcount\n");
#endif

    H5E_BEGIN_TRY
    {
        if (H5Oincr_refcount(file_id) >= 0)
            TEST_ERROR
        if (H5Odecr_refcount(file_id) >= 0)
            TEST_ERROR
    }
    H5E_END_TRY;

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_h5o_copy(void)
{
    hsize_t dims[OBJECT_COPY_TEST_SPACE_RANK];
    size_t  i;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id  = -1;
    hid_t   space_id = -1;

    TESTING("object copy")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, OBJECT_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, OBJECT_COPY_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT,
                               H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container subgroup\n");
        goto error;
    }

    for (i = 0; i < OBJECT_COPY_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((space_id = H5Screate_simple(OBJECT_COPY_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, OBJECT_COPY_TEST_DSET_NAME, OBJECT_COPY_TEST_DSET_DTYPE, space_id,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Copying object with H5Ocopy\n");
#endif

    H5E_BEGIN_TRY
    {
        if (H5Ocopy(group_id, OBJECT_COPY_TEST_DSET_NAME, group_id, OBJECT_COPY_TEST_DSET_NAME2, H5P_DEFAULT,
                    H5P_DEFAULT) >= 0) {
            H5_FAILED();
            printf("    unsupported API succeeded\n");
            goto error;
        }
    }
    H5E_END_TRY;

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(space_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_h5o_close(void)
{
    hsize_t dims[H5O_CLOSE_TEST_SPACE_RANK];
    size_t  i;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   fspace_id  = -1;
    hid_t   dtype_id   = -1;
    hid_t   dset_id    = -1;
    hid_t   dset_dtype = -1;

    TESTING("H5Oclose")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, OBJECT_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    for (i = 0; i < H5O_CLOSE_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((fspace_id = H5Screate_simple(H5O_CLOSE_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, H5O_CLOSE_TEST_DSET_NAME, dset_dtype, fspace_id, H5P_DEFAULT,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if ((dtype_id = generate_random_datatype(H5T_NO_CLASS)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if (H5Tcommit2(container_group, H5O_CLOSE_TEST_TYPE_NAME, dtype_id, H5P_DEFAULT, H5P_DEFAULT,
                   H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't commit datatype\n");
        goto error;
    }

    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Tclose(dtype_id) < 0)
        TEST_ERROR

    if ((group_id = H5Oopen(file_id, "/", H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open group with H5Oopen()\n");
        goto error;
    }

    if ((dset_id = H5Oopen(file_id, "/" OBJECT_TEST_GROUP_NAME "/" H5O_CLOSE_TEST_DSET_NAME, H5P_DEFAULT)) <
        0) {
        H5_FAILED();
        printf("    couldn't open dataset with H5Oopen()\n");
        goto error;
    }

    if ((dtype_id = H5Oopen(file_id, "/" OBJECT_TEST_GROUP_NAME "/" H5O_CLOSE_TEST_TYPE_NAME, H5P_DEFAULT)) <
        0) {
        H5_FAILED();
        printf("    couldn't open datatype with H5Oopen()\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Making sure H5Oclose does its job correctly\n");
#endif

    if (H5Oclose(group_id) < 0)
        TEST_ERROR
    if (H5Oclose(dtype_id) < 0)
        TEST_ERROR
    if (H5Oclose(dset_id) < 0)
        TEST_ERROR
    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(fspace_id);
        H5Tclose(dset_dtype);
        H5Tclose(dtype_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_object_visit(void)
{
    hid_t file_id = -1, fapl_id = -1;
    hid_t container_group = -1;

    TESTING("H5Ovisit")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, OBJECT_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    H5E_BEGIN_TRY
    {
#ifdef RV_CONNECTOR_DEBUG
        puts("Visiting objects with H5Ovisit\n");
#endif

        if (H5Ovisit3(container_group, H5_INDEX_NAME, H5_ITER_INC, object_visit_callback, NULL,
                      H5O_INFO_ALL) < 0) {
            H5_FAILED();
            printf("    H5Ovisit using container_group failed!\n");
            goto error;
        }
        if (H5Ovisit(file_id, H5_INDEX_NAME, H5_ITER_DEC, object_visit_callback, NULL, H5O_INFO_ALL) < 0) {
            H5_FAILED();
            printf("    H5Ovisit using file_id failed!\n");
            goto error;
        }

#ifdef RV_CONNECTOR_DEBUG
        puts("Visiting objects with H5Ovisit_by_name\n");
#endif

        if (H5Ovisit_by_name3(file_id, "/" OBJECT_TEST_GROUP_NAME, H5_INDEX_NAME, H5_ITER_INC,
                              object_visit_callback, NULL, H5O_INFO_ALL, H5P_DEFAULT) < 0) {
            H5_FAILED();
            printf("    H5Ovisit failed!\n");
            goto error;
        }
    }
    H5E_END_TRY;

    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_create_obj_ref(void)
{
    rv_obj_ref_t ref;
    hid_t        file_id = -1, fapl_id = -1;

    TESTING("create an object reference")

    SKIPPED()
    return 0;

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating an object reference\n");
#endif

    if (H5Rcreate((void *)&ref, file_id, "/", H5R_OBJECT, -1) < 0) {
        H5_FAILED();
        printf("    couldn't create obj. ref\n");
        goto error;
    }

    if (H5R_OBJECT != ref.ref_type)
        TEST_ERROR
    if (H5I_GROUP != ref.ref_obj_type)
        TEST_ERROR
    if (strcmp(H5rest_get_object_uri(file_id), ref.ref_obj_URI))
        TEST_ERROR

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_dereference_reference(void)
{
    TESTING("dereference a reference")

    /* H5Rdereference2 */

    SKIPPED();

    return 0;
}

static int
test_get_ref_type(void)
{
    rv_obj_ref_t ref_array[3];
    H5O_type_t   obj_type;
    hsize_t      dims[OBJ_REF_GET_TYPE_TEST_SPACE_RANK];
    size_t       i;
    hid_t        file_id = -1, fapl_id = -1;
    hid_t        container_group = -1, group_id = -1;
    hid_t        ref_dset_id    = -1;
    hid_t        ref_dtype_id   = -1;
    hid_t        ref_dset_dtype = -1;
    hid_t        space_id       = -1;

    TESTING("retrieve type of object reference by an object/region reference")
    SKIPPED()
    return 0;

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, OBJECT_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, OBJ_REF_GET_TYPE_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT,
                               H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    for (i = 0; i < OBJ_REF_GET_TYPE_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % 8 + 1);

    if ((space_id = H5Screate_simple(OBJ_REF_GET_TYPE_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((ref_dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    /* Create the dataset and datatype which will be referenced */
    if ((ref_dset_id = H5Dcreate2(group_id, OBJ_REF_GET_TYPE_TEST_DSET_NAME, ref_dset_dtype, space_id,
                                  H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset for referencing\n");
        goto error;
    }

    if ((ref_dtype_id = generate_random_datatype(H5T_NO_CLASS)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if (H5Tcommit2(group_id, OBJ_REF_GET_TYPE_TEST_TYPE_NAME, ref_dtype_id, H5P_DEFAULT, H5P_DEFAULT,
                   H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype for referencing\n");
        goto error;
    }

    {
        /* TODO: Temporary workaround for datatypes */
        if (H5Tclose(ref_dtype_id) < 0)
            TEST_ERROR

        if ((ref_dtype_id = H5Topen2(group_id, OBJ_REF_GET_TYPE_TEST_TYPE_NAME, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't open datatype for referencing\n");
            goto error;
        }
    }

    /* Create and check the group reference */
    if (H5Rcreate(&ref_array[0], file_id, "/", H5R_OBJECT, -1) < 0) {
        H5_FAILED();
        printf("    couldn't create group object reference\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving the type of the referenced object for this reference\n");
#endif

    if (H5Rget_obj_type2(file_id, H5R_OBJECT, &ref_array[0], &obj_type) < 0) {
        H5_FAILED();
        printf("    couldn't get object reference's object type\n");
        goto error;
    }

    if (H5O_TYPE_GROUP != obj_type) {
        H5_FAILED();
        printf("    referenced object was not a group\n");
        goto error;
    }

    /* Create and check the datatype reference */
    if (H5Rcreate(&ref_array[1], group_id, OBJ_REF_GET_TYPE_TEST_TYPE_NAME, H5R_OBJECT, -1) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype object reference\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving the type of the referenced object for this reference\n");
#endif

    if (H5Rget_obj_type2(file_id, H5R_OBJECT, &ref_array[1], &obj_type) < 0) {
        H5_FAILED();
        printf("    couldn't get object reference's object type\n");
        goto error;
    }

    if (H5O_TYPE_NAMED_DATATYPE != obj_type) {
        H5_FAILED();
        printf("    referenced object was not a datatype\n");
        goto error;
    }

    /* Create and check the dataset reference */
    if (H5Rcreate(&ref_array[2], group_id, OBJ_REF_GET_TYPE_TEST_DSET_NAME, H5R_OBJECT, -1) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset object reference\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Retrieving the type of the referenced object for this reference\n");
#endif

    if (H5Rget_obj_type2(file_id, H5R_OBJECT, &ref_array[2], &obj_type) < 0) {
        H5_FAILED();
        printf("    couldn't get object reference's object type\n");
        goto error;
    }

    if (H5O_TYPE_DATASET != obj_type) {
        H5_FAILED();
        printf("    referenced object was not a dataset\n");
        goto error;
    }

    /* TODO: Support for region references in this test */

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Tclose(ref_dset_dtype) < 0)
        TEST_ERROR
    if (H5Tclose(ref_dtype_id) < 0)
        TEST_ERROR
    if (H5Dclose(ref_dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(space_id);
        H5Tclose(ref_dset_dtype);
        H5Tclose(ref_dtype_id);
        H5Dclose(ref_dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_get_ref_name(void)
{
    TESTING("get ref. name")

    /* H5Rget_name */

    SKIPPED();

    return 0;
}

static int
test_get_region(void)
{
    TESTING("get region for region reference")

    /* H5Rget_region */

    SKIPPED();

    return 0;
}

static int
test_write_dataset_w_obj_refs(void)
{
    rv_obj_ref_t *ref_array = NULL;
    hsize_t       dims[OBJ_REF_DATASET_WRITE_TEST_SPACE_RANK];
    size_t        i, ref_array_size = 0;
    hid_t         file_id = -1, fapl_id = -1;
    hid_t         container_group = -1, group_id = -1;
    hid_t         dset_id = -1, ref_dset_id = -1;
    hid_t         ref_dtype_id   = -1;
    hid_t         ref_dset_dtype = -1;
    hid_t         space_id       = -1;

    TESTING("write to a dataset w/ object reference type")
    SKIPPED()
    return 0;

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, OBJECT_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, OBJ_REF_DATASET_WRITE_TEST_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    for (i = 0; i < OBJ_REF_DATASET_WRITE_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % 8 + 1);

    if ((space_id = H5Screate_simple(OBJ_REF_DATASET_WRITE_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((ref_dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    /* Create the dataset and datatype which will be referenced */
    if ((ref_dset_id = H5Dcreate2(group_id, OBJ_REF_DATASET_WRITE_TEST_REF_DSET_NAME, ref_dset_dtype,
                                  space_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset for referencing\n");
        goto error;
    }

    if ((ref_dtype_id = generate_random_datatype(H5T_NO_CLASS)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if (H5Tcommit2(group_id, OBJ_REF_DATASET_WRITE_TEST_REF_TYPE_NAME, ref_dtype_id, H5P_DEFAULT, H5P_DEFAULT,
                   H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype for referencing\n");
        goto error;
    }

    {
        /* TODO: Temporary workaround for datatypes */
        if (H5Tclose(ref_dtype_id) < 0)
            TEST_ERROR

        if ((ref_dtype_id = H5Topen2(group_id, OBJ_REF_DATASET_WRITE_TEST_REF_TYPE_NAME, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't open datatype for referencing\n");
            goto error;
        }
    }

    if ((dset_id = H5Dcreate2(group_id, OBJ_REF_DATASET_WRITE_TEST_DSET_NAME, H5T_STD_REF_OBJ, space_id,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    for (i = 0, ref_array_size = 1; i < OBJ_REF_DATASET_WRITE_TEST_SPACE_RANK; i++)
        ref_array_size *= dims[i];

    if (NULL == (ref_array = (rv_obj_ref_t *)malloc(ref_array_size * sizeof(*ref_array))))
        TEST_ERROR

    for (i = 0; i < dims[0]; i++) {
        const char *URI;

        /* Create a reference to either a group, datatype or dataset */
        switch (rand() % 3) {
            case 0:
                if (H5Rcreate(&ref_array[i], file_id, "/", H5R_OBJECT, -1) < 0) {
                    H5_FAILED();
                    printf("    couldn't create reference\n");
                    goto error;
                }

                if (NULL == (URI = H5rest_get_object_uri(file_id)))
                    TEST_ERROR

                break;

            case 1:
                if (H5Rcreate(&ref_array[i], group_id, OBJ_REF_DATASET_WRITE_TEST_REF_TYPE_NAME, H5R_OBJECT,
                              -1) < 0) {
                    H5_FAILED();
                    printf("    couldn't create reference\n");
                    goto error;
                }

                if (NULL == (URI = H5rest_get_object_uri(ref_dtype_id)))
                    TEST_ERROR

                break;

            case 2:
                if (H5Rcreate(&ref_array[i], group_id, OBJ_REF_DATASET_WRITE_TEST_REF_DSET_NAME, H5R_OBJECT,
                              -1) < 0) {
                    H5_FAILED();
                    printf("    couldn't create reference\n");
                    goto error;
                }

                if (NULL == (URI = H5rest_get_object_uri(ref_dset_id)))
                    TEST_ERROR

                break;

            default:
                TEST_ERROR
        }

        if (strcmp(URI, ref_array[i].ref_obj_URI)) {
            H5_FAILED();
            printf("    ref type had mismatched URI\n");
            goto error;
        }
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Writing to dataset with buffer of object references\n");
#endif

    if (H5Dwrite(dset_id, H5T_STD_REF_OBJ, H5S_ALL, H5S_ALL, H5P_DEFAULT, ref_array) < 0) {
        H5_FAILED();
        printf("    couldn't write to dataset\n");
        goto error;
    }

    if (ref_array) {
        free(ref_array);
        ref_array = NULL;
    }

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Tclose(ref_dset_dtype) < 0)
        TEST_ERROR
    if (H5Tclose(ref_dtype_id) < 0)
        TEST_ERROR
    if (H5Dclose(ref_dset_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        if (ref_array)
            free(ref_array);
        H5Sclose(space_id);
        H5Tclose(ref_dset_dtype);
        H5Tclose(ref_dtype_id);
        H5Dclose(ref_dset_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_read_dataset_w_obj_refs(void)
{
    rv_obj_ref_t *ref_array = NULL;
    hsize_t       dims[OBJ_REF_DATASET_READ_TEST_SPACE_RANK];
    size_t        i, ref_array_size = 0;
    hid_t         file_id = -1, fapl_id = -1;
    hid_t         container_group = -1, group_id = -1;
    hid_t         dset_id = -1, ref_dset_id = -1;
    hid_t         ref_dtype_id   = -1;
    hid_t         ref_dset_dtype = -1;
    hid_t         space_id       = -1;

    TESTING("read from a dataset w/ object reference type")

    SKIPPED()
    return 0;

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, OBJECT_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, OBJ_REF_DATASET_READ_TEST_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    for (i = 0; i < OBJ_REF_DATASET_READ_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % 8 + 1);

    if ((space_id = H5Screate_simple(OBJ_REF_DATASET_READ_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((ref_dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    /* Create the dataset and datatype which will be referenced */
    if ((ref_dset_id = H5Dcreate2(group_id, OBJ_REF_DATASET_READ_TEST_REF_DSET_NAME, ref_dset_dtype, space_id,
                                  H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset for referencing\n");
        goto error;
    }

    if ((ref_dtype_id = generate_random_datatype(H5T_NO_CLASS)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if (H5Tcommit2(group_id, OBJ_REF_DATASET_READ_TEST_REF_TYPE_NAME, ref_dtype_id, H5P_DEFAULT, H5P_DEFAULT,
                   H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype for referencing\n");
        goto error;
    }

    {
        /* TODO: Temporary workaround for datatypes */
        if (H5Tclose(ref_dtype_id) < 0)
            TEST_ERROR

        if ((ref_dtype_id = H5Topen2(group_id, OBJ_REF_DATASET_READ_TEST_REF_TYPE_NAME, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't open datatype for referencing\n");
            goto error;
        }
    }

    if ((dset_id = H5Dcreate2(group_id, OBJ_REF_DATASET_READ_TEST_DSET_NAME, H5T_STD_REF_OBJ, space_id,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    for (i = 0, ref_array_size = 1; i < OBJ_REF_DATASET_READ_TEST_SPACE_RANK; i++)
        ref_array_size *= dims[i];

    if (NULL == (ref_array = (rv_obj_ref_t *)malloc(ref_array_size * sizeof(*ref_array))))
        TEST_ERROR

    for (i = 0; i < dims[0]; i++) {
        const char *URI;

        /* Create a reference to either a group, datatype or dataset */
        switch (rand() % 3) {
            case 0:
                if (H5Rcreate(&ref_array[i], file_id, "/", H5R_OBJECT, -1) < 0) {
                    H5_FAILED();
                    printf("    couldn't create reference\n");
                    goto error;
                }

                if (NULL == (URI = H5rest_get_object_uri(file_id)))
                    TEST_ERROR

                break;

            case 1:
                if (H5Rcreate(&ref_array[i], group_id, OBJ_REF_DATASET_READ_TEST_REF_TYPE_NAME, H5R_OBJECT,
                              -1) < 0) {
                    H5_FAILED();
                    printf("    couldn't create reference\n");
                    goto error;
                }

                if (NULL == (URI = H5rest_get_object_uri(ref_dtype_id)))
                    TEST_ERROR

                break;

            case 2:
                if (H5Rcreate(&ref_array[i], group_id, OBJ_REF_DATASET_READ_TEST_REF_DSET_NAME, H5R_OBJECT,
                              -1) < 0) {
                    H5_FAILED();
                    printf("    couldn't create reference\n");
                    goto error;
                }

                if (NULL == (URI = H5rest_get_object_uri(ref_dset_id)))
                    TEST_ERROR

                break;

            default:
                TEST_ERROR
        }

        if (strcmp(URI, ref_array[i].ref_obj_URI)) {
            H5_FAILED();
            printf("    ref type had mismatched URI\n");
            goto error;
        }
    }

    if (H5Dwrite(dset_id, H5T_STD_REF_OBJ, H5S_ALL, H5S_ALL, H5P_DEFAULT, ref_array) < 0) {
        H5_FAILED();
        printf("    couldn't write to dataset\n");
        goto error;
    }

    /* Now read from the dataset */
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR

    if ((dset_id = H5Dopen2(group_id, OBJ_REF_DATASET_READ_TEST_DSET_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Reading from dataset with object reference type\n");
#endif

    memset(ref_array, 0, ref_array_size * sizeof(*ref_array));

    if (H5Dread(dset_id, H5T_STD_REF_OBJ, H5S_ALL, H5S_ALL, H5P_DEFAULT, ref_array) < 0) {
        H5_FAILED();
        printf("    couldn't read from dataset\n");
        goto error;
    }

    for (i = 0; i < dims[0]; i++) {
        /* Check the reference type */
        if (H5R_OBJECT != ref_array[i].ref_type) {
            H5_FAILED();
            printf("    ref type was not H5R_OBJECT\n");
            goto error;
        }

        /* Check the object type referenced */
        if (H5I_FILE != ref_array[i].ref_obj_type && H5I_GROUP != ref_array[i].ref_obj_type &&
            H5I_DATATYPE != ref_array[i].ref_obj_type && H5I_DATASET != ref_array[i].ref_obj_type) {
            H5_FAILED();
            printf("    ref object type mismatch\n");
            goto error;
        }

        /* Check the URI of the referenced object according to
         * the server spec where each URI is prefixed as
         * 'X-', where X is a character denoting the type
         * of object */
        if ((ref_array[i].ref_obj_URI[1] != '-') ||
            (ref_array[i].ref_obj_URI[0] != 'g' && ref_array[i].ref_obj_URI[0] != 't' &&
             ref_array[i].ref_obj_URI[0] != 'd')) {
            H5_FAILED();
            printf("    ref URI mismatch\n");
            goto error;
        }
    }

    if (ref_array) {
        free(ref_array);
        ref_array = NULL;
    }

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Tclose(ref_dset_dtype) < 0)
        TEST_ERROR
    if (H5Tclose(ref_dtype_id) < 0)
        TEST_ERROR
    if (H5Dclose(ref_dset_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        if (ref_array)
            free(ref_array);
        H5Sclose(space_id);
        H5Tclose(ref_dset_dtype);
        H5Tclose(ref_dtype_id);
        H5Dclose(ref_dset_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_write_dataset_w_obj_refs_empty_data(void)
{
    rv_obj_ref_t *ref_array = NULL;
    hsize_t       dims[OBJ_REF_DATASET_EMPTY_WRITE_TEST_SPACE_RANK];
    size_t        i, ref_array_size = 0;
    hid_t         file_id = -1, fapl_id = -1;
    hid_t         container_group = -1, group_id = -1;
    hid_t         dset_id  = -1;
    hid_t         space_id = -1;

    TESTING("write to a dataset w/ object reference type and some empty data")

    SKIPPED()
    return 0;

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, OBJECT_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, OBJ_REF_DATASET_EMPTY_WRITE_TEST_SUBGROUP_NAME, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group");
        goto error;
    }

    for (i = 0; i < OBJ_REF_DATASET_EMPTY_WRITE_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % 8 + 1);

    if ((space_id = H5Screate_simple(OBJ_REF_DATASET_EMPTY_WRITE_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, OBJ_REF_DATASET_EMPTY_WRITE_TEST_DSET_NAME, H5T_STD_REF_OBJ, space_id,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    for (i = 0, ref_array_size = 1; i < OBJ_REF_DATASET_EMPTY_WRITE_TEST_SPACE_RANK; i++)
        ref_array_size *= dims[i];

    if (NULL == (ref_array = (rv_obj_ref_t *)calloc(1, ref_array_size * sizeof(*ref_array))))
        TEST_ERROR

    for (i = 0; i < dims[0]; i++) {
        const char *URI;

        switch (rand() % 2) {
            case 0:
                if (H5Rcreate(&ref_array[i], file_id, "/", H5R_OBJECT, -1) < 0) {
                    H5_FAILED();
                    printf("    couldn't create reference\n");
                    goto error;
                }

                if (NULL == (URI = H5rest_get_object_uri(file_id)))
                    TEST_ERROR

                if (strcmp(URI, ref_array[i].ref_obj_URI)) {
                    H5_FAILED();
                    printf("    ref type had mismatched URI\n");
                    goto error;
                }

                break;

            case 1:
                break;

            default:
                TEST_ERROR
        }
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Writing to dataset with buffer of empty object references\n");
#endif

    if (H5Dwrite(dset_id, H5T_STD_REF_OBJ, H5S_ALL, H5S_ALL, H5P_DEFAULT, ref_array) < 0) {
        H5_FAILED();
        printf("    couldn't write to dataset\n");
        goto error;
    }

    if (ref_array) {
        free(ref_array);
        ref_array = NULL;
    }

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(space_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_unused_object_API_calls(void)
{
    hid_t file_id = -1, fapl_id = -1;

    TESTING("unused object API calls")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Testing that all of the unused object API calls don't cause application issues\n");
#endif

    H5E_BEGIN_TRY
    {
        const char *comment = "comment";

        if (H5Oset_comment(file_id, comment) >= 0)
            TEST_ERROR
        if (H5Oset_comment_by_name(file_id, "/", comment, H5P_DEFAULT) >= 0)
            TEST_ERROR
    }
    H5E_END_TRY;

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

/*****************************************************
 *                                                   *
 *                Miscellaneous tests                *
 *                                                   *
 *****************************************************/

static int
test_open_link_without_leading_slash(void)
{
    hsize_t dims[OPEN_LINK_WITHOUT_SLASH_DSET_SPACE_RANK];
    size_t  i;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   group_id        = -1;
    hid_t   dset_id         = -1;
    hid_t   dset_dtype      = -1;
    hid_t   space_id        = -1;

    TESTING("opening a link without a leading slash")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, MISCELLANEOUS_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    for (i = 0; i < OPEN_LINK_WITHOUT_SLASH_DSET_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((space_id = H5Screate_simple(OPEN_LINK_WITHOUT_SLASH_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, OPEN_LINK_WITHOUT_SLASH_DSET_NAME, dset_dtype, space_id,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((group_id = H5Gopen2(file_id, "/", H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open root group\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Testing that an object can be opened by a relative path\n");
#endif

    if ((dset_id = H5Dopen2(group_id, MISCELLANEOUS_TEST_GROUP_NAME "/" OPEN_LINK_WITHOUT_SLASH_DSET_NAME,
                            H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset\n");
        goto error;
    }

    if ((space_id = H5Dget_space(dset_id)) < 0)
        TEST_ERROR

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(space_id);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_object_creation_by_absolute_path(void)
{
    hsize_t dims[OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DSET_SPACE_RANK];
    htri_t  link_exists;
    size_t  i;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1, sub_group_id = -1;
    hid_t   dset_id    = -1;
    hid_t   fspace_id  = -1;
    hid_t   dtype_id   = -1;
    hid_t   dset_dtype = -1;

    TESTING("object creation by absolute path")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, MISCELLANEOUS_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    /* Start by creating a group to hold all the objects for this test */
    if ((group_id = H5Gcreate2(container_group, OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_CONTAINER_GROUP_NAME,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container group\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating a variety of objects using absolute pathnames\n");
#endif

    /* Next try to create a group under the container group by using an absolute pathname */
    if ((sub_group_id = H5Gcreate2(file_id,
                                   "/" MISCELLANEOUS_TEST_GROUP_NAME
                                   "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_CONTAINER_GROUP_NAME
                                   "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_SUBGROUP_NAME,
                                   H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create subgroup by absolute pathname\n");
        goto error;
    }

    /* Next try to create a dataset nested at the end of this group chain by using an absolute pathname */
    for (i = 0; i < OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DSET_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((fspace_id = H5Screate_simple(OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(file_id,
                              "/" MISCELLANEOUS_TEST_GROUP_NAME
                              "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_CONTAINER_GROUP_NAME
                              "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_SUBGROUP_NAME
                              "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DSET_NAME,
                              dset_dtype, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    /* Next try to create a committed datatype in the same fashion as the preceding dataset */
    if ((dtype_id = generate_random_datatype(H5T_NO_CLASS)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if (H5Tcommit2(file_id,
                   "/" MISCELLANEOUS_TEST_GROUP_NAME
                   "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_CONTAINER_GROUP_NAME
                   "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_SUBGROUP_NAME
                   "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DTYPE_NAME,
                   dtype_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't commit datatype\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that all of the objects exist in the correct place\n");
#endif

    /* Finally try to verify that all of the previously-created objects exist in the correct location */
    if ((link_exists = H5Lexists(file_id,
                                 "/" MISCELLANEOUS_TEST_GROUP_NAME
                                 "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_CONTAINER_GROUP_NAME,
                                 H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    container group didn't exist at the correct location\n");
        goto error;
    }

    if ((link_exists = H5Lexists(file_id,
                                 "/" MISCELLANEOUS_TEST_GROUP_NAME
                                 "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_CONTAINER_GROUP_NAME
                                 "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_SUBGROUP_NAME,
                                 H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    subgroup didn't exist at the correct location\n");
        goto error;
    }

    if ((link_exists = H5Lexists(file_id,
                                 "/" MISCELLANEOUS_TEST_GROUP_NAME
                                 "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_CONTAINER_GROUP_NAME
                                 "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_SUBGROUP_NAME
                                 "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DSET_NAME,
                                 H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    dataset didn't exist at the correct location\n");
        goto error;
    }

    if ((link_exists = H5Lexists(file_id,
                                 "/" MISCELLANEOUS_TEST_GROUP_NAME
                                 "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_CONTAINER_GROUP_NAME
                                 "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_SUBGROUP_NAME
                                 "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DTYPE_NAME,
                                 H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    datatype didn't exist at the correct location\n");
        goto error;
    }

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Tclose(dtype_id) < 0)
        TEST_ERROR
    if (H5Gclose(sub_group_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(fspace_id);
        H5Tclose(dset_dtype);
        H5Dclose(dset_id);
        H5Tclose(dtype_id);
        H5Gclose(sub_group_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_absolute_vs_relative_path(void)
{
    hsize_t dims[ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET_SPACE_RANK];
    htri_t  link_exists;
    size_t  i;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id1 = -1, dset_id2 = -1, dset_id3 = -1, dset_id4 = -1, dset_id5 = -1, dset_id6 = -1;
    hid_t   dset_dtype1 = -1, dset_dtype2 = -1, dset_dtype3 = -1, dset_dtype4 = -1, dset_dtype5 = -1,
          dset_dtype6 = -1;
    hid_t fspace_id   = -1;

    TESTING("absolute vs. relative pathnames")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, MISCELLANEOUS_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    /* Start by creating a group to be used during some of the dataset creation operations */
    if ((group_id = H5Gcreate2(container_group, ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container group\n");
        goto error;
    }

    for (i = 0; i < ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((fspace_id = H5Screate_simple(ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_dtype1 = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR
    if ((dset_dtype2 = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR
    if ((dset_dtype3 = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR
    if ((dset_dtype4 = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR
    if ((dset_dtype5 = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR
    if ((dset_dtype6 = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating a variety of datasets using different forms of absolute and relative pathnames\n");
#endif

    /* Create a dataset by absolute path in the form "/group/dataset" starting from the root group */
    if ((dset_id1 = H5Dcreate2(file_id,
                               "/" MISCELLANEOUS_TEST_GROUP_NAME
                               "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME
                               "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET1_NAME,
                               dset_dtype1, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset by absolute path from root\n");
        goto error;
    }

    /* Create a dataset by relative path in the form "group/dataset" starting from the container group */
    if ((dset_id2 = H5Dcreate2(container_group,
                               ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME
                               "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET2_NAME,
                               dset_dtype2, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset by relative path from root\n");
        goto error;
    }

    /* Create a dataset by relative path in the form "./group/dataset" starting from the root group */
    if ((dset_id3 = H5Dcreate2(file_id,
                               "./" MISCELLANEOUS_TEST_GROUP_NAME
                               "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME
                               "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET3_NAME,
                               dset_dtype3, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset by relative path from root with leading '.'\n");
        goto error;
    }

    /* Create a dataset by absolute path in the form "/group/dataset" starting from the container group */
    if ((dset_id4 = H5Dcreate2(container_group,
                               "/" MISCELLANEOUS_TEST_GROUP_NAME
                               "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME
                               "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET4_NAME,
                               dset_dtype4, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset by absolute path from container group\n");
        goto error;
    }

    /* Create a dataset by relative path in the form "dataset" starting from the container group */
    if ((dset_id5 = H5Dcreate2(group_id, ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET5_NAME, dset_dtype5, fspace_id,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset by relative path from container group\n");
        goto error;
    }

    /* Create a dataset by relative path in the form "./dataset" starting from the container group */
    if ((dset_id6 = H5Dcreate2(group_id, "./" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET6_NAME, dset_dtype6,
                               fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset by relative path from container group with leading '.'\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Verifying that all of the datasets are in the correct place\n");
#endif

    /* Verify that all of the previously-created datasets exist in the correct locations */
    if ((link_exists = H5Lexists(file_id,
                                 "/" MISCELLANEOUS_TEST_GROUP_NAME
                                 "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME
                                 "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET1_NAME,
                                 H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    didn't exist at the correct location\n");
        goto error;
    }

    if ((link_exists = H5Lexists(file_id,
                                 "/" MISCELLANEOUS_TEST_GROUP_NAME
                                 "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME
                                 "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET2_NAME,
                                 H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    didn't exist at the correct location\n");
        goto error;
    }

    if ((link_exists = H5Lexists(file_id,
                                 "/" MISCELLANEOUS_TEST_GROUP_NAME
                                 "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME
                                 "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET3_NAME,
                                 H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    didn't exist at the correct location\n");
        goto error;
    }

    if ((link_exists = H5Lexists(file_id,
                                 "/" MISCELLANEOUS_TEST_GROUP_NAME
                                 "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME
                                 "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET4_NAME,
                                 H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    didn't exist at the correct location\n");
        goto error;
    }

    if ((link_exists = H5Lexists(file_id,
                                 "/" MISCELLANEOUS_TEST_GROUP_NAME
                                 "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME
                                 "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET5_NAME,
                                 H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    didn't exist at the correct location\n");
        goto error;
    }

    if ((link_exists = H5Lexists(file_id,
                                 "/" MISCELLANEOUS_TEST_GROUP_NAME
                                 "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME
                                 "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET6_NAME,
                                 H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    didn't exist at the correct location\n");
        goto error;
    }

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype1) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype2) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype3) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype4) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype5) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype6) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id1) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id2) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id3) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id4) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id5) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id6) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(fspace_id);
        H5Tclose(dset_dtype1);
        H5Tclose(dset_dtype2);
        H5Tclose(dset_dtype3);
        H5Tclose(dset_dtype4);
        H5Tclose(dset_dtype5);
        H5Tclose(dset_dtype6);
        H5Dclose(dset_id1);
        H5Dclose(dset_id2);
        H5Dclose(dset_id3);
        H5Dclose(dset_id4);
        H5Dclose(dset_id5);
        H5Dclose(dset_id6);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

/* Simple test to ensure that calling H5rest_init() and
 * H5rest_term() twice doesn't do anything bad
 */
static int
test_double_init_free(void)
{
    hid_t fapl_id = -1;

    TESTING("double init/free correctness")

    if (H5rest_init() < 0)
        TEST_ERROR
    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Pclose(fapl_id);
        H5rest_term();
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

/* Test to ensure that URL-encoding of attribute and link names works
 * correctly
 */
static int
test_url_encoding(void)
{
    hsize_t dims[URL_ENCODING_TEST_SPACE_RANK];
    size_t  i;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id    = -1;
    hid_t   attr_id    = -1;
    hid_t   attr_dtype = -1;
    hid_t   dset_dtype = -1;
    hid_t   space_id   = -1;

    TESTING("correct URL-encoding behavior")

    if (H5rest_init() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, MISCELLANEOUS_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating group with variety of symbols in name\n");
#endif

    if ((group_id = H5Gcreate2(container_group, URL_ENCODING_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT,
                               H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group\n");
        goto error;
    }

    for (i = 0; i < URL_ENCODING_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t)(rand() % 64 + 1);

    if ((space_id = H5Screate_simple(URL_ENCODING_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR
    if ((dset_dtype = generate_random_datatype(H5T_NO_CLASS)) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating dataset with variety of symbols in name\n");
#endif

    if ((dset_id = H5Dcreate2(group_id, URL_ENCODING_TEST_DSET_NAME, dset_dtype, space_id, H5P_DEFAULT,
                              H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

#ifdef RV_CONNECTOR_DEBUG
    puts("Creating attribute with variety of symbols in name\n");
#endif

    if ((attr_id = H5Acreate2(dset_id, URL_ENCODING_TEST_ATTR_NAME, attr_dtype, space_id, H5P_DEFAULT,
                              H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR

#ifdef RV_CONNECTOR_DEBUG
    puts("Attempting to re-open these objects\n");
#endif

    if ((group_id = H5Gopen2(container_group, URL_ENCODING_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open group\n");
        goto error;
    }

    if ((dset_id = H5Dopen2(group_id, URL_ENCODING_TEST_DSET_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset\n");
        goto error;
    }

    if ((attr_id = H5Aopen(dset_id, URL_ENCODING_TEST_ATTR_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute\n");
        goto error;
    }

    /* XXX: Test all of the other places where URL-encoding is used */

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Tclose(attr_dtype) < 0)
        TEST_ERROR
    if (H5Tclose(dset_dtype) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Sclose(space_id);
        H5Tclose(attr_dtype);
        H5Tclose(dset_dtype);
        H5Aclose(attr_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static int
test_symbols_in_compound_field_name(void)
{
    hsize_t dims[COMPOUND_WITH_SYMBOLS_IN_MEMBER_NAMES_TEST_DSET_RANK];
    size_t  i;
    size_t  total_type_size;
    size_t  next_offset;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   compound_type = -1;
    hid_t   dset_id       = -1;
    hid_t   fspace_id     = -1;
    hid_t   type_pool[COMPOUND_WITH_SYMBOLS_IN_MEMBER_NAMES_TEST_NUM_SUBTYPES];
    char    member_names[COMPOUND_WITH_SYMBOLS_IN_MEMBER_NAMES_TEST_NUM_SUBTYPES][256];

    TESTING("usage of '{', '}' and '\\\"' symbols in compound type\'s field name")

    if (H5rest_init() < 0)
        TEST_ERROR

    for (i = 0; i < COMPOUND_WITH_SYMBOLS_IN_MEMBER_NAMES_TEST_NUM_SUBTYPES; i++)
        type_pool[i] = -1;

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, MISCELLANEOUS_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, COMPOUND_WITH_SYMBOLS_IN_MEMBER_NAMES_TEST_SUBGROUP_NAME,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    for (i = 0, total_type_size = 0; i < COMPOUND_WITH_SYMBOLS_IN_MEMBER_NAMES_TEST_NUM_SUBTYPES; i++) {
        type_pool[i] = generate_random_datatype(H5T_NO_CLASS);
        total_type_size += H5Tget_size(type_pool[i]);
    }

    snprintf(member_names[0], 256, "{{{ member0");
    snprintf(member_names[1], 256, "member1 }}}");
    snprintf(member_names[2], 256, "{{{ member2 }}");
    snprintf(member_names[3], 256, "{{ member3 }}}");
    snprintf(member_names[4], 256, "\\\"member4");
    snprintf(member_names[5], 256, "member5\\\"");
    snprintf(member_names[6], 256, "mem\\\"ber6");
    snprintf(member_names[7], 256, "{{ member7\\\" }");
    snprintf(member_names[8], 256, "{{ member8\\\\");

    if ((compound_type = H5Tcreate(H5T_COMPOUND, total_type_size)) < 0) {
        H5_FAILED();
        printf("    couldn't create compound datatype\n");
        goto error;
    }

    for (i = 0, next_offset = 0; i < COMPOUND_WITH_SYMBOLS_IN_MEMBER_NAMES_TEST_NUM_SUBTYPES; i++) {
        if (H5Tinsert(compound_type, member_names[i], next_offset, type_pool[i]) < 0) {
            H5_FAILED();
            printf("    couldn't insert compound member %zu\n", i);
            goto error;
        }

        next_offset += H5Tget_size(type_pool[i]);
    }

    if (H5Tpack(compound_type) < 0)
        TEST_ERROR

    for (i = 0; i < COMPOUND_WITH_SYMBOLS_IN_MEMBER_NAMES_TEST_DSET_RANK; i++)
        dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

    if ((fspace_id = H5Screate_simple(COMPOUND_WITH_SYMBOLS_IN_MEMBER_NAMES_TEST_DSET_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, COMPOUND_WITH_SYMBOLS_IN_MEMBER_NAMES_TEST_DSET_NAME, compound_type,
                              fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if (H5Dclose(dset_id) < 0)
        TEST_ERROR

    if ((dset_id = H5Dopen2(group_id, COMPOUND_WITH_SYMBOLS_IN_MEMBER_NAMES_TEST_DSET_NAME, H5P_DEFAULT)) <
        0) {
        H5_FAILED();
        printf("    failed to open dataset\n");
        goto error;
    }

    for (i = 0; i < COMPOUND_WITH_SYMBOLS_IN_MEMBER_NAMES_TEST_NUM_SUBTYPES; i++)
        if (type_pool[i] >= 0 && H5Tclose(type_pool[i]) < 0)
            TEST_ERROR

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Tclose(compound_type) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (H5rest_term() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY
    {
        for (i = 0; i < COMPOUND_WITH_SYMBOLS_IN_MEMBER_NAMES_TEST_NUM_SUBTYPES; i++)
            H5Tclose(type_pool[i]);
        H5Sclose(fspace_id);
        H5Tclose(compound_type);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5rest_term();
    }
    H5E_END_TRY;

    return 1;
}

static herr_t
attr_iter_callback1(hid_t location_id, const char *attr_name, const H5A_info_t *ainfo, void *op_data)
{
    if (!strcmp(attr_name, ATTRIBUTE_ITERATE_TEST_ATTR_NAME)) {
        if (ainfo->corder != 0) {
            H5_FAILED();
            printf("    attribute corder didn't match\n");
            goto error;
        }

        if (ainfo->corder_valid != 0) {
            H5_FAILED();
            printf("    attribute corder_valid didn't match\n");
            goto error;
        }

        if (ainfo->cset != 0) {
            H5_FAILED();
            printf("    attribute cset didn't match\n");
            goto error;
        }

        if (ainfo->data_size != 0) {
            H5_FAILED();
            printf("    attribute data_size didn't match\n");
            goto error;
        }
    }
    else if (!strcmp(attr_name, ATTRIBUTE_ITERATE_TEST_ATTR_NAME2)) {
        if (ainfo->corder != 0) {
            H5_FAILED();
            printf("    attribute corder didn't match\n");
            goto error;
        }

        if (ainfo->corder_valid != 0) {
            H5_FAILED();
            printf("    attribute corder_valid didn't match\n");
            goto error;
        }

        if (ainfo->cset != 0) {
            H5_FAILED();
            printf("    attribute cset didn't match\n");
            goto error;
        }

        if (ainfo->data_size != 0) {
            H5_FAILED();
            printf("    attribute data_size didn't match\n");
            goto error;
        }
    }
    else if (!strcmp(attr_name, ATTRIBUTE_ITERATE_TEST_ATTR_NAME3)) {
        if (ainfo->corder != 0) {
            H5_FAILED();
            printf("    attribute corder didn't match\n");
            goto error;
        }

        if (ainfo->corder_valid != 0) {
            H5_FAILED();
            printf("    attribute corder_valid didn't match\n");
            goto error;
        }

        if (ainfo->cset != 0) {
            H5_FAILED();
            printf("    attribute cset didn't match\n");
            goto error;
        }

        if (ainfo->data_size != 0) {
            H5_FAILED();
            printf("    attribute data_size didn't match\n");
            goto error;
        }
    }
    else if (!strcmp(attr_name, ATTRIBUTE_ITERATE_TEST_ATTR_NAME4)) {
        if (ainfo->corder != 0) {
            H5_FAILED();
            printf("    attribute corder didn't match\n");
            goto error;
        }

        if (ainfo->corder_valid != 0) {
            H5_FAILED();
            printf("    attribute corder_valid didn't match\n");
            goto error;
        }

        if (ainfo->cset != 0) {
            H5_FAILED();
            printf("    attribute cset didn't match\n");
            goto error;
        }

        if (ainfo->data_size != 0) {
            H5_FAILED();
            printf("    attribute data_size didn't match\n");
            goto error;
        }
    }
    else {
        H5_FAILED();
        printf("    attribute name didn't match known names\n");
        goto error;
    }

    return 0;

error:
    return -1;
}

static herr_t
attr_iter_callback2(hid_t location_id, const char *attr_name, const H5A_info_t *ainfo, void *op_data)
{
    return 0;
}

/*
 * Link iteration callback to simply iterate through all of the links in a
 * group and check to make sure their names and link classes match what is
 * expected.
 */
static herr_t
link_iter_callback1(hid_t group_id, const char *name, const H5L_info2_t *info, void *op_data)
{
    if (!strcmp(name, LINK_ITER_TEST_HARD_LINK_NAME)) {
        if (H5L_TYPE_HARD != info->type) {
            H5_FAILED();
            printf("    link type did not match\n");
            goto error;
        }
    }
    else if (!strcmp(name, LINK_ITER_TEST_SOFT_LINK_NAME)) {
        if (H5L_TYPE_SOFT != info->type) {
            H5_FAILED();
            printf("    link type did not match\n");
            goto error;
        }
    }
    else if (!strcmp(name, LINK_ITER_TEST_EXT_LINK_NAME)) {
        if (H5L_TYPE_EXTERNAL != info->type) {
            H5_FAILED();
            printf("    link type did not match\n");
            goto error;
        }
    }
    else {
        H5_FAILED();
        printf("    link name didn't match known names\n");
        goto error;
    }

    return 0;

error:
    return -1;
}

/*
 * Link iteration callback to test that the index-saving behavior of H5Literate
 * works correctly.
 */
static herr_t
link_iter_callback2(hid_t group_id, const char *name, const H5L_info2_t *info, void *op_data)
{
    int *broken = (int *)op_data;

    if (broken && !*broken && !strcmp(name, LINK_ITER_TEST_EXT_LINK_NAME)) {
        return (*broken = 1);
    }

    if (!strcmp(name, LINK_ITER_TEST_HARD_LINK_NAME)) {
        if (H5L_TYPE_HARD != info->type) {
            H5_FAILED();
            printf("    link type did not match\n");
            goto error;
        }
    }
    else if (!strcmp(name, LINK_ITER_TEST_SOFT_LINK_NAME)) {
        if (H5L_TYPE_SOFT != info->type) {
            H5_FAILED();
            printf("    link type did not match\n");
            goto error;
        }
    }
    else if (!strcmp(name, LINK_ITER_TEST_EXT_LINK_NAME)) {
        if (H5L_TYPE_EXTERNAL != info->type) {
            H5_FAILED();
            printf("    link type did not match\n");
            goto error;
        }
    }
    else {
        H5_FAILED();
        printf("    link name didn't match known names\n");
        goto error;
    }

    return 0;

error:
    return -1;
}

static herr_t
link_iter_callback3(hid_t group_id, const char *name, const H5L_info2_t *info, void *op_data)
{
    return 0;
}

/*
 * Link visit callback to simply iterate recursively through all of the links in a
 * group and check to make sure their names and link classes match what is expected
 * expected.
 */
static herr_t
link_visit_callback1(hid_t group_id, const char *name, const H5L_info2_t *info, void *op_data)
{
    if (!strcmp(name, LINK_VISIT_TEST_NO_CYCLE_SUBGROUP_NAME2 "/" LINK_VISIT_TEST_NO_CYCLE_DSET_NAME)) {
        if (H5L_TYPE_HARD != info->type) {
            H5_FAILED();
            printf("    link type did not match\n");
            goto error;
        }
    }
    else if (!strcmp(name, LINK_VISIT_TEST_NO_CYCLE_SUBGROUP_NAME2 "/" LINK_VISIT_TEST_NO_CYCLE_LINK_NAME1)) {
        if (H5L_TYPE_HARD != info->type) {
            H5_FAILED();
            printf("    link type did not match\n");
            goto error;
        }
    }
    else if (!strcmp(name, LINK_VISIT_TEST_NO_CYCLE_SUBGROUP_NAME2 "/" LINK_VISIT_TEST_NO_CYCLE_LINK_NAME2)) {
        if (H5L_TYPE_SOFT != info->type) {
            H5_FAILED();
            printf("    link type did not match\n");
            goto error;
        }
    }
    else if (!strcmp(name, LINK_VISIT_TEST_NO_CYCLE_SUBGROUP_NAME3 "/" LINK_VISIT_TEST_NO_CYCLE_DSET_NAME)) {
        if (H5L_TYPE_HARD != info->type) {
            H5_FAILED();
            printf("    link type did not match\n");
            goto error;
        }
    }
    else if (!strcmp(name, LINK_VISIT_TEST_NO_CYCLE_SUBGROUP_NAME3 "/" LINK_VISIT_TEST_NO_CYCLE_LINK_NAME3)) {
        if (H5L_TYPE_EXTERNAL != info->type) {
            H5_FAILED();
            printf("    link type did not match\n");
            goto error;
        }
    }
    else if (!strcmp(name, LINK_VISIT_TEST_NO_CYCLE_SUBGROUP_NAME3 "/" LINK_VISIT_TEST_NO_CYCLE_LINK_NAME4)) {
        if (H5L_TYPE_HARD != info->type) {
            H5_FAILED();
            printf("    link type did not match\n");
            goto error;
        }
    }
    else if (!strcmp(name, LINK_VISIT_TEST_NO_CYCLE_SUBGROUP_NAME2)) {
        if (H5L_TYPE_HARD != info->type) {
            H5_FAILED();
            printf("    link type did not match\n");
            goto error;
        }
    }
    else if (!strcmp(name, LINK_VISIT_TEST_NO_CYCLE_SUBGROUP_NAME3)) {
        if (H5L_TYPE_HARD != info->type) {
            H5_FAILED();
            printf("    link type did not match\n");
            goto error;
        }
    }
    else {
        H5_FAILED();
        printf("    link name didn't match known names\n");
        goto error;
    }

    return 0;

error:
    return -1;
}

static herr_t
link_visit_callback2(hid_t group_id, const char *name, const H5L_info2_t *info, void *op_data)
{
    if (!strcmp(name, LINK_VISIT_TEST_CYCLE_SUBGROUP_NAME2 "/" LINK_VISIT_TEST_CYCLE_LINK_NAME1)) {
        if (H5L_TYPE_HARD != info->type) {
            H5_FAILED();
            printf("    link type did not match\n");
            goto error;
        }
    }
    else if (!strcmp(name, LINK_VISIT_TEST_CYCLE_SUBGROUP_NAME2 "/" LINK_VISIT_TEST_CYCLE_LINK_NAME2)) {
        if (H5L_TYPE_SOFT != info->type) {
            H5_FAILED();
            printf("    link type did not match\n");
            goto error;
        }
    }
    else if (!strcmp(name, LINK_VISIT_TEST_CYCLE_SUBGROUP_NAME3 "/" LINK_VISIT_TEST_CYCLE_LINK_NAME3)) {
        if (H5L_TYPE_EXTERNAL != info->type) {
            H5_FAILED();
            printf("    link type did not match\n");
            goto error;
        }
    }
    else if (!strcmp(name, LINK_VISIT_TEST_CYCLE_SUBGROUP_NAME3 "/" LINK_VISIT_TEST_CYCLE_LINK_NAME4)) {
        if (H5L_TYPE_HARD != info->type) {
            H5_FAILED();
            printf("    link type did not match\n");
            goto error;
        }
    }
    else if (!strcmp(name, LINK_VISIT_TEST_CYCLE_SUBGROUP_NAME2)) {
        if (H5L_TYPE_HARD != info->type) {
            H5_FAILED();
            printf("    link type did not match\n");
            goto error;
        }
    }
    else if (!strcmp(name, LINK_VISIT_TEST_CYCLE_SUBGROUP_NAME3)) {
        if (H5L_TYPE_HARD != info->type) {
            H5_FAILED();
            printf("    link type did not match\n");
            goto error;
        }
    }
    else {
        H5_FAILED();
        printf("    link name didn't match known names\n");
        goto error;
    }

    return 0;

error:
    return -1;
}

static herr_t
link_visit_callback3(hid_t group_id, const char *name, const H5L_info2_t *info, void *op_data)
{
    return 0;
}

/*
 * H5Ovisit callback to simply iterate through all of the objects in a given
 * group.
 */
static herr_t
object_visit_callback(hid_t o_id, const char *name, const H5O_info2_t *object_info, void *op_data)
{
    return 0;
}

/* Helper function to generate a random HDF5 datatype in order to thoroughly
 * test the REST VOL connector's support for datatypes
 */
static hid_t
generate_random_datatype(H5T_class_t parent_class)
{
    static int depth      = 0;
    hsize_t   *array_dims = NULL;
    hid_t      compound_members[COMPOUND_TYPE_MAX_MEMBERS];
    hid_t      datatype = -1;

    depth++;

    switch (rand() % H5T_NCLASSES) {
case_integer:
    case H5T_INTEGER: {
        switch (rand() % 16) {
            case 0:
                if ((datatype = H5Tcopy(H5T_STD_I8BE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy predefined integer type\n");
                    goto error;
                }

                break;

            case 1:
                if ((datatype = H5Tcopy(H5T_STD_I8LE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy predefined integer type\n");
                    goto error;
                }

                break;

            case 2:
                if ((datatype = H5Tcopy(H5T_STD_I16BE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy predefined integer type\n");
                    goto error;
                }

                break;

            case 3:
                if ((datatype = H5Tcopy(H5T_STD_I16LE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy predefined integer type\n");
                    goto error;
                }

                break;

            case 4:
                if ((datatype = H5Tcopy(H5T_STD_I32BE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy predefined integer type\n");
                    goto error;
                }

                break;

            case 5:
                if ((datatype = H5Tcopy(H5T_STD_I32LE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy predefined integer type\n");
                    goto error;
                }

                break;

            case 6:
                if ((datatype = H5Tcopy(H5T_STD_I64BE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy predefined integer type\n");
                    goto error;
                }

                break;

            case 7:
                if ((datatype = H5Tcopy(H5T_STD_I64LE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy predefined integer type\n");
                    goto error;
                }

                break;

            case 8:
                if ((datatype = H5Tcopy(H5T_STD_U8BE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy predefined integer type\n");
                    goto error;
                }

                break;

            case 9:
                if ((datatype = H5Tcopy(H5T_STD_U8LE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy predefined integer type\n");
                    goto error;
                }

                break;

            case 10:
                if ((datatype = H5Tcopy(H5T_STD_U16BE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy predefined integer type\n");
                    goto error;
                }

                break;

            case 11:
                if ((datatype = H5Tcopy(H5T_STD_U16LE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy predefined integer type\n");
                    goto error;
                }

                break;

            case 12:
                if ((datatype = H5Tcopy(H5T_STD_U32BE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy predefined integer type\n");
                    goto error;
                }

                break;

            case 13:
                if ((datatype = H5Tcopy(H5T_STD_U32LE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy predefined integer type\n");
                    goto error;
                }

                break;

            case 14:
                if ((datatype = H5Tcopy(H5T_STD_U64BE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy predefined integer type\n");
                    goto error;
                }

                break;

            case 15:
                if ((datatype = H5Tcopy(H5T_STD_U64LE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy predefined integer type\n");
                    goto error;
                }

                break;

            default:
                H5_FAILED();
                printf("    invalid value for predefined integer type; should not happen\n");
                goto error;
        }

        break;
    }

case_float:
    case H5T_FLOAT: {
        switch (rand() % 4) {
            case 0:
                if ((datatype = H5Tcopy(H5T_IEEE_F32BE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy predefined floating-point type\n");
                    goto error;
                }

                break;

            case 1:
                if ((datatype = H5Tcopy(H5T_IEEE_F32LE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy predefined floating-point type\n");
                    goto error;
                }

                break;

            case 2:
                if ((datatype = H5Tcopy(H5T_IEEE_F64BE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy predefined floating-point type\n");
                    goto error;
                }

                break;

            case 3:
                if ((datatype = H5Tcopy(H5T_IEEE_F64LE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy predefined floating-point type\n");
                    goto error;
                }

                break;

            default:
                H5_FAILED();
                printf("    invalid value for floating point type; should not happen\n");
                goto error;
        }

        break;
    }

case_time:
    case H5T_TIME: {
        /* Time datatype is unsupported, try again */
        switch (rand() % H5T_NCLASSES) {
            case H5T_INTEGER:
                goto case_integer;
            case H5T_FLOAT:
                goto case_float;
            case H5T_TIME:
                goto case_time;
            case H5T_STRING:
                goto case_string;
            case H5T_BITFIELD:
                goto case_bitfield;
            case H5T_OPAQUE:
                goto case_opaque;
            case H5T_COMPOUND:
                goto case_compound;
            case H5T_REFERENCE:
                goto case_reference;
            case H5T_ENUM:
                goto case_enum;
            case H5T_VLEN:
                goto case_vlen;
            case H5T_ARRAY:
                goto case_array;
            default:
                H5_FAILED();
                printf("    invalid value for goto\n");
                break;
        }

        break;
    }

case_string:
    case H5T_STRING: {
        /* Note: currently only H5T_CSET_ASCII is supported for the character set and
         * only H5T_STR_NULLTERM is supported for string padding for variable-length
         * strings and only H5T_STR_NULLPAD is supported for string padding for
         * fixed-length strings, but these may change in the future.
         */
#if 0
            if (0 == (rand() % 2)) {
#endif
        if ((datatype = H5Tcreate(H5T_STRING, (size_t)(rand() % STRING_TYPE_MAX_SIZE) + 1)) < 0) {
            H5_FAILED();
            printf("    couldn't create fixed-length string datatype\n");
            goto error;
        }

        if (H5Tset_strpad(datatype, H5T_STR_NULLPAD) < 0) {
            H5_FAILED();
            printf("    couldn't set H5T_STR_NULLPAD for fixed-length string type\n");
            goto error;
        }
#if 0
            }
#endif
#if 0
            else {
                if ((datatype = H5Tcreate(H5T_STRING, H5T_VARIABLE)) < 0) {
                    H5_FAILED();
                    printf("    couldn't create variable-length string datatype\n");
                    goto error;
                }

                if (H5Tset_strpad(datatype, H5T_STR_NULLTERM) < 0) {
                    H5_FAILED();
                    printf("    couldn't set H5T_STR_NULLTERM for variable-length string type\n");
                    goto error;
                }
            }
#endif

        if (H5Tset_cset(datatype, H5T_CSET_ASCII) < 0) {
            H5_FAILED();
            printf("    couldn't set string datatype character set\n");
            goto error;
        }

        break;
    }

case_bitfield:
    case H5T_BITFIELD: {
        /* Bitfield datatype is unsupported, try again */
        switch (rand() % H5T_NCLASSES) {
            case H5T_INTEGER:
                goto case_integer;
            case H5T_FLOAT:
                goto case_float;
            case H5T_TIME:
                goto case_time;
            case H5T_STRING:
                goto case_string;
            case H5T_BITFIELD:
                goto case_bitfield;
            case H5T_OPAQUE:
                goto case_opaque;
            case H5T_COMPOUND:
                goto case_compound;
            case H5T_REFERENCE:
                goto case_reference;
            case H5T_ENUM:
                goto case_enum;
            case H5T_VLEN:
                goto case_vlen;
            case H5T_ARRAY:
                goto case_array;
            default:
                H5_FAILED();
                printf("    invalid value for goto\n");
                break;
        }

        break;
    }

case_opaque:
    case H5T_OPAQUE: {
        /* Opaque datatype is unsupported, try again */
        switch (rand() % H5T_NCLASSES) {
            case H5T_INTEGER:
                goto case_integer;
            case H5T_FLOAT:
                goto case_float;
            case H5T_TIME:
                goto case_time;
            case H5T_STRING:
                goto case_string;
            case H5T_BITFIELD:
                goto case_bitfield;
            case H5T_OPAQUE:
                goto case_opaque;
            case H5T_COMPOUND:
                goto case_compound;
            case H5T_REFERENCE:
                goto case_reference;
            case H5T_ENUM:
                goto case_enum;
            case H5T_VLEN:
                goto case_vlen;
            case H5T_ARRAY:
                goto case_array;
            default:
                H5_FAILED();
                printf("    invalid value for goto\n");
                break;
        }

        break;
    }

case_compound:
    case H5T_COMPOUND: {
        size_t num_members;
        size_t next_offset   = 0;
        size_t compound_size = 0;
        size_t i;

        /* Currently only allows arrays of integer, float or string. Pick another type if we
         * are creating an array of something other than these. Also don't allow recursion
         * to go too deep. Pick another type that doesn't recursively call this function. */
        if (H5T_ARRAY == parent_class || depth > RECURSION_MAX_DEPTH) {
            switch (rand() % H5T_NCLASSES) {
                case H5T_INTEGER:
                    goto case_integer;
                case H5T_FLOAT:
                    goto case_float;
                case H5T_TIME:
                    goto case_time;
                case H5T_STRING:
                    goto case_string;
                case H5T_BITFIELD:
                    goto case_bitfield;
                case H5T_OPAQUE:
                    goto case_opaque;
                case H5T_COMPOUND:
                    goto case_compound;
                case H5T_REFERENCE:
                    goto case_reference;
                case H5T_ENUM:
                    goto case_enum;
                case H5T_VLEN:
                    goto case_vlen;
                case H5T_ARRAY:
                    goto case_array;
                default:
                    H5_FAILED();
                    printf("    invalid value for goto\n");
                    break;
            }
        }

        for (i = 0; i < COMPOUND_TYPE_MAX_MEMBERS; i++)
            compound_members[i] = -1;

        if ((datatype = H5Tcreate(H5T_COMPOUND, 1)) < 0) {
            H5_FAILED();
            printf("    couldn't create compound datatype\n");
            goto error;
        }

        num_members = (size_t)(rand() % COMPOUND_TYPE_MAX_MEMBERS + 1);

        for (i = 0; i < num_members; i++) {
            size_t member_size;
            char   member_name[256];

            snprintf(member_name, 256, "compound_member%zu", i);

            if ((compound_members[i] = generate_random_datatype(H5T_NO_CLASS)) < 0) {
                H5_FAILED();
                printf("    couldn't create compound datatype member %zu\n", i);
                goto error;
            }

            if ((member_size = H5Tget_size(compound_members[i])) < 0) {
                H5_FAILED();
                printf("    couldn't get compound member %zu size\n", i);
                goto error;
            }

            compound_size += member_size;

            if (H5Tset_size(datatype, compound_size) < 0) {
                H5_FAILED();
                printf("    couldn't set size for compound datatype\n");
                goto error;
            }

            if (H5Tinsert(datatype, member_name, next_offset, compound_members[i]) < 0) {
                H5_FAILED();
                printf("    couldn't insert compound datatype member %zu\n", i);
                goto error;
            }

            next_offset += member_size;
        }

        break;
    }

case_reference:
    case H5T_REFERENCE: {
        /* Currently only allows arrays of integer, float or string. Pick another type if we
         * are creating an array of something other than these. */
        if (H5T_ARRAY == parent_class) {
            switch (rand() % H5T_NCLASSES) {
                case H5T_INTEGER:
                    goto case_integer;
                case H5T_FLOAT:
                    goto case_float;
                case H5T_TIME:
                    goto case_time;
                case H5T_STRING:
                    goto case_string;
                case H5T_BITFIELD:
                    goto case_bitfield;
                case H5T_OPAQUE:
                    goto case_opaque;
                case H5T_COMPOUND:
                    goto case_compound;
                case H5T_REFERENCE:
                    goto case_reference;
                case H5T_ENUM:
                    goto case_enum;
                case H5T_VLEN:
                    goto case_vlen;
                case H5T_ARRAY:
                    goto case_array;
                default:
                    H5_FAILED();
                    printf("    invalid value for goto\n");
                    break;
            }
        }

        if (0 == (rand() % 2)) {
            if ((datatype = H5Tcopy(H5T_STD_REF_OBJ)) < 0) {
                H5_FAILED();
                printf("    couldn't copy object reference datatype\n");
                goto error;
            }
        }
        else {
            /* Region references are currently unsupported */
            switch (rand() % H5T_NCLASSES) {
                case H5T_INTEGER:
                    goto case_integer;
                case H5T_FLOAT:
                    goto case_float;
                case H5T_TIME:
                    goto case_time;
                case H5T_STRING:
                    goto case_string;
                case H5T_BITFIELD:
                    goto case_bitfield;
                case H5T_OPAQUE:
                    goto case_opaque;
                case H5T_COMPOUND:
                    goto case_compound;
                case H5T_REFERENCE:
                    goto case_reference;
                case H5T_ENUM:
                    goto case_enum;
                case H5T_VLEN:
                    goto case_vlen;
                case H5T_ARRAY:
                    goto case_array;
                default:
                    H5_FAILED();
                    printf("    invalid value for goto\n");
                    break;
            }

#if 0
                if ((datatype = H5Tcopy(H5T_STD_REF_DSETREG)) < 0) {
                    H5_FAILED();
                    printf("    couldn't copy region reference datatype\n");
                    goto error;
                }
#endif
        }

        break;
    }

case_enum:
    case H5T_ENUM: {
        size_t i;

        /* Currently doesn't currently support ARRAY of ENUM, so try another type
         * if this happens. */
        if (H5T_ARRAY == parent_class) {
            switch (rand() % H5T_NCLASSES) {
                case H5T_INTEGER:
                    goto case_integer;
                case H5T_FLOAT:
                    goto case_float;
                case H5T_TIME:
                    goto case_time;
                case H5T_STRING:
                    goto case_string;
                case H5T_BITFIELD:
                    goto case_bitfield;
                case H5T_OPAQUE:
                    goto case_opaque;
                case H5T_COMPOUND:
                    goto case_compound;
                case H5T_REFERENCE:
                    goto case_reference;
                case H5T_ENUM:
                    goto case_enum;
                case H5T_VLEN:
                    goto case_vlen;
                case H5T_ARRAY:
                    goto case_array;
                default:
                    H5_FAILED();
                    printf("    invalid value for goto\n");
                    break;
            }
        }

        if ((datatype = H5Tenum_create(H5T_NATIVE_INT)) < 0) {
            H5_FAILED();
            printf("    couldn't create enum datatype\n");
            goto error;
        }

        for (i = 0; i < (size_t)(rand() % ENUM_TYPE_MAX_MEMBERS + 1); i++) {
            int  value = rand();
            char name[ENUM_TYPE_MAX_MEMBER_NAME_LENGTH];

            snprintf(name, ENUM_TYPE_MAX_MEMBER_NAME_LENGTH, "enum_val%zu", i);

            if (H5Tenum_insert(datatype, name, &value) < 0) {
                H5_FAILED();
                printf("    couldn't insert member into enum datatype\n");
                goto error;
            }
        }

        break;
    }

case_vlen:
    case H5T_VLEN: {
        /* Variable-length datatypes are unsupported, try again */
        switch (rand() % H5T_NCLASSES) {
            case H5T_INTEGER:
                goto case_integer;
            case H5T_FLOAT:
                goto case_float;
            case H5T_TIME:
                goto case_time;
            case H5T_STRING:
                goto case_string;
            case H5T_BITFIELD:
                goto case_bitfield;
            case H5T_OPAQUE:
                goto case_opaque;
            case H5T_COMPOUND:
                goto case_compound;
            case H5T_REFERENCE:
                goto case_reference;
            case H5T_ENUM:
                goto case_enum;
            case H5T_VLEN:
                goto case_vlen;
            case H5T_ARRAY:
                goto case_array;
            default:
                H5_FAILED();
                printf("    invalid value for goto\n");
                break;
        }

        break;
    }

case_array:
    case H5T_ARRAY: {
        unsigned ndims;
        size_t   i;
        hid_t    base_datatype = -1;

        /* Currently doesn't currently support ARRAY of ARRAY, so try another type
         * if this happens. Also check for too much recursion. */
        if (H5T_ARRAY == parent_class || depth > RECURSION_MAX_DEPTH) {
            switch (rand() % H5T_NCLASSES) {
                case H5T_INTEGER:
                    goto case_integer;
                case H5T_FLOAT:
                    goto case_float;
                case H5T_TIME:
                    goto case_time;
                case H5T_STRING:
                    goto case_string;
                case H5T_BITFIELD:
                    goto case_bitfield;
                case H5T_OPAQUE:
                    goto case_opaque;
                case H5T_COMPOUND:
                    goto case_compound;
                case H5T_REFERENCE:
                    goto case_reference;
                case H5T_ENUM:
                    goto case_enum;
                case H5T_VLEN:
                    goto case_vlen;
                case H5T_ARRAY:
                    goto case_array;
                default:
                    H5_FAILED();
                    printf("    invalid value for goto\n");
                    break;
            }
        }

        ndims = (unsigned)(rand() % ARRAY_TYPE_MAX_DIMS + 1);

        if (NULL == (array_dims = (hsize_t *)malloc(ndims * sizeof(*array_dims))))
            TEST_ERROR

        for (i = 0; i < ndims; i++)
            array_dims[i] = (hsize_t)(rand() % MAX_DIM_SIZE + 1);

        if ((base_datatype = generate_random_datatype(H5T_ARRAY)) < 0) {
            H5_FAILED();
            printf("    couldn't create array base datatype\n");
            goto error;
        }

        if ((datatype = H5Tarray_create2(base_datatype, ndims, array_dims)) < 0) {
            H5_FAILED();
            printf("    couldn't create array datatype\n");
            goto error;
        }

        break;
    }

    default:
        H5_FAILED();
        printf("    invalid datatype class\n");
        break;
    } /* end if */

error:
    depth--;

    if (datatype < 0) {
        size_t i;

        for (i = 0; i < COMPOUND_TYPE_MAX_MEMBERS; i++) {
            if (compound_members[i] > 0 && H5Tclose(compound_members[i]) < 0) {
                H5_FAILED();
                printf("    couldn't close compound member %zu\n", i);
            }
        }
    }

    if (array_dims)
        free(array_dims);

    return datatype;
}

int
main(int argc, char **argv)
{
    size_t i;
    int    nerrors = 0;

    if (NULL == (username = getenv("HSDS_USERNAME"))) {
        printf("HSDS_USERNAME is not set! Tests cannot proceed.\n\n");
        goto error;
    }

    if (!getenv("HSDS_ENDPOINT")) {
        printf("HSDS_ENDPOINT is not set! Tests cannot proceed.\n\n");
        goto error;
    }

    snprintf(filename, FILENAME_MAX_LENGTH, "%s/%s/%s", TEST_DIR_PREFIX, username, TEST_FILE_NAME);

    printf("Test parameters:\n\n");
    printf("  - URL: %s\n", getenv("HSDS_ENDPOINT") ? getenv("HSDS_ENDPOINT") : "(null)");
    printf("  - Username: %s\n", getenv("HSDS_USERNAME") ? getenv("HSDS_USERNAME") : "(null)");
    printf("  - Password: %s\n", getenv("HSDS_PASSWORD") ? getenv("HSDS_PASSWORD") : "(null)");
    printf("  - Test File name: %s\n", filename);
    printf("\n\n");

    srand((unsigned)time(NULL));

    for (i = 0, nerrors = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int (**func)(void) = tests[i];

        while (*func) {
            nerrors += (*func++)();
        }
    }

    if (nerrors)
        goto error;

    puts("All REST VOL connector tests passed");

    return 0;

error:
    printf("*** %d TEST%s FAILED ***\n", nerrors, (!nerrors || nerrors > 1) ? "S" : "");

    return 1;
}
