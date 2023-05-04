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
 * Purpose: The public header file for the REST VOL connector.
 */

#ifndef rest_vol_public_H
#define rest_vol_public_H

#include <hdf5.h>
#include "H5PLextern.h"

/* Maximum length in characters of an addressable URL used in the server requests sent by
 * this VOL connector. If the URLs used in operation are longer than this, the value will have
 * to be adjusted. Otherwise, the URLs will be truncated.
 */
#define URL_MAX_LENGTH 2048

/* Maximum length in characters of the URI of an object as returned by the server. If the
 * server in question returns URIs which are longer than this, the value will have to be
 * adjusted. Otherwise, the URIs will be truncated and invalid, likely causing severe
 * problems.
 */
#define URI_MAX_LENGTH 256

/* Maximum length of a large unsigned value in terms of characters.
 * This is used in places such as specifying the size of each dimension
 * of a Dataset. If the maximum length of a number value becomes larger
 * than this in the future (due to larger types), this value will need
 * to be adjusted slightly. Otherwise, numerical values sent by this
 * VOL connector will likely be truncated. */
#define MAX_NUM_LENGTH 20

/* Maximum length of the name of a link used for an HDF5 object. This
 * is particularly important for performance by keeping locality of
 * reference for link names during H5Literate/visit calls. If it appears
 * that link names are being truncated by the connector, this value should
 * be adjusted.
 */
#define LINK_NAME_MAX_LENGTH 2048

/* Maximum length of the name of an HDF5 attribute. This is particularly
 * important for performance by keeping locality of reference for attribute
 * names during H5Aiterate calls. If it appears that attribute names are
 * being truncated by the connector, this value should be adjusted.
 */
#define ATTRIBUTE_NAME_MAX_LENGTH 2048

/* Maximum length of the name of an external file used for storage for
 * contiguous dataset layouts.
 */
#define EXTERNAL_FILE_NAME_MAX_LENGTH 2048

/* Maximum length of the name of a filter, as well as the maximum length
 * of the supplemental filter options array that can be specified along
 * with the filter.
 */
#define FILTER_NAME_MAX_LENGTH 256
#define FILTER_MAX_CD_VALUES   32

typedef struct rv_obj_ref_t {
    H5R_type_t ref_type;
    H5I_type_t ref_obj_type;
    char       ref_obj_URI[URI_MAX_LENGTH];
} rv_obj_ref_t;

#ifdef __cplusplus
extern "C" {
#endif

H5PLUGIN_DLL herr_t      H5rest_init(void);
H5PLUGIN_DLL herr_t      H5rest_term(void);
H5PLUGIN_DLL herr_t      H5Pset_fapl_rest_vol(hid_t fapl_id);
H5PLUGIN_DLL const char *H5rest_get_object_uri(hid_t);

#ifdef __cplusplus
}
#endif

#endif /* rest_vol_public_H */
