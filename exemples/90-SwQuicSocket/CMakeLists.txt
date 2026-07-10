cmake_minimum_required(VERSION 3.10)
project(SwQuicSocketSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SwQuicSocketSelfTest
    SwQuicSocketSelfTest.cpp
)

target_include_directories(SwQuicSocketSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(SwQuicSocketSelfTest PRIVATE SwQuicCore)

if(WIN32)
    target_link_libraries(SwQuicSocketSelfTest PRIVATE ws2_32)
endif()

add_test(NAME SwQuicSocketSelfTest COMMAND SwQuicSocketSelfTest)
