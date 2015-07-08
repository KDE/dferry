project(dferry)

cmake_minimum_required(VERSION 2.6.2 FATAL_ERROR)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=return-type ")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -fvisibility-inlines-hidden")
add_definitions(-std=c++11)

include(TestBigEndian)
if(${BIGENDIAN})
    add_definitions(-DBIGENDIAN)
endif()

option(DFERRY_BUILD_ANALYZER "Build the dfer-analyzer bus analyzer GUI" TRUE)

include(GNUInstallDirs)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include_directories(${CMAKE_SOURCE_DIR}/buslogic
                    ${CMAKE_SOURCE_DIR}/client
                    ${CMAKE_SOURCE_DIR}/connection
                    ${CMAKE_SOURCE_DIR}/events
                    ${CMAKE_SOURCE_DIR}/serialization
                    ${CMAKE_SOURCE_DIR}/util)

set(DFER_SOURCES
    buslogic/connectioninfo.cpp
    buslogic/imessagereceiver.cpp
    buslogic/pendingreply.cpp
    buslogic/transceiver.cpp
    connection/authnegotiator.cpp
    connection/iconnection.cpp
    connection/iconnectionclient.cpp
    connection/iserver.cpp
    connection/localserver.cpp
    connection/localsocket.cpp
    connection/stringtools.cpp
    events/epolleventpoller.cpp
    events/event.cpp
    events/eventdispatcher.cpp
    events/ieventpoller.cpp
    events/iioeventclient.cpp
    events/platformtime.cpp
    events/timer.cpp
    serialization/arguments.cpp
    serialization/message.cpp
    util/error.cpp
    util/icompletionclient.cpp
    util/types.cpp)

set(DFER_PUBLIC_HEADERS
    buslogic/connectioninfo.h
    buslogic/imessagereceiver.h
    buslogic/pendingreply.h
    buslogic/transceiver.h
    client/introspection.h
    events/eventdispatcher.h
    events/timer.h
    serialization/message.h
    serialization/arguments.h
    util/commutex.h
    util/error.h
    util/export.h
    util/icompletionclient.h
    util/types.h
    util/valgrind-noop.h)

set(DFER_PRIVATE_HEADERS
    connection/authnegotiator.h
    connection/iauthmechanism.h
    connection/iconnection.h
    connection/iconnectionclient.h
    connection/iserver.h
    connection/localserver.h
    connection/localsocket.h
    connection/platform.h
    connection/stringtools.h
    events/event.h
    events/epolleventpoller.h
    events/ieventpoller.h
    events/iioeventclient.h
    events/platformtime.h
    serialization/basictypeio.h)

set(DFER_HEADERS ${DFER_PUBLIC_HEADERS} ${DFER_PRIVATE_HEADERS})

add_library(dfer SHARED ${DFER_SOURCES} ${DFER_HEADERS})

find_package(LibTinyxml2 REQUIRED) # for the introspection parser in dferclient
include_directories(${LIBTINYXML2_INCLUDE_DIRS})

find_package(Valgrind) # for checking homemade multithreading primitives
if (VALGRIND_INCLUDE_DIR)
   add_definitions(-DHAVE_VALGRIND)
   include_directories(${VALGRIND_INCLUDE_DIR})
endif()

set(DFERCLIENT_SOURCES
    client/introspection.h)

set(DFERCLIENT_HEADERS
    client/introspection.cpp)

add_library(dferclient SHARED ${DFERCLIENT_SOURCES} ${DFERCLIENT_HEADERS})
target_link_libraries(dferclient dfer ${LIBTINYXML2_LIBRARIES})

install(TARGETS dfer dferclient LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})
install(FILES ${DFER_PUBLIC_HEADERS} DESTINATION ${CMAKE_INSTALL_PREFIX}/include/dferry)

enable_testing() # need this here to get the "test" target in the toplevel build dir
add_subdirectory(tests)
add_subdirectory(applications)
