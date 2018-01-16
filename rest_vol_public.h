/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the files COPYING and Copyright.html.  COPYING can be found at the root   *
 * of the source code distribution tree; Copyright.html can be found at the  *
 * root level of an installed copy of the electronic HDF5 document set and   *
 * is linked from the top-level documents page.  It can also be found at     *
 * http://hdfgroup.org/HDF5/doc/Copyright.html.  If you do not have          *
 * access to either file, you may request a copy from help@hdfgroup.org.     *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * Programmer:  Jordan Henderson
 *              February, 2017
 *
 * Purpose: The public header file for the REST VOL plugin.
 */

#ifndef rest_vol_public_H
#define rest_vol_public_H

#include <curl/curl.h>
#include <yajl/yajl_tree.h>

#include "H5public.h"
#include "H5Rpublic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Uncomment to allow this VOL plugin to print out debugging information to stdout */
/* #define PLUGIN_DEBUG */

/* Uncomment to allow cURL to print out verbose information about the HTTP requests it makes */
/* #define CURL_DEBUG */

/* Uncomment to track memory usage in this VOL plugin. When calling RVterm(),
 * the plugin will throw an error if memory was still allocated at termination time,
 * generally signifying a memory leak in either the application code or in this plugin.
 */
/* #define TRACK_MEM_USAGE */

/* Maximum length in characters of an addressable URL used in the server requests sent by
 * this VOL plugin. If the URLs used in operation are longer than this, the value will have
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
 * VOL plugin will likely be truncated. */
#define MAX_NUM_LENGTH 20

/* Maximum length of the name of a link used for an HDF5 object. This
 * is particularly important for performance by keeping locality of
 * reference for link names during H5Literate/visit calls. If it appears
 * that link names are being truncated by the plugin, this value should
 * be adjusted.
 */
#define LINK_NAME_MAX_LENGTH 2048

/* Maximum length of the name of an HDF5 attribute. This is particularly
 * important for performance by keeping locality of reference for attribute
 * names during H5Aiterate calls. If it appears that attribute names are
 * being truncated by the plugin, this value should be adjusted.
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
#define FILTER_NAME_MAX_LENGTH                        256
#define FILTER_MAX_CD_VALUES                          32

typedef struct rv_obj_ref_t {
    H5R_type_t ref_type;
    H5I_type_t ref_obj_type;
    char       ref_obj_URI[URI_MAX_LENGTH];
} rv_obj_ref_t;

H5_DLL herr_t      RVinit(void);
H5_DLL herr_t      RVterm(void);
H5_DLL herr_t      H5Pset_fapl_rest_vol(hid_t fapl_id);
H5_DLL const char *RVget_uri(hid_t);

#ifdef __cplusplus
}
#endif

#endif /* rest_vol_public_H */
