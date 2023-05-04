/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of the HDF5 REST VOL connector. The full copyright      *
 * notice, including terms governing use, modification, and redistribution,  *
 * is contained in the COPYING file, which can be found at the root of the   *
 * source code distribution tree.                                            *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef REST_VOL_MEM_H_
#define REST_VOL_MEM_H_

#include "rest_vol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Macros to default directly to standard library functions when REST VOL
 * memory tracking isn't enabled.
 */
#ifdef RV_TRACK_MEM_USAGE
/* Internal malloc/free functions to track memory usage for debugging purposes */
void *RV_malloc_debug(size_t size);
void *RV_calloc_debug(size_t size);
void *RV_realloc_debug(void *mem, size_t size);
void *RV_free_debug(void *mem);

#define RV_malloc(size)       RV_malloc_debug(size)
#define RV_calloc(size)       RV_malloc_debug(size)
#define RV_realloc(mem, size) RV_realloc_debug(mem, size)
#define RV_free(mem)          RV_free_debug(mem)
#else
#define RV_malloc(size)       malloc(size)
#define RV_calloc(size)       calloc(1, size)
#define RV_realloc(mem, size) realloc(mem, size)
#define RV_free(mem)          free(mem)
#endif

/* Macro to check whether the size of a buffer matches the given target size
 * and reallocate the buffer if it is too small, keeping track of a given
 * pointer into the buffer. This is used when doing multiple formatted
 * prints to the same buffer. A pointer into the buffer is kept and
 * incremented so that the next print operation can continue where the
 * last one left off, and not overwrite the current contents of the buffer.
 */
#define CHECKED_REALLOC(buffer, buffer_len, target_size, ptr_to_buffer, ERR_MAJOR, ret_value)                \
    while (target_size > buffer_len) {                                                                       \
        char *tmp_realloc;                                                                                   \
                                                                                                             \
        if (NULL == (tmp_realloc = (char *)RV_realloc(buffer, 2 * buffer_len))) {                            \
            RV_free(buffer);                                                                                 \
            buffer = NULL;                                                                                   \
            FUNC_GOTO_ERROR(ERR_MAJOR, H5E_CANTALLOC, ret_value, "can't allocate space");                    \
        } /* end if */                                                                                       \
                                                                                                             \
        /* Place the "current position" pointer at the correct spot in the new buffer */                     \
        if (ptr_to_buffer)                                                                                   \
            ptr_to_buffer = tmp_realloc + ((char *)ptr_to_buffer - buffer);                                  \
        buffer = tmp_realloc;                                                                                \
        buffer_len *= 2;                                                                                     \
    }

/* Helper macro to call the above with a temporary useless variable, since directly passing
 * NULL to the macro generates invalid code
 */
#define CHECKED_REALLOC_NO_PTR(buffer, buffer_len, target_size, ERR_MAJOR, ret_value)                        \
    do {                                                                                                     \
        char *tmp = NULL;                                                                                    \
        CHECKED_REALLOC(buffer, buffer_len, target_size, tmp, ERR_MAJOR, ret_value);                         \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* REST_VOL_MEM_H_ */
