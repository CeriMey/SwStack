cmake_minimum_required(VERSION 3.10)
project(VideoWidgetExample)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(WARNING "VideoWidgetExample is only supported on Windows and will not be built on this platform.")
    return()
endif()

add_executable(VideoWidgetExample VideoWidgetExample.cpp)
target_include_directories(VideoWidgetExample PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core)

if(WIN32)
    target_link_libraries(VideoWidgetExample PRIVATE
        ws2_32
        user32
        gdi32
        mfplat
        mfreadwrite
        mf
        mfuuid
        ole32
        oleaut32
        uuid)
endif()
