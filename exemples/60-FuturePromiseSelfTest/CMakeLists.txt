cmake_minimum_required(VERSION 3.10)
project(FuturePromiseSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(FuturePromiseSelfTest
    FuturePromiseSelfTest.cpp
)

target_include_directories(FuturePromiseSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)
