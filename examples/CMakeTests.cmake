
##############################################################################
##############################################################################
###           T E S T I N G                                                ###
##############################################################################
##############################################################################
file (MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/red ${PROJECT_BINARY_DIR}/blue ${PROJECT_BINARY_DIR}/u2w)
if (BUILD_SHARED_LIBS)
  file (MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/RVEX-shared")
  file (MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/RVEX-shared/red ${PROJECT_BINARY_DIR}/RVEX-shared/blue ${PROJECT_BINARY_DIR}/RVEX-shared/u2w)
endif (BUILD_SHARED_LIBS)

if (BUILD_STATIC_LIBS)
  # Remove any output file left over from previous test run
  add_test (
      NAME EXAMPLES-clear-objects
      COMMAND    ${CMAKE_COMMAND}
          -E remove 
          Attributes.h5
          btrees_file.h5
          cmprss.h5
          default_file.h5
          dset.h5
          extend.h5 
          extlink_prefix_source.h5
          extlink_source.h5
          extlink_target.h5
          group.h5
          groups.h5
          hard_link.h5
          mount1.h5
          mount2.h5
          one_index_file.h5
          only_dspaces_and_attrs_file.h5
          only_huge_mesgs_file.h5
          REF_REG.h5
          refere.h5
          SDS.h5
          SDScompound.h5
          SDSextendible.h5
          Select.h5
          separate_indexes_file.h5
          small_lists_file.h5
          soft_link.h5
          subset.h5
          unix2win.h5
          blue/prefix_target.h5
          red/prefix_target.h5
          u2w/u2w_target.h5
          vds.h5
          vds-excalibur.h5
          vds-exclim.h5
          vds-percival.h5
          vds-percival-unlim.h5
          vds-percival-unlim-maxmin.h5
          a.h5
          b.h5
          c.h5
          d.h5
          vds-simpleIO.h5
          vds-eiger.h5
  )
  if (NOT "${last_test}" STREQUAL "")
    set_tests_properties (EXAMPLES-clear-objects PROPERTIES DEPENDS ${last_test})
  endif (NOT "${last_test}" STREQUAL "")
  set (last_test "EXAMPLES-clear-objects")

  foreach (example ${examples})
    add_test (NAME EXAMPLES-${example} COMMAND $<TARGET_FILE:${example}>)
    if (NOT "${last_test}" STREQUAL "")
      set_tests_properties (EXAMPLES-${example} PROPERTIES DEPENDS ${last_test})
    endif (NOT "${last_test}" STREQUAL "")
    set (last_test "EXAMPLES-${example}")
  endforeach (example ${examples})
endif ()

if (BUILD_SHARED_LIBS)
  # Remove any output file left over from previous test run
  add_test (
      NAME EXAMPLES-shared-clear-objects
      COMMAND    ${CMAKE_COMMAND}
          -E remove 
          Attributes.h5
          btrees_file.h5
          cmprss.h5
          default_file.h5
          dset.h5
          extend.h5 
          extlink_prefix_source.h5
          extlink_source.h5
          extlink_target.h5
          group.h5
          groups.h5
          hard_link.h5
          mount1.h5
          mount2.h5
          one_index_file.h5
          only_dspaces_and_attrs_file.h5
          only_huge_mesgs_file.h5
          REF_REG.h5
          refere.h5
          SDS.h5
          SDScompound.h5
          SDSextendible.h5
          Select.h5
          separate_indexes_file.h5
          small_lists_file.h5
          soft_link.h5
          subset.h5
          unix2win.h5
          blue/prefix_target.h5
          red/prefix_target.h5
          u2w/u2w_target.h5
      WORKING_DIRECTORY
          ${PROJECT_BINARY_DIR}/H5EX-shared
  )
  if (NOT "${last_test}" STREQUAL "")
    set_tests_properties (EXAMPLES-shared-clear-objects PROPERTIES DEPENDS ${last_test})
  endif (NOT "${last_test}" STREQUAL "")
  set (last_test "EXAMPLES-shared-clear-objects")

  foreach (example ${examples})
    add_test (NAME EXAMPLES-shared-${example} COMMAND $<TARGET_FILE:${example}-shared>)
    set_tests_properties (EXAMPLES-shared-${example} PROPERTIES WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/RVEX-shared)
    if (NOT "${last_test}" STREQUAL "")
      set_tests_properties (EXAMPLES-shared-${example} PROPERTIES DEPENDS ${last_test})
    endif (NOT "${last_test}" STREQUAL "")
    set (last_test "EXAMPLES-shared-${example}")
  endforeach (example ${examples})
endif (BUILD_SHARED_LIBS)
