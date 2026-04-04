cmake_minimum_required(VERSION 3.10)
project(SwLaunchSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SwLaunchSelfTest
    SwLaunchSelfTest.cpp
)

add_executable(SwLaunchDeployFixtureNodeV1
    SwLaunchDeployFixtureNode.cpp
)

add_executable(SwLaunchDeployFixtureNodeV2
    SwLaunchDeployFixtureNode.cpp
)

target_compile_definitions(SwLaunchDeployFixtureNodeV1 PRIVATE
    SWLAUNCH_FIXTURE_VERSION="V1"
)

target_compile_definitions(SwLaunchDeployFixtureNodeV2 PRIVATE
    SWLAUNCH_FIXTURE_VERSION="V2"
)

add_executable(SwLaunchDeployIntegrationTest
    SwLaunchDeployIntegrationTest.cpp
)

if(WIN32)
    target_link_libraries(SwLaunchSelfTest PRIVATE crypt32)
    target_link_libraries(SwLaunchDeployIntegrationTest PRIVATE crypt32)
endif()

target_include_directories(SwLaunchSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_include_directories(SwLaunchDeployFixtureNodeV1 PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_include_directories(SwLaunchDeployFixtureNodeV2 PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_include_directories(SwLaunchDeployIntegrationTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_dependencies(SwLaunchDeployIntegrationTest
    SwLaunch
    SwLaunchDeployFixtureNodeV1
    SwLaunchDeployFixtureNodeV2
)
