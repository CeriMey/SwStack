cmake_minimum_required(VERSION 3.10)
project(Http3ServerOverUdpSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(Http3ServerOverUdpSelfTest
    Http3ServerOverUdpSelfTest.cpp
)

target_include_directories(Http3ServerOverUdpSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(Http3ServerOverUdpSelfTest PRIVATE SwQuicCore)

if(WIN32)
    target_link_libraries(Http3ServerOverUdpSelfTest PRIVATE ws2_32)
    # The full HTTP router + QUIC stack in one translation unit overflows the
    # default COFF object section limit under MinGW; big-obj lifts it.
    if(MINGW)
        target_compile_options(Http3ServerOverUdpSelfTest PRIVATE -Wa,-mbig-obj)
    endif()
endif()

add_test(NAME Http3ServerOverUdpSelfTest COMMAND Http3ServerOverUdpSelfTest)
