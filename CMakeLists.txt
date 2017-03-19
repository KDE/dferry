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
endif()

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
    connection/ipserver.cpp
    connection/ipsocket.cpp
    connection/iserver.cpp
    connection/stringtools.cpp
    events/event.cpp
    events/eventdispatcher.cpp
    events/ieventpoller.cpp
    events/iioeventclient.cpp
    events/platformtime.cpp
    events/timer.cpp
    serialization/arguments.cpp
    serialization/argumentsreader.cpp
    serialization/argumentswriter.cpp
    serialization/message.cpp
    util/error.cpp
    util/icompletionclient.cpp
    util/types.cpp)
if (UNIX)
    list(APPEND DFER_SOURCES
        connection/localserver.cpp
        connection/localsocket.cpp)
endif()

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
    connection/ipserver.h
    connection/ipsocket.h
    connection/iserver.h
    connection/platform.h
    connection/stringtools.h
    events/event.h
    events/ieventpoller.h
    events/iioeventclient.h
    events/platformtime.h
    serialization/basictypeio.h)
if (UNIX)
    list(APPEND DFER_PRIVATE_HEADERS
         connection/localserver.h
         connection/localsocket.h)
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
