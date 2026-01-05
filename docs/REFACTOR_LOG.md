# Refactor log (structure uniquement)

Date: 2025-12-26

## Résumé

- Organisation de `src/core/` par modules: `runtime/`, `object/`, `remote/`, `io/`, `types/`, `fs/`, `gui/`, `hw/`, `platform/`.
- Mise à jour des include dirs CMake pour refléter ces modules (sans changer le comportement).
- Mise à jour des includes `core/...` dans `src/platform/**`, `src/media/**` et certains exemples.
- Normalisation des "node main":
  - remplacement des usages `SW_NODE_MAIN*` par `SW_REMOTE_OBJECT_NODE` (voir `src/core/remote/SwRemoteObjectNode.h`).

## Build

- `CMakeLists.txt` (racine):
  - ajout/ajustement des include dirs `src/core/<module>` pour conserver les includes existants par nom (`#include "SwX.h"`) et permettre les includes `core/<module>/...`.

## Changements de code (sélection)

- Node macros:
  - `src/core/remote/SwRemoteObjectNode.h`: expose `SW_REMOTE_OBJECT_NODE` (1 ou 3 arguments) et garde l'impl via `SW_NODE_MAIN_WITH_DEFAULTS` (`À CONFIRMER`: si d'autres projets externes dépendaient de `SW_NODE_MAIN*`).
- Exemples:
  - `exemples/29-IpcPingPongNodes/SwPingNode.cpp`: `SW_REMOTE_OBJECT_NODE(SwPingNode)`
  - `exemples/29-IpcPingPongNodes/SwPongNode.cpp`: `SW_REMOTE_OBJECT_NODE(SwPongNode)`
  - `docs/nodes/SwLaunch.md`: doc du launch JSON (basée sur `src/core/remote/SwRemoteObjectNode.h`)
  - `exemples/21-UdpProbe/UdpProbe.cpp`: include mis à jour vers `core/io/SwUdpSocket.h`
- Dépendances internes:
  - `src/platform/SwPlatformIntegration.h`, `src/platform/x11/SwX11Painter.h`: `core/types/SwString.h`
  - `src/media/SwRtspUdpSource.h`, `src/media/SwHttpMjpegSource.h`: `core/io/SwTcpSocket.h` / `core/io/SwUdpSocket.h`
  - `src/media/SwVideoPacket.h`: `core/types/SwByteArray.h`

## Déplacements d'artefacts (doc/refs)

- `src/core/Doxyfile` -> `docs/doxygen/Doxyfile`
- `src/core/qserialport_ref.txt` -> `docs/refs/qserialport_ref.txt`
- Divers fichiers legacy/backups -> `docs/legacy/**`

## Points d'attention

- Le repo est majoritairement header-only: attention aux collisions de noms si plusieurs headers portent le même basename, car plusieurs include dirs sont ajoutés globalement.
- `SwBackendSsl` (TLS) contient un comportement de vérification de certificat `À CONFIRMER` et doit être encadré si usage prod (voir `src/core/io/SwBackendSsl.h`).
