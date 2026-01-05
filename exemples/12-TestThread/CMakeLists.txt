cmake_minimum_required(VERSION 3.10)
project(TestThread)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(TestThread
    TestThread.cpp
)

target_include_directories(TestThread PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
