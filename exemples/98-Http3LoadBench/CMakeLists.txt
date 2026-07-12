cmake_minimum_required(VERSION 3.10)
project(Http3LoadBench)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(Http3LoadBench
    Http3LoadBench.cpp
)

target_include_directories(Http3LoadBench PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(Http3LoadBench PRIVATE SwQuicCore Threads::Threads)

if(WIN32)
    target_link_libraries(Http3LoadBench PRIVATE ws2_32)
    if(MINGW)
        target_compile_options(Http3LoadBench PRIVATE -Wa,-mbig-obj)
    endif()
endif()

add_test(NAME Http3LoadBench COMMAND Http3LoadBench --duration 2)
