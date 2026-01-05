cmake_minimum_required(VERSION 3.10)
project(RtspVideoWidget)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(WARNING "RtspVideoWidget is Windows-only (Media Foundation decode). Skipping.")
    return()
endif()

add_executable(RtspVideoWidget RtspVideoWidget.cpp)
target_include_directories(RtspVideoWidget PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core)

target_link_libraries(RtspVideoWidget PRIVATE
    ws2_32
    user32
    gdi32
    gdiplus
    wmcodecdspuuid
    mfplat
    mfreadwrite
    mf
    mfuuid
    ole32
    oleaut32
    uuid)
