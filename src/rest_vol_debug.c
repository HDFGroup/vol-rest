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
 * Useful code for debugging the REST VOL connector.
 */

#include "rest_vol_debug.h"

#ifdef RV_CONNECTOR_DEBUG

/*-------------------------------------------------------------------------
 * Function:    object_type_to_string
 *
 * Purpose:     Helper function to convert an object's type into a string
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
const char *
object_type_to_string(H5I_type_t obj_type)
{
    switch (obj_type) {
        case H5I_UNINIT:
            return "H5I_UNINIT";
        case H5I_BADID:
            return "H5I_BADID";
        case H5I_FILE:
            return "H5I_FILE";
        case H5I_GROUP:
            return "H5I_GROUP";
        case H5I_DATATYPE:
            return "H5I_DATATYPE";
        case H5I_DATASPACE:
            return "H5I_DATASPACE";
        case H5I_DATASET:
            return "H5I_DATASET";
        case H5I_ATTR:
            return "H5I_ATTR";
        case H5I_VFL:
            return "H5I_VFL";
        case H5I_VOL:
            return "H5I_VOL";
        case H5I_GENPROP_CLS:
            return "H5I_GENPROP_CLS";
        case H5I_GENPROP_LST:
            return "H5I_GENPROP_LST";
        case H5I_ERROR_CLASS:
            return "H5I_ERROR_CLASS";
        case H5I_ERROR_MSG:
            return "H5I_ERROR_MSG";
        case H5I_ERROR_STACK:
            return "H5I_ERROR_STACK";
        case H5I_NTYPES:
            return "H5I_NTYPES";
        default:
            return "(unknown)";
    } /* end switch */
} /* end object_type_to_string() */

/*-------------------------------------------------------------------------
 * Function:    object_type_to_string2
 *
 * Purpose:     Helper function to convert an object's type into a string
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
const char *
object_type_to_string2(H5O_type_t obj_type)
{
    switch (obj_type) {
        case H5O_TYPE_UNKNOWN:
            return "H5O_TYPE_UNKNOWN";
        case H5O_TYPE_GROUP:
            return "H5O_TYPE_GROUP";
        case H5O_TYPE_DATASET:
            return "H5O_TYPE_DATASET";
        case H5O_TYPE_NAMED_DATATYPE:
            return "H5O_TYPE_NAMED_DATATYPE";
        case H5O_TYPE_MAP:
            return "H5O_TYPE_MAP";
        case H5O_TYPE_NTYPES:
            return "H5O_TYPE_NTYPES";
        default:
            return "(unknown)";
    } /* end switch */
} /* end object_type_to_string2() */

/*-------------------------------------------------------------------------
 * Function:    datatype_class_to_string
 *
 * Purpose:     Helper function to convert a datatype's class into a string
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
const char *
datatype_class_to_string(hid_t dtype)
{
    switch (H5Tget_class(dtype)) {
        case H5T_NO_CLASS:
            return "H5T_NO_CLASS";
        case H5T_INTEGER:
            return "H5T_INTEGER";
        case H5T_FLOAT:
            return "H5T_FLOAT";
        case H5T_TIME:
            return "H5T_TIME";
        case H5T_STRING:
            return "H5T_STRING";
        case H5T_BITFIELD:
            return "H5T_BITFIELD";
        case H5T_OPAQUE:
            return "H5T_OPAQUE";
        case H5T_COMPOUND:
            return "H5T_COMPOUND";
        case H5T_REFERENCE:
            return "H5T_REFERENCE";
        case H5T_ENUM:
            return "H5T_ENUM";
        case H5T_VLEN:
            return "H5T_VLEN";
        case H5T_ARRAY:
            return "H5T_ARRAY";
        case H5T_NCLASSES:
            return "H5T_NCLASSES";
        default:
            return "(unknown)";
    } /* end switch */
} /* end datatype_class_to_string() */

/*-------------------------------------------------------------------------
 * Function:    link_class_to_string
 *
 * Purpose:     Helper function to convert a link's class into a string
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
const char *
link_class_to_string(H5L_type_t link_type)
{
    switch (link_type) {
        case H5L_TYPE_ERROR:
            return "H5L_TYPE_ERROR";
        case H5L_TYPE_HARD:
            return "H5L_TYPE_HARD";
        case H5L_TYPE_SOFT:
            return "H5L_TYPE_SOFT";
        case H5L_TYPE_EXTERNAL:
            return "H5L_TYPE_EXTERNAL";
        case H5L_TYPE_MAX:
            return "H5L_TYPE_MAX";
        default:
            return "(unknown)";
    } /* end switch */
} /* end link_class_to_string() */

/*-------------------------------------------------------------------------
 * Function:    attr_get_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_attr_get_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
const char *
attr_get_type_to_string(H5VL_attr_get_t get_type)
{
    switch (get_type) {
        case H5VL_ATTR_GET_ACPL:
            return "H5VL_ATTR_GET_ACPL";
        case H5VL_ATTR_GET_INFO:
            return "H5VL_ATTR_GET_INFO";
        case H5VL_ATTR_GET_NAME:
            return "H5VL_ATTR_GET_NAME";
        case H5VL_ATTR_GET_SPACE:
            return "H5VL_ATTR_GET_SPACE";
        case H5VL_ATTR_GET_STORAGE_SIZE:
            return "H5VL_ATTR_GET_STORAGE_SIZE";
        case H5VL_ATTR_GET_TYPE:
            return "H5VL_ATTR_GET_TYPE";
        default:
            return "(unknown)";
    } /* end switch */
} /* end attr_get_type_to_string() */

/*-------------------------------------------------------------------------
 * Function:    attr_specific_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_attr_specific_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
const char *
attr_specific_type_to_string(H5VL_attr_specific_t specific_type)
{
    switch (specific_type) {
        case H5VL_ATTR_DELETE:
            return "H5VL_ATTR_DELETE";
        case H5VL_ATTR_EXISTS:
            return "H5VL_ATTR_EXISTS";
        case H5VL_ATTR_ITER:
            return "H5VL_ATTR_ITER";
        case H5VL_ATTR_RENAME:
            return "H5VL_ATTR_RENAME";
        default:
            return "(unknown)";
    } /* end switch */
} /* end attr_specific_type_to_string() */

/*-------------------------------------------------------------------------
 * Function:    datatype_get_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_datatype_get_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
const char *
datatype_get_type_to_string(H5VL_datatype_get_t get_type)
{
    switch (get_type) {
        case H5VL_DATATYPE_GET_BINARY:
            return "H5VL_DATATYPE_GET_BINARY";
        case H5VL_DATATYPE_GET_TCPL:
            return "H5VL_DATATYPE_GET_TCPL";
        default:
            return "(unknown)";
    } /* end switch */
} /* end datatype_get_type_to_string() */

/*-------------------------------------------------------------------------
 * Function:    dataset_get_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_dataset_get_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
const char *
dataset_get_type_to_string(H5VL_dataset_get_t get_type)
{
    switch (get_type) {
        case H5VL_DATASET_GET_DAPL:
            return "H5VL_DATASET_GET_DAPL";
        case H5VL_DATASET_GET_DCPL:
            return "H5VL_DATASET_GET_DCPL";
        case H5VL_DATASET_GET_SPACE:
            return "H5VL_DATASET_GET_SPACE";
        case H5VL_DATASET_GET_SPACE_STATUS:
            return "H5VL_DATASET_GET_SPACE_STATUS";
        case H5VL_DATASET_GET_STORAGE_SIZE:
            return "H5VL_DATASET_GET_STORAGE_SIZE";
        case H5VL_DATASET_GET_TYPE:
            return "H5VL_DATASET_GET_TYPE";
        default:
            return "(unknown)";
    } /* end switch */
} /* end dataset_get_type_to_string() */

/*-------------------------------------------------------------------------
 * Function:    dataset_specific_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_dataset_specific_t enum into its string
 *              representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
const char *
dataset_specific_type_to_string(H5VL_dataset_specific_t specific_type)
{
    switch (specific_type) {
        case H5VL_DATASET_SET_EXTENT:
            return "H5VL_DATASET_SET_EXTENT";
        case H5VL_DATASET_FLUSH:
            return "H5VL_DATASET_FLUSH";
        case H5VL_DATASET_REFRESH:
            return "H5VL_DATASET_REFRESH";
        default:
            return "(unknown)";
    } /* end switch */
} /* end dataset_specific_type_to_string() */

/*-------------------------------------------------------------------------
 * Function:    file_flags_to_string
 *
 * Purpose:     Helper function to convert File creation/access flags
 *              into their string representations
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
const char *
file_flags_to_string(unsigned flags)
{
    /* When included from a public header, these macros cannot be switched upon */
    if (flags == H5F_ACC_TRUNC)
        return "H5F_ACC_TRUNC";
    else if (flags == H5F_ACC_EXCL)
        return "H5F_ACC_EXCL";
    else if (flags == H5F_ACC_RDWR)
        return "H5F_ACC_RDWR";
    else if (flags == H5F_ACC_RDONLY)
        return "H5F_ACC_RDONLY";
    else
        return "(unknown)";
} /* end file_flags_to_string() */

/*-------------------------------------------------------------------------
 * Function:    file_get_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_file_get_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
const char *
file_get_type_to_string(H5VL_file_get_t get_type)
{
    switch (get_type) {
        case H5VL_FILE_GET_CONT_INFO:
            return "H5VL_FILE_GET_CONT_INFO";
        case H5VL_FILE_GET_FAPL:
            return "H5VL_FILE_GET_FAPL";
        case H5VL_FILE_GET_FCPL:
            return "H5VL_FILE_GET_FCPL";
        case H5VL_FILE_GET_FILENO:
            return "H5VL_FILE_GET_FILENO";
        case H5VL_FILE_GET_INTENT:
            return "H5VL_FILE_GET_INTENT";
        case H5VL_FILE_GET_NAME:
            return "H5VL_FILE_GET_NAME";
        case H5VL_FILE_GET_OBJ_COUNT:
            return "H5VL_FILE_GET_OBJ_COUNT";
        case H5VL_FILE_GET_OBJ_IDS:
            return "H5VL_FILE_GET_OBJ_IDS";
        default:
            return "(unknown)";
    } /* end switch */
} /* end file_get_type_to_string() */

/*-------------------------------------------------------------------------
 * Function:    file_specific_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_file_specific_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
const char *
file_specific_type_to_string(H5VL_file_specific_t specific_type)
{
    switch (specific_type) {
        case H5VL_FILE_FLUSH:
            return "H5VL_FILE_FLUSH";
        case H5VL_FILE_REOPEN:
            return "H5VL_FILE_REOPEN";
        case H5VL_FILE_IS_ACCESSIBLE:
            return "H5VL_FILE_IS_ACCESSIBLE";
        case H5VL_FILE_DELETE:
            return "H5VL_FILE_DELETE";
        case H5VL_FILE_IS_EQUAL:
            return "H5VL_FILE_IS_EQUAL";
        default:
            return "(unknown)";
    } /* end switch */
} /* end file_specific_type_to_string() */

/*-------------------------------------------------------------------------
 * Function:    group_get_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_group_get_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
const char *
group_get_type_to_string(H5VL_group_get_t get_type)
{
    switch (get_type) {
        case H5VL_GROUP_GET_GCPL:
            return "H5VL_GROUP_GET_GCPL";
        case H5VL_GROUP_GET_INFO:
            return "H5VL_GROUP_GET_INFO";
        default:
            return "(unknown)";
    } /* end switch */
} /* end group_get_type_to_string() */

/*-------------------------------------------------------------------------
 * Function:    link_create_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_link_create_type_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
const char *
link_create_type_to_string(H5VL_link_create_t link_create_type)
{
    switch (link_create_type) {
        case H5VL_LINK_CREATE_HARD:
            return "H5VL_LINK_CREATE_HARD";
        case H5VL_LINK_CREATE_SOFT:
            return "H5VL_LINK_CREATE_SOFT";
        case H5VL_LINK_CREATE_UD:
            return "H5VL_LINK_CREATE_UD";
        default:
            return "(unknown)";
    } /* end switch */
} /* end link_create_type_to_string() */

/*-------------------------------------------------------------------------
 * Function:    link_get_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_link_get_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
const char *
link_get_type_to_string(H5VL_link_get_t get_type)
{
    switch (get_type) {
        case H5VL_LINK_GET_INFO:
            return "H5VL_LINK_GET_INFO";
        case H5VL_LINK_GET_NAME:
            return "H5VL_LINK_GET_NAME";
        case H5VL_LINK_GET_VAL:
            return "H5VL_LINK_GET_VAL";
        default:
            return "(unknown)";
    } /* end switch */
} /* end link_get_type_to_string() */

/*-------------------------------------------------------------------------
 * Function:    link_specific_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_link_specific_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
const char *
link_specific_type_to_string(H5VL_link_specific_t specific_type)
{
    switch (specific_type) {
        case H5VL_LINK_DELETE:
            return "H5VL_LINK_DELETE";
        case H5VL_LINK_EXISTS:
            return "H5VL_LINK_EXISTS";
        case H5VL_LINK_ITER:
            return "H5VL_LINK_ITER";
        default:
            return "(unknown)";
    } /* end switch */
} /* end link_specific_type_to_string() */

/*-------------------------------------------------------------------------
 * Function:    object_get_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_object_get_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
const char *
object_get_type_to_string(H5VL_object_get_t get_type)
{
    switch (get_type) {
        case H5VL_OBJECT_GET_FILE:
            return "H5VL_OBJECT_GET_FILE";
        case H5VL_OBJECT_GET_NAME:
            return "H5VL_OBJECT_GET_NAME";
        case H5VL_OBJECT_GET_TYPE:
            return "H5VL_OBJECT_GET_TYPE";
        case H5VL_OBJECT_GET_INFO:
            return "H5VL_OBJECT_GET_INFO";
        default:
            return "(unknown)";
    } /* end switch */
} /* end object_get_type_to_string() */

/*-------------------------------------------------------------------------
 * Function:    object_specific_type_to_string
 *
 * Purpose:     Helper function to convert each member of the
 *              H5VL_object_specific_t enum into its string representation
 *
 * Return:      String representation of given object or '(unknown)' if
 *              the function can't determine the type of object it has
 *              been given (can't fail).
 *
 * Programmer:  Jordan Henderson
 *              December, 2017
 */
const char *
object_specific_type_to_string(H5VL_object_specific_t specific_type)
{
    switch (specific_type) {
        case H5VL_OBJECT_CHANGE_REF_COUNT:
            return "H5VL_OBJECT_CHANGE_REF_COUNT";
        case H5VL_OBJECT_EXISTS:
            return "H5VL_OBJECT_EXISTS";
        case H5VL_OBJECT_LOOKUP:
            return "H5VL_OBJECT_LOOKUP";
        case H5VL_OBJECT_VISIT:
            return "H5VL_OBJECT_VISIT";
        case H5VL_OBJECT_FLUSH:
            return "H5VL_OBJECT_FLUSH";
        case H5VL_OBJECT_REFRESH:
            return "H5VL_OBJECT_REFRESH";
        default:
            return "(unknown)";
    } /* end switch */
} /* end object_specific_type_to_string() */
#endif /* RV_CONNECTOR_DEBUG */
