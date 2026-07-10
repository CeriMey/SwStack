cmake_minimum_required(VERSION 3.10)
project(SwByteRingBufferSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SwByteRingBufferSelfTest
    SwByteRingBufferSelfTest.cpp
)

target_include_directories(SwByteRingBufferSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

add_test(NAME SwByteRingBufferSelfTest COMMAND SwByteRingBufferSelfTest)
