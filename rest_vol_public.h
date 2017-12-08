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

#define PLUGIN_DEBUG

/* Uncomment to track memory usage in this VOL plugin. When calling H5VLrest_term(),
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

typedef struct rest_obj_ref_t {
    H5R_type_t ref_type;
    H5I_type_t ref_obj_type;
    char       ref_obj_URI[URI_MAX_LENGTH];
} rest_obj_ref_t;

H5_DLL herr_t      H5VLrest_init(void);
H5_DLL herr_t      H5VLrest_term(void);
H5_DLL herr_t      H5Pset_fapl_rest_vol(hid_t fapl_id, const char *URL, const char *username, const char *password);
H5_DLL const char *H5VLrest_get_uri(hid_t);

#ifdef __cplusplus
}
#endif

#endif /* H5VLrest_public_H */
