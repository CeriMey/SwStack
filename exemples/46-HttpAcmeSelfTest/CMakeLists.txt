cmake_minimum_required(VERSION 3.10)
project(HttpAcmeSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/src/core/io/SwHttpAcmeManager.h")
    message(WARNING "SwHttpAcmeManager.h not found, skipping HttpAcmeSelfTest target.")
    return()
endif()

add_executable(HttpAcmeSelfTest
    SwHttpAcmeSelfTest.cpp
)

set(SW_HTTP_ACME_SELFTEST_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
file(TO_CMAKE_PATH "${SW_HTTP_ACME_SELFTEST_RUNTIME_DIR}" SW_HTTP_ACME_SELFTEST_RUNTIME_DIR)

configure_file(
    SwHttpAcmeSelfTestConfig.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/SwHttpAcmeSelfTestConfig.h
    @ONLY
)

target_include_directories(HttpAcmeSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(HttpAcmeSelfTest PRIVATE SwQuicCore)

if(WIN32)
    # OpenSSL::Crypto and Winsock are propagated by SwQuicCore; ACME also
    # needs the Windows security package API.
    target_link_libraries(HttpAcmeSelfTest PRIVATE secur32)
endif()
