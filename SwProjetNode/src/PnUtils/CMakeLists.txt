cmake_minimum_required(VERSION 3.10)
project(PnUtils)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    add_compile_options(/Zc:preprocessor)
endif()

set(CORE_SW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
get_filename_component(CORE_SW_ROOT "${CORE_SW_ROOT}" ABSOLUTE)

add_library(PnUtils STATIC
    PnUtils.cpp
)

target_include_directories(PnUtils PRIVATE
    ${CORE_SW_ROOT}/src
    ${CORE_SW_ROOT}/src/core
    ${CORE_SW_ROOT}/src/core/runtime
    ${CORE_SW_ROOT}/src/core/object
    ${CORE_SW_ROOT}/src/core/types
    ${CORE_SW_ROOT}/src/core/io
    ${CORE_SW_ROOT}/src/core/fs
    ${CORE_SW_ROOT}/src/core/platform
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../PnCore
)

find_library(PNCORE_LIB NAMES PnCore PATHS "${CMAKE_INSTALL_PREFIX}/lib" NO_DEFAULT_PATH)
if(NOT PNCORE_LIB)
    message(FATAL_ERROR "PnCore not found in ${CMAKE_INSTALL_PREFIX}/lib. Build with SwBuild (check deps.depend).")
endif()

target_link_libraries(PnUtils PRIVATE "${PNCORE_LIB}")

if(WIN32)
    target_link_libraries(PnUtils PRIVATE crypt32)
endif()
