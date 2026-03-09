cmake_minimum_required(VERSION 3.10)
project(HttpServerSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/src/core/io/SwHttpServer.h")
    message(WARNING "SwHttpServer.h not found, skipping HttpServerSelfTest target.")
    return()
endif()

add_executable(HttpServerSelfTest
    SwHttpServerSelfTest.cpp
)

target_include_directories(HttpServerSelfTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(WIN32)
    target_link_libraries(HttpServerSelfTest PRIVATE ws2_32)
endif()
