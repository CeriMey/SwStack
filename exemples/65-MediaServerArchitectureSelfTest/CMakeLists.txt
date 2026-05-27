cmake_minimum_required(VERSION 3.10)
project(MediaServerArchitectureSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(MediaServerArchitectureSelfTest
    MediaServerArchitectureSelfTest.cpp
)

target_include_directories(MediaServerArchitectureSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

if(WIN32)
    target_link_libraries(MediaServerArchitectureSelfTest PRIVATE ws2_32)
endif()

add_test(NAME MediaServerArchitectureSelfTest COMMAND MediaServerArchitectureSelfTest)
