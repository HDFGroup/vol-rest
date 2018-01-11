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
 * Programmer:  Mohamad Chaarawi <chaarawi@hdfgroup.gov>
 *              January, 2012
 *
 * Purpose:	The public header file for the Native VOL plugin.
 */
#ifndef H5VLnative_H
#define H5VLnative_H

#define H5VL_NATIVE	(H5VL_native_init())
#define HDF5_VOL_NATIVE_VERSION_1	1	/* Version number of Native VOL plugin */

#ifdef __cplusplus
extern "C" {
#endif

H5_DLL hid_t H5VL_native_init(void);
H5_DLL herr_t H5Pset_fapl_native(hid_t fapl_id);

#ifdef __cplusplus
}
#endif

#endif
