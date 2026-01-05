cmake_minimum_required(VERSION 3.10)
project(TileServer)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(TileServer
    TileServer.cpp
    routes/TileRouteHandler.cpp
    routes/MapApiRouteHandler.h
    MapDatabase.h
)

target_include_directories(TileServer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(WIN32)
    target_link_libraries(TileServer PRIVATE ws2_32)
else()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(TileServer PRIVATE OpenSSL::Crypto)
endif()
