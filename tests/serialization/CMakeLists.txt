foreach(_testname arguments arguments_slow message fercode)
    add_executable(tst_${_testname} tst_${_testname}.cpp)
    target_link_libraries(tst_${_testname} testutil dfer)
    add_test(NAME serialization/${_testname} COMMAND tst_${_testname})
endforeach()

# tst_arguments_prepare collects type signatures, codegen.py generates marshalling code for them,
# then tst_arguments includes and tests the generated code.
add_executable(tst_arguments_prepare tst_arguments.cpp)
target_compile_definitions(tst_arguments_prepare PRIVATE COLLECT_SIGNATURES=1)
target_link_libraries(tst_arguments_prepare testutil dfer)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/tst_arguments_signatures.txt"
                   COMMAND tst_arguments_prepare
                           "${CMAKE_CURRENT_BINARY_DIR}/tst_arguments_signatures.txt"
                   DEPENDS tst_arguments_prepare)

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/cgreader.h"
                   COMMAND "${CMAKE_SOURCE_DIR}/serialization/codegen.py"
                           "-t"
                           "${CMAKE_CURRENT_BINARY_DIR}/cgreader.h"
                           "-i"
                           "${CMAKE_CURRENT_BINARY_DIR}/tst_arguments_signatures.txt"
                   DEPENDS "${CMAKE_SOURCE_DIR}/serialization/ferCode.py"
                           "${CMAKE_SOURCE_DIR}/serialization/codegen.py"
                           "${CMAKE_SOURCE_DIR}/serialization/argumentscgreader_t.cpp"
                           "${CMAKE_SOURCE_DIR}/serialization/argumentscgtester_t.cpp"
                           "${CMAKE_CURRENT_BINARY_DIR}/tst_arguments_signatures.txt")

add_custom_target(cgreader_gen DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/cgreader.h")
add_dependencies(tst_arguments cgreader_gen)
target_include_directories(tst_arguments PRIVATE "${CMAKE_CURRENT_BINARY_DIR}") # to find cgreader.h
