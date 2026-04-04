cmake_minimum_required(VERSION 3.10)
project(MailSelfTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/src/core/io/SwMailService.h")
    message(WARNING "SwMailService.h not found, skipping MailSelfTest target.")
    return()
endif()

add_executable(MailSelfTest
    SwMailSelfTest.cpp
)

set(SW_MAIL_SELFTEST_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
file(TO_CMAKE_PATH "${SW_MAIL_SELFTEST_RUNTIME_DIR}" SW_MAIL_SELFTEST_RUNTIME_DIR)

configure_file(
    SwMailSelfTestConfig.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/SwMailSelfTestConfig.h
    @ONLY
)

target_include_directories(MailSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

if(WIN32)
    set(SW_OPENSSL_WIN_ROOT "C:/OpenSSL-Win64")
    set(SW_OPENSSL_WIN_SSL_LIB "${SW_OPENSSL_WIN_ROOT}/lib/VC/x64/MD/libssl.lib")
    set(SW_OPENSSL_WIN_CRYPTO_LIB "${SW_OPENSSL_WIN_ROOT}/lib/VC/x64/MD/libcrypto.lib")
    if(EXISTS "${SW_OPENSSL_WIN_ROOT}/include/openssl/ssl.h" AND
       EXISTS "${SW_OPENSSL_WIN_SSL_LIB}" AND
       EXISTS "${SW_OPENSSL_WIN_CRYPTO_LIB}")
        target_include_directories(MailSelfTest PRIVATE "${SW_OPENSSL_WIN_ROOT}/include")
        target_link_libraries(MailSelfTest PRIVATE
            "${SW_OPENSSL_WIN_SSL_LIB}"
            "${SW_OPENSSL_WIN_CRYPTO_LIB}"
            ws2_32
            secur32
        )
    else()
        find_package(OpenSSL REQUIRED)
        target_link_libraries(MailSelfTest PRIVATE OpenSSL::SSL OpenSSL::Crypto ws2_32 secur32)
    endif()
else()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(MailSelfTest PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()
