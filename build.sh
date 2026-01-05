#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_BUILD_DIR="$ROOT_DIR/build-wsl"
BUILD_DIR="${BUILD_DIR:-$DEFAULT_BUILD_DIR}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
CMAKE_BIN="${CMAKE_BIN:-cmake}"
BUILD_TARGET="${BUILD_TARGET:-}"

if ! command -v "$CMAKE_BIN" >/dev/null 2>&1; then
    echo "Erreur: impossible de trouver l'executable CMake \"$CMAKE_BIN\". Ajustez la variable d'environnement CMAKE_BIN."
    exit 1
fi

mkdir -p "$BUILD_DIR"

# If a previous CMake cache points to another source tree, wipe it to avoid mismatched-source errors.
CMAKE_CACHE="$BUILD_DIR/CMakeCache.txt"
if [[ -f "$CMAKE_CACHE" ]]; then
    CACHE_HOME="$(grep -E '^CMAKE_HOME_DIRECTORY:INTERNAL=' "$CMAKE_CACHE" | cut -d= -f2- || true)"
    if [[ -n "$CACHE_HOME" && "$CACHE_HOME" != "$ROOT_DIR" ]]; then
        echo "[Info] Nettoyage du cache CMake (ancien projet: $CACHE_HOME)"
        rm -rf "$BUILD_DIR/CMakeCache.txt" "$BUILD_DIR/CMakeFiles"
    fi
fi

echo "[Info] Build directory: $BUILD_DIR"
echo "[CMake] Configuration..."
"$CMAKE_BIN" -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

echo "[CMake] Compilation $BUILD_TYPE..."
if [[ -n "$BUILD_TARGET" ]]; then
    echo "[CMake] Target: $BUILD_TARGET"
    "$CMAKE_BIN" --build "$BUILD_DIR" --config "$BUILD_TYPE" --target "$BUILD_TARGET"
else
    "$CMAKE_BIN" --build "$BUILD_DIR" --config "$BUILD_TYPE"
fi

declare -a EXEC_PATHS=()
declare -a EXEC_NAMES=()
declare -A EXEC_SEEN=()

scan_dir() {
    local dir="$1"
    [[ -d "$dir" ]] || return 0
    while IFS= read -r -d '' exe; do
        [[ -n "${EXEC_SEEN["$exe"]:-}" ]] && continue
        EXEC_SEEN["$exe"]=1
        EXEC_PATHS+=("$exe")
        EXEC_NAMES+=("$(basename "$exe")")
    done < <(find "$dir" -maxdepth 1 -type f \( -name "*.exe" -o -perm -u+x \) \
             ! -name "Makefile" ! -name "cmake_install.cmake" ! -name "*.cmake" -print0 | sort -z)
}

EXAMPLES_DIR="$BUILD_DIR/exemples"
if [[ -d "$EXAMPLES_DIR" ]]; then
    for example in "$EXAMPLES_DIR"/*; do
        [[ -d "$example" ]] || continue
        scan_dir "$example/$BUILD_TYPE"
        scan_dir "$example/Release"
        scan_dir "$example"
    done
fi

if [[ "${#EXEC_PATHS[@]}" -eq 0 ]]; then
    echo "Aucun executable trouve dans \"$EXAMPLES_DIR\"."
    exit 0
fi

while true; do
    echo "=============================================="
    echo "  Executables disponibles ($BUILD_TYPE)"
    echo "=============================================="
    for i in "${!EXEC_PATHS[@]}"; do
        idx=$((i + 1))
        echo "  $idx) ${EXEC_NAMES[$i]}  [${EXEC_PATHS[$i]}]"
    done
    echo "  q) Quitter"
    read -rp "Selectionner un executable (1-${#EXEC_PATHS[@]} ou q) : " choice
    case "$choice" in
        q|Q|"")
            exit 0
            ;;
        *)
            if [[ "$choice" =~ ^[0-9]+$ ]] && (( choice >= 1 && choice <= ${#EXEC_PATHS[@]} )); then
                target="${EXEC_PATHS[$((choice - 1))]}"
                echo
                echo "[RUN] $target"
                "$target"
                echo
                read -rp "Appuyez sur Entree pour revenir au menu..." _
            else
                echo "Choix invalide."
            fi
            ;;
    esac
done
