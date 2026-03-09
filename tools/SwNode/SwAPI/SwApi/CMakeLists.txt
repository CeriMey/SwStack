cmake_minimum_required(VERSION 3.10)
project(swapi)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(swapi
    main.cpp
    SwApiApp.cpp
    SwApiAppsCommand.cpp
    SwApiCli.cpp
    SwApiCommand.cpp
    SwApiConfigCommand.cpp
    SwApiContainerCommand.cpp
    SwApiGraphCommand.cpp
    SwApiIpcInspector.cpp
    SwApiJson.cpp
    SwApiNodesCommand.cpp
    SwApiPingCommand.cpp
    SwApiRpcsCommand.cpp
    SwApiSignalsCommand.cpp
)

if(WIN32)
    target_link_libraries(swapi PRIVATE crypt32)
endif()

target_include_directories(swapi PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}
)
