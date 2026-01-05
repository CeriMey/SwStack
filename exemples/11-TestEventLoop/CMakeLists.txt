cmake_minimum_required(VERSION 3.10)
project(TestEventLoop)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(TestEventLoop
    TestEventLoop.cpp
)

target_include_directories(TestEventLoop PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
