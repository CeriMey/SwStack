cmake_minimum_required(VERSION 3.10)
project(SwLaunchSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SwLaunchSelfTest
    SwLaunchSelfTest.cpp
)

if(WIN32)
    target_link_libraries(SwLaunchSelfTest PRIVATE crypt32)
endif()

target_include_directories(SwLaunchSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
