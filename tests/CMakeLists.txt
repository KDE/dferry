include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(TESTUTIL_SOURCES
    testutil.cpp)

set(TESTUTIL_HEADERS
    testutil.h)

add_library(testutil SHARED ${TESTUTIL_SOURCES} ${TESTUTIL_HEADERS})
target_compile_definitions(testutil PRIVATE -DBUILDING_TESTUTIL)

add_subdirectory(serialization)
add_subdirectory(events)
add_subdirectory(connection)
if (DFER_BUILD_CLIENTLIB)
    add_subdirectory(client)
endif()
