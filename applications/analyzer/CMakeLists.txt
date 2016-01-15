set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

set(analyzer_SOURCES
    analyzer.cpp
    analyzermainwindow.cpp
    argumentsmodel.cpp
    eavesdroppermodel.cpp
    eavesdropperthread.cpp
    mainwidget.cpp
    messagesortfilter.cpp)

qt5_wrap_ui(analyzer_SOURCES mainwidget.ui)

add_executable(dfer-analyzer ${analyzer_SOURCES})
target_link_libraries(dfer-analyzer Qt5::Widgets dfer)

install(TARGETS dfer-analyzer DESTINATION ${CMAKE_INSTALL_BINDIR})
