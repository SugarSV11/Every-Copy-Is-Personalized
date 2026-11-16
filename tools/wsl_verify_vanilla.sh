#!/usr/bin/env bash
# With every Living hook applied but LIVING_ROM=0, the build must still match the
# original ROM byte for byte (proves the hooks are fully compiled out).
cd /root/slr/sm64 || exit 1
make clean VERSION=us >/dev/null
make VERSION=us LIVING_ROM=0 COMPARE=1 -j"$(nproc)" > /root/vanilla_hooked.log 2>&1
echo "make exit=$?"
tail -1 /root/vanilla_hooked.log
sha1sum build/us/sm64.us.z64
git -C /root/slr/sm64 status --porcelain | grep -c "^ M" | sed 's/^/patched engine files: /'
