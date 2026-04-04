cmake_minimum_required(VERSION 3.10)
project(LoadingOverlayDemo)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(LoadingOverlayDemo LoadingOverlayDemo.cpp)
target_include_directories(LoadingOverlayDemo PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

if(WIN32)
    target_link_libraries(LoadingOverlayDemo PRIVATE ws2_32 user32 gdi32)
elseif(UNIX AND NOT APPLE)
    find_package(X11)
    if(NOT X11_FOUND)
        message(WARNING "X11 not found, skipping LoadingOverlayDemo target.")
        return()
    endif()
    target_include_directories(LoadingOverlayDemo PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(LoadingOverlayDemo PRIVATE ${X11_LIBRARIES} pthread)

    find_package(OpenSSL REQUIRED)
    target_link_libraries(LoadingOverlayDemo PRIVATE OpenSSL::Crypto)
endif()
