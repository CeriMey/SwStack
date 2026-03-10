cmake_minimum_required(VERSION 3.22)

project(SwAndroidHost LANGUAGES C CXX)

if(NOT ANDROID)
    message(FATAL_ERROR "SwAndroidHost must be built with the Android NDK toolchain.")
endif()

set(SW_ANDROID_NATIVE_APP_GLUE_DIR "${ANDROID_NDK}/sources/android/native_app_glue")
if(NOT EXISTS "${SW_ANDROID_NATIVE_APP_GLUE_DIR}/android_native_app_glue.c")
    message(FATAL_ERROR "android_native_app_glue was not found in ${SW_ANDROID_NATIVE_APP_GLUE_DIR}")
endif()

add_library(SwAndroidHost SHARED
    main.cpp
    "../../exemples/38-DocumentEditor/DocumentEditor.cpp"
    "${SW_ANDROID_NATIVE_APP_GLUE_DIR}/android_native_app_glue.c"
)

set_source_files_properties("../../exemples/38-DocumentEditor/DocumentEditor.cpp" PROPERTIES
    COMPILE_DEFINITIONS "main=SwDocumentEditorEmbeddedEntryPoint"
)

target_include_directories(SwAndroidHost PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/core"
    "${CMAKE_SOURCE_DIR}/src/core/runtime"
    "${CMAKE_SOURCE_DIR}/src/core/object"
    "${CMAKE_SOURCE_DIR}/src/core/remote"
    "${CMAKE_SOURCE_DIR}/src/core/gui"
    "${CMAKE_SOURCE_DIR}/src/core/io"
    "${CMAKE_SOURCE_DIR}/src/core/fs"
    "${CMAKE_SOURCE_DIR}/src/core/hw"
    "${CMAKE_SOURCE_DIR}/src/core/platform"
    "${CMAKE_SOURCE_DIR}/src/core/types"
    "${SW_ANDROID_NATIVE_APP_GLUE_DIR}"
)

target_link_libraries(SwAndroidHost PRIVATE
    android
    log
)

set_target_properties(SwAndroidHost PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
