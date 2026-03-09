cmake_minimum_required(VERSION 3.10)
project(NodeEditorExample)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET SwizioNodes)
    message(FATAL_ERROR "Build NodeEditorExample from the repository root so the SwizioNodes target is available.")
endif()

add_executable(NodeEditorExample
    NodeEditorExample.cpp
    nodes/AddNodeModel.cpp
    nodes/BinaryMathModel.cpp
    nodes/GenericNodeModel.cpp
    nodes/InputNodeModel.cpp
    nodes/OutputNodeModel.cpp
    nodes/TimesNodeModel.cpp
)
target_include_directories(NodeEditorExample PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)
target_link_libraries(NodeEditorExample PRIVATE SwizioNodes)

if(MSVC)
    # Debug builds can exceed the COFF section limit due to large translation units.
    target_compile_options(NodeEditorExample PRIVATE /bigobj)
endif()

if(WIN32)
    target_link_libraries(NodeEditorExample PRIVATE ws2_32 user32 gdi32)
elseif(UNIX AND NOT APPLE)
    find_package(X11)
    if(NOT X11_FOUND)
        message(WARNING "X11 not found, skipping NodeEditorExample target.")
        return()
    endif()
    target_include_directories(NodeEditorExample PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(NodeEditorExample PRIVATE ${X11_LIBRARIES} pthread)

    find_package(OpenSSL REQUIRED)
    target_link_libraries(NodeEditorExample PRIVATE OpenSSL::Crypto)
endif()
