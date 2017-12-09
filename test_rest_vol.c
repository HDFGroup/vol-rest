/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5. The full HDF5 copyright notice, including      *
 * terms governing use, modification, and redistribution, is contained in    *
 * the files COPYING and Copyright.html.  COPYING can be found at the root   *
 * of the source code distribution tree; Copyright.html can be found at the  *
 * root level of an installed copy of the electronic document set and is     *
 * linked from the top-level documents page.  It can also be found at        *
 * http://hdfgroup.org/HDF5/doc/Copyright.html.  If you do not have access   *
 * to either file, you may request a copy from help@hdfgroup.org.            *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * Programmer:  Jordan Henderson
 *              March 2017
 *
 * Purpose: Tests the REST VOL plugin
 */

/* XXX: Eliminate all test inter-dependencies */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hdf5.h"
#include "rest_vol_public.h"

#define ARRAY_LENGTH(array) sizeof(array) / sizeof(array[0])

/* Macros for error handling */
/* Use FUNC to safely handle variations of C99 __func__ keyword handling */
#ifdef H5_HAVE_C99_FUNC
#define FUNC __func__
#elif defined(H5_HAVE_FUNCTION)
#define FUNC __FUNCTION__
#else
#error "We need __func__ or __FUNCTION__ to test function names!"
#endif

/*
 * Print the current location on the standard output stream.
 */
#define AT()     printf ("   at %s:%d in %s()...\n",        \
        __FILE__, __LINE__, FUNC);

/*
 * The name of the test is printed by saying TESTING("something") which will
 * result in the string `Testing something' being flushed to standard output.
 * If a test passes, fails, or is skipped then the PASSED(), H5_FAILED(), or
 * SKIPPED() macro should be called.  After H5_FAILED() or SKIPPED() the caller
 * should print additional information to stdout indented by at least four
 * spaces.  If the h5_errors() is used for automatic error handling then
 * the H5_FAILED() macro is invoked automatically when an API function fails.
 */
#define TESTING(S)  {printf("Testing %-62s", S); fflush(stdout);}
#define PASSED()    {puts("PASSED"); fflush(stdout);}
#define H5_FAILED() {puts("*FAILED*"); fflush(stdout);}
#define SKIPPED()   {puts(" - SKIPPED -"); fflush(stdout);}
#define TEST_ERROR  {H5_FAILED(); AT(); goto error;}


/* The HSDS endpoint and authentication information */
#define URL getenv("HSDS_ENDPOINT")
#define USERNAME "test_user1"
#define PASSWORD "test"

#define FILENAME "/home/test_user1/new_file"

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
#define FILE_INTENT_TEST_FILENAME    "/home/test_user1/intent_test_file"

#define NONEXISTENT_FILENAME "/home/test_user1/nonexistent_file"

#define FILE_PROPERTY_LIST_TEST_FNAME1 "/home/test_user1/property_list_test_file1"
#define FILE_PROPERTY_LIST_TEST_FNAME2 "/home/test_user1/property_list_test_file2"


/*****************************************************
 *                                                   *
 *             Plugin Group test defines             *
 *                                                   *
 *****************************************************/

#define GROUP_CREATE_INVALID_LOC_ID_GNAME "/test_group"

#define GROUP_CREATE_UNDER_ROOT_GNAME "/group_under_root"

#define GROUP_CREATE_UNDER_GROUP_REL_GNAME "group_under_group2"

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
#define ATTRIBUTE_CREATE_ON_ROOT_DTYPE      H5T_STD_U8LE

#define ATTRIBUTE_CREATE_ON_DATASET_DSET_SPACE_RANK 2
#define ATTRIBUTE_CREATE_ON_DATASET_ATTR_SPACE_RANK 2
#define ATTRIBUTE_CREATE_ON_DATASET_DSET_DTYPE      H5T_NATIVE_INT
#define ATTRIBUTE_CREATE_ON_DATASET_ATTR_DTYPE      H5T_STD_U8LE
#define ATTRIBUTE_CREATE_ON_DATASET_DSET_NAME       "dataset_with_attr"
#define ATTRIBUTE_CREATE_ON_DATASET_ATTR_NAME       "attr_on_dataset"

#define ATTRIBUTE_CREATE_ON_DATATYPE_SPACE_RANK 2
#define ATTRIBUTE_CREATE_ON_DATATYPE_DTYPE_SIZE 50
#define ATTRIBUTE_CREATE_ON_DATATYPE_DTYPE_NAME "datatype_with_attr"
#define ATTRIBUTE_CREATE_ON_DATATYPE_ATTR_NAME  "attr_on_datatype"
#define ATTRIBUTE_CREATE_ON_DATATYPE_DTYPE      H5T_STD_U8LE

#define ATTRIBUTE_CREATE_WITH_SPACE_IN_NAME_SPACE_RANK 2
#define ATTRIBUTE_CREATE_WITH_SPACE_IN_NAME_DTYPE      H5T_NATIVE_INT
#define ATTRIBUTE_CREATE_WITH_SPACE_IN_NAME_ATTR_NAME  "attr with space in name"

#define ATTRIBUTE_GET_INFO_TEST_SPACE_RANK 2
#define ATTRIBUTE_GET_INFO_TEST_ATTR_DTYPE H5T_NATIVE_INT
#define ATTRIBUTE_GET_INFO_TEST_ATTR_NAME  "get_info_test_attr"

#define ATTRIBUTE_GET_NAME_TEST_ATTRIBUTE_NAME "retrieve_attr_name_test"
#define ATTRIBUTE_GET_NAME_TEST_ATTRIBUTE_TYPE H5T_NATIVE_INT
#define ATTRIBUTE_GET_NAME_TEST_SPACE_RANK     2

#define ATTRIBUTE_DELETION_TEST_SPACE_RANK 2
#define ATTRIBUTE_DELETION_TEST_ATTR_DTYPE H5T_NATIVE_INT
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
#define ATTRIBUTE_GET_NUM_ATTRS_TEST_ATTRIBUTE_TYPE H5T_NATIVE_INT
#define ATTRIBUTE_GET_NUM_ATTRS_TEST_SPACE_RANK     2

#define ATTRIBUTE_PROPERTY_LIST_TEST_SUBGROUP_NAME   "attribute_property_list_test_group"
#define ATTRIBUTE_PROPERTY_LIST_TEST_ATTRIBUTE_NAME1 "property_list_test_attribute1"
#define ATTRIBUTE_PROPERTY_LIST_TEST_ATTRIBUTE_NAME2 "property_list_test_attribute2"
#define ATTRIBUTE_PROPERTY_LIST_TEST_ATTRIBUTE_TYPE1 H5T_NATIVE_INT
#define ATTRIBUTE_PROPERTY_LIST_TEST_ATTRIBUTE_TYPE2 H5T_NATIVE_INT
#define ATTRIBUTE_PROPERTY_LIST_TEST_SPACE_RANK      2


/*****************************************************
 *                                                   *
 *            Plugin Dataset test defines            *
 *                                                   *
 *****************************************************/

#define DATASET_CREATE_UNDER_ROOT_DSET_NAME  "/dset_under_root"
#define DATASET_CREATE_UNDER_ROOT_SPACE_RANK 2
#define DATASET_CREATE_UNDER_ROOT_NX         100
#define DATASET_CREATE_UNDER_ROOT_NY         100

#define DATASET_CREATE_ANONYMOUS_DATASET_NAME "anon_dset"
#define DATASET_CREATE_ANONYMOUS_SPACE_RANK   2
#define DATASET_CREATE_ANONYMOUS_NX           100
#define DATASET_CREATE_ANONYMOUS_NY           100

#define DATASET_CREATE_UNDER_EXISTING_SPACE_RANK 2
#define DATASET_CREATE_UNDER_EXISTING_DSET_NAME  "nested_dset"
#define DATASET_CREATE_UNDER_EXISTING_NX         256
#define DATASET_CREATE_UNDER_EXISTING_NY         256

/* Defines for testing the plugin's ability to parse different types
 * of Datatypes for Dataset creation
 */
#define DATASET_PREDEFINED_TYPE_TEST_SHAPE_RANK    2
#define DATASET_PREDEFINED_TYPE_TEST_BASE_NAME     "predefined_type_dset"
#define DATASET_PREDEFINED_TYPE_TEST_SUBGROUP_NAME "predefined_type_dataset_test"

#define DATASET_STRING_TYPE_TEST_STRING_LENGTH  40
#define DATASET_STRING_TYPE_TEST_SHAPE_RANK     2
#define DATASET_STRING_TYPE_TEST_DSET_NAME1     "fixed_length_string_dset"
#define DATASET_STRING_TYPE_TEST_DSET_NAME2     "variable_length_string_dset"
#define DATASET_STRING_TYPE_TEST_SUBGROUP_NAME  "string_type_dataset_test"

#define DATASET_ENUM_TYPE_TEST_VAL_BASE_NAME "INDEX"
#define DATASET_ENUM_TYPE_TEST_SUBGROUP_NAME "enum_type_dataset_test"
#define DATASET_ENUM_TYPE_TEST_SHAPE_RANK    2
#define DATASET_ENUM_TYPE_TEST_DSET_NAME1    "enum_native_dset"
#define DATASET_ENUM_TYPE_TEST_DSET_NAME2    "enum_non_native_dset"

#define DATASET_ARRAY_TYPE_TEST_NON_PREDEFINED_SIZE 20
#define DATASET_ARRAY_TYPE_TEST_SHAPE_RANK          2
#define DATASET_ARRAY_TYPE_TEST_SUBGROUP_NAME       "array_type_dataset_test"
#define DATASET_ARRAY_TYPE_TEST_DSET_NAME1          "array_type_test1"
#define DATASET_ARRAY_TYPE_TEST_DSET_NAME2          "array_type_test2"
#define DATASET_ARRAY_TYPE_TEST_DSET_NAME3          "array_type_test3"
#define DATASET_ARRAY_TYPE_TEST_DSET_NAME4          "array_type_test4"
#define DATASET_ARRAY_TYPE_TEST_RANK1               2
#define DATASET_ARRAY_TYPE_TEST_RANK2               2
#define DATASET_ARRAY_TYPE_TEST_RANK3               2
#define DATASET_ARRAY_TYPE_TEST_RANK4               2

#define DATASET_COMPOUND_TYPE_TEST_MAX_SUBTYPE_SIZE 8
#define DATASET_COMPOUND_TYPE_TEST_MAX_SUBTYPES     30
#define DATASET_COMPOUND_TYPE_TEST_MAX_PASSES       5
#define DATASET_COMPOUND_TYPE_TEST_DSET_RANK        2
#define DATASET_COMPOUND_TYPE_TEST_DSET_NAME        "compound_type_test"
#define DATASET_COMPOUND_TYPE_TEST_SUBGROUP_NAME    "compound_type_dataset_test"

/* Defines for testing the plugin's ability to parse different
 * Dataset shapes for creation
 */
#define DATASET_SHAPE_TEST_DSET_BASE_NAME "dataset_shape_test"
#define DATASET_SHAPE_TEST_SUBGROUP_NAME  "dataset_shape_test"
#define DATASET_SHAPE_TEST_NUM_ITERATIONS 5
#define DATASET_SHAPE_TEST_MAX_DIMS       32
#define DATASET_SHAPE_TEST_DATATYPE       H5T_STD_U16LE

#define DATASET_CREATION_PROPERTIES_TEST_TRACK_TIMES_YES_DSET_NAME "track_times_true_test"
#define DATASET_CREATION_PROPERTIES_TEST_TRACK_TIMES_NO_DSET_NAME  "track_times_false_test"
#define DATASET_CREATION_PROPERTIES_TEST_PHASE_CHANGE_DSET_NAME    "attr_phase_change_test"
#define DATASET_CREATION_PROPERTIES_TEST_ALLOC_TIMES_BASE_NAME     "alloc_time_test"
#define DATASET_CREATION_PROPERTIES_TEST_FILL_TIMES_BASE_NAME      "fill_times_test"
#define DATASET_CREATION_PROPERTIES_TEST_CRT_ORDER_BASE_NAME       "creation_order_test"
#define DATASET_CREATION_PROPERTIES_TEST_LAYOUTS_BASE_NAME         "layout_test"
#define DATASET_CREATION_PROPERTIES_TEST_GROUP_NAME                "creation_properties_test"
#define DATASET_CREATION_PROPERTIES_TEST_CHUNK_DIM_RANK            DATASET_CREATION_PROPERTIES_TEST_SHAPE_RANK
#define DATASET_CREATION_PROPERTIES_TEST_BASE_DATATYPE             H5T_STD_I32LE
#define DATASET_CREATION_PROPERTIES_TEST_MAX_COMPACT               12
#define DATASET_CREATION_PROPERTIES_TEST_MIN_DENSE                 8
#define DATASET_CREATION_PROPERTIES_TEST_SHAPE_RANK                3

#define DATASET_CREATE_COMBINATIONS_TEST_NUM_ITERATIONS 10

#define DATASET_SMALL_WRITE_TEST_ALL_DSET_SPACE_RANK 3
#define DATASET_SMALL_WRITE_TEST_ALL_DSET_DTYPE      H5T_NATIVE_INT
#define DATASET_SMALL_WRITE_TEST_ALL_DSET_NAME       "dataset_write_small_all"

#define DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK 3
#define DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_DTYPESIZE  sizeof(int)
#define DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_DTYPE      H5T_NATIVE_INT
#define DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_NAME       "dataset_write_small_hyperslab"
#define DATASET_SMALL_WRITE_TEST_HYPERSLAB_DIM_SIZE        10

#define DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_SPACE_RANK 3
#define DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_DTYPESIZE  sizeof(int)
#define DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_DTYPE      H5T_NATIVE_INT
#define DATASET_SMALL_WRITE_TEST_POINT_SELECTION_NUM_POINTS      DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DIM_SIZE
#define DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_NAME       "dataset_write_small_point_selection"
#define DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DIM_SIZE        10

#define DATASET_SMALL_READ_TEST_ALL_DSET_SPACE_RANK 3
#define DATASET_SMALL_READ_TEST_ALL_DSET_DTYPESIZE  sizeof(int)
#define DATASET_SMALL_READ_TEST_ALL_DSET_DTYPE      H5T_NATIVE_INT
#define DATASET_SMALL_READ_TEST_ALL_DSET_NAME       "dataset_read_small_all"

#define DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_SPACE_RANK 3
#define DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_DTYPESIZE  sizeof(int)
#define DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_DTYPE      H5T_NATIVE_INT
#define DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_NAME       "dataset_read_small_hyperslab"
#define DATASET_SMALL_READ_TEST_HYPERSLAB_DIM_SIZE        10

#define DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_SPACE_RANK 3
#define DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_DTYPESIZE  sizeof(int)
#define DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_DTYPE      H5T_NATIVE_INT
#define DATASET_SMALL_READ_TEST_POINT_SELECTION_NUM_POINTS      DATASET_SMALL_READ_TEST_POINT_SELECTION_DIM_SIZE
#define DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_NAME       "dataset_read_small_point_selection"
#define DATASET_SMALL_READ_TEST_POINT_SELECTION_DIM_SIZE        10

#define DATASET_DATA_VERIFY_WRITE_TEST_DSET_SPACE_RANK 3
#define DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE  sizeof(int)
#define DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPE      H5T_NATIVE_INT
#define DATASET_DATA_VERIFY_WRITE_TEST_DSET_NAME       "dataset_data_verification"
#define DATASET_DATA_VERIFY_WRITE_TEST_DIM_SIZE        5

#define DATASET_PROPERTY_LIST_TEST_SUBGROUP_NAME "dataset_property_list_test_group"
#define DATASET_PROPERTY_LIST_TEST_SPACE_RANK    2
#define DATASET_PROPERTY_LIST_TEST_DSET_TYPE1    H5T_NATIVE_INT
#define DATASET_PROPERTY_LIST_TEST_DSET_TYPE2    H5T_NATIVE_INT
#define DATASET_PROPERTY_LIST_TEST_DSET_TYPE3    H5T_NATIVE_INT
#define DATASET_PROPERTY_LIST_TEST_DSET_TYPE4    H5T_NATIVE_INT
#define DATASET_PROPERTY_LIST_TEST_DSET_NAME1    "property_list_test_dataset1"
#define DATASET_PROPERTY_LIST_TEST_DSET_NAME2    "property_list_test_dataset2"
#define DATASET_PROPERTY_LIST_TEST_DSET_NAME3    "property_list_test_dataset3"
#define DATASET_PROPERTY_LIST_TEST_DSET_NAME4    "property_list_test_dataset4"


/*****************************************************
 *                                                   *
 *           Plugin Datatype test defines            *
 *                                                   *
 *****************************************************/

#define DATATYPE_CREATE_TEST_DATASET_DIMS  2

#define DATATYPE_CREATE_TEST_STRING_LENGTH 40
#define DATATYPE_CREATE_TEST_TYPE_NAME     "test_type"

#define DATATYPE_CREATE_ANONYMOUS_TYPE_LENGTH 25
#define DATATYPE_CREATE_ANONYMOUS_TYPE_NAME   "anon_type"

#define DATASET_CREATE_WITH_DATATYPE_TEST_DATASET_DIMS 2
#define DATASET_CREATE_WITH_DATATYPE_TEST_TYPE_NAME    "committed_type_test_dtype1"
#define DATASET_CREATE_WITH_DATATYPE_TEST_DSET_NAME    "committed_type_test_dset"

#define ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_SPACE_RANK 2
#define ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_DTYPE_SIZE 30
#define ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_DTYPE_NAME "committed_type_test_dtype2"
#define ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_ATTR_NAME  "committed_type_test_attr"
#define ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_DTYPE      H5T_STRING

#define DATATYPE_DELETE_TEST_DTYPE_NAME "delete_test_dtype"
#define DATATYPE_DELETE_TEST_DTYPE      H5T_STRING
#define DATATYPE_DELETE_TEST_SIZE       50

#define DATATYPE_PROPERTY_LIST_TEST_SUBGROUP_NAME  "datatype_property_list_test_group"
#define DATATYPE_PROPERTY_LIST_TEST_DATATYPE_NAME1 "property_list_test_datatype1"
#define DATATYPE_PROPERTY_LIST_TEST_DATATYPE_NAME2 "property_list_test_datatype2"
#define DATATYPE_PROPERTY_LIST_TEST_DATATYPE_SIZE1 80
#define DATATYPE_PROPERTY_LIST_TEST_DATATYPE_SIZE2 50
#define DATATYPE_PROPERTY_LIST_TEST_DATATYPE1      H5T_STRING
#define DATATYPE_PROPERTY_LIST_TEST_DATATYPE2      H5T_STRING

/*****************************************************
 *                                                   *
 *             Plugin Link test defines              *
 *                                                   *
 *****************************************************/

#define HARD_LINK_TEST_LINK_NAME     "test_link"
#define SOFT_LINK_TEST_LINK_NAME     "softlink"
#define SOFT_LINK_TEST_LINK_PATH     "/softlink"
#define EXTERNAL_LINK_TEST_FILE_NAME "/home/test_user1/ext_link_file"
#define EXTERNAL_LINK_TEST_LINK_NAME "ext_link"

#define H5L_SAME_LOC_TEST_DSET_SPACE_RANK 2
#define H5L_SAME_LOC_TEST_DSET_DTYPE      H5T_NATIVE_INT
#define H5L_SAME_LOC_TEST_GROUP_NAME      "h5l_same_loc_test_group"
#define H5L_SAME_LOC_TEST_LINK_NAME1      "h5l_same_loc_test_link1"
#define H5L_SAME_LOC_TEST_LINK_NAME2      "h5l_same_loc_test_link2"
#define H5L_SAME_LOC_TEST_DSET_NAME       "h5l_same_loc_test_dset"

#define COPY_LINK_TEST_SOFT_LINK_TARGET_PATH "/" COPY_LINK_TEST_GROUP_NAME "/" COPY_LINK_TEST_DSET_NAME
#define COPY_LINK_TEST_HARD_LINK_COPY_NAME   "hard_link_to_dset_copy"
#define COPY_LINK_TEST_SOFT_LINK_COPY_NAME   "soft_link_to_dset_copy"
#define COPY_LINK_TEST_HARD_LINK_NAME        "hard_link_to_dset"
#define COPY_LINK_TEST_SOFT_LINK_NAME        "soft_link_to_dset"
#define COPY_LINK_TEST_GROUP_NAME            "link_copy_test_group"
#define COPY_LINK_TEST_DSET_NAME             "link_copy_test_dset"
#define COPY_LINK_TEST_DSET_SPACE_RANK       2
#define COPY_LINK_TEST_DSET_TYPE             H5T_NATIVE_INT

#define MOVE_LINK_TEST_SOFT_LINK_TARGET_PATH "/" MOVE_LINK_TEST_GROUP_NAME "/" MOVE_LINK_TEST_DSET_NAME
#define MOVE_LINK_TEST_HARD_LINK_NAME        "hard_link_to_dset"
#define MOVE_LINK_TEST_SOFT_LINK_NAME        "soft_link_to_dset"
#define MOVE_LINK_TEST_GROUP_NAME            "link_move_test_group"
#define MOVE_LINK_TEST_DSET_NAME             "link_move_test_dset"
#define MOVE_LINK_TEST_DSET_SPACE_RANK       2
#define MOVE_LINK_TEST_DSET_TYPE             H5T_NATIVE_INT

/*****************************************************
 *                                                   *
 *            Plugin Object test defines             *
 *                                                   *
 *****************************************************/

#define GENERIC_DATASET_OPEN_TEST_SPACE_RANK 2
#define GENERIC_DATASET_OPEN_TEST_DSET_DTYPE H5T_NATIVE_INT
#define GENERIC_DATASET_OPEN_TEST_DSET_NAME  "generic_dataset_open_test"

#define GENERIC_GROUP_OPEN_TEST_GROUP_NAME "generic_group_open_test"

#define GENERIC_DATATYPE_OPEN_TEST_TYPE_NAME "generic_datatype_open_test"
#define GENERIC_DATATYPE_OPEN_TEST_TYPE_SIZE 50

#define OBJ_REF_DATASET_WRITE_TEST_REF_DSET_DTYPE H5T_NATIVE_INT
#define OBJ_REF_DATASET_WRITE_TEST_REF_TYPE_DTYPE H5T_STRING
#define OBJ_REF_DATASET_WRITE_TEST_REF_TYPE_SIZE  50
#define OBJ_REF_DATASET_WRITE_TEST_SUBGROUP_NAME  "obj_ref_write_test"
#define OBJ_REF_DATASET_WRITE_TEST_REF_DSET_NAME  "ref_dset"
#define OBJ_REF_DATASET_WRITE_TEST_REF_TYPE_NAME  "ref_dtype"
#define OBJ_REF_DATASET_WRITE_TEST_SPACE_RANK     1
#define OBJ_REF_DATASET_WRITE_TEST_DSET_NAME      "obj_ref_dset"

#define OBJ_REF_DATASET_READ_TEST_REF_DSET_DTYPE H5T_NATIVE_INT
#define OBJ_REF_DATASET_READ_TEST_REF_TYPE_DTYPE H5T_STRING
#define OBJ_REF_DATASET_READ_TEST_REF_TYPE_SIZE  50
#define OBJ_REF_DATASET_READ_TEST_SUBGROUP_NAME  "obj_ref_read_test"
#define OBJ_REF_DATASET_READ_TEST_REF_DSET_NAME  "ref_dset"
#define OBJ_REF_DATASET_READ_TEST_REF_TYPE_NAME  "ref_dtype"
#define OBJ_REF_DATASET_READ_TEST_SPACE_RANK     1
#define OBJ_REF_DATASET_READ_TEST_DSET_NAME      "obj_ref_dset"

#define OBJ_REF_DATASET_EMPTY_WRITE_TEST_SUBGROUP_NAME  "obj_ref_empty_write_test"
#define OBJ_REF_DATASET_EMPTY_WRITE_TEST_SPACE_RANK     1
#define OBJ_REF_DATASET_EMPTY_WRITE_TEST_DSET_NAME      "obj_ref_dset"

/*****************************************************
 *                                                   *
 *         Plugin Miscellaneous test defines         *
 *                                                   *
 *****************************************************/

#define OPEN_LINK_WITHOUT_SLASH_DSET_DIMS 2
#define OPEN_LINK_WITHOUT_SLASH_DSET_NAME "link_without_slash_test_dset"

#define OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_CONTAINER_GROUP_NAME "absolute_path_test_container_group"
#define OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_SUBGROUP_NAME        "absolute_path_test_subgroup"
#define OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DTYPE_NAME           "absolute_path_test_dtype"
#define OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DSET_NAME            "absolute_path_test_dset"
#define OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DSET_SPACE_RANK      3
#define OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DSET_DIM_SIZE        5
#define OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DSET_DTYPE           H5T_NATIVE_INT
#define OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DTYPE_SIZE           30

#define ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME "absolute_vs_relative_test_container_group"
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET1_NAME           "absolute_vs_relative_test_dset1"
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET2_NAME           "absolute_vs_relative_test_dset2"
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET3_NAME           "absolute_vs_relative_test_dset3"
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET4_NAME           "absolute_vs_relative_test_dset4"
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET5_NAME           "absolute_vs_relative_test_dset5"
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET6_NAME           "absolute_vs_relative_test_dset6"
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET_SPACE_RANK      3
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET_DIM_SIZE        5
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET1_DTYPE          H5T_NATIVE_INT
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET2_DTYPE          H5T_NATIVE_INT
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET3_DTYPE          H5T_NATIVE_INT
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET4_DTYPE          H5T_NATIVE_INT
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET5_DTYPE          H5T_NATIVE_INT
#define ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET6_DTYPE          H5T_NATIVE_INT

#define URL_ENCODING_TEST_DSET_DIM_SIZE 10
#define URL_ENCODING_TEST_SPACE_RANK    2
#define URL_ENCODING_TEST_DSET_DTYPE    H5T_NATIVE_INT
#define URL_ENCODING_TEST_ATTR_DTYPE    H5T_NATIVE_INT
#define URL_ENCODING_TEST_GROUP_NAME    "url_encoding_group !*'();:@&=+$,?#[]-.<>\\\\^`{}|~"
#define URL_ENCODING_TEST_DSET_NAME     "url_encoding_dset !*'();:@&=+$,?#[]-.<>\\\\^`{}|~"
#define URL_ENCODING_TEST_ATTR_NAME     "url_encoding_attr !*'();:@&=+$,?#[]-.<>\\\\^`{}|~"


/* Plugin initialization/termination test */
static int test_setup_plugin(void);

/* File interface tests */
static int test_create_file(void);
static int test_get_existing_file_info(void);
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
static int test_get_existing_group_info(void);
static int test_nonexistent_group(void);
static int test_unused_group_API_calls(void);
static int test_group_property_lists(void);

/* Attribute interface tests */
static int test_create_attribute_on_root(void);
static int test_create_attribute_on_dataset(void);
static int test_create_attribute_on_datatype(void);
static int test_get_existing_attribute_info(void);
static int test_get_attribute_name(void);
static int test_create_attribute_with_space_in_name(void);
static int test_delete_attribute(void);
static int test_write_attribute(void);
static int test_read_attribute(void);
static int test_get_number_attributes(void);
static int test_attribute_property_lists(void);

/* Dataset interface tests */
static int test_create_dataset_under_root(void);
static int test_create_anonymous_dataset(void);
static int test_create_dataset_under_existing_group(void);
static int test_create_dataset_predefined_types(void);
static int test_create_dataset_string_types(void);
static int test_create_dataset_compound_types(void);
static int test_create_dataset_enum_types(void);
static int test_create_dataset_array_types(void);
static int test_create_dataset_shapes(void);
static int test_create_dataset_creation_properties(void);
static int test_create_dataset_combinations(void);
static int test_create_dataset_large_datatype(void);
static int test_write_dataset_small_all(void);
static int test_write_dataset_small_hyperslab(void);
static int test_write_dataset_small_point_selection(void);
static int test_write_dataset_large_all(void);
static int test_write_dataset_large_hyperslab(void);
static int test_write_dataset_large_point_selection(void);
static int test_read_dataset_small_all(void);
static int test_read_dataset_small_hyperslab(void);
static int test_read_dataset_small_point_selection(void);
static int test_read_dataset_large_all(void);
static int test_read_dataset_large_hyperslab(void);
static int test_read_dataset_large_point_selection(void);
static int test_write_dataset_data_verification(void);
static int test_unused_dataset_API_calls(void);
static int test_dataset_property_lists(void);

/* Link interface tests */
static int test_create_hard_link(void);
static int test_create_hard_link_same_loc(void);
static int test_open_object_by_hard_link(void);
static int test_create_soft_link_existing_relative(void);
static int test_create_soft_link_existing_absolute(void);
static int test_create_soft_link_dangling_relative(void);
static int test_create_soft_link_dangling_absolute(void);
static int test_open_object_by_soft_link(void);
static int test_create_external_link(void);
static int test_open_object_by_external_link(void);
static int test_copy_link(void);
static int test_move_link(void);
static int test_unused_link_API_calls(void);

/* Committed Datatype interface tests */
static int test_create_committed_datatype(void);
static int test_create_anonymous_committed_datatype(void);
static int test_create_committed_datatype_combinations(void);
static int test_create_dataset_with_committed_type(void);
static int test_create_attribute_with_committed_type(void);
static int test_delete_committed_type(void);
static int test_get_existing_type_info(void);
static int test_unused_datatype_API_calls(void);
static int test_datatype_property_lists(void);

/* Object interface tests */
static int test_open_dataset_generically(void);
static int test_open_group_generically(void);
static int test_open_datatype_generically(void);
static int test_h5o_close(void);
static int test_create_obj_ref(void);
static int test_write_dataset_w_obj_refs(void);
static int test_read_dataset_w_obj_refs(void);
static int test_write_dataset_w_obj_refs_empty_data(void);
static int test_unused_object_API_calls(void);

/* Miscellaneous tests to check edge cases */
static int test_open_link_without_leading_slash(void);
static int test_object_creation_by_absolute_path(void);
static int test_absolute_vs_relative_path(void);
static int test_double_init_free(void);
static int test_url_encoding(void);
static int test_H5P_DEFAULT(void);

static int cleanup(void);

static int (*tests[])(void) = {
        test_setup_plugin,
        test_create_file,
        test_get_existing_file_info,
        test_nonexistent_file,
        test_get_file_intent,
        test_get_file_name,
        test_file_reopen,
        test_unused_file_API_calls,
        test_file_property_lists,
        test_create_group_invalid_loc_id,
        test_create_group_under_root,
        test_create_group_under_existing_group,
        test_get_existing_group_info,
        test_nonexistent_group,
        test_unused_group_API_calls,
        test_group_property_lists,
        test_create_attribute_on_root,
        test_create_attribute_on_dataset,
        test_create_attribute_on_datatype,
        test_get_existing_attribute_info,
        test_get_attribute_name,
        test_create_attribute_with_space_in_name,
        test_delete_attribute,
        test_write_attribute,
        test_read_attribute,
        test_get_number_attributes,
        test_attribute_property_lists,
        test_create_dataset_under_root,
        test_create_anonymous_dataset,
        test_create_dataset_under_existing_group,
        test_create_dataset_predefined_types,
        test_create_dataset_string_types,
        test_create_dataset_compound_types,
        test_create_dataset_enum_types,
        test_create_dataset_array_types,
        test_create_dataset_shapes,
        test_create_dataset_creation_properties,
        test_create_dataset_combinations,
        test_create_dataset_large_datatype,
        test_write_dataset_small_all,
        test_write_dataset_small_hyperslab,
        test_write_dataset_small_point_selection,
        test_write_dataset_large_all,
        test_write_dataset_large_hyperslab,
        test_write_dataset_large_point_selection,
        test_read_dataset_small_all,
        test_read_dataset_small_hyperslab,
        test_read_dataset_small_point_selection,
        test_read_dataset_large_all,
        test_read_dataset_large_hyperslab,
        test_read_dataset_large_point_selection,
        test_write_dataset_data_verification,
        test_unused_dataset_API_calls,
        test_dataset_property_lists,
        test_create_hard_link,
        test_create_hard_link_same_loc,
        test_open_object_by_hard_link,
        test_create_soft_link_existing_relative,
        test_create_soft_link_existing_absolute,
        test_create_soft_link_dangling_relative,
        test_create_soft_link_dangling_absolute,
        test_open_object_by_soft_link,
        test_create_external_link,
        test_open_object_by_external_link,
        test_copy_link,
        test_move_link,
        test_unused_link_API_calls,
        test_create_committed_datatype,
        test_create_anonymous_committed_datatype,
        test_create_committed_datatype_combinations,
        test_create_dataset_with_committed_type,
        test_create_attribute_with_committed_type,
        test_delete_committed_type,
        test_get_existing_type_info,
        test_unused_datatype_API_calls,
        test_datatype_property_lists,
        test_open_dataset_generically,
        test_open_group_generically,
        test_open_datatype_generically,
        test_h5o_close,
        test_create_obj_ref,
        test_write_dataset_w_obj_refs,
        test_read_dataset_w_obj_refs,
        test_write_dataset_w_obj_refs_empty_data,
        test_unused_object_API_calls,
        test_open_link_without_leading_slash,
        test_object_creation_by_absolute_path,
        test_absolute_vs_relative_path,
        test_double_init_free,
        test_url_encoding,
        test_H5P_DEFAULT,
        /*cleanup*/
};

/*****************************************************
 *                                                   *
 *      Plugin initialization/termination tests      *
 *                                                   *
 *****************************************************/


static int
test_setup_plugin(void)
{
    hid_t fapl = -1;

    TESTING("plugin setup");

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if (H5Pclose(fapl) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Pclose(fapl);
        RVterm();
    } H5E_END_TRY;

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

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fcreate(FILENAME, H5F_ACC_TRUNC, H5P_DEFAULT, fapl)) < 0) {
        H5_FAILED();
        printf("    couldn't create file\n");
        goto error;
    }

    /* Setup container groups for the different classes of tests */
    if ((group_id = H5Gcreate2(file_id, GROUP_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group for group tests\n");
        goto error;
    }

    if (H5Gclose(group_id) < 0)
        TEST_ERROR

    if ((group_id = H5Gcreate2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group for attribute tests\n");
        goto error;
    }

    if (H5Gclose(group_id) < 0)
        TEST_ERROR

    if ((group_id = H5Gcreate2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group for dataset tests\n");
        goto error;
    }

    if (H5Gclose(group_id) < 0)
        TEST_ERROR

    if ((group_id = H5Gcreate2(file_id, DATATYPE_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
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

    if ((group_id = H5Gcreate2(file_id, MISCELLANEOUS_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Gclose(group_id);
        H5Pclose(fapl);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_get_existing_file_info(void)
{
    H5F_info2_t file_info;
    hid_t       file_id = -1, fapl_id = -1;

    TESTING("retrieve file info")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if (H5Fget_info2(file_id, &file_info) < 0) {
        H5_FAILED();
        printf("    couldn't get file info\n");
        goto error;
    }

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_nonexistent_file(void)
{
    hid_t file_id = -1, fapl_id = -1;

    TESTING("failure for opening non-existent file")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    H5E_BEGIN_TRY {
        file_id = H5Fopen(NONEXISTENT_FILENAME, H5F_ACC_RDWR, fapl_id);
    } H5E_END_TRY;

    if (file_id >= 0) {
        H5_FAILED();
        printf("    non-existent file was opened\n");
        goto error;
    }

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Pclose(fapl_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_get_file_intent(void)
{
    unsigned file_intent;
    hsize_t  space_dims[FILE_INTENT_TEST_DSET_RANK] = { 10, 10 };
    hid_t    file_id = -1, fapl_id = -1, dset_id = -1, space_id = -1;

    TESTING("retrieve file intent")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    /* Test that file intent works correctly for file create */
    if ((file_id = H5Fcreate(FILE_INTENT_TEST_FILENAME, H5F_ACC_TRUNC, H5P_DEFAULT, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't create file\n");
        goto error;
    }

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
    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDONLY, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if (H5Fget_intent(file_id, &file_intent) < 0)
        TEST_ERROR

    if (H5F_ACC_RDONLY != file_intent) {
        H5_FAILED();
        printf("    received incorrect file intent\n");
        goto error;
    }

    if ((space_id = H5Screate_simple(FILE_INTENT_TEST_DSET_RANK, space_dims, NULL)) < 0)
        TEST_ERROR

    /* Ensure that no objects can be created when a file is opened in read-only mode */
    H5E_BEGIN_TRY {
        dset_id = H5Dcreate2(file_id, FILE_INTENT_TEST_DATASETNAME, H5T_NATIVE_INT, space_id,
                      H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    } H5E_END_TRY;

    if (dset_id >= 0) {
        H5_FAILED();
        printf("    read-only file was modified\n");
        goto error;
    }

    if (H5Fclose(file_id) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if (H5Fget_intent(file_id, &file_intent) < 0)
        TEST_ERROR

    if (H5F_ACC_RDWR != file_intent) {
        H5_FAILED();
        printf("    received incorrect file intent\n");
        goto error;
    }

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(space_id);
        H5Dclose(dset_id);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_get_file_name(void)
{
    ssize_t  file_name_buf_len = 0;
    char    *file_name_buf = NULL;
    hid_t    file_id = -1, fapl_id = -1;

    TESTING("get file name with H5Fget_name")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    /* Retrieve the size of the file name */
    if ((file_name_buf_len = H5Fget_name(file_id, NULL, 0)) < 0)
        TEST_ERROR

    /* Allocate buffer for file name */
    if (NULL == (file_name_buf = (char *) malloc((size_t) file_name_buf_len + 1)))
        TEST_ERROR

    /* Retrieve the actual file name */
    if (H5Fget_name(file_id, file_name_buf, (size_t) file_name_buf_len + 1) < 0)
        TEST_ERROR

    if (file_name_buf) {
        free(file_name_buf);
        file_name_buf = NULL;
    }

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        if (file_name_buf) free(file_name_buf);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_file_reopen(void)
{
    hid_t file_id = -1, file_id2 = -1, fapl_id = -1;

    TESTING("re-open file w/ H5Freopen")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        H5Fclose(file_id2);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_unused_file_API_calls(void)
{
    hid_t file_id = -1, fapl_id = -1;
    int   nerrors = 0;

    TESTING("unused File API calls")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    H5E_BEGIN_TRY {
        H5AC_cache_config_t  mdc_config = { 0 };
        hsize_t              filesize;
        double               mdc_hit_rate;
        size_t               file_image_buf_len = 0;
        void                *file_handle;

        if (H5Fclear_elink_file_cache(file_id) >= 0)
            nerrors++;
        if (H5Fget_file_image(file_id, NULL, file_image_buf_len) >= 0)
            nerrors++;
        if (H5Fget_free_sections(file_id, H5FD_MEM_DEFAULT, 0, NULL) >= 0)
            nerrors++;
        if (H5Fget_freespace(file_id) >= 0)
            nerrors++;
        if (H5Fget_mdc_config(file_id, &mdc_config) >= 0)
            nerrors++;
        if (H5Fget_mdc_hit_rate(file_id, &mdc_hit_rate) >= 0)
            nerrors++;
        if (H5Fget_mdc_size(file_id, NULL, NULL, NULL, NULL) >= 0)
            nerrors++;
        if (H5Fget_filesize(file_id, &filesize) >= 0)
            nerrors++;
        if (H5Fget_vfd_handle(file_id, fapl_id, &file_handle) >= 0)
            nerrors++;
        if (H5Freset_mdc_hit_rate_stats(file_id) >= 0)
            nerrors++;
        if (H5Fset_mdc_config(file_id, &mdc_config) >= 0)
            nerrors++;
    } H5E_END_TRY;

    if (nerrors)
        TEST_ERROR

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_file_property_lists(void)
{
    hid_t file_id1 = -1, file_id2 = -1, fapl_id = -1;
    hid_t fcpl_id1 = -1, fcpl_id2 = -1;
    hid_t fapl_id1 = -1, fapl_id2 = -1;

    TESTING("file property list operations")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((fcpl_id1 = H5Pcreate(H5P_FILE_CREATE)) < 0) {
        H5_FAILED();
        printf("    couldn't create FCPL\n");
        goto error;
    }



    if ((file_id1 = H5Fcreate(FILE_PROPERTY_LIST_TEST_FNAME1, H5F_ACC_TRUNC, fcpl_id1, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't create file\n");
        goto error;
    }

    if ((file_id2 = H5Fcreate(FILE_PROPERTY_LIST_TEST_FNAME2, H5F_ACC_TRUNC, H5P_DEFAULT, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't create file\n");
        goto error;
    }

    if (H5Pclose(fcpl_id1) < 0)
        TEST_ERROR

    /* Try to receive copies of the two property lists, one which has the property set and one which does not */
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



    /* Due to the nature of needing to supply a FAPL with the REST VOL having been set on it to the H5Fcreate() call,
     * we cannot exactly test using H5P_DEFAULT as the FAPL for one of the create calls in this test. However, the
     * use of H5Fget_create_plist() will still be used to check that the FAPL is correct after both creating and
     * opening a file
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

    /* Check that both FAPLs have the REST VOL set on them */
    {
        void *vol_info;

        if (NULL == (vol_info = H5Pget_vol_info(fapl_id1))) {
            H5_FAILED();
            printf("    couldn't retrieve VOL info from FAPL\n");
            goto error;
        }



        if (NULL == (vol_info = H5Pget_vol_info(fapl_id2))) {
            H5_FAILED();
            printf("    couldn't retrieve VOL infor from FAPL\n");
            goto error;
        }


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

    if ((file_id1 = H5Fopen(FILE_PROPERTY_LIST_TEST_FNAME1, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((file_id2 = H5Fopen(FILE_PROPERTY_LIST_TEST_FNAME2, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((fcpl_id1 = H5Fget_create_plist(file_id1)) < 0) {
        H5_FAILED();
        printf("    couldn't get property list\n");
        goto error;
    }

    if ((fcpl_id1 = H5Fget_create_plist(file_id2)) < 0) {
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Pclose(fcpl_id1);
        H5Pclose(fcpl_id2);
        H5Pclose(fapl_id1);
        H5Pclose(fapl_id2);
        H5Pclose(fapl_id);
        H5Fclose(file_id1);
        H5Fclose(file_id2);
        RVterm();
    } H5E_END_TRY;

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

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    H5E_BEGIN_TRY {
        group_id = H5Gcreate2(file_id, GROUP_CREATE_INVALID_LOC_ID_GNAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    } H5E_END_TRY;

    if (group_id >= 0) {
        H5_FAILED();
        printf("    created group in invalid loc_id\n");
        goto error;
    }

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Pclose(fapl_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_create_group_under_root(void)
{
    hid_t file_id = -1, group_id = -1, fapl_id = -1;

    TESTING("create group under root group")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    /* Create the group under the root group of the file */
    if ((group_id = H5Gcreate2(file_id, GROUP_CREATE_UNDER_ROOT_GNAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Gclose(group_id);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_create_group_under_existing_group(void)
{
    hid_t file_id = -1;
    hid_t parent_group_id = -1, new_group_id = -1;
    hid_t fapl_id = -1;

    TESTING("create group under existing group using relative path")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
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

    /* Create a new Group under the already-existing parent Group using a relative path */
    if ((new_group_id = H5Gcreate2(parent_group_id, GROUP_CREATE_UNDER_GROUP_REL_GNAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Gclose(new_group_id);
        H5Gclose(parent_group_id);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;

}

static int
test_get_existing_group_info(void)
{
    H5G_info_t group_info;
    hid_t      file_id = -1, fapl_id = -1;

    TESTING("retrieve group info")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if (H5Gget_info(file_id, &group_info) < 0) {
        H5_FAILED();
        printf("    couldn't get group info\n");
        goto error;
    }

    if (H5Gget_info_by_name(file_id, "/", &group_info, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't get group info by name\n");
        goto error;
    }

    if (H5Gget_info_by_idx(file_id, "/", H5_INDEX_NAME, H5_ITER_INC, 0, &group_info, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't get group info by index\n");
        goto error;
    }

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_nonexistent_group(void)
{
    hid_t file_id = -1, group_id = -1, fapl_id = -1;

    TESTING("failure for opening nonexistent group")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    H5E_BEGIN_TRY {
        group_id = H5Gopen2(file_id, NONEXISTENT_GROUP_TEST_GNAME, H5P_DEFAULT);
    } H5E_END_TRY;

    if (group_id >= 0) {
        H5_FAILED();
        printf("    opened non-existent group\n");
        goto error;
    }

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_unused_group_API_calls(void)
{
    TESTING("unused group API calls")

    SKIPPED();

    return 0;

error:
    return 1;
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

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
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

    if (H5Pset_local_heap_size_hint(gcpl_id1, dummy_prop_val) < 0) {
        H5_FAILED();
        printf("    couldn't set   property on GCPL\n");
        goto error;
    }

    /* Create the group in the file */
    if ((group_id1 = H5Gcreate2(container_group, GROUP_PROPERTY_LIST_TEST_GROUP_NAME1, H5P_DEFAULT, gcpl_id1, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group\n");
        goto error;
    }

    /* Create the second group using H5P_DEFAULT for the GCPL */
    if ((group_id2 = H5Gcreate2(container_group, GROUP_PROPERTY_LIST_TEST_GROUP_NAME2, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group\n");
        goto error;
    }

    if (H5Pclose(gcpl_id1) < 0)
        TEST_ERROR

    /* Try to retrieve copies of the two property lists, one which has the property set and one which does not */
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Pclose(gcpl_id1);
        H5Pclose(gcpl_id2);
        H5Gclose(group_id1);
        H5Gclose(group_id2);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

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
    hid_t   attr_id = -1;
    hid_t   space_id = -1;

    TESTING("create, close and open attribute on root group")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    for (i = 0; i < ATTRIBUTE_CREATE_ON_ROOT_SPACE_RANK; i++)
        dims[i] = (hsize_t) (rand() % 64 + 1);

    if ((space_id = H5Screate_simple(ATTRIBUTE_CREATE_ON_ROOT_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_id = H5Acreate2(file_id, ATTRIBUTE_CREATE_ON_ROOT_ATTR_NAME, ATTRIBUTE_CREATE_ON_ROOT_DTYPE,
            space_id, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    /* Verify the attribute has been created */
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

    /* Now close the attribute and verify we can open it */
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR

    if ((attr_id = H5Aopen(file_id, ATTRIBUTE_CREATE_ON_ROOT_ATTR_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute\n");
        goto error;
    }

    if (H5Aclose(attr_id) < 0)
        TEST_ERROR

    if ((attr_id = H5Aopen_by_name(file_id, "/", ATTRIBUTE_CREATE_ON_ROOT_ATTR_NAME, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute by name\n");
        goto error;
    }

#if 0
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR

    if ((attr_id = H5Aopen_by_idx(file_id, "/", H5_INDEX_NAME, H5_ITER_INC, 0, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute by index\n");
        goto error;
    }
#endif

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(space_id);
        H5Aclose(attr_id);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

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
    hid_t   dset_id = -1;
    hid_t   attr_id = -1;
    hid_t   dset_space_id = -1;
    hid_t   attr_space_id = -1;

    TESTING("create attribute on dataset")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
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
        dset_dims[i] = (hsize_t) rand() % 64 + 1;
    for (i = 0; i < ATTRIBUTE_CREATE_ON_DATASET_ATTR_SPACE_RANK; i++)
        attr_dims[i] = (hsize_t) rand() % 64 + 1;

    if ((dset_space_id = H5Screate_simple(ATTRIBUTE_CREATE_ON_DATASET_DSET_SPACE_RANK, dset_dims, NULL)) < 0)
        TEST_ERROR
    if ((attr_space_id = H5Screate_simple(ATTRIBUTE_CREATE_ON_DATASET_ATTR_SPACE_RANK, attr_dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, ATTRIBUTE_CREATE_ON_DATASET_DSET_NAME, ATTRIBUTE_CREATE_ON_DATASET_DSET_DTYPE,
            dset_space_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if ((attr_id = H5Acreate2(dset_id, ATTRIBUTE_CREATE_ON_DATASET_ATTR_NAME, ATTRIBUTE_CREATE_ON_DATASET_ATTR_DTYPE,
            attr_space_id, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    /* Verify the attribute has been created */
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

    /* Now close the attribute and verify we can open it */
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR

    if ((attr_id = H5Aopen(dset_id, ATTRIBUTE_CREATE_ON_DATASET_ATTR_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute\n");
        goto error;
    }

    if (H5Aclose(attr_id) < 0)
        TEST_ERROR

    if ((attr_id = H5Aopen_by_name(dset_id, "/" ATTRIBUTE_CREATE_ON_DATASET_DSET_NAME, ATTRIBUTE_CREATE_ON_DATASET_ATTR_NAME, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute by name\n");
        goto error;
    }

#if 0
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR

    if ((attr_id = H5Aopen_by_idx(dset_id, "/" ATTRIBUTE_CREATE_ON_DATASET_DSET_NAME, H5_INDEX_NAME, H5_ITER_INC, 0, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute by index\n");
        goto error;
    }
#endif

    if (H5Sclose(dset_space_id) < 0)
        TEST_ERROR
    if (H5Sclose(attr_space_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(dset_space_id);
        H5Sclose(attr_space_id);
        H5Dclose(dset_id);
        H5Aclose(attr_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

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
    hid_t   dtype_id = -1;
    hid_t   attr_id = -1;
    hid_t   space_id = -1;

    TESTING("create attribute on committed datatype")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((dtype_id = H5Tcreate(H5T_STRING, ATTRIBUTE_CREATE_ON_DATATYPE_DTYPE_SIZE)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if (H5Tcommit2(container_group, ATTRIBUTE_CREATE_ON_DATATYPE_DTYPE_NAME, dtype_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't commit datatype\n");
        goto error;
    }

    {
        /* Temporary workaround for now since H5Tcommit2 doesn't return something public useable
         * for a VOL object */
        if (H5Tclose(dtype_id) < 0)
            TEST_ERROR

        if ((dtype_id = H5Topen2(container_group, ATTRIBUTE_CREATE_ON_DATATYPE_DTYPE_NAME, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't open committed datatype\n");
            goto error;
        }
    }

    for (i = 0; i < ATTRIBUTE_CREATE_ON_DATATYPE_SPACE_RANK; i++)
        dims[i] = (hsize_t) rand() % 64 + 1;

    if ((space_id = H5Screate_simple(ATTRIBUTE_CREATE_ON_DATATYPE_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_id = H5Acreate2(dtype_id, ATTRIBUTE_CREATE_ON_DATATYPE_ATTR_NAME, ATTRIBUTE_CREATE_ON_DATATYPE_DTYPE,
            space_id, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    /* Verify the attribute has been created */
    if ((attr_exists = H5Aexists(dtype_id, ATTRIBUTE_CREATE_ON_DATATYPE_ATTR_NAME)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if attribute exists\n");
        goto error;
    }

    if (!attr_exists) {
        H5_FAILED();
        printf("    attribute did not exist\n");
        goto error;
    }

    /* Now close the attribute and verify we can open it */
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR

    if ((attr_id = H5Aopen(dtype_id, ATTRIBUTE_CREATE_ON_DATATYPE_ATTR_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute\n");
        goto error;
    }

    if (H5Aclose(attr_id) < 0)
        TEST_ERROR

    if ((attr_id = H5Aopen_by_name(dtype_id, "/" ATTRIBUTE_CREATE_ON_DATATYPE_DTYPE_NAME, ATTRIBUTE_CREATE_ON_DATATYPE_ATTR_NAME, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute by name\n");
        goto error;
    }

#if 0
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR

    if ((attr_id = H5Aopen_by_idx(dtype_id, "/" ATTRIBUTE_CREATE_ON_DATATYPE_DTYPE_NAME, H5_INDEX_NAME, H5_ITER_INC, 0, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open attribute by index\n");
        goto error;
    }
#endif

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Tclose(dtype_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(space_id);
        H5Aclose(attr_id);
        H5Tclose(dtype_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_get_existing_attribute_info(void)
{
    H5A_info_t attr_info;
    hsize_t    dims[ATTRIBUTE_GET_INFO_TEST_SPACE_RANK];
    size_t     i;
    htri_t     attr_exists;
    hid_t      file_id = -1, fapl_id = -1;
    hid_t      container_group = -1;
    hid_t      attr_id = -1;
    hid_t      space_id = -1;

    TESTING("retrieve attribute info")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
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
        dims[i] = (hsize_t) (rand() % 64 + 1);

    if ((space_id = H5Screate_simple(ATTRIBUTE_GET_INFO_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_id = H5Acreate2(container_group, ATTRIBUTE_GET_INFO_TEST_ATTR_NAME, ATTRIBUTE_GET_INFO_TEST_ATTR_DTYPE,
            space_id, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
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

    if (H5Aget_info(attr_id, &attr_info) < 0) {
        H5_FAILED();
        printf("    couldn't get attribute info\n");
        goto error;
    }

    if (H5Aget_info_by_name(container_group, "/", ATTRIBUTE_GET_INFO_TEST_ATTR_NAME, &attr_info, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't get attribute info by name\n");
        goto error;
    }

    if (H5Aget_info_by_idx(container_group, "/", H5_INDEX_NAME, H5_ITER_INC, 0, &attr_info, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't get attribute info by index\n");
        goto error;
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(space_id);
        H5Aclose(attr_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_get_attribute_name(void)
{
    hsize_t  dims[ATTRIBUTE_GET_NAME_TEST_SPACE_RANK];
    ssize_t  name_buf_size;
    size_t   i;
    htri_t   attr_exists;
    char    *name_buf = NULL;
    hid_t    file_id = -1, fapl_id = -1;
    hid_t    container_group = -1;
    hid_t    attr_id = -1;
    hid_t    space_id = -1;

    TESTING("retrieve attribute name")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
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
        dims[i] = (hsize_t) (rand() % 64 + 1);

    if ((space_id = H5Screate_simple(ATTRIBUTE_GET_NAME_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_id = H5Acreate2(container_group, ATTRIBUTE_GET_NAME_TEST_ATTRIBUTE_NAME, ATTRIBUTE_GET_NAME_TEST_ATTRIBUTE_TYPE,
            space_id, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
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

    /* Retrieve the name buffer size */
    if ((name_buf_size = H5Aget_name(attr_id, 0, NULL)) < 0) {
        H5_FAILED();
        printf("    couldn't retrieve name buf size\n");
        goto error;
    }

    if (NULL == (name_buf = (char *) malloc((size_t) name_buf_size + 1)))
        TEST_ERROR

    if (H5Aget_name(attr_id, (size_t) name_buf_size + 1, name_buf) < 0) {
        H5_FAILED();
        printf("    couldn't retrieve attribute name\n");
    }

    if (strcmp(name_buf, ATTRIBUTE_GET_NAME_TEST_ATTRIBUTE_NAME)) {
        H5_FAILED();
        printf("    retrieved attribute name didn't match\n");
        goto error;
    }

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

    if (H5Aget_name(attr_id, (size_t) name_buf_size + 1, name_buf) < 0) {
        H5_FAILED();
        printf("    couldn't retrieve attribute name\n");
        goto error;
    }

    if (strcmp(name_buf, ATTRIBUTE_GET_NAME_TEST_ATTRIBUTE_NAME)) {
        H5_FAILED();
        printf("    attribute name didn't match\n");
        goto error;
    }

    if (name_buf) {
        free(name_buf);
        name_buf = NULL;
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        if (name_buf) free(name_buf);
        H5Sclose(space_id);
        H5Aclose(attr_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

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
    hid_t   attr_id = -1;
    hid_t   space_id = -1;

    TESTING("create attribute with a space in its name")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
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
        dims[i] = (hsize_t) (rand() % 64 + 1);

    if ((space_id = H5Screate_simple(ATTRIBUTE_CREATE_WITH_SPACE_IN_NAME_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_id = H5Acreate2(container_group, ATTRIBUTE_CREATE_WITH_SPACE_IN_NAME_ATTR_NAME, ATTRIBUTE_CREATE_WITH_SPACE_IN_NAME_DTYPE,
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
    if (H5Aclose(attr_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(space_id);
        H5Aclose(attr_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

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
    hid_t   attr_id = -1;
    hid_t   space_id = -1;

    TESTING("delete an attribute")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
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
        dims[i] = (hsize_t) (rand() % 64 + 1);

    if ((space_id = H5Screate_simple(ATTRIBUTE_DELETION_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_id = H5Acreate2(container_group, ATTRIBUTE_DELETION_TEST_ATTR_NAME, ATTRIBUTE_DELETION_TEST_ATTR_DTYPE,
            space_id, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
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
        printf("    attribute existed!\n");
        goto error;
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(space_id);
        H5Aclose(attr_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_write_attribute(void)
{
    hsize_t  dims[ATTRIBUTE_WRITE_TEST_SPACE_RANK];
    size_t   i, data_size;
    htri_t   attr_exists;
    hid_t    file_id = -1, fapl_id = -1;
    hid_t    container_group = -1;
    hid_t    attr_id = -1;
    hid_t    space_id = -1;
    void    *data = NULL;

    TESTING("write data to an attribute")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
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
        dims[i] = (hsize_t) (rand() % 64 + 1);

    if ((space_id = H5Screate_simple(ATTRIBUTE_WRITE_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_id = H5Acreate2(container_group, ATTRIBUTE_WRITE_TEST_ATTR_NAME, ATTRIBUTE_WRITE_TEST_ATTR_DTYPE,
            space_id, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
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
        ((int *) data)[i] = (int) i;

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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(space_id);
        H5Aclose(attr_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_read_attribute(void)
{
    hsize_t  dims[ATTRIBUTE_READ_TEST_SPACE_RANK];
    size_t   i, data_size;
    htri_t   attr_exists;
    hid_t    file_id = -1, fapl_id = -1;
    hid_t    container_group = -1;
    hid_t    attr_id = -1;
    hid_t    space_id = -1;
    void    *data = NULL;
    void    *read_buf = NULL;

    TESTING("read data from an attribute")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
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
        dims[i] = (hsize_t) (rand() % 64 + 1);

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
        ((int *) data)[i] = (int) i;

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

    if (H5Aread(attr_id, ATTRIBUTE_READ_TEST_ATTR_DTYPE, read_buf) < 0) {
        H5_FAILED();
        printf("    couldn't read from attribute\n");
        goto error;
    }

    for (i = 0; i < data_size / ATTRIBUTE_READ_TEST_ATTR_DTYPE_SIZE; i++)
        if (((int *) read_buf)[i] != (int) i) {
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        if (data) free(data);
        if (read_buf) free(read_buf);
        H5Sclose(space_id);
        H5Aclose(attr_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_get_number_attributes(void)
{
    H5O_info_t obj_info;
    hsize_t    dims[ATTRIBUTE_GET_NUM_ATTRS_TEST_SPACE_RANK];
    size_t     i;
    htri_t     attr_exists;
    hid_t      file_id = -1, fapl_id = -1;
    hid_t      container_group = -1;
    hid_t      attr_id = -1;
    hid_t      space_id = -1;

    TESTING("retrieve the number of attributes on an object")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
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
        dims[i] = (hsize_t) (rand() % 64 + 1);

    if ((space_id = H5Screate_simple(ATTRIBUTE_GET_NUM_ATTRS_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((attr_id = H5Acreate2(container_group, ATTRIBUTE_GET_NUM_ATTRS_TEST_ATTRIBUTE_NAME, ATTRIBUTE_GET_NUM_ATTRS_TEST_ATTRIBUTE_TYPE,
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

    /* Now get the number of attributes from the group */
    if (H5Oget_info(container_group, &obj_info) < 0) {
        H5_FAILED();
        printf("    couldn't retrieve root group info\n");
        goto error;
    }

    if (obj_info.num_attrs < 1) {
        H5_FAILED();
        printf("    invalid number of attributes received\n");
        goto error;
    }

    if (H5Oget_info_by_name(file_id, "/" ATTRIBUTE_TEST_GROUP_NAME, &obj_info, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't retrieve group info\n");
        goto error;
    }

    if (obj_info.num_attrs < 1) {
        H5_FAILED();
        printf("    invalid number of attributes received\n");
        goto error;
    }

    if (H5Oget_info_by_idx(file_id, "/" ATTRIBUTE_TEST_GROUP_NAME, H5_INDEX_NAME, H5_ITER_INC, 0, &obj_info, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't retrieve root group info\n");
        goto error;
    }

    if (obj_info.num_attrs < 1) {
        H5_FAILED();
        printf("    invalid number of attributes received\n");
        goto error;
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(space_id);
        H5Aclose(attr_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

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
    hid_t      acpl_id1 = -1, acpl_id2 = -1;
    hid_t      space_id = -1;

    TESTING("attribute property list operations")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, ATTRIBUTE_PROPERTY_LIST_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    for (i = 0; i < ATTRIBUTE_PROPERTY_LIST_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t) (rand() % 64 + 1);

    if ((space_id = H5Screate_simple(ATTRIBUTE_PROPERTY_LIST_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((acpl_id1 = H5Pcreate(H5P_ATTRIBUTE_CREATE)) < 0) {
        H5_FAILED();
        printf("    couldn't create ACPL\n");
        goto error;
    }

    if (H5Pset_char_encoding(acpl_id1, encoding) < 0) {
        H5_FAILED();
        printf("    couldn't set ACPL property value\n");
        goto error;
    }

    if ((attr_id1 = H5Acreate2(group_id, ATTRIBUTE_PROPERTY_LIST_TEST_ATTRIBUTE_NAME1, ATTRIBUTE_PROPERTY_LIST_TEST_ATTRIBUTE_TYPE1,
            space_id, acpl_id1, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create attribute\n");
        goto error;
    }

    if ((attr_id2 = H5Acreate2(group_id, ATTRIBUTE_PROPERTY_LIST_TEST_ATTRIBUTE_NAME2, ATTRIBUTE_PROPERTY_LIST_TEST_ATTRIBUTE_TYPE2,
            space_id, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
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

    /* Try to retrieve copies of the two property lists, one which ahs the property set and one which does not */
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Pclose(acpl_id1);
        H5Pclose(acpl_id2);
        H5Sclose(space_id);
        H5Aclose(attr_id1);
        H5Aclose(attr_id2);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

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
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   dset_id = -1;
    hid_t   fspace_id = -1;

    TESTING("create dataset under root group")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    dims[0] = DATASET_CREATE_UNDER_ROOT_NY;
    dims[1] = DATASET_CREATE_UNDER_ROOT_NX;

    if ((fspace_id = H5Screate_simple(DATASET_CREATE_UNDER_ROOT_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    /* Create the Dataset under the root group of the file */
    if ((dset_id = H5Dcreate2(file_id, DATASET_CREATE_UNDER_ROOT_DSET_NAME, H5T_STD_U8LE, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_create_anonymous_dataset(void)
{
    hsize_t dims[DATASET_CREATE_ANONYMOUS_SPACE_RANK];
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   dset_id = -1;
    hid_t   fspace_id = -1;

    TESTING("create anonymous dataset")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    dims[0] = DATASET_CREATE_ANONYMOUS_NY;
    dims[1] = DATASET_CREATE_ANONYMOUS_NX;

    if ((fspace_id = H5Screate_simple(DATASET_CREATE_ANONYMOUS_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate_anon(container_group, H5T_STD_U8LE, fspace_id, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if (H5Olink(dset_id, container_group, DATASET_CREATE_ANONYMOUS_DATASET_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't link anonymous dataset into file structure\n");
        goto error;
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_create_dataset_under_existing_group(void)
{
    hsize_t dims[DATASET_CREATE_UNDER_EXISTING_SPACE_RANK];
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   dset_id = -1;
    hid_t   group_id = -1;
    hid_t   fspace_id = -1;

    TESTING("create dataset under existing group")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((group_id = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open group\n");
        goto error;
    }

    dims[0] = DATASET_CREATE_UNDER_EXISTING_NY;
    dims[1] = DATASET_CREATE_UNDER_EXISTING_NX;

    if ((fspace_id = H5Screate_simple(DATASET_CREATE_UNDER_EXISTING_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, DATASET_CREATE_UNDER_EXISTING_DSET_NAME, H5T_IEEE_F64BE, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_create_dataset_predefined_types(void)
{
    size_t i;
    hid_t  file_id = -1, fapl_id = -1;
    hid_t  container_group = -1, group_id = -1;
    hid_t  fspace_id = -1;
    hid_t  dset_id = -1;
    hid_t  predefined_type_test_table[] = {
            H5T_STD_U8LE,   H5T_STD_U8BE,   H5T_STD_I8LE,   H5T_STD_I8BE,
            H5T_STD_U16LE,  H5T_STD_U16BE,  H5T_STD_I16LE,  H5T_STD_I16BE,
            H5T_STD_U32LE,  H5T_STD_U32BE,  H5T_STD_I32LE,  H5T_STD_I32BE,
            H5T_STD_U64LE,  H5T_STD_U64BE,  H5T_STD_I64LE,  H5T_STD_I64BE,
            H5T_IEEE_F32LE, H5T_IEEE_F32BE, H5T_IEEE_F64LE, H5T_IEEE_F64BE
    };

    TESTING("dataset creation w/ predefined datatypes")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, DATASET_PREDEFINED_TYPE_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create sub-container group\n");
        goto error;
    }

    for (i = 0; i < ARRAY_LENGTH(predefined_type_test_table); i++) {
        hsize_t dims[DATASET_PREDEFINED_TYPE_TEST_SHAPE_RANK];
        char    name[100];

        dims[0] = (hsize_t) (rand() % 64 + 1);
        dims[1] = (hsize_t) (rand() % 64 + 1);

        if ((fspace_id = H5Screate_simple(DATASET_PREDEFINED_TYPE_TEST_SHAPE_RANK, dims, NULL)) < 0)
            TEST_ERROR

        sprintf(name, "%s%zu", DATASET_PREDEFINED_TYPE_TEST_BASE_NAME, i);

        if ((dset_id = H5Dcreate2(group_id, name, predefined_type_test_table[i], fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_create_dataset_string_types(void)
{
    hsize_t dims[DATASET_STRING_TYPE_TEST_SHAPE_RANK];
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id_fixed = -1, dset_id_variable = -1;
    hid_t   type_id_fixed = -1, type_id_variable = -1;
    hid_t   fspace_id = -1;

    TESTING("dataset creation w/ string types")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, DATASET_STRING_TYPE_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
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

    dims[0] = (hsize_t) (rand() % 64 + 1);
    dims[1] = (hsize_t) (rand() % 64 + 1);

    if ((fspace_id = H5Screate_simple(DATASET_STRING_TYPE_TEST_SHAPE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    TESTING("dataset creation w/ fixed-length string type")

    if ((dset_id_fixed = H5Dcreate2(group_id, DATASET_STRING_TYPE_TEST_DSET_NAME1, type_id_fixed, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create fixed-length string dataset\n");
        goto error;
    }

    TESTING("dataset creation w/ variable-length string type")

    if ((dset_id_variable = H5Dcreate2(group_id, DATASET_STRING_TYPE_TEST_DSET_NAME2, type_id_variable, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create variable-length string dataset\n");
        goto error;
    }

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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Tclose(type_id_fixed);
        H5Tclose(type_id_variable);
        H5Sclose(fspace_id);
        H5Dclose(dset_id_fixed);
        H5Dclose(dset_id_variable);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

/* XXX: More variety in datatypes used would be helpful */
static int
test_create_dataset_compound_types(void)
{
    hsize_t dims[DATASET_COMPOUND_TYPE_TEST_DSET_RANK] = { 10, 5 };
    size_t  i, j;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   compound_type = -1;
    hid_t   dset_id = -1;
    hid_t   fspace_id = -1;
    hid_t   type_pool[DATASET_COMPOUND_TYPE_TEST_MAX_SUBTYPES];
    hid_t   predefined_types[] = {
             H5T_STD_I8BE, H5T_STD_I8LE, H5T_STD_I16BE, H5T_STD_I16LE,
             H5T_STD_I32BE, H5T_STD_I32LE, H5T_STD_I64BE, H5T_STD_I64LE,
             H5T_STD_U8BE, H5T_STD_U8LE, H5T_STD_U16BE, H5T_STD_U16LE,
             H5T_STD_U32BE, H5T_STD_U32LE, H5T_STD_U64BE, H5T_STD_U64LE,
             H5T_IEEE_F32BE, H5T_IEEE_F32LE, H5T_IEEE_F64BE, H5T_IEEE_F64LE,
    };
    int     num_passes;

    TESTING("dataset creation w/ compound datatypes")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, DATASET_COMPOUND_TYPE_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    if ((fspace_id = H5Screate_simple(DATASET_COMPOUND_TYPE_TEST_DSET_RANK, dims, NULL)) < 0)
        TEST_ERROR

    num_passes = (rand() % DATASET_COMPOUND_TYPE_TEST_MAX_PASSES) + 1;

    for (i = 0; i < (size_t) num_passes; i++) {
        size_t num_subtypes;
        size_t last_member_offset = 0;
        char   dset_name[256];

        num_subtypes = (size_t) (rand() % DATASET_COMPOUND_TYPE_TEST_MAX_SUBTYPES) + 1;

        for (j = 0; j < num_subtypes; j++)
            type_pool[j] = -1;

        /* Allocate a Compound Datatype large enough to hold "num_subtypes" types, each of
         * which can be "MAX_SUBTYPE_SIZE" bytes at most
         */
        if ((compound_type = H5Tcreate(H5T_COMPOUND, num_subtypes * DATASET_COMPOUND_TYPE_TEST_MAX_SUBTYPE_SIZE + 64)) < 0) {
            H5_FAILED();
            printf("    couldn't create compound datatype\n");
            goto error;
        }

        /* Start adding subtypes to the compound type */
        for (j = 0; j < num_subtypes; j++) {
            char member_name[64];
            int  type_index = rand() % (int) (sizeof(predefined_types) / sizeof(predefined_types[0]));

            sprintf(member_name, "member%zu", j);

            if (H5Tinsert(compound_type, member_name, last_member_offset, predefined_types[type_index]) < 0)
                TEST_ERROR

            last_member_offset += H5Tget_size(predefined_types[type_index]);
        }

        if (H5Tpack(compound_type) < 0)
            TEST_ERROR

        snprintf(dset_name, sizeof(dset_name), "%s%zu", DATASET_COMPOUND_TYPE_TEST_DSET_NAME, i);

        if ((dset_id = H5Dcreate2(group_id, dset_name, compound_type, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        for (i = 0; i < DATASET_COMPOUND_TYPE_TEST_MAX_SUBTYPES; i++)
            H5Tclose(type_pool[i]);
        H5Tclose(compound_type);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_create_dataset_enum_types(void)
{
    hsize_t dims[DATASET_ENUM_TYPE_TEST_SHAPE_RANK];
    size_t  i;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id_native = -1, dset_id_non_native = -1;
    hid_t   fspace_id = -1;
    hid_t   enum_native = -1, enum_non_native = -1;
    const char *enum_type_test_table[] = {
            "RED",    "GREEN",  "BLUE",
            "BLACK",  "WHITE",  "PURPLE",
            "ORANGE", "YELLOW", "BROWN"
    };

    TESTING("dataset creation w/ enum types")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, DATASET_ENUM_TYPE_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    if ((enum_native = H5Tcreate(H5T_ENUM, sizeof(size_t))) < 0) {
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

    for (i = 0; i < 256; i++) {
        char val_name[15];

        sprintf(val_name, "%s%zu", DATASET_ENUM_TYPE_TEST_VAL_BASE_NAME, i);

        if (H5Tenum_insert(enum_non_native, val_name, &i) < 0)
            TEST_ERROR
    }

    dims[0] = (hsize_t) (rand() % 64 + 1);
    dims[1] = (hsize_t) (rand() % 64 + 1);

    if ((fspace_id = H5Screate_simple(DATASET_ENUM_TYPE_TEST_SHAPE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    TESTING("dataset creation w/ native integer enum type")

    if ((dset_id_native = H5Dcreate2(group_id, DATASET_ENUM_TYPE_TEST_DSET_NAME1, enum_native, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create native enum dataset\n");
        goto error;
    }

    TESTING("dataset creation w/ non-native integer enum type")

    if ((dset_id_non_native = H5Dcreate2(group_id, DATASET_ENUM_TYPE_TEST_DSET_NAME2, enum_non_native, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create non-native enum dataset\n");
        goto error;
    }

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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Tclose(enum_native);
        H5Tclose(enum_non_native);
        H5Sclose(fspace_id);
        H5Dclose(dset_id_native);
        H5Dclose(dset_id_non_native);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_create_dataset_array_types(void)
{
    hsize_t dset_dims[DATASET_ARRAY_TYPE_TEST_SHAPE_RANK];
    hsize_t array_dims1[DATASET_ARRAY_TYPE_TEST_RANK1];
    hsize_t array_dims2[DATASET_ARRAY_TYPE_TEST_RANK2];
    hsize_t array_dims3[DATASET_ARRAY_TYPE_TEST_RANK3];
    hsize_t array_dims4[DATASET_ARRAY_TYPE_TEST_RANK4];
    size_t  i;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1, group_id = -1;
    hid_t   dset_id1 = -1, dset_id2 = -1, dset_id3 = -1, dset_id4 = -1;
    hid_t   fspace_id = -1;
    hid_t   array_type_id1 = -1, array_type_id2 = -1, array_type_id3 = -1, array_type_id4 = -1;
    hid_t   nested_type_id = -1;
    hid_t   non_predefined_type_id = -1;

    TESTING("dataset creation w/ array types")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, DATASET_ARRAY_TYPE_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    /* Test creation of array with some predefined types */
    for (i = 0; i < DATASET_ARRAY_TYPE_TEST_RANK1; i++)
        array_dims1[i] = (hsize_t) (rand() % 64 + 1);

    if ((array_type_id1 = H5Tarray_create(H5T_STD_U16LE, DATASET_ARRAY_TYPE_TEST_RANK1, array_dims1)) < 0) {
        H5_FAILED();
        printf("    couldn't create predefined integer array type\n");
        goto error;
    }

    for (i = 0; i < DATASET_ARRAY_TYPE_TEST_RANK2; i++)
        array_dims2[i] = (hsize_t) (rand() % 64 + 1);

    if ((array_type_id2 = H5Tarray_create(H5T_IEEE_F32BE, DATASET_ARRAY_TYPE_TEST_RANK2, array_dims2)) < 0) {
        H5_FAILED();
        printf("    couldn't create predefined floating-point array type\n");
        goto error;
    }

    /* Test creation of array with non-predefined type */
    for (i = 0; i < DATASET_ARRAY_TYPE_TEST_RANK3; i++)
        array_dims3[i] = (hsize_t) (rand() % 64 + 1);

    if ((non_predefined_type_id = H5Tcreate(H5T_STRING, DATASET_ARRAY_TYPE_TEST_NON_PREDEFINED_SIZE)) < 0) {
        H5_FAILED();
        printf("    couldn't create non-predefined type\n");
        goto error;
    }

    if ((array_type_id3 = H5Tarray_create(non_predefined_type_id, DATASET_ARRAY_TYPE_TEST_RANK3, array_dims3)) < 0) {
        H5_FAILED();
        printf("    couldn't create non-predefined array type\n");
        goto error;
    }

#if 0
    /* Test nested arrays */
    for (i = 0; i < DATASET_ARRAY_TYPE_TEST_RANK4; i++)
        array_dims4[i] = (hsize_t) (rand() % 64 + 1);

    if ((nested_type_id = H5Tarray_create(non_predefined_type_id, DATASET_ARRAY_TYPE_TEST_RANK4, array_dims4)) < 0) {
        H5_FAILED();
        printf("    couldn't create nested array base type\n");
        goto error;
    }

    if ((array_type_id4 = H5Tarray_create(nested_type_id, DATASET_ARRAY_TYPE_TEST_RANK4, array_dims4)) < 0) {
        H5_FAILED();
        printf("    couldn't create nested array type\n");
        goto error;
    }
#endif

    /* XXX: Test arrays with nested compound w/ array */

    dset_dims[0] = (hsize_t) (rand() % 64 + 1);
    dset_dims[1] = (hsize_t) (rand() % 64 + 1);

    if ((fspace_id = H5Screate_simple(DATASET_ARRAY_TYPE_TEST_SHAPE_RANK, dset_dims, NULL)) < 0)
        TEST_ERROR

    TESTING("dataset creation w/ predefined integer array type")

    if ((dset_id1 = H5Dcreate2(group_id, DATASET_ARRAY_TYPE_TEST_DSET_NAME1, array_type_id1, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create predefined integer array type dataset\n");
        goto error;
    }

    TESTING("dataset creation w/ predefined floating-point array type")

    if ((dset_id2 = H5Dcreate2(group_id, DATASET_ARRAY_TYPE_TEST_DSET_NAME2, array_type_id2, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create predefined floating-point array type dataset\n");
        goto error;
    }

    TESTING("dataset creation w/ string array type")

    if ((dset_id3 = H5Dcreate2(group_id, DATASET_ARRAY_TYPE_TEST_DSET_NAME3, array_type_id3, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create non-predefined array type dataset\n");
        goto error;
    }

#if 0
    TESTING("dataset creation w/ nested array type")

    if ((dset_id4 = H5Dcreate2(group_id, DATASET_ARRAY_TYPE_TEST_DSET_NAME4, array_type_id4, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create nested array type dataset\n");
        goto error;
    }
#endif

    if (H5Dclose(dset_id1) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id2) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id3) < 0)
        TEST_ERROR
#if 0
    if (H5Dclose(dset_id4) < 0)
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

    if ((dset_id3 = H5Dopen2(group_id, DATASET_ARRAY_TYPE_TEST_DSET_NAME3, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    failed to open dataset\n");
        goto error;
    }

#if 0
    if ((dset_id4 = H5Dopen2(group_id, DATASET_ARRAY_TYPE_TEST_DSET_NAME4, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    failed to open dataset\n");
        goto error;
    }
#endif

    if (H5Tclose(array_type_id1) < 0)
        TEST_ERROR
    if (H5Tclose(array_type_id2) < 0)
        TEST_ERROR
    if (H5Tclose(array_type_id3) < 0)
        TEST_ERROR
#if 0
    if (H5Tclose(array_type_id4) < 0)
        TEST_ERROR
    if (H5Tclose(nested_type_id) < 0)
        TEST_ERROR
#endif
    if (H5Tclose(non_predefined_type_id) < 0)
        TEST_ERROR
    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id1) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id2) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id3) < 0)
        TEST_ERROR
#if 0
    if (H5Dclose(dset_id4) < 0)
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Tclose(array_type_id1);
        H5Tclose(array_type_id2);
        H5Tclose(array_type_id3);
#if 0
        H5Tclose(array_type_id4);
        H5Tclose(nested_type_id);
#endif
        H5Tclose(non_predefined_type_id);
        H5Sclose(fspace_id);
        H5Dclose(dset_id1);
        H5Dclose(dset_id2);
        H5Dclose(dset_id3);
#if 0
        H5Dclose(dset_id4);
#endif
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

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

    TESTING("dataset creation w/ random dimension sizes")

    if (RVinit() < 0)
        TEST_ERROR

    srand((unsigned) time(NULL));

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, DATASET_SHAPE_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    for (i = 0; i < DATASET_SHAPE_TEST_NUM_ITERATIONS; i++) {
        size_t j;
        char   name[100];
        int    ndims = rand() % DATASET_SHAPE_TEST_MAX_DIMS + 1;

        if (NULL == (dims = (hsize_t *) malloc((size_t) ndims * sizeof(*dims)))) {
            H5_FAILED();
            printf("    couldn't allocate space for dataspace dimensions\n");
            goto error;
        }

        for (j = 0; j < (size_t) ndims; j++)
            dims[j] = (hsize_t) (rand() % 64 + 1);

        if ((space_id = H5Screate_simple(ndims, dims, NULL)) < 0) {
            H5_FAILED();
            printf("    couldn't create dataspace\n");
            goto error;
        }

        sprintf(name, "%s%zu", DATASET_SHAPE_TEST_DSET_BASE_NAME, i + 1);

        if ((dset_id = H5Dcreate2(group_id, name, DATASET_SHAPE_TEST_DATATYPE, space_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
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

    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Gclose(container_group) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        if (dims) free(dims);
        H5Sclose(space_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

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
    hid_t   fspace_id = -1;

    TESTING("dataset creation properties")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, DATASET_CREATION_PROPERTIES_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group\n");
        goto error;
    }

    for (i = 0; i < DATASET_CREATION_PROPERTIES_TEST_SHAPE_RANK; i++)
        dims[i] = (hsize_t) (rand() % 64 + 1);

    if ((fspace_id = H5Screate_simple(DATASET_CREATION_PROPERTIES_TEST_SHAPE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    /* Test the alloc time property */
    {
        H5D_alloc_time_t alloc_times[] = {
                H5D_ALLOC_TIME_DEFAULT, H5D_ALLOC_TIME_EARLY,
                H5D_ALLOC_TIME_INCR, H5D_ALLOC_TIME_LATE
        };

        TESTING("dataset creation w/ different space-allocation times")

        if ((dcpl_id = H5Pcreate(H5P_DATASET_CREATE)) < 0)
            TEST_ERROR

        for (i = 0; i < ARRAY_LENGTH(alloc_times); i++) {
            char name[100];

            if (H5Pset_alloc_time(dcpl_id, alloc_times[i]) < 0)
                TEST_ERROR

            sprintf(name, "%s%zu", DATASET_CREATION_PROPERTIES_TEST_ALLOC_TIMES_BASE_NAME, i);

            if ((dset_id = H5Dcreate2(group_id, name, DATASET_CREATION_PROPERTIES_TEST_BASE_DATATYPE,
                    fspace_id, H5P_DEFAULT, dcpl_id, H5P_DEFAULT)) < 0) {
                H5_FAILED();
                printf("    couldn't create dataset\n");
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
        unsigned creation_orders[] = {
                H5P_CRT_ORDER_TRACKED,
                H5P_CRT_ORDER_TRACKED | H5P_CRT_ORDER_INDEXED
        };

        TESTING("dataset creation w/ different creation orders")

        if ((dcpl_id = H5Pcreate(H5P_DATASET_CREATE)) < 0)
            TEST_ERROR

        for (i = 0; i < ARRAY_LENGTH(creation_orders); i++) {
            char name[100];

            if (H5Pset_attr_creation_order(dcpl_id, creation_orders[i]) < 0)
                TEST_ERROR

            sprintf(name, "%s%zu", DATASET_CREATION_PROPERTIES_TEST_CRT_ORDER_BASE_NAME, i);

            if ((dset_id = H5Dcreate2(group_id, name, DATASET_CREATION_PROPERTIES_TEST_BASE_DATATYPE,
                    fspace_id, H5P_DEFAULT, dcpl_id, H5P_DEFAULT)) < 0) {
                H5_FAILED();
                printf("    couldn't create dataset\n");
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
        TESTING("dataset creation w/ different attribute phase change settings")

        if ((dcpl_id = H5Pcreate(H5P_DATASET_CREATE)) < 0)
            TEST_ERROR

        if (H5Pset_attr_phase_change(dcpl_id,
                DATASET_CREATION_PROPERTIES_TEST_MAX_COMPACT, DATASET_CREATION_PROPERTIES_TEST_MIN_DENSE) < 0)
            TEST_ERROR

        if ((dset_id = H5Dcreate2(group_id, DATASET_CREATION_PROPERTIES_TEST_PHASE_CHANGE_DSET_NAME,
                DATASET_CREATION_PROPERTIES_TEST_BASE_DATATYPE, fspace_id, H5P_DEFAULT, dcpl_id, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't create dataset\n");
            goto error;
        }

        if (H5Dclose(dset_id) < 0)
            TEST_ERROR
        if (H5Pclose(dcpl_id) < 0)
            TEST_ERROR
    }

    /* Test the fill time property */
    {
        H5D_fill_time_t fill_times[] = {
                H5D_FILL_TIME_IFSET, H5D_FILL_TIME_ALLOC,
                H5D_FILL_TIME_NEVER
        };

        TESTING("dataset creation w/ different fill times")

        if ((dcpl_id = H5Pcreate(H5P_DATASET_CREATE)) < 0)
            TEST_ERROR

        for (i = 0; i < ARRAY_LENGTH(fill_times); i++) {
            char name[100];

            if (H5Pset_fill_time(dcpl_id, fill_times[i]) < 0)
                TEST_ERROR

            sprintf(name, "%s%zu", DATASET_CREATION_PROPERTIES_TEST_FILL_TIMES_BASE_NAME, i);

            if ((dset_id = H5Dcreate2(group_id, name, DATASET_CREATION_PROPERTIES_TEST_BASE_DATATYPE,
                    fspace_id, H5P_DEFAULT, dcpl_id, H5P_DEFAULT)) < 0) {
                H5_FAILED();
                printf("    couldn't create dataset\n");
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

    /* TODO: Test filters */
    {

    }

    /* Test the storage layout property */
    {
        H5D_layout_t layouts[] = {
                H5D_COMPACT, H5D_CONTIGUOUS, H5D_CHUNKED
        };

        TESTING("dataset creation w/ different layouts")

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
                    chunk_dims[j] = (hsize_t) (rand() % (int) dims[j] + 1);

                if (H5Pset_chunk(dcpl_id , DATASET_CREATION_PROPERTIES_TEST_CHUNK_DIM_RANK, chunk_dims) < 0)
                    TEST_ERROR
            }

            sprintf(name, "%s%zu", DATASET_CREATION_PROPERTIES_TEST_LAYOUTS_BASE_NAME, i);

            if ((dset_id = H5Dcreate2(group_id, name, DATASET_CREATION_PROPERTIES_TEST_BASE_DATATYPE,
                    fspace_id, H5P_DEFAULT, dcpl_id, H5P_DEFAULT)) < 0) {
                H5_FAILED();
                printf("    couldn't create dataset\n");
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
        TESTING("dataset creation w/ different 'track object times' settings")

        if ((dcpl_id = H5Pcreate(H5P_DATASET_CREATE)) < 0)
            TEST_ERROR

        if (H5Pset_obj_track_times(dcpl_id, true) < 0)
            TEST_ERROR

        if ((dset_id = H5Dcreate2(group_id, DATASET_CREATION_PROPERTIES_TEST_TRACK_TIMES_YES_DSET_NAME,
                DATASET_CREATION_PROPERTIES_TEST_BASE_DATATYPE, fspace_id, H5P_DEFAULT, dcpl_id, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't create dataset\n");
            goto error;
        }

        if (H5Dclose(dset_id) < 0)
            TEST_ERROR

        if (H5Pset_obj_track_times(dcpl_id, false) < 0)
            TEST_ERROR

        if ((dset_id = H5Dcreate2(group_id, DATASET_CREATION_PROPERTIES_TEST_TRACK_TIMES_NO_DSET_NAME,
                DATASET_CREATION_PROPERTIES_TEST_BASE_DATATYPE, fspace_id, H5P_DEFAULT, dcpl_id, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't create dataset\n");
            goto error;
        }

        if (H5Dclose(dset_id) < 0)
            TEST_ERROR
        if (H5Pclose(dcpl_id) < 0)
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Pclose(dcpl_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_create_dataset_combinations(void)
{
    TESTING("create datasets with various combinations of shape, type, etc.")

    SKIPPED();

    return 0;

error:
    return 1;
}

/* Test creating a Dataset with a large Datatype to ensure
 * that the plugin grows the string buffer correctly without
 * corrupting memory. This will typically only be a problem
 * for Array and Compound Datatypes where Datatypes can be
 * nested inside to an arbitrary depth.
 */
static int
test_create_dataset_large_datatype(void)
{
    TESTING("create dataset with a large datatype")

    SKIPPED();

    return 0;

error:
    return 1;
}

static int
test_write_dataset_small_all(void)
{
    hssize_t  space_npoints;
    hsize_t   dims[DATASET_SMALL_WRITE_TEST_ALL_DSET_SPACE_RANK];
    size_t    i;
    hid_t     file_id = -1, fapl_id = -1;
    hid_t     container_group = -1;
    hid_t     dset_id = -1;
    hid_t     fspace_id = -1;
    void     *data = NULL;

    TESTING("small write to dataset w/ H5S_ALL")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    dims[0] = 4;
    dims[1] = 6;
    dims[2] = 8;

    if ((fspace_id = H5Screate_simple(DATASET_SMALL_WRITE_TEST_ALL_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_SMALL_WRITE_TEST_ALL_DSET_NAME, DATASET_SMALL_WRITE_TEST_ALL_DSET_DTYPE,
            fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    /* Close the dataset and dataspace to ensure that retrieval of file space ID is working */
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR;
    if (H5Sclose(fspace_id) < 0)
        TEST_ERROR

    if ((dset_id = H5Dopen2(file_id, "/" DATASET_TEST_GROUP_NAME "/" DATASET_SMALL_WRITE_TEST_ALL_DSET_NAME, H5P_DEFAULT)) < 0) {
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

    if (NULL == (data = malloc((hsize_t) space_npoints * sizeof(int))))
        TEST_ERROR

    for (i = 0; i < (hsize_t) space_npoints; i++)
        ((int *) data)[i] = (int) i;

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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        if (data) free(data);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_write_dataset_small_hyperslab(void)
{
    hsize_t  start[DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t  stride[DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t  count[DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t  block[DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t  dims[DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK];
    size_t   i, data_size;
    hid_t    file_id = -1, fapl_id = -1;
    hid_t    container_group = -1;
    hid_t    dset_id = -1;
    hid_t    mspace_id = -1, fspace_id = -1;
    void    *data = NULL;

    TESTING("small write to dataset w/ hyperslab")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    dims[0] = dims[1] = dims[2] = DATASET_SMALL_WRITE_TEST_HYPERSLAB_DIM_SIZE;

    if ((fspace_id = H5Screate_simple(DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR
    if ((mspace_id = H5Screate_simple(DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_SPACE_RANK - 1, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_NAME, DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_DTYPE,
            fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
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
        ((int *) data)[i] = (int) i;

    start[0] = start[1] = start[2] = 0;
    stride[0] = stride[1] = stride[2] = 1;
    count[0] = dims[0]; count[1] = dims[1]; count[2] = 1;
    block[0] = block[1] = block[2] = 1;

    if (H5Sselect_hyperslab(fspace_id, H5S_SELECT_SET, start, stride, count, block) < 0)
        TEST_ERROR

    if (H5Dwrite(dset_id, DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_DTYPE, mspace_id, fspace_id, H5P_DEFAULT, data) < 0) {
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        if (data) free(data);
        H5Sclose(mspace_id);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_write_dataset_small_point_selection(void)
{
    hsize_t  points[DATASET_SMALL_WRITE_TEST_POINT_SELECTION_NUM_POINTS * DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_SPACE_RANK];
    hsize_t  dims[DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_SPACE_RANK];
    size_t   i, data_size;
    hid_t    file_id = -1, fapl_id = -1;
    hid_t    container_group = -1;
    hid_t    dset_id = -1;
    hid_t    fspace_id = -1;
    void    *data = NULL;

    TESTING("small write to dataset w/ point selection")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    dims[0] = dims[1] = dims[2] = DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DIM_SIZE;

    if ((fspace_id = H5Screate_simple(DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_NAME, DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_DTYPE,
            fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    data_size = DATASET_SMALL_WRITE_TEST_POINT_SELECTION_NUM_POINTS * DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_DTYPESIZE;

    if (NULL == (data = malloc(data_size)))
        TEST_ERROR

    for (i = 0; i < data_size / DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_DTYPESIZE; i++)
        ((int *) data)[i] = (int) i;

    for (i = 0; i < DATASET_SMALL_WRITE_TEST_POINT_SELECTION_NUM_POINTS; i++) {
        size_t j;

        for (j = 0; j < DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_SPACE_RANK; j++)
            points[(i * DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_SPACE_RANK) + j] = j;
    }

    if (H5Sselect_elements(fspace_id, H5S_SELECT_SET, DATASET_SMALL_WRITE_TEST_POINT_SELECTION_NUM_POINTS, points) < 0) {
        H5_FAILED();
        printf("    couldn't select points\n");
        goto error;
    }

    if (H5Dwrite(dset_id, DATASET_SMALL_WRITE_TEST_POINT_SELECTION_DSET_DTYPE, H5S_ALL, fspace_id, H5P_DEFAULT, data) < 0) {
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        if (data) free(data);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_write_dataset_large_all(void)
{
    TESTING("write to large dataset w/ H5S_ALL")

    SKIPPED();

    return 0;

error:
    return 1;
}

static int
test_write_dataset_large_hyperslab(void)
{
    TESTING("write to large dataset w/ hyperslab selection")

    SKIPPED();

    return 0;

error:
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

static int
test_read_dataset_small_all(void)
{
    hsize_t  dims[DATASET_SMALL_READ_TEST_ALL_DSET_SPACE_RANK];
    size_t   i, data_size;
    hid_t    file_id = -1, fapl_id = -1;
    hid_t    container_group = -1;
    hid_t    dset_id = -1;
    hid_t    fspace_id = -1;
    void    *read_buf = NULL;

    TESTING("small read from dataset w/ H5S_ALL")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    dims[0] = dims[1] = dims[2] = 5;

    if ((fspace_id = H5Screate_simple(DATASET_SMALL_READ_TEST_ALL_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_SMALL_READ_TEST_ALL_DSET_NAME, DATASET_SMALL_READ_TEST_ALL_DSET_DTYPE,
            fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    for (i = 0, data_size = 1; i < DATASET_SMALL_READ_TEST_ALL_DSET_SPACE_RANK; i++)
        data_size *= dims[i];
    data_size *= DATASET_SMALL_READ_TEST_ALL_DSET_DTYPESIZE;

    if (NULL == (read_buf = malloc(data_size)))
        TEST_ERROR

    if (H5Dread(dset_id, DATASET_SMALL_READ_TEST_ALL_DSET_DTYPE, H5S_ALL, H5S_ALL, H5P_DEFAULT, read_buf) < 0) {
        H5_FAILED();
        printf("    couldn't read dataset\n");
        goto error;
    }

    for (i = 0; i < data_size / DATASET_SMALL_READ_TEST_ALL_DSET_DTYPESIZE; i++)
        printf("%zu: %d.\n", i, ((int *) read_buf)[i]);

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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        if (read_buf) free(read_buf);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_read_dataset_small_hyperslab(void)
{
    hsize_t  start[DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t  stride[DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t  count[DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t  block[DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_SPACE_RANK];
    hsize_t  dims[DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_SPACE_RANK];
    size_t   i, data_size;
    hid_t    file_id = -1, fapl_id = -1;
    hid_t    container_group = -1;
    hid_t    dset_id = -1;
    hid_t    mspace_id = -1, fspace_id = -1;
    void    *read_buf = NULL;

    TESTING("small read from dataset w/ hyperslab")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    dims[0] = dims[1] = dims[2] = DATASET_SMALL_READ_TEST_HYPERSLAB_DIM_SIZE;

    if ((fspace_id = H5Screate_simple(DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR
    if ((mspace_id = H5Screate_simple(DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_SPACE_RANK - 1, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_NAME, DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_DTYPE,
            fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    start[0] = start[1] = start[2] = 0;
    stride[0] = stride[1] = stride[2] = 1;
    count[0] = dims[0]; count[1] = dims[1]; count[2] = 1;
    block[0] = block[1] = block[2] = 1;

    if (H5Sselect_hyperslab(fspace_id, H5S_SELECT_SET, start, stride, count, block) < 0)
        TEST_ERROR

    for (i = 0, data_size = 1; i < DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_SPACE_RANK - 1; i++)
        data_size *= dims[i];
    data_size *= DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_DTYPESIZE;

    if (NULL == (read_buf = malloc(data_size)))
        TEST_ERROR

    if (H5Dread(dset_id, DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_DTYPE, mspace_id, fspace_id, H5P_DEFAULT, read_buf) < 0) {
        H5_FAILED();
        printf("    couldn't read from dataset\n");
        goto error;
    }

    for (i = 0; i < data_size / DATASET_SMALL_READ_TEST_HYPERSLAB_DSET_DTYPESIZE; i++)
        printf("%zu: %d\n", i, ((int *) read_buf)[i]);

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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        if (read_buf) free(read_buf);
        H5Sclose(mspace_id);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_read_dataset_small_point_selection(void)
{
    hsize_t  points[DATASET_SMALL_READ_TEST_POINT_SELECTION_NUM_POINTS * DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_SPACE_RANK];
    hsize_t  dims[DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_SPACE_RANK];
    hsize_t  mspace_dims[] = { DATASET_SMALL_READ_TEST_POINT_SELECTION_NUM_POINTS };
    size_t   i, data_size;
    hid_t    file_id = -1, fapl_id = -1;
    hid_t    container_group = -1;
    hid_t    dset_id = -1;
    hid_t    fspace_id = -1;
    hid_t    mspace_id = -1;
    void    *data = NULL;

    TESTING("small read from dataset w/ point selection")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    dims[0] = dims[1] = dims[2] = DATASET_SMALL_READ_TEST_POINT_SELECTION_DIM_SIZE;

    if ((fspace_id = H5Screate_simple(DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR
    if ((mspace_id = H5Screate_simple(1, mspace_dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_NAME, DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_DTYPE,
            fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    data_size = DATASET_SMALL_READ_TEST_POINT_SELECTION_NUM_POINTS * DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_DTYPESIZE;

    if (NULL == (data = malloc(data_size)))
        TEST_ERROR

    for (i = 0; i < DATASET_SMALL_READ_TEST_POINT_SELECTION_NUM_POINTS; i++) {
        size_t j;

        for (j = 0; j < DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_SPACE_RANK; j++)
            points[(i * DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_SPACE_RANK) + j] = i;
    }

    if (H5Sselect_elements(fspace_id, H5S_SELECT_SET, DATASET_SMALL_READ_TEST_POINT_SELECTION_NUM_POINTS, points) < 0) {
        H5_FAILED();
        printf("    couldn't select points\n");
        goto error;
    }

    if (H5Dread(dset_id, DATASET_SMALL_READ_TEST_POINT_SELECTION_DSET_DTYPE, mspace_id, fspace_id, H5P_DEFAULT, data) < 0) {
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        if (data) free(data);
        H5Sclose(mspace_id);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_read_dataset_large_all(void)
{
    TESTING("read from large dataset w/ H5S_ALL")

    SKIPPED();

    return 0;

error:
    return 1;
}

static int
test_read_dataset_large_hyperslab(void)
{
    TESTING("read from large dataset w/ hyperslab selection")

    SKIPPED();

    return 0;

error:
    return 1;
}

static int
test_read_dataset_large_point_selection(void)
{
    TESTING("read from large dataset w/ point selection")

    SKIPPED();

    return 0;

error:
    return 1;
}

static int
test_write_dataset_data_verification(void)
{
    hssize_t space_npoints;
    hsize_t  dims[DATASET_DATA_VERIFY_WRITE_TEST_DSET_SPACE_RANK];
    size_t   i, data_size;
    hid_t    file_id = -1, fapl_id = -1;
    hid_t    container_group = -1;
    hid_t    dset_id = -1;
    hid_t    fspace_id = -1;
    void    *data = NULL;

    TESTING("verification of dataset data after write then read")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    dims[0] = dims[1] = dims[2] = DATASET_DATA_VERIFY_WRITE_TEST_DIM_SIZE;

    if ((fspace_id = H5Screate_simple(DATASET_DATA_VERIFY_WRITE_TEST_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_DATA_VERIFY_WRITE_TEST_DSET_NAME, DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPE,
            fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    for (i = 0, data_size = 1; i < DATASET_DATA_VERIFY_WRITE_TEST_DSET_SPACE_RANK; i++)
        data_size *= dims[i];
    data_size *= DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE;

    if (NULL == (data = malloc(data_size)))
        TEST_ERROR

    for (i = 0; i < data_size / DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE; i++)
        ((int *) data)[i] = (int) i;

    if (H5Dwrite(dset_id, DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data) < 0) {
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

    if ((dset_id = H5Dopen2(file_id, "/" DATASET_TEST_GROUP_NAME "/" DATASET_DATA_VERIFY_WRITE_TEST_DSET_NAME, H5P_DEFAULT)) < 0) {
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

    if (NULL == (data = malloc((hsize_t) space_npoints * DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPESIZE)))
        TEST_ERROR

    if (H5Dread(dset_id, DATASET_DATA_VERIFY_WRITE_TEST_DSET_DTYPE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data) < 0) {
        H5_FAILED();
        printf("    couldn't read from dataset\n");
        goto error;
    }

    for (i = 0; i < (hsize_t) space_npoints; i++)
        if (((int *) data)[i] != (int) i) {
            H5_FAILED();
            printf("    data verification failed\n");
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        if (data) free(data);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_unused_dataset_API_calls(void)
{
    TESTING("unused dataset API calls")

    SKIPPED();

    return 0;

error:
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
    hid_t       space_id = -1;
    char       *tmp_prefix = NULL;

    TESTING("dataset property list operations")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, DATASET_PROPERTY_LIST_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    for (i = 0; i < DATASET_PROPERTY_LIST_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t) (rand() % 64 + 1);
    for (i = 0; i < DATASET_PROPERTY_LIST_TEST_SPACE_RANK; i++)
        chunk_dims[i] = (hsize_t) (rand() % (int) dims[i] + 1);

    if ((space_id = H5Screate_simple(DATASET_PROPERTY_LIST_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dcpl_id1 = H5Pcreate(H5P_DATASET_CREATE)) < 0) {
        H5_FAILED();
        printf("    couldn't create DCPL\n");
        goto error;
    }

    if (H5Pset_chunk(dcpl_id1, DATASET_PROPERTY_LIST_TEST_SPACE_RANK, chunk_dims) < 0) {
        H5_FAILED();
        printf("    couldn't set DCPL property\n");
        goto error;
    }

    if ((dset_id1 = H5Dcreate2(group_id, DATASET_PROPERTY_LIST_TEST_DSET_NAME1, DATASET_PROPERTY_LIST_TEST_DSET_TYPE1,
            space_id, H5P_DEFAULT, dcpl_id1, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if ((dset_id2 = H5Dcreate2(group_id, DATASET_PROPERTY_LIST_TEST_DSET_NAME2, DATASET_PROPERTY_LIST_TEST_DSET_TYPE2,
            space_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if (H5Pclose(dcpl_id1) < 0)
        TEST_ERROR

    /* Try to receive copies of the two property lists, one which has the property set and one which does not */
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

        for (i = 0; i < DATASET_PROPERTY_LIST_TEST_SPACE_RANK; i++)
            if (tmp_chunk_dims[i] != chunk_dims[i]) {
                H5_FAILED();
                printf("    DCPL property values were incorrect\n");
                goto error;
            }

        H5E_BEGIN_TRY {
            if (H5Pget_chunk(dcpl_id2, DATASET_PROPERTY_LIST_TEST_SPACE_RANK, tmp_chunk_dims) >= 0) {
                H5_FAILED();
                printf("    property list 2 shouldn't have had chunk dimensionality set (not a chunked layout)\n");
                goto error;
            }
        } H5E_END_TRY;
    }

    if ((dapl_id1 = H5Pcreate(H5P_DATASET_ACCESS)) < 0) {
        H5_FAILED();
        printf("    couldn't create DAPL\n");
        goto error;
    }

    if (H5Pset_efile_prefix(dapl_id1, path_prefix) < 0) {
        H5_FAILED();
        printf("    couldn't set DAPL property\n");
        goto error;
    }

    if ((dset_id3 = H5Dcreate2(group_id, DATASET_PROPERTY_LIST_TEST_DSET_NAME3, DATASET_PROPERTY_LIST_TEST_DSET_TYPE3,
            space_id, H5P_DEFAULT, H5P_DEFAULT, dapl_id1)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if ((dset_id4 = H5Dcreate2(group_id, DATASET_PROPERTY_LIST_TEST_DSET_NAME4, DATASET_PROPERTY_LIST_TEST_DSET_TYPE4,
            space_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if (H5Pclose(dapl_id1) < 0)
        TEST_ERROR

    /* Try to receive copies of the two property lists, one which has the property set and one which does not */
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

        if ((buf_size = H5Pget_efile_prefix(dapl_id1, NULL, 0)) < 0) {
            H5_FAILED();
            printf("    couldn't retrieve size for property value buffer\n");
            goto error;
        }

        if (NULL == (tmp_prefix = (char *) calloc(1, (size_t) buf_size + 1)))
            TEST_ERROR

        if (H5Pget_efile_prefix(dapl_id1, tmp_prefix, (size_t) buf_size + 1) < 0) {
            H5_FAILED();
            printf("    couldn't retrieve property list value\n");
            goto error;
        }

        if (strcmp(tmp_prefix, path_prefix)) {
            H5_FAILED();
            printf("    DAPL values were incorrect!\n");
            goto error;
        }

        memset(tmp_prefix, 0, (size_t) buf_size + 1);

        if (H5Pget_efile_prefix(dapl_id2, tmp_prefix, (size_t) buf_size) < 0) {
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        free(tmp_prefix);
        H5Pclose(dcpl_id1);
        H5Pclose(dcpl_id2);
        H5Pclose(dapl_id1);
        H5Pclose(dapl_id2);
        H5Sclose(space_id);
        H5Dclose(dset_id1);
        H5Dclose(dset_id2);
        H5Dclose(dset_id3);
        H5Dclose(dset_id4);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

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
    hid_t  file_id = -1, fapl = -1;
    hid_t  container_group = -1;

    TESTING("create hard link")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if (H5Lcreate_hard(file_id, "/" DATASET_TEST_GROUP_NAME "/" DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_NAME,
            container_group, HARD_LINK_TEST_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create hard link\n");
        goto error;
    }

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

    /* Delete the link */
    if (H5Ldelete(container_group, HARD_LINK_TEST_LINK_NAME, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't delete link\n");
        goto error;
    }

    /* Verify the link has been deleted */
    if ((link_exists = H5Lexists(container_group, HARD_LINK_TEST_LINK_NAME, H5P_DEFAULT)) < 0) {
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
    if (H5Pclose(fapl) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Gclose(container_group);
        H5Pclose(fapl);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

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
    hid_t   dset_id = -1;
    hid_t   space_id = -1;

    TESTING("create hard link with H5L_SAME_LOC")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, H5L_SAME_LOC_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group\n");
        goto error;
    }

    memset(dims, 0, sizeof(dims));
    for (i = 0; i < H5L_SAME_LOC_TEST_DSET_SPACE_RANK; i++)
        dims[i] = (hsize_t) (rand() % 64 + 1);

    if ((space_id = H5Screate_simple(H5L_SAME_LOC_TEST_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, H5L_SAME_LOC_TEST_DSET_NAME, H5L_SAME_LOC_TEST_DSET_DTYPE,
            space_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

#if 0 /* Library functionality for this part of the test is broken */
    if (H5Lcreate_hard(H5L_SAME_LOC, H5L_SAME_LOC_TEST_DSET_NAME, group_id, H5L_SAME_LOC_TEST_LINK_NAME1, H5P_DEFAULT, H5P_DEFAULT) < 0) {
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
#endif

    if (H5Lcreate_hard(group_id, H5L_SAME_LOC_TEST_DSET_NAME, H5L_SAME_LOC, H5L_SAME_LOC_TEST_LINK_NAME2, H5P_DEFAULT, H5P_DEFAULT) < 0) {
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(space_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_open_object_by_hard_link(void)
{
    TESTING("open object in file by using a hard link")

    SKIPPED();

    return 0;

error:
    return 1;
}

static int
test_create_soft_link_existing_relative(void)
{
    TESTING("create soft link to existing object by relative path")

    SKIPPED();

    return 0;

error:
    return 1;
}

static int
test_create_soft_link_existing_absolute(void)
{
    htri_t link_exists;
    hid_t  file_id = -1, fapl = -1;
    hid_t  container_group = -1;

    TESTING("create soft link to existing object by absolute path")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if (H5Lcreate_soft("/" DATASET_TEST_GROUP_NAME "/" DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_NAME, container_group,
            SOFT_LINK_TEST_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create soft link\n");
        goto error;
    }

    /* Verify the link has been created */
    if ((link_exists = H5Lexists(file_id, "/" LINK_TEST_GROUP_NAME "/" SOFT_LINK_TEST_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link did not exist\n");
        goto error;
    }

    /* Delete the link */
    if (H5Ldelete(container_group, SOFT_LINK_TEST_LINK_PATH, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't delete link\n");
        goto error;
    }

    /* Verify the link has been deleted */
    if ((link_exists = H5Lexists(container_group, SOFT_LINK_TEST_LINK_NAME, H5P_DEFAULT)) < 0) {
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
    if (H5Pclose(fapl) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Gclose(container_group);
        H5Pclose(fapl);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_create_soft_link_dangling_relative(void)
{
    TESTING("create dangling soft link to object by relative path")

    SKIPPED();

    return 0;

error:
    return 1;
}

static int
test_create_soft_link_dangling_absolute(void)
{
    TESTING("create dangling soft link to object by absolute path")

    SKIPPED();

    return 0;

error:
    return 1;
}

static int
test_open_object_by_soft_link(void)
{
    TESTING("open object in file by using a soft link")

    SKIPPED();

    return 0;

error:
    return 1;
}

static int
test_create_external_link(void)
{
    htri_t link_exists;
    hid_t  file_id = -1, fapl = -1;
    hid_t  container_group = -1;

    TESTING("create external link to existing object")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if (H5Lcreate_external(EXTERNAL_LINK_TEST_FILE_NAME, "/" DATASET_SMALL_WRITE_TEST_HYPERSLAB_DSET_NAME,
            container_group, EXTERNAL_LINK_TEST_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create external link\n");
        goto error;
    }

    /* Verify the link has been created */
    if ((link_exists = H5Lexists(container_group, EXTERNAL_LINK_TEST_LINK_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    link did not exist\n");
        goto error;
    }

    /* Delete the link */
    if (H5Ldelete(container_group, EXTERNAL_LINK_TEST_LINK_NAME, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't delete link\n");
        goto error;
    }

    /* Verify the link has been deleted */
    if ((link_exists = H5Lexists(container_group, EXTERNAL_LINK_TEST_LINK_NAME, H5P_DEFAULT)) < 0) {
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
    if (H5Pclose(fapl) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Gclose(container_group);
        H5Pclose(fapl);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_open_object_by_external_link(void)
{
    TESTING("open object in file by using an external link")

    SKIPPED();

    return 0;

error:
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
    hid_t   dset_id = -1;
    hid_t   space_id = -1;

    TESTING("copy a link")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, COPY_LINK_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group\n");
        goto error;
    }

    for (i = 0; i < COPY_LINK_TEST_DSET_SPACE_RANK; i++)
        dims[i] = (hsize_t) (rand() % 64 + 1);

    if ((space_id = H5Screate_simple(COPY_LINK_TEST_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, COPY_LINK_TEST_DSET_NAME, COPY_LINK_TEST_DSET_TYPE,
            space_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }


    /* Try to copy a hard link */
    if (H5Lcreate_hard(group_id, COPY_LINK_TEST_DSET_NAME, group_id, COPY_LINK_TEST_HARD_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
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

    /* Copy the link */
    if (H5Lcopy(group_id, COPY_LINK_TEST_HARD_LINK_NAME, group_id, COPY_LINK_TEST_HARD_LINK_COPY_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't copy hard link\n");
        goto error;
    }

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


    /* Try to copy a soft link */
    if (H5Lcreate_soft(COPY_LINK_TEST_SOFT_LINK_TARGET_PATH, group_id, COPY_LINK_TEST_SOFT_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
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

    /* Copy the link */
    if (H5Lcopy(group_id, COPY_LINK_TEST_SOFT_LINK_NAME, group_id, COPY_LINK_TEST_SOFT_LINK_COPY_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't copy soft link\n");
        goto error;
    }

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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(space_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

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
    hid_t   dset_id = -1;
    hid_t   space_id = -1;

    TESTING("move a link")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, MOVE_LINK_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group\n");
        goto error;
    }

    for (i = 0; i < MOVE_LINK_TEST_DSET_SPACE_RANK; i++)
        dims[i] = (hsize_t) (rand() % 64 + 1);

    if ((space_id = H5Screate_simple(MOVE_LINK_TEST_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, MOVE_LINK_TEST_DSET_NAME, MOVE_LINK_TEST_DSET_TYPE,
            space_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }


    /* Try to move a hard link */
    if (H5Lcreate_hard(group_id, MOVE_LINK_TEST_DSET_NAME, file_id, MOVE_LINK_TEST_HARD_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
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

    /* Move the link */
    if (H5Lmove(file_id, MOVE_LINK_TEST_HARD_LINK_NAME, group_id, MOVE_LINK_TEST_HARD_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't move hard link\n");
        goto error;
    }

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


    /* Try to move a soft link */
    if (H5Lcreate_soft(MOVE_LINK_TEST_SOFT_LINK_TARGET_PATH, file_id, MOVE_LINK_TEST_SOFT_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
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

    /* Move the link */
    if (H5Lmove(file_id, MOVE_LINK_TEST_SOFT_LINK_NAME, group_id, MOVE_LINK_TEST_SOFT_LINK_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't move soft link\n");
        goto error;
    }

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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(space_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_unused_link_API_calls(void)
{
    TESTING("unused link API calls")

    SKIPPED();

    return 0;

error:
    return 1;
}


/*****************************************************
 *                                                   *
 *          Plugin Committed Datatype tests          *
 *                                                   *
 *****************************************************/

/* XXX: More variety in datatypes used would be helpful */
static int
test_create_committed_datatype(void)
{
    hid_t file_id = -1, fapl_id = -1;
    hid_t container_group = -1;
    hid_t type_id = -1;

    TESTING("creation of committed datatype")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATATYPE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((type_id = H5Tcreate(H5T_STRING, DATATYPE_CREATE_TEST_STRING_LENGTH)) < 0) {
        H5_FAILED();
        printf("    couldn't create fixed-length string type\n");
        goto error;
    }

    if (H5Tcommit2(container_group, DATATYPE_CREATE_TEST_TYPE_NAME, type_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT) < 0) {
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Tclose(type_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_create_anonymous_committed_datatype(void)
{
    hid_t file_id = -1, fapl_id = -1;
    hid_t container_group = -1;
    hid_t type_id = -1;

    TESTING("creation of anonymous committed datatype")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATATYPE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((type_id = H5Tcreate(H5T_STRING, DATATYPE_CREATE_ANONYMOUS_TYPE_LENGTH)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if (H5Tcommit_anon(container_group, type_id, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't commit anonymous datatype\n");
        goto error;
    }

    if (H5Olink(type_id, container_group, DATATYPE_CREATE_ANONYMOUS_TYPE_NAME, H5P_DEFAULT, H5P_DEFAULT) < 0) {
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Tclose(type_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_create_committed_datatype_combinations(void)
{
    TESTING("create committed datatype with various combinations of types")

    SKIPPED();

    return 0;

error:
    return 1;
}

static int
test_create_dataset_with_committed_type(void)
{
    hsize_t dims[DATASET_CREATE_WITH_DATATYPE_TEST_DATASET_DIMS];
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   dset_id = -1;
    hid_t   type_id = -1;
    hid_t   fspace_id = -1;

    TESTING("dataset creation w/ committed datatype")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATATYPE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((type_id = H5Tcreate(H5T_STRING, H5T_VARIABLE)) < 0) {
        H5_FAILED();
        printf("    couldn't create variable-length string type\n");
        goto error;
    }

    if (H5Tcommit2(container_group, DATASET_CREATE_WITH_DATATYPE_TEST_TYPE_NAME, type_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT) < 0) {
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

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
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

    dims[0] = (hsize_t) (rand() % 64 + 1);
    dims[1] = (hsize_t) (rand() % 64 + 1);

    if ((fspace_id = H5Screate_simple(DATATYPE_CREATE_TEST_DATASET_DIMS, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, DATASET_CREATE_WITH_DATATYPE_TEST_DSET_NAME, type_id, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Tclose(type_id);
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

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
    hid_t   attr_id = -1;
    hid_t   type_id = -1;
    hid_t   space_id = -1;

    TESTING("attribute creation w/ committed datatype")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATATYPE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((type_id = H5Tcreate(ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_DTYPE, ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_DTYPE_SIZE)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if (H5Tcommit2(container_group, ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_DTYPE_NAME, type_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't commit datatype\n");
        goto error;
    }

    if (H5Tclose(type_id) < 0)
        TEST_ERROR

    if ((type_id = H5Topen2(container_group, ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_DTYPE_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open committed datatype\n");
        goto error;
    }

    for (i = 0; i < ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t) (rand() % 64 + 1);

    if ((space_id = H5Screate_simple(ATTRIBUTE_CREATE_WITH_DATATYPE_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Tclose(type_id);
        H5Sclose(space_id);
        H5Aclose(attr_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_delete_committed_type(void)
{
    htri_t type_exists;
    hid_t  file_id = -1, fapl_id = -1;
    hid_t  container_group = -1;
    hid_t  type_id = -1;

    TESTING("delete committed datatype")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATATYPE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((type_id = H5Tcreate(DATATYPE_DELETE_TEST_DTYPE, DATATYPE_DELETE_TEST_SIZE)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if (H5Tcommit2(container_group, DATATYPE_DELETE_TEST_DTYPE_NAME, type_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT) < 0) {
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
        printf("    datatype didn't exists\n");
        goto error;
    }

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
        printf("    link existed\n");
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Tclose(type_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_get_existing_type_info(void)
{
    TESTING("get existing committed datatype info")

    SKIPPED();

    return 0;

error:
    return 1;
}

static int
test_unused_datatype_API_calls(void)
{
    TESTING("unused datatype API calls")

    SKIPPED();

    return 0;

error:
    return 1;
}

static int
test_datatype_property_lists(void)
{
    hid_t file_id = -1, fapl_id = -1;
    hid_t container_group = -1, group_id = -1;
    hid_t type_id1 = -1, type_id2 = -1;
    hid_t tcpl_id1 = -1, tcpl_id2 = -1;

    TESTING("datatype property list operations")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, DATATYPE_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, DATATYPE_PROPERTY_LIST_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    if ((type_id1 = H5Tcreate(DATATYPE_PROPERTY_LIST_TEST_DATATYPE1, DATATYPE_PROPERTY_LIST_TEST_DATATYPE_SIZE1)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if ((type_id2 = H5Tcreate(DATATYPE_PROPERTY_LIST_TEST_DATATYPE2, DATATYPE_PROPERTY_LIST_TEST_DATATYPE_SIZE2)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if ((tcpl_id1 = H5Pcreate(H5P_DATATYPE_CREATE)) < 0) {
        H5_FAILED();
        printf("    couldn't create TCPL\n");
        goto error;
    }

    /* XXX: Currently no TCPL routines are defined */

    if (H5Tcommit2(group_id, DATATYPE_PROPERTY_LIST_TEST_DATATYPE_NAME1, type_id1, H5P_DEFAULT, tcpl_id1, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't commit datatype\n");
        goto error;
    }

    if (H5Tcommit2(group_id, DATATYPE_PROPERTY_LIST_TEST_DATATYPE_NAME2, type_id2, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT) < 0 ) {
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

    /* Now close the property lists and datatypes and see if we can still retieve copies of
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Pclose(tcpl_id1);
        H5Pclose(tcpl_id2);
        H5Tclose(type_id1);
        H5Tclose(type_id2);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
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
    hid_t   fspace_id = -1;
    hid_t   dset_id = -1;

    TESTING("open dataset generically w/ H5Oopen()")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
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
        dims[i] = (hsize_t) (rand() % 64 + 1);

    if ((fspace_id = H5Screate_simple(GENERIC_DATASET_OPEN_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, GENERIC_DATASET_OPEN_TEST_DSET_NAME, GENERIC_DATASET_OPEN_TEST_DSET_DTYPE,
            fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if (H5Dclose(dset_id) < 0)
        TEST_ERROR

    if ((dset_id = H5Oopen(file_id, "/" OBJECT_TEST_GROUP_NAME "/" GENERIC_DATASET_OPEN_TEST_DSET_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset with H5Oopen()\n");
        goto error;
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_open_group_generically(void)
{
    hid_t file_id = -1, fapl_id = -1;
    hid_t container_group = -1;
    hid_t group_id = -1;

    TESTING("open group generically w/ H5Oopen()")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, OBJECT_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, GENERIC_GROUP_OPEN_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group\n");
        goto error;
    }

    if (H5Gclose(group_id) < 0)
        TEST_ERROR

    if ((group_id = H5Oopen(file_id, "/" OBJECT_TEST_GROUP_NAME "/" GENERIC_GROUP_OPEN_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open group with H5Oopen()\n");
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_open_datatype_generically(void)
{
    hid_t file_id = -1, fapl_id = -1;
    hid_t container_group = -1;
    hid_t type_id = -1;

    TESTING("open datatype generically w/ H5Oopen()")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, OBJECT_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((type_id = H5Tcreate(H5T_STRING, GENERIC_DATATYPE_OPEN_TEST_TYPE_SIZE)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if (H5Tcommit2(container_group, GENERIC_DATATYPE_OPEN_TEST_TYPE_NAME, type_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't commit datatype\n");
        goto error;
    }

    if (H5Tclose(type_id) < 0)
        TEST_ERROR

    if ((type_id = H5Oopen(file_id, "/" OBJECT_TEST_GROUP_NAME "/" GENERIC_DATATYPE_OPEN_TEST_TYPE_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open datatype generically w/ H5Oopen()\n");
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Tclose(type_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_h5o_close(void)
{
    TESTING("H5Oclose")

    SKIPPED();

    return 0;

error:
    return 1;
}

static int
test_create_obj_ref(void)
{
    RV_obj_ref_t ref;
    hid_t        file_id = -1, fapl_id = -1;

    TESTING("create an object reference")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if (H5Rcreate((void *) &ref, file_id, "/", H5R_OBJECT, -1) < 0) {
        H5_FAILED();
        printf("    couldn't create obj. ref\n");
        goto error;
    }

    if (H5R_OBJECT != ref.ref_type) TEST_ERROR
    if (H5I_GROUP != ref.ref_obj_type) TEST_ERROR
    if (strcmp(RVget_uri(file_id), ref.ref_obj_URI)) TEST_ERROR

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_write_dataset_w_obj_refs(void)
{
    RV_obj_ref_t *ref_array = NULL;
    hsize_t       dims[OBJ_REF_DATASET_WRITE_TEST_SPACE_RANK];
    size_t        i, ref_array_size = 0;
    hid_t         file_id = -1, fapl_id = -1;
    hid_t         container_group = -1, group_id = -1;
    hid_t         dset_id = -1, ref_dset_id = -1;
    hid_t         ref_dtype_id = -1;
    hid_t         space_id = -1;

    TESTING("write to a dataset w/ object reference type")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, OBJECT_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, OBJ_REF_DATASET_WRITE_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    for (i = 0; i < OBJ_REF_DATASET_WRITE_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t) (rand() % 8 + 1);

    if ((space_id = H5Screate_simple(OBJ_REF_DATASET_WRITE_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    /* Create the dataset and datatype which will be referenced */
    if ((ref_dset_id = H5Dcreate2(group_id, OBJ_REF_DATASET_WRITE_TEST_REF_DSET_NAME, OBJ_REF_DATASET_WRITE_TEST_REF_DSET_DTYPE,
            space_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset for referencing\n");
        goto error;
    }

    if ((ref_dtype_id = H5Tcreate(OBJ_REF_DATASET_WRITE_TEST_REF_TYPE_DTYPE, OBJ_REF_DATASET_WRITE_TEST_REF_TYPE_SIZE)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if (H5Tcommit2(group_id, OBJ_REF_DATASET_WRITE_TEST_REF_TYPE_NAME, ref_dtype_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype for referencing\n");
        goto error;
    }

    {
        /* XXX: Temporary workaround for datatypes */
        if (H5Tclose(ref_dtype_id) < 0)
            TEST_ERROR

        if ((ref_dtype_id = H5Topen2(group_id, OBJ_REF_DATASET_WRITE_TEST_REF_TYPE_NAME, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't open datatype for referencing\n");
            goto error;
        }
    }


    if ((dset_id = H5Dcreate2(group_id, OBJ_REF_DATASET_WRITE_TEST_DSET_NAME, H5T_STD_REF_OBJ,
            space_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    for (i = 0, ref_array_size = 1; i < OBJ_REF_DATASET_WRITE_TEST_SPACE_RANK; i++)
        ref_array_size *= dims[i];

    if (NULL == (ref_array = (RV_obj_ref_t *) malloc(ref_array_size * sizeof(*ref_array))))
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

                if (NULL == (URI = RVget_uri(file_id)))
                    TEST_ERROR

                break;

            case 1:
                if (H5Rcreate(&ref_array[i], group_id, OBJ_REF_DATASET_WRITE_TEST_REF_TYPE_NAME, H5R_OBJECT, -1) < 0) {
                    H5_FAILED();
                    printf("    couldn't create reference\n");
                    goto error;
                }

                if (NULL == (URI = RVget_uri(ref_dtype_id)))
                    TEST_ERROR

                break;

            case 2:
                if (H5Rcreate(&ref_array[i], group_id, OBJ_REF_DATASET_WRITE_TEST_REF_DSET_NAME, H5R_OBJECT, -1) < 0) {
                    H5_FAILED();
                    printf("    couldn't create reference\n");
                    goto error;
                }

                if (NULL == (URI = RVget_uri(ref_dset_id)))
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

    if (ref_array) {
        free(ref_array);
        ref_array = NULL;
    }

    if (H5Sclose(space_id) < 0)
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        if (ref_array) free(ref_array);
        H5Sclose(space_id);
        H5Tclose(ref_dtype_id);
        H5Dclose(ref_dset_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_read_dataset_w_obj_refs(void)
{
    RV_obj_ref_t *ref_array = NULL;
    hsize_t       dims[OBJ_REF_DATASET_READ_TEST_SPACE_RANK];
    size_t        i, ref_array_size = 0;
    hid_t         file_id = -1, fapl_id = -1;
    hid_t         container_group = -1, group_id = -1;
    hid_t         dset_id = -1, ref_dset_id = -1;
    hid_t         ref_dtype_id = -1;
    hid_t         space_id = -1;

    TESTING("read from a dataset w/ object reference type")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, OBJECT_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, OBJ_REF_DATASET_READ_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group\n");
        goto error;
    }

    for (i = 0; i < OBJ_REF_DATASET_READ_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t) (rand() % 8 + 1);

    if ((space_id = H5Screate_simple(OBJ_REF_DATASET_READ_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    /* Create the dataset and datatype which will be referenced */
    if ((ref_dset_id = H5Dcreate2(group_id, OBJ_REF_DATASET_READ_TEST_REF_DSET_NAME, OBJ_REF_DATASET_READ_TEST_REF_DSET_DTYPE,
            space_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset for referencing\n");
        goto error;
    }

    if ((ref_dtype_id = H5Tcreate(OBJ_REF_DATASET_READ_TEST_REF_TYPE_DTYPE, OBJ_REF_DATASET_READ_TEST_REF_TYPE_SIZE)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if (H5Tcommit2(group_id, OBJ_REF_DATASET_READ_TEST_REF_TYPE_NAME, ref_dtype_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype for referencing\n");
        goto error;
    }

    {
        /* XXX: Temporary workaround for datatypes */
        if (H5Tclose(ref_dtype_id) < 0)
            TEST_ERROR

        if ((ref_dtype_id = H5Topen2(group_id, OBJ_REF_DATASET_READ_TEST_REF_TYPE_NAME, H5P_DEFAULT)) < 0) {
            H5_FAILED();
            printf("    couldn't open datatype for referencing\n");
            goto error;
        }
    }


    if ((dset_id = H5Dcreate2(group_id, OBJ_REF_DATASET_READ_TEST_DSET_NAME, H5T_STD_REF_OBJ,
            space_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    for (i = 0, ref_array_size = 1; i < OBJ_REF_DATASET_READ_TEST_SPACE_RANK; i++)
        ref_array_size *= dims[i];

    if (NULL == (ref_array = (RV_obj_ref_t *) malloc(ref_array_size * sizeof(*ref_array))))
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

                if (NULL == (URI = RVget_uri(file_id)))
                    TEST_ERROR

                    break;

            case 1:
                if (H5Rcreate(&ref_array[i], group_id, OBJ_REF_DATASET_READ_TEST_REF_TYPE_NAME, H5R_OBJECT, -1) < 0) {
                    H5_FAILED();
                    printf("    couldn't create reference\n");
                    goto error;
                }

                if (NULL == (URI = RVget_uri(ref_dtype_id)))
                    TEST_ERROR

                    break;

            case 2:
                if (H5Rcreate(&ref_array[i], group_id, OBJ_REF_DATASET_READ_TEST_REF_DSET_NAME, H5R_OBJECT, -1) < 0) {
                    H5_FAILED();
                    printf("    couldn't create reference\n");
                    goto error;
                }

                if (NULL == (URI = RVget_uri(ref_dset_id)))
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
        if (   H5I_FILE != ref_array[i].ref_obj_type
            && H5I_GROUP != ref_array[i].ref_obj_type
            && H5I_DATATYPE != ref_array[i].ref_obj_type
            && H5I_DATASET != ref_array[i].ref_obj_type
           ) {
            H5_FAILED();
            printf("    ref object type mismatch\n");
            goto error;
        }

        /* Check the URI of the referenced object according to
         * the HSDS spec where each URI is prefixed as
         * 'X-', where X is a character denoting the type
         * of object */
        if (   (ref_array[i].ref_obj_URI[1] != '-')
            || (ref_array[i].ref_obj_URI[0] != 'g'
            &&  ref_array[i].ref_obj_URI[0] != 't'
            &&  ref_array[i].ref_obj_URI[0] != 'd')
           ) {
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        if (ref_array) free(ref_array);
        H5Sclose(space_id);
        H5Tclose(ref_dtype_id);
        H5Dclose(ref_dset_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_write_dataset_w_obj_refs_empty_data(void)
{
    RV_obj_ref_t *ref_array = NULL;
    hsize_t       dims[OBJ_REF_DATASET_EMPTY_WRITE_TEST_SPACE_RANK];
    size_t        i, ref_array_size = 0;
    hid_t         file_id = -1, fapl_id = -1;
    hid_t         container_group = -1, group_id = -1;
    hid_t         dset_id = -1;
    hid_t         space_id = -1;

    TESTING("write to a dataset w/ object reference type; partially initialized ref. data")

    srand((unsigned) time(NULL));

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, OBJECT_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, OBJ_REF_DATASET_EMPTY_WRITE_TEST_SUBGROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container sub-group");
        goto error;
    }

    for (i = 0; i < OBJ_REF_DATASET_EMPTY_WRITE_TEST_SPACE_RANK; i++)
        dims[i] = (hsize_t) (rand() % 8 + 1);

    if ((space_id = H5Screate_simple(OBJ_REF_DATASET_EMPTY_WRITE_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, OBJ_REF_DATASET_EMPTY_WRITE_TEST_DSET_NAME, H5T_STD_REF_OBJ,
            space_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    for (i = 0, ref_array_size = 1; i < OBJ_REF_DATASET_EMPTY_WRITE_TEST_SPACE_RANK; i++)
        ref_array_size *= dims[i];

    if (NULL == (ref_array = (RV_obj_ref_t *) malloc(ref_array_size * sizeof(*ref_array))))
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

                if (NULL == (URI = RVget_uri(file_id)))
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(space_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_unused_object_API_calls(void)
{
    hid_t file_id = -1, fapl_id = -1;
    int   nerrors = 0;

    TESTING("unused object API calls")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    H5E_BEGIN_TRY {
        const char *comment = "comment";

        if (H5Oset_comment(file_id, comment) >= 0)
            nerrors++;
        if (H5Oget_comment(file_id, NULL, 0) >= 0)
            nerrors++;
    } H5E_END_TRY;

    if (nerrors)
        TEST_ERROR

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

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
    hsize_t dims[OPEN_LINK_WITHOUT_SLASH_DSET_DIMS] = { 5, 10 };
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   group_id = -1;
    hid_t   dset_id = -1;
    hid_t   space_id = -1;

    TESTING("opening a link without a leading slash")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, MISCELLANEOUS_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((space_id = H5Screate_simple(OPEN_LINK_WITHOUT_SLASH_DSET_DIMS, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(container_group, OPEN_LINK_WITHOUT_SLASH_DSET_NAME, H5T_NATIVE_INT, space_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
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

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((group_id = H5Gopen2(file_id, "/", H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open root group\n");
        goto error;
    }

    if ((dset_id = H5Dopen2(group_id, MISCELLANEOUS_TEST_GROUP_NAME "/" OPEN_LINK_WITHOUT_SLASH_DSET_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open dataset\n");
        goto error;
    }

    if ((space_id = H5Dget_space(dset_id)) < 0)
        TEST_ERROR

    if (H5Sclose(space_id) < 0)
        TEST_ERROR
    if (H5Dclose(dset_id) < 0)
        TEST_ERROR
    if (H5Gclose(group_id) < 0)
        TEST_ERROR
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (H5Fclose(file_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(space_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

static int
test_object_creation_by_absolute_path(void)
{
    hsize_t dims[OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DSET_SPACE_RANK];
    htri_t  link_exists;
    size_t  i;
    hid_t   file_id = -1, fapl_id = -1;
    hid_t   container_group = -1;
    hid_t   dset_id = -1;
    hid_t   fspace_id = -1;
    hid_t   group_id = -1, sub_group_id = -1;
    hid_t   dtype_id = -1;

    TESTING("object creation by absolute path")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
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
    if ((group_id = H5Gcreate2(container_group, OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_CONTAINER_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container group\n");
        goto error;
    }

    /* Next try to create a group under the container group by using an absolute pathname */
    if ((sub_group_id = H5Gcreate2(file_id, "/" MISCELLANEOUS_TEST_GROUP_NAME "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_CONTAINER_GROUP_NAME "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_SUBGROUP_NAME,
            H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create subgroup by absolute pathname\n");
        goto error;
    }

    /* Next try to create a dataset nested at the end of this group chain by using an absolute pathname */
    for (i = 0; i < OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DSET_SPACE_RANK; i++)
        dims[i] = OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DSET_DIM_SIZE;

    if ((fspace_id = H5Screate_simple(OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DSET_SPACE_RANK, dims, NULL)) <0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(file_id, "/" MISCELLANEOUS_TEST_GROUP_NAME "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_CONTAINER_GROUP_NAME "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_SUBGROUP_NAME "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DSET_NAME,
            OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DSET_DTYPE, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    /* Next try to create a committed datatype in the same fashion as the preceding dataset */
    if ((dtype_id = H5Tcreate(H5T_STRING, OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DTYPE_SIZE)) < 0) {
        H5_FAILED();
        printf("    couldn't create datatype\n");
        goto error;
    }

    if (H5Tcommit2(file_id, "/" MISCELLANEOUS_TEST_GROUP_NAME "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_CONTAINER_GROUP_NAME "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_SUBGROUP_NAME "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DTYPE_NAME,
            dtype_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        H5_FAILED();
        printf("    couldn't commit datatype\n");
        goto error;
    }

    /* Finally try to verify that all of the previously-created objects exist in the correct location */
    if ((link_exists = H5Lexists(file_id, "/" MISCELLANEOUS_TEST_GROUP_NAME "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_CONTAINER_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    container group didn't exist at the correct location\n");
        goto error;
    }

    if ((link_exists = H5Lexists(file_id, "/" MISCELLANEOUS_TEST_GROUP_NAME "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_CONTAINER_GROUP_NAME "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_SUBGROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    subgroup didn't exist at the correct location\n");
        goto error;
    }

    if ((link_exists = H5Lexists(file_id, "/" MISCELLANEOUS_TEST_GROUP_NAME "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_CONTAINER_GROUP_NAME "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_SUBGROUP_NAME "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DSET_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    dataset didn't exist at the correct location\n");
        goto error;
    }

    if ((link_exists = H5Lexists(file_id, "/" MISCELLANEOUS_TEST_GROUP_NAME "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_CONTAINER_GROUP_NAME "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_SUBGROUP_NAME "/" OBJECT_CREATE_BY_ABSOLUTE_PATH_TEST_DTYPE_NAME, H5P_DEFAULT)) < 0) {
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(fspace_id);
        H5Dclose(dset_id);
        H5Tclose(dtype_id);
        H5Gclose(sub_group_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

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
    hid_t   dset_id1 = -1;
    hid_t   dset_id2 = -1;
    hid_t   dset_id3 = -1;
    hid_t   dset_id4 = -1;
    hid_t   dset_id5 = -1;
    hid_t   dset_id6 = -1;
    hid_t   fspace_id = -1;

    TESTING("absolute vs. relative pathnames")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
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
    if ((group_id = H5Gcreate2(container_group, ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create container group\n");
        goto error;
    }

    for (i = 0; i < ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET_SPACE_RANK; i++)
        dims[i] = ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET_DIM_SIZE;

    if ((fspace_id = H5Screate_simple(ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    /* Create a dataset by absolute path in the form "/group/dataset" starting from the root group */
    if ((dset_id1 = H5Dcreate2(file_id, "/" MISCELLANEOUS_TEST_GROUP_NAME "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET1_NAME,
            ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET1_DTYPE, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset by absolute path from root\n");
        goto error;
    }

    /* Create a dataset by relative path in the form "group/dataset" starting from the container group */
    if ((dset_id2 = H5Dcreate2(container_group, ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET2_NAME,
            ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET2_DTYPE, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset by relative path from root\n");
        goto error;
    }

    /* Create a dataset by relative path in the form "./group/dataset" starting from the root group */
    if ((dset_id3 = H5Dcreate2(file_id, "./" MISCELLANEOUS_TEST_GROUP_NAME "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET3_NAME,
            ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET3_DTYPE, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset by relative path from root with leading '.'\n");
        goto error;
    }

    /* Create a dataset by absolute path in the form "/group/dataset" starting from the container group */
    if ((dset_id4 = H5Dcreate2(container_group, "/" MISCELLANEOUS_TEST_GROUP_NAME "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET4_NAME,
            ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET4_DTYPE, fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset by absolute path from container group\n");
        goto error;
    }

    /* Create a dataset by relative path in the form "dataset" starting from the container group */
    if ((dset_id5 = H5Dcreate2(group_id, ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET5_NAME, ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET5_DTYPE,
            fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset by relative path from container group\n");
        goto error;
    }

    /* Create a dataset by relative path in the form "./dataset" starting from the container group */
    if ((dset_id6 = H5Dcreate2(group_id, "./" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET6_NAME, ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET6_DTYPE,
            fspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset by relative path from container group with leading '.'\n");
        goto error;
    }

    /* Verify that all of the previously-created datasets exist in the correct locations */
    if ((link_exists = H5Lexists(file_id, "/" MISCELLANEOUS_TEST_GROUP_NAME "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET1_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    didn't exist at the correct location\n");
        goto error;
    }

    if ((link_exists = H5Lexists(file_id, "/" MISCELLANEOUS_TEST_GROUP_NAME "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET2_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    didn't exist at the correct location\n");
        goto error;
    }

    if ((link_exists = H5Lexists(file_id, "/" MISCELLANEOUS_TEST_GROUP_NAME "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET3_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    didn't exist at the correct location\n");
        goto error;
    }

    if ((link_exists = H5Lexists(file_id, "/" MISCELLANEOUS_TEST_GROUP_NAME "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET4_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    didn't exist at the correct location\n");
        goto error;
    }

    if ((link_exists = H5Lexists(file_id, "/" MISCELLANEOUS_TEST_GROUP_NAME "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET5_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't determine if link exists\n");
        goto error;
    }

    if (!link_exists) {
        H5_FAILED();
        printf("    didn't exist at the correct location\n");
        goto error;
    }

    if ((link_exists = H5Lexists(file_id, "/" MISCELLANEOUS_TEST_GROUP_NAME "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_CONTAINER_GROUP_NAME "/" ABSOLUTE_VS_RELATIVE_PATH_TEST_DSET6_NAME, H5P_DEFAULT)) < 0) {
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(fspace_id);
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
        RVterm();
    } H5E_END_TRY;

    return 1;
}

/* Simple test to ensure that calling RVinit() and
 * RVterm() twice doesn't do anything bad
 */
static int
test_double_init_free(void)
{
    hid_t fapl_id = -1;

    TESTING("double init/free correctness")

    if (RVinit() < 0)
        TEST_ERROR
    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Pclose(fapl_id);
        RVterm();
        RVterm();
    } H5E_END_TRY;

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
    hid_t   dset_id = -1;
    hid_t   attr_id = -1;
    hid_t   space_id = -1;

    TESTING("Correct URL-encoding behavior")

    if (RVinit() < 0)
        TEST_ERROR

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        TEST_ERROR
    if (H5Pset_fapl_rest_vol(fapl_id, URL, USERNAME, PASSWORD) < 0)
        TEST_ERROR

    if ((file_id = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl_id)) < 0) {
        H5_FAILED();
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, MISCELLANEOUS_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, URL_ENCODING_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create group\n");
        goto error;
    }

    for (i = 0; i < URL_ENCODING_TEST_SPACE_RANK; i++)
        dims[i] = URL_ENCODING_TEST_DSET_DIM_SIZE;

    if ((space_id = H5Screate_simple(URL_ENCODING_TEST_SPACE_RANK, dims, NULL)) < 0)
        TEST_ERROR

    if ((dset_id = H5Dcreate2(group_id, URL_ENCODING_TEST_DSET_NAME, URL_ENCODING_TEST_DSET_DTYPE, space_id,
            H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        H5_FAILED();
        printf("    couldn't create dataset\n");
        goto error;
    }

    if ((attr_id = H5Acreate2(dset_id, URL_ENCODING_TEST_ATTR_NAME, URL_ENCODING_TEST_ATTR_DTYPE, space_id,
            H5P_DEFAULT, H5P_DEFAULT)) < 0) {
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

    if (H5Sclose(space_id) < 0)
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
    if (RVterm() < 0)
        TEST_ERROR

    PASSED();

    return 0;

error:
    H5E_BEGIN_TRY {
        H5Sclose(space_id);
        H5Aclose(attr_id);
        H5Dclose(dset_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Pclose(fapl_id);
        H5Fclose(file_id);
        RVterm();
    } H5E_END_TRY;

    return 1;
}

/* Large test to ensure that H5P_DEFAULT works correctly in all of the places that it can be used */
static int
test_H5P_DEFAULT(void)
{
    TESTING("use of H5P_DEFAULT")

    SKIPPED();

    return 0;

error:
    return 1;
}

static int
cleanup(void)
{
    /* Delete the top-level domain */

    return 0;

error:
    return 1;
}

int
main( int argc, char** argv )
{
    size_t i;
    int    nerrors;

    printf("Test parameters:\n\n");
    printf("  - URL: %s\n", URL);
    printf("  - Username: %s\n", USERNAME);
    printf("  - Password: %s\n", PASSWORD);
    printf("  - Test File name: %s\n", FILENAME);
    printf("\n\n");

    for (i = 0, nerrors = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        nerrors += (tests[i])();
        printf("\n");
        fflush(stdout);
    }

    if (nerrors) goto error;

    puts("All REST VOL plugin tests passed");

    return 0;

error:
    printf("*** %d TEST%s FAILED ***\n", nerrors, nerrors > 1 ? "S" : "");
    return 1;
}
