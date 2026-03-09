cmake_minimum_required(VERSION 3.10)
project(SwBridge)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SwBridge
    SwBridge.cpp
    SwBridgeApp.cpp
    SwBridgeHttpServer.cpp
)

add_custom_command(TARGET SwBridge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:SwBridge>/css
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:SwBridge>/js
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/index.html
        $<TARGET_FILE_DIR:SwBridge>/index.html
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/css/style.css
        $<TARGET_FILE_DIR:SwBridge>/css/style.css
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/js/app.js
        $<TARGET_FILE_DIR:SwBridge>/js/app.js
)

if(WIN32)
    target_link_libraries(SwBridge PRIVATE crypt32)
endif()

target_include_directories(SwBridge PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}
)
