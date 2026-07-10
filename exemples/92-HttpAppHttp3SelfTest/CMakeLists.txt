cmake_minimum_required(VERSION 3.10)
project(HttpAppHttp3SelfTest)

# SwHttpApp currently includes SwAny, whose public implementation uses C++17
# `if constexpr`. Keep this integration test on the same standard advertised
# transitively by SwQuicCore so it validates the real consumer contract.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(DEFINED SW_ENABLE_QUIC AND NOT SW_ENABLE_QUIC)
    message(FATAL_ERROR "HttpAppHttp3SelfTest requires SW_ENABLE_QUIC=ON")
endif()

add_executable(HttpAppHttp3SelfTest
    HttpAppHttp3SelfTest.cpp
)

target_include_directories(HttpAppHttp3SelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_compile_definitions(HttpAppHttp3SelfTest PRIVATE
    SW_HTTP3_TEST_CERT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/localhost_cert.pem"
    SW_HTTP3_TEST_KEY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/localhost_key.pem"
)

# SwQuicCore must carry OpenSSL, threading, platform socket/crypto libraries and
# the MinGW large-object option for a public SwHttpApp consumer.
target_link_libraries(HttpAppHttp3SelfTest PRIVATE SwQuicCore)

add_test(NAME HttpAppHttp3SelfTest COMMAND HttpAppHttp3SelfTest)
