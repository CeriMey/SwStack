cmake_minimum_required(VERSION 3.10)
project(IpcRingBufferCameraPlayer)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(WARNING "IpcRingBufferCameraPlayer is only supported on Windows and will not be built on this platform.")
    return()
endif()

add_executable(IpcRingBufferCamera
    IpcRingBufferCameraMain.cpp
    IpcRingBufferCameraPublisher.cpp
)

target_include_directories(IpcRingBufferCamera PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(IpcRingBufferCamera PRIVATE
    ws2_32
    user32
    gdi32
    mfplat
    mfreadwrite
    mf
    mfuuid
    ole32
    oleaut32
    uuid
)

add_executable(IpcRingBufferPlayer
    IpcRingBufferPlayerMain.cpp
    IpcRingBufferPlayer.cpp
    IpcRingBufferVideoWidget.cpp
)

target_include_directories(IpcRingBufferPlayer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(IpcRingBufferPlayer PRIVATE
    ws2_32
    user32
    gdi32
    mfplat
    mfreadwrite
    mf
    mfuuid
    ole32
    oleaut32
    uuid
)

