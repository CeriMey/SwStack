cmake_minimum_required(VERSION 3.10)
project(SimpleConsoleExample)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ConsoleApplication ConsoleApplication.cpp)

if(WIN32)
    target_link_libraries(ConsoleApplication PRIVATE crypt32)
else()
    find_package(OpenSSL)
    if(OpenSSL_FOUND)
        target_link_libraries(ConsoleApplication PRIVATE OpenSSL::Crypto)
    else()
        target_link_libraries(ConsoleApplication PRIVATE crypto)
    endif()
endif()

# Inclure le répertoire de src/core pour les en-têtes
target_include_directories(ConsoleApplication PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
