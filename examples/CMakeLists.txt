foreach(_examplename busMessage bytecoded codegenerated p2pClient p2pServer typesystem)
    add_executable(${_examplename} ${_examplename}.cpp)
    target_link_libraries(${_examplename} dfer)
endforeach()

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/codegenerated_reader.h"
                   COMMAND "${CMAKE_SOURCE_DIR}/serialization/codegen.py"
                           "(ia(ia(dda(id))))"
                           "MachineProgramReader"
                           "${CMAKE_CURRENT_BINARY_DIR}/codegenerated_reader.h"
                   VERBATIM
                   DEPENDS "${CMAKE_SOURCE_DIR}/serialization/ferCode.py"
                           "${CMAKE_SOURCE_DIR}/serialization/codegen.py"
                           "${CMAKE_SOURCE_DIR}/serialization/argumentscgreader_t.cpp")

add_custom_target(codegenerated_gen DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/codegenerated_reader.h")
add_dependencies(codegenerated codegenerated_gen)
target_include_directories(codegenerated PRIVATE "${CMAKE_CURRENT_BINARY_DIR}") # to find codegenerated_reader.h
