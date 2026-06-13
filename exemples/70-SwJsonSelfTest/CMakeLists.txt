cmake_minimum_required(VERSION 3.10)
project(SwJsonSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SwJsonSelfTest
    SwJsonSelfTest.cpp
)

target_include_directories(SwJsonSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

add_test(NAME SwJsonSelfTest COMMAND SwJsonSelfTest)
