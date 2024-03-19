/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of the HDF5 REST VOL connector. The full copyright      *
 * notice, including terms governing use, modification, and redistribution,  *
 * is contained in the COPYING file, which can be found at the root of the   *
 * source code distribution tree.                                            *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef REST_VOL_LINK_H_
#define REST_VOL_LINK_H_

#include "rest_vol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* REST VOL Link callbacks */
herr_t RV_link_create(H5VL_link_create_args_t *args, void *obj, const H5VL_loc_params_t *loc_params,
                      hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req);
;
herr_t RV_link_copy(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj,
                    const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id,
                    void **req);
herr_t RV_link_move(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj,
                    const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id,
                    void **req);
herr_t RV_link_get(void *obj, const H5VL_loc_params_t *loc_params, H5VL_link_get_args_t *args, hid_t dxpl_id,
                   void **req);
herr_t RV_link_specific(void *obj, const H5VL_loc_params_t *loc_params, H5VL_link_specific_args_t *args,
                        hid_t dxpl_id, void **req);

herr_t RV_get_link_info_callback(char *HTTP_response, const void *callback_data_in, void *callback_data_out);
herr_t RV_get_link_val_callback(char *HTTP_response, const void *callback_data_in, void *callback_data_out);
herr_t RV_get_link_obj_type_callback(char *HTTP_response, const void *callback_data_in,
                                     void *callback_data_out);

#ifdef __cplusplus
}
#endif

#endif /* REST_VOL_LINK_H_ */
