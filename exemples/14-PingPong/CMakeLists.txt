cmake_minimum_required(VERSION 3.10)
project(PingPongExample)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(PingPongExample
    PingPong.cpp
    ScoreBanner.cpp
    PingPongGameWidget.cpp
)

target_include_directories(PingPongExample PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(WIN32)
    target_link_libraries(PingPongExample PRIVATE ws2_32 user32 gdi32)
elseif(UNIX AND NOT APPLE)
    find_package(X11)
    if(NOT X11_FOUND)
        message(WARNING "X11 not found, skipping PingPongExample target.")
        return()
    endif()
    target_include_directories(PingPongExample PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(PingPongExample PRIVATE ${X11_LIBRARIES})
endif()
