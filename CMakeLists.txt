project(d-sel)

cmake_minimum_required(VERSION 2.6.2 FATAL_ERROR)
#set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(DSEL_SOURCES
    serialization/argumentlist.cpp
    serialization/message.cpp
    serialization/types.cpp)

set(DSEL_HEADERS
    serialization/argumentlist.h
    serialization/basictypeio.h
    serialization/message.h
    serialization/types.h)

# other subdirs for later: connection, authentication, bus

add_library(dsel SHARED ${DSEL_SOURCES} ${DSEL_HEADERS})


add_subdirectory(tests)
