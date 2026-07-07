cmake_minimum_required(VERSION 3.10)
project(QuicHandshakeSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(QuicHandshakeSelfTest
    QuicHandshakeSelfTest.cpp
)

target_include_directories(QuicHandshakeSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(QuicHandshakeSelfTest PRIVATE SwQuicCore)

if(WIN32)
    target_link_libraries(QuicHandshakeSelfTest PRIVATE ws2_32)
endif()

add_test(NAME QuicHandshakeSelfTest COMMAND QuicHandshakeSelfTest)
