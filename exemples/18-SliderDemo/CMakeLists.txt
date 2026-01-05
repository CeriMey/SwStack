cmake_minimum_required(VERSION 3.10)
project(SliderDemo)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SliderDemo SliderDemo.cpp)
target_include_directories(SliderDemo PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

if(WIN32)
    target_link_libraries(SliderDemo PRIVATE ws2_32 user32 gdi32)
elseif(UNIX AND NOT APPLE)
    find_package(X11)
    if(NOT X11_FOUND)
        message(WARNING "X11 not found, skipping SliderDemo target.")
        return()
    endif()
    target_include_directories(SliderDemo PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(SliderDemo PRIVATE ${X11_LIBRARIES} pthread)

    find_package(OpenSSL REQUIRED)
    target_link_libraries(SliderDemo PRIVATE OpenSSL::Crypto)
endif()
