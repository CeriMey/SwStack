cmake_minimum_required(VERSION 3.10)
project(QuicPacketCodecSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(QuicPacketCodecSelfTest
    QuicPacketCodecSelfTest.cpp
)

target_include_directories(QuicPacketCodecSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(QuicPacketCodecSelfTest PRIVATE SwQuicCore)

if(WIN32)
    target_link_libraries(QuicPacketCodecSelfTest PRIVATE ws2_32)
endif()

add_test(NAME QuicPacketCodecSelfTest COMMAND QuicPacketCodecSelfTest)
