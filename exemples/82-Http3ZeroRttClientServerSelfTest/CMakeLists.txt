cmake_minimum_required(VERSION 3.10)
project(Http3ZeroRttClientServerSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(Http3ZeroRttClientServerSelfTest
    Http3ZeroRttClientServerSelfTest.cpp
)

target_include_directories(Http3ZeroRttClientServerSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(Http3ZeroRttClientServerSelfTest PRIVATE SwQuicCore Threads::Threads)

if(WIN32)
    target_link_libraries(Http3ZeroRttClientServerSelfTest PRIVATE ws2_32)
    if(MINGW)
        target_compile_options(Http3ZeroRttClientServerSelfTest PRIVATE -Wa,-mbig-obj)
    endif()
endif()

add_test(NAME Http3ZeroRttClientServerSelfTest COMMAND Http3ZeroRttClientServerSelfTest)
