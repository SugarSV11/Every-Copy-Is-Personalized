#!/usr/bin/env bash
# DMA-sensitive buffers must be 16-byte aligned (SI/PIF) / 8-byte (PI, audio). Lists any that are not.
cd /root/slr/sm64/build/us || exit 1
mips-linux-gnu-nm -S -n sm64.us.elf | awk '$3 ~ /^[bBdD]$/ {print}' | \
  grep -E " (__osEepPifRam|__osContPifRam|__osPfsPifRam|__osPifInternalBuff|gAudio|gThread[0-9]Stack|gGfxPool|gGfxSPTask|gZBuffer|gFramebuffer|sAudioDma|gAudioHeap|gSoundDataADSR|gSaveBuffer|gControllerPads|gControllerStatuses)" | \
  while read addr size type name; do
    a=$((16#$addr)); mod=$((a % 16)); echo "$addr mod16=$mod $name"; done
