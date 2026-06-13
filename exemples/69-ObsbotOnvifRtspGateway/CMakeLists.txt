cmake_minimum_required(VERSION 3.10)
project(ObsbotOnvifRtspGateway)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ObsbotOnvifRtspGateway
    ObsbotOnvifRtspGateway.cpp
)

target_include_directories(ObsbotOnvifRtspGateway PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

if(WIN32)
    target_link_libraries(ObsbotOnvifRtspGateway PRIVATE ws2_32 shell32 ole32 oleaut32 strmiids mf mfplat mfreadwrite mfuuid)
endif()
