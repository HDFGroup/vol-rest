/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of the HDF5 REST VOL connector. The full copyright      *
 * notice, including terms governing use, modification, and redistribution,  *
 * is contained in the COPYING file, which can be found at the root of the   *
 * source code distribution tree.                                            *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef REST_VOL_FILE_H_
#define REST_VOL_FILE_H_

#include "rest_vol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* REST VOL File callbacks */
void  *RV_file_create(const char *name, unsigned flags, hid_t fcpl_id, hid_t fapl_id, hid_t dxpl_id,
                      void **req);
void  *RV_file_open(const char *name, unsigned flags, hid_t fapl_id, hid_t dxpl_id, void **req);
herr_t RV_file_get(void *obj, H5VL_file_get_args_t *args, hid_t dxpl_id, void **req);
herr_t RV_file_specific(void *obj, H5VL_file_specific_args_t *args, hid_t dxpl_id, void **req);
herr_t RV_file_close(void *file, hid_t dxpl_id, void **req);

#ifdef __cplusplus
}
#endif

#endif /* REST_VOL_FILE_H_ */
