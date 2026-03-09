cmake_minimum_required(VERSION 3.10)
project(PnCore)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    add_compile_options(/Zc:preprocessor)
endif()

set(CORE_SW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
get_filename_component(CORE_SW_ROOT "${CORE_SW_ROOT}" ABSOLUTE)

add_library(PnCore STATIC
    PnCore.cpp
)

target_include_directories(PnCore PRIVATE
    ${CORE_SW_ROOT}/src
    ${CORE_SW_ROOT}/src/core
    ${CORE_SW_ROOT}/src/core/runtime
    ${CORE_SW_ROOT}/src/core/object
    ${CORE_SW_ROOT}/src/core/types
    ${CORE_SW_ROOT}/src/core/io
    ${CORE_SW_ROOT}/src/core/fs
    ${CORE_SW_ROOT}/src/core/platform
    ${CMAKE_CURRENT_LIST_DIR}
)

if(WIN32)
    target_link_libraries(PnCore PRIVATE crypt32)
endif()
