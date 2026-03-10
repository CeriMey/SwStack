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

set(SW_HTTP_SERVER_SELFTEST_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
file(TO_CMAKE_PATH "${SW_HTTP_SERVER_SELFTEST_RUNTIME_DIR}/http_selftest_www"
     SW_HTTP_SERVER_SELFTEST_STATIC_ROOT)
file(TO_CMAKE_PATH "${SW_HTTP_SERVER_SELFTEST_RUNTIME_DIR}/http_selftest_tmp"
     SW_HTTP_SERVER_SELFTEST_TEMP_ROOT)

configure_file(
    SwHttpServerSelfTestConfig.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/SwHttpServerSelfTestConfig.h
    @ONLY
)

target_include_directories(HttpServerSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

if(WIN32)
    target_link_libraries(HttpServerSelfTest PRIVATE ws2_32)
endif()
