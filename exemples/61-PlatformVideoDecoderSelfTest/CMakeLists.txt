cmake_minimum_required(VERSION 3.10)
project(PlatformVideoDecoderSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(PlatformVideoDecoderSelfTest
    PlatformVideoDecoderSelfTest.cpp
)

target_include_directories(PlatformVideoDecoderSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

if(WIN32)
    target_link_libraries(PlatformVideoDecoderSelfTest PRIVATE
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
    target_link_libraries(PlatformVideoDecoderSelfTest PRIVATE pthread)
endif()
