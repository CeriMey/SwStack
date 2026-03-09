cmake_minimum_required(VERSION 3.10)
project(SwApiSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SwApiSelfTest
    SwApiSelfTest.cpp
    ../SwApi/SwApiCli.cpp
    ../SwApi/SwApiJson.cpp
)

if(WIN32)
    target_link_libraries(SwApiSelfTest PRIVATE crypt32)
endif()

target_include_directories(SwApiSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/../SwApi
)
