cmake_minimum_required(VERSION 3.10)
project(HttpAppDbBench)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(HttpAppDbBench
    HttpAppDbBench.cpp
)

target_include_directories(HttpAppDbBench PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(HttpAppDbBench PRIVATE SwQuicCore Threads::Threads)

if(WIN32)
    target_link_libraries(HttpAppDbBench PRIVATE ws2_32 secur32 crypt32 shell32)
endif()

if(MINGW)
    target_compile_options(HttpAppDbBench PRIVATE -Wa,-mbig-obj)
endif()
