cmake_minimum_required(VERSION 3.10)
project(SwUdpSocketSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SwUdpSocketSelfTest SwUdpSocketSelfTest.cpp)
target_include_directories(SwUdpSocketSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

if(WIN32)
    target_link_libraries(SwUdpSocketSelfTest PRIVATE ws2_32)
endif()

add_test(NAME SwUdpSocketSelfTest COMMAND SwUdpSocketSelfTest)
