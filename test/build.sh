export HDF5_DIR=/us
export H5CC=gcc
export LD_LIBRARY_PATH=${HDF5_DIR}/lib
# $H5CC -lhdf5_vol_rest -lcurl -lyajl test_rest_vol.c -o test_rest_vol
$H5CC -lhdf5  -lhdf5_vol_rest test_rest_vol.c -o test_rest_vol