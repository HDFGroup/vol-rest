## This file should be placed in the root directory of your project.
## Then modify the CMakeLists.txt file in the root directory of your
## project to incorporate the testing dashboard.
## # The following are required to uses Dart and the Cdash dashboard
##   ENABLE_TESTING()
##   INCLUDE(CTest)
set (CTEST_NIGHTLY_START_TIME "18:00:00 CST")
set (CTEST_PROJECT_NAME "HDF5App")

set (UPDATE_TYPE git)

set (CTEST_TESTING_TIMEOUT 1200) 
