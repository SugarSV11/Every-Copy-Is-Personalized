#!/usr/bin/env bash
# Builds in a Linux-side mirror (/root/slr) because building on the OneDrive/NTFS mount
# from WSL is very slow. Usage (from Windows):
#   wsl -d Ubuntu-24.04 -u root -- bash /mnt/c/.../SM64-Living-ROM/tools/wsl_mirror_build.sh [extra make vars]
set -uo pipefail
W="$(cd "$(dirname "$0")/.." && pwd)"
M=/root/slr
mkdir -p "$M"
if [ ! -d "$M/sm64/.git" ]; then
  rsync -a --exclude builds --exclude sm64/build "$W/" "$M/"
  git -C "$M/sm64" clean -qfdX tools   # drop host binaries built on Windows
fi
rsync -a --delete "$W/src/" "$M/src/"
# debugging aid: BISECT_DEFINE=NAME compiles the mirror with "#define NAME" in living.c
for d in ${BISECT_DEFINE:-}; do sed -i "1i #define $d" "$M/src/living.c"; done
if [ -n "${BISECT_INTEG:-}" ]; then sed -i "1i $BISECT_INTEG" "$M/src/integration/living_sm64.c"; fi
rsync -a "$W/tools/" "$M/tools/"
rsync -a "$W/rom/" "$M/rom/"
cd "$M/sm64"
cp --update=none ../rom/baserom.us.z64 baserom.us.z64
git checkout -q -- Makefile lib src
rm -f src/game/living_core.c src/game/living_sm64.c src/game/living_hooks_inc.h
python3 ../tools/apply_living_hooks.py .
make clean VERSION=us >/dev/null
make VERSION=us LIVING_ROM=1 NON_MATCHING=1 COMPARE=0 -j"$(nproc)" "$@" > /root/living.log 2>&1
rc=$?
echo "make exit=$rc"
grep -nE "error|warning|undefined" /root/living.log | grep -v dangerous | head -60
tail -2 /root/living.log
if [ $rc -eq 0 ]; then
  mkdir -p "$W/builds"
  tag=living; case " $* " in *LIVING_SELFTEST=1*) tag=selftest;; esac
  python3 "$W/tools/living_postbuild.py" build/us/sm64.us.z64 "$tag"
fi
