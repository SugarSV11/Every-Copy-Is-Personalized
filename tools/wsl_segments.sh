#!/usr/bin/env bash
# Key segment boundaries of the last mirror build (IPL3 only loads the first 1 MiB of main).
cd /root/slr/sm64/build/us || exit 1
grep -E "^ +0x[0-9a-f]+ +_(main|engine|framebuffers|buffers|goddard|zbuffer|entry)Segment[A-Za-z]*(Start|End) " sm64.us.map | awk '{print $1, $2}'
python3 - <<'PY'
import re
m=open('sm64.us.map').read()
def g(n):
    r=re.search(r'0x([0-9a-f]+)\s+'+n+r'\s', m); return int(r.group(1),16) if r else None
end=g('_mainSegmentEnd'); start=0x80246000
print("main code+data: %d bytes, IPL3 limit 1048576, margin %d" % (end-start, 0x100000-(end-start)))
PY
