#!/bin/bash
# Builds the UE project's editor target (the game included) with UBT.
# usage: UE_ROOT=<Unreal Engine folder> scripts/build.sh   (UBT's log: build/ubt.log)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE="${UE_ROOT:?Set UE_ROOT to your Unreal Engine folder}"
mkdir -p "$ROOT/build"
"$UE/Engine/Build/BatchFiles/Linux/Build.sh" LegoRacersEditor Linux Development -Project="$ROOT/LegoRacers.uproject" \
  -WaitMutex -NoHotReloadFromIDE >"$ROOT/build/ubt.log" 2>&1 || { grep -E 'error' "$ROOT/build/ubt.log" | head; exit 1; }
echo "built LegoRacersEditor"
