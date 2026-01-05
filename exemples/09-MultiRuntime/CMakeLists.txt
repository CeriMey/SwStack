cmake_minimum_required(VERSION 3.10)
project(MultiRuntime)

# Utiliser le standard C++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ajouter l'exécutable MultiRuntime
add_executable(MultiRuntime MultiRuntime.cpp)

# Inclure le répertoire de src/core pour les en-têtes
target_include_directories(MultiRuntime PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
