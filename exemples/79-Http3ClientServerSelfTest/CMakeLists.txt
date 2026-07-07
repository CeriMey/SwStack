cmake_minimum_required(VERSION 3.10)
project(Http3ClientServerSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(Http3ClientServerSelfTest
    Http3ClientServerSelfTest.cpp
)

target_include_directories(Http3ClientServerSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(Http3ClientServerSelfTest PRIVATE SwQuicCore Threads::Threads)

if(WIN32)
    target_link_libraries(Http3ClientServerSelfTest PRIVATE ws2_32)
    if(MINGW)
        target_compile_options(Http3ClientServerSelfTest PRIVATE -Wa,-mbig-obj)
    endif()
endif()

add_test(NAME Http3ClientServerSelfTest COMMAND Http3ClientServerSelfTest)
