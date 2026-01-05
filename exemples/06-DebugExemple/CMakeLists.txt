cmake_minimum_required(VERSION 3.10)
project(DebugExemple)

# Utiliser le standard C++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ajouter l'exécutable DebugExemple
add_executable(DebugExemple DebugExemple.cpp)

if(WIN32)
    target_link_libraries(DebugExemple PRIVATE ws2_32)
endif()

find_package(OpenSSL)
if(OpenSSL_FOUND)
    target_link_libraries(DebugExemple PRIVATE OpenSSL::Crypto)
elseif(UNIX)
    target_link_libraries(DebugExemple PRIVATE crypto)
endif()

# Inclure le répertoire de src/core pour les en-têtes
target_include_directories(DebugExemple PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
