cmake_minimum_required(VERSION 3.10)
project(HttpSeoSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(HttpSeoSelfTest HttpSeoSelfTest.cpp)
target_include_directories(HttpSeoSelfTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(HttpSeoSelfTest PRIVATE SwQuicCore OpenSSL::Crypto)

if(WIN32)
    target_link_libraries(HttpSeoSelfTest PRIVATE ws2_32 secur32 crypt32 bcrypt)
endif()

enable_testing()
add_test(NAME HttpSeoSelfTest COMMAND HttpSeoSelfTest)
