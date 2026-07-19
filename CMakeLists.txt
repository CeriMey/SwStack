cmake_minimum_required(VERSION 3.10)
project(SwCore)

include(CheckIPOSupported)
enable_testing()

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
    if(APPLE)
        # ld64 uses -dead_strip; GNU --gc-sections is rejected by AppleClang's
        # linker driver even though AppleClang matches the compiler branch.
        string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " -Wl,-dead_strip")
        string(APPEND CMAKE_SHARED_LINKER_FLAGS_RELEASE " -Wl,-dead_strip")
    else()
        string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " -Wl,--gc-sections")
        string(APPEND CMAKE_SHARED_LINKER_FLAGS_RELEASE " -Wl,--gc-sections")
    endif()
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

# QUIC's TLS 1.3 implementation uses the OpenSSL EVP/X.509 APIs directly.
# Keep that dependency opt-out at configure time so Android, macOS and builds
# which do not consume SwQuicCore are not forced by the QUIC integration to
# provide a desktop OpenSSL package. Windows and Linux retain the existing
# HTTP/3-enabled default.
set(SW_ENABLE_QUIC_DEFAULT ON)
if(APPLE OR ANDROID)
    set(SW_ENABLE_QUIC_DEFAULT OFF)
endif()
option(SW_ENABLE_QUIC "Enable QUIC/HTTP3 targets and their OpenSSL dependency" ${SW_ENABLE_QUIC_DEFAULT})

if(SW_ENABLE_QUIC)
    find_package(OpenSSL 1.1.1 REQUIRED)
endif()

find_package(Threads REQUIRED)

if(UNIX AND NOT APPLE AND NOT ANDROID)
    find_package(CURL REQUIRED)
    include_directories(${CURL_INCLUDE_DIRS})
    link_libraries(${CURL_LIBRARIES})

    if(SW_ENABLE_QUIC)
        link_libraries(OpenSSL::Crypto)
    endif()
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

# Third-party: portable ML-KEM-768 implementation used by the QUIC TLS 1.3
# X25519MLKEM768 group.  It is independent from the system OpenSSL version.
add_subdirectory(src/core/third_party/mlkem)
link_libraries(SwMlKem)

add_library(SwQuicCore INTERFACE)
target_include_directories(SwQuicCore INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/object
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/gui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/io
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/installer
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/fs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/hw
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/platform
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/storage
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/types
)
# SwHttpApp is part of this public header aggregate and currently reaches
# SwAny, which uses C++17 `if constexpr`. Isolated QUIC primitives remain
# C++11-compatible, but consumers of this aggregate need the honest standard.
target_compile_features(SwQuicCore INTERFACE cxx_std_17)
target_link_libraries(SwQuicCore INTERFACE SwMiniz SwMlKem Threads::Threads ${CMAKE_DL_LIBS})

if(SW_ENABLE_QUIC)
    target_link_libraries(SwQuicCore INTERFACE OpenSSL::Crypto)
    if(WIN32)
        target_link_libraries(SwQuicCore INTERFACE bcrypt crypt32 ws2_32)
    endif()
    if(MINGW)
        # The header-only HTTP/3 stack creates large COFF translation units.
        target_compile_options(SwQuicCore INTERFACE -Wa,-mbig-obj)
    endif()
else()
    # Downstream projects can use this marker to avoid including the QUIC
    # headers. A target which actually includes SwHttpApp/QUIC still needs to
    # configure with SW_ENABLE_QUIC=ON because those public headers use OpenSSL.
    target_compile_definitions(SwQuicCore INTERFACE SW_QUIC_DEPENDENCIES_DISABLED=1)
endif()

option(SW_BUILD_AUTH "Build the generic authentication module" ${SW_ENABLE_QUIC})
if(SW_BUILD_AUTH)
    add_subdirectory(src/core/auth)
endif()

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

# These targets include SwHttpApp or the native QUIC/TLS headers and therefore
# require the OpenSSL dependency enabled above. Keep SW_BUILD_ALL_EXAMPLES
# usable on Apple/Android and in deliberate dependency-free configurations.
set(SW_QUIC_DEPENDENT_EXAMPLES
    exemples/34-WebSocket
    exemples/37-HttpAppQuickstart
    exemples/44-HttpsDownloadTest
    exemples/46-HttpAcmeSelfTest
    exemples/71-QuicPacketCodecSelfTest
    exemples/72-QuicCloudflareProbe
    exemples/73-QuicHttp3FoundationsSelfTest
    exemples/74-QuicHandshakeSelfTest
    exemples/75-QuicHandshakeProbe
    exemples/76-Http3ServerSelfTest
    exemples/77-QuicHandshakeLoopbackSelfTest
    exemples/78-Http3ServerOverUdpSelfTest
    exemples/79-Http3ClientServerSelfTest
    exemples/80-QuicZeroRttSelfTest
    exemples/81-QuicZeroRttLiveSelfTest
    exemples/82-Http3ZeroRttClientServerSelfTest
    exemples/86-SwTlsSecretLifetimeSelfTest
    exemples/90-SwQuicSocket
    exemples/92-HttpAppHttp3SelfTest
    exemples/93-HttpSeoSelfTest
    exemples/94-HttpAppDbBench
    exemples/98-Http3LoadBench
)

function(sw_add_example_subdirectory example_dir default_enabled)
    list(FIND SW_QUIC_DEPENDENT_EXAMPLES "${example_dir}" quic_example_index)
    if(NOT SW_ENABLE_QUIC AND NOT quic_example_index EQUAL -1)
        message(STATUS "Skipping ${example_dir}: SW_ENABLE_QUIC=OFF")
        return()
    endif()

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
sw_add_example_subdirectory(exemples/58-WidgetPerfSelfTest OFF)
sw_add_example_subdirectory(exemples/59-GridLayoutSelfTest OFF)
sw_add_example_subdirectory(exemples/60-FuturePromiseSelfTest OFF)
sw_add_example_subdirectory(exemples/61-PlatformVideoDecoderSelfTest ON)
sw_add_example_subdirectory(exemples/62-SwVtpAv1SelfTest ON)
sw_add_example_subdirectory(exemples/63-SwVtpUdpLoopback ON)
sw_add_example_subdirectory(exemples/64-SwVtpPlayer ON)
sw_add_example_subdirectory(exemples/65-IpcLatestOnlySelfTest OFF)
sw_add_example_subdirectory(exemples/65-MediaServerArchitectureSelfTest ON)
sw_add_example_subdirectory(exemples/66-MediaTransportEndToEndSelfTest ON)
  sw_add_example_subdirectory(exemples/67-SdpMediaOptionsSelfTest OFF)
  sw_add_example_subdirectory(exemples/69-ObsbotOnvifRtspGateway OFF)
  sw_add_example_subdirectory(exemples/70-SwJsonSelfTest OFF)
  sw_add_example_subdirectory(exemples/71-QuicPacketCodecSelfTest OFF)
  sw_add_example_subdirectory(exemples/72-QuicCloudflareProbe OFF)
  sw_add_example_subdirectory(exemples/73-QuicHttp3FoundationsSelfTest OFF)
  sw_add_example_subdirectory(exemples/74-QuicHandshakeSelfTest OFF)
  sw_add_example_subdirectory(exemples/75-QuicHandshakeProbe OFF)
  sw_add_example_subdirectory(exemples/76-Http3ServerSelfTest OFF)
  sw_add_example_subdirectory(exemples/77-QuicHandshakeLoopbackSelfTest OFF)
  sw_add_example_subdirectory(exemples/78-Http3ServerOverUdpSelfTest OFF)
  sw_add_example_subdirectory(exemples/79-Http3ClientServerSelfTest OFF)
  sw_add_example_subdirectory(exemples/80-QuicZeroRttSelfTest OFF)
  sw_add_example_subdirectory(exemples/81-QuicZeroRttLiveSelfTest OFF)
  sw_add_example_subdirectory(exemples/82-Http3ZeroRttClientServerSelfTest OFF)
  sw_add_example_subdirectory(exemples/83-SwDequeueSelfTest OFF)
  sw_add_example_subdirectory(exemples/84-SwRingQueueSelfTest OFF)
  sw_add_example_subdirectory(exemples/85-SwByteRingBufferSelfTest OFF)
  sw_add_example_subdirectory(exemples/86-SwTlsSecretLifetimeSelfTest OFF)
  sw_add_example_subdirectory(exemples/87-SwTcpSocketSelfTest OFF)
  sw_add_example_subdirectory(exemples/88-SwUdpSocketSelfTest OFF)
  sw_add_example_subdirectory(exemples/89-HttpClientSelfTest OFF)
  sw_add_example_subdirectory(exemples/90-SwQuicSocket OFF)
  sw_add_example_subdirectory(exemples/91-TimerSchedulerSelfTest OFF)
  sw_add_example_subdirectory(exemples/92-HttpAppHttp3SelfTest OFF)
  sw_add_example_subdirectory(exemples/93-HttpSeoSelfTest OFF)
  sw_add_example_subdirectory(exemples/94-HttpAppDbBench OFF)
  sw_add_example_subdirectory(exemples/95-LinuxMediaBackendsSelfTest ON)
  sw_add_example_subdirectory(exemples/96-SrtMediaSelfTest ON)
  sw_add_example_subdirectory(exemples/97-SrtFileStreamer OFF)
  sw_add_example_subdirectory(exemples/98-Http3LoadBench OFF)
  sw_add_example_subdirectory(sandbox/SwMainWindowApplication OFF)
endif()

# Sw "nodes"/tools
add_subdirectory(tools/SwNode)

# SwBridge includes SwHttpApp and therefore cannot be built without the QUIC
# crypto headers. Keep the remaining SwNode tools available in a deliberate
# dependency-free configuration while preventing SwBridge from entering ALL.
if(NOT SW_ENABLE_QUIC AND TARGET SwBridge)
    set_target_properties(SwBridge PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

# SwCreator (UI designer)
if(NOT ANDROID)
    add_subdirectory(SwCreator)
endif()
