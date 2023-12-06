/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of the HDF5 REST VOL connector. The full copyright      *
 * notice, including terms governing use, modification, and redistribution,  *
 * is contained in the COPYING file, which can be found at the root of the   *
 * source code distribution tree.                                            *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef REST_VOL_DATATYPE_H_
#define REST_VOL_DATATYPE_H_

#include "rest_vol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* REST VOL Datatype callbacks */
void  *RV_datatype_commit(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t type_id,
                          hid_t lcpl_id, hid_t tcpl_id, hid_t tapl_id, hid_t dxpl_id, void **req);
void  *RV_datatype_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t tapl_id,
                        hid_t dxpl_id, void **req);
herr_t RV_datatype_get(void *obj, H5VL_datatype_get_args_t *args, hid_t dxpl_id, void **req);
herr_t RV_datatype_close(void *dt, hid_t dxpl_id, void **req);

/* REST VOL Datatype helper functions */
hid_t RV_parse_datatype(char *type, hbool_t need_truncate);

/* Determine whether datatype conversion is necessary between 'same' datatypes */
static htri_t RV_detect_vl_vlstr_ref(hid_t type_id);

/* Determine if a background buffer is needed for type conversion */
static htri_t RV_need_bkg(hid_t src_type_id, hid_t dst_type_id, hbool_t dst_file, size_t *dst_type_size,
                          hbool_t *fill_bkg);

#ifdef __cplusplus
}
#endif

#endif /* REST_VOL_DATATYPE_H_ */
