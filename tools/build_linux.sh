#!/usr/bin/env bash
# Every Copy Is Personalized - compilacion en Linux nativo (sin WSL).
#
# Uso, desde la raiz del repositorio:
#   mkdir -p rom && cp /ruta/a/tu/Super_Mario_64_USA.z64 rom/baserom.us.z64
#   bash tools/build_linux.sh            # compila la ROM
#   bash tools/build_linux.sh --bps      # ademas crea el parche .bps contra tu baserom
#
# Dependencias (Debian/Ubuntu):
#   sudo apt install build-essential git python3 pkg-config binutils-mips-linux-gnu \
#                    libcapstone-dev libaudiofile-dev zlib1g-dev
# Necesita tu propia copia de Super Mario 64 (USA); este repositorio no la incluye.
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
SM64="$HERE/sm64"
SM64_REPO="https://github.com/n64decomp/sm64.git"
SM64_COMMIT="9921382"
EXPECTED_SHA1="9bef1128717f958171a4afac3ed78ee2bb4e86ce"
JOBS="${JOBS:-$(nproc)}"
MAKE_BPS=0
[ "${1:-}" = "--bps" ] && MAKE_BPS=1

need() { command -v "$1" >/dev/null 2>&1 || { echo "Falta '$1'  ->  $2"; exit 1; }; }
need make "sudo apt install build-essential"
need git "sudo apt install git"
need python3 "sudo apt install python3"
need pkg-config "sudo apt install pkg-config"
need mips-linux-gnu-ld "sudo apt install binutils-mips-linux-gnu"

BASE="$HERE/rom/baserom.us.z64"
[ -f "$BASE" ] || { echo "Falta $BASE (tu propia ROM de Super Mario 64 USA, formato .z64)"; exit 1; }
[ "$(sha1sum "$BASE" | cut -d' ' -f1)" = "$EXPECTED_SHA1" ] || {
  echo "El SHA-1 de rom/baserom.us.z64 no coincide con $EXPECTED_SHA1 (se necesita la USA .z64 big-endian)"; exit 1; }

# 1. Decompilacion base, en el commit exacto sobre el que se hizo el hack
if [ ! -d "$SM64/.git" ]; then
  git -c core.autocrlf=false clone "$SM64_REPO" "$SM64"
fi
git -C "$SM64" checkout -q "$SM64_COMMIT"
git -C "$SM64" checkout -q -- Makefile lib src
rm -f "$SM64"/src/game/living_core.c "$SM64"/src/game/living_sm64.c "$SM64"/src/game/living_hooks_inc.h
cp -n "$BASE" "$SM64/baserom.us.z64"

# 2. Ganchos del hack (idempotente) y compilacion
python3 "$HERE/tools/apply_living_hooks.py" "$SM64"
make -C "$SM64" clean VERSION=us >/dev/null
make -C "$SM64" VERSION=us LIVING_ROM=1 NON_MATCHING=1 COMPARE=0 -j"$JOBS"

# 3. Cabecera 16 Kbit EEPROM (byte 0x3F = 0x20) + resumen en builds/
python3 "$HERE/tools/living_postbuild.py" "$SM64/build/us/sm64.us.z64"

# 4. Parche BPS opcional
if [ "$MAKE_BPS" = 1 ]; then
  OUT="$(ls -t "$HERE"/builds/sm64_living_*.us.z64 | head -n1)"
  python3 "$HERE/tools/bps_tool.py" create "$BASE" "$OUT" "$HERE/builds/Every-Copy-Is-Personalized.bps"
  echo "Parche: $HERE/builds/Every-Copy-Is-Personalized.bps"
fi
