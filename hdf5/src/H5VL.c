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
 * Purpose:	The Virtual Object Layer as described in documentation.
 *              The pupose is to provide an abstraction on how to access the
 *              underlying HDF5 container, whether in a local file with 
 *              a specific file format, or remotely on other machines, etc...
 */

/****************/
/* Module Setup */
/****************/

#include "H5VLmodule.h"         /* This source code file is part of the H5VL module */


/***********/
/* Headers */
/***********/
#include "H5private.h"		/* Generic Functions			*/
#include "H5Aprivate.h"		/* Attributes				*/
#include "H5Eprivate.h"		/* Error handling		  	*/
#include "H5Iprivate.h"		/* IDs			  		*/
#include "H5MMprivate.h"	/* Memory management			*/
#include "H5PLprivate.h"        /* Plugins                              */
#include "H5VLpkg.h"		/* VOL package header		  	*/
#include "H5VLprivate.h"	/* VOL          		  	*/

/********************/
/* Local Prototypes */
/********************/
static herr_t H5VL_free_cls(H5VL_class_t *cls);

/*********************/
/* Package Variables */
/*********************/

/* Package initialization variable */
hbool_t H5_PKG_INIT_VAR = FALSE;

/*******************/
/* Local Variables */
/*******************/
typedef struct {
    const char *name;
    hid_t ret_id;
} H5VL_get_plugin_ud_t;

/* VOL ID class */
static const H5I_class_t H5I_VOL_CLS[1] = {{
    H5I_VOL,			/* ID class value */
    0,				/* Class flags */
    0,				/* # of reserved IDs for class */
    (H5I_free_t)H5VL_free_cls   /* Callback routine for closing objects of this class */
}};


/*-------------------------------------------------------------------------
 * Function:H5VL_init
 *
 * Purpose:Initialize the interface from some other package.
 *
 * Return:Success:non-negative
 *Failure:negative
 *
 * Programmer:Mohamad Chaarawi
 *              January, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VL_init(void)
{
    herr_t ret_value = SUCCEED;   /* Return value */

    FUNC_ENTER_NOAPI(FAIL)
    /* FUNC_ENTER() does all the work */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_init() */


/*--------------------------------------------------------------------------
NAME
   H5VL__init_package -- Initialize interface-specific information
USAGE
    herr_t H5VL__init_package()
RETURNS
    Non-negative on success/Negative on failure
DESCRIPTION
    Initializes any interface-specific data or routines.
--------------------------------------------------------------------------*/
herr_t
H5VL__init_package(void)
{
    herr_t ret_value = SUCCEED;   /* Return value */

    FUNC_ENTER_PACKAGE

    /* Initialize the atom group for the VL IDs */
    if(H5I_register_type(H5I_VOL_CLS) < 0)
	HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "unable to initialize interface")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__init_package() */


/*--------------------------------------------------------------------------
 NAME
    H5VL_term_package
 PURPOSE
    Terminate various H5VL objects
 USAGE
    void H5VL_term_package()
 RETURNS
    Non-negative on success/Negative on failure
 DESCRIPTION
    Release the atom group and any other resources allocated.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
     Can't report errors...

     Finishes shutting down the interface, after H5VL_top_term_package()
     is called
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
int
H5VL_term_package(void)
{
    int	n = 0;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    if(H5_PKG_INIT_VAR) {

        if(H5I_nmembers(H5I_VOL) > 0) {
            (void)H5I_clear_type(H5I_VOL, FALSE, FALSE);
            n++;
        }
        else {
            n += (H5I_dec_type_ref(H5I_VOL) > 0);

            /* Mark interface as closed */
            if(0 == n)
                H5_PKG_INIT_VAR = FALSE;
        }
    } /* end if */

    FUNC_LEAVE_NOAPI(n)
} /* end H5VL_term_package() */


/*-------------------------------------------------------------------------
 * Function:	H5VL_free_cls
 *
 * Purpose:	Frees a file vol class struct and returns an indication of
 *		success. This function is used as the free callback for the
 *		virtual object layer object identifiers (cf H5VL_init_interface).
 *
 * Return:	Success:	Non-negative
 *
 *		Failure:	Negative
 *
 * Programmer:	Mohamad Chaarawi
 *              January, 2012
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_free_cls(H5VL_class_t *cls)
{
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    /* Sanity check */
    HDassert(cls);

    /* XXX: Need to retrieve the VOL termination property list for the
     * terminate operation - JTH
     */
    if (cls->terminate && cls->terminate(H5P_DEFAULT) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTCLOSEOBJ, FAIL, "VOL plugin did not terminate cleanly")

    H5MM_free(cls);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_free_cls() */


/*-------------------------------------------------------------------------
 * Function:	H5VL__get_plugin_cb
 *
 * Purpose:	Callback routine to search through registered VLs
 *
 * Return:	Success:	The first object in the type for which FUNC
 *				returns non-zero. NULL if FUNC returned zero
 *				for every object in the type.
 *		Failure:	NULL
 *
 * Programmer:	Quincey Koziol
 *		Friday, March 30, 2012
 *
 *-------------------------------------------------------------------------
 */
static int
H5VL__get_plugin_cb(void *obj, hid_t id, void *_op_data)
{
    H5VL_get_plugin_ud_t *op_data = (H5VL_get_plugin_ud_t *)_op_data; /* User data for callback */
    H5VL_class_t *cls = (H5VL_class_t *)obj;
    int ret_value = H5_ITER_CONT;     /* Callback return value */

    FUNC_ENTER_STATIC_NOERR

    if(0 == strcmp(cls->name, op_data->name)) {
        op_data->ret_id = id;
        ret_value = H5_ITER_STOP;
    }
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__get_plugin_cb() */


/*-------------------------------------------------------------------------
 * Function:	H5VLregister
 *
 * Purpose:	Registers a new vol plugin as a member of the virtual object
 *		layer class.
 *
 * Return:	Success:	A vol plugin ID which is good until the
 *				library is closed or the plugin is
 *				unregistered.
 *
 *		Failure:	A negative value.
 *
 * Programmer:	Mohamad Chaarawi
 *              January, 2012
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5VLregister(const H5VL_class_t *cls)
{
    H5VL_get_plugin_ud_t op_data;
    hid_t ret_value;

    FUNC_ENTER_API(FAIL)
    H5TRACE1("i", "*x", cls);

    /* Check arguments */
    if(!cls)
	HGOTO_ERROR(H5E_ARGS, H5E_UNINITIALIZED, FAIL, "null class pointer is disallowed")

    if(cls->value < H5_VOL_MAX_LIB_VALUE)
        HGOTO_ERROR(H5E_VOL, H5E_CANTREGISTER, FAIL, 
                    "registered class value must not be smaller than %d", H5_VOL_MAX_LIB_VALUE)

    if(!cls->name)
        HGOTO_ERROR(H5E_VOL, H5E_CANTREGISTER, FAIL, "invalid VOL class name");

    op_data.ret_id = FAIL;
    op_data.name = cls->name;

    /* check if plugin is already registered */
    if(H5I_iterate(H5I_VOL, H5VL__get_plugin_cb, &op_data, TRUE) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_BADITER,FAIL, "can't iterate over VOL ids")

    if(op_data.ret_id != FAIL)
        HGOTO_ERROR(H5E_VOL, H5E_CANTREGISTER, FAIL, "VOL plugin with the same name is already registered.")

    /* Create the new class ID */
    if((ret_value = H5VL_register(cls, sizeof(H5VL_class_t), TRUE)) < 0)
        HGOTO_ERROR(H5E_ATOM, H5E_CANTREGISTER, FAIL, "unable to register vol plugin ID")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLregister() */


/*-------------------------------------------------------------------------
 * Function:	H5VLregister_by_name
 *
 * Purpose:	Registers a new vol plugin as a member of the virtual object
 *		layer class.
 *
 * Return:	Success:	A vol plugin ID which is good until the
 *				library is closed or the plugin is
 *				unregistered.
 *
 *		Failure:	A negative value.
 *
 * Programmer:	Mohamad Chaarawi
 *              September, 2014
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5VLregister_by_name(const char *name)
{
    H5VL_get_plugin_ud_t op_data;
    hid_t ret_value;

    FUNC_ENTER_API(FAIL)
    H5TRACE1("i", "*s", name);

    /* Check arguments */
    if(!name)
	HGOTO_ERROR(H5E_ARGS, H5E_UNINITIALIZED, FAIL, "null plugin name is disallowed")

    op_data.ret_id = FAIL;
    op_data.name = name;

    /* check if plugin is already registered */
    if(H5I_iterate(H5I_VOL, H5VL__get_plugin_cb, &op_data, TRUE) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_BADITER,FAIL, "can't iterate over VOL ids")

    if(op_data.ret_id != FAIL) {
        /* If plugin alread registered, increment ref count on ID and return ID */
        if(H5I_inc_ref(op_data.ret_id, TRUE) < 0)
            HGOTO_ERROR(H5E_VOL, H5E_CANTINC, FAIL, "unable to increment ref count on VOL plugin")
        ret_value = op_data.ret_id;
    }
    else {
        const H5VL_class_t *cls;

        /* Try loading the plugin */
        if(NULL == (cls = (const H5VL_class_t *)H5PL_load(H5PL_TYPE_VOL, -1, name)))
            HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "unable to load VOL plugin")

        /* Register the plugin we loaded */
        if((ret_value = H5VL_register(cls, sizeof(H5VL_class_t), TRUE)) < 0)
            HGOTO_ERROR(H5E_ATOM, H5E_CANTREGISTER, FAIL, "unable to register vol plugin ID")
    }

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLregister() */


/*-------------------------------------------------------------------------
 * Function:	H5VLunregister
 *
 * Purpose:	Removes a vol plugin ID from the library. This in no way affects
 *		file access property lists which have been defined to use
 *		this vol plugin or files which are already opened under with
 *              this plugin.
 *
 * Return:	Success:	Non-negative
 *
 *		Failure:	Negative
 *
 * Programmer:	Mohamad Chaarawi
 *              January, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLunregister(hid_t vol_id)
{
    H5VL_class_t *cls = NULL;
    herr_t ret_value = SUCCEED;       /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE1("e", "i", vol_id);

    /* Check arguments */
    if(NULL == (cls = (H5VL_class_t *)H5I_object_verify(vol_id, H5I_VOL)))
	HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a vol plugin")

    if(cls->value <= H5_VOL_MAX_LIB_VALUE)
	HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "can't unregister an internal plugin")

    /* The H5VL_class_t struct will be freed by this function */
    if(H5I_dec_app_ref(vol_id) < 0)
	HGOTO_ERROR(H5E_VOL, H5E_CANTDEC, FAIL, "unable to unregister vol plugin")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLunregister() */


/*-------------------------------------------------------------------------
 * Function:	H5VLinitialize
 *
 * Purpose:	Calls the plugin specific callback to init the plugin.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Mohamad Chaarawi
 *		August 2014
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLinitialize(hid_t plugin_id, hid_t vipl_id)
{
    H5VL_class_t *cls = NULL;
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE2("e", "ii", plugin_id, vipl_id);

    /* Check args */
    if(NULL == (cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
	HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if(cls->initialize && cls->initialize(vipl_id) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTCLOSEOBJ, FAIL, "VOL plugin did not initialize")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLinitialize() */


/*-------------------------------------------------------------------------
 * Function:	H5VLterminate
 *
 * Purpose:	
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Mohamad Chaarawi
 *		August 2014
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLterminate(hid_t plugin_id, hid_t vtpl_id)
{
    H5VL_class_t *cls = NULL;
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE2("e", "ii", plugin_id, vtpl_id);

    /* Check args */
    if(NULL == (cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
	HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if(cls->terminate && cls->terminate(vtpl_id) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTCLOSEOBJ, FAIL, "VOL plugin did not terminate cleanly")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLterminate() */


/*-------------------------------------------------------------------------
 * Function:	H5VLis_registered
 *
 * Purpose:	Tests whether a VOL class has been registered or not
 *
 * Return:	Positive if the VOL class has been registered
 *              Zero if it is unregistered
 *              Negative on error (if the class is not a valid class ID)
 *
 * Programmer:	Mohamad Chaarawi
 *              June 2012
 *
 *-------------------------------------------------------------------------
 */
htri_t
H5VLis_registered(const char *name)
{
    H5VL_get_plugin_ud_t op_data;
    htri_t ret_value = FALSE;     /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE1("t", "*s", name);

    op_data.ret_id = FAIL;
    op_data.name = name;

    /* Check arguments */
    if(H5I_iterate(H5I_VOL, H5VL__get_plugin_cb, &op_data, TRUE) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_BADITER, FAIL, "can't iterate over VOL ids")

    if(op_data.ret_id != FAIL)
        ret_value = TRUE;

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLis_registered() */


/*-------------------------------------------------------------------------
 * Function:	H5VLget_plugin_id
 *
 * Purpose:	Retrieves the registered plugin ID for a VOL.
 *
 * Return:	Positive if the VOL class has been registered
 *              Negative on error (if the class is not a valid class or not registered)
 *
 * Programmer:	Mohamad Chaarawi
 *              August 2014
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5VLget_plugin_id(const char *name)
{
    H5VL_get_plugin_ud_t op_data;
    hid_t ret_value = FAIL;     /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE1("i", "*s", name);

    op_data.ret_id = FAIL;
    op_data.name = name;

    /* Check arguments */
    if(H5I_iterate(H5I_VOL, H5VL__get_plugin_cb, &op_data, TRUE) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_BADITER,FAIL, "can't iterate over VOL ids")

    if(op_data.ret_id != FAIL) {
        if(H5I_inc_ref(op_data.ret_id, TRUE) < 0)
            HGOTO_ERROR(H5E_FILE, H5E_CANTINC, FAIL, "unable to increment ref count on VOL plugin")

        ret_value = op_data.ret_id;
    }

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLget_plugin_id() */


/*-------------------------------------------------------------------------
 * Function:	H5VLget_plugin_name
 *
 * Purpose:	Returns the plugin name for the VOL associated with the 
 *              object or file ID
 *
 * Return:      Success:        The length of the plugin name
 *              Failure:        Negative
 *
 * Programmer:	Mohamad Chaarawi
 *              June, 2012
 *
 *-------------------------------------------------------------------------
 */
ssize_t
H5VLget_plugin_name(hid_t obj_id, char *name/*out*/, size_t size)
{
    ssize_t    ret_value;

    FUNC_ENTER_API(FAIL)
    H5TRACE3("Zs", "ixz", obj_id, name, size);

    if((ret_value = H5VL_get_plugin_name(obj_id, name, size)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "Can't get plugin name")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLget_plugin_name() */


/*-------------------------------------------------------------------------
 * Function:	H5VLclose
 *
 * Purpose:	Closes the specified VOL plugin.  The VOL ID will no longer be
 *		valid for accessing the VOL.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Mohamad Chaarawi
 *		August 2014
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLclose(hid_t vol_id)
{
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE1("e", "i", vol_id);

    /* Check args */
    if(NULL == H5I_object_verify(vol_id, H5I_VOL))
	HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if(H5I_dec_app_ref(vol_id) < 0)
    	HGOTO_ERROR(H5E_VOL, H5E_CANTRELEASE, FAIL, "unable to close VOL plugin ID")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLclose() */


/*---------------------------------------------------------------------------
 * Function:	H5VLobject_register
 *
 * Purpose:     Public routine to create an HDF5 hid_t with library
 *              specific types, bypassing the limitation of H5Iregister.
 *
 * Returns:     Non-negative on success or negative on failure
 *
 * Programmer:  Mohamad Chaarawi
 *              June, 2012
 *
 *---------------------------------------------------------------------------
 */
hid_t
H5VLobject_register(void *obj, H5I_type_t obj_type, hid_t plugin_id)
{
    hid_t ret_value = FAIL;

    FUNC_ENTER_API(FAIL)
    H5TRACE3("i", "*xIti", obj, obj_type, plugin_id);

    if(NULL == obj)
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid object to register")

    if ((ret_value = H5VL_object_register(obj, obj_type, plugin_id, TRUE)) < 0)
        HGOTO_ERROR(H5E_ATOM, H5E_CANTREGISTER, FAIL, "unable to atomize dataset handle")

done:
    FUNC_LEAVE_API(ret_value)
} /* H5VLobject_register */


/*---------------------------------------------------------------------------
 * Function:	H5VLget_object
 *
 * Purpose:	Retrieve the object pointer associated with the ID. This 
 *              also optionally returns the H5VL_t struct that this ID 
 *              belongs to, if the user passes a valid pointer value.
 *
 * Returns:     Non-negative on success or negative on failure
 *
 * Programmer:  Mohamad Chaarawi
 *              July, 2013
 *
 *---------------------------------------------------------------------------
 */
herr_t
H5VLget_object(hid_t obj_id, void **obj)
{
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE2("e", "i**x", obj_id, obj);

    /* Check args */
    if(!obj)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object pointer")

    if(NULL == (*obj = H5VL_get_object(obj_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "ID does not contain a valid object")

done:
    FUNC_LEAVE_API(ret_value)
} /* H5VLget_object */


/*-------------------------------------------------------------------------
 * Function:	H5VLattr_create
 *
 * Purpose:	Creates an attribute through the VOL
 *
 * Return:      Success: pointer to the new attr. 
 *		Failure: NULL
 *
 * Programmer:	Mohamad Chaarawi
 *              April, 2012
 *
 *-------------------------------------------------------------------------
 */
void *
H5VLattr_create(void *obj, H5VL_loc_params_t loc_params, hid_t plugin_id, const char *name, 
                hid_t acpl_id, hid_t aapl_id, hid_t dxpl_id, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    void *ret_value = NULL; /* Return value */

    FUNC_ENTER_API(NULL)
    H5TRACE8("*x", "*xxi*siii**x", obj, loc_params, plugin_id, name, acpl_id,
             aapl_id, dxpl_id, req);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, NULL, "not a VOL plugin ID")

    if(NULL == (ret_value = H5VL_attr_create(obj, loc_params, vol_cls, name, 
                                             acpl_id, aapl_id, dxpl_id, req)))
	HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, NULL, "unable to create attribute")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLattr_create() */


/*-------------------------------------------------------------------------
 * Function:	H5VLattr_open
 *
 * Purpose:	Opens an attribute through the VOL
 *
 * Return:      Success: pointer to the new attr. 
 *		Failure: NULL
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
void *
H5VLattr_open(void *obj, H5VL_loc_params_t loc_params, hid_t plugin_id, const char *name, 
              hid_t aapl_id, hid_t dxpl_id, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    void *ret_value = NULL; /* Return value */

    FUNC_ENTER_API(NULL)
    H5TRACE7("*x", "*xxi*sii**x", obj, loc_params, plugin_id, name, aapl_id,
             dxpl_id, req);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, NULL, "not a VOL plugin ID")

    if(NULL == (ret_value = H5VL_attr_open(obj, loc_params, vol_cls, name, aapl_id, 
                                           dxpl_id, req)))
	HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, NULL, "unable to open attribute")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLattr_open() */


/*-------------------------------------------------------------------------
 * Function:	H5VLattr_read
 *
 * Purpose:	Reads data from attr through the VOL
 *
 * Return:	Success:	Non Negative
 *
 *		Failure:	Negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t H5VLattr_read(void *attr, hid_t plugin_id, hid_t mem_type_id, void *buf, hid_t dxpl_id, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)

    if (NULL == attr)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if((ret_value = H5VL_attr_read(attr, vol_cls, mem_type_id, buf, dxpl_id, req)) < 0)
	HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "unable to read attribute")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLattr_read() */


/*-------------------------------------------------------------------------
 * Function:	H5VLattr_write
 *
 * Purpose:	Writes data to attr through the VOL
 *
 * Return:	Success:	Non Negative
 *		Failure:	Negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t H5VLattr_write(void *attr, hid_t plugin_id, hid_t mem_type_id, const void *buf, hid_t dxpl_id, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)

    if (NULL == attr)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if((ret_value = H5VL_attr_write(attr, vol_cls, mem_type_id, buf, dxpl_id, req)) < 0)
	HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "unable to write attribute")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLattr_write() */


/*-------------------------------------------------------------------------
 * Function:	H5VLattr_get
 *
 * Purpose:	Get specific information about the attribute through the VOL
 *
 * Return:	Success:        non negative
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLattr_get(void *obj, hid_t plugin_id, H5VL_attr_get_t get_type, 
             hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE6("e", "*xiVai**xx", obj, plugin_id, get_type, dxpl_id, req, arguments);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    /* Bypass the H5VLint layer */
    if(NULL == vol_cls->attr_cls.get)
	HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `attr get' method")
    if((ret_value = (vol_cls->attr_cls.get)(obj, get_type, dxpl_id, req, arguments)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "Unable to get attribute information")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLattr_get() */


/*-------------------------------------------------------------------------
 * Function:	H5VLattr_specific
 *
 * Purpose:	specific operation on attributes through the VOL
 *
 * Return:	Success:        non negative
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLattr_specific(void *obj, H5VL_loc_params_t loc_params, hid_t plugin_id, 
                  H5VL_attr_specific_t specific_type, hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE7("e", "*xxiVbi**xx", obj, loc_params, plugin_id, specific_type,
             dxpl_id, req, arguments);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    /* Bypass the H5VLint layer */
    if(NULL == vol_cls->attr_cls.specific)
	HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `attr specific' method")
    if((ret_value = (vol_cls->attr_cls.specific)
        (obj, loc_params, specific_type, dxpl_id, req, arguments)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTOPERATE, FAIL, "Unable to execute attribute specific callback")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLattr_specific() */


/*-------------------------------------------------------------------------
 * Function:	H5VLattr_optional
 *
 * Purpose:	optional operation specific to plugins.
 *
 * Return:	Success:        non negative
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLattr_optional(void *obj, hid_t plugin_id, hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE5("e", "*xii**xx", obj, plugin_id, dxpl_id, req, arguments);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    /* Have to bypass the H5VLint layer due to unknown val_list arguments */
    if(NULL == vol_cls->attr_cls.optional)
	HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `attr optional' method")
    if((ret_value = (vol_cls->attr_cls.optional)(obj, dxpl_id, req, arguments)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTOPERATE, FAIL, "Unable to execute attribute optional callback")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLattr_optional() */


/*-------------------------------------------------------------------------
 * Function:	H5VLattr_close
 *
 * Purpose:	Closes an attribute through the VOL
 *
 * Return:	Success:	Non Negative
 *		Failure:	Negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLattr_close(void *attr, hid_t plugin_id, hid_t dxpl_id, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE4("e", "*xii**x", attr, plugin_id, dxpl_id, req);

    if (NULL == attr)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if((ret_value = H5VL_attr_close(attr, vol_cls, dxpl_id, req)) < 0)
	HGOTO_ERROR(H5E_VOL, H5E_CANTRELEASE, FAIL, "unable to close attribute")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLattr_close() */


/*-------------------------------------------------------------------------
 * Function:	H5VLdatatype_commit
 *
 * Purpose:	Commits a datatype to the file through the VOL
 *
 * Return:      Success: Positive
 *		Failure: Negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
void *
H5VLdatatype_commit(void *obj, H5VL_loc_params_t loc_params, hid_t plugin_id, const char *name, 
                     hid_t type_id, hid_t lcpl_id, hid_t tcpl_id, hid_t tapl_id, hid_t dxpl_id, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    void *ret_value = NULL; /* Return value */

    FUNC_ENTER_API(NULL)
    H5TRACE10("*x", "*xxi*siiiii**x", obj, loc_params, plugin_id, name, type_id,
             lcpl_id, tcpl_id, tapl_id, dxpl_id, req);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, NULL, "not a VOL plugin ID")

    if(NULL == (ret_value = H5VL_datatype_commit(obj, loc_params, vol_cls, name, type_id, 
                                                 lcpl_id, tcpl_id, tapl_id, dxpl_id, req)))
	HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, NULL, "unable to commit datatype")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLdatatype_commit() */


/*-------------------------------------------------------------------------
 * Function:	H5VLdatatype_open
 *
 * Purpose:	Opens a named datatype through the VOL
 *
 * Return:      Success: User ID of the datatype. 
 *		Failure: NULL
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
void *
H5VLdatatype_open(void *obj, H5VL_loc_params_t loc_params, hid_t plugin_id, const char *name, 
                   hid_t tapl_id, hid_t dxpl_id, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    void *ret_value = NULL; /* Return value */

    FUNC_ENTER_API(NULL)
    H5TRACE7("*x", "*xxi*sii**x", obj, loc_params, plugin_id, name, tapl_id,
             dxpl_id, req);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, NULL, "not a VOL plugin ID")

    if(NULL == (ret_value = H5VL_datatype_open(obj, loc_params, vol_cls, name, 
                                               tapl_id, dxpl_id, req)))
	HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, NULL, "unable to open datatype")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLdatatype_open() */


/*-------------------------------------------------------------------------
 * Function:	H5VLdatatype_specific
 *
 * Purpose:	specific operation on datatypes through the VOL
 *
 * Return:	Success:        non negative
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLdatatype_specific(void *obj, hid_t plugin_id, H5VL_datatype_specific_t specific_type, 
                      hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE6("e", "*xiVfi**xx", obj, plugin_id, specific_type, dxpl_id, req,
             arguments);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if(NULL == vol_cls->datatype_cls.specific)
	HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `datatype specific' method")
    if((ret_value = (vol_cls->datatype_cls.specific)
        (obj, specific_type, dxpl_id, req, arguments)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTOPERATE, FAIL, "Unable to execute datatype specific callback")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLdatatype_specific() */


/*-------------------------------------------------------------------------
 * Function:	H5VLdatatype_optional
 *
 * Purpose:	optional operation specific to plugins.
 *
 * Return:	Success:        non negative
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLdatatype_optional(void *obj, hid_t plugin_id, hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE5("e", "*xii**xx", obj, plugin_id, dxpl_id, req, arguments);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if(NULL == vol_cls->datatype_cls.optional)
	HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `datatype optional' method")
    if((ret_value = (vol_cls->datatype_cls.optional)(obj, dxpl_id, req, arguments)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTOPERATE, FAIL, "Unable to execute datatype optional callback")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLdatatype_optional() */


/*-------------------------------------------------------------------------
 * Function:	H5VLdatatype_get
 *
 * Purpose:	Get specific information about the datatype through the VOL
 *
 * Return:	Success:        non negative
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              June, 2013
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLdatatype_get(void *obj, hid_t plugin_id, H5VL_datatype_get_t get_type, 
                 hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE6("e", "*xiVei**xx", obj, plugin_id, get_type, dxpl_id, req, arguments);

    if(NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    /* Bypass the H5VLint layer */
    if(NULL == vol_cls->datatype_cls.get)
	HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `datatype get' method")
    if((ret_value = (vol_cls->datatype_cls.get)(obj, get_type, dxpl_id, req, arguments)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "Unable to execute datatype get callback")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLdatatype_get() */


/*-------------------------------------------------------------------------
 * Function:	H5VLdatatype_close
 *
 * Purpose:	Closes a datatype through the VOL
 *
 * Return:      Success: Positive
 *		Failure: Negative
 *
 * Programmer:	Mohamad Chaarawi
 *              May, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLdatatype_close(void *dt, hid_t plugin_id, hid_t dxpl_id, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE4("e", "*xii**x", dt, plugin_id, dxpl_id, req);

    if (NULL == dt)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if((ret_value = H5VL_datatype_close(dt, vol_cls, dxpl_id, req)) < 0)
	HGOTO_ERROR(H5E_VOL, H5E_CANTRELEASE, FAIL, "unable to close datatype")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLdatatype_close() */


/*-------------------------------------------------------------------------
 * Function:	H5VLdataset_create
 *
 * Purpose:	Creates a dataset through the VOL
 *
 * Return:      Success: pointer to dataset
 *		Failure: NULL
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
void *
H5VLdataset_create(void *obj, H5VL_loc_params_t loc_params, hid_t plugin_id, const char *name, 
                    hid_t dcpl_id, hid_t dapl_id, hid_t dxpl_id, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    void *ret_value = NULL; /* Return value */

    FUNC_ENTER_API(NULL)
    H5TRACE8("*x", "*xxi*siii**x", obj, loc_params, plugin_id, name, dcpl_id,
             dapl_id, dxpl_id, req);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, NULL, "not a VOL plugin ID")

    if(NULL == (ret_value = H5VL_dataset_create(obj, loc_params, vol_cls, name, 
                                                dcpl_id, dapl_id, dxpl_id, req)))
	HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, NULL, "unable to create dataset")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLdataset_create() */


/*-------------------------------------------------------------------------
 * Function:	H5VLdataset_open
 *
 * Purpose:	Opens a dataset through the VOL
 *
 * Return:      Success: pointer to dataset 
 *		Failure: NULL
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
void *
H5VLdataset_open(void *obj, H5VL_loc_params_t loc_params, hid_t plugin_id, const char *name, 
                  hid_t dapl_id, hid_t dxpl_id, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    void *ret_value = NULL; /* Return value */

    FUNC_ENTER_API(NULL)
    H5TRACE7("*x", "*xxi*sii**x", obj, loc_params, plugin_id, name, dapl_id,
             dxpl_id, req);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, NULL, "not a VOL plugin ID")

    if(NULL == (ret_value = H5VL_dataset_open(obj, loc_params, vol_cls, name, dapl_id, dxpl_id, req)))
	HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, NULL, "unable to open dataset")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLdataset_open() */


/*-------------------------------------------------------------------------
 * Function:	H5VLdataset_read
 *
 * Purpose:	Reads data from dataset through the VOL
 *
 * Return:	Success:	Non Negative
 *		Failure:	Negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLdataset_read(void *dset, hid_t plugin_id, hid_t mem_type_id, hid_t mem_space_id, 
                  hid_t file_space_id, hid_t plist_id, void *buf, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE8("e", "*xiiiii*x**x", dset, plugin_id, mem_type_id, mem_space_id,
             file_space_id, plist_id, buf, req);

    if (NULL == dset)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if((ret_value = H5VL_dataset_read(dset, vol_cls, mem_type_id, mem_space_id, file_space_id, 
                                      plist_id, buf, req)) < 0)
	HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "unable to read dataset")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLdataset_read() */


/*-------------------------------------------------------------------------
 * Function:	H5VLdataset_write
 *
 * Purpose:	Writes data from dataset through the VOL
 *
 * Return:	Success:	Non Negative
 *		Failure:	Negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLdataset_write(void *dset, hid_t plugin_id, hid_t mem_type_id, hid_t mem_space_id, 
                   hid_t file_space_id, hid_t plist_id, const void *buf, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE8("e", "*xiiiii*x**x", dset, plugin_id, mem_type_id, mem_space_id,
             file_space_id, plist_id, buf, req);

    if (NULL == dset)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if((ret_value = H5VL_dataset_write(dset, vol_cls, mem_type_id, mem_space_id, file_space_id, 
                                       plist_id, buf, req)) < 0)
	HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "unable to write dataset")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLdataset_write() */


/*-------------------------------------------------------------------------
 * Function:	H5VLdataset_get
 *
 * Purpose:	Get specific information about the dataset through the VOL
 *
 * Return:	Success:        non negative
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLdataset_get(void *dset, hid_t plugin_id, H5VL_dataset_get_t get_type, 
                hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE6("e", "*xiVci**xx", dset, plugin_id, get_type, dxpl_id, req, arguments);

    if (NULL == dset)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    /* Bypass the H5VLint layer */
    if(NULL == vol_cls->dataset_cls.get)
	HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `dataset get' method")
    if((ret_value = (vol_cls->dataset_cls.get)(dset, get_type, dxpl_id, req, arguments)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "Unable to execute dataset get callback")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLdataset_get() */


/*-------------------------------------------------------------------------
 * Function:	H5VLdataset_specific
 *
 * Purpose:	specific operation on datasets through the VOL
 *
 * Return:	Success:        non negative
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLdataset_specific(void *obj, hid_t plugin_id, H5VL_dataset_specific_t specific_type, 
                      hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE6("e", "*xiVdi**xx", obj, plugin_id, specific_type, dxpl_id, req,
             arguments);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if(NULL == vol_cls->dataset_cls.specific)
	HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `dataset specific' method")
    if((ret_value = (vol_cls->dataset_cls.specific)
        (obj, specific_type, dxpl_id, req, arguments)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTOPERATE, FAIL, "Unable to execute dataset specific callback")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLdataset_specific() */


/*-------------------------------------------------------------------------
 * Function:	H5VLdataset_optional
 *
 * Purpose:	optional operation specific to plugins.
 *
 * Return:	Success:        non negative
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLdataset_optional(void *obj, hid_t plugin_id, hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE5("e", "*xii**xx", obj, plugin_id, dxpl_id, req, arguments);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if(NULL == vol_cls->dataset_cls.optional)
	HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `dataset optional' method")
    if((ret_value = (vol_cls->dataset_cls.optional)(obj, dxpl_id, req, arguments)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTOPERATE, FAIL, "Unable to execute dataset optional callback")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLdataset_optional() */


/*-------------------------------------------------------------------------
 * Function:	H5VLdataset_close
 *
 * Purpose:	Closes a dataset through the VOL
 *
 * Return:	Success:	Non Negative
 *		Failure:	Negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLdataset_close(void *dset, hid_t plugin_id, hid_t dxpl_id, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE4("e", "*xii**x", dset, plugin_id, dxpl_id, req);

    if (NULL == dset)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if((ret_value = H5VL_dataset_close(dset, vol_cls, dxpl_id, req)) < 0)
	HGOTO_ERROR(H5E_VOL, H5E_CANTRELEASE, FAIL, "unable to close dataset")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLdataset_close() */


/*-------------------------------------------------------------------------
 * Function:	H5VLfile_create
 *
 * Purpose:	Creates a file through the VOL
 *
 * Return:      Success: pointer to file. 
 *		Failure: NULL
 *
 * Programmer:	Mohamad Chaarawi
 *              January, 2012
 *
 *-------------------------------------------------------------------------
 */
void *
H5VLfile_create(const char *name, unsigned flags, hid_t fcpl_id, hid_t fapl_id, 
                hid_t dxpl_id, void **req)
{
    H5P_genplist_t     *plist;                 /* Property list pointer */
    H5VL_plugin_prop_t  plugin_prop;           /* Property for vol plugin ID & info */
    H5VL_class_t       *vol_cls = NULL;
    void	       *ret_value = NULL;      /* Return value */

    FUNC_ENTER_API(NULL)
    H5TRACE6("*x", "*sIuiii**x", name, flags, fcpl_id, fapl_id, dxpl_id, req);

    /* get the VOL info from the fapl */
    if(NULL == (plist = (H5P_genplist_t *)H5I_object(fapl_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, NULL, "not a file access property list")
    if(H5P_peek(plist, H5F_ACS_VOL_NAME, &plugin_prop) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, NULL, "can't get vol plugin info")

    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_prop.plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, NULL, "not a VOL plugin ID")

    if(NULL == (ret_value = H5VL_file_create(vol_cls, name, flags, fcpl_id, fapl_id, 
                                             dxpl_id, req)))
	HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, NULL, "unable to create file")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLfile_create() */


/*-------------------------------------------------------------------------
 * Function:	H5VLfile_open
 *
 * Purpose:	Opens a file through the VOL.
 *
 * Return:      Success: pointer to file. 
 *		Failure: NULL
 *
 * Programmer:	Mohamad Chaarawi
 *              January, 2012
 *
 *-------------------------------------------------------------------------
 */
void *
H5VLfile_open(const char *name, unsigned flags, hid_t fapl_id, hid_t dxpl_id, void **req)
{
    H5P_genplist_t     *plist;                 /* Property list pointer */
    H5VL_plugin_prop_t plugin_prop;            /* Property for vol plugin ID & info */
    H5VL_class_t       *vol_cls = NULL;
    void	       *ret_value = NULL;      /* Return value */

    FUNC_ENTER_API(NULL)
    H5TRACE5("*x", "*sIuii**x", name, flags, fapl_id, dxpl_id, req);

    /* get the VOL info from the fapl */
    if(NULL == (plist = (H5P_genplist_t *)H5I_object(fapl_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, NULL, "not a file access property list")
    if(H5P_peek(plist, H5F_ACS_VOL_NAME, &plugin_prop) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, NULL, "can't get vol plugin info")

    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_prop.plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, NULL, "not a VOL plugin ID")

    if(NULL == (ret_value = H5VL_file_open(vol_cls, name, flags, fapl_id, dxpl_id, req)))
	HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, NULL, "unable to create file")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLfile_open() */


/*-------------------------------------------------------------------------
 * Function:	H5VLfile_get
 *
 * Purpose:	Get specific information about the file through the VOL
 *
 * Return:	Success:        non negative
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              February, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLfile_get(void *file, hid_t plugin_id, H5VL_file_get_t get_type, 
             hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE6("e", "*xiVgi**xx", file, plugin_id, get_type, dxpl_id, req, arguments);

    if(NULL == file)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    /* Bypass the H5VLint layer */
    if(NULL == vol_cls->file_cls.get)
	HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `file get' method")
    if((ret_value = (vol_cls->file_cls.get)(file, get_type, dxpl_id, req, arguments)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "Unable to execute file get callback")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLfile_get() */


/*-------------------------------------------------------------------------
 * Function:	H5VLfile_specific
 *
 * Purpose:	perform File specific operations through the VOL
 *
 * Return:	Success:        non negative
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              April, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLfile_specific(void *file, hid_t plugin_id, H5VL_file_specific_t specific_type, 
                  hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE6("e", "*xiVhi**xx", file, plugin_id, specific_type, dxpl_id, req,
             arguments);

    if(specific_type == H5VL_FILE_IS_ACCESSIBLE) {
        H5P_genplist_t     *plist;          /* Property list pointer */
        H5VL_plugin_prop_t  plugin_prop;            /* Property for vol plugin ID & info */
        hid_t               fapl_id;

        fapl_id = va_arg (arguments, hid_t);

        /* get the VOL info from the fapl */
        if(NULL == (plist = (H5P_genplist_t *)H5I_object(fapl_id)))
            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a file access property list")

        if(H5P_peek(plist, H5F_ACS_VOL_NAME, &plugin_prop) < 0)
            HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get vol plugin info")

        if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_prop.plugin_id, H5I_VOL)))
            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

        if((ret_value = (vol_cls->file_cls.specific)
            (file, specific_type, dxpl_id, req, arguments)) < 0)
            HGOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "specific failed")
    }
    else {
        if(NULL == file)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
        if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

        if(NULL == vol_cls->file_cls.specific)
            HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `file specific' method")
        if((ret_value = (vol_cls->file_cls.specific)
            (file, specific_type, dxpl_id, req, arguments)) < 0)
            HGOTO_ERROR(H5E_VOL, H5E_CANTOPERATE, FAIL, "Unable to execute file specific callback")
    }
done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLfile_specific() */


/*-------------------------------------------------------------------------
 * Function:	H5VLfile_optional
 *
 * Purpose:	perform a plugin specific operation
 *
 * Return:	Success:        non negative
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              April, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLfile_optional(void *file, hid_t plugin_id, hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE5("e", "*xii**xx", file, plugin_id, dxpl_id, req, arguments);

    if(NULL == file)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if(NULL == vol_cls->file_cls.optional)
	HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `file optional' method")
    if((ret_value = (vol_cls->file_cls.optional)(file, dxpl_id, req, arguments)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTOPERATE, FAIL, "Unable to execute file optional callback")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLfile_optional() */


/*-------------------------------------------------------------------------
 * Function:	H5VLfile_close
 *
 * Purpose:	Closes a file through the VOL
 *
 * Return:	Success:	Non Negative
 *		Failure:	Negative
 *
 * Programmer:	Mohamad Chaarawi
 *              January, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLfile_close(void *file, hid_t plugin_id, hid_t dxpl_id, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE4("e", "*xii**x", file, plugin_id, dxpl_id, req);

    if(NULL == file)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if((ret_value = H5VL_file_close(file, vol_cls, dxpl_id, req)) < 0)
	HGOTO_ERROR(H5E_VOL, H5E_CANTRELEASE, FAIL, "unable to close file")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLfile_close() */


/*-------------------------------------------------------------------------
 * Function:	H5VLgroup_create
 *
 * Purpose:	Creates a group through the VOL
 * Return:      Success: pointer to new group.
 *
 *		Failure: NULL
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
void *
H5VLgroup_create(void *obj, H5VL_loc_params_t loc_params, hid_t plugin_id, const char *name, 
                 hid_t gcpl_id, hid_t gapl_id, hid_t dxpl_id, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    void *ret_value = NULL; /* Return value */

    FUNC_ENTER_API(NULL)
    H5TRACE8("*x", "*xxi*siii**x", obj, loc_params, plugin_id, name, gcpl_id,
             gapl_id, dxpl_id, req);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, NULL, "not a VOL plugin ID")

    if(NULL == (ret_value = H5VL_group_create(obj, loc_params, vol_cls, name, 
                                              gcpl_id, gapl_id, dxpl_id, req)))
	HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, NULL, "unable to create group")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLgroup_create() */


/*-------------------------------------------------------------------------
 * Function:	H5VLgroup_open
 *
 * Purpose:	Opens a group through the VOL
 *
 * Return:      Success: pointer to new group.
 *		Failure: NULL
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
void *
H5VLgroup_open(void *obj, H5VL_loc_params_t loc_params, hid_t plugin_id, const char *name, 
                hid_t gapl_id, hid_t dxpl_id, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    void *ret_value = NULL; /* Return value */

    FUNC_ENTER_API(NULL)
    H5TRACE7("*x", "*xxi*sii**x", obj, loc_params, plugin_id, name, gapl_id,
             dxpl_id, req);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, NULL, "not a VOL plugin ID")

    if(NULL == (ret_value = H5VL_group_open(obj, loc_params, vol_cls, name, 
                                              gapl_id, dxpl_id, req)))
	HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, NULL, "unable to open group")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLgroup_open() */


/*-------------------------------------------------------------------------
 * Function:	H5VLgroup_get
 *
 * Purpose:	Get specific information about the group through the VOL
 *
 * Return:	Success:        non negative
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              February, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLgroup_get(void *obj, hid_t plugin_id, H5VL_group_get_t get_type, 
              hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE6("e", "*xiVii**xx", obj, plugin_id, get_type, dxpl_id, req, arguments);

    if(NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    /* Bypass the H5VLint layer */
    if(NULL == vol_cls->group_cls.get)
	HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `group get' method")
    if((ret_value = (vol_cls->group_cls.get)(obj, get_type, dxpl_id, req, arguments)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "Unable to execute group get callback")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLgroup_get() */


/*-------------------------------------------------------------------------
 * Function:	H5VLgroup_specific
 *
 * Purpose:	specific operation on groups through the VOL
 *
 * Return:	Success:        non negative
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLgroup_specific(void *obj, hid_t plugin_id, H5VL_group_specific_t specific_type, 
                      hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE6("e", "*xiVji**xx", obj, plugin_id, specific_type, dxpl_id, req,
             arguments);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if(NULL == vol_cls->group_cls.specific)
	HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `group specific' method")
    if((ret_value = (vol_cls->group_cls.specific)
        (obj, specific_type, dxpl_id, req, arguments)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTOPERATE, FAIL, "Unable to execute group specific callback")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLgroup_specific() */


/*-------------------------------------------------------------------------
 * Function:	H5VLgroup_optional
 *
 * Purpose:	optional operation specific to plugins.
 *
 * Return:	Success:        non negative
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLgroup_optional(void *obj, hid_t plugin_id, hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE5("e", "*xii**xx", obj, plugin_id, dxpl_id, req, arguments);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if(NULL == vol_cls->group_cls.optional)
	HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `group optional' method")
    if((ret_value = (vol_cls->group_cls.optional)(obj, dxpl_id, req, arguments)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTOPERATE, FAIL, "Unable to execute group optional callback")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLgroup_optional() */


/*-------------------------------------------------------------------------
 * Function:	H5VLgroup_close
 *
 * Purpose:	Closes a group through the VOL
 *
 * Return:	Success:	Non Negative
 *		Failure:	Negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLgroup_close(void *grp, hid_t plugin_id, hid_t dxpl_id, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE4("e", "*xii**x", grp, plugin_id, dxpl_id, req);

    if(NULL == grp)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if((ret_value = H5VL_group_close(grp, vol_cls, dxpl_id, req)) < 0)
	HGOTO_ERROR(H5E_VOL, H5E_CANTRELEASE, FAIL, "unable to close group")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLgroup_close() */


/*-------------------------------------------------------------------------
 * Function:	H5VLlink_create
 *
 * Purpose:	Creates a hard link through the VOL
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Mohamad Chaarawi
 *              April, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLlink_create(H5VL_link_create_type_t create_type, void *obj, H5VL_loc_params_t loc_params, 
                 hid_t plugin_id, hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE8("e", "Vk*xxiiii**x", create_type, obj, loc_params, plugin_id, lcpl_id,
             lapl_id, dxpl_id, req);

    if(NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if((ret_value = H5VL_link_create(create_type, obj, loc_params, vol_cls, lcpl_id, lapl_id, dxpl_id, req)) < 0)
	HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "unable to create link")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLlink_create() */


/*-------------------------------------------------------------------------
 * Function:	H5VLlink_copy
 *
 * Purpose:	Copy a link from src to dst.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Mohamad Chaarawi
 *              April, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLlink_copy(void *src_obj, H5VL_loc_params_t loc_params1, void *dst_obj, 
              H5VL_loc_params_t loc_params2, hid_t plugin_id, 
              hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE9("e", "*xx*xxiiii**x", src_obj, loc_params1, dst_obj, loc_params2,
             plugin_id, lcpl_id, lapl_id, dxpl_id, req);

    if(NULL == src_obj || NULL == dst_obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if((ret_value = H5VL_link_copy(src_obj, loc_params1, dst_obj, loc_params2, vol_cls, 
                                   lcpl_id, lapl_id, dxpl_id, req)) < 0)
	HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "unable to copy object")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLlink_copy() */


/*-------------------------------------------------------------------------
 * Function:	H5VLlink_move
 *
 * Purpose:	Move a link from src to dst.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Mohamad Chaarawi
 *              April, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLlink_move(void *src_obj, H5VL_loc_params_t loc_params1, void *dst_obj, 
              H5VL_loc_params_t loc_params2, hid_t plugin_id, 
              hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE9("e", "*xx*xxiiii**x", src_obj, loc_params1, dst_obj, loc_params2,
             plugin_id, lcpl_id, lapl_id, dxpl_id, req);

    if(NULL == src_obj || NULL == dst_obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if((ret_value = H5VL_link_move(src_obj, loc_params1, dst_obj, loc_params2, vol_cls, 
                                   lcpl_id, lapl_id, dxpl_id, req)) < 0)
	HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "unable to move object")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLlink_move() */


/*-------------------------------------------------------------------------
 * Function:	H5VLlink_get
 *
 * Purpose:	Get specific information about the link through the VOL
 *
 * Return:	Success:        non negative
 *
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              April, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLlink_get(void *obj, H5VL_loc_params_t loc_params, hid_t plugin_id, H5VL_link_get_t get_type, 
              hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE7("e", "*xxiVli**xx", obj, loc_params, plugin_id, get_type, dxpl_id, req,
             arguments);

    if(NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if(NULL == vol_cls->link_cls.get)
	HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `link get' method")
    if((ret_value = (vol_cls->link_cls.get)
        (obj, loc_params, get_type, dxpl_id, req, arguments)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTOPERATE, FAIL, "Unable to execute link get callback")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLlink_get() */


/*-------------------------------------------------------------------------
 * Function:	H5VLlink_specific
 *
 * Purpose:	specific operation on links through the VOL
 *
 * Return:	Success:        non negative
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLlink_specific(void *obj, H5VL_loc_params_t loc_params, hid_t plugin_id, 
                  H5VL_link_specific_t specific_type, hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE7("e", "*xxiVmi**xx", obj, loc_params, plugin_id, specific_type,
             dxpl_id, req, arguments);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    /* Bypass the H5VLint layer */
    if(NULL == vol_cls->link_cls.specific)
	HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `link specific' method")
    if((ret_value = (vol_cls->link_cls.specific)
        (obj, loc_params, specific_type, dxpl_id, req, arguments)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTOPERATE, FAIL, "Unable to execute link specific callback")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLlink_specific() */


/*-------------------------------------------------------------------------
 * Function:	H5VLlink_optional
 *
 * Purpose:	optional operation specific to plugins.
 *
 * Return:	Success:        non negative
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLlink_optional(void *obj, hid_t plugin_id, hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE5("e", "*xii**xx", obj, plugin_id, dxpl_id, req, arguments);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    /* Have to bypass the H5VLint layer due to unknown val_list arguments */
    if(NULL == vol_cls->link_cls.optional)
	HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `link optional' method")
    if((ret_value = (vol_cls->link_cls.optional)(obj, dxpl_id, req, arguments)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTOPERATE, FAIL, "Unable to execute link optional callback")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLlink_optional() */


/*-------------------------------------------------------------------------
 * Function:	H5VLobject_open
 *
 * Purpose:	Opens a object through the VOL
 *
 * Return:      Success: User ID of the new object. 
 *		Failure: NULL
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
void *
H5VLobject_open(void *obj, H5VL_loc_params_t params, hid_t plugin_id, H5I_type_t *opened_type,
                hid_t dxpl_id, void **req)
{
    H5VL_class_t *vol_cls = NULL;
    void *ret_value = NULL; /* Return value */

    FUNC_ENTER_API(NULL)
    H5TRACE6("*x", "*xxi*Iti**x", obj, params, plugin_id, opened_type, dxpl_id, req);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, NULL, "not a VOL plugin ID")

    if(NULL == (ret_value = H5VL_object_open(obj, params, vol_cls, opened_type, dxpl_id, req)))
	HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, NULL, "unable to create group")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLobject_open() */


/*-------------------------------------------------------------------------
 * Function:	H5VLobject_copy
 *
 * Purpose:	Copies an object to another destination through the VOL
 *
 * Return:	Success:	Non Negative
 *		Failure:	Negative
 *
 * Programmer:	Mohamad Chaarawi
 *              April, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLobject_copy(void *src_obj, H5VL_loc_params_t loc_params1, hid_t plugin_id1, const char *src_name, 
                void *dst_obj, H5VL_loc_params_t loc_params2, hid_t plugin_id2, const char *dst_name, 
                hid_t ocpypl_id, hid_t lcpl_id, hid_t dxpl_id, void **req)
{
    H5VL_class_t *vol_cls1 = NULL;
    H5VL_class_t *vol_cls2 = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE12("e", "*xxi*s*xxi*siii**x", src_obj, loc_params1, plugin_id1,
             src_name, dst_obj, loc_params2, plugin_id2, dst_name, ocpypl_id,
             lcpl_id, dxpl_id, req);

    if(NULL == src_obj || NULL == dst_obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls1 = (H5VL_class_t *)H5I_object_verify(plugin_id1, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")
    if(NULL == (vol_cls2 = (H5VL_class_t *)H5I_object_verify(plugin_id2, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if((ret_value = H5VL_object_copy(src_obj, loc_params1, vol_cls1, src_name, 
                                     dst_obj, loc_params2, vol_cls2, dst_name,
                                     ocpypl_id, lcpl_id, dxpl_id, req)) < 0)
	HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "unable to move object")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLobject_copy() */


/*-------------------------------------------------------------------------
 * Function:	H5VLobject_get
 *
 * Purpose:	Get specific information about the object through the VOL
 *
 * Return:	Success:        non negative
 *
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              February, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLobject_get(void *obj, H5VL_loc_params_t loc_params, hid_t plugin_id, H5VL_object_get_t get_type, 
               hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE7("e", "*xxiVni**xx", obj, loc_params, plugin_id, get_type, dxpl_id, req,
             arguments);

    if(NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if(NULL == vol_cls->object_cls.get)
	HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `object get' method")
    if((ret_value = (vol_cls->object_cls.get)
        (obj, loc_params, get_type, dxpl_id, req, arguments)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTOPERATE, FAIL, "Unable to execute object get callback")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLobject_get() */


/*-------------------------------------------------------------------------
 * Function:	H5VLobject_specific
 *
 * Purpose:	specific operation on objects through the VOL
 *
 * Return:	Success:        non negative
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              April, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLobject_specific(void *obj, H5VL_loc_params_t loc_params, hid_t plugin_id, 
                    H5VL_object_specific_t specific_type, hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE7("e", "*xxiVoi**xx", obj, loc_params, plugin_id, specific_type,
             dxpl_id, req, arguments);

    if(NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    /* Bypass the H5VLint layer */
    if(NULL == vol_cls->object_cls.specific)
	HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `object specific' method")
    if((ret_value = (vol_cls->object_cls.specific)
        (obj, loc_params, specific_type, dxpl_id, req, arguments)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTOPERATE, FAIL, "Unable to execute object specific callback")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLobject_specific() */


/*-------------------------------------------------------------------------
 * Function:	H5VLobject_optional
 *
 * Purpose:	optional operation specific to plugins.
 *
 * Return:	Success:        non negative
 *		Failure:	negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLobject_optional(void *obj, hid_t plugin_id, hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE5("e", "*xii**xx", obj, plugin_id, dxpl_id, req, arguments);

    if (NULL == obj)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid object")
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    /* Have to bypass the H5VLint layer due to unknown val_list arguments */
    if(NULL == vol_cls->object_cls.optional)
	HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "vol plugin has no `object optional' method")
    if((ret_value = (vol_cls->object_cls.optional)(obj, dxpl_id, req, arguments)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTOPERATE, FAIL, "Unable to execute object optional callback")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLobject_optional() */


/*-------------------------------------------------------------------------
 * Function:	H5VLrequest_cancel
 *
 * Purpose:	Cancels a request through the VOL
 *
 * Return:	Success:	Non Negative
 *		Failure:	Negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2013
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLrequest_cancel(void **req, hid_t plugin_id, H5ES_status_t *status)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE3("e", "**xi*Es", req, plugin_id, status);

    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if((ret_value = H5VL_request_cancel(req, vol_cls, status)) < 0)
	HGOTO_ERROR(H5E_VOL, H5E_CANTRELEASE, FAIL, "unable to cancel request")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLrequest_cancel() */


/*-------------------------------------------------------------------------
 * Function:	H5VLrequest_test
 *
 * Purpose:	Tests a request through the VOL
 *
 * Return:	Success:	Non Negative
 *		Failure:	Negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2013
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLrequest_test(void **req, hid_t plugin_id, H5ES_status_t *status)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE3("e", "**xi*Es", req, plugin_id, status);

    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if((ret_value = H5VL_request_test(req, vol_cls, status)) < 0)
	HGOTO_ERROR(H5E_VOL, H5E_CANTRELEASE, FAIL, "unable to test request")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLrequest_test() */


/*-------------------------------------------------------------------------
 * Function:	H5VLrequest_wait
 *
 * Purpose:	Waits on a request through the VOL
 *
 * Return:	Success:	Non Negative
 *		Failure:	Negative
 *
 * Programmer:	Mohamad Chaarawi
 *              March, 2013
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VLrequest_wait(void **req, hid_t plugin_id, H5ES_status_t *status)
{
    H5VL_class_t *vol_cls = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE3("e", "**xi*Es", req, plugin_id, status);

    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")

    if((ret_value = H5VL_request_wait(req, vol_cls, status)) < 0)
	HGOTO_ERROR(H5E_VOL, H5E_CANTRELEASE, FAIL, "unable to wait on request")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5VLrequest_wait() */
