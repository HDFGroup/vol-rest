#-----------------------------------------------------------------------------
# Define Sources, one application for multiple files
#-----------------------------------------------------------------------------
set (hdf5app_name "name_of_app")
set (hdf5app
    ${PROJECT_SOURCE_DIR}/hdf5app_file1.f90
    ${PROJECT_SOURCE_DIR}/hdf5app_file2.f90
    ${PROJECT_SOURCE_DIR}/hdf5app_file3.f90
    ${PROJECT_SOURCE_DIR}/hdf5app_file4.f90
)
