cmake_minimum_required(VERSION 3.10)
project(SocketTrafficMonitorSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SocketTrafficMonitorSelfTest
    SocketTrafficSelfTest.cpp
    widgets/SocketTrafficMonitoringBarWidget.cpp
    widgets/SocketTrafficProfilingView.cpp
    widgets/SocketTrafficInspectorWidget.cpp
    widgets/SocketTrafficConsumerTableWidget.cpp
    widgets/SocketTrafficConsumerTimelineWidget.cpp
    widgets/SocketTrafficRtspPlayerWidget.cpp
    ../41-RuntimeProfilerSelfTest/widgets/RuntimeProfilerMonitoringBarWidget.cpp
    ../41-RuntimeProfilerSelfTest/widgets/RuntimeProfilerProfilingView.cpp
    ../41-RuntimeProfilerSelfTest/widgets/RuntimeProfilerStackInspectorWidget.cpp
    ../41-RuntimeProfilerSelfTest/widgets/RuntimeProfilerStallTableWidget.cpp
    ../41-RuntimeProfilerSelfTest/widgets/RuntimeProfilerStallTimelineWidget.cpp
)

target_include_directories(SocketTrafficMonitorSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/exemples/41-RuntimeProfilerSelfTest/widgets
)

if(WIN32)
    target_link_libraries(SocketTrafficMonitorSelfTest PRIVATE
        ws2_32
        user32
        gdi32
        d3d11
        dxgi
        dbghelp
        winmm
        secur32
        gdiplus
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
    find_package(X11)
    if(NOT X11_FOUND)
        message(WARNING "X11 not found, skipping SocketTrafficMonitorSelfTest target.")
        return()
    endif()

    target_include_directories(SocketTrafficMonitorSelfTest PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(SocketTrafficMonitorSelfTest PRIVATE ${X11_LIBRARIES} pthread)

    find_package(OpenSSL REQUIRED)
    target_link_libraries(SocketTrafficMonitorSelfTest PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()
