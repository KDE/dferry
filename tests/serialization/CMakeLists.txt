MACRO(SERIALIZATIONTESTS)
    FOREACH(_testname ${ARGN})
        add_executable(tst_${_testname} tst_${_testname}.cpp)
        target_link_libraries(tst_${_testname} testutil dsel)
    ENDFOREACH(_testname)
ENDMACRO(SERIALIZATIONTESTS)

SERIALIZATIONTESTS(argumentlist message)
