# Build & Run

## Pré-requis

Commun:
- CMake ≥ 3.10 (`CMakeLists.txt` utilise `cmake_minimum_required(VERSION 3.10)`).
- Compilateur C++ (C++11).

Linux (et WSL):
- libcurl + headers (root `CMakeLists.txt`: `find_package(CURL REQUIRED)`).
- OpenSSL (root `CMakeLists.txt`: `find_package(OpenSSL REQUIRED)`).
- X11 (certaines cibles GUI: `find_package(X11)` dans `exemples/**/CMakeLists.txt`).

Windows:
- Visual Studio (MSVC) ou toolchain compatible.
- OpenSSL pour les exemples qui appellent `find_package(OpenSSL REQUIRED)` (`exemples/**/CMakeLists.txt`).

## Build via scripts (recommandé)

### Windows: `build.bat`

Variables utiles:
- `BUILD_DIR` (défaut: `build-win`)
- `BUILD_TYPE` (défaut: `Release`)
- `BUILD_TARGET` (optionnel: cible unique, ex: `ConfigurableObjectDemo`)
- `CMAKE_BIN` (optionnel: chemin vers `cmake`)

Exemples:
- `build.bat`
- `set BUILD_TYPE=Debug && build.bat`
- `set BUILD_TARGET=SwLaunch && build.bat`

Le script:
- configure CMake (`cmake -S . -B <builddir> ...`),
- build (`cmake --build ...`),
- scanne `builddir/exemples/**` et propose un menu pour lancer un `.exe`.

Référence: `build.bat`.

### Linux/WSL: `build.sh`

Variables utiles:
- `BUILD_DIR` (défaut: `build-wsl`)
- `BUILD_TYPE` (défaut: `Release`)
- `BUILD_TARGET` (optionnel)
- `CMAKE_BIN` (optionnel)

Exemples:
- `./build.sh`
- `BUILD_TYPE=Debug ./build.sh`
- `BUILD_TARGET=IpcPerfMonitor ./build.sh`

Le script nettoie un `CMakeCache.txt` incohérent si `CMAKE_HOME_DIRECTORY` ne correspond plus au repo.

Référence: `build.sh`.

## Build “manuel” (CMake)

Windows (PowerShell):
- `cmake -S . -B build-win -DCMAKE_BUILD_TYPE=Release`
- `cmake --build build-win --config Release`

Linux/WSL:
- `cmake -S . -B build-wsl -DCMAKE_BUILD_TYPE=Release`
- `cmake --build build-wsl --config Release`

## Exécution

Les executables sont générés sous:
- `build-*/exemples/<NN>-<Name>/<Config>/...` (Windows/MSVC)
- `build-*/exemples/<NN>-<Name>/...` (Linux selon generator)
- `build-*/SwNode/<Name>/<Config>/...` (Windows/MSVC) et `build-*/SwNode/<Name>/...` (Linux) pour les outils dans `SwNode/`

Le plus simple est d’utiliser `build.bat` / `build.sh` qui proposent un menu.

## Dépannage

- Si CMake pointe vers un mauvais dossier source:
  - supprimez `build-*/CMakeCache.txt` + `build-*/CMakeFiles/` ou utilisez le mécanisme de nettoyage de `build.sh`.
- Si OpenSSL/CURL/X11 ne sont pas trouvés:
  - installez les paquets dev correspondants et/ou ajustez `CMAKE_PREFIX_PATH`.
- `include/` n’existe pas: les includes reposent sur `src/` et `src/core/` ajoutés en include dirs dans `CMakeLists.txt`.
