cmake_minimum_required(VERSION 3.10)
project(Http3ServerSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(Http3ServerSelfTest
    Http3ServerSelfTest.cpp
)

target_include_directories(Http3ServerSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(Http3ServerSelfTest PRIVATE SwQuicCore)

if(WIN32)
    target_link_libraries(Http3ServerSelfTest PRIVATE ws2_32)
endif()

add_test(NAME Http3ServerSelfTest COMMAND Http3ServerSelfTest)
