cmake_minimum_required(VERSION 3.10)
project(RuntimeProfilerSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(RuntimeProfilerSelfTest
    RuntimeProfilerSelfTest.cpp
    widgets/RuntimeProfilerMonitoringBarWidget.cpp
    widgets/RuntimeProfilerProfilingView.cpp
    widgets/RuntimeProfilerStackInspectorWidget.cpp
    widgets/RuntimeProfilerStallTableWidget.cpp
    widgets/RuntimeProfilerStallTimelineWidget.cpp
)

target_include_directories(RuntimeProfilerSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

if(WIN32)
    target_link_libraries(RuntimeProfilerSelfTest PRIVATE ws2_32 user32 gdi32 dbghelp winmm)
elseif(UNIX AND NOT APPLE)
    find_package(X11)
    if(NOT X11_FOUND)
        message(WARNING "X11 not found, skipping RuntimeProfilerSelfTest target.")
        return()
    endif()

    target_include_directories(RuntimeProfilerSelfTest PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(RuntimeProfilerSelfTest PRIVATE ${X11_LIBRARIES} pthread)

    find_package(OpenSSL REQUIRED)
    target_link_libraries(RuntimeProfilerSelfTest PRIVATE OpenSSL::Crypto)
endif()
