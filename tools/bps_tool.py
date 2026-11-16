#!/usr/bin/env python3
"""
bps_tool.py - create and apply BPS patches (byuu's "beat" format, the one used by
ROM hacks such as B3313). Pure Python 3, no dependencies.

  python bps_tool.py create  original.z64  modified.z64  out.bps  [--meta TEXT]
  python bps_tool.py apply   original.z64  patch.bps     out.z64
  python bps_tool.py info    patch.bps

Why a custom creator: the Living ROM grows the game's code, which SHIFTS everything
after it inside the cartridge. A naive patch would then contain megabytes of the
original game. This creator indexes the original ROM and emits SourceCopy actions for
any data that merely moved, so the patch only carries what is genuinely new.

BPS layout: "BPS1", varint sourceSize, varint targetSize, varint metadataSize, metadata,
actions..., u32le CRC32(source), u32le CRC32(target), u32le CRC32(patch so far).
Action = varint(((length - 1) << 2) | mode):
  0 SourceRead  copy from source at the current output offset
  1 TargetRead  literal bytes follow
  2 SourceCopy  varint signed delta to the source cursor, then copy
  3 TargetCopy  varint signed delta to the target cursor, then copy (not emitted here)
"""
import sys
import zlib

SOURCE_READ, TARGET_READ, SOURCE_COPY, TARGET_COPY = 0, 1, 2, 3
KEY = 16          # bytes hashed to find moved data
STRIDE = 4        # index every 4th source position (any shift is still found)
MIN_COPY = 12     # shorter fresh matches are cheaper as literals
MIN_FOLLOW = 4    # continuing the previous shift costs ~2 bytes, so even 4 pays off


def enc(n):
    out = bytearray()
    while True:
        x = n & 0x7F
        n >>= 7
        if n == 0:
            out.append(0x80 | x)
            return bytes(out)
        out.append(x)
        n -= 1


def dec(buf, pos):
    data, shift = 0, 1
    while True:
        x = buf[pos]
        pos += 1
        data += (x & 0x7F) * shift
        if x & 0x80:
            return data, pos
        shift <<= 7
        data += shift


def enc_signed(d):
    return enc((abs(d) << 1) | (1 if d < 0 else 0))


def match_len(a, ai, b, bi, limit):
    """Length of the common run a[ai:], b[bi:], compared in chunks for speed."""
    n = 0
    chunk = 4096
    while n < limit:
        c = min(chunk, limit - n)
        if a[ai + n:ai + n + c] == b[bi + n:bi + n + c]:
            n += c
            continue
        if c <= 8:
            while n < limit and a[ai + n] == b[bi + n]:
                n += 1
            return n
        chunk = max(8, c // 8)
    return n


def create(src, dst, meta=b""):
    index = {}
    for s in range(0, len(src) - KEY, STRIDE):
        index.setdefault(src[s:s + KEY], s)

    out = bytearray(b"BPS1")
    out += enc(len(src)) + enc(len(dst)) + enc(len(meta)) + meta
    literal = bytearray()
    src_cursor = 0
    stats = {"source_read": 0, "source_copy": 0, "literal": 0}

    def flush():
        if literal:
            out.extend(enc(((len(literal) - 1) << 2) | TARGET_READ))
            out.extend(literal)
            stats["literal"] += len(literal)
            literal.clear()

    t = 0
    n = len(dst)
    shift = None  # source - target offset of the last copy: relocated code keeps it
    while t < n:
        # 0) keep following the last moved block: in relocated MIPS code only the
        #    address halves of some instructions differ, everything around them matches
        if shift is not None and 0 <= t + shift < len(src):
            s = t + shift
            m = match_len(dst, t, src, s, min(n - t, len(src) - s))
            if m >= MIN_FOLLOW:
                flush()
                out.extend(enc(((m - 1) << 2) | SOURCE_COPY))
                out.extend(enc_signed(s - src_cursor))
                src_cursor = s + m
                stats["source_copy"] += m
                t += m
                continue
        # 1) same place in the original?
        if t < len(src):
            m = match_len(dst, t, src, t, min(n - t, len(src) - t))
            if m >= MIN_COPY:
                flush()
                out.extend(enc(((m - 1) << 2) | SOURCE_READ))
                stats["source_read"] += m
                shift = 0
                t += m
                continue
        # 2) the same bytes somewhere else in the original (moved data)?
        s = index.get(bytes(dst[t:t + KEY])) if t + KEY <= n else None
        if s is not None:
            m = match_len(dst, t, src, s, min(n - t, len(src) - s))
            if m >= MIN_COPY:
                flush()
                out.extend(enc(((m - 1) << 2) | SOURCE_COPY))
                out.extend(enc_signed(s - src_cursor))
                src_cursor = s + m
                stats["source_copy"] += m
                shift = s - t
                t += m
                continue
        literal.append(dst[t])
        t += 1
    flush()
    out += zlib.crc32(src).to_bytes(4, "little")
    out += zlib.crc32(dst).to_bytes(4, "little")
    out += zlib.crc32(bytes(out)).to_bytes(4, "little")
    return bytes(out), stats


def parse_header(patch):
    if patch[:4] != b"BPS1":
        raise ValueError("not a BPS patch")
    pos = 4
    ssize, pos = dec(patch, pos)
    tsize, pos = dec(patch, pos)
    msize, pos = dec(patch, pos)
    meta = patch[pos:pos + msize]
    return ssize, tsize, meta, pos + msize


def apply(src, patch):
    if zlib.crc32(patch[:-4]) != int.from_bytes(patch[-4:], "little"):
        raise ValueError("patch is corrupt (patch CRC mismatch)")
    ssize, tsize, _meta, pos = parse_header(patch)
    if len(src) != ssize or zlib.crc32(src) != int.from_bytes(patch[-12:-8], "little"):
        raise ValueError("wrong original ROM: this patch needs Super Mario 64 (USA) .z64, "
                         "SHA-1 9bef1128717f958171a4afac3ed78ee2bb4e86ce")
    out = bytearray(tsize)
    o = 0
    scur = tcur = 0
    end = len(patch) - 12
    while pos < end:
        data, pos = dec(patch, pos)
        mode, length = data & 3, (data >> 2) + 1
        if mode == SOURCE_READ:
            out[o:o + length] = src[o:o + length]
        elif mode == TARGET_READ:
            out[o:o + length] = patch[pos:pos + length]
            pos += length
        elif mode == SOURCE_COPY:
            d, pos = dec(patch, pos)
            scur += -(d >> 1) if d & 1 else (d >> 1)
            out[o:o + length] = src[scur:scur + length]
            scur += length
        else:
            d, pos = dec(patch, pos)
            tcur += -(d >> 1) if d & 1 else (d >> 1)
            for i in range(length):  # may overlap: byte by byte
                out[o + i] = out[tcur + i]
            tcur += length
        o += length
    if o != tsize or zlib.crc32(bytes(out)) != int.from_bytes(patch[-8:-4], "little"):
        raise ValueError("patched result does not match the expected target CRC")
    return bytes(out)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == "create":
        src = open(sys.argv[2], "rb").read()
        dst = open(sys.argv[3], "rb").read()
        meta = b""
        if "--meta" in sys.argv:
            meta = sys.argv[sys.argv.index("--meta") + 1].encode("utf-8")
        patch, stats = create(src, dst, meta)
        open(sys.argv[4], "wb").write(patch)
        print(f"patch: {len(patch)} bytes  ->  {sys.argv[4]}")
        print(f"  unchanged in place : {stats['source_read']:>9} bytes (taken from the player's ROM)")
        print(f"  moved              : {stats['source_copy']:>9} bytes (taken from the player's ROM)")
        print(f"  new (in the patch) : {stats['literal']:>9} bytes")
        rebuilt = apply(src, patch)
        print("self-check:", "OK, applies to an identical result" if rebuilt == dst else "FAILED")
    elif cmd == "apply":
        src = open(sys.argv[2], "rb").read()
        patch = open(sys.argv[3], "rb").read()
        open(sys.argv[4], "wb").write(apply(src, patch))
        print("patched ->", sys.argv[4])
    elif cmd == "info":
        patch = open(sys.argv[2], "rb").read()
        ssize, tsize, meta, _ = parse_header(patch)
        print(f"source {ssize} bytes CRC32 {int.from_bytes(patch[-12:-8], 'little'):08X}")
        print(f"target {tsize} bytes CRC32 {int.from_bytes(patch[-8:-4], 'little'):08X}")
        print("metadata:", meta.decode("utf-8", "replace"))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
