#!/usr/bin/env bash
# Reports the AI's footprint in the last mirror build.
cd /root/slr/sm64/build/us || exit 1
mips-linux-gnu-size src/game/living_core.o src/game/living_sm64.o
echo "--- largest AI symbols (bytes) ---"
mips-linux-gnu-nm -S --size-sort sm64.us.elf | grep -E " (gLiving|sObj|sBeh|sOurs|sDiag|lsm_|lme_|lobj_|lgeo_|ld_|lp_|lt_|lm_|lg_|le_|living_)" | tail -12
echo "--- IDO warnings mentioning living sources ---"
grep -i "living" /root/living.log | grep -iE "warning|error" | head -20
echo "--- end of main segment / free RAM indicators ---"
grep -E "_mainSegmentNoloadEnd|_mainSegmentEnd|_engineSegmentStart|_goddardSegmentStart" sm64.us.map | head
