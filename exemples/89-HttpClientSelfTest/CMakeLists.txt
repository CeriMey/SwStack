cmake_minimum_required(VERSION 3.10)
project(HttpClientSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(HttpClientSelfTest HttpClientSelfTest.cpp)
target_include_directories(HttpClientSelfTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(WIN32)
    target_link_libraries(HttpClientSelfTest PRIVATE ws2_32)
endif()

enable_testing()
add_test(NAME HttpClientSelfTest COMMAND HttpClientSelfTest)
