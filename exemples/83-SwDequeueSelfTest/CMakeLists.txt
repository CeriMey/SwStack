cmake_minimum_required(VERSION 3.10)
project(SwDequeueSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SwDequeueSelfTest
    SwDequeueSelfTest.cpp
)

target_include_directories(SwDequeueSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

add_test(NAME SwDequeueSelfTest COMMAND SwDequeueSelfTest)
