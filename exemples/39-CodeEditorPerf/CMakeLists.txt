cmake_minimum_required(VERSION 3.10)
project(CodeEditorPerf)

add_executable(CodeEditorPerf CodeEditorPerf.cpp)

if(WIN32)
    target_link_libraries(CodeEditorPerf PRIVATE ws2_32 user32 gdi32)
elseif(UNIX AND NOT APPLE)
    find_package(X11)
    if(NOT X11_FOUND)
        message(WARNING "X11 not found, skipping CodeEditorPerf target.")
        return()
    endif()

    target_include_directories(CodeEditorPerf PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(CodeEditorPerf PRIVATE ${X11_LIBRARIES})
endif()
