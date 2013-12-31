include_directories(${CMAKE_SOURCE_DIR}/connection ${CMAKE_SOURCE_DIR}/serialization)

add_executable(dfer-bin dfer.cpp) # "dfer" is already the target name of the library
SET_TARGET_PROPERTIES(dfer-bin PROPERTIES OUTPUT_NAME dfer) # call the output binary "dfer" anyway
target_link_libraries(dfer-bin dfer)
install(TARGETS dfer-bin DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)

if (DFERRY_BUILD_ANALYZER)
    find_package(KDE4 4.7.0) # the version is somewhat arbitrary, needs testing

    if (KDE4_FOUND)
        add_subdirectory(analyzerpart)
        include_directories(${KDE4_INCLUDES})
        kde4_add_executable(dfer-analyzer analyzer.cpp analyzermainwindow.cpp)
        target_link_libraries(dfer-analyzer ${KDE4_KPARTS_LIBS})
        install(TARGETS dfer-analyzer DESTINATION ${BIN_INSTALL_DIR})
        install(FILES analyzerui.rc DESTINATION ${DATA_INSTALL_DIR}/dfer)
    else()
        message(STATUS "Not building the dfer-analyzer GUI because KDE4 was not found.")
    endif()
endif()
