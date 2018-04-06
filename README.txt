    D. Building the REST VOL plugin against HDF5

        a) Building with autotools

        Included with the REST VOL source code is a script called
        called 'build_vol_autotools.sh', which is meant to do all of the work
        necessary in building HDF5 and then building the REST VOL against HDF5.
        However, if you wish to build the VOL manually, you should first proceed
        with building a version of HDF5 which utilizes the VOL layer, as per the
        instructions in Section C. above. The included HDF5 source distribution
        is one such version of HDF5, but may potentially be out of date. In
        order to ensure the latest source code is being used, please consult
        help@hdfgroup.org. With this available, the build process for the REST
        VOL should follow the familiar

        $ ./autogen.sh

        $ ./configure

        $ make

        method of building software with autotools.


        When building the REST VOL, there are a number of options that configure
        understands to help control the build process. If building using the
        'build_vol_autotools.sh' script, these options are as follows:


        -h        Prints out a help message indicating script usage and
                  available options.

        -d        Enables debugging information printouts within the REST VOL
                  plugin.

        -C        Enables debug information printouts from cURL within the REST
                  VOL plugin.

        -m        Enables memory usage tracking within the REST VOL plugin. This
                  option is mostly used to help diagnose any possible memory
                  leaks or other memory errors.

        -H DIR    Used to specify the directory where an HDF5 distribution that
                  uses the VOL layer has already been built. This is useful to
                  keep from having to rebuild HDF5 each time the
                  'build_vol_autotools.sh' script is invoked.

        -p DIR    Similar to 'configure --prefix', specifies where the REST VOL
                  should be installed to. Default is a directory named
                  'rest_vol_build' inside the source directory.

        -c DIR    Specifies the top-level directory where cURL is installed, if
                  cURL was not installed to a system path.

        -y DIR    Specifies the top-level directory where YAJL is installed, if
                  YAJL was not installed to a system path.

        -t        Build the tools with REST VOL support. Note that this option
                  is experimental and should not be used for the time being.

        After the build process has succeeded, an executable named
        'test_rest_vol' should have been created under the 'bin' directory
        inside the install directory for the REST VOL. This program tests a
        moderate amount of HDF5's public API functionality with the REST VOL and
        should be a good indicator of whether the REST VOL is working correctly
        in conjunction with a running HSDS instance.

