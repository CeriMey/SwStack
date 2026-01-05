# Cartographie du code (CODEMAP)

## Racine du dépôt

- Build:
  - `CMakeLists.txt` (ajoute chaque exemple via `add_subdirectory(exemples/**)`)
  - `build.bat`, `build.sh` (wrappers CMake)
  - `build-win/`, `build-check/`, `build-wsl/` (répertoires de build déjà présents; généralement régénérables)
- Documentation:
  - `docs/` (documentation technique)
- Source:
  - `src/` (bibliothèque majoritairement header-only, consommée par les exemples)
  - `exemples/` (applications de démonstration, chacune avec un `CMakeLists.txt` + un `main`)
  - `systemConfig/` (configuration JSON lue/écrite par `SwRemoteObject`)

## `src/` (bibliothèque)

> Convention observée: beaucoup de composants sont header-only (`*.h`).

### `src/atomic/` — Concurrence (thread worker + event loop)

- `src/atomic/thread.h`: `sw::atomic::Thread` (thread worker + `SwCoreApplication` interne; adoption du thread courant)

### `src/core/` — Runtime, objets, IPC/remote, IO, UI

Sous-systèmes principaux (organisés par modules):

- Runtime / scheduling:
  - `src/core/runtime/SwCoreApplication.h` (event loop + fibres + timers + waitables OS)
  - `src/core/runtime/SwEventLoop.h` (nested loop; macros coopératives `swhile/tswhile`)
  - `src/core/runtime/SwTimer.h` (timers + `singleShot`)
  - `src/core/runtime/SwThread.h` (`SwThread`, pont haut niveau vers `sw::atomic::Thread`)
  - `src/core/runtime/linux_fiber.h` (fibres Linux via `ucontext`)
  - `src/core/runtime/SwCommandLineParser.h`, `src/core/runtime/SwCommandLineOption.h` (CLI)
  - `src/core/runtime/SwLibrary.h` (wrapper LoadLibrary/dlopen)
- Modèle objet & meta:
  - `src/core/object/SwObject.h` (signaux/slots, propriétés, lifecycle, `moveToThread`)
  - `src/core/object/SwMetaType.h` (meta-type `À CONFIRMER`: usages exacts)
  - `src/core/object/SwPointer.h` (pointeur "faible" sur `SwObject`)
  - `src/core/types/SwAny.h` (variant/any typé)
- IPC/Remote (même machine):
  - `src/core/remote/SwSharedMemorySignal.h` (pub/sub SHM, registries, wakeups OS, `LoopPoller`)
  - `src/core/remote/SwIpcRpc.h` (RPC request/response via queues SHM)
  - `src/core/remote/SwIpcNoCopyRingBuffer.h` (ring buffer SHM no-copy)
  - `src/core/remote/SwProxyObject.h`, `src/core/remote/SwProxyObjectBrowser.h` (découverte / proxy objects)
  - `src/core/remote/SwRemoteObjectComponent.h`, `src/core/remote/SwRemoteObjectComponentRegistry.h` (container + plugins)
  - `src/core/remote/SwRemoteObject.h` (config JSON multi-couches + IPC optionnel + helpers RPC)
  - `src/core/remote/SwRemoteObjectNode.h` (macro `SW_REMOTE_OBJECT_NODE` pour standardiser un `main` piloté par JSON/args)
- IO (réseau + fichiers/process):
  - `src/core/io/SwAbstractSocket.h` (base)
  - `src/core/io/SwTcpSocket.h`, `src/core/io/SwTcpServer.h`, `src/core/io/SwUdpSocket.h`
  - `src/core/io/SwNetworkAccessManager.h` (HTTP/URL; `À CONFIRMER`: backends exacts selon OS)
  - `src/core/io/SwBackendSsl.h` (backend TLS OpenSSL dynamique; attention vérification certif)
  - `src/core/io/SwFile.h`, `src/core/io/SwIODevice.h`, `src/core/io/SwIODescriptor.h`
  - `src/core/io/SwProcess.h`
  - `src/core/io/SwSerial.h`
- Types & sérialisation:
  - JSON: `src/core/types/SwJsonDocument.h`, `src/core/types/SwJsonObject.h`, `src/core/types/SwJsonArray.h`, `src/core/types/SwJsonValue.h`
  - Strings/containers: `src/core/types/SwString.h`, `src/core/types/SwByteArray.h`, `src/core/types/SwList.h`, `src/core/types/SwMap.h`, `src/core/types/SwVector.h`, `src/core/types/SwPair.h`
  - Utilitaires: `src/core/types/SwCrypto.h`, `src/core/types/SwHash.h`, `src/core/types/SwFlags.h`, `src/core/types/SwDateTime.h`, `src/core/types/SwRegularExpression.h`, `src/core/types/SwDebug.h`, `src/core/types/Sw.h`
- FS / settings / paths:
  - `src/core/fs/SwDir.h`, `src/core/fs/SwFileInfo.h`
  - `src/core/fs/SwStandardPaths.h`, `src/core/fs/SwStandardLocation.h`, `src/core/fs/SwStandardLocationDefs.h`
  - `src/core/fs/SwSettings.h` (settings persistants)
  - `src/core/fs/SwMutex.h`
- GUI/widgets:
  - `src/core/gui/SwGuiApplication.h` (intégration platform)
  - `src/core/gui/SwWidget.h`, `src/core/gui/SwMainWindow.h`, `src/core/gui/SwLayout.h`, `src/core/gui/SwStyle.h`, `src/core/gui/StyleSheet.h`
  - Widgets: `src/core/gui/SwLabel.h`, `src/core/gui/SwPushButton.h`, `src/core/gui/SwLineEdit.h`, `src/core/gui/SwSlider.h`, `src/core/gui/SwAbstractSlider.h`, etc.
  - Rendu vidéo: `src/core/gui/SwVideoWidget.h`, `src/core/gui/SwMediaControlWidget.h`
- HW:
  - `src/core/hw/SwPinOut.h`
  - `src/core/hw/SwSerialInfo.h`
- Backends OS (FS):
  - `src/core/platform/SwPlatform.h` + variantes `src/core/platform/SwPlatformPosix.h`, `src/core/platform/SwPlatformWin.h`, `src/core/platform/SwPlatformSelector.h`

### `src/media/` — Pipeline vidéo (sources/décodeurs)

- Types & interfaces: `src/media/SwVideoTypes.h`, `src/media/SwVideoSource.h`, `src/media/SwVideoDecoder.h`
- Impl: `src/media/SwRtspUdpSource.h`, `src/media/SwHttpMjpegSource.h`, `src/media/SwFileVideoSource.h`
- Windows MediaFoundation: `src/media/SwMediaFoundation*.h`

### `src/platform/` — Intégration GUI (Win32 / X11)

- Interfaces: `src/platform/SwPlatformIntegration.h`, `src/platform/SwPlatformFactory.h`
- Win32: `src/platform/win/*.h`
- X11: `src/platform/x11/*.h`

## `exemples/` (applications)

Chaque dossier `exemples/<NN>-<Nom>/` contient:

- un `CMakeLists.txt` (cible exécutable),
- un (ou plusieurs) `*.cpp` dont un `main()`.

Entrypoints (repère rapide, non exhaustif):

- Simple GUI: `exemples/01-SimpleWindow/GuiApplication.cpp`
- Core loop: `exemples/02-CoreApplication/ConsoleApplication.cpp`
- Sockets: `exemples/03-TcpServer/TcpServer.cpp`, `exemples/21-UdpProbe/UdpProbe.cpp`
- HTTP: `exemples/04-NetworkAccesManager/NetworkAccesManager.cpp`
- IPC/RPC: `exemples/23-ConfigurableObjectDemo/*.cpp`, `exemples/25-IpcPerfMonitor/IpcPerfMonitor.cpp`, `exemples/26-IpcThreadStress/IpcThreadStress.cpp`
- Ring buffer vidéo: `exemples/27-IpcVideoFrameRingBuffer/IpcVideoFrameRingBuffer.cpp`, `exemples/28-IpcRingBufferCameraPlayer/*.cpp`
- (ex-n°28/29) Ces deux programmes ont été extraits de `exemples/` vers `SwNode/` (outils/runtimes):
  - Container/plugins: `SwNode/SwComponentContainer/SwComponentContainer.cpp`, `exemples/24-ComponentPlugin/PingPongPlugin.cpp`
  - Launch JSON / demo nodes: `SwNode/SwLaunch/SwLaunch.cpp`, `exemples/29-IpcPingPongNodes/SwPingNode.cpp`, `exemples/29-IpcPingPongNodes/SwPongNode.cpp`
