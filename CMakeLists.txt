project(d-sel)

cmake_minimum_required(VERSION 2.6.2 FATAL_ERROR)
#set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(DSEL_SOURCES
    serialization/message.cpp)
set(DSEL_HEADERS
    serialization/message.h)
# other subdirs for later: connection, authentication, bus

add_library(dsel SHARED ${DSEL_SOURCES} ${DSEL_HEADERS})


add_subdirectory(tests)