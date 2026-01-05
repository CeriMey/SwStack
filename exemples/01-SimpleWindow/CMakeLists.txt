cmake_minimum_required(VERSION 3.10)
project(SimpleWindow)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SimpleWindow GuiApplication.cpp)
target_include_directories(SimpleWindow PRIVATE ${CMAKE_SOURCE_DIR}/src/core)


if(WIN32)
    target_link_libraries(SimpleWindow PRIVATE ws2_32 user32 gdi32)
elseif(UNIX AND NOT APPLE)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(SimpleWindow PRIVATE OpenSSL::Crypto)

    find_package(X11)
    if(NOT X11_FOUND)
        message(WARNING "X11 not found, skipping SimpleWindow target.")
        return()
    endif()

    target_include_directories(SimpleWindow PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(SimpleWindow PRIVATE ${X11_LIBRARIES})
endif()
