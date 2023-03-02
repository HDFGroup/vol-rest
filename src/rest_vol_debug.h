/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of the HDF5 REST VOL connector. The full copyright      *
 * notice, including terms governing use, modification, and redistribution,  *
 * is contained in the COPYING file, which can be found at the root of the   *
 * source code distribution tree.                                            *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef REST_VOL_DEBUG_H_
#define REST_VOL_DEBUG_H_

#include "rest_vol.h"

#ifdef RV_CONNECTOR_DEBUG

#ifdef __cplusplus
extern "C" {
#endif

/* Helper functions to print out useful debugging information */
const char *object_type_to_string(H5I_type_t obj_type);
const char *object_type_to_string2(H5O_type_t obj_type);
const char *datatype_class_to_string(hid_t dtype);
const char *link_class_to_string(H5L_type_t link_type);
const char *attr_get_type_to_string(H5VL_attr_get_t get_type);
const char *attr_specific_type_to_string(H5VL_attr_specific_t specific_type);
const char *datatype_get_type_to_string(H5VL_datatype_get_t get_type);
const char *dataset_get_type_to_string(H5VL_dataset_get_t get_type);
const char *dataset_specific_type_to_string(H5VL_dataset_specific_t specific_type);
const char *file_flags_to_string(unsigned flags);
const char *file_get_type_to_string(H5VL_file_get_t get_type);
const char *file_specific_type_to_string(H5VL_file_specific_t specific_type);
const char *group_get_type_to_string(H5VL_group_get_t get_type);
const char *link_create_type_to_string(H5VL_link_create_t link_create_type);
const char *link_get_type_to_string(H5VL_link_get_t get_type);
const char *link_specific_type_to_string(H5VL_link_specific_t specific_type);
const char *object_get_type_to_string(H5VL_object_get_t get_type);
const char *object_specific_type_to_string(H5VL_object_specific_t specific_type);

#ifdef __cplusplus
}
#endif

#endif /* RV_CONNECTOR_DEBUG */

#endif /* REST_VOL_DEBUG_H_ */
