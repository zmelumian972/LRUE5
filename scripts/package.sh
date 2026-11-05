#!/bin/bash
# A packaged build of LEGO Racers for this machine (Linux, Development), with no game data inside: on first start the
# game finds the player's install (LEGO.JAM, LEGORacers.exe and the *.tun music) and builds its data cache from it
# Output: build/package/Linux/LegoRacers.sh
# --share: a build to give to others, archived. Output: build/share/LegoRacers-Linux.tar.gz
# usage: UE_ROOT=<Unreal Engine folder> scripts/package.sh [--share]   (after scripts/build.sh; the cook compiles shaders: slow the first time)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE="${UE_ROOT:?Set UE_ROOT to your Unreal Engine folder}"
OUT="$ROOT/build/package"
if [ "$1" = --share ]; then
  OUT="$ROOT/build/share"
  rm -rf "$OUT"
fi
"$UE/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun -project="$ROOT/LegoRacers.uproject" -platform=Linux \
  -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory="$OUT" \
  -nop4 -utf8output -unattended -NoHotReloadFromIDE
python3 "$ROOT/scripts/strip_paths.py" "$OUT/Linux/LegoRacers/Binaries/Linux/LegoRacers" "$ROOT" "$UE"
if [ "$1" = --share ]; then
  cat >"$OUT/Linux/README.txt" <<'EOF'
LEGO Racers (UE5 port)

You need your own copy of LEGO Racers (1999). Run ./LegoRacers.sh and, the first time,
select LEGO.JAM in your installed LEGO Racers folder (LEGORacers.exe has to be next to it).
No file dialog? Put that folder's files in LegoRacers/GameData yourself.

Video settings: OPTIONS > VIDEO OPTIONS, or Esc in a race.
EOF
  tar -C "$OUT" --exclude='*.debug' --exclude='*.sym' --exclude='Manifest_*' --exclude='*VkLayer_*' --exclude='GPUDumpViewer' --transform 's,^Linux,LegoRacers,' -czf "$OUT/LegoRacers-Linux.tar.gz" Linux
  ls -la "$OUT/LegoRacers-Linux.tar.gz"
  exit
fi
ls -la "$OUT"/Linux/LegoRacers.sh
