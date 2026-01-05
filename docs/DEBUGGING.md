# Debugging & observabilité

## Logs: `SwDebug` (local + remote)

### API

- Macros:
  - `swDebug()`, `swWarning()`, `swError()` (retournent un `SwDebugMessage` streamable)
- Métadonnées:
  - `SW_DEBUG_INCLUDE_METAINFOS` (timestamp)
  - `SW_DEBUG_INCLUDE_SOURCE` (file/line/function)
- Paramètres:
  - `SwDebug::setAppName(name)`
  - `SwDebug::setVersion(version)`
  - `SwDebug::setRemoteEndpoint(host, port)` (envoi des logs en TCP)

Référence: `src/core/types/SwDebug.h`.

### Format

Chaque log construit un objet JSON (type=log, level, appName, version, message, …) puis:

- l’écrit sur stderr (préfixes `[DEBUG]/[WARNING]/[ERROR]`),
- et optionnellement l’envoie sur un endpoint TCP (une ligne JSON terminée par `\n`).

Référence: `src/core/types/SwDebug.h` (`SwDebug::logMessage`).

### Exemple: client + serveur de logs

- Client: `exemples/06-DebugExemple/DebugExemple.cpp`
  - usage: `DebugExemple <host> <port>`
- Serveur: `exemples/07-ServeurDebug/ServeurDebug.cpp`
  - écoute sur `12345` et print ce qu’il reçoit.

## Runtime: charge et watchdog

### Charge event loop

`SwCoreApplication` expose:

- `getLoadPercentage()`
- `getLastSecondLoadPercentage()`

Référence: `src/core/runtime/SwCoreApplication.h`.

### Watchdog (Windows)

Optionnel:
- `SwCoreApplication::activeWatchDog()` / `desactiveWatchDog()`
- détecte une fibre bloquante et tente de forcer un retour au main fiber.

Référence: `src/core/runtime/SwCoreApplication.h` (`watchdogLoop`, `forceBackToMainFiber`)  
`À CONFIRMER`: safety/impact sur le thread (manipulation de contexte Win32).

## IPC: introspection best-effort

`SwSharedMemorySignal` maintient des registries SHM best-effort:

- registry apps (domain alive),
- registry signaux (domain/object/signal/type),
- registry subscribers.

C’est la base de discovery (`SwIpcRemote.h`) et d’outils (ex: console web).

Références:
- `src/core/remote/SwSharedMemorySignal.h` (RegistryTable/App registry)
- `src/core/remote/SwIpcRemote.h` (`discoverRpcTargets`, `functions`, `argType`)

## Reproduction d’un bug de “deadlock” typique

Cas fréquent:
- `BlockingQueuedConnection` vers un thread dont l’event loop ne tourne plus → blocage.

Check-list:
- vérifier que `SwThread` a bien démarré (`isRunning()`),
- vérifier que l’event loop du thread tourne (`sw::atomic::Thread` crée un `SwCoreApplication` et exécute `exec()`),
- préférer `QueuedConnection` si possible.

Références:
- `src/core/object/SwObject.h` (`BlockingQueuedConnection`, `executeBlockingOnThread`)
- `src/core/runtime/SwThread.h`, `src/atomic/thread.h`
