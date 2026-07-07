cmake_minimum_required(VERSION 3.10)
project(QuicHandshakeLoopbackSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(QuicHandshakeLoopbackSelfTest
    QuicHandshakeLoopbackSelfTest.cpp
)

target_include_directories(QuicHandshakeLoopbackSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(QuicHandshakeLoopbackSelfTest PRIVATE SwQuicCore)

if(WIN32)
    target_link_libraries(QuicHandshakeLoopbackSelfTest PRIVATE ws2_32)
endif()

add_test(NAME QuicHandshakeLoopbackSelfTest COMMAND QuicHandshakeLoopbackSelfTest)
