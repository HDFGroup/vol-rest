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
 * Created:     H5O.c
 *
 * Purpose:     Public object header routines
 *
 *-------------------------------------------------------------------------
 */

/****************/
/* Module Setup */
/****************/

#include "H5Omodule.h"          /* This source code file is part of the H5O module */


/***********/
/* Headers */
/***********/
#include "H5private.h"          /* Generic Functions                        */
#include "H5Eprivate.h"         /* Error handling                           */
#include "H5Fprivate.h"         /* File access                              */
#include "H5Iprivate.h"         /* IDs                                      */
#include "H5Lprivate.h"         /* Links                                    */
#include "H5Opkg.h"             /* Object headers                           */


/****************/
/* Local Macros */
/****************/


/******************/
/* Local Typedefs */
/******************/


/********************/
/* Package Typedefs */
/********************/


/********************/
/* Local Prototypes */
/********************/


/*********************/
/* Package Variables */
/*********************/


/*****************************/
/* Library Private Variables */
/*****************************/


/*******************/
/* Local Variables */
/*******************/


/*-------------------------------------------------------------------------
 * Function:    H5Oopen
 *
 * Purpose:     Opens an object within an HDF5 file.
 *
 *              This function opens an object in the same way that H5Gopen2,
 *              H5Topen2, and H5Dopen2 do. However, H5Oopen doesn't require
 *              the type of object to be known beforehand. This can be
 *              useful in user-defined links, for instance, when only a
 *              path is known.
 *
 *              The opened object should be closed again with H5Oclose
 *              or H5Gclose, H5Tclose, or H5Dclose.
 *
 * Return:      Success:    An open object identifier
 *
 *              Failure:    H5I_INVALID_HID
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5Oopen(hid_t loc_id, const char *name, hid_t lapl_id)
{
    H5VL_object_t  *obj         = NULL;                     /* Object token of loc_id */
    H5I_type_t      opened_type;
    void           *opened_obj  = NULL;
    H5VL_loc_params_t loc_params;
    hid_t           dxpl_id     = H5AC_ind_read_dxpl_id;    /* dxpl used by library */
    hid_t           ret_value   = H5I_INVALID_HID;

    FUNC_ENTER_API(H5I_INVALID_HID)
    H5TRACE3("i", "i*si", loc_id, name, lapl_id);

    /* Check args */
    if (!name)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, H5I_INVALID_HID, "name parameter cannot be NULL")
    if (!*name)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, H5I_INVALID_HID, "name parameter cannot be an empty string")

    /* Verify access property list and get correct dxpl */
    if (H5P_verify_apl_and_dxpl(&lapl_id, H5P_CLS_LACC, &dxpl_id, loc_id, FALSE) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_CANTSET, H5I_INVALID_HID, "can't set access and transfer property lists")

    /* Get the location object */
    if (NULL == (obj = (H5VL_object_t *)H5I_object(loc_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, H5I_INVALID_HID, "invalid location identifier")

    /* Set location struct fields */
    loc_params.type                         = H5VL_OBJECT_BY_NAME;
    loc_params.loc_data.loc_by_name.name    = name;
    loc_params.loc_data.loc_by_name.lapl_id = lapl_id;
    loc_params.obj_type                     = H5I_get_type(loc_id);

    /* Open the object through the VOL */
    if (NULL == (opened_obj = H5VL_object_open(obj->vol_obj, loc_params, obj->vol_info->vol_cls, 
                                              &opened_type, dxpl_id, H5_REQUEST_NULL)))
        HGOTO_ERROR(H5E_OHDR, H5E_CANTOPENOBJ, H5I_INVALID_HID, "unable to open object")

    /* Get an atom for the object */
    if ((ret_value = H5VL_register_id(opened_type, opened_obj, obj->vol_info, TRUE)) < 0)
        HGOTO_ERROR(H5E_ATOM, H5E_CANTREGISTER, H5I_INVALID_HID, "unable to atomize object handle")
done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Oopen() */


/*-------------------------------------------------------------------------
 * Function:	H5Oopen_by_idx
 *
 * Purpose:	Opens an object within an HDF5 file, according to the offset
 *              within an index.
 *
 *              This function opens an object in the same way that H5Gopen,
 *              H5Topen, and H5Dopen do. However, H5Oopen doesn't require
 *              the type of object to be known beforehand. This can be
 *              useful in user-defined links, for instance, when only a
 *              path is known.
 *
 *              The opened object should be closed again with H5Oclose
 *              or H5Gclose, H5Tclose, or H5Dclose.
 *
 * Return:	Success:	An open object identifier
 *		Failure:	H5I_INVALID_HID
 *
 * Programmer:	Quincey Koziol
 *		November 20 2006
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5Oopen_by_idx(hid_t loc_id, const char *group_name, H5_index_t idx_type,
    H5_iter_order_t order, hsize_t n, hid_t lapl_id)
{
    H5VL_object_t *obj = NULL;        /* object token of loc_id */
    H5I_type_t opened_type;
    void *opened_obj = NULL;
    H5VL_loc_params_t loc_params;
    hid_t dxpl_id = H5AC_ind_read_dxpl_id; /* dxpl used by library */
    hid_t ret_value = H5I_INVALID_HID;

    FUNC_ENTER_API(H5I_INVALID_HID)
    H5TRACE6("i", "i*sIiIohi", loc_id, group_name, idx_type, order, n, lapl_id);

    /* Check args */
    if(!group_name || !*group_name)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, H5I_INVALID_HID, "no name specified")
    if(idx_type <= H5_INDEX_UNKNOWN || idx_type >= H5_INDEX_N)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, H5I_INVALID_HID, "invalid index type specified")
    if(order <= H5_ITER_UNKNOWN || order >= H5_ITER_N)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, H5I_INVALID_HID, "invalid iteration order specified")

    /* Verify access property list and get correct dxpl */
    if(H5P_verify_apl_and_dxpl(&lapl_id, H5P_CLS_LACC, &dxpl_id, loc_id, FALSE) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_CANTSET, H5I_INVALID_HID, "can't set access and transfer property lists")

    loc_params.type = H5VL_OBJECT_BY_IDX;
    loc_params.loc_data.loc_by_idx.name = group_name;
    loc_params.loc_data.loc_by_idx.idx_type = idx_type;
    loc_params.loc_data.loc_by_idx.order = order;
    loc_params.loc_data.loc_by_idx.n = n;
    loc_params.loc_data.loc_by_idx.lapl_id = lapl_id;
    loc_params.obj_type = H5I_get_type(loc_id);

    /* get the location object */
    if(NULL == (obj = (H5VL_object_t *)H5I_object(loc_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, H5I_INVALID_HID, "invalid location identifier")

    /* Open the object through the VOL */
    if(NULL == (opened_obj = H5VL_object_open(obj->vol_obj, loc_params, obj->vol_info->vol_cls, 
                                              &opened_type, dxpl_id, H5_REQUEST_NULL)))
        HGOTO_ERROR(H5E_OHDR, H5E_CANTOPENOBJ, H5I_INVALID_HID, "unable to open object")

    if((ret_value = H5VL_register_id(opened_type, opened_obj, obj->vol_info, TRUE)) < 0)
        HGOTO_ERROR(H5E_ATOM, H5E_CANTREGISTER, H5I_INVALID_HID, "unable to atomize object handle")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Oopen_by_idx() */


/*-------------------------------------------------------------------------
 * Function:	H5Oopen_by_addr
 *
 * Purpose:	Warning! This function is EXTREMELY DANGEROUS!
 *              Improper use can lead to FILE CORRUPTION, INACCESSIBLE DATA,
 *              and other VERY BAD THINGS!
 *
 *              This function opens an object using its address within the
 *              HDF5 file, similar to an HDF5 hard link. The open object
 *              is identical to an object opened with H5Oopen() and should
 *              be closed with H5Oclose() or a type-specific closing
 *              function (such as H5Gclose() ).
 *
 *              This function is very dangerous if called on an invalid
 *              address. For this reason, H5Oincr_refcount() should be
 *              used to prevent HDF5 from deleting any object that is
 *              referenced by address (e.g. by a user-defined link).
 *              H5Odecr_refcount() should be used when the object is
 *              no longer being referenced by address (e.g. when the UD link
 *              is deleted).
 *
 *              The address of the HDF5 file on disk has no effect on
 *              H5Oopen_by_addr(), nor does the use of any unusual file
 *              drivers. The "address" is really the offset within the
 *              HDF5 file, and HDF5's file drivers will transparently
 *              map this to an address on disk for the filesystem.
 *
 * Return:	Success:	An open object identifier
 *		Failure:	H5I_INVALID_HID
 *
 * Programmer:	James Laird
 *		July 14 2006
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5Oopen_by_addr(hid_t loc_id, haddr_t addr)
{
    H5VL_object_t *obj = NULL;        /* object token of loc_id */
    H5I_type_t opened_type;
    void *opened_obj = NULL;
    H5VL_loc_params_t loc_params;
    hid_t ret_value = H5I_INVALID_HID;

    FUNC_ENTER_API(H5I_INVALID_HID)
    H5TRACE2("i", "ia", loc_id, addr);

    loc_params.type = H5VL_OBJECT_BY_ADDR;
    loc_params.loc_data.loc_by_addr.addr = addr;
    loc_params.obj_type = H5I_get_type(loc_id);

    /* get the location object */
    if(NULL == (obj = (H5VL_object_t *)H5I_object(loc_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, H5I_INVALID_HID, "invalid location identifier")

    /* Open the object through the VOL */
    if(NULL == (opened_obj = H5VL_object_open(obj->vol_obj, loc_params, obj->vol_info->vol_cls, &opened_type, 
                                              H5AC_ind_read_dxpl_id, H5_REQUEST_NULL)))
        HGOTO_ERROR(H5E_OHDR, H5E_CANTOPENOBJ, H5I_INVALID_HID, "unable to open object")

    if((ret_value = H5VL_register_id(opened_type, opened_obj, obj->vol_info, TRUE)) < 0)
        HGOTO_ERROR(H5E_ATOM, H5E_CANTREGISTER, H5I_INVALID_HID, "unable to atomize object handle")

done:

    FUNC_LEAVE_API(ret_value)
} /* end H5Oopen_by_addr() */


/*-------------------------------------------------------------------------
 * Function:	H5Olink
 *
 * Purpose:	Creates a hard link from NEW_NAME to the object specified
 *		by OBJ_ID using properties defined in the Link Creation
 *              Property List LCPL.
 *
 *		This function should be used to link objects that have just
 *              been created.
 *
 *		NEW_NAME is interpreted relative to
 *		NEW_LOC_ID, which is either a file ID or a
 *		group ID.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	James Laird
 *              Tuesday, December 13, 2005
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Olink(hid_t obj_id, hid_t new_loc_id, const char *new_name, hid_t lcpl_id,
    hid_t lapl_id)
{
    H5VL_object_t    *obj1 = NULL;        /* object token of obj_id */
    H5VL_object_t    *obj2 = NULL;        /* object token of new_loc_id */
    H5VL_loc_params_t loc_params1;
    H5VL_loc_params_t loc_params2;
    H5P_genplist_t *plist;      /* Property list pointer */
    hid_t dxpl_id = H5AC_ind_read_dxpl_id; /* dxpl used by library */
    herr_t         ret_value = SUCCEED;       /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE5("e", "ii*sii", obj_id, new_loc_id, new_name, lcpl_id, lapl_id);

    /* Check arguments */
    if(new_loc_id == H5L_SAME_LOC)
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "cannot use H5L_SAME_LOC when only one location is specified")
    if(!new_name || !*new_name)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "no name specified")
/* Avoid compiler warning on 32-bit machines */
#if H5_SIZEOF_SIZE_T > H5_SIZEOF_INT32_T
    if(HDstrlen(new_name) > H5L_MAX_LINK_NAME_LEN)
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, FAIL, "name too long")
#endif /* H5_SIZEOF_SIZE_T > H5_SIZEOF_INT32_T */
    if(lcpl_id != H5P_DEFAULT && (TRUE != H5P_isa_class(lcpl_id, H5P_LINK_CREATE)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a link creation property list")

    /* Check the group access property list */
    if(H5P_DEFAULT == lcpl_id)
        lcpl_id = H5P_LINK_CREATE_DEFAULT;

    /* Verify access property list and get correct dxpl */
    if(H5P_verify_apl_and_dxpl(&lapl_id, H5P_CLS_LACC, &dxpl_id, obj_id, TRUE) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_CANTSET, FAIL, "can't set access and transfer property lists")

    loc_params1.type = H5VL_OBJECT_BY_SELF;
    loc_params1.obj_type = H5I_get_type(obj_id);

    loc_params2.type = H5VL_OBJECT_BY_NAME;
    loc_params2.obj_type = H5I_get_type(new_loc_id);
    loc_params2.loc_data.loc_by_name.name = new_name;
    loc_params2.loc_data.loc_by_name.lapl_id = lapl_id;

    if(H5L_SAME_LOC != obj_id) {
        /* get the location object */
        if(NULL == (obj1 = H5VL_get_object(obj_id)))
            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid location identifier")
    }
    if(H5L_SAME_LOC != new_loc_id) {
        /* get the location object */
        if(NULL == (obj2 = H5VL_get_object(new_loc_id)))
            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid location identifier")
    }
    /* Make sure that the VOL plugins are the same */
    if(obj1 && obj2) {
        if (obj1->vol_info->vol_cls->value != obj2->vol_info->vol_cls->value)
            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "Objects are accessed through different VOL plugins and can't be linked")
    }

    /* Get the plist structure */
    if(NULL == (plist = (H5P_genplist_t *)H5I_object(lcpl_id)))
        HGOTO_ERROR(H5E_ATOM, H5E_BADATOM, FAIL, "can't find object for ID")

    /* set creation properties */
    if(H5P_set(plist, H5VL_PROP_LINK_TARGET, &obj1->vol_obj) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't set property value for target id")
    if(H5P_set(plist, H5VL_PROP_LINK_TARGET_LOC_PARAMS, &loc_params1) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't set property value for target id")

    /* Create the link through the VOL */
    if(H5VL_link_create(H5VL_LINK_CREATE_HARD, obj2->vol_obj, loc_params2, 
                        (obj1!=NULL ? obj1->vol_info->vol_cls : obj2->vol_info->vol_cls),
                        lcpl_id, lapl_id, dxpl_id, H5_REQUEST_NULL) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTINIT, FAIL, "unable to create link")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Olink() */


/*-------------------------------------------------------------------------
 * Function:	H5Oincr_refcount
 *
 * Purpose:	Warning! This function is EXTREMELY DANGEROUS!
 *              Improper use can lead to FILE CORRUPTION, INACCESSIBLE DATA,
 *              and other VERY BAD THINGS!
 *
 *              This function increments the "hard link" reference count
 *              for an object. It should be used when a user-defined link
 *              that references an object by address is created. When the
 *              link is deleted, H5Odecr_refcount should be used.
 *
 * Return:	Success:	Non-negative
 *		Failure:	Negative
 *
 * Programmer:	James Laird
 *		July 14 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Oincr_refcount(hid_t object_id)
{
    H5VL_object_t    *obj = NULL;        /* object token of loc_id */
    H5VL_loc_params_t loc_params;
    herr_t      ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE1("e", "i", object_id);

    loc_params.type = H5VL_OBJECT_BY_SELF;
    loc_params.obj_type = H5I_get_type(object_id);

    /* get the location object */
    if(NULL == (obj = H5VL_get_object(object_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid location identifier")

    /* change the ref count through the VOL */
    if(H5VL_object_specific(obj->vol_obj, loc_params, obj->vol_info->vol_cls, H5VL_OBJECT_CHANGE_REF_COUNT, 
                            H5AC_ind_read_dxpl_id, H5_REQUEST_NULL, 1) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_LINKCOUNT, FAIL, "modifying object link count failed")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5O_incr_refcount() */


/*-------------------------------------------------------------------------
 * Function:	H5Odecr_refcount
 *
 * Purpose:	Warning! This function is EXTREMELY DANGEROUS!
 *              Improper use can lead to FILE CORRUPTION, INACCESSIBLE DATA,
 *              and other VERY BAD THINGS!
 *
 *              This function decrements the "hard link" reference count
 *              for an object. It should be used when user-defined links
 *              that reference an object by address are deleted, and only
 *              after H5Oincr_refcount has already been used.
 *
 * Return:	Success:	Non-negative
 *		Failure:	Negative
 *
 * Programmer:	James Laird
 *		July 14 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Odecr_refcount(hid_t object_id)
{
    H5VL_object_t    *obj = NULL;        /* object token of loc_id */
    H5VL_loc_params_t loc_params;
    herr_t      ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE1("e", "i", object_id);

    loc_params.type = H5VL_OBJECT_BY_SELF;
    loc_params.obj_type = H5I_get_type(object_id);

    /* get the location object */
    if(NULL == (obj = H5VL_get_object(object_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid location identifier")

    /* change the ref count through the VOL */
    if(H5VL_object_specific(obj->vol_obj, loc_params, obj->vol_info->vol_cls, H5VL_OBJECT_CHANGE_REF_COUNT, 
                            H5AC_ind_read_dxpl_id, H5_REQUEST_NULL, -1) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_LINKCOUNT, FAIL, "modifying object link count failed")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Odecr_refcount() */


/*-------------------------------------------------------------------------
 * Function:    H5Oexists_by_name
 *
 * Purpose:     Determine if a linked-to object exists
 *
 * Return:      Success:    TRUE/FALSE
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
htri_t
H5Oexists_by_name(hid_t loc_id, const char *name, hid_t lapl_id)
{
    H5VL_object_t  *obj         = NULL;         /* Object token of loc_id */
    H5VL_loc_params_t loc_params;
    hid_t           dxpl_id     = H5AC_ind_read_dxpl_id; /* dxpl used by library */
    htri_t          ret_value   = -1;           /* Return value */

    FUNC_ENTER_API((-1))
    H5TRACE3("t", "i*si", loc_id, name, lapl_id);

    /* Check args */
    if (!name)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, (-1), "name parameter cannot be NULL")
    if (!*name)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, (-1), "name parameter cannot be an empty string")

    /* Verify access property list and get correct dxpl */
    if (H5P_verify_apl_and_dxpl(&lapl_id, H5P_CLS_LACC, &dxpl_id, loc_id, FALSE) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_CANTSET, (-1), "can't set access and transfer property lists")

    /* Get the location object */
    if (NULL == (obj = H5VL_get_object(loc_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, (-1), "invalid location identifier")

    /* Set the location struct fields */
    loc_params.type                         = H5VL_OBJECT_BY_NAME;
    loc_params.loc_data.loc_by_name.name    = name;
    loc_params.loc_data.loc_by_name.lapl_id = lapl_id;
    loc_params.obj_type                     = H5I_get_type(loc_id);

    /* Check if the object exists via the VOL */
    if (H5VL_object_specific(obj->vol_obj, loc_params, obj->vol_info->vol_cls, H5VL_OBJECT_EXISTS, 
                            dxpl_id, H5_REQUEST_NULL, &ret_value) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_CANTGET, (-1), "unable to determine if '%s' exists", name)

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Oexists_by_name() */


/*-------------------------------------------------------------------------
 * Function:    H5Oget_info
 *
 * Purpose:     Retrieve information about an object.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Oget_info(hid_t loc_id, H5O_info_t *oinfo)
{
    H5VL_object_t      *obj         = NULL;         /* Object token of loc_id */
    H5VL_loc_params_t   loc_params;
    herr_t              ret_value   = SUCCEED;      /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE2("e", "i*x", loc_id, oinfo);

    /* Check args */
    if (!oinfo)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "oinfo parameter cannot be NULL")

    /* Set location struct fields */
    loc_params.type         = H5VL_OBJECT_BY_SELF;
    loc_params.obj_type     = H5I_get_type(loc_id);

    /* Get the location object */
    if (NULL == (obj = H5VL_get_object(loc_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid location identifier")

    /* Get the group info through the VOL using the location token */
    if (H5VL_object_optional(obj->vol_obj, obj->vol_info->vol_cls, H5AC_ind_read_dxpl_id, 
                            H5_REQUEST_NULL, H5VL_OBJECT_GET_INFO, loc_params, oinfo) < 0)
        HGOTO_ERROR(H5E_INTERNAL, H5E_CANTGET, FAIL, "unable to get group info")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Oget_info() */


/*-------------------------------------------------------------------------
 * Function:    H5Oget_info_by_name
 *
 * Purpose:     Retrieve information about an object.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Oget_info_by_name(hid_t loc_id, const char *name, H5O_info_t *oinfo, hid_t lapl_id)
{
    H5VL_object_t    *obj = NULL;        /* object token of loc_id */
    H5VL_loc_params_t loc_params;
    hid_t       dxpl_id = H5AC_ind_read_dxpl_id; /* dxpl used by library */
    herr_t      ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE4("e", "i*s*xi", loc_id, name, oinfo, lapl_id);

    /* Check args */
    if (!name)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "name parameter cannot be NULL")
    if (!*name)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "name parameter cannot be an empty string")
    if (!oinfo)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "oinfo parameter cannot be NULL")

    /* Verify access property list and get correct dxpl */
    if (H5P_verify_apl_and_dxpl(&lapl_id, H5P_CLS_LACC, &dxpl_id, loc_id, FALSE) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_CANTSET, FAIL, "can't set access and transfer property lists")

    /* Fill out location struct */
    loc_params.type                         = H5VL_OBJECT_BY_NAME;
    loc_params.loc_data.loc_by_name.name    = name;
    loc_params.loc_data.loc_by_name.lapl_id = lapl_id;
    loc_params.obj_type                     = H5I_get_type(loc_id);

    /* Get the location object */
    if (NULL == (obj = H5VL_get_object(loc_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid location identifier")

    /* Get the group info through the VOL using the location token */
    if (H5VL_object_optional(obj->vol_obj, obj->vol_info->vol_cls, dxpl_id, H5_REQUEST_NULL,
                            H5VL_OBJECT_GET_INFO, loc_params, oinfo) < 0)
        HGOTO_ERROR(H5E_INTERNAL, H5E_CANTGET, FAIL, "unable to get group info")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Oget_info_by_name() */


/*-------------------------------------------------------------------------
 * Function:	H5Oget_info_by_idx
 *
 * Purpose:	Retrieve information about an object, according to the order
 *              of an index.
 *
 * Return:	Success:	Non-negative
 *		Failure:	Negative
 *
 * Programmer:	Quincey Koziol
 *		November 26 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Oget_info_by_idx(hid_t loc_id, const char *group_name, H5_index_t idx_type,
    H5_iter_order_t order, hsize_t n, H5O_info_t *oinfo, hid_t lapl_id)
{
    H5VL_object_t    *obj = NULL;        /* object token of loc_id */
    H5VL_loc_params_t loc_params;
    hid_t       dxpl_id = H5AC_ind_read_dxpl_id; /* dxpl used by library */
    herr_t      ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE7("e", "i*sIiIoh*xi", loc_id, group_name, idx_type, order, n, oinfo,
             lapl_id);

    /* Check args */
    if(!group_name || !*group_name)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "no name specified")
    if(idx_type <= H5_INDEX_UNKNOWN || idx_type >= H5_INDEX_N)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid index type specified")
    if(order <= H5_ITER_UNKNOWN || order >= H5_ITER_N)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid iteration order specified")
    if(!oinfo)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "no info struct")

    /* Verify access property list and get correct dxpl */
    if(H5P_verify_apl_and_dxpl(&lapl_id, H5P_CLS_LACC, &dxpl_id, loc_id, FALSE) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_CANTSET, FAIL, "can't set access and transfer property lists")

    loc_params.type = H5VL_OBJECT_BY_IDX;
    loc_params.loc_data.loc_by_idx.name = group_name;
    loc_params.loc_data.loc_by_idx.idx_type = idx_type;
    loc_params.loc_data.loc_by_idx.order = order;
    loc_params.loc_data.loc_by_idx.n = n;
    loc_params.loc_data.loc_by_idx.lapl_id = lapl_id;
    loc_params.obj_type = H5I_get_type(loc_id);

    /* get the location object */
    if(NULL == (obj = H5VL_get_object(loc_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid location identifier")

    /* Get the group info through the VOL using the location token */
    if(H5VL_object_optional(obj->vol_obj, obj->vol_info->vol_cls, dxpl_id, H5_REQUEST_NULL, 
                            H5VL_OBJECT_GET_INFO, loc_params, oinfo) < 0)
        HGOTO_ERROR(H5E_INTERNAL, H5E_CANTGET, FAIL, "unable to get group info")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Oget_info_by_idx() */


/*-------------------------------------------------------------------------
 * Function:    H5Oset_comment
 *
 * Purpose:     Gives the specified object a comment.  The COMMENT string
 *              should be a null terminated string.  An object can have only
 *              one comment at a time.  Passing NULL for the COMMENT argument
 *              will remove the comment property from the object.
 *
 * Note:        Deprecated in favor of using attributes on objects
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Oset_comment(hid_t obj_id, const char *comment)
{
    H5VL_object_t    *obj = NULL;        /* object token of loc_id */
    H5VL_loc_params_t loc_params;
    herr_t      ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE2("e", "i*s", obj_id, comment);

    /* Get the location object */
    if (NULL == (obj = H5VL_get_object(obj_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid location identifier")

    /* Fill in location struct fields */
    loc_params.type = H5VL_OBJECT_BY_SELF;
    loc_params.obj_type = H5I_get_type(obj_id);

    /* set comment on object through the VOL */
    if (H5VL_object_optional(obj->vol_obj, obj->vol_info->vol_cls, H5AC_ind_read_dxpl_id, 
                            H5_REQUEST_NULL, H5VL_OBJECT_SET_COMMENT, loc_params, comment) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_CANTINIT, FAIL, "unable to set comment value")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Oset_comment() */


/*-------------------------------------------------------------------------
 * Function:    H5Oset_comment_by_name
 *
 * Purpose:     Gives the specified object a comment.  The COMMENT string
 *              should be a null terminated string.  An object can have only
 *              one comment at a time.  Passing NULL for the COMMENT argument
 *              will remove the comment property from the object.
 *
 * Note:        Deprecated in favor of using attributes on objects
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Oset_comment_by_name(hid_t loc_id, const char *name, const char *comment,
    hid_t lapl_id)
{
    H5VL_object_t    *obj = NULL;        /* object token of loc_id */
    H5VL_loc_params_t loc_params;
    hid_t       dxpl_id = H5AC_ind_read_dxpl_id; /* dxpl used by library */
    herr_t      ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE4("e", "i*s*si", loc_id, name, comment, lapl_id);

    /* Check args */
    if (!name || !*name)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "no name")

    /* Verify access property list and get correct dxpl */
    if (H5P_verify_apl_and_dxpl(&lapl_id, H5P_CLS_LACC, &dxpl_id, loc_id, TRUE) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_CANTSET, FAIL, "can't set access and transfer property lists")

    /* Fill in location struct fields */
    loc_params.type                         = H5VL_OBJECT_BY_NAME;
    loc_params.loc_data.loc_by_name.name    = name;
    loc_params.loc_data.loc_by_name.lapl_id = lapl_id;
    loc_params.obj_type                     = H5I_get_type(loc_id);

    /* get the location object */
    if (NULL == (obj = H5VL_get_object(loc_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid location identifier")

    /* Set comment on object through the VOL */
    if (H5VL_object_optional(obj->vol_obj, obj->vol_info->vol_cls, dxpl_id, H5_REQUEST_NULL, 
                            H5VL_OBJECT_SET_COMMENT, loc_params, comment) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_CANTINIT, FAIL, "unable to set comment value")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Oset_comment_by_name() */


/*-------------------------------------------------------------------------
 * Function:    H5Oget_comment
 *
 * Purpose:     Retrieve comment for an object.
 *
 * Return:      Success:    Number of bytes in the comment excluding the
 *                          null terminator. Zero if the object has no
 *                          comment.
 *
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
ssize_t
H5Oget_comment(hid_t obj_id, char *comment, size_t bufsize)
{
    H5VL_object_t    *obj = NULL;        /* object token of loc_id */
    H5VL_loc_params_t loc_params;
    ssize_t     ret_value = -1;              /* Return value */

    FUNC_ENTER_API((-1))
    H5TRACE3("Zs", "i*sz", obj_id, comment, bufsize);

    /* Get the object */
    if (NULL == (obj = H5VL_get_object(obj_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, (-1), "invalid location identifier")

    /* Set fields in the location struct */
    loc_params.type         = H5VL_OBJECT_BY_SELF;
    loc_params.obj_type     = H5I_get_type(obj_id);

    /* Get the comment via the VOL */
    if (H5VL_object_optional(obj->vol_obj, obj->vol_info->vol_cls, H5AC_ind_read_dxpl_id, H5_REQUEST_NULL, 
                            H5VL_OBJECT_GET_COMMENT, loc_params, comment, bufsize, &ret_value) < 0)
        HGOTO_ERROR(H5E_INTERNAL, H5E_CANTGET, (-1), "unable to get object comment")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Oget_comment() */


/*-------------------------------------------------------------------------
 * Function:    H5Oget_comment_by_name
 *
 * Purpose:     Retrieve comment for an object.
 *
 * Return:      Success:    Number of bytes in the comment excluding the
 *                          null terminator. Zero if the object has no
 *                          comment.
 *
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
ssize_t
H5Oget_comment_by_name(hid_t loc_id, const char *name, char *comment, size_t bufsize,
    hid_t lapl_id)
{
    H5VL_object_t    *obj = NULL;        /* object token of loc_id */
    H5VL_loc_params_t loc_params;
    hid_t       dxpl_id = H5AC_ind_read_dxpl_id; /* dxpl used by library */
    ssize_t     ret_value = -1;              /* Return value */

    FUNC_ENTER_API((-1))
    H5TRACE5("Zs", "i*s*szi", loc_id, name, comment, bufsize, lapl_id);

    /* Check args */
    if (!name || !*name)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, (-1), "no name")

    /* Verify access property list and get correct dxpl */
    if (H5P_verify_apl_and_dxpl(&lapl_id, H5P_CLS_LACC, &dxpl_id, loc_id, FALSE) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_CANTSET, (-1), "can't set access and transfer property lists")

    /* Fill in location struct fields */
    loc_params.type                         = H5VL_OBJECT_BY_NAME;
    loc_params.loc_data.loc_by_name.name    = name;
    loc_params.loc_data.loc_by_name.lapl_id = lapl_id;
    loc_params.obj_type                     = H5I_get_type(loc_id);

    /* Get the location object */
    if (NULL == (obj = H5VL_get_object(loc_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, (-1), "invalid location identifier")

    /* Get the comment via the VOL */
    if (H5VL_object_optional(obj->vol_obj, obj->vol_info->vol_cls, dxpl_id, H5_REQUEST_NULL, 
                            H5VL_OBJECT_GET_COMMENT, loc_params, comment, bufsize, &ret_value) < 0)
        HGOTO_ERROR(H5E_INTERNAL, H5E_CANTGET, (-1), "unable to get object info")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Oget_comment_by_name() */


/*-------------------------------------------------------------------------
 * Function:    H5Ovisit
 *
 * Purpose:     Recursively visit an object and all the objects reachable
 *              from it.  If the starting object is a group, all the objects
 *              linked to from that group will be visited.  Links within
 *              each group are visited according to the order within the
 *              specified index (unless the specified index does not exist for
 *              a particular group, then the "name" index is used).
 *
 *              NOTE: Soft links and user-defined links are ignored during
 *              this operation.
 *
 *              NOTE: Each _object_ reachable from the initial group will only
 *              be visited once.  If multiple hard links point to the same
 *              object, the first link to the object's path (according to the
 *              iteration index and iteration order given) will be used to in
 *              the callback about the object.
 *
 * Return:      Success:    The return value of the first operator that
 *                          returns non-zero, or zero if all members were
 *                          processed with no operator returning non-zero.
 *
 *              Failure:    Negative if something goes wrong within the
 *                          library, or the negative value returned by one
 *                          of the operators.
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Ovisit(hid_t obj_id, H5_index_t idx_type, H5_iter_order_t order,
    H5O_iterate_t op, void *op_data)
{
    H5VL_object_t      *obj         = NULL;     /* Object token of loc_id */
    H5VL_loc_params_t   loc_params;
    herr_t              ret_value;              /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE5("e", "iIiIox*x", obj_id, idx_type, order, op, op_data);

    /* Check args */
    if (idx_type <= H5_INDEX_UNKNOWN || idx_type >= H5_INDEX_N)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid index type specified")
    if (order <= H5_ITER_UNKNOWN || order >= H5_ITER_N)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid iteration order specified")
    if(!op)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "no callback operator specified")

    /* Get the location object */
    if (NULL == (obj = H5VL_get_object(obj_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid location identifier")

    /* Set location parameters */
    loc_params.type         = H5VL_OBJECT_BY_SELF;
    loc_params.obj_type     = H5I_get_type(obj_id);

    /* Iterate over the objects through the VOL */
    if ((ret_value = H5VL_object_specific(obj->vol_obj, loc_params, obj->vol_info->vol_cls, 
                                         H5VL_OBJECT_VISIT, H5AC_ind_read_dxpl_id, H5_REQUEST_NULL, 
                                         idx_type, order, op, op_data)) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_BADITER, FAIL, "object iteration failed")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Ovisit() */


/*-------------------------------------------------------------------------
 * Function:    H5Ovisit_by_name
 *
 * Purpose:     Recursively visit an object and all the objects reachable
 *              from it.  If the starting object is a group, all the objects
 *              linked to from that group will be visited.  Links within
 *              each group are visited according to the order within the
 *              specified index (unless the specified index does not exist for
 *              a particular group, then the "name" index is used).
 *
 *              NOTE: Soft links and user-defined links are ignored during
 *              this operation.
 *
 *              NOTE: Each _object_ reachable from the initial group will only
 *              be visited once.  If multiple hard links point to the same
 *              object, the first link to the object's path (according to the
 *              iteration index and iteration order given) will be used to in
 *              the callback about the object.
 *
 * Return:      Success:    The return value of the first operator that
 *                          returns non-zero, or zero if all members were
 *                          processed with no operator returning non-zero.
 *
 *              Failure:    Negative if something goes wrong within the
 *                          library, or the negative value returned by one
 *                          of the operators.
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Ovisit_by_name(hid_t loc_id, const char *obj_name, H5_index_t idx_type,
    H5_iter_order_t order, H5O_iterate_t op, void *op_data, hid_t lapl_id)
{
    H5VL_object_t       *obj        = NULL;     /* Object token of loc_id */
    H5VL_loc_params_t   loc_params; 
    hid_t               dxpl_id     = H5AC_ind_read_dxpl_id; /* dxpl used by library */
    herr_t              ret_value;              /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE7("e", "i*sIiIox*xi", loc_id, obj_name, idx_type, order, op, op_data,
             lapl_id);

    /* Check args */
    if (!obj_name)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "obj_name parameter cannot be NULL")
    if (!*obj_name)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "obj_name parameter cannot be an empty string")
    if (idx_type <= H5_INDEX_UNKNOWN || idx_type >= H5_INDEX_N)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid index type specified")
    if (order <= H5_ITER_UNKNOWN || order >= H5_ITER_N)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid iteration order specified")
    if (!op)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "no callback operator specified")

    /* Verify access property list and get correct dxpl */
    if (H5P_verify_apl_and_dxpl(&lapl_id, H5P_CLS_LACC, &dxpl_id, loc_id, FALSE) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_CANTSET, FAIL, "can't set access and transfer property lists")

    /* Get the location object */
    if (NULL == (obj = H5VL_get_object(loc_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid location identifier")

    /* Set location parameters */
    loc_params.type                         = H5VL_OBJECT_BY_NAME;
    loc_params.loc_data.loc_by_name.name    = obj_name;
    loc_params.loc_data.loc_by_name.lapl_id = lapl_id;
    loc_params.obj_type                     = H5I_get_type(loc_id);

    /* iterate over the objects through the VOL */
    if ((ret_value = H5VL_object_specific(obj->vol_obj, loc_params, obj->vol_info->vol_cls, 
                                         H5VL_OBJECT_VISIT, dxpl_id, H5_REQUEST_NULL, 
                                         idx_type, order, op, op_data)) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_BADITER, FAIL, "link iteration failed")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Ovisit_by_name() */


/*-------------------------------------------------------------------------
 * Function:    H5Oclose
 *
 * Purpose:     Close an open file object.
 *
 *              This is the companion to H5Oopen. It is used to close any
 *              open object in an HDF5 file (but not IDs are that not file
 *              objects, such as property lists and dataspaces). It has
 *              the same effect as calling H5Gclose, H5Dclose, or H5Tclose.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Oclose(hid_t object_id)
{
    herr_t       ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE1("e", "i", object_id);

    /* Get the type of the object and close it in the correct way */
    switch(H5I_get_type(object_id)) {
        case H5I_GROUP:
        case H5I_DATATYPE:
        case H5I_DATASET:
            if(H5I_object(object_id) == NULL)
                HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a valid object")
            if(H5I_dec_app_ref(object_id) < 0)
                HGOTO_ERROR(H5E_OHDR, H5E_CANTRELEASE, FAIL, "unable to close object")
            break;

        case H5I_UNINIT:
        case H5I_BADID:
        case H5I_FILE:
        case H5I_DATASPACE:
        case H5I_ATTR:
        case H5I_REFERENCE:
        case H5I_VFL:
        case H5I_VOL:
        case H5I_GENPROP_CLS:
        case H5I_GENPROP_LST:
        case H5I_ERROR_CLASS:
        case H5I_ERROR_MSG:
        case H5I_ERROR_STACK:
        case H5I_NTYPES:
        default:
            HGOTO_ERROR(H5E_ARGS, H5E_CANTRELEASE, FAIL, "not a valid file object ID (dataset, group, or datatype)")
        break;
    }

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Oclose() */


/*-------------------------------------------------------------------------
 * Function:	H5Odisable_mdc_flushes
 *
 * Purpose:	To "cork" an object:
 *		--keep dirty entries assoicated with the object in the metadata cache
 *
 * Return:	Success:	Non-negative
 *		Failure:	Negative
 *
 * Programmer:	Vailin Choi
 *		January 2014
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Odisable_mdc_flushes(hid_t object_id)
{
    H5O_loc_t  *oloc;			/* Object location */
    herr_t      ret_value = SUCCEED;	/* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE1("e", "i", object_id);

    /* Get the object's oloc */
    if(NULL == (oloc = H5O_get_loc(object_id)))
        HGOTO_ERROR(H5E_OHDR, H5E_BADVALUE, FAIL, "unable to get object location from ID")

    if(H5AC_cork(oloc->file, oloc->addr, H5AC__SET_CORK, NULL) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_CANTCORK, FAIL, "unable to cork an object")

done:
    FUNC_LEAVE_API(ret_value)
} /* H5Odisable_mdc_flushes() */


/*-------------------------------------------------------------------------
 * Function:	H5Oenable_mdc_flushes
 *
 * Purpose:	To "uncork" an object
 *		--release keeping dirty entries associated with the object 
 *		  in the metadata cache
 *
 * Return:	Success:	Non-negative
 *		Failure:	Negative
 *
 * Programmer:	Vailin Choi
 *		January 2014
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Oenable_mdc_flushes(hid_t object_id)
{
    H5O_loc_t  *oloc;			/* Object location */
    herr_t      ret_value = SUCCEED;	/* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE1("e", "i", object_id);

    /* Get the object's oloc */
    if(NULL == (oloc = H5O_get_loc(object_id)))
        HGOTO_ERROR(H5E_OHDR, H5E_BADVALUE, FAIL, "unable to get object location from ID")

    /* Set the value */
    if(H5AC_cork(oloc->file, oloc->addr, H5AC__UNCORK, NULL) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_CANTUNCORK, FAIL, "unable to uncork an object")

done:
    FUNC_LEAVE_API(ret_value)
} /* H5Oenable_mdc_flushes() */


/*-------------------------------------------------------------------------
 * Function:	H5Oare_mdc_flushes_disabled
 *
 * Purpose:	Retrieve the object's "cork" status in the parameter "are_disabled":
 *		  TRUE if mdc flushes for the object is disabled
 *		  FALSE if mdc flushes for the object is not disabled
 *		Return error if the parameter "are_disabled" is not supplied
 *
 * Return:	Success:	Non-negative
 *		Failure:	Negative
 *
 * Programmer:	Vailin Choi
 *		January 2014
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Oare_mdc_flushes_disabled(hid_t object_id, hbool_t *are_disabled)
{
    H5O_loc_t  *oloc;			/* Object location */
    herr_t      ret_value = SUCCEED;	/* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE2("e", "i*b", object_id, are_disabled);

    /* Check args */

    /* Get the object's oloc */
    if(NULL == (oloc = H5O_get_loc(object_id)))
        HGOTO_ERROR(H5E_OHDR, H5E_BADVALUE, FAIL, "unable to get object location from ID")
    if(!are_disabled)
        HGOTO_ERROR(H5E_OHDR, H5E_BADVALUE, FAIL, "unable to get object location from ID")

    /* Get the cork status */
    if(H5AC_cork(oloc->file, oloc->addr, H5AC__GET_CORKED, are_disabled) < 0)
        HGOTO_ERROR(H5E_OHDR, H5E_CANTGET, FAIL, "unable to retrieve an object's cork status")

done:
    FUNC_LEAVE_API(ret_value)
} /* H5Oare_mdc_flushes_disabled() */

