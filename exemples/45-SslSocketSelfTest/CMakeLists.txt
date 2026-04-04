cmake_minimum_required(VERSION 3.10)
project(SslSocketSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SslSocketSelfTest
    SwSslSocketSelfTest.cpp
)

target_include_directories(SslSocketSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(WIN32)
    target_link_libraries(SslSocketSelfTest PRIVATE ws2_32 secur32)
endif()
