
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "hdf5.h"

#define LOG 502

static herr_t H5VL_log_init(hid_t vipl_id);
static herr_t H5VL_log_term(hid_t vtpl_id);

/* Datatype callbacks */
static void *H5VL_log_datatype_commit(void *obj, H5VL_loc_params_t loc_params, const char *name, hid_t type_id, hid_t lcpl_id, hid_t tcpl_id, hid_t tapl_id, hid_t dxpl_id, void **req);
static void *H5VL_log_datatype_open(void *obj, H5VL_loc_params_t loc_params, const char *name, hid_t tapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_log_datatype_get(void *dt, H5VL_datatype_get_t get_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_log_datatype_close(void *dt, hid_t dxpl_id, void **req);

/* Dataset callbacks */
static void *H5VL_log_dataset_create(void *obj, H5VL_loc_params_t loc_params, const char *name, hid_t dcpl_id, hid_t dapl_id, hid_t dxpl_id, void **req);
static void *H5VL_log_dataset_open(void *obj, H5VL_loc_params_t loc_params, const char *name, hid_t dapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_log_dataset_read(void *dset, hid_t mem_type_id, hid_t mem_space_id,
                                    hid_t file_space_id, hid_t plist_id, void *buf, void **req);
static herr_t H5VL_log_dataset_write(void *dset, hid_t mem_type_id, hid_t mem_space_id,
                                     hid_t file_space_id, hid_t plist_id, const void *buf, void **req);
static herr_t H5VL_log_dataset_close(void *dset, hid_t dxpl_id, void **req);

/* File callbacks */
static void *H5VL_log_file_create(const char *name, unsigned flags, hid_t fcpl_id, hid_t fapl_id, hid_t dxpl_id, void **req);
static void *H5VL_log_file_open(const char *name, unsigned flags, hid_t fapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_log_file_get(void *file, H5VL_file_get_t get_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_log_file_close(void *file, hid_t dxpl_id, void **req);

/* Group callbacks */
static void *H5VL_log_group_create(void *obj, H5VL_loc_params_t loc_params, const char *name, hid_t gcpl_id, hid_t gapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_log_group_close(void *grp, hid_t dxpl_id, void **req);

/* Link callbacks */

/* Object callbacks */
static void *H5VL_log_object_open(void *obj, H5VL_loc_params_t loc_params, H5I_type_t *opened_type, hid_t dxpl_id, void **req);
static herr_t H5VL_log_object_specific(void *obj, H5VL_loc_params_t loc_params, H5VL_object_specific_t specific_type, hid_t dxpl_id, void **req, va_list arguments);

hid_t native_driver_id = -1;

static const H5VL_class_t H5VL_log_g = {
    0,
    LOG,
    "log",					/* name */
    H5VL_log_init,                              /* initialize */
    H5VL_log_term,                              /* terminate */
    sizeof(hid_t),
    NULL,
    NULL,
    {                                           /* attribute_cls */
        NULL, //H5VL_log_attr_create,                /* create */
        NULL, //H5VL_log_attr_open,                  /* open */
        NULL, //H5VL_log_attr_read,                  /* read */
        NULL, //H5VL_log_attr_write,                 /* write */
        NULL, //H5VL_log_attr_get,                   /* get */
        NULL, //H5VL_log_attr_specific,              /* specific */
        NULL, //H5VL_log_attr_optional,              /* optional */
        NULL  //H5VL_log_attr_close                  /* close */
    },
    {                                           /* dataset_cls */
        H5VL_log_dataset_create,                    /* create */
        H5VL_log_dataset_open,                      /* open */
        H5VL_log_dataset_read,                      /* read */
        H5VL_log_dataset_write,                     /* write */
        NULL, //H5VL_log_dataset_get,               /* get */
        NULL, //H5VL_log_dataset_specific,          /* specific */
        NULL, //H5VL_log_dataset_optional,          /* optional */
        H5VL_log_dataset_close                      /* close */
    },
    {                                               /* datatype_cls */
        H5VL_log_datatype_commit,                   /* commit */
        H5VL_log_datatype_open,                     /* open */
        H5VL_log_datatype_get,                      /* get_size */
        NULL, //H5VL_log_datatype_specific,         /* specific */
        NULL, //H5VL_log_datatype_optional,         /* optional */
        H5VL_log_datatype_close                     /* close */
    },
    {                                           /* file_cls */
        H5VL_log_file_create,                      /* create */
        H5VL_log_file_open,                        /* open */
        H5VL_log_file_get,                         /* get */
        NULL, //H5VL_log_file_specific,            /* specific */
        NULL, //H5VL_log_file_optional,            /* optional */
        H5VL_log_file_close                        /* close */
    },
    {                                           /* group_cls */
        H5VL_log_group_create,                     /* create */
        NULL, //H5VL_log_group_open,               /* open */
        NULL, //H5VL_log_group_get,                /* get */
        NULL, //H5VL_log_group_specific,           /* specific */
        NULL, //H5VL_log_group_optional,           /* optional */
        H5VL_log_group_close                       /* close */
    },
    {                                           /* link_cls */
        NULL, //H5VL_log_link_create,                /* create */
        NULL, //H5VL_log_link_copy,                  /* copy */
        NULL, //H5VL_log_link_move,                  /* move */
        NULL, //H5VL_log_link_get,                   /* get */
        NULL, //H5VL_log_link_specific,              /* specific */
        NULL, //H5VL_log_link_optional,              /* optional */
    },
    {                                           /* object_cls */
        H5VL_log_object_open,                        /* open */
        NULL, //H5VL_log_object_copy,                /* copy */
        NULL, //H5VL_log_object_get,                 /* get */
        H5VL_log_object_specific,                    /* specific */
        NULL, //H5VL_log_object_optional,            /* optional */
    },
    {
        NULL,
        NULL,
        NULL
    },
    NULL
};

typedef struct H5VL_log_t {
    void   *under_object;
} H5VL_log_t;

static herr_t
visit_cb(hid_t oid, const char *name,
         const H5O_info_t *oinfo, void *udata)
{
    ssize_t len;
    char n[25];

    if(H5Iget_type(oid) == H5I_GROUP) {
        len = H5VLget_driver_name(oid, n, 50);
        printf ("Visiting GROUP VOL name = %s  %d\n", n, len);
    }
    if(H5Iget_type(oid) == H5I_DATASET) 
        printf("visiting dataset\n");
    if(H5Iget_type(oid) == H5I_DATATYPE) 
        printf("visiting datatype\n");

    return 1;
} /* end h5_verify_cached_stabs_cb() */

int main(int argc, char **argv) {
        const char file_name[]="large_dataset.h5";
	const char group_name[]="/Group";
	const char dataset_name[]="Data";
	char fullpath[500];
	hid_t file_id;
	hid_t group_id;
	hid_t dataspaceId;
        hid_t datasetId;
        hid_t acc_tpl;
        hid_t under_fapl;
        hid_t vol_id, vol_id2;
        hid_t int_id;
        hid_t attr;
        hid_t space;
	const unsigned int nelem=60;
	int *data;
	unsigned int i;
	hsize_t dims[1];
        ssize_t len;
        char name[25];
        static hsize_t      ds_size[2] = {10, 20};

        under_fapl = H5Pcreate (H5P_FILE_ACCESS);
        H5Pset_fapl_native(under_fapl);
        assert(H5VLis_registered("native") == 1);

        vol_id = H5VLregister (&H5VL_log_g);
        assert(vol_id > 0);
        assert(H5VLis_registered("log") == 1);

        vol_id2 = H5VLget_driver_id("log");
        H5VLinitialize(vol_id2, H5P_DEFAULT);
        H5VLclose(vol_id2);

        native_driver_id = H5VLget_driver_id("native");
        assert(native_driver_id > 0);

        acc_tpl = H5Pcreate (H5P_FILE_ACCESS);
        H5Pset_vol(acc_tpl, vol_id, &under_fapl);

	file_id = H5Fcreate(file_name, H5F_ACC_TRUNC, H5P_DEFAULT, acc_tpl);
        len = H5VLget_driver_name(file_id, name, 25);
        printf ("FILE VOL name = %s  %d\n", name, len);

	group_id = H5Gcreate2(file_id, group_name, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        len = H5VLget_driver_name(group_id, name, 50);
        printf ("GROUP VOL name = %s  %d\n", name, len);

        int_id = H5Tcopy(H5T_NATIVE_INT);
        H5Tcommit2(file_id, "int", int_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        len = H5VLget_driver_name(int_id, name, 50);
        printf ("DT COMMIT name = %s  %d\n", name, len);
        H5Tclose(int_id);

        int_id = H5Topen2(file_id, "int", H5P_DEFAULT);
        len = H5VLget_driver_name(int_id, name, 50);
        printf ("DT OPEN name = %s  %d\n", name, len);
        H5Tclose(int_id);

        int_id = H5Oopen(file_id,"int",H5P_DEFAULT);
        len = H5VLget_driver_name(int_id, name, 50);
        printf ("DT OOPEN name = %s  %d\n", name, len);

        len = H5Fget_name(file_id, name, 50);
        printf("name = %d  %s\n", len, name);

	data = malloc (sizeof(int)*nelem);
	for(i=0;i<nelem;++i)
	  data[i]=i;

	dims [0] = 60;
	dataspaceId = H5Screate_simple(1, dims, NULL); 
        space = H5Screate_simple (2, ds_size, ds_size);

	sprintf(fullpath,"%s/%s",group_name,dataset_name);
	datasetId = H5Dcreate2(file_id,fullpath,H5T_NATIVE_INT,dataspaceId,H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
	H5Sclose(dataspaceId);

        len = H5VLget_driver_name(datasetId, name, 50);
        printf ("DSET name = %s  %d\n", name, len);

	H5Dwrite(datasetId, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);
	H5Dclose(datasetId);

        H5Ovisit(file_id, H5_INDEX_NAME, H5_ITER_NATIVE, visit_cb, NULL);

	free (data);
        H5Oclose(int_id);
        H5Sclose (space);
	H5Gclose(group_id);
#if 0

        attr = H5Acreate2(group_id, "attr1", int_id, space, H5P_DEFAULT, H5P_DEFAULT);
        int_id = H5Aget_type(attr);
        len = H5VLget_driver_name(int_id, name, 50);
        printf ("DT OPEN name = %s  %d\n", name, len);

        H5Aclose (attr);

        int_id = H5Oopen(file_id,"int",H5P_DEFAULT);
        len = H5VLget_driver_name(int_id, name, 50);
        printf ("DT OOPEN name = %s  %d\n", name, len);
        H5Oclose(int_id);


	H5Fclose(file_id);
	file_id = H5Fopen(file_name, H5F_ACC_RDWR, H5P_DEFAULT);//acc_tpl);
        H5Fflush(file_id, H5F_SCOPE_GLOBAL);
#endif

	H5Fclose(file_id);
        H5Pclose(acc_tpl);
        H5Pclose(under_fapl);

        H5VLclose(native_driver_id);
        H5VLterminate(vol_id, H5P_DEFAULT);
        H5VLunregister (vol_id);
        assert(H5VLis_registered("log") == 0);
	return 0;
}

static herr_t H5VL_log_init(hid_t vipl_id)
{
    printf("------- LOG INIT\n");
    return 0;
}

static herr_t H5VL_log_term(hid_t vtpl_id)
{
    printf("------- LOG TERM\n");
    return 0;
}

static void *
H5VL_log_file_create(const char *name, unsigned flags, hid_t fcpl_id, hid_t fapl_id, hid_t dxpl_id, void **req)
{
    hid_t under_fapl;
    H5VL_log_t *file;

    file = (H5VL_log_t *)calloc(1, sizeof(H5VL_log_t));

    under_fapl = *((hid_t *)H5Pget_vol_info(fapl_id));
    file->under_object = H5VLfile_create(name, flags, fcpl_id, under_fapl, dxpl_id, req);

    printf("------- LOG H5Fcreate\n");
    return (void *)file;
}

static void *
H5VL_log_file_open(const char *name, unsigned flags, hid_t fapl_id, hid_t dxpl_id, void **req)
{
    hid_t under_fapl;
    H5VL_log_t *file;

    file = (H5VL_log_t *)calloc(1, sizeof(H5VL_log_t));

    under_fapl = *((hid_t *)H5Pget_vol_info(fapl_id));
    file->under_object = H5VLfile_open(name, flags, under_fapl, dxpl_id, req);

    printf("------- LOG H5Fopen\n");
    return (void *)file;
}

static herr_t 
H5VL_log_file_get(void *file, H5VL_file_get_t get_type, hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_log_t *f = (H5VL_log_t *)file;

    H5VLfile_get(f->under_object, native_driver_id, get_type, dxpl_id, req, arguments);

    printf("------- LOG H5Fget %d\n", get_type);
    return 1;
}
static herr_t 
H5VL_log_file_close(void *file, hid_t dxpl_id, void **req)
{
    H5VL_log_t *f = (H5VL_log_t *)file;

    H5VLfile_close(f->under_object, native_driver_id, dxpl_id, req);
    free(f);

    printf("------- LOG H5Fclose\n");
    return 1;
}

static void *
H5VL_log_group_create(void *obj, H5VL_loc_params_t loc_params, const char *name, 
                      hid_t gcpl_id, hid_t gapl_id, hid_t dxpl_id, void **req)
{
    H5VL_log_t *group;
    H5VL_log_t *o = (H5VL_log_t *)obj;

    group = (H5VL_log_t *)calloc(1, sizeof(H5VL_log_t));

    group->under_object = H5VLgroup_create(o->under_object, loc_params, native_driver_id, name, gcpl_id,  gapl_id, dxpl_id, req);

    printf("------- LOG H5Gcreate\n");
    return (void *)group;
}

static herr_t 
H5VL_log_group_close(void *grp, hid_t dxpl_id, void **req)
{
    H5VL_log_t *g = (H5VL_log_t *)grp;

    H5VLgroup_close(g->under_object, native_driver_id, dxpl_id, req);
    free(g);

    printf("------- LOG H5Gclose\n");
    return 1;
}

static void *
H5VL_log_datatype_commit(void *obj, H5VL_loc_params_t loc_params, const char *name, 
                         hid_t type_id, hid_t lcpl_id, hid_t tcpl_id, hid_t tapl_id, hid_t dxpl_id, void **req)
{
    H5VL_log_t *dt;
    H5VL_log_t *o = (H5VL_log_t *)obj;

    dt = (H5VL_log_t *)calloc(1, sizeof(H5VL_log_t));

    dt->under_object = H5VLdatatype_commit(o->under_object, loc_params, native_driver_id, name, 
                                           type_id, lcpl_id, tcpl_id, tapl_id, dxpl_id, req);

    printf("------- LOG H5Tcommit\n");
    return dt;
}
static void *
H5VL_log_datatype_open(void *obj, H5VL_loc_params_t loc_params, const char *name, hid_t tapl_id, hid_t dxpl_id, void **req)
{
    H5VL_log_t *dt;
    H5VL_log_t *o = (H5VL_log_t *)obj;  

    dt = (H5VL_log_t *)calloc(1, sizeof(H5VL_log_t));

    dt->under_object = H5VLdatatype_open(o->under_object, loc_params, native_driver_id, name, tapl_id, dxpl_id, req);

    printf("------- LOG H5Topen\n");
    return (void *)dt;
}

static herr_t 
H5VL_log_datatype_get(void *dt, H5VL_datatype_get_t get_type, hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_log_t *o = (H5VL_log_t *)dt;
    herr_t ret_value;

    ret_value = H5VLdatatype_get(o->under_object, native_driver_id, get_type, dxpl_id, req, arguments);

    printf("------- LOG datatype get\n");
    return ret_value;
}

static herr_t 
H5VL_log_datatype_close(void *dt, hid_t dxpl_id, void **req)
{
    H5VL_log_t *type = (H5VL_log_t *)dt;

    assert(type->under_object);

    H5VLdatatype_close(type->under_object, native_driver_id, dxpl_id, req);
    free(type);

    printf("------- LOG H5Tclose\n");
    return 1;
}

static void *
H5VL_log_object_open(void *obj, H5VL_loc_params_t loc_params, H5I_type_t *opened_type, hid_t dxpl_id, void **req)
{
    H5VL_log_t *new_obj;
    H5VL_log_t *o = (H5VL_log_t *)obj;

    new_obj = (H5VL_log_t *)calloc(1, sizeof(H5VL_log_t));
    
    new_obj->under_object = H5VLobject_open(o->under_object, loc_params, native_driver_id, opened_type, dxpl_id, req);

    printf("------- LOG H5Oopen\n");
    return (void *)new_obj;
}

static herr_t 
H5VL_log_object_specific(void *obj, H5VL_loc_params_t loc_params, H5VL_object_specific_t specific_type, 
                         hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_log_t *o = (H5VL_log_t *)obj;

    H5VLobject_specific(o->under_object, loc_params, native_driver_id, specific_type, dxpl_id, req, arguments);

    printf("------- LOG Object specific\n");
    return 1;
}

static void *
H5VL_log_dataset_create(void *obj, H5VL_loc_params_t loc_params, const char *name, hid_t dcpl_id, hid_t dapl_id, hid_t dxpl_id, void **req) 
{
    H5VL_log_t *dset;
    H5VL_log_t *o = (H5VL_log_t *)obj;

    dset = (H5VL_log_t *)calloc(1, sizeof(H5VL_log_t));

    dset->under_object = H5VLdataset_create(o->under_object, loc_params, native_driver_id, name, dcpl_id,  dapl_id, dxpl_id, req);

    printf("------- LOG H5Dcreate\n");
    return (void *)dset;
}

static void *
H5VL_log_dataset_open(void *obj, H5VL_loc_params_t loc_params, const char *name, hid_t dapl_id, hid_t dxpl_id, void **req)
{
    H5VL_log_t *dset;
    H5VL_log_t *o = (H5VL_log_t *)obj;

    dset = (H5VL_log_t *)calloc(1, sizeof(H5VL_log_t));

    dset->under_object = H5VLdataset_open(o->under_object, loc_params, native_driver_id, name, dapl_id, dxpl_id, req);

    printf("------- LOG H5Dopen\n");
    return (void *)dset;
}

static herr_t 
H5VL_log_dataset_read(void *dset, hid_t mem_type_id, hid_t mem_space_id,
                      hid_t file_space_id, hid_t plist_id, void *buf, void **req)
{
    H5VL_log_t *d = (H5VL_log_t *)dset;

    H5VLdataset_read(d->under_object, native_driver_id, mem_type_id, mem_space_id, file_space_id, 
                     plist_id, buf, req);

    printf("------- LOG H5Dread\n");
    return 1;
}
static herr_t 
H5VL_log_dataset_write(void *dset, hid_t mem_type_id, hid_t mem_space_id,
                       hid_t file_space_id, hid_t plist_id, const void *buf, void **req)
{
    H5VL_log_t *d = (H5VL_log_t *)dset;

    H5VLdataset_write(d->under_object, native_driver_id, mem_type_id, mem_space_id, file_space_id, 
                     plist_id, buf, req);

    printf("------- LOG H5Dwrite\n");
    return 1;
}
static herr_t 
H5VL_log_dataset_close(void *dset, hid_t dxpl_id, void **req)
{
    H5VL_log_t *d = (H5VL_log_t *)dset;

    H5VLdataset_close(d->under_object, native_driver_id, dxpl_id, req);
    free(d);

    printf("------- LOG H5Dclose\n");
    return 1;
}

#if 0
static void *H5VL_log_attr_create(void *obj, H5VL_loc_params_t loc_params, const char *attr_name, hid_t acpl_id, hid_t aapl_id, hid_t dxpl_id, void **req){    
static herr_t H5VL_log_attr_close(void *attr, hid_t dxpl_id, void **req){

/* Datatype callbacks */


/* Dataset callbacks */
static void *H5VL_log_dataset_create(void *obj, H5VL_loc_params_t loc_params, const char *name, hid_t dcpl_id, hid_t dapl_id, hid_t dxpl_id, void **req){
static herr_t H5VL_log_dataset_close(void *dset, hid_t dxpl_id, void **req){

/* File callbacks */

    
static void *H5VL_log_file_open(const char *name, unsigned flags, hid_t fapl_id, hid_t dxpl_id, void **req){


/* Group callbacks */

static void *H5VL_log_group_open(void *obj, H5VL_loc_params_t loc_params, const char *name, hid_t gapl_id, hid_t dxpl_id, void **req){
static herr_t H5VL_log_group_get(void *obj, H5VL_group_get_t get_type, hid_t dxpl_id, void **req, va_list arguments){


/* Link callbacks */

/* Object callbacks */


#endif
