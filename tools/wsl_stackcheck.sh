#!/usr/bin/env bash
# Stack frame size of every function in the AI objects of the last mirror build.
cd /root/slr/sm64/build/us || exit 1
for o in src/game/living_core.o src/game/living_sm64.o; do
  mips-linux-gnu-objdump -d "$o" | awk '/^[0-9a-f]+ <.*>:/{f=$2} /addiu\tsp,sp,-/{split($NF,a,"-"); print a[2]+0, f}' | sort -n | tail -6
done
grep -n "gThread5Stack\|STACK_SIZE\|0x2000" ../../src/game/main.c ../../include/*.h 2>/dev/null | grep -i "thread5\|STACKSIZE\|stack" | head -5
