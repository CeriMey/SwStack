cmake_minimum_required(VERSION 3.10)
project(SwVtpAv1SelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SwVtpAv1SelfTest
    SwVtpAv1SelfTest.cpp
)

add_test(NAME SwVtpAv1SelfTest COMMAND SwVtpAv1SelfTest)

target_include_directories(SwVtpAv1SelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

if(UNIX AND NOT APPLE)
    target_link_libraries(SwVtpAv1SelfTest PRIVATE pthread)
endif()
