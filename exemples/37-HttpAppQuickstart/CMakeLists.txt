cmake_minimum_required(VERSION 3.10)
project(HttpAppQuickstart)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/src/core/io/SwHttpApp.h")
    message(WARNING "SwHttpApp.h not found, skipping HttpAppQuickstart target.")
    return()
endif()

add_executable(HttpAppQuickstart
    SwHttpAppQuickstart.cpp
)

target_include_directories(HttpAppQuickstart PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(WIN32)
    target_link_libraries(HttpAppQuickstart PRIVATE ws2_32)
endif()
