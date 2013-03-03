include_directories(${KDE4_INCLUDES})

set(dselrigpart_SOURCES
    dselrig_part.cpp
    eavesdroppermodel.cpp
    eavesdropperthread.cpp
    messagesortfilter.cpp)

kde4_add_plugin(dselrigpart ${dselrigpart_SOURCES})
target_link_libraries(dselrigpart ${KDE4_KPARTS_LIBS} dsel)

install(TARGETS dselrigpart LIBRARY DESTINATION ${PLUGIN_INSTALL_DIR})
install(FILES dselrig_part.desktop DESTINATION ${SERVICES_INSTALL_DIR})
