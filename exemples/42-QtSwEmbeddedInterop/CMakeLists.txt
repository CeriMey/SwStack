cmake_minimum_required(VERSION 3.10)
project(QtSwEmbeddedInterop)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(WARNING "QtSwEmbeddedInterop is Windows-only and will be skipped.")
    return()
endif()

find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets QUIET)
if(NOT QT_FOUND)
    message(WARNING "Qt Widgets not found, skipping QtSwEmbeddedInterop target.")
    return()
endif()

find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets QUIET)
if(NOT TARGET Qt${QT_VERSION_MAJOR}::Widgets)
    message(WARNING "Qt Widgets target not available, skipping QtSwEmbeddedInterop target.")
    return()
endif()

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" EXAMPLE42_SOURCE_DIR_CMAKE)

add_executable(QtSwEmbeddedInterop
    QtSwEmbeddedInterop.cpp
    ui/QtStudioPane.cpp
    ui/QtSwHostWidget.cpp
    demo/ExampleSwThreadFiberBridge.cpp
)
target_include_directories(QtSwEmbeddedInterop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(QtSwEmbeddedInterop PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    ws2_32
    user32
    gdi32
)

target_compile_definitions(QtSwEmbeddedInterop PRIVATE
    EXAMPLE42_SOURCE_DIR="${EXAMPLE42_SOURCE_DIR_CMAKE}"
)
