cmake_minimum_required(VERSION 3.10)
project(SwInstallerPack)

add_executable(SwInstallerPack
    SwInstallerPack.cpp
)

target_include_directories(SwInstallerPack PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/installer
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(SwInstallerPack PRIVATE cxx_std_17)
target_link_libraries(SwInstallerPack PRIVATE SwMiniz)

add_subdirectory(tests)
