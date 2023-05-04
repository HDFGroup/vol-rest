/*
 * Copyright (C) 2013-2017 Argonne National Laboratory, Department of Energy,
 *                    UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * The full copyright notice, including terms governing use, modification,
 * and redistribution, is contained in the COPYING file that can be
 * found at the root of the source code distribution tree.
 */

#ifndef RV_HASH_STRING_H
#define RV_HASH_STRING_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Hash function name for unique ID to register.
 *
 * \param string [IN]           string name
 *
 * \return Non-negative ID that corresponds to string name
 */
static unsigned int
rv_hash_string(void *string)
{
    /* This is the djb2 string hash function */

    unsigned int         result = 5381;
    const unsigned char *p;

    p = (const unsigned char *)string;

    while (*p != '\0') {
        result = (result << 5) + result + *p;
        ++p;
    }
    return result;
}

#ifdef __cplusplus
}
#endif

#endif /* RV_HASH_STRING_H */
