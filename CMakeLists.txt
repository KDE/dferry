project(dferry)

cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

if (CMAKE_COMPILER_IS_GNUCXX)
   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wpedantic -Wextra -Werror -Wno-error=unused-result")
   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -fvisibility-inlines-hidden")
endif()
set(CMAKE_CXX_STANDARD 11)

if (WIN32 AND CMAKE_SYSTEM_VERSION VERSION_LESS 6.0)
    message(FATAL_ERROR "Windows Vista or later is required.")
endif()

include(TestBigEndian)
if (BIGENDIAN)
    add_definitions(-DBIGENDIAN)
endif()
if (UNIX)
    add_definitions(-D__unix__) # help for platforms that don't define this standard macro
endif()

option(DFERRY_BUILD_ANALYZER "Build the dfer-analyzer bus analyzer GUI" TRUE)

include(GNUInstallDirs)

if (WIN32)
    # Windows doesn't have an RPATH equivalent, so just make sure that all .dll and .exe files
    # are located together, so that the .exes find the .dlls at runtime
    set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
else()
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

    set(CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_FULL_LIBDIR}) # add libdfer install dir to rpath
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE) # add Qt (etc.) dir to rpath, if necessary
endif()

include_directories(${CMAKE_SOURCE_DIR}/client
                    ${CMAKE_SOURCE_DIR}/connection
                    ${CMAKE_SOURCE_DIR}/events
                    ${CMAKE_SOURCE_DIR}/serialization
                    ${CMAKE_SOURCE_DIR}/transport
                    ${CMAKE_SOURCE_DIR}/util)

set(DFER_SOURCES
    connection/authclient.cpp
    connection/connectaddress.cpp
    connection/connection.cpp
    connection/iconnectionstatelistener.cpp
    connection/imessagereceiver.cpp
    connection/inewconnectionlistener.cpp
    connection/pendingreply.cpp
    connection/server.cpp
    events/event.cpp
    events/eventdispatcher.cpp
    events/foreigneventloopintegrator.cpp
    events/ieventpoller.cpp
    events/iioeventforwarder.cpp
    events/iioeventlistener.cpp
    events/iioeventsource.cpp
    events/platformtime.cpp
    events/timer.cpp
    serialization/arguments.cpp
    serialization/argumentsreader.cpp
    serialization/argumentswriter.cpp
    serialization/message.cpp
    transport/ipserver.cpp
    transport/ipsocket.cpp
    transport/iserver.cpp
    transport/itransport.cpp
    transport/itransportlistener.cpp
    transport/stringtools.cpp
    util/error.cpp
    util/icompletionlistener.cpp
    util/types.cpp)
if (UNIX)
    list(APPEND DFER_SOURCES
        transport/localserver.cpp
        transport/localsocket.cpp)
endif()

set(DFER_PUBLIC_HEADERS
    connection/connectaddress.h
    connection/connection.h
    connection/iconnectionstatelistener.h
    connection/imessagereceiver.h
    connection/inewconnectionlistener.h
    connection/pendingreply.h
    connection/server.h
    client/introspection.h
    events/eventdispatcher.h
    events/foreigneventloopintegrator.h
    events/timer.h
    serialization/message.h
    serialization/arguments.h
    util/commutex.h
    util/error.h
    util/export.h
    util/icompletionlistener.h
    util/types.h
    util/valgrind-noop.h)

set(DFER_PRIVATE_HEADERS
    connection/authclient.h
    events/event.h
    events/ieventpoller.h
    events/iioeventforwarder.h
    events/iioeventlistener.h
    events/iioeventsource.h
    events/platformtime.h
    serialization/basictypeio.h
    transport/ipserver.h
    transport/ipsocket.h
    transport/iserver.h
    transport/itransport.h
    transport/itransportlistener.h
    transport/platform.h
    transport/stringtools.h)
if (UNIX)
    list(APPEND DFER_PRIVATE_HEADERS
         transport/localserver.h
         transport/localsocket.h)
endif()
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND DFER_SOURCES events/epolleventpoller.cpp)
    list(APPEND DFER_PRIVATE_HEADERS events/epolleventpoller.h)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND DFER_PRIVATE_HEADERS events/selecteventpoller_win32.h util/winutil.h)
    list(APPEND DFER_SOURCES events/selecteventpoller_win32.cpp util/winutil.cpp)
elseif(UNIX)
    list(APPEND DFER_PRIVATE_HEADERS events/selecteventpoller_unix.h)
    list(APPEND DFER_SOURCES events/selecteventpoller_unix.cpp)
else()
    message(FATAL_ERROR "This operating system is not supported.")
endif()

set(DFER_HEADERS ${DFER_PUBLIC_HEADERS} ${DFER_PRIVATE_HEADERS})

add_library(dfer SHARED ${DFER_SOURCES} ${DFER_HEADERS})
target_include_directories(dfer INTERFACE "$<INSTALL_INTERFACE:include/dferry>")
if (WIN32)
    target_link_libraries(dfer PRIVATE ws2_32)
endif()

find_package(LibTinyxml2 REQUIRED) # for the introspection parser in dferclient
include_directories(${LIBTINYXML2_INCLUDE_DIRS})

find_package(Valgrind) # for checking homemade multithreading primitives
if (VALGRIND_INCLUDE_DIR)
   add_definitions(-DHAVE_VALGRIND)
   include_directories(${VALGRIND_INCLUDE_DIR})
endif()

 # for small_vector, optional; small_vector appeared in 1.58
find_package(Boost 1.58)
if (BOOST_FOUND)
    add_definitions(-DHAVE_BOOST)
endif()

set(DFERCLIENT_SOURCES
    client/introspection.h)

set(DFERCLIENT_HEADERS
    client/introspection.cpp)

add_library(dferclient SHARED ${DFERCLIENT_SOURCES} ${DFERCLIENT_HEADERS})
target_include_directories(dferclient INTERFACE "$<INSTALL_INTERFACE:include/dferry>")
target_link_libraries(dferclient PUBLIC dfer PRIVATE ${LIBTINYXML2_LIBRARIES})

install(TARGETS dfer dferclient EXPORT dferryExports DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES ${DFER_PUBLIC_HEADERS} DESTINATION include/dferry)

enable_testing() # need this here to get the "test" target in the toplevel build dir
add_subdirectory(tests)
add_subdirectory(applications)

set(configModuleLocation "lib/cmake/dferry")

install(EXPORT dferryExports DESTINATION "${configModuleLocation}" FILE dferryTargets.cmake)

file(WRITE ${PROJECT_BINARY_DIR}/dferryConfig.cmake
     "include(\"\${CMAKE_CURRENT_LIST_DIR}/dferryTargets.cmake\")")
install(FILES "${PROJECT_BINARY_DIR}/dferryConfig.cmake"
        DESTINATION "${configModuleLocation}")
