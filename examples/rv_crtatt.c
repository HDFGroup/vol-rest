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
 *  This example illustrates how to create an attribute attached to a
 *  dataset.  It is used in the HDF5 Tutorial.
 */

#include <stdlib.h>

#include "hdf5.h"
#include "rest_vol_public.h"

#define FILE                 "dset.h5"
#define FILE_NAME_MAX_LENGTH 256

int
main(void)
{

    hid_t       file_id, fapl_id, dataset_id, attribute_id, dataspace_id; /* identifiers */
    hsize_t     dims;
    int         attr_data[2];
    const char *username;
    char        filename[FILE_NAME_MAX_LENGTH];
    herr_t      status;

    H5rest_init();

    /* Initialize the attribute data. */
    attr_data[0] = 100;
    attr_data[1] = 200;

    fapl_id = H5Pcreate(H5P_FILE_ACCESS);
    H5Pset_fapl_rest_vol(fapl_id);

    username = getenv("HSDS_USERNAME");

    snprintf(filename, FILE_NAME_MAX_LENGTH, "/home/%s/" FILE, username);

    /* Open an existing file. */
    file_id = H5Fopen(filename, H5F_ACC_RDWR, fapl_id);

    /* Open an existing dataset. */
    dataset_id = H5Dopen2(file_id, "/dset", H5P_DEFAULT);

    /* Create the data space for the attribute. */
    dims         = 2;
    dataspace_id = H5Screate_simple(1, &dims, NULL);

    /* Create a dataset attribute. */
    attribute_id = H5Acreate2(dataset_id, "Units", H5T_STD_I32BE, dataspace_id, H5P_DEFAULT, H5P_DEFAULT);

    /* Write the attribute data. */
    status = H5Awrite(attribute_id, H5T_NATIVE_INT, attr_data);

    /* Close the attribute. */
    status = H5Aclose(attribute_id);

    /* Close the dataspace. */
    status = H5Sclose(dataspace_id);

    /* Close to the dataset. */
    status = H5Dclose(dataset_id);

    status = H5Pclose(fapl_id);

    /* Close the file. */
    status = H5Fclose(file_id);

    H5rest_term();
}
