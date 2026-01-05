# Référence API “publique” (dans ce dépôt)

## Constat (structure actuelle)

- Le dépôt **ne contient pas** de répertoire `include/`.
- Les exemples consomment directement des headers dans `src/**` via les include dirs CMake définis dans `CMakeLists.txt`.
- Conséquence: l’API “publique” *de fait* correspond aux headers utilisés (ou réutilisables) depuis `exemples/**`.

## Règles d’inclusion recommandées

- Inclure des `*.h` (ex: `#include "SwCoreApplication.h"`).
- Préférer les chemins “module” quand c’est plus clair depuis l’extérieur:
  - `#include "core/remote/SwRemoteObject.h"`
  - `#include "core/io/SwTcpSocket.h"`
  - `#include "core/types/SwJsonDocument.h"`
  - `#include "media/SwVideoSource.h"`
  - `#include "platform/SwPlatformIntegration.h"`

`À CONFIRMER`: il n’y a pas de politique explicite de compat (stabilité/semver) visible dans le code; en pratique les headers réutilisés par plusieurs exemples (runtime/remote/object model) sont les plus “stables”.

## Modules et headers clés

### Runtime / scheduling

- `src/core/runtime/SwCoreApplication.h` (event loop + fibres + timers + waitables OS)
- `src/core/runtime/SwEventLoop.h` (nested loop)
- `src/core/runtime/SwTimer.h`
- `src/core/runtime/SwThread.h` et `src/atomic/thread.h` (threads + event loop)
- CLI: `src/core/runtime/SwCommandLineParser.h`, `src/core/runtime/SwCommandLineOption.h`

### Modèle objet / signaux/slots

- `src/core/object/SwObject.h` (signaux/slots/propriétés, `moveToThread`, `deleteLater`)
- `src/core/object/SwPointer.h` (pointeur faible)
- `src/core/object/SwMetaType.h`, `src/core/types/SwAny.h` (`À CONFIRMER`: usages exacts)

### IPC local machine (remote)

- `src/core/remote/SwSharedMemorySignal.h` (pub/sub SHM + registries + wakeups)
- `src/core/remote/SwIpcRpc.h` (client/infra RPC)
- `src/core/remote/SwProxyObject.h`, `src/core/remote/SwProxyObjectBrowser.h` (discovery/proxy objects)
- `src/core/remote/SwIpcNoCopyRingBuffer.h` (ring buffer SHM no-copy)
- Container/plugins:
  - `SwNode/SwComponentContainer/SwComponentContainer.cpp`
  - `src/core/remote/SwRemoteObjectComponent.h`
  - `src/core/remote/SwRemoteObjectComponentRegistry.h`

### Config & nodes

- `src/core/remote/SwRemoteObject.h` (config multi-couches + IPC config + helpers RPC)
- `src/core/remote/SwRemoteObjectNode.h` (macro `SW_REMOTE_OBJECT_NODE` pour générer un `main()` standard piloté par JSON/args)
- Données: `systemConfig/**`

### IO (réseau + fichiers/process)

- Sockets:
  - `src/core/io/SwAbstractSocket.h`
  - `src/core/io/SwTcpSocket.h`, `src/core/io/SwTcpServer.h`, `src/core/io/SwUdpSocket.h`
- HTTP:
  - `src/core/io/SwNetworkAccessManager.h` (HTTP/HTTPS GET async)
- TLS backend:
  - `src/core/io/SwBackendSsl.h` (OpenSSL dynamique; attention vérification certif potentiellement désactivée)
- Fichiers/process:
  - `src/core/io/SwFile.h`, `src/core/io/SwIODevice.h`, `src/core/io/SwIODescriptor.h`
  - `src/core/io/SwProcess.h`
- Série:
  - `src/core/io/SwSerial.h` + `src/core/hw/SwSerialInfo.h`

### Types & sérialisation

- `src/core/types/SwString.h`, `src/core/types/SwByteArray.h`
- JSON: `src/core/types/SwJsonDocument.h`, `src/core/types/SwJsonObject.h`, `src/core/types/SwJsonArray.h`, `src/core/types/SwJsonValue.h`
- Regex: `src/core/types/SwRegularExpression.h`
- Debug/log: `src/core/types/SwDebug.h`

### GUI (widgets + intégration OS)

- `src/core/gui/SwGuiApplication.h`
- `src/platform/SwPlatformIntegration.h`, `src/platform/SwPlatformFactory.h`
- Widgets: `src/core/gui/SwWidget.h`, `src/core/gui/SwMainWindow.h`, `src/core/gui/SwLayout.h`, `src/core/gui/SwStyle.h`, `src/core/gui/StyleSheet.h`

### Media

- Types: `src/media/SwVideoTypes.h`, `src/media/SwVideoPacket.h`, `src/media/SwVideoFrame.h`
- Pipeline: `src/media/SwVideoSource.h`, `src/media/SwVideoDecoder.h`
- Widget vidéo: `src/core/gui/SwVideoWidget.h`
