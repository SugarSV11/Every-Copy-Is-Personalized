#!/usr/bin/env bash
# SM64 Living ROM - full N64 build. Run inside WSL (Ubuntu) from the SM64-Living-ROM folder:
#   bash tools/build_rom.sh
# Steps:
#   1. check toolchain        2. copy baserom (never the Desktop original)
#   3. vanilla matching build (LIVING_ROM=0, COMPARE=1) -> proves the toolchain is exact
#   4. living build (LIVING_ROM=1, NON_MATCHING=1)      5. post-build report into builds/
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
SM64="$HERE/sm64"
JOBS="${JOBS:-$(nproc)}"
EXPECTED_SHA1="9bef1128717f958171a4afac3ed78ee2bb4e86ce"

need() { command -v "$1" >/dev/null 2>&1 || { echo "missing: $1  ($2)"; exit 1; }; }
need make "sudo apt install build-essential"
need python3 "sudo apt install python3"
need mips-linux-gnu-ld "sudo apt install binutils-mips-linux-gnu"
need pkg-config "sudo apt install pkg-config"

sha() { sha1sum "$1" | cut -d' ' -f1; }

[ "$(sha "$HERE/rom/baserom.us.z64")" = "$EXPECTED_SHA1" ] || { echo "rom/baserom.us.z64 hash mismatch"; exit 1; }
cp -n "$HERE/rom/baserom.us.z64" "$SM64/baserom.us.z64"

python3 "$HERE/tools/apply_living_hooks.py" "$SM64"

echo "== vanilla matching build (verifies toolchain) =="
make -C "$SM64" VERSION=us LIVING_ROM=0 COMPARE=1 -j"$JOBS"
[ "$(sha "$SM64/build/us/sm64.us.z64")" = "$EXPECTED_SHA1" ] && echo "MATCHING OK"
cp "$SM64/build/us/sm64.us.z64" "$HERE/builds/vanilla_rebuild.us.z64"

echo "== living build =="
make -C "$SM64" clean VERSION=us >/dev/null
make -C "$SM64" VERSION=us LIVING_ROM=1 NON_MATCHING=1 COMPARE=0 -j"$JOBS"
python3 "$HERE/tools/living_postbuild.py" "$SM64/build/us/sm64.us.z64"
