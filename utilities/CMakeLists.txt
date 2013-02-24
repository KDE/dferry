include_directories(${CMAKE_SOURCE_DIR}/connection ${CMAKE_SOURCE_DIR}/serialization)

add_executable(dselpunk punk.cpp)
target_link_libraries(dselpunk dsel)
install(TARGETS dselpunk DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)

if (DSEL_BUILD_DSELRIG)
    find_package(KDE4 4.7.0) # the version is somewhat arbitrary, needs testing
    find_package(LibTinyxml2)

    if (KDE4_FOUND AND LIBTINYXML2_FOUND)
        add_subdirectory(dselrigpart)
        include_directories(${KDE4_INCLUDES})
        kde4_add_executable(dselrig dselrig.cpp rigmainwindow.cpp)
        target_link_libraries(dselrig ${KDE4_KPARTS_LIBS})
        install(TARGETS dselrig DESTINATION ${BIN_INSTALL_DIR})
        install(FILES dselrigui.rc DESTINATION ${DATA_INSTALL_DIR}/dselrig)
    else()
        message(STATUS "Not building the dselrig bus monitor GUI because KDE4 and/or tinyxml2 were not found.")
    endif()
endif()
