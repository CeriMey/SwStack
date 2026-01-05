cmake_minimum_required(VERSION 3.10)
project(ObjectLifecycleTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ObjectLifecycleTest
    ObjectLifecycleTest.cpp
)

target_include_directories(ObjectLifecycleTest PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
