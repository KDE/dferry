foreach(_testname connectaddress errorpropagation pendingreply server threads)
    add_executable(tst_${_testname} tst_${_testname}.cpp)
    set_target_properties(tst_${_testname}
                          PROPERTIES COMPILE_FLAGS -DTEST_DATADIR="\\"${CMAKE_CURRENT_SOURCE_DIR}\\"")
    target_link_libraries(tst_${_testname} testutil dfer)
    add_test(NAME connection/${_testname} COMMAND tst_${_testname})
endforeach()

if (UNIX)
    target_link_libraries(tst_threads pthread)
    target_link_libraries(tst_server pthread)
endif()
