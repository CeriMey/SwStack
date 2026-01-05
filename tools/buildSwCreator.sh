#!/usr/bin/env bash

set -euo pipefail

# ------------------------------------------------------------------------------
# Build SwCreator (Linux/WSL) and copy to <repo>/bin
# ------------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-wsl}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
CMAKE_BIN="${CMAKE_BIN:-cmake}"

if ! command -v "$CMAKE_BIN" >/dev/null 2>&1; then
  echo "Erreur: impossible de trouver l'executable CMake \"$CMAKE_BIN\". Ajustez CMAKE_BIN."
  exit 1
fi

mkdir -p "$BUILD_DIR"

echo "[Info] Root: $ROOT_DIR"
echo "[Info] Build: $BUILD_DIR ($BUILD_TYPE)"

echo "[CMake] Configuration..."
"$CMAKE_BIN" -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

echo "[CMake] Build target: SwCreator"
"$CMAKE_BIN" --build "$BUILD_DIR" --config "$BUILD_TYPE" --target SwCreator

EXE=""
declare -a CANDIDATES=(
  "$BUILD_DIR/SwCreator/$BUILD_TYPE/SwCreator"
  "$BUILD_DIR/SwCreator/SwCreator"
  "$BUILD_DIR/bin/$BUILD_TYPE/SwCreator"
  "$BUILD_DIR/bin/SwCreator"
)

for c in "${CANDIDATES[@]}"; do
  if [[ -f "$c" ]]; then
    EXE="$c"
    break
  fi
done

if [[ -z "$EXE" ]]; then
  EXE="$(find "$BUILD_DIR" -type f -name SwCreator -perm -u+x 2>/dev/null | head -n 1 || true)"
fi

if [[ -z "$EXE" ]]; then
  echo "[Erreur] Binaire SwCreator introuvable dans \"$BUILD_DIR\"."
  exit 1
fi

BIN_DIR="$ROOT_DIR/bin"
mkdir -p "$BIN_DIR"

cp -f "$EXE" "$BIN_DIR/SwCreator"
chmod +x "$BIN_DIR/SwCreator" || true

echo "[OK] Copie: $BIN_DIR/SwCreator"

