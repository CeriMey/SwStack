cmake_minimum_required(VERSION 3.10)
project(PingPongPlugin)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Shared library meant to be loaded by SwComponentContainer (plugin entrypoint exported when SW_COMPONENT_PLUGIN=1).
add_library(PingPongPlugin SHARED
    PingPongPlugin.cpp
)

target_compile_definitions(PingPongPlugin PRIVATE SW_COMPONENT_PLUGIN=1)

target_include_directories(PingPongPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(WIN32)
    target_link_libraries(PingPongPlugin PRIVATE crypt32)
endif()

