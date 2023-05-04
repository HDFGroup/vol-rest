/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of the HDF5 REST VOL connector. The full copyright      *
 * notice, including terms governing use, modification, and redistribution,  *
 * is contained in the COPYING file, which can be found at the root of the   *
 * source code distribution tree.                                            *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef REST_VOL_OBJECT_H_
#define REST_VOL_OBJECT_H_

#include "rest_vol.h"

#ifdef __cplusplus
extern "C" {
#endif

void  *RV_object_open(void *obj, const H5VL_loc_params_t *loc_params, H5I_type_t *opened_type, hid_t dxpl_id,
                      void **req);
herr_t RV_object_copy(void *src_obj, const H5VL_loc_params_t *loc_params1, const char *src_name,
                      void *dst_obj, const H5VL_loc_params_t *loc_params2, const char *dst_name,
                      hid_t ocpypl_id, hid_t lcpl_id, hid_t dxpl_id, void **req);
herr_t RV_object_get(void *obj, const H5VL_loc_params_t *loc_params, H5VL_object_get_args_t *args,
                     hid_t dxpl_id, void **req);
herr_t RV_object_specific(void *obj, const H5VL_loc_params_t *loc_params, H5VL_object_specific_args_t *args,
                          hid_t dxpl_id, void **req);

#ifdef __cplusplus
}
#endif

#endif /* REST_VOL_OBJECT_H_ */
