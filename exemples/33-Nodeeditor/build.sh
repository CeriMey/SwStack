#!/bin/bash
set -euo pipefail

BUILD_MODE="${1:-Release}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "Configuring NodeEditorExample ($BUILD_MODE) from $ROOT_DIR"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DSW_BUILD_exemples_33_Nodeeditor=ON

echo "Building NodeEditorExample"
cmake --build "$BUILD_DIR" --config "$BUILD_MODE" --target NodeEditorExample

echo "Build completed."
