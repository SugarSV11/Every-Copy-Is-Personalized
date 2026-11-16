<div align="center">

# 🍄 Every Copy Is Personalized

**A Super Mario 64 hack with an AI inside the cartridge: it watches how you play, changes the world and remembers.**

![Status](https://img.shields.io/badge/status-beta-orange)
![Version](https://img.shields.io/badge/version-1.4--beta-blue)
![Platform](https://img.shields.io/badge/N64-ROM%20hack-red)
![Tests](https://img.shields.io/badge/tests-101%20passing-brightgreen)
![Language](https://img.shields.io/badge/AI-C89%20no%20malloc-lightgrey)

[🇪🇸 Español](README.md) · 🇬🇧 English

</div>

> ⚠️ **Beta**: it may have bugs.
> Recommended: [Vortex64](https://sugarsv11.github.io/vortex64-site/), which ships the database entry for the 16 Kbit EEPROM. **It runs on any emulator or console**: without a 16K EEPROM the game still boots and the AI works in RAM only (it forgets on power-off).

## 📸 Screenshots

> Pending: GIFs will be added to [`docs/img/`](docs/img/).

## 🚀 How to play

1. Download **`Every-Copy-Is-Personalized-v1.4-beta.bps`** from [Releases](../../releases).
2. Apply it to **your own** *Super Mario 64 (USA)* `.z64` (SHA-1 `9bef1128717f958171a4afac3ed78ee2bb4e86ce`) using [Rom Patcher JS](https://www.marcrobledo.com/RomPatcher.js/), Floating IPS or `python tools/bps_tool.py apply`.
3. Open the patched ROM in your emulator or flashcart.

This repository **contains no ROM or Nintendo data**: only the patch and the hack's source code.

## 🕹️ Emulators and console

The ROM header declares a 16 Kbit EEPROM (id `ED`, byte `0x3F = 0x20`), so modern emulators pick it automatically.

| Platform | Status | Notes |
|---|:--:|---|
| [Vortex64](https://sugarsv11.github.io/vortex64-site/) | ✅ Tested on v1.3 · v1.4 _Pending_ | v1.3 (locked) was tested with saving; the unlocked v1.4 has not been |
| ares | ⏳ Pending | |
| RMG / mupen64plus | ⏳ Pending | |
| Console with EverDrive | ⏳ Pending | |

## 🧠 How the AI works

> **Important:** the "AI" is **not a language model or a neural network**. It is a decision system written in C: counters, rules, weighted utilities and a deterministic pseudo-random generator. It fits in the ROM, uses no network and costs ~8 µs per frame (measured in the emulator).

Pipeline: **observe** (position, deaths, coins) → **remember** (per-level memory and traits) → **personality** (curious, protective, playful…) → **decide** (utility + learned affinity) → **change the world** (blocks, rooms, passages, music/scale) → **save** (16K EEPROM, A/B copies with CRC). The first minutes it only observes; later it starts changing things, subtly.

## 🔨 Building

| Environment | Command |
|---|---|
| Native Linux | `bash tools/build_linux.sh` (add `--bps` to create the patch) |
| Windows + WSL | `wsl -d Ubuntu-24.04 -u root -- bash tools/wsl_mirror_build.sh` |
| PC tests (MSVC) | `build_host.bat`, then `builds\host\test_living.exe` |

You need your own ROM at `rom/baserom.us.z64`. See [`docs/DESARROLLO.md`](docs/DESARROLLO.md) (Spanish).

## 🔗 Links

- 🌐 Vortex64 website: https://sugarsv11.github.io/vortex64-site/
- 🪟 Vortex64 for Windows: https://github.com/SugarSV11/Vortex64-Windows

---

*Super Mario 64 is a trademark of Nintendo. Non-profit fan project, not affiliated with Nintendo.*
