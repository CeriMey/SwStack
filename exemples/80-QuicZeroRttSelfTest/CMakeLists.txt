cmake_minimum_required(VERSION 3.10)
project(QuicZeroRttSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(QuicZeroRttSelfTest
    QuicZeroRttSelfTest.cpp
)

target_include_directories(QuicZeroRttSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(QuicZeroRttSelfTest PRIVATE SwQuicCore)

if(WIN32)
    target_link_libraries(QuicZeroRttSelfTest PRIVATE ws2_32)
    if(MINGW)
        target_compile_options(QuicZeroRttSelfTest PRIVATE -Wa,-mbig-obj)
    endif()
endif()

add_test(NAME QuicZeroRttSelfTest COMMAND QuicZeroRttSelfTest)
