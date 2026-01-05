#!/usr/bin/env bash

set -euo pipefail

# ------------------------------------------------------------------------------
# Build SwNode utilities (Linux/WSL) and copy to <repo>/bin
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

targets=(SwBridge swapi SwLaunch SwComponentContainer SwBuild)
for t in "${targets[@]}"; do
  echo "[CMake] Build target: $t"
  "$CMAKE_BIN" --build "$BUILD_DIR" --config "$BUILD_TYPE" --target "$t"
done

find_exe() {
  local name="$1"
  local rel_dir="$2"
  local exe=""

  local candidates=(
    "$BUILD_DIR/$rel_dir/$BUILD_TYPE/$name"
    "$BUILD_DIR/$rel_dir/$name"
    "$BUILD_DIR/bin/$BUILD_TYPE/$name"
    "$BUILD_DIR/bin/$name"
    "$BUILD_DIR/$BUILD_TYPE/$name"
    "$BUILD_DIR/$name"
  )

  for c in "${candidates[@]}"; do
    if [[ -f "$c" ]]; then
      exe="$c"
      break
    fi
  done

  if [[ -z "$exe" && -d "$BUILD_DIR/$rel_dir" ]]; then
    exe="$(find "$BUILD_DIR/$rel_dir" -type f -name "$name" -perm -u+x 2>/dev/null | head -n 1 || true)"
  fi
  if [[ -z "$exe" ]]; then
    exe="$(find "$BUILD_DIR" -type f -name "$name" -perm -u+x 2>/dev/null | head -n 1 || true)"
  fi

  echo "$exe"
}

BIN_DIR="$ROOT_DIR/bin"
mkdir -p "$BIN_DIR"

copy_one() {
  local name="$1"
  local rel_dir="$2"

  local exe_path
  exe_path="$(find_exe "$name" "$rel_dir")"
  if [[ -z "$exe_path" ]]; then
    echo "[Erreur] Binaire $name introuvable dans \"$BUILD_DIR\"."
    exit 1
  fi

  cp -f "$exe_path" "$BIN_DIR/$name"
  chmod +x "$BIN_DIR/$name" || true
  echo "[OK] Copie: $BIN_DIR/$name"

  if [[ "$name" == "SwBridge" ]]; then
    local exe_dir
    exe_dir="$(cd "$(dirname "$exe_path")" && pwd)"

    if [[ -f "$exe_dir/index.html" ]]; then
      cp -f "$exe_dir/index.html" "$BIN_DIR/index.html"
    fi
    if [[ -f "$exe_dir/css/style.css" ]]; then
      mkdir -p "$BIN_DIR/css"
      cp -f "$exe_dir/css/style.css" "$BIN_DIR/css/style.css"
    fi
    if [[ -f "$exe_dir/js/app.js" ]]; then
      mkdir -p "$BIN_DIR/js"
      cp -f "$exe_dir/js/app.js" "$BIN_DIR/js/app.js"
    fi
  fi
}

copy_one SwBridge "SwNode/SwAPI/SwBridge"
copy_one swapi "SwNode/SwAPI/SwApi"
copy_one SwLaunch "SwNode/SwLaunch"
copy_one SwComponentContainer "SwNode/SwComponentContainer"
copy_one SwBuild "SwNode/SwBuild"

echo "[OK] Termine."

