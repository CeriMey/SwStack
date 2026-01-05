cmake_minimum_required(VERSION 3.10)
project(ConfigurableObjectDemo)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ConfigurableObjectDemo
    ConfigurableObjectDemo.cpp
    DemoSubscriber.cpp
)

add_executable(RpcClientDemo
    RpcClientDemo.cpp
)

add_executable(RpcFactoryDemo
    RpcFactoryDemo.cpp
)

if(WIN32)
    target_link_libraries(ConfigurableObjectDemo PRIVATE crypt32)
    target_link_libraries(RpcClientDemo PRIVATE crypt32)
    target_link_libraries(RpcFactoryDemo PRIVATE crypt32)
endif()

target_include_directories(ConfigurableObjectDemo PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(RpcClientDemo PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(RpcFactoryDemo PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}
)
