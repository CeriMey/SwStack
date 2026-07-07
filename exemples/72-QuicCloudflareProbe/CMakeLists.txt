cmake_minimum_required(VERSION 3.10)
project(QuicCloudflareProbe)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(QuicCloudflareProbe
    QuicCloudflareProbe.cpp
)

target_include_directories(QuicCloudflareProbe PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(QuicCloudflareProbe PRIVATE SwQuicCore)

if(WIN32)
    target_link_libraries(QuicCloudflareProbe PRIVATE ws2_32)
endif()
