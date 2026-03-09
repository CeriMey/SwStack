cmake_minimum_required(VERSION 3.10)
project(DocumentEditor)

add_executable(DocumentEditor DocumentEditor.cpp)

if(WIN32)
    target_link_libraries(DocumentEditor PRIVATE ws2_32 user32 gdi32)
elseif(UNIX AND NOT APPLE)
    find_package(X11)
    if(NOT X11_FOUND)
        message(WARNING "X11 not found, skipping DocumentEditor target.")
        return()
    endif()

    target_include_directories(DocumentEditor PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(DocumentEditor PRIVATE ${X11_LIBRARIES})
endif()
