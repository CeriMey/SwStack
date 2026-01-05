cmake_minimum_required(VERSION 3.10)
project(WhatsApp)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(WhatsApp
    main.cpp
    WhatsAppWindow.cpp
    WhatsAppWidget.cpp
    WaAvatarCircle.cpp
    WaIconButton.cpp
    WaChatWallpaper.cpp
    WaAttachMenuPopup.cpp
    WaLocalStore.cpp
    WaConversationListDelegate.cpp
    WaConversationRowWidget.cpp
    WaDemoAssets.cpp
    WaEmojiPickerPopup.cpp
    WaLoginPage.cpp
    WaMessageEdit.cpp
    WaMediaImageLoader.cpp
    WaThreadBubbleDelegate.cpp
    WaMediaViewerDialog.cpp
)

target_include_directories(WhatsApp PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

if(MSVC)
    target_compile_options(WhatsApp PRIVATE /bigobj)
endif()

if(WIN32)
    target_link_libraries(WhatsApp PRIVATE
        ws2_32
        user32
        gdi32
        mfplat
        mfreadwrite
        mf
        mfuuid
        ole32
        oleaut32
        uuid)
elseif(UNIX AND NOT APPLE)
    find_package(X11)
    if(NOT X11_FOUND)
        message(WARNING "X11 not found, skipping WhatsApp target.")
        return()
    endif()
    target_include_directories(WhatsApp PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(WhatsApp PRIVATE ${X11_LIBRARIES} pthread)

    find_package(OpenSSL REQUIRED)
    target_link_libraries(WhatsApp PRIVATE OpenSSL::Crypto)
endif()
