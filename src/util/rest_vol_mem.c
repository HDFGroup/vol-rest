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
 * Wrapper functions around the standard library memory functions which can be
 * used to track memory usage within the REST VOL for debugging purposes.
 */

#include "rest_vol_mem.h"

#ifdef RV_TRACK_MEM_USAGE

/*-------------------------------------------------------------------------
 * Function:    RV_malloc_debug
 *
 * Purpose:     Similar to the C89 version of malloc().
 *
 *              On size of 0, we return a NULL pointer instead of the
 *              standard-allowed 'special' pointer since that's more
 *              difficult to check as a return value. This is still
 *              considered an error condition since allocations of zero
 *              bytes usually indicate problems.
 *
 * Return:      Success:    Pointer to new memory
 *              Failure:    NULL
 *
 * Programmer:  Jordan Henderson
 *              Nov 8, 2017
 */
void *
RV_malloc_debug(size_t size)
{
    void *ret_value = NULL;

    if (size) {
#ifdef RV_TRACK_MEM_USAGE
        size_t block_size = size;

        /* Keep track of the allocated size */
        if (NULL != (ret_value = malloc(size + sizeof(block_size)))) {
            memcpy(ret_value, &block_size, sizeof(block_size));
            ret_value = (char *)ret_value + sizeof(block_size);

            H5_rest_curr_alloc_bytes += size;
        } /* end if */
#else
        ret_value = malloc(size);
#endif
    } /* end if */
    else
        ret_value = NULL;

    return ret_value;
} /* end RV_malloc_debug() */

/*-------------------------------------------------------------------------
 * Function:    RV_calloc_debug
 *
 * Purpose:     Similar to the C89 version of calloc(), except this
 *              routine just takes a 'size' parameter.
 *
 *              On size of 0, we return a NULL pointer instead of the
 *              standard-allowed 'special' pointer since that's more
 *              difficult to check as a return value. This is still
 *              considered an error condition since allocations of zero
 *              bytes usually indicate problems.
 *
 * Return:      Success:    Pointer to new memory
 *              Failure:    NULL
 *
 * Programmer:  Jordan Henderson
 *              Nov 8, 2017
 */
void *
RV_calloc_debug(size_t size)
{
    void *ret_value = NULL;

    if (size) {
#ifdef RV_TRACK_MEM_USAGE
        if (NULL != (ret_value = RV_malloc(size)))
            memset(ret_value, 0, size);
#else
        ret_value = calloc(1, size);
#endif
    } /* end if */
    else
        ret_value = NULL;

    return ret_value;
} /* end RV_calloc_debug() */

/*-------------------------------------------------------------------------
 * Function:    RV_realloc_debug
 *
 * Purpose:     Similar semantics as C89's realloc(). Specifically, the
 *              following calls are equivalent:
 *
 *              RV_realloc_debug(NULL, size)  <==> RV_realloc_debug(size)
 *              RV_realloc_debug(ptr, 0)      <==> RV_realloc_debug(ptr)
 *              RV_realloc_debug(NULL, 0)     <==> NULL
 *
 *              Note that the (NULL, 0) combination is undefined behavior
 *              in the C standard.
 *
 * Return:      Success:    Ptr to new memory if size > 0
 *                          NULL if size is zero
 *              Failure:    NULL (input buffer is unchanged on failure)
 *
 * Programmer:  Jordan Henderson
 *              Nov 8, 2017
 */
void *
RV_realloc_debug(void *mem, size_t size)
{
    void *ret_value = NULL;

    if (!(NULL == mem && 0 == size)) {
#ifdef RV_TRACK_MEM_USAGE
        if (size > 0) {
            if (mem) {
                size_t block_size;

                memcpy(&block_size, (char *)mem - sizeof(block_size), sizeof(block_size));

                ret_value = RV_malloc(size);
                memcpy(ret_value, mem, size < block_size ? size : block_size);
                RV_free(mem);
            } /* end if */
            else
                ret_value = RV_malloc(size);
        } /* end if */
        else
            ret_value = RV_free(mem);
#else
        ret_value = realloc(mem, size);

        if (0 == size)
            ret_value = NULL;
#endif
    } /* end if */

    return ret_value;
} /* end RV_realloc_debug() */

/*-------------------------------------------------------------------------
 * Function:    RV_free_debug
 *
 * Purpose:     Just like free(3) except null pointers are allowed as
 *              arguments, and the return value (always NULL) can be
 *              assigned to the pointer whose memory was just freed:
 *
 *              thing = rest_free (thing);
 *
 * Return:      Success:    NULL
 *              Failure:    never fails
 *
 * Programmer:  Jordan Henderson
 *              Nov 8, 2017
 */
void *
RV_free_debug(void *mem)
{
    if (mem) {
#ifdef RV_TRACK_MEM_USAGE
        size_t block_size;

        memcpy(&block_size, (char *)mem - sizeof(block_size), sizeof(block_size));
        H5_rest_curr_alloc_bytes -= block_size;

        free((char *)mem - sizeof(block_size));
#else
        free(mem);
#endif
    } /* end if */

    return NULL;
} /* end RV_free_debug() */
#endif
