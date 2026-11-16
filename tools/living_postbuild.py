#!/usr/bin/env python3
"""
Post-build: copies the built Living ROM into builds/ under a new, never-overwritten
name, reports hashes/size and writes the emulator database entry needed for a
16 Kbit EEPROM (without it, emulators default SM64-like ROMs to 4 Kbit and the AI
runs in RAM-only mode: the game still works, the AI just forgets on power-off).

Usage: python tools/living_postbuild.py path/to/sm64.us.z64
"""
import datetime
import hashlib
import os
import shutil
import sys

HERE = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
ORIGINAL = os.path.join(os.path.dirname(HERE), "Super Mario 64 (USA).z64")
ORIGINAL_SHA1 = "9bef1128717f958171a4afac3ed78ee2bb4e86ce"


def digest(path, algo):
    h = hashlib.new(algo)
    with open(path, "rb") as fh:
        h.update(fh.read())
    return h.hexdigest()


def main():
    src = sys.argv[1]
    data = open(src, "rb").read()
    if data[:4] != b"\x80\x37\x12\x40":
        sys.exit("not a big-endian .z64 ROM")
    crc1 = data[0x10:0x14].hex().upper()
    crc2 = data[0x14:0x18].hex().upper()
    country = data[0x3E]

    out_dir = os.path.join(HERE, "builds")
    os.makedirs(out_dir, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    tag = sys.argv[2] if len(sys.argv) > 2 else "living"
    dst = os.path.join(out_dir, f"sm64_{tag}_{stamp}.us.z64")
    if os.path.exists(dst):
        sys.exit("refusing to overwrite " + dst)
    # Advanced Homebrew ROM Header (n64brew.dev/wiki/ROM_Header): Game ID "ED" at 0x3C and
    # savetype in bits 4-7 of 0x3F (2 = 16 Kbit EEPROM). ares, mupen64plus/RMG, cen64,
    # Parallel Launcher, Gopher64, EverDrive 64 OS 3.x and SummerCart64 then pick the
    # right EEPROM by themselves. The header is outside the IPL3 checksum (0x1000+).
    rom = bytearray(data)
    rom[0x3C:0x3E] = b"ED"
    rom[0x3F] = 0x20
    with open(dst, "wb") as fh:
        fh.write(rom)

    rdb = (f"[{crc1}-{crc2}-C:{country:02X}]\n"
           "Good Name=Every Copy Is Personalized (U)\n"
           "Internal Name=SUPER MARIO 64\n"
           "Status=Compatible\n32bit=1\nCulling=1\nRDRAM Size=8\n"
           "Save Type=16kbit Eeprom\n")
    with open(dst + ".rdb.txt", "w") as fh:
        fh.write(rdb)

    print("ROM      :", dst)
    print("size     :", len(data), "bytes")
    print("sha1     :", digest(dst, "sha1"))
    print("sha256   :", digest(dst, "sha256"))
    print("CRC      :", crc1, crc2)
    print("RDB entry (Project64 / Vortex64 Config/Project64.rdb):\n" + rdb)
    if os.path.exists(ORIGINAL):
        ok = digest(ORIGINAL, "sha1") == ORIGINAL_SHA1
        print("Desktop original untouched:", "yes" if ok else "NO - CHECK!")


if __name__ == "__main__":
    main()
