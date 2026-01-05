# Organisation des sources (layout)

## Objectifs

- Rendre `src/` navigable par modules plutôt que par un dossier plat.
- Garder une refactorisation **structurelle** (dossiers/includes/CMake) sans changement de logique.

## Arborescence `src/core/` (actuelle)

- `src/core/runtime/`
  - Event loop, fibres, timers, threads haut niveau, CLI, chargement de librairies.
- `src/core/object/`
  - `SwObject` + pointeurs/meta “Qt-like”.
- `src/core/remote/`
  - IPC SHM, RPC, discovery/remotes, config IPC (`SwRemoteObject`), nodes (`SwRemoteObjectNode`), plugins/container.
- `src/core/io/`
  - IO réseau (TCP/UDP/TLS/HTTP), fichiers/process/serial (wrappers).
- `src/core/types/`
  - Types utilitaires (string/bytearray/containers/json/regex/debug/date/crypto…).
- `src/core/fs/`
  - Dir/FileInfo, paths, settings, locations.
- `src/core/gui/`
  - `SwGuiApplication` et widgets (layout/style/painter/controls).
- `src/core/hw/`
  - Helpers HW (pinout, infos série).
- `src/core/platform/`
  - Backends Win/POSIX pour l’accès filesystem (à ne pas confondre avec `src/platform/` qui est la GUI).

## Oû mettre un nouveau fichier ?

Règles simples:

- Runtime event loop/timers/threads/CLI: `src/core/runtime/`
- Signaux/slots, lifecycle, meta objet: `src/core/object/`
- SHM/RPC/discovery/plugins/container/config remote: `src/core/remote/`
- TCP/UDP/TLS/HTTP: `src/core/io/`
- IO fichiers/process/serial: `src/core/io/`
- String/containers/JSON/regex/debug/crypto/date: `src/core/types/`
- Dir/FileInfo/paths/settings/locations: `src/core/fs/`
- Widgets/layout/style/render: `src/core/gui/`
- Hardware helpers: `src/core/hw/`
- Backends OS FS (POSIX/Win): `src/core/platform/`
- Plateforme GUI (Win32/X11): `src/platform/`
- Media: `src/media/`

## Includes et build

Le repo est majoritairement header-only:

- les include dirs CMake sont centralisés dans `CMakeLists.txt` (racine),
- garder des noms de headers uniques (éviter collisions) car plusieurs répertoires sont ajoutés à `include_directories(...)`.
