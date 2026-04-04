# Organisation des sources (layout)

## Objectifs

- Rendre `src/` navigable par modules plutot que par un dossier plat.
- Garder une refactorisation structurelle (dossiers/includes/CMake) sans changer la logique.

## Arborescence `src/core/`

- `src/core/runtime/`
  - Event loop, fibres, timers, threads haut niveau, CLI, chargement de librairies.
- `src/core/object/`
  - `SwObject`, meta-objets, signaux/slots, lifecycle.
- `src/core/remote/`
  - IPC SHM, RPC, discovery/remotes, config IPC, nodes, plugins/container.
- `src/core/io/`
  - IO reseau (TCP/UDP/TLS/HTTP), fichiers/process/serial.
- `src/core/types/`
  - Types utilitaires (string/bytearray/containers/json/regex/debug/date/crypto).
- `src/core/fs/`
  - Dir/FileInfo, paths, settings, locations.
- `src/core/gui/`
  - `SwGuiApplication` et widgets.
- `src/core/hw/`
  - Helpers hardware.
- `src/core/platform/`
  - Backends Win/POSIX et primitives bas niveau, y compris IO dediee DB.
- `src/core/storage/`
  - Stockage embarque local: `SwEmbeddedDb`, WAL, SSTables, blobs, snapshots et scans d'index.

## Ou mettre un nouveau fichier ?

- Runtime event loop/timers/threads/CLI: `src/core/runtime/`
- Signaux/slots, lifecycle, meta objet: `src/core/object/`
- SHM/RPC/discovery/plugins/container/config remote: `src/core/remote/`
- TCP/UDP/TLS/HTTP: `src/core/io/`
- IO fichiers/process/serial: `src/core/io/`
- String/containers/JSON/regex/debug/crypto/date: `src/core/types/`
- Dir/FileInfo/paths/settings/locations: `src/core/fs/`
- Widgets/layout/style/render: `src/core/gui/`
- Hardware helpers: `src/core/hw/`
- Backends OS FS, mmap, verrous de fichiers, replace atomique: `src/core/platform/`
- Base embarquee locale et stockage LSM/blob: `src/core/storage/`
- Plateforme GUI (Win32/X11): `src/platform/`
- Media: `src/media/`

## Includes et build

- Les include dirs CMake sont centralises dans `CMakeLists.txt` a la racine.
- Garder des noms de headers uniques car plusieurs repertoires sont exposes via `include_directories(...)`.
- Le module `src/core/storage/` suit la meme regle que les autres modules `core/*` et est expose au build racine.
