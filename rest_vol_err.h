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
 *              December, 2017
 *
 * Purpose: Contains macros to facilitate error reporting for the REST VOL plugin.
 */

#ifndef REST_VOL_ERR_H_
#define REST_VOL_ERR_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "H5Epublic.h"
#include "H5Ipublic.h"

extern hid_t h5_err_class_g;


/* Use FUNC to safely handle variations of C99 __func__ keyword handling */
#ifdef H5_HAVE_C99_FUNC
#define FUNC __func__
#elif defined(H5_HAVE_FUNCTION)
#define FUNC __FUNCTION__
#else
#error "We need __func__ or __FUNCTION__ to test function names!"
#endif


#define PRINT_ERR_STACK(err_major, ret_val)                                                                  \
{                                                                                                            \
    ssize_t num_errors;                                                                                      \
    if ((num_errors = H5Eget_num(H5P_DEFAULT)) < 0)                                                          \
        FUNC_DONE_ERROR(err_major, H5E_CANTGET, ret_val, "can't retrieve number of messages on error stack") \
    else                                                                                                     \
        if (num_errors)                                                                                      \
            H5Eprint2(H5P_DEFAULT, NULL);                                                                    \
}


/* Macro to push the current function to the current error stack
 * and then goto the "done" label, which should appear inside
 * the function
 */
#define FUNC_GOTO_ERROR(err_major, err_minor, ret_val, ...)                                                  \
{                                                                                                            \
    H5Epush2(H5P_DEFAULT, __FILE__, FUNC, __LINE__, h5_err_class_g, err_major, err_minor, __VA_ARGS__);      \
    ret_value = ret_val;                                                                                     \
    goto done;                                                                                               \
}

/* Macro to push the current function to the current error stack
 * without calling goto. This is used for handling the case where
 * an error occurs during cleanup past the "done" label inside a
 * function so that an infinite loop does not occur where goto
 * continually branches back to the label.
 */
#define FUNC_DONE_ERROR(err_major, err_minor, ret_val, ...)                                                  \
{                                                                                                            \
    H5Epush2(H5P_DEFAULT, __FILE__, FUNC, __LINE__, h5_err_class_g, err_major, err_minor, __VA_ARGS__);      \
    ret_value = ret_val;                                                                                     \
}

#define FUNC_GOTO_DONE(ret_val)                                                                              \
{                                                                                                            \
    ret_value = ret_val;                                                                                     \
    goto done;                                                                                               \
}


/* Error handling macros for the REST VOL test suite */

/*
 * Print the current location on the standard output stream.
 */
#define AT()     printf ("   at %s:%d in %s()...\n",        \
        __FILE__, __LINE__, FUNC);

/*
 * The name of the test is printed by saying TESTING("something") which will
 * result in the string `Testing something' being flushed to standard output.
 * If a test passes, fails, or is skipped then the PASSED(), H5_FAILED(), or
 * SKIPPED() macro should be called.  After H5_FAILED() or SKIPPED() the caller
 * should print additional information to stdout indented by at least four
 * spaces.  If the h5_errors() is used for automatic error handling then
 * the H5_FAILED() macro is invoked automatically when an API function fails.
 */
#define TESTING(S)  {printf("Testing %-62s", S); fflush(stdout);}
#define PASSED()    {puts("PASSED"); fflush(stdout);}
#define H5_FAILED() {puts("*FAILED*"); fflush(stdout);}
#define SKIPPED()   {puts(" - SKIPPED -"); fflush(stdout);}
#define TEST_ERROR  {H5_FAILED(); AT(); goto error;}

#ifdef __cplusplus
}
#endif

#endif /* REST_VOL_ERR_H_ */
