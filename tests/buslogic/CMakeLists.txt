foreach(_testname pendingreply threads)
    add_executable(tst_${_testname} tst_${_testname}.cpp)
    set_target_properties(tst_${_testname}
                          PROPERTIES COMPILE_FLAGS -DTEST_DATADIR="\\"${CMAKE_CURRENT_SOURCE_DIR}\\"")
    target_link_libraries(tst_${_testname} testutil dfer)
    add_test(buslogic/${_testname} tst_${_testname})
endforeach()

if (UNIX)
    target_link_libraries(tst_threads pthread)
endif()
