cmake_minimum_required(VERSION 3.10)
project(HttpsDownloadTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/src/core/io/SwHttpApp.h")
    message(WARNING "SwHttpApp.h not found, skipping HttpsDownloadTest target.")
    return()
endif()

add_executable(HttpsDownloadTest
    SwHttpsDownloadTest.cpp
)

target_include_directories(HttpsDownloadTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(WIN32)
    target_link_libraries(HttpsDownloadTest PRIVATE ws2_32 secur32)
endif()
