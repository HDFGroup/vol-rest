/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
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
 *  This example illustrates how to create and close a group.
 *  It is used in the HDF5 Tutorial.
 */

#include <stdlib.h>

#include "hdf5.h"
#include "rest_vol_public.h"

#define FILE                 "group.h5"
#define FILE_NAME_MAX_LENGTH 256

int
main(void)
{

    hid_t       file_id, fapl_id, group_id; /* identifiers */
    const char *username;
    char        filename[FILE_NAME_MAX_LENGTH];
    herr_t      status;

    H5rest_init();

    fapl_id = H5Pcreate(H5P_FILE_ACCESS);
    H5Pset_fapl_rest_vol(fapl_id);

    username = getenv("HSDS_USERNAME");

    snprintf(filename, FILE_NAME_MAX_LENGTH, "/home/%s/" FILE, username);

    /* Create a new file using default properties. */
    file_id = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, fapl_id);

    /* Create a group named "/MyGroup" in the file. */
    group_id = H5Gcreate2(file_id, "/MyGroup", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

    /* Close the group. */
    status = H5Gclose(group_id);

    status = H5Pclose(fapl_id);

    /* Terminate access to the file. */
    status = H5Fclose(file_id);

    H5rest_term();
}
