foreach(_testname introspection)
    add_executable(tst_${_testname} tst_${_testname}.cpp)
    set_target_properties(tst_${_testname} PROPERTIES COMPILE_FLAGS
                            -DTEST_DATADIR="\\"${CMAKE_CURRENT_SOURCE_DIR}\\"")
    target_link_libraries(tst_${_testname} testutil dferclient ${LIBTINYXML2_LIBRARIES})
    add_test(NAME client/${_testname} COMMAND tst_${_testname})
endforeach()
