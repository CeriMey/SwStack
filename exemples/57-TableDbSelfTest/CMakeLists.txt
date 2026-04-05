cmake_minimum_required(VERSION 3.10)
project(TableDbSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/src/core/storage/SwTableDb.h")
    message(WARNING "SwTableDb.h not found, skipping TableDbSelfTest target.")
    return()
endif()

add_executable(SwTableDbSelfTest
    SwTableDbSelfTest.cpp
)

target_include_directories(SwTableDbSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(WIN32)
    target_link_libraries(SwTableDbSelfTest PRIVATE ws2_32 shell32)
endif()
