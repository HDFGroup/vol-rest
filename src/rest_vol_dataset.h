/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of the HDF5 REST VOL connector. The full copyright      *
 * notice, including terms governing use, modification, and redistribution,  *
 * is contained in the COPYING file, which can be found at the root of the   *
 * source code distribution tree.                                            *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef REST_VOL_DATASET_H_
#define REST_VOL_DATASET_H_

#include "rest_vol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* REST VOL Dataset callbacks */
void  *RV_dataset_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t lcpl_id,
                         hid_t type_id, hid_t space_id, hid_t dcpl_id, hid_t dapl_id, hid_t dxpl_id,
                         void **req);
void  *RV_dataset_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t dapl_id,
                       hid_t dxpl_id, void **req);
herr_t RV_dataset_read(size_t count, void *dset[], hid_t mem_type_id[], hid_t mem_space_id[],
                       hid_t file_space_id[], hid_t dxpl_id, void *buf[], void **req);
herr_t RV_dataset_write(size_t count, void *dset[], hid_t mem_type_id[], hid_t mem_space_id[],
                        hid_t file_space_id[], hid_t dxpl_id, const void *buf[], void **req);
herr_t RV_dataset_get(void *obj, H5VL_dataset_get_args_t *args, hid_t dxpl_id, void **req);
herr_t RV_dataset_specific(void *obj, H5VL_dataset_specific_args_t *args, hid_t dxpl_id, void **req);
herr_t RV_dataset_close(void *dset, hid_t dxpl_id, void **req);

#ifdef __cplusplus
}
#endif

#endif /* REST_VOL_DATASET_H_ */
