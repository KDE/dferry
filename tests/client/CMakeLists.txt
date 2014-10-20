MACRO(CLIENTTESTS)
    FOREACH(_testname ${ARGN})
        add_executable(tst_${_testname} tst_${_testname}.cpp)
        set_target_properties(tst_${_testname} PROPERTIES COMPILE_FLAGS
                              -DTEST_DATADIR="\\"${CMAKE_CURRENT_SOURCE_DIR}\\"")
        target_link_libraries(tst_${_testname} testutil dferclient dfer)
        add_test(${_testname} tst_${_testname})
    ENDFOREACH(_testname)
ENDMACRO(CLIENTTESTS)

CLIENTTESTS(introspection)
