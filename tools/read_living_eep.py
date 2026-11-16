#!/usr/bin/env python3
"""
Decodes a 2 KiB (16 Kbit) EEPROM dump written by the Living ROM (e.g. an emulator .eep file):
original SM64 save integrity, both AI copies (header, CRC, seq), the AI memory, and the
self-test performance block (blocks 253-255, LIVING_SELFTEST builds only).

Usage: python tools/read_living_eep.py path/to/file.eep
Layout mirrors src/persistence/save_manager.c (payload v1, 640 bytes).
"""
import struct
import sys

FIRST, COPY_BLOCKS, HDR = 64, 96, 10
TRAITS = ["curiosity", "playful", "benevolent", "menace", "patience", "attachment"]
MOODS = ["DORMANT", "OBSERVING", "CURIOUS", "PLAYFUL", "PROTECTIVE", "TESTING", "UNSETTLING"]
OPS = ["NONE", "ROOM", "BOX_BRIDGE", "BOX_STAIRS", "BOX_WALL", "BOX_TOWER",
       "ENEMY_AMBUSH", "ENEMY_ROUTINE", "GIFT", "AMBIENCE", "SECRET"]
EVENTS = ["FIRST_CONTACT", "WELCOME_BACK", "IDLE_STIR", "MEMORIAL", "LOOP_BREAKER",
          "CAMERA_GAZE", "SILENCE", "HUNT", "GIFT_RAIN", "SECRET_FOUND", "PASSAGE_SHIFT"]
LOADS = ["OK", "FRESH", "RECOVERED", "CORRUPT_RESET", "NEWER_VERSION", "NO_STORAGE"]
LEVELS = {4: "BBH", 5: "CCM", 6: "CASTLE_INSIDE", 7: "HMC", 8: "SSL", 9: "BOB", 10: "SL", 11: "WDW",
          12: "JRB", 13: "THI", 14: "TTC", 15: "RR", 16: "CASTLE_GROUNDS", 17: "BITDW", 18: "VCUTM",
          19: "BITFS", 20: "SA", 21: "BITS", 22: "LLL", 23: "DDD", 24: "WF", 26: "CASTLE_COURTYARD",
          27: "PSS", 28: "COTMC", 29: "TOTWC", 36: "WMOTR"}


def crc16(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def copy_status(img):
    if all(b == img[0] for b in img[:HDR]) and img[0] in (0, 0xFF):
        return "empty", None
    magic, ver, _flags, seq, ln, crc = struct.unpack(">HBBHHH", img[:HDR])
    if magic != 0x4C56 or ln > len(img) - HDR:
        return "corrupt (magic)", None
    calc = crc16(img[HDR:HDR + ln], crc16(img[:8]))
    if calc != crc:
        return f"corrupt (crc {calc:04X} != {crc:04X})", None
    return f"valid v{ver} seq={seq} len={ln}", (seq, img[HDR:HDR + ln])


def decode(p):
    o = 0

    def take(fmt):
        nonlocal o
        v = struct.unpack_from(">" + fmt, p, o)
        o += struct.calcsize(">" + fmt)
        return v if len(v) > 1 else v[0]

    m = {}
    m["seed"], m["sessions"], m["playSec"] = take("I"), take("H"), take("I")
    m["act"] = take("10H")
    (m["coins"], m["stars"], m["deaths"], m["camChanges"], m["idleSec"], m["loops"],
     m["areaChanges"]) = take("7H")
    m["traits"] = take("6b")
    m["mood"], m["moodAge"], m["reflections"] = take("B"), take("B"), take("H")
    m["affinity"] = take("11B")
    m["eventsSeen"], m["eventsFired"], m["mutApplied"], m["mutRejected"], m["recoveries"] = (
        take("I"), take("H"), take("H"), take("H"), take("B"))
    areas = []
    for _ in range(12):
        a = {}
        a["level"], a["area"], a["visits"], a["returns"] = take("4B")
        a["dwell"], a["deaths"], a["lastSession"] = take("H"), take("B"), take("B")
        a["explored"] = bin(int.from_bytes(take("8s"), "big")).count("1")
        a["hot"], a["deathCell"], a["gen"], a["flags"], a["vsg"] = take("5B")
        a["plan"] = [take("2B") for _ in range(4)]
        a["anchors"] = [take("3h") for _ in range(3)]
        if a["level"]:
            areas.append(a)
    m["areas"] = areas
    m["shiftSession"], m["shiftCount"] = (take("B"), take("B")) if len(p) >= 644 else (0, 0)
    m["version"] = 2 if len(p) >= 644 else 1
    return m


def main():
    raw = open(sys.argv[1], "rb").read()
    print(f"file: {sys.argv[1]} ({len(raw)} bytes)")
    if len(raw) <= 512:
        print("NOTE: no data beyond 512 bytes -> 4 Kbit EEPROM or the AI never saved; the AI was RAM-only.")
    # emulators only extend the file up to the last block written: pad like an erased chip
    data = raw + bytes([0xFF]) * max(0, 2048 - len(raw))
    nonzero = any(data[:512])
    print("original SM64 save (blocks 0-63):", "present" if nonzero else "empty")
    best = None
    for c in range(2):
        start = (FIRST + c * COPY_BLOCKS) * 8
        img = data[start:start + COPY_BLOCKS * 8]
        if len(img) < COPY_BLOCKS * 8:
            print(f"copy {'AB'[c]}: missing")
            continue
        st, payload = copy_status(img)
        print(f"copy {'AB'[c]}: {st}")
        if payload and (best is None or ((payload[0] - best[0]) & 0xFFFF) < 0x8000):
            best = payload
    if best:
        m = decode(best[1])
        print(f"\nAI memory (seq {best[0]}): sessions={m['sessions']} playSec={m['playSec']} seed={m['seed']:08X}")
        print(f"  format v{m['version']}  passage shifts so far: {m['shiftCount']} (last in session {m['shiftSession']})")
        print("  mood:", MOODS[m["mood"]] if m["mood"] < len(MOODS) else m["mood"], " reflections:", m["reflections"])
        print("  traits:", ", ".join(f"{n}={v}" for n, v in zip(TRAITS, m["traits"])))
        print(f"  observed: coins={m['coins']} stars={m['stars']} deaths={m['deaths']} cam={m['camChanges']}"
              f" idle={m['idleSec']}s loops={m['loops']} areaChanges={m['areaChanges']}")
        print(f"  actions (jump,triple,backflip,long,wallkick,gp,dive,swim,fly):", m["act"][1:])
        print(f"  mutations applied={m['mutApplied']} rejected={m['mutRejected']}  events fired={m['eventsFired']}:",
              [e for i, e in enumerate(EVENTS) if m["eventsSeen"] >> i & 1])
        print("  learned affinity:", {OPS[i]: v for i, v in enumerate(m["affinity"]) if i and v != 128})
        for a in m["areas"]:
            plan = [OPS[op & 0x7F] + ("(rejected)" if op & 0x80 else "") for op, _ in a["plan"] if op]
            print(f"  area {LEVELS.get(a['level'], a['level'])}:{a['area']} visits={a['visits']} returns={a['returns']}"
                  f" dwell={a['dwell']}s deaths={a['deaths']} explored={a['explored']} cells gen={a['gen']} plan={plan}")
    cr = data[251 * 8:252 * 8]
    if cr[:2] == b"CR":
        names = {1: "after boot", 2: "area_loaded start", 3: "area_loaded end", 4: "unloading start",
                 5: "unloading after AI", 6: "unloading end", 7: "update", 8: "frame"}
        print(f"\nbreadcrumb: last hook = {names.get(cr[2], cr[2])}, level {cr[3]}, main frame {int.from_bytes(cr[4:8], 'big')}")
    diag = data[253 * 8:256 * 8]
    if len(diag) == 24 and diag[:2] == b"DG":
        maxu, = struct.unpack(">I", diag[4:8])
        avg, maxe = struct.unpack(">II", diag[8:16])
        frames, enters = struct.unpack(">IH", diag[16:22])
        tick_us = 1e6 / 46875000.0  # osGetCount runs at half the 93.75 MHz CPU clock
        frame_us = 1e6 / 30
        print("\nself-test performance (osGetCount ticks; as accurate as the emulator's COUNT emulation):")
        print(f"  load result: {LOADS[diag[3]] if diag[3] < len(LOADS) else diag[3]}  frames measured: {frames}  area entries: {enters}")
        print(f"  living_update avg {avg} ticks = {avg * tick_us:.1f} us ({100 * avg * tick_us / frame_us:.3f}% of a 30 fps frame)")
        print(f"  living_update max {maxu} ticks = {maxu * tick_us:.1f} us ({100 * maxu * tick_us / frame_us:.2f}%)")
        print(f"  area entry (plan apply) max {maxe} ticks = {maxe * tick_us:.1f} us ({100 * maxe * tick_us / frame_us:.2f}%)")
        hb = data[252 * 8:253 * 8]
        if len(hb) == 8 and any(hb):
            mf, = struct.unpack(">I", hb[:4])
            print(f"  heartbeat: {mf} main-loop frames (~{mf / 30:.0f} s of game time), last level={hb[4]} area={hb[5]}"
                  f" entry stage={hb[6]} play mode={hb[7]}")


if __name__ == "__main__":
    main()
