#!/usr/bin/env bash

set -euo pipefail

# -----------------------------------------------------------------------------
# SwProjetNode - build puis launch (Linux/WSL)
# Usage:
#   ./run.sh                 -> build incremental puis launch (duree depuis JSON)
#   ./run.sh -c|--clean       -> build --clean puis launch
#   ./run.sh 0                -> build incremental puis launch (override: run infini)
#   ./run.sh -c|--clean 0     -> build --clean puis launch (override: run infini)
#
# Legacy (no-op / kept for compatibility):
#   ./run.sh noclean          -> equivalent to default (incremental)
# -----------------------------------------------------------------------------

THIS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

pushd "$THIS_DIR" >/dev/null
cleanup_() { popd >/dev/null || true; }
trap cleanup_ EXIT

SWBUILD_BIN="${SWBUILD_BIN:-"$THIS_DIR/../build-wsl/tools/SwNode/SwBuild/SwBuild"}"
SWLAUNCH_BIN="${SWLAUNCH_BIN:-"$THIS_DIR/../build-wsl/tools/SwNode/SwLaunch/SwLaunch"}"
LAUNCH_JSON="$THIS_DIR/SwProjetNode.launch.json"

if [[ ! -f "$SWBUILD_BIN" ]]; then
  echo "[Erreur] SwBuild introuvable: \"$SWBUILD_BIN\""
  echo "        Lance d'abord le build du repo parent: \"../build.sh\""
  exit 1
fi
if [[ ! -f "$SWLAUNCH_BIN" ]]; then
  echo "[Erreur] SwLaunch introuvable: \"$SWLAUNCH_BIN\""
  echo "        Lance d'abord le build du repo parent: \"../build.sh\""
  exit 1
fi
if [[ ! -f "$LAUNCH_JSON" ]]; then
  echo "[Erreur] Launch JSON introuvable: \"$LAUNCH_JSON\""
  exit 1
fi

BUILD_CLEAN=""
BUILD_VERBOSE="--verbose"
LAUNCH_DURATION=()

BUILD_ROOT="build-wsl"

while [[ $# -gt 0 ]]; do
  case "$1" in
    noclean)
      shift
      ;;
    -c|--clean)
      BUILD_CLEAN="--clean"
      shift
      ;;
    -h|--help)
      echo "Usage: ./run.sh [-c|--clean] [duration_ms|0]"
      exit 0
      ;;
    *)
      if [[ "${#LAUNCH_DURATION[@]}" -eq 0 ]]; then
        LAUNCH_DURATION=( "--duration_ms=$1" )
        shift
      else
        echo "[Erreur] argument inconnu: \"$1\""
        exit 2
      fi
      ;;
  esac
done

echo "[SwBuild] build SwProjetNode..."
"$SWBUILD_BIN" --root "$THIS_DIR" --scan src --build_root "$BUILD_ROOT" \
  ${BUILD_CLEAN:+$BUILD_CLEAN} ${BUILD_VERBOSE:+$BUILD_VERBOSE}

echo "[SwLaunch] launch..."
"$SWLAUNCH_BIN" --config_file "$LAUNCH_JSON" "${LAUNCH_DURATION[@]}"
