cmake_minimum_required(VERSION 3.10)
include(${CMAKE_CURRENT_LIST_DIR}/SwLaunchVersion.cmake)
project(SwLaunch VERSION ${SWLAUNCH_VERSION_CORE})

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Git QUIET)

set(SWLAUNCH_GIT_REVISION "")
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE SWLAUNCH_GIT_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if("${SWLAUNCH_GIT_REVISION}" STREQUAL "")
    set(SWLAUNCH_GIT_REVISION "unknown")
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/SwLaunchVersion.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/SwLaunchVersion.h
    @ONLY
)

add_executable(SwLaunch
    SwLaunch.cpp
    SwLaunchCrash.cpp
    SwLaunchTraceConfig.cpp
)

if(MSVC)
    target_compile_options(SwLaunch PRIVATE
        $<$<CONFIG:Release>:/Zi>
    )
    target_link_options(SwLaunch PRIVATE
        $<$<CONFIG:Release>:/DEBUG:FULL>
    )
endif()

if(WIN32)
    target_link_libraries(SwLaunch PRIVATE crypt32)
endif()

target_include_directories(SwLaunch PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

add_subdirectory(tests)
