cmake_minimum_required(VERSION 3.10)
project(SwTlsSecretLifetimeSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SwTlsSecretLifetimeSelfTest
    SwTlsSecretLifetimeSelfTest.cpp
)

target_compile_definitions(SwTlsSecretLifetimeSelfTest PRIVATE
    SW_QUIC_ENABLE_SECRET_LIFETIME_TEST_HOOKS=1
)

target_include_directories(SwTlsSecretLifetimeSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(SwTlsSecretLifetimeSelfTest PRIVATE SwQuicCore)

if(WIN32)
    target_link_libraries(SwTlsSecretLifetimeSelfTest PRIVATE ws2_32)
endif()

add_test(NAME SwTlsSecretLifetimeSelfTest COMMAND SwTlsSecretLifetimeSelfTest)
