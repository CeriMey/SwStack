cmake_minimum_required(VERSION 3.10)
project(SwProcessExample)

# Utiliser le standard C++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ajouter l'exécutable SwProcessExample
add_executable(SwProcessExample SwProcessExample.cpp)

# Inclure le répertoire de src/core pour les en-têtes
target_include_directories(SwProcessExample PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
