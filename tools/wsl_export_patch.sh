#!/usr/bin/env bash
# Exports the changes made to n64decomp/sm64 as a single git patch (engine hooks + wrappers),
# excluding tools/ (which contains an unrelated local MSYS2 fix).
set -e
cd /root/slr/sm64
git add -N src/game/living_core.c src/game/living_sm64.c src/game/living_hooks_inc.h
git diff -- . ':(exclude)tools' > "$1"
git reset -q src/game/living_core.c src/game/living_sm64.c src/game/living_hooks_inc.h
echo "commit base: $(git rev-parse HEAD)"
wc -l "$1"
