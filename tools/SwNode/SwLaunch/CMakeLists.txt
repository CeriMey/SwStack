cmake_minimum_required(VERSION 3.10)
project(SwLaunch)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SwLaunch
    SwLaunch.cpp
    SwLaunchTraceConfig.cpp
)

if(WIN32)
    target_link_libraries(SwLaunch PRIVATE crypt32)
endif()

target_include_directories(SwLaunch PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_subdirectory(tests)
