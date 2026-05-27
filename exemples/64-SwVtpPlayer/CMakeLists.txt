cmake_minimum_required(VERSION 3.10)
project(SwVtpPlayer)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(WARNING "SwVtpPlayer GUI example is Windows-only for the current platform video widget build. Skipping.")
    return()
endif()

add_executable(SwVtpPlayer
    SwVtpPlayer.cpp
)

target_include_directories(SwVtpPlayer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(SwVtpPlayer PRIVATE
    ws2_32
    user32
    gdi32
    gdiplus
    d3d11
    dxgi
    winmm
    wmcodecdspuuid
    mfplat
    mfreadwrite
    mf
    mfuuid
    ole32
    oleaut32
    uuid
)
