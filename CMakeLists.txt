cmake_minimum_required(VERSION 3.10)
project(SwCore)

include(CheckIPOSupported)

# Utiliser le standard C++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Avoid injecting system include dirs as -isystem /usr/include, which breaks WSL toolchains in this repo layout.
set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES "")
set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES "")

if (MSVC)
    add_compile_options(/Zc:preprocessor /bigobj)
    # Keep Release optimized for speed while still emitting PDB/symbol data.
    string(APPEND CMAKE_C_FLAGS_RELEASE " /Zi /GL /Gw")
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " /Zi /GL /Gw")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " /DEBUG /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_RELEASE " /DEBUG /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    # Preserve symbols in Release and enable section splitting so the linker can strip more dead code.
    string(APPEND CMAKE_C_FLAGS_RELEASE " -g -ffunction-sections -fdata-sections")
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " -g -ffunction-sections -fdata-sections")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " -Wl,--gc-sections")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_RELEASE " -Wl,--gc-sections")
endif()

check_ipo_supported(RESULT SW_ENABLE_IPO OUTPUT SW_IPO_ERROR)
if(SW_ENABLE_IPO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
else()
    message(STATUS "IPO/LTO disabled: ${SW_IPO_ERROR}")
endif()

# Ajouter les répertoires sources
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/runtime)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/object)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/gui)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/io)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/installer)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/fs)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/hw)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/platform)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/storage)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/types)

if(UNIX AND NOT APPLE AND NOT ANDROID)
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

add_subdirectory(tools/SwInstallerPack)

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

# Top-level examples are selectable directly from CMake without relying on build.bat.
# Default behavior stays close to the current tree: only WhatsApp is enabled by default.
option(SW_BUILD_ALL_EXAMPLES "Build all top-level examples" OFF)

function(sw_add_example_subdirectory example_dir default_enabled)
    string(REPLACE "/" "_" option_suffix "${example_dir}")
    string(REPLACE "-" "_" option_suffix "${option_suffix}")
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" option_suffix "${option_suffix}")
    set(option_name "SW_BUILD_${option_suffix}")

    option(${option_name} "Build ${example_dir}" ${default_enabled})

    if((SW_BUILD_ALL_EXAMPLES OR ${option_name}) AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${example_dir}/CMakeLists.txt")
        add_subdirectory(${example_dir})
    endif()
endfunction()

if(ANDROID)
    add_subdirectory(android/SwAndroidHost)
else()
    sw_add_example_subdirectory(exemples/01-SimpleWindow OFF)
    sw_add_example_subdirectory(exemples/02-CoreApplication OFF)
    sw_add_example_subdirectory(exemples/03-TcpServer OFF)
    sw_add_example_subdirectory(exemples/04-NetworkAccesManager OFF)
    sw_add_example_subdirectory(exemples/05-InteractiveConsoleApplication OFF)
    sw_add_example_subdirectory(exemples/06-DebugExemple OFF)
    sw_add_example_subdirectory(exemples/07-ServeurDebug OFF)
    sw_add_example_subdirectory(exemples/08-RegisterType OFF)
    sw_add_example_subdirectory(exemples/09-MultiRuntime OFF)
    sw_add_example_subdirectory(exemples/10-SwProcessExample OFF)
    sw_add_example_subdirectory(exemples/11-TestEventLoop OFF)
    sw_add_example_subdirectory(exemples/12-TestThread OFF)
    sw_add_example_subdirectory(exemples/13-ObjectLifecycleTest OFF)
    sw_add_example_subdirectory(exemples/14-PingPong OFF)
    sw_add_example_subdirectory(exemples/15-TestThreadPool OFF)
    sw_add_example_subdirectory(exemples/16-GpioMonitor OFF)
    sw_add_example_subdirectory(exemples/16-VideoWidget OFF)
    sw_add_example_subdirectory(exemples/17-MjpegServer OFF)
    sw_add_example_subdirectory(exemples/18-MjpegClient OFF)
    sw_add_example_subdirectory(exemples/18-SliderDemo OFF)
    sw_add_example_subdirectory(exemples/19-RtspUdpClient OFF)
    sw_add_example_subdirectory(exemples/20-RtspVideoWidget OFF)
    sw_add_example_subdirectory(exemples/21-UdpProbe OFF)
    sw_add_example_subdirectory(exemples/22-TileServer OFF)
    sw_add_example_subdirectory(exemples/23-ConfigurableObjectDemo OFF)
    sw_add_example_subdirectory(exemples/24-ComponentPlugin OFF)
    sw_add_example_subdirectory(exemples/25-IpcPerfMonitor OFF)
    sw_add_example_subdirectory(exemples/26-IpcThreadStress OFF)
    sw_add_example_subdirectory(exemples/27-IpcVideoFrameRingBuffer OFF)
    sw_add_example_subdirectory(exemples/28-IpcRingBufferCameraPlayer OFF)
    sw_add_example_subdirectory(exemples/29-IpcPingPongNodes OFF)
    sw_add_example_subdirectory(exemples/32-WhatsApp ON)
    sw_add_example_subdirectory(exemples/33-Nodeeditor OFF)
    sw_add_example_subdirectory(exemples/34-WebSocket OFF)
    sw_add_example_subdirectory(exemples/35-HttpServerSelfTest OFF)
sw_add_example_subdirectory(exemples/37-HttpAppQuickstart OFF)
sw_add_example_subdirectory(exemples/38-DocumentEditor OFF)
sw_add_example_subdirectory(exemples/39-CodeEditorPerf OFF)
sw_add_example_subdirectory(exemples/40-FiberPoolSelfTest OFF)
sw_add_example_subdirectory(exemples/41-RuntimeProfilerSelfTest OFF)
sw_add_example_subdirectory(exemples/42-QtSwEmbeddedInterop OFF)
sw_add_example_subdirectory(exemples/43-VigilVpn OFF)
sw_add_example_subdirectory(exemples/44-HttpsDownloadTest OFF)
sw_add_example_subdirectory(exemples/45-SslSocketSelfTest OFF)
sw_add_example_subdirectory(exemples/46-HttpAcmeSelfTest OFF)
sw_add_example_subdirectory(exemples/47-SocketTrafficMonitorSelfTest OFF)
sw_add_example_subdirectory(exemples/48-SurfaceWidgetSelfTest OFF)
sw_add_example_subdirectory(exemples/49-RtspTrackGraphSelfTest OFF)
sw_add_example_subdirectory(exemples/50-VideoPipelinePressureSelfTest OFF)
sw_add_example_subdirectory(exemples/51-EmbeddedDbSelfTest OFF)
sw_add_example_subdirectory(exemples/52-EmbeddedDbBench OFF)
sw_add_example_subdirectory(exemples/53-EmbeddedDbQtSqlBench OFF)
sw_add_example_subdirectory(exemples/54-MailSelfTest OFF)
sw_add_example_subdirectory(exemples/55-HttpPerfSelfTest OFF)
sw_add_example_subdirectory(exemples/56-LoadingOverlayDemo OFF)
sw_add_example_subdirectory(exemples/57-TableDbSelfTest OFF)
sw_add_example_subdirectory(sandbox/SwMainWindowApplication OFF)
endif()

# Sw "nodes"/tools
add_subdirectory(tools/SwNode)

# SwCreator (UI designer)
if(NOT ANDROID)
    add_subdirectory(SwCreator)
endif()
