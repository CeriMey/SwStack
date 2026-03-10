cmake_minimum_required(VERSION 3.10)
project(HttpAppQuickstart)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/src/core/io/SwHttpApp.h")
    message(WARNING "SwHttpApp.h not found, skipping HttpAppQuickstart target.")
    return()
endif()

add_executable(HttpAppQuickstart
    SwHttpAppQuickstart.cpp
)

set(SW_HTTP_APP_QUICKSTART_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
file(TO_CMAKE_PATH "${SW_HTTP_APP_QUICKSTART_RUNTIME_DIR}/http_app_quickstart_www"
     SW_HTTP_APP_QUICKSTART_DEFAULT_WWW_DIR)
file(TO_CMAKE_PATH "${SW_HTTP_APP_QUICKSTART_RUNTIME_DIR}/http_app_quickstart_uploads"
     SW_HTTP_APP_QUICKSTART_DEFAULT_UPLOADS_DIR)

configure_file(
    SwHttpAppQuickstartConfig.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/SwHttpAppQuickstartConfig.h
    @ONLY
)

target_include_directories(HttpAppQuickstart PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

if(WIN32)
    target_link_libraries(HttpAppQuickstart PRIVATE ws2_32)
endif()
