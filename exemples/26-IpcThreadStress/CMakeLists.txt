cmake_minimum_required(VERSION 3.10)
project(IpcThreadStress)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(IpcThreadStress
    IpcThreadStress.cpp
)

if(WIN32)
    target_link_libraries(IpcThreadStress PRIVATE crypt32)
endif()

target_include_directories(IpcThreadStress PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}
)

