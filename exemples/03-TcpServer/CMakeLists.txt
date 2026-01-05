cmake_minimum_required(VERSION 3.10)
project(TcpServer)

# Utiliser le standard C++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ajouter l'exécutable TcpServer
add_executable(TcpServer TcpServer.cpp)

if(WIN32)
    target_link_libraries(TcpServer PRIVATE ws2_32)
endif()

# Inclure le répertoire de src/core pour les en-têtes
target_include_directories(TcpServer PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
