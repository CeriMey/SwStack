cmake_minimum_required(VERSION 3.10)
project(QuicHttp3FoundationsSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(QuicHttp3FoundationsSelfTest
    QuicHttp3FoundationsSelfTest.cpp
)

target_include_directories(QuicHttp3FoundationsSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(QuicHttp3FoundationsSelfTest PRIVATE SwQuicCore)

if(WIN32)
    target_link_libraries(QuicHttp3FoundationsSelfTest PRIVATE ws2_32)
endif()

add_test(NAME QuicHttp3FoundationsSelfTest COMMAND QuicHttp3FoundationsSelfTest)
