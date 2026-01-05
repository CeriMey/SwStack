cmake_minimum_required(VERSION 3.10)
project(WebSocketSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(WebSocketSelfTest
    WebSocketSelfTest.cpp
)

target_include_directories(WebSocketSelfTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(WIN32)
    target_link_libraries(WebSocketSelfTest PRIVATE ws2_32)
endif()

add_executable(WebSocketAdvancedSelfTest
    WebSocketAdvancedSelfTest.cpp
)

target_include_directories(WebSocketAdvancedSelfTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(WIN32)
    target_link_libraries(WebSocketAdvancedSelfTest PRIVATE ws2_32)
endif()
