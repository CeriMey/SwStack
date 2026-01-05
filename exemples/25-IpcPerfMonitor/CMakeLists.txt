cmake_minimum_required(VERSION 3.10)
project(IpcPerfMonitor)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(IpcPerfMonitor
    IpcPerfMonitor.cpp
)

if(WIN32)
    target_link_libraries(IpcPerfMonitor PRIVATE crypt32)
endif()

target_include_directories(IpcPerfMonitor PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}
)

