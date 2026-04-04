cmake_minimum_required(VERSION 3.10)
project(EmbeddedDbBench)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/src/core/storage/SwEmbeddedDb.h")
    message(WARNING "SwEmbeddedDb.h not found, skipping EmbeddedDbBench target.")
    return()
endif()

add_executable(EmbeddedDbBench
    EmbeddedDbBench.cpp
)

target_include_directories(EmbeddedDbBench PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(WIN32)
    target_link_libraries(EmbeddedDbBench PRIVATE ws2_32 shell32)
endif()
