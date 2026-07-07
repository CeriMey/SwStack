cmake_minimum_required(VERSION 3.10)
project(QuicZeroRttLiveSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(QuicZeroRttLiveSelfTest
    QuicZeroRttLiveSelfTest.cpp
)

target_include_directories(QuicZeroRttLiveSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(QuicZeroRttLiveSelfTest PRIVATE SwQuicCore)

if(WIN32)
    target_link_libraries(QuicZeroRttLiveSelfTest PRIVATE ws2_32)
    if(MINGW)
        target_compile_options(QuicZeroRttLiveSelfTest PRIVATE -Wa,-mbig-obj)
    endif()
endif()

add_test(NAME QuicZeroRttLiveSelfTest COMMAND QuicZeroRttLiveSelfTest)
