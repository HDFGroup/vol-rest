#-----------------------------------------------------------------------------
# Define Sources, one application for multiple files
#-----------------------------------------------------------------------------
set (hdf5app_name "name_of_app")
set (hdf5app
    ${PROJECT_SOURCE_DIR}/hdf5app_file1.c
    ${PROJECT_SOURCE_DIR}/hdf5app_file2.c
    ${PROJECT_SOURCE_DIR}/hdf5app_file3.c
    ${PROJECT_SOURCE_DIR}/hdf5app_file4.h
)
