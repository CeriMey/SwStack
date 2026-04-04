cmake_minimum_required(VERSION 3.10)
project(EmbeddedDbQtSqlBench)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/src/core/storage/SwEmbeddedDb.h")
    message(WARNING "SwEmbeddedDb.h not found, skipping EmbeddedDbQtSqlBench target.")
    return()
endif()

if(NOT WIN32)
    message(WARNING "EmbeddedDbQtSqlBench currently targets Windows and will be skipped.")
    return()
endif()

find_package(QT NAMES Qt6 Qt5 COMPONENTS Core Sql QUIET)
if(NOT QT_FOUND)
    message(WARNING "Qt Core/Sql not found, skipping EmbeddedDbQtSqlBench target.")
    return()
endif()

find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Sql QUIET)
if(NOT TARGET Qt${QT_VERSION_MAJOR}::Core OR NOT TARGET Qt${QT_VERSION_MAJOR}::Sql)
    message(WARNING "Qt Core/Sql targets not available, skipping EmbeddedDbQtSqlBench target.")
    return()
endif()

add_executable(EmbeddedDbQtSqlBench
    EmbeddedDbQtSqlBench.cpp
)

target_compile_definitions(EmbeddedDbQtSqlBench PRIVATE
    QT_NO_KEYWORDS
)

target_include_directories(EmbeddedDbQtSqlBench PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(EmbeddedDbQtSqlBench PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Sql
    ws2_32
    shell32
)
