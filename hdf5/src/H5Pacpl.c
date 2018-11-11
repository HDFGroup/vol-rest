/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the root of the source code       *
 * distribution tree, or in https://support.hdfgroup.org/ftp/HDF5/releases.  *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*-------------------------------------------------------------------------
 *
 * Created:     H5Pacpl.c
 *
 * Purpose:     Attribute creation property list class routines
 *
 *-------------------------------------------------------------------------
 */

/****************/
/* Module Setup */
/****************/

#include "H5Pmodule.h"          /* This source code file is part of the H5P module */


/***********/
/* Headers */
/***********/
#include "H5private.h"          /* Generic Functions                        */
#include "H5Eprivate.h"         /* Error handling                           */
#include "H5Ppkg.h"             /* Property lists                           */
#include "H5VLprivate.h"        /* Virtual Object Layer                     */


/****************/
/* Local Macros */
/****************/

/* Definitions for locations parameters */
#define H5A_CRT_LOCATION_SIZE   sizeof(H5VL_loc_params_t)
#define H5A_CRT_LOCATION_DEF    H5I_BADID


/******************/
/* Local Typedefs */
/******************/


/********************/
/* Package Typedefs */
/********************/


/********************/
/* Local Prototypes */
/********************/

/* Property class callbacks */
static herr_t H5P_acrt_reg_prop(H5P_genclass_t *pclass);


/*********************/
/* Package Variables */
/*********************/

/* Attribute creation property list class library initialization object */
const H5P_libclass_t H5P_CLS_ACRT[1] = {{
    "attribute create",             /* Class name for debugging                 */
    H5P_TYPE_ATTRIBUTE_CREATE,      /* Class type                               */

    &H5P_CLS_STRING_CREATE_g,       /* Parent class                             */
    &H5P_CLS_ATTRIBUTE_CREATE_g,    /* Pointer to class                         */
    &H5P_CLS_ATTRIBUTE_CREATE_ID_g, /* Pointer to class ID                      */
    &H5P_LST_ATTRIBUTE_CREATE_ID_g, /* Pointer to default property list ID      */
    H5P_acrt_reg_prop,              /* Default property registration routine    */

    NULL,                           /* Class creation callback                  */
    NULL,                           /* Class creation callback info             */
    NULL,                           /* Class copy callback                      */
    NULL,                           /* Class copy callback info                 */
    NULL,                           /* Class close callback                     */
    NULL                            /* Class close callback info                */
}};


/*****************************/
/* Library Private Variables */
/*****************************/


/*-------------------------------------------------------------------------
 * Function:    H5P_acrt_reg_prop
 *
 * Purpose:     Register the attribute creation property list class's properties
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5P_acrt_reg_prop(H5P_genclass_t *pclass)
{
    hid_t               type_id = H5I_INVALID_HID;
    hid_t               space_id = H5I_INVALID_HID;
    H5VL_loc_params_t   loc_params;
    herr_t              ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    /* Register the type ID property*/
    if(H5P__register_real(pclass, H5VL_PROP_ATTR_TYPE_ID, sizeof(hid_t), &type_id, 
                         NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTINSERT, FAIL, "can't insert property into class")

    /* Register the space ID property */
    if(H5P__register_real(pclass, H5VL_PROP_ATTR_SPACE_ID, sizeof(hid_t), &space_id, 
                         NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTINSERT, FAIL, "can't insert property into class")

    /* Register the lcpl ID property */
    HDmemset(&loc_params, 0, sizeof(H5VL_loc_params_t));
    loc_params.obj_type = H5A_CRT_LOCATION_DEF;
    if(H5P__register_real(pclass, H5VL_PROP_ATTR_LOC_PARAMS, H5A_CRT_LOCATION_SIZE, &loc_params, 
                         NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTINSERT, FAIL, "can't insert property into class")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5P_acrt_reg_prop() */

