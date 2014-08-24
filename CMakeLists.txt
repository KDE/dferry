project(dferry)

cmake_minimum_required(VERSION 2.6.2 FATAL_ERROR)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

add_definitions(-std=c++11)

option(DFERRY_BUILD_ANALYZER "Build the dfer-analyzer bus analyzer GUI" TRUE)

include(GNUInstallDirs)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include_directories(${CMAKE_SOURCE_DIR}/buslogic
                    ${CMAKE_SOURCE_DIR}/client
                    ${CMAKE_SOURCE_DIR}/connection
                    ${CMAKE_SOURCE_DIR}/events
                    ${CMAKE_SOURCE_DIR}/serialization)

set(DFER_SOURCES
    buslogic/itransceiverclient.cpp
    buslogic/peeraddress.cpp
    buslogic/transceiver.cpp
    connection/authnegotiator.cpp
    connection/iconnection.cpp
    connection/iconnectionclient.cpp
    connection/localsocket.cpp
    connection/stringtools.cpp
    events/epolleventpoller.cpp
    events/eventdispatcher.cpp
    events/ieventpoller.cpp
    serialization/argumentlist.cpp
    serialization/icompletionclient.cpp
    serialization/message.cpp
    serialization/types.cpp)

set(DFER_HEADERS
    buslogic/itransceiverclient.h
    buslogic/peeraddress.h
    buslogic/transceiver.h
    connection/authnegotiator.h
    connection/iauthmechanism.h
    connection/iconnection.h
    connection/iconnectionclient.h
    connection/localsocket.h
    connection/platform.h
    connection/stringtools.h
    events/epolleventpoller.h
    events/eventdispatcher.h
    events/ieventpoller.h
    serialization/argumentlist.h
    serialization/basictypeio.h
    serialization/icompletionclient.h
    serialization/message.h
    serialization/types.h)

add_library(dfer SHARED ${DFER_SOURCES} ${DFER_HEADERS})


find_package(LibTinyxml2 REQUIRED) # for the introspection parser in dferclient
include_directories(${LIBTINYXML2_INCLUDE_DIRS})

set(DFERCLIENT_SOURCES
    client/introspection.h)

set(DFERCLIENT_HEADERS
    client/introspection.cpp)

add_library(dferclient SHARED ${DFERCLIENT_SOURCES} ${DFERCLIENT_HEADERS})
target_link_libraries(dferclient dfer ${LIBTINYXML2_LIBRARIES})

install(TARGETS dfer dferclient LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})

add_subdirectory(tests)
add_subdirectory(utilities)
