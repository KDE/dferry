project(d-sel)

cmake_minimum_required(VERSION 2.6.2 FATAL_ERROR)
#set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

#include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/connection ${CMAKE_SOURCE_DIR}/serialization)

set(DSEL_SOURCES
    connection/authnegotiator.cpp
    connection/epolleventdispatcher.cpp
    connection/iconnection.cpp
    connection/iconnectionclient.cpp
    connection/ieventdispatcher.cpp
    connection/localsocket.cpp
    connection/pathfinder.cpp
    connection/stringtools.cpp
    serialization/argumentlist.cpp
    serialization/message.cpp
    serialization/types.cpp)

set(DSEL_HEADERS
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
    serialization/message.h
    serialization/types.h)

# other subdirs for later: connection, authentication, bus

add_library(dsel SHARED ${DSEL_SOURCES} ${DSEL_HEADERS})


add_subdirectory(tests)
add_subdirectory(utilities)
