cmake_minimum_required(VERSION 3.10)
project(PnCaptureManagerNode)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/Zc:preprocessor)
endif()

set(CORE_SW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
get_filename_component(CORE_SW_ROOT "${CORE_SW_ROOT}" ABSOLUTE)

add_executable(PnCaptureManagerNode
    PnCaptureManagerNode.cpp
    PnCaptureManagerNodeMain.cpp
)

target_include_directories(PnCaptureManagerNode PRIVATE
    ${CORE_SW_ROOT}/src
    ${CORE_SW_ROOT}/src/core
    ${CORE_SW_ROOT}/src/core/runtime
    ${CORE_SW_ROOT}/src/core/object
    ${CORE_SW_ROOT}/src/core/types
    ${CORE_SW_ROOT}/src/core/io
    ${CORE_SW_ROOT}/src/core/fs
    ${CORE_SW_ROOT}/src/core/platform
    ${CORE_SW_ROOT}/src/core/remote
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../PnUtils
    ${CMAKE_CURRENT_LIST_DIR}/../PnCore
)

find_library(PNUTILS_LIB NAMES PnUtils PATHS "${CMAKE_INSTALL_PREFIX}/lib" NO_DEFAULT_PATH)
find_library(PNCORE_LIB NAMES PnCore PATHS "${CMAKE_INSTALL_PREFIX}/lib" NO_DEFAULT_PATH)
if(NOT PNUTILS_LIB OR NOT PNCORE_LIB)
    message(FATAL_ERROR "PnCore/PnUtils not found in ${CMAKE_INSTALL_PREFIX}/lib. Build with SwBuild (check deps.depend).")
endif()
target_link_libraries(PnCaptureManagerNode PRIVATE "${PNUTILS_LIB}" "${PNCORE_LIB}")

if(WIN32)
    target_link_libraries(PnCaptureManagerNode PRIVATE
        crypt32
        mfplat
        mfreadwrite
        mf
        mfuuid
        ole32
        oleaut32
        uuid
    )
endif()

if(NOT WIN32)
    target_link_libraries(PnCaptureManagerNode PRIVATE crypto)
endif()
