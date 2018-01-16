#-----------------------------------------------------------------------------
# Define Sources, one application for multiple files
#-----------------------------------------------------------------------------
set (hdf5app_name "name_of_app")
set (hdf5app
    ${PROJECT_SOURCE_DIR}/hdf5app_file1.cpp
    ${PROJECT_SOURCE_DIR}/hdf5app_file2.cpp
    ${PROJECT_SOURCE_DIR}/hdf5app_file3.cpp
    ${PROJECT_SOURCE_DIR}/hdf5app_file4.hpp
)
