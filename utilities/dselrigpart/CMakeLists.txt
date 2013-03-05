include_directories(${KDE4_INCLUDES} ${CMAKE_CURRENT_BINARY_DIR})

set(dselrigpart_SOURCES
    dselrig_part.cpp
    eavesdroppermodel.cpp
    eavesdropperthread.cpp
    mainwidget.cpp
    messagesortfilter.cpp)

kde4_add_ui_files(dselrigpart_SOURCES mainwidget.ui)

kde4_add_plugin(dselrigpart ${dselrigpart_SOURCES})
target_link_libraries(dselrigpart ${KDE4_KPARTS_LIBS} dsel)

install(TARGETS dselrigpart LIBRARY DESTINATION ${PLUGIN_INSTALL_DIR})
install(FILES dselrig_part.desktop DESTINATION ${SERVICES_INSTALL_DIR})
