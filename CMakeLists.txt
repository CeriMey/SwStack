cmake_minimum_required(VERSION 3.10)
project(SwCore)

# Utiliser le standard C++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Avoid injecting system include dirs as -isystem /usr/include, which breaks WSL toolchains in this repo layout.
set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES "")
set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES "")

if (MSVC)
    add_compile_options(/Zc:preprocessor)
endif()


# Ajouter les répertoires sources
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/runtime)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/object)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/gui)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/io)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/fs)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/hw)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/platform)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/types)

if(UNIX AND NOT APPLE)
    find_package(CURL REQUIRED)
    include_directories(${CURL_INCLUDE_DIRS})
    link_libraries(${CURL_LIBRARIES})

    find_package(OpenSSL REQUIRED)
    link_libraries(OpenSSL::Crypto)
endif()

# Third-party: miniz (used by SwWebSocket permessage-deflate).
add_library(SwMiniz STATIC
    src/core/third_party/miniz/miniz.c
    src/core/third_party/miniz/miniz_tdef.c
    src/core/third_party/miniz/miniz_tinfl.c
)
target_include_directories(SwMiniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core/third_party/miniz)
target_compile_definitions(SwMiniz PUBLIC MINIZ_NO_STDIO MINIZ_NO_ARCHIVE_APIS)
set_target_properties(SwMiniz PROPERTIES POSITION_INDEPENDENT_CODE ON)
link_libraries(SwMiniz)

# SwizioNodes (ported node editor core)
add_library(SwizioNodes STATIC
    src/core/gui/SwizioNodes/internal/NodeDelegateModel.cpp
    src/core/gui/SwizioNodes/internal/NodeDelegateModelRegistry.cpp
)
target_include_directories(SwizioNodes PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)
set_target_properties(SwizioNodes PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Ajouter tous les exemples dans le dossier exemples
#add_subdirectory(exemples/01-SimpleWindow)

#add_subdirectory(exemples/02-CoreApplication)


#add_subdirectory(exemples/05-InteractiveConsoleApplication)

#add_subdirectory(exemples/06-DebugExemple)
#add_subdirectory(exemples/07-ServeurDebug)

#add_subdirectory(exemples/08-RegisterType)
#add_subdirectory(exemples/03-TcpServer)
#add_subdirectory(exemples/04-NetworkAccesManager)
#add_subdirectory(exemples/09-MultiRuntime)
#add_subdirectory(exemples/10-SwProcessExample)
#add_subdirectory(exemples/11-TestEventLoop)
#add_subdirectory(exemples/12-TestThread)
#add_subdirectory(exemples/13-ObjectLifecycleTest)
##add_subdirectory(exemples/14-PingPong)
#add_subdirectory(exemples/15-GimbalControl)
#add_subdirectory(exemples/15b-GimbalStatus)
#add_subdirectory(exemples/16-VideoWidget)
#add_subdirectory(exemples/18-SliderDemo)
#add_subdirectory(exemples/17-MjpegServer)
#add_subdirectory(exemples/18-MjpegClient)
#add_subdirectory(exemples/19-RtspUdpClient)
#add_subdirectory(exemples/20-RtspVideoWidget)
#add_subdirectory(exemples/22-TileServer)
#add_subdirectory(exemples/23-ConfigurableObjectDemo)
#add_subdirectory(exemples/24-ComponentPlugin)
#add_subdirectory(exemples/25-IpcPerfMonitor)
#add_subdirectory(exemples/26-IpcThreadStress)
#add_subdirectory(exemples/27-IpcVideoFrameRingBuffer)
#add_subdirectory(exemples/28-IpcRingBufferCameraPlayer)
#add_subdirectory(exemples/29-IpcPingPongNodes)
#add_subdirectory(exemples/30-TabWidgetSnapshot)
#add_subdirectory(exemples/31-WidgetGallerySnapshot)
#add_subdirectory(exemples/31bis-WidgetGallerySnapshot)
add_subdirectory(exemples/32-WhatsApp)
add_subdirectory(exemples/33-Nodeeditor)
#add_subdirectory(exemples/34-WebSocket)

# Sw "nodes"/tools
#add_subdirectory(SwNode)

# SwCreator (UI designer)
add_subdirectory(SwCreator)
