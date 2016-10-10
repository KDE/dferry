MACRO(SERIALIZATIONTESTS)
    FOREACH(_testname ${ARGN})
        add_executable(tst_${_testname} tst_${_testname}.cpp)
        target_link_libraries(tst_${_testname} testutil dfer)
        add_test(serialization/${_testname} tst_${_testname})
    ENDFOREACH(_testname)
ENDMACRO(SERIALIZATIONTESTS)

SERIALIZATIONTESTS(arguments arguments_slow message)
