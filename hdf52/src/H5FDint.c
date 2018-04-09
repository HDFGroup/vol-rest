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
 * Created:     H5FDint.c
 *
 * Purpose:     Internal routine for VFD operations
 *
 *-------------------------------------------------------------------------
 */

/****************/
/* Module Setup */
/****************/

#include "H5FDmodule.h"         /* This source code file is part of the H5FD module */


/***********/
/* Headers */
/***********/
#include "H5private.h"          /* Generic Functions                        */
#include "H5Eprivate.h"         /* Error handling                           */
#include "H5Fprivate.h"         /* File access                              */
#include "H5FDpkg.h"            /* File Drivers                             */
#include "H5Iprivate.h"         /* IDs                                      */


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
 * Function:    H5FD_locate_signature
 *
 * Purpose:     Finds the HDF5 superblock signature in a file.  The
 *              signature can appear at address 0, or any power of two
 *              beginning with 512.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_locate_signature(H5FD_io_info_t *fdio_info, haddr_t *sig_addr)
{
    H5FD_t         *file;
    haddr_t         addr;
    haddr_t         eoa;
    haddr_t         eof;
    uint8_t         buf[H5F_SIGNATURE_LEN];
    unsigned        n;
    unsigned        maxpow;
    herr_t          ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(fdio_info);
    HDassert(fdio_info->file);
    HDassert(sig_addr);

    file = fdio_info->file;

    /* Find the least N such that 2^N is larger than the file size */
    eof = H5FD_get_eof(file, H5FD_MEM_SUPER);
    eoa = H5FD_get_eoa(file, H5FD_MEM_SUPER);
    addr = MAX(eof, eoa);
    if (HADDR_UNDEF == addr)
        HGOTO_ERROR(H5E_IO, H5E_CANTINIT, FAIL, "unable to obtain EOF/EOA value")
    for (maxpow = 0; addr; maxpow++)
        addr >>= 1;
    maxpow = MAX(maxpow, 9);

    /* Search for the file signature at format address zero followed by
     * powers of two larger than 9.
     */
    for (n = 8; n < maxpow; n++) {
        addr = (8 == n) ? 0 : (haddr_t)1 << n;
        if (H5FD_set_eoa(file, H5FD_MEM_SUPER, addr + H5F_SIGNATURE_LEN) < 0)
            HGOTO_ERROR(H5E_IO, H5E_CANTINIT, FAIL, "unable to set EOA value for file signature")
        if (H5FD_read(fdio_info, H5FD_MEM_SUPER, addr, (size_t)H5F_SIGNATURE_LEN, buf) < 0)
            HGOTO_ERROR(H5E_IO, H5E_CANTINIT, FAIL, "unable to read file signature")
        if (!HDmemcmp(buf, H5F_SIGNATURE, (size_t)H5F_SIGNATURE_LEN))
            break;
    }

    /* If the signature was not found then reset the EOA value and return
     * HADDR_UNDEF.
     */
    if (n >= maxpow) {
        if (H5FD_set_eoa(file, H5FD_MEM_SUPER, eoa) < 0)
            HGOTO_ERROR(H5E_IO, H5E_CANTINIT, FAIL, "unable to reset EOA value")
        *sig_addr = HADDR_UNDEF;
    }
    else
        /* Set return value */
        *sig_addr = addr;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_locate_signature() */


/*-------------------------------------------------------------------------
 * Function:    H5FD_read
 *
 * Purpose:     Private version of H5FDread()
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_read(H5FD_io_info_t *fdio_info, H5FD_mem_t type, haddr_t addr,
    size_t size, void *buf/*out*/)
{
    H5FD_t         *file;
    H5P_genplist_t *io_dxpl;
    haddr_t         eoa = HADDR_UNDEF;
    herr_t          ret_value = SUCCEED;       /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    HDassert(fdio_info);
    HDassert(fdio_info->file);
    HDassert(fdio_info->file->cls);
    HDassert(TRUE == H5P_class_isa(H5P_CLASS(fdio_info->meta_dxpl), H5P_CLS_DATASET_XFER_g));
    HDassert(TRUE == H5P_class_isa(H5P_CLASS(fdio_info->raw_dxpl), H5P_CLS_DATASET_XFER_g));
    HDassert(buf);

    file = fdio_info->file;

    /* Set up proper DXPL for I/O */
    if (H5FD_MEM_DRAW == type)
        io_dxpl = fdio_info->raw_dxpl;
    else
        io_dxpl = fdio_info->meta_dxpl;

    /* Sanity check the dxpl type against the mem type */
#ifdef H5_DEBUG_BUILD
    {
        H5FD_dxpl_type_t dxpl_type;    /* Property indicating the type of the internal dxpl */

        /* get the dxpl type */
        /* XXX: Raises const warnings. Would need a get function that takes a constant pointer
         *      to squash this.
         */
        if (H5P_get(io_dxpl, H5FD_DXPL_TYPE_NAME, &dxpl_type) < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "can't retrieve dxpl type")

        /* we shouldn't be here if the dxpl is labeled with NO I/O */
        HDassert(H5FD_NOIO_DXPL != dxpl_type);

        if (H5FD_MEM_DRAW == type)
            HDassert(H5FD_RAWDATA_DXPL == dxpl_type);
        else
            HDassert(H5FD_METADATA_DXPL == dxpl_type);
    }
#endif /* H5_DEBUG_BUILD */

#ifndef H5_HAVE_PARALLEL
    /* The no-op case
     *
     * Do not return early for Parallel mode since the I/O could be a
     * collective transfer.
     */
    if (0 == size)
        HGOTO_DONE(SUCCEED)
#endif /* H5_HAVE_PARALLEL */

    if (HADDR_UNDEF == (eoa = (file->cls->get_eoa)(file, type)))
        HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, FAIL, "driver get_eoa request failed")

    /* If the file is open for SWMR read access, allow access to data past
     * the end of the allocated space (the 'eoa').  This is done because the
     * eoa stored in the file's superblock might be out of sync with the
     * objects being written within the file by the application performing
     * SWMR write operations.
     */
    if (!(file->access_flags & H5F_ACC_SWMR_READ) && ((addr + file->base_addr + size) > eoa))
        HGOTO_ERROR(H5E_ARGS, H5E_OVERFLOW, FAIL, "addr overflow, addr = %llu, size = %llu, eoa = %llu", (unsigned long long)(addr + file->base_addr), (unsigned long long)size, (unsigned long long)eoa)

    /* Dispatch to driver */
    if ((file->cls->read)(file, type, H5P_PLIST_ID(io_dxpl), addr + file->base_addr, size, buf) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_READERROR, FAIL, "driver read request failed")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_read() */


/*-------------------------------------------------------------------------
 * Function:    H5FD_write
 *
 * Purpose:     Private version of H5FDwrite()
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_write(const H5FD_io_info_t *fdio_info, H5FD_mem_t type, haddr_t addr,
    size_t size, const void *buf)
{
    H5FD_t         *file;
    H5P_genplist_t *io_dxpl;
    haddr_t         eoa = HADDR_UNDEF;
    herr_t          ret_value = SUCCEED;       /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    HDassert(fdio_info);
    HDassert(fdio_info->file);
    HDassert(fdio_info->file->cls);
    HDassert(TRUE == H5P_class_isa(H5P_CLASS(fdio_info->meta_dxpl), H5P_CLS_DATASET_XFER_g));
    HDassert(TRUE == H5P_class_isa(H5P_CLASS(fdio_info->raw_dxpl), H5P_CLS_DATASET_XFER_g));
    HDassert(buf);

    file = fdio_info->file;

    /* Set up proper DXPL for I/O */
    if (H5FD_MEM_DRAW == type)
        io_dxpl = fdio_info->raw_dxpl;
    else
        io_dxpl = fdio_info->meta_dxpl;

    /* Sanity check the dxpl type against the mem type */
#ifdef H5_DEBUG_BUILD
    {
        H5FD_dxpl_type_t dxpl_type;    /* Property indicating the type of the internal dxpl */

        /* get the dxpl type */
        if (H5P_get(io_dxpl, H5FD_DXPL_TYPE_NAME, &dxpl_type) < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "can't retrieve dxpl type")

        /* we shouldn't be here if the dxpl is labeled with NO I/O */
        HDassert(H5FD_NOIO_DXPL != dxpl_type);

        if (H5FD_MEM_DRAW == type)
            HDassert(H5FD_RAWDATA_DXPL == dxpl_type);
        else
            HDassert(H5FD_METADATA_DXPL == dxpl_type);
    }
#endif /* H5_DEBUG_BUILD */

#ifndef H5_HAVE_PARALLEL
    /* The no-op case
     *
     * Do not return early for Parallel mode since the I/O could be a
     * collective transfer.
     */
    if (0 == size)
        HGOTO_DONE(SUCCEED)
#endif /* H5_HAVE_PARALLEL */

    if (HADDR_UNDEF == (eoa = (file->cls->get_eoa)(file, type)))
        HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, FAIL, "driver get_eoa request failed")
    if ((addr + file->base_addr + size) > eoa)
        HGOTO_ERROR(H5E_ARGS, H5E_OVERFLOW, FAIL, "addr overflow, addr = %llu, size=%llu, eoa=%llu", 
                    (unsigned long long)(addr+ file->base_addr), (unsigned long long)size, (unsigned long long)eoa)

    /* Dispatch to driver */
    if ((file->cls->write)(file, type, H5P_PLIST_ID(io_dxpl), addr + file->base_addr, size, buf) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_WRITEERROR, FAIL, "driver write request failed")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_write() */


/*-------------------------------------------------------------------------
 * Function:    H5FD_set_eoa
 *
 * Purpose:     Private version of H5FDset_eoa()
 *
 *              This function expects the EOA is a RELATIVE address, i.e.
 *              relative to the base address.  This is NOT the same as the
 *              EOA stored in the superblock, which is an absolute
 *              address.  Object addresses are relative.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_set_eoa(H5FD_t *file, H5FD_mem_t type, haddr_t addr)
{
    herr_t      ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    HDassert(file && file->cls);
    HDassert(H5F_addr_defined(addr) && addr <= file->maxaddr);

    /* Dispatch to driver, convert to absolute address */
    if ((file->cls->set_eoa)(file, type, addr + file->base_addr) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, FAIL, "driver set_eoa request failed")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_set_eoa() */


/*-------------------------------------------------------------------------
 * Function:    H5FD_get_eoa
 *
 * Purpose:     Private version of H5FDget_eoa()
 *
 *              This function returns the EOA as a RELATIVE address, i.e.
 *              relative to the base address.  This is NOT the same as the
 *              EOA stored in the superblock, which is an absolute
 *              address.  Object addresses are relative.
 *
 * Return:      Success:    First byte after allocated memory
 *
 *              Failure:    HADDR_UNDEF
 *
 *-------------------------------------------------------------------------
 */
haddr_t
H5FD_get_eoa(const H5FD_t *file, H5FD_mem_t type)
{
    haddr_t ret_value = HADDR_UNDEF;    /* Return value */

    FUNC_ENTER_NOAPI(HADDR_UNDEF)

    HDassert(file && file->cls);

    /* Dispatch to driver */
    if (HADDR_UNDEF == (ret_value = (file->cls->get_eoa)(file, type)))
        HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, HADDR_UNDEF, "driver get_eoa request failed")

    /* Adjust for base address in file (convert to relative address) */
    ret_value -= file->base_addr;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_get_eoa() */


/*-------------------------------------------------------------------------
 * Function:    H5FD_get_eof
 *
 * Purpose:     Private version of H5FDget_eof()
 *
 *              This function returns the EOF as a RELATIVE address, i.e.
 *              relative to the base address.  This will be different
 *              from  the end of the physical file if there is a user
 *              block.
 *
 * Return:      Success:    The EOF address.
 *
 *              Failure:    HADDR_UNDEF
 *
 *-------------------------------------------------------------------------
 */
haddr_t
H5FD_get_eof(const H5FD_t *file, H5FD_mem_t type)
{
    haddr_t ret_value = HADDR_UNDEF;    /* Return value */

    FUNC_ENTER_NOAPI(HADDR_UNDEF)

    HDassert(file && file->cls);

    /* Dispatch to driver */
    if (file->cls->get_eof) {
        if (HADDR_UNDEF == (ret_value = (file->cls->get_eof)(file, type)))
            HGOTO_ERROR(H5E_VFL, H5E_CANTGET, HADDR_UNDEF, "driver get_eof request failed")
    }
    else
        ret_value = file->maxaddr;

    /* Adjust for base address in file (convert to relative address)  */
    ret_value -= file->base_addr;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_get_eof() */


/*-------------------------------------------------------------------------
* Function:     H5FD_driver_query
*
* Purpose:      Similar to H5FD_query(), but intended for cases when we don't
*               have a file available (e.g. before one is opened). Since we
*               can't use the file to get the driver, the driver is passed in
*               as a parameter.
*
* Return:       SUCCEED/FAIL
*
*-------------------------------------------------------------------------
*/
herr_t
H5FD_driver_query(const H5FD_class_t *driver, unsigned long *flags/*out*/)
{
    herr_t ret_value = SUCCEED;          /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    HDassert(driver);
    HDassert(flags);

    /* Check for the driver to query and then query it */
    if (driver->query)
        ret_value = (driver->query)(NULL, flags);
    else 
        *flags = 0;

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_driver_query() */

