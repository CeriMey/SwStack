cmake_minimum_required(VERSION 3.10)
project(SwBuild)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SwBuild
    SwBuild.cpp
    SwBuildController.cpp
    SwBuildDependencyResolver.cpp
    SwBuildInstaller.cpp
    SwBuildOptions.cpp
    SwBuildProject.cpp
    SwBuildProjectScanner.cpp
)

if(WIN32)
    target_link_libraries(SwBuild PRIVATE crypt32)
endif()

target_include_directories(SwBuild PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_subdirectory(tests)
