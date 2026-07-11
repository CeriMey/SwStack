cmake_minimum_required(VERSION 3.10)
project(SrtMediaSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SrtMediaSelfTest
    SrtMediaSelfTest.cpp
)

add_test(NAME SrtMediaSelfTest COMMAND SrtMediaSelfTest)

target_include_directories(SrtMediaSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

if(WIN32)
    target_link_libraries(SrtMediaSelfTest PRIVATE
        ws2_32
        secur32
        crypt32
        gdiplus
        winmm
        d3d11
        dxgi
        mfplat
        mfreadwrite
        mf
        mfuuid
        wmcodecdspuuid
        ole32
        oleaut32
        uuid
    )
elseif(UNIX AND NOT APPLE)
    target_link_libraries(SrtMediaSelfTest PRIVATE pthread ${CMAKE_DL_LIBS})
endif()
