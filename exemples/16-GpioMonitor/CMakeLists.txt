cmake_minimum_required(VERSION 3.10)
project(GpioMonitorExample)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(GpioMonitorExample
    GpioMonitorExample.cpp
)

target_include_directories(GpioMonitorExample PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

if(WIN32)
    target_link_libraries(GpioMonitorExample PRIVATE ws2_32)
endif()
