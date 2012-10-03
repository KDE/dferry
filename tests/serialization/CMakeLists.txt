include_directories(${CMAKE_SOURCE_DIR}/serialization)

MACRO(SERIALIZATIONTESTS)
    FOREACH(_testname ${ARGN})
        add_executable(${_testname} ${_testname}.cpp)
        target_link_libraries(${_testname} testutil dsel)
    ENDFOREACH(_testname)
ENDMACRO(SERIALIZATIONTESTS)

SERIALIZATIONTESTS(argumentlist message)
