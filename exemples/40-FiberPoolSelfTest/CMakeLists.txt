cmake_minimum_required(VERSION 3.10)
project(FiberPoolSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(FiberPoolSelfTest
    FiberPoolSelfTest.cpp
)

target_include_directories(FiberPoolSelfTest PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
