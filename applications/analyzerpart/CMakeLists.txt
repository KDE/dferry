include_directories(${KDE4_INCLUDES} ${CMAKE_CURRENT_BINARY_DIR})

set(dferanalyzerpart_SOURCES
    argumentsmodel.cpp
    dferanalyzer_part.cpp
    eavesdroppermodel.cpp
    eavesdropperthread.cpp
    mainwidget.cpp
    messagesortfilter.cpp)

kde4_add_ui_files(dferanalyzerpart_SOURCES mainwidget.ui)

kde4_add_plugin(dferanalyzerpart ${dferanalyzerpart_SOURCES})
target_link_libraries(dferanalyzerpart ${KDE4_KPARTS_LIBS} dfer)

install(TARGETS dferanalyzerpart LIBRARY DESTINATION ${PLUGIN_INSTALL_DIR})
install(FILES dferanalyzer_part.desktop DESTINATION ${SERVICES_INSTALL_DIR})
