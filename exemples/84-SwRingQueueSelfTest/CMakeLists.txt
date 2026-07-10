cmake_minimum_required(VERSION 3.10)
project(SwRingQueueSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SwRingQueueSelfTest
    SwRingQueueSelfTest.cpp
)

target_include_directories(SwRingQueueSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

add_test(NAME SwRingQueueSelfTest COMMAND SwRingQueueSelfTest)
