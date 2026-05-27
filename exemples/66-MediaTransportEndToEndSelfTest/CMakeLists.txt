cmake_minimum_required(VERSION 3.10)
project(MediaTransportEndToEndSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(MediaTransportEndToEndSelfTest
    MediaTransportEndToEndSelfTest.cpp
)

target_include_directories(MediaTransportEndToEndSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

if(WIN32)
    target_link_libraries(MediaTransportEndToEndSelfTest PRIVATE
        ws2_32
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
elseif(UNIX AND NOT APPLE)
    target_link_libraries(MediaTransportEndToEndSelfTest PRIVATE pthread)
endif()

add_test(NAME MediaTransportEndToEndSelfTest COMMAND MediaTransportEndToEndSelfTest)
