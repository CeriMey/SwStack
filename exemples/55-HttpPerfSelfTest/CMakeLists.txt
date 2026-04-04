cmake_minimum_required(VERSION 3.10)
project(HttpPerfSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(HttpPerfSelfTest
    HttpPerfSelfTest.cpp
)

target_include_directories(HttpPerfSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(WIN32)
    target_link_libraries(HttpPerfSelfTest PRIVATE ws2_32)
endif()
