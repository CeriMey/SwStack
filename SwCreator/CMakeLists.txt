cmake_minimum_required(VERSION 3.10)
project(SwCreator)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(SwCreatorUi
    app/SwCreatorDocument.cpp
    app/SwCreatorController.cpp
    commands/SwCreatorCommandStack.cpp
    commands/SwCreatorCreateWidgetCommand.cpp
    commands/SwCreatorDeleteWidgetCommand.cpp
    serialization/SwCreatorSwuiSerializer.cpp
    serialization/SwCreatorQtUiImporter.cpp
    designer/SwCreatorFormCanvas.cpp
    designer/SwCreatorSystemDragDrop.cpp
    designer/SwCreatorWorkspace.cpp
    designer/SwCreatorMainPanel.cpp
    designer/dialogs/SwCreatorDockDialog.cpp
    designer/dialogs/SwCreatorTextEditDialog.cpp
    designer/dialogs/SwCreatorStringListDialog.cpp
    designer/inspector/SwCreatorPropertyInspector.cpp
    designer/palette/SwCreatorPaletteItem.cpp
    designer/palette/SwCreatorPalettePage.cpp
    designer/palette/SwCreatorWidgetPalette.cpp
    ui/SwCreatorShell.cpp
)
if(MSVC)
    # Debug builds can exceed the COFF section limit due to large translation units.
    target_compile_options(SwCreatorUi PRIVATE /bigobj /FS)
endif()
target_include_directories(SwCreatorUi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

set(SWCREATOR_EDITOR_SOURCES
    editor/SwCreatorEditorCodeSupport.cpp
    editor/SwCreatorEditorPage.cpp
    editor/SwCreatorEditorTabWidget.cpp
    editor/SwCreatorEditorPanel.cpp
)
set(SWCREATOR_EDITOR_APP app/SwCreatorEditorApp.cpp)
set(SWCREATOR_EDITOR_AVAILABLE TRUE)
foreach(swcreator_editor_file ${SWCREATOR_EDITOR_SOURCES} ${SWCREATOR_EDITOR_APP})
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${swcreator_editor_file}")
        set(SWCREATOR_EDITOR_AVAILABLE FALSE)
    endif()
endforeach()

if(SWCREATOR_EDITOR_AVAILABLE)
    add_library(SwCreatorEditorUi
        ${SWCREATOR_EDITOR_SOURCES}
    )
    if(MSVC)
        target_compile_options(SwCreatorEditorUi PRIVATE /bigobj /FS)
    endif()
    target_include_directories(SwCreatorEditorUi PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/core
    )
    target_link_libraries(SwCreatorUi PUBLIC SwCreatorEditorUi)
endif()

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

if(SWCREATOR_EDITOR_AVAILABLE)
    add_executable(SwCreatorEditor ${SWCREATOR_EDITOR_APP})
    target_link_libraries(SwCreatorEditor PRIVATE SwCreatorEditorUi)
    if(MSVC)
        target_compile_options(SwCreatorEditor PRIVATE /bigobj)
    endif()
endif()

if(WIN32)
    target_link_libraries(SwCreator PRIVATE ws2_32 user32 gdi32 ole32)
    target_link_libraries(SwCreatorSnapshot PRIVATE ws2_32 user32 gdi32 ole32)
    if(SWCREATOR_EDITOR_AVAILABLE)
        target_link_libraries(SwCreatorEditor PRIVATE ws2_32 user32 gdi32 ole32)
    endif()
elseif(UNIX AND NOT APPLE)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(SwCreator PRIVATE OpenSSL::Crypto)
    target_link_libraries(SwCreatorSnapshot PRIVATE OpenSSL::Crypto)
    if(SWCREATOR_EDITOR_AVAILABLE)
        target_link_libraries(SwCreatorEditor PRIVATE OpenSSL::Crypto)
    endif()

    find_package(X11)
    if(NOT X11_FOUND)
        message(WARNING "X11 not found, skipping SwCreator targets.")
        return()
    endif()

    target_include_directories(SwCreator PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(SwCreator PRIVATE ${X11_LIBRARIES} pthread)

    target_include_directories(SwCreatorSnapshot PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(SwCreatorSnapshot PRIVATE ${X11_LIBRARIES} pthread)

    if(SWCREATOR_EDITOR_AVAILABLE)
        target_include_directories(SwCreatorEditor PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(SwCreatorEditor PRIVATE ${X11_LIBRARIES} pthread)
    endif()
endif()
