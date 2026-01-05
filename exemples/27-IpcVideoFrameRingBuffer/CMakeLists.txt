cmake_minimum_required(VERSION 3.10)
project(IpcVideoFrameRingBuffer)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(IpcVideoFrameRingBuffer
    IpcVideoFrameRingBuffer.cpp
)

target_include_directories(IpcVideoFrameRingBuffer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}
)

