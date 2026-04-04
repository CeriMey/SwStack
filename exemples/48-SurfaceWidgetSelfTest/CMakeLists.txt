cmake_minimum_required(VERSION 3.10)
project(SurfaceWidgetSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SurfaceWidgetSelfTest
    SurfaceWidgetSelfTest.cpp
)

target_include_directories(SurfaceWidgetSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

if(WIN32)
    target_link_libraries(SurfaceWidgetSelfTest PRIVATE
        user32
        gdi32
        d3d11
        dxgi
        winmm
        mfplat
        mf
        mfuuid
        ole32
        oleaut32
        uuid
    )
endif()
