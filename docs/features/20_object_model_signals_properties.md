# Modèle objet: `SwObject`, signaux/slots, propriétés, thread affinity

## 1) But (Pourquoi)

Fournir une ossature inspirée de Qt pour:

- structurer le code autour d’objets avec lifecycle parent/enfant,
- connecter des signaux/slots (callbacks) avec déconnexion,
- gérer l’affinité thread et les dispatch “queued” via l’event loop.

## 2) Périmètre

Inclut:
- `SwObject` (lifecycle, parent/child, `deleteLater`),
- signaux/slots + types de connexion,
- propriétés (API “property” `À CONFIRMER` détail),
- thread affinity (`moveToThread`, `threadHandle`).

Exclut:
- runtime internals (fibres, wait) documentés ailleurs,
- IPC/remote documenté ailleurs.

## 3) API & concepts

### `SwObject`

Concepts visibles (liste non exhaustive `À CONFIRMER`):
- parent/child, destructeur, `destroyed`,
- `deleteLater()` (planification via event loop),
- connexions:
  - `QueuedConnection`
  - `BlockingQueuedConnection` (attention deadlocks)
  - `DirectConnection` (`À CONFIRMER` si présent)
- thread:
  - `moveToThread(...)`
  - exécution sur thread: `executeOnThread` / `executeBlockingOnThread` (`À CONFIRMER` API exacte)

Référence: `src/core/object/SwObject.h`.

### Meta / any / pointers

- `SwPointer`: pointeur “faible” (évite déréférencement après destruction).
  - Référence: `src/core/object/SwPointer.h`
- `SwMetaType`: meta-types (enregistrement, introspection `À CONFIRMER`).
  - Référence: `src/core/object/SwMetaType.h`
- `SwAny`: “variant/any” typé.
  - Référence: `src/core/types/SwAny.h`

## 4) Flux d’exécution (Comment)

### Connexion “queued” (résumé)

```mermaid
sequenceDiagram
  participant Emit as Emitter thread
  participant Loop as Receiver event loop
  participant Slot as Receiver slot

  Emit->>Loop: postEvent(callback)
  Loop->>Slot: invoke slot
```

`À CONFIRMER`: impl exacte des “events”/dispatch utilisés par `SwObject` (types internes, capture args, etc).

### moveToThread (résumé)

```mermaid
flowchart TD
  A[SwObject] --> B[moveToThread(target)]
  B --> C[update thread handle]
  C --> D[queued connections go to target loop]
```

## 5) Gestion d’erreurs

- Connexions:
  - les connect/disconnect peuvent échouer si signatures incompatibles (`À CONFIRMER` mode de report).
- BlockingQueuedConnection:
  - deadlock si le receiver thread ne process plus l’event loop.

## 6) Perf & mémoire

- Queued connections: allocation/capture des args + post dans la queue d’events.
- BlockingQueuedConnection: latence + risque de contention.
- Propriétés: mise à jour peut déclencher callbacks; attention aux storms (`À CONFIRMER` throttling).

## 7) Fichiers concernés (liste + rôle)

- `src/core/object/SwObject.h`
- `src/core/object/SwPointer.h`
- `src/core/object/SwMetaType.h`
- `src/core/types/SwAny.h`

Exemples:
- `exemples/13-ObjectLifecycleTest/ObjectLifecycleTest.cpp` (lifecycle)
- `exemples/08-RegisterType/RegisterType.cpp` (`À CONFIRMER`: meta-type)
- `exemples/05-InteractiveConsoleApplication/InteractiveConsoleApplication.cpp` (callbacks/UI)

## 8) Exemples d’usage

```cpp
SwObject a;
SwObject b;
SwObject::connect(&a, SIGNAL(destroyed), [&](){ /* ... */ });
(void)&b;
```

`À CONFIRMER`: signature exacte des macros `SIGNAL(...)` et de l’API `connect` selon la version du header.

## 9) TODO / À CONFIRMER

- `À CONFIRMER`: matrice exacte des types de connexion et leurs garanties (ordre, thread safety).
- `À CONFIRMER`: API propriétés (notification, stockage, binding).
