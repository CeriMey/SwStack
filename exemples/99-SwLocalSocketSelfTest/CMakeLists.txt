cmake_minimum_required(VERSION 3.10)
project(SwLocalSocketSelfTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SwLocalSocketSelfTest SwLocalSocketSelfTest.cpp)
target_include_directories(SwLocalSocketSelfTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(WIN32)
    target_link_libraries(SwLocalSocketSelfTest PRIVATE ws2_32 advapi32)
endif()

add_test(NAME SwLocalSocketSelfTest COMMAND SwLocalSocketSelfTest)
