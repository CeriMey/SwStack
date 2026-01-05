cmake_minimum_required(VERSION 3.10)
project(MjpegServer)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(WARNING "MjpegServer is only supported on Windows (Media Foundation + GDI+) and will not be built here.")
    return()
endif()

add_executable(MjpegServer MjpegServer.cpp)
target_include_directories(MjpegServer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core)

if(WIN32)
    target_link_libraries(MjpegServer PRIVATE
        ws2_32
        user32
        gdi32
        gdiplus
        mfplat
        mfreadwrite
        mf
        mfuuid
        ole32
        oleaut32
        uuid)
endif()
