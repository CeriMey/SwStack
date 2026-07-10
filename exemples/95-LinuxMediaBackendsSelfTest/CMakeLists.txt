cmake_minimum_required(VERSION 3.10)
project(LinuxMediaBackendsSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(LinuxMediaBackendsSelfTest
    LinuxMediaBackendsSelfTest.cpp
)

add_test(NAME LinuxMediaBackendsSelfTest COMMAND LinuxMediaBackendsSelfTest)

target_include_directories(LinuxMediaBackendsSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

if(WIN32)
    target_link_libraries(LinuxMediaBackendsSelfTest PRIVATE
        ws2_32
        secur32
        crypt32
        winmm
        d3d11
        dxgi
        gdiplus
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
    target_link_libraries(LinuxMediaBackendsSelfTest PRIVATE pthread ${CMAKE_DL_LIBS})
endif()
