cmake_minimum_required(VERSION 3.10)
project(SwBuildSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SwBuildSelfTest
    SwBuildSelfTest.cpp
    ../SwBuildProject.cpp
    ../SwBuildDependencyResolver.cpp
    ../SwBuildProjectScanner.cpp
    ../SwBuildOptions.cpp
)

if(WIN32)
    target_link_libraries(SwBuildSelfTest PRIVATE crypt32)
endif()

target_include_directories(SwBuildSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
