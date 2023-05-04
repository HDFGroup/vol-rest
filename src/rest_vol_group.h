/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of the HDF5 REST VOL connector. The full copyright      *
 * notice, including terms governing use, modification, and redistribution,  *
 * is contained in the COPYING file, which can be found at the root of the   *
 * source code distribution tree.                                            *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef REST_VOL_GROUP_H_
#define REST_VOL_GROUP_H_

#include "rest_vol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* REST VOL Group callbacks */
void  *RV_group_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t lcpl_id,
                       hid_t gcpl_id, hid_t gapl_id, hid_t dxpl_id, void **req);
void  *RV_group_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t gapl_id,
                     hid_t dxpl_id, void **req);
herr_t RV_group_get(void *obj, H5VL_group_get_args_t *args, hid_t dxpl_id, void **req);
herr_t RV_group_close(void *grp, hid_t dxpl_id, void **req);

#ifdef __cplusplus
}
#endif

#endif /* REST_VOL_GROUP_H_ */
