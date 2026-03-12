cmake_minimum_required(VERSION 3.10)
project(TestThreadPool)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(TestThreadPool
    TestThreadPool.cpp
)

target_include_directories(TestThreadPool PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
