/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of the HDF5 REST VOL connector. The full copyright      *
 * notice, including terms governing use, modification, and redistribution,  *
 * is contained in the COPYING file, which can be found at the root of the   *
 * source code distribution tree.                                            *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef REST_VOL_ATTR_H_
#define REST_VOL_ATTR_H_

#include "rest_vol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* REST VOL Attribute callbacks */
void  *RV_attr_create(void *obj, const H5VL_loc_params_t *loc_params, const char *attr_name, hid_t type_id,
                      hid_t space_id, hid_t acpl_id, hid_t aapl_id, hid_t dxpl_id, void **req);
void  *RV_attr_open(void *obj, const H5VL_loc_params_t *loc_params, const char *attr_name, hid_t aapl_id,
                    hid_t dxpl_id, void **req);
herr_t RV_attr_read(void *attr, hid_t dtype_id, void *buf, hid_t dxpl_id, void **req);
herr_t RV_attr_write(void *attr, hid_t dtype_id, const void *buf, hid_t dxpl_id, void **req);
herr_t RV_attr_get(void *obj, H5VL_attr_get_args_t *args, hid_t dxpl_id, void **req);
herr_t RV_attr_specific(void *obj, const H5VL_loc_params_t *loc_params, H5VL_attr_specific_args_t *args,
                        hid_t dxpl_id, void **req);
herr_t RV_attr_close(void *attr, hid_t dxpl_id, void **req);

#ifdef __cplusplus
}
#endif

#endif /* REST_VOL_ATTR_H_ */
