# Décisions & notes (mini-ADR)

Cette page regroupe des décisions visibles dans le code (ou fortement suggérées) et leurs impacts.

> Règle: si un point n’est pas certain, marquer `À CONFIRMER` et pointer le code.

## 1) “Core” majoritairement header-only

**Observation**
- La majorité des composants sont implémentés dans des headers (`src/**`).

**Pourquoi (probable)**
- Simplifier l’intégration dans les exemples et réduire le besoin de packaging de librairies.
- Permettre une compilation “à la demande” par target.

**Impacts**
- Temps de compilation potentiellement plus élevé si beaucoup d’unités incluent les mêmes headers.
- Attention aux ODR et aux symboles `static`/`inline`.

**Références**
- `CMakeLists.txt` (structure build)
- `src/core/remote/SwSharedMemorySignal.h` (commentaires “Why: core is header-only…” `À CONFIRMER` selon version exacte)

## 2) IPC par mémoire partagée + wakeups OS (plutôt que TCP)

**Observation**
- Le repo implémente pub/sub et RPC inter-process sur la même machine via SHM + notifications OS.

**Pourquoi (probable)**
- Latence faible et overhead minimal (éviter kernel networking stack).
- Partage de buffers sans copies (ring buffer no-copy).

**Impacts**
- Complexité de synchronisation (multi-process) et gestion du lifecycle (registries “best-effort”).
- Débogage plus difficile qu’un transport texte.

**Références**
- `src/core/remote/SwSharedMemorySignal.h` (`Signal`, `RingQueue`, `LoopPoller`, `IpcWakeup`)
- `src/core/remote/SwIpcNoCopyRingBuffer.h` (transport no-copy)

## 3) “Discovery” best-effort via registries SHM

**Observation**
- Le code maintient des registries (apps, signaux, subscribers) pour permettre discovery/introspection.

**Pourquoi (probable)**
- Supporter des outils (console web, launcher, superviseur) sans dépendre d’un service central.

**Impacts**
- Incohérences possibles (process crash, entrées stale) ⇒ code “best-effort”.

**Références**
- `src/core/remote/SwSharedMemorySignal.h` (tables registry, snapshots)
- `src/core/remote/SwProxyObject.h` (introspection: `functions`, `argType`, discovery)

## 4) TLS via backend OpenSSL dynamique (et/ou SChannel Windows)

**Observation**
- Un backend TLS OpenSSL est chargé dynamiquement (évite un linkage strict à OpenSSL).

**Impacts**
- Premier usage plus coûteux (LoadLibrary/dlopen).
- Risque de comportement variable selon la présence des DLL.

**Point sécurité**
- Le code mentionne une vérification de certificat désactivée pour débloquer la connectivité `À CONFIRMER` (risque MITM).

**Références**
- `src/core/io/SwBackendSsl.h`
