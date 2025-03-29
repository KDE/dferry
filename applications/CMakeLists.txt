include_directories(${CMAKE_SOURCE_DIR}/connection ${CMAKE_SOURCE_DIR}/serialization)

add_executable(dfer-bin dfer.cpp) # "dfer" is already the target name of the library
SET_TARGET_PROPERTIES(dfer-bin PROPERTIES OUTPUT_NAME dfer) # call the output binary "dfer" anyway
target_link_libraries(dfer-bin dfer)
install(TARGETS dfer-bin DESTINATION ${CMAKE_INSTALL_BINDIR})

if (DFERRY_BUILD_ANALYZER)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    if (TARGET Qt6::Widgets)
        add_subdirectory(analyzer)
    else()
        message(STATUS "Not building the dfer-analyzer GUI because Qt6 Widgets was not found.")
    endif()
endif()
