# Documentation `coreSwExample`

Ce dossier documente le dépôt **tel qu’il est implémenté dans le code** (scan statique).

- Règle d’or: chaque affirmation importante doit être traçable à un **fichier** et idéalement un **symbole** (classe / fonction).
- Quand un point n’est pas certain, il est noté `À CONFIRMER` et on pointe vers l’endroit à vérifier.

## Comment naviguer (2 minutes)

1. Lire `docs/ARCHITECTURE.md` pour la vue d’ensemble (composants, runtime, flux majeurs).
2. Ouvrir `docs/CODEMAP.md` pour la cartographie dossiers ↔ responsabilités ↔ fichiers clés.
3. Parcourir `docs/features/00_feature_index.md` puis les fiches de fonctionnalités.
4. Consulter `docs/BUILD_AND_RUN.md` et `docs/DEBUGGING.md` pour l’opérationnel.

## Sommaire

- Vue d’ensemble: `docs/ARCHITECTURE.md`
- Cartographie code: `docs/CODEMAP.md`
- Index des fonctionnalités: `docs/features/00_feature_index.md`
- Nodes/outils: `docs/nodes/SwLaunch.md`, `docs/nodes/SwComponentContainer.md`
- Référence API: `docs/PUBLIC_API.md`
- Build & run: `docs/BUILD_AND_RUN.md`
- Debug: `docs/DEBUGGING.md`
- Décisions/mini-ADR: `docs/DECISIONS.md`
- Organisation des sources: `docs/SOURCE_LAYOUT.md`
- Journal des déplacements: `docs/REFACTOR_LOG.md`

## Notes (état actuel du dépôt)

- Build system: CMake (`CMakeLists.txt` à la racine, + un `CMakeLists.txt` par exemple dans `exemples/**`).
- La lib est essentiellement header-only: les exemples incluent `src/**` via les include dirs CMake.
- Il n’y a pas de `include/` dans ce dépôt: l’API consommée par les exemples est dans `src/core/**` (modules `runtime/`, `object/`, `remote/`, `io/`, `types/`, `fs/`, `gui/`, `hw/`, `platform/`), `src/media/*.h`, `src/platform/*.h`, `src/atomic/thread.h`.
