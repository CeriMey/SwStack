cmake_minimum_required(VERSION 3.10)
project(IpcPingPongNodes)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SwPingNode
    SwPingNode.cpp
)

add_executable(SwPongNode
    SwPongNode.cpp
)

if(WIN32)
    target_link_libraries(SwPingNode PRIVATE crypt32)
    target_link_libraries(SwPongNode PRIVATE crypt32)
endif()

target_include_directories(SwPingNode PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(SwPongNode PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}
)

