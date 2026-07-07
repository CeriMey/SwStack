cmake_minimum_required(VERSION 3.10)
project(QuicHandshakeProbe)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(QuicHandshakeProbe
    QuicHandshakeProbe.cpp
)

target_include_directories(QuicHandshakeProbe PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(QuicHandshakeProbe PRIVATE SwQuicCore)

if(WIN32)
    target_link_libraries(QuicHandshakeProbe PRIVATE ws2_32)
endif()
