project(d-sel)

cmake_minimum_required(VERSION 2.6.2 FATAL_ERROR)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

add_definitions(-std=c++11)

option(DSEL_BUILD_DSELRIG "Build the dselrig bus monitor GUI" TRUE)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include_directories(${CMAKE_SOURCE_DIR}/buslogic
                    ${CMAKE_SOURCE_DIR}/client
                    ${CMAKE_SOURCE_DIR}/connection
                    ${CMAKE_SOURCE_DIR}/serialization)

set(DSEL_SOURCES
    buslogic/itransceiverclient.cpp
    buslogic/transceiver.cpp
    connection/authnegotiator.cpp
    connection/epolleventdispatcher.cpp
    connection/iconnection.cpp
    connection/iconnectionclient.cpp
    connection/ieventdispatcher.cpp
    connection/localsocket.cpp
    connection/pathfinder.cpp
    connection/stringtools.cpp
    serialization/argumentlist.cpp
    serialization/icompletionclient.cpp
    serialization/message.cpp
    serialization/types.cpp)

set(DSEL_HEADERS
    buslogic/itransceiverclient.h
    buslogic/transceiver.h
    connection/authnegotiator.h
    connection/epolleventdispatcher.h
    connection/iauthmechanism.h
    connection/iconnection.h
    connection/iconnectionclient.h
    connection/ieventdispatcher.h
    connection/localsocket.h
    connection/pathfinder.h
    connection/platform.h
    connection/stringtools.h
    serialization/argumentlist.h
    serialization/basictypeio.h
    serialization/icompletionclient.h
    serialization/message.h
    serialization/types.h)

add_library(dsel SHARED ${DSEL_SOURCES} ${DSEL_HEADERS})


find_package(LibTinyxml2 REQUIRED) # for the introspection parser in dselclient
include_directories(${LIBTINYXML2_INCLUDE_DIRS})

set(DSELCLIENT_SOURCES
    client/introspection.h)

set(DSELCLIENT_HEADERS
    client/introspection.cpp)

add_library(dselclient SHARED ${DSELCLIENT_SOURCES} ${DSELCLIENT_HEADERS})
target_link_libraries(dselclient dsel ${LIBTINYXML2_LIBRARIES})

install(TARGETS dsel dselclient LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)

add_subdirectory(tests)
add_subdirectory(utilities)
