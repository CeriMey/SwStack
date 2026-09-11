cmake_minimum_required(VERSION 3.10)
project(SwApiSelfTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SwApiSelfTest
    SwApiSelfTest.cpp
    ../SwApi/SwApiCli.cpp
    ../SwApi/SwApiJson.cpp
)

if(WIN32)
    target_link_libraries(SwApiSelfTest PRIVATE crypt32)
endif()

target_include_directories(SwApiSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/../SwApi
)

add_executable(SwRosBridgeFixture SwRosBridgeFixture.cpp)
target_link_libraries(SwRosBridgeFixture PRIVATE SwQuicCore)
if(WIN32)
    target_link_libraries(SwRosBridgeFixture PRIVATE crypt32)
endif()
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING)
    add_test(NAME SwRosBridgeIntegration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/verify_rosbridge.py
            --bridge $<TARGET_FILE:SwBridge> --fixture $<TARGET_FILE:SwRosBridgeFixture>)
    set_tests_properties(SwRosBridgeIntegration PROPERTIES TIMEOUT 60)
endif()
