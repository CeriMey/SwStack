cmake_minimum_required(VERSION 3.10)
project(SwCreator)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(SwCreatorUi
    commands/SwCreatorCommandStack.cpp
    commands/SwCreatorCreateWidgetCommand.cpp
    commands/SwCreatorDeleteWidgetCommand.cpp
    serialization/SwCreatorSwuiSerializer.cpp
    ui/SwCreatorFormCanvas.cpp
    ui/SwCreatorSystemDragDrop.cpp
    ui/SwCreatorWorkspace.cpp
    ui/SwCreatorMainPanel.cpp
    ui/dialogs/SwCreatorDockDialog.cpp
    ui/dialogs/SwCreatorTextEditDialog.cpp
    ui/dialogs/SwCreatorStringListDialog.cpp
    ui/inspector/SwCreatorPropertyInspector.cpp
    ui/palette/SwCreatorPaletteItem.cpp
    ui/palette/SwCreatorPalettePage.cpp
    ui/palette/SwCreatorWidgetPalette.cpp
)
if(MSVC)
    # Debug builds can exceed the COFF section limit due to large translation units.
    target_compile_options(SwCreatorUi PRIVATE /bigobj)
endif()
target_include_directories(SwCreatorUi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

add_executable(SwCreator app/SwCreatorApp.cpp)
target_link_libraries(SwCreator PRIVATE SwCreatorUi)
if(MSVC)
    target_compile_options(SwCreator PRIVATE /bigobj)
endif()

add_executable(SwCreatorSnapshot app/SwCreatorSnapshot.cpp)
target_link_libraries(SwCreatorSnapshot PRIVATE SwCreatorUi)
if(MSVC)
    target_compile_options(SwCreatorSnapshot PRIVATE /bigobj)
endif()

if(WIN32)
    target_link_libraries(SwCreator PRIVATE ws2_32 user32 gdi32 ole32)
    target_link_libraries(SwCreatorSnapshot PRIVATE ws2_32 user32 gdi32 ole32)
elseif(UNIX AND NOT APPLE)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(SwCreator PRIVATE OpenSSL::Crypto)
    target_link_libraries(SwCreatorSnapshot PRIVATE OpenSSL::Crypto)

    find_package(X11)
    if(NOT X11_FOUND)
        message(WARNING "X11 not found, skipping SwCreator targets.")
        return()
    endif()

    target_include_directories(SwCreator PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(SwCreator PRIVATE ${X11_LIBRARIES} pthread)

    target_include_directories(SwCreatorSnapshot PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(SwCreatorSnapshot PRIVATE ${X11_LIBRARIES} pthread)
endif()
