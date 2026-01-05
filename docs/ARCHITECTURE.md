# Architecture (vue d’ensemble)

## Vue “composants”

Le dépôt est construit autour de 3 piliers:

1. **Runtime coopératif** (event loop + timers + fibres) pour exécuter des tâches sans bloquer le thread.
2. **Modèle objet** façon Qt (signaux/slots, propriétés, affinité thread) pour structurer l’app.
3. **IPC same machine** via mémoire partagée + wakeups OS, pour pub/sub et RPC inter-process.

Les couches GUI et Media se greffent au runtime.

```mermaid
flowchart LR
  subgraph R[Runtime]
    A[SwCoreApplication\n(src/core/runtime/SwCoreApplication.h)]
    EL[SwEventLoop\n(src/core/runtime/SwEventLoop.h)]
    T[SwTimer\n(src/core/runtime/SwTimer.h)]
    TH[Threads\n(src/atomic/thread.h + src/core/runtime/SwThread.h)]
  end

  subgraph O[Modèle objet]
    OBJ[SwObject\n(src/core/object/SwObject.h)]
    MT[Meta/Any\n(src/core/object/SwMetaType.h + src/core/types/SwAny.h)]
  end

  subgraph IPC[IPC (same machine)]
    SHM[Signal/pubsub\n(src/core/remote/SwSharedMemorySignal.h)]
    RPC[RPC\n(src/core/remote/SwIpcRpc.h)]
    RB[NoCopyRingBuffer\n(src/core/remote/SwIpcNoCopyRingBuffer.h)]
    REM[Remote/Discovery\n(src/core/remote/SwIpcRemote*.h)]
  end

  subgraph CFG[Config/Nodes]
    RO[SwRemoteObject\n(src/core/remote/SwRemoteObject.h)]
    NODE[Node main macro\n(src/core/remote/SwRemoteObjectNode.h)]
    SC[systemConfig/**]
  end

  subgraph IO[IO réseau]
    SOCK[TCP/UDP\n(src/core/io/Sw*Socket.h)]
    HTTP[HTTP GET\n(src/core/io/SwNetworkAccessManager.h)]
    TLS[TLS\n(src/core/io/SwBackendSsl.h)]
  end

  subgraph GUI[GUI]
    GA[SwGuiApplication\n(src/core/gui/SwGuiApplication.h)]
    PI[PlatformIntegration\n(src/platform/SwPlatformIntegration.h)]
    W[Widgets\n(src/core/gui/SwWidget.h + ...)]
  end

  subgraph MEDIA[Media]
    VS[VideoSource\n(src/media/SwVideoSource.h)]
    VD[VideoDecoder\n(src/media/SwVideoDecoder.h)]
    RTSP[RTSP/UDP\n(src/media/SwRtspUdpSource.h)]
  end

  A --> EL
  A --> T
  A --> TH
  A --> OBJ

  OBJ --> MT
  A --> SHM --> RPC
  SHM --> RB
  SHM --> REM

  RO --> SHM
  NODE --> RO
  RO --> SC

  A --> SOCK --> HTTP
  SOCK --> TLS

  GA --> A
  GA --> PI
  W --> PI
  W --> OBJ
  MEDIA --> A
```

## Runtime: boucle d’événements + fibres

### Intention (Pourquoi)

Le runtime cherche à:

- exécuter des callbacks **sans bloquer** (approche coopérative),
- gérer **timers** et **wakeups**,
- permettre des “attentes” locales (nested event loop) sans arrêter toute l’app.

### Références (où)

- `src/core/runtime/SwCoreApplication.h` (`exec`, `processEvent`, `yieldFiber`, `waitForWork_`)
- `src/core/runtime/SwEventLoop.h` (`SwEventLoop::exec`, macros `swhile/tswhile`)
- `src/core/runtime/SwTimer.h` (`SwTimer`, `SwTimer::singleShot`)
- Fibres Linux: `src/core/runtime/linux_fiber.h` (`À CONFIRMER`: portabilité/maturité selon plateformes)

## Modèle objet: `SwObject`, signaux/slots, affinité thread

### Intention

Fournir une ossature proche de Qt:

- hiérarchie parent/enfant, lifecycle, `destroyed()` etc,
- signaux/slots avec déconnexion,
- thread affinity + migration `moveToThread` vers un event loop dédié.

### Références

- `src/core/object/SwObject.h` (macros `SW_OBJECT`, `DECLARE_SIGNAL`, `connect`, `disconnect`, `moveToThread`)
- `src/core/runtime/SwThread.h` (pont `SwThread` ↔ `sw::atomic::Thread`)
- `src/atomic/thread.h` (`sw::atomic::Thread`: thread worker + son `SwCoreApplication`)

## IPC: mémoire partagée + notifications OS

### Intention

Permettre pub/sub et RPC **entre processus** sur la même machine, avec:

- transport “latest value wins” pour signaux d’état,
- wakeup event-driven (éviter polling),
- introspection “best-effort” via registries partagés.

### Références

- `src/core/remote/SwSharedMemorySignal.h` (`Registry`, `LoopPoller`, registries `RegistryTable`…)
- `src/core/remote/SwIpcRpc.h` (`RpcMethodClient`, queues `__rpc__|...` / `__rpc_ret__|...`)
- `src/core/remote/SwIpcNoCopyRingBuffer.h` (`NoCopyRingBuffer`, `ShmMappingDyn`)

## Config & “nodes”

### Config multi-couches

`SwRemoteObject` charge une config JSON en surcouches et peut publier/recevoir des updates via IPC:

- `systemConfig/global/<objectName>.json`
- `systemConfig/local/<nameSpace>_<objectName>.json`
- `systemConfig/user/<nameSpace>_<objectName>.json`

Référence: `src/core/remote/SwRemoteObject.h` (commentaire “Load order”, `ConfigPaths`, `setConfigValue`).

### Pattern “node main”

`src/core/remote/SwRemoteObjectNode.h` fournit `SW_REMOTE_OBJECT_NODE` pour générer un `main()` standard:

- parsing args (`--sys`, `--ns`, `--name`, `--duration_ms`, `--config_file`, `--config_json`, …),
- application des params JSON sur la config (`SwRemoteObject::setConfigValue`),
- watchdog optionnel (`SwCoreApplication::activeWatchDog`),
- auto-quit via `SwTimer::singleShot`.

## GUI: intégration platform + widgets

`SwGuiApplication` hérite de `SwCoreApplication` et ajoute une pompe d’événements platform.

Références:

- `src/core/gui/SwGuiApplication.h` (`exec`, `platformIntegration()`)
- `src/platform/SwPlatformIntegration.h` (interfaces `SwPlatformWindow/Painter/Image`)
- `src/platform/SwPlatformFactory.h` (sélection win/x11)
- Widgets: `src/core/gui/SwWidget.h`, `src/core/gui/SwMainWindow.h`, `src/core/gui/SwLayout.h`, etc.

## Media: sources + décodage + rendu

Références:

- Types: `src/media/SwVideoTypes.h`, `src/media/SwVideoFrame.h`, `src/media/SwVideoPacket.h`
- Sources: `src/media/SwVideoSource.h`, `src/media/SwRtspUdpSource.h`, `src/media/SwHttpMjpegSource.h`, `src/media/SwFileVideoSource.h`
- Décodage: `src/media/SwVideoDecoder.h`, `src/media/SwMediaFoundationH264Decoder.h` (`À CONFIRMER`: backends Linux)
- Rendu widget: `src/core/gui/SwVideoWidget.h`
