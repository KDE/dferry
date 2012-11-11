include_directories(${CMAKE_SOURCE_DIR}/connection ${CMAKE_SOURCE_DIR}/serialization)

add_executable(dselpunk punk.cpp)
target_link_libraries(dselpunk dsel)
