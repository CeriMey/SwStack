cmake_minimum_required(VERSION 3.10)
project(SwTcpSocketSelfTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SwTcpSocketSelfTest SwTcpSocketSelfTest.cpp)
target_include_directories(SwTcpSocketSelfTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(WIN32)
    target_link_libraries(SwTcpSocketSelfTest PRIVATE ws2_32)
endif()

add_test(NAME SwTcpSocketSelfTest COMMAND SwTcpSocketSelfTest)
