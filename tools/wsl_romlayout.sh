#!/usr/bin/env bash
# Highest ROM address used by any segment of the last mirror build (must stay < 8 MiB).
cd /root/slr/sm64/build/us || exit 1
grep -oE "0x[0-9a-f]+ +_[A-Za-z0-9_]+SegmentRomEnd" sm64.us.map | sort | tail -3
ls -l sm64.us.z64 | awk '{print "rom file size:", $5}'
mips-linux-gnu-objdump -h sm64.us.elf | awk '$4 ~ /^0/ {print $2, $3, $6}' | sort -k3 | tail -3
