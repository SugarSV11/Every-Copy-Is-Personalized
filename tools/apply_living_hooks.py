#!/usr/bin/env python3
"""
Inserts the SM64 Living ROM hooks into a clean n64decomp/sm64 checkout.

Usage:  python tools/apply_living_hooks.py [path/to/sm64]   (default: ./sm64)

Every insertion is guarded by #ifdef LIVING_ROM, and `make LIVING_ROM=0`
builds the untouched game. The script refuses to patch if any anchor text
is missing or ambiguous, and is idempotent (already-patched files are skipped).
"""
import os
import sys

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "sm64"))
MARK = "LIVING_ROM"

WRAPPERS = {
    "src/game/living_core.c": """/* SM64 Living ROM: portable AI core, compiled as one translation unit.
 * Canonical sources live in SM64-Living-ROM/src (outside the decomp tree). */
#ifdef LIVING_ROM
#include "../../../src/living.c"
#include "../../../src/ai/behavior_tracker.c"
#include "../../../src/ai/player_memory.c"
#include "../../../src/ai/personality_engine.c"
#include "../../../src/ai/decision_engine.c"
#include "../../../src/world/procedural_generator.c"
#include "../../../src/world/mutation_engine.c"
#include "../../../src/world/geometry_mutator.c"
#include "../../../src/world/object_mutator.c"
#include "../../../src/world/event_system.c"
#include "../../../src/world/passage_shifter.c"
#include "../../../src/persistence/save_manager.c"
#include "../../../src/persistence/memory_storage.c"
#endif
""",
    "src/game/living_sm64.c": """/* SM64 Living ROM: engine integration layer (separate TU from the portable core). */
#include "../../../src/integration/living_sm64.c"
""",
    "src/game/living_hooks_inc.h": """/* Included by engine files (game_init.c, area.c, level_update.c, save_file.c). */
#include "../../../src/integration/living_hooks.h"
""",
}

INC = '#include "living_hooks_inc.h"\n'

PATCHES = [
    # (file, anchor, replacement)
    ("Makefile", "DEFINES += _FINALROM=1\n",
     "DEFINES += _FINALROM=1\n\n# SM64 Living ROM Engine (LIVING_ROM=0 builds the untouched game)\n"
     "LIVING_ROM ?= 1\nifeq ($(LIVING_ROM),1)\n  DEFINES += LIVING_ROM=1\nendif\n"),
    ("Makefile", "  DEFINES += LIVING_ROM=1\nendif\n",
     "  DEFINES += LIVING_ROM=1\nendif\n"
     "# LIVING_SELFTEST=1: learns from the title demos, no dormant period, perf counters in EEPROM\n"
     "ifeq ($(LIVING_SELFTEST),1)\n  DEFINES += LIVING_SELFTEST=1\nendif\n"),
    # No SRC_DIRS change: the wrappers live in src/game/, which the Makefile already
    # compiles and sm64.ld already links via its BUILD_DIR/src/game*.o wildcards
    # (.text / .data / .rodata / .bss on US).

    # IPL3 (the boot code in every cartridge) copies only the first 1 MiB of the main
    # segment into RAM. The Living ROM's code pushes main past that limit, and whatever
    # lies beyond it (the tail of .data/.rodata) would be garbage. Load the tail with a
    # raw PI DMA as the very first thing main_func does (all .text precedes the tail).
    ("src/game/main.c", "void main_func(void) {\n    UNUSED u8 filler[64];\n\n",
     "#ifdef LIVING_ROM\n"
     "/* PI registers (uncached KSEG1); rcp.h is not included because it redefines RDRAM_END */\n"
     "#define LIVING_PI_REG(o) (*(volatile u32 *) (0xA4600000 + (o)))\n"
     "extern u32 osVirtualToPhysical(void *);\n"
     "extern u8 _mainSegmentStart[];\n"
     "extern u8 _mainSegmentEnd[];\n"
     "extern u8 _mainSegmentRomStart[];\n"
     "#define LIVING_IPL3_LOAD_SIZE 0x100000\n"
     "static void living_load_main_tail(void) {\n"
     "    u32 size = (u32) _mainSegmentEnd - (u32) _mainSegmentStart;\n"
     "    u8 *dest;\n"
     "    u32 len;\n"
     "\n"
     "    if (size <= LIVING_IPL3_LOAD_SIZE) {\n"
     "        return;\n"
     "    }\n"
     "    dest = _mainSegmentStart + LIVING_IPL3_LOAD_SIZE;\n"
     "    len = size - LIVING_IPL3_LOAD_SIZE;\n"
     "    while (LIVING_PI_REG(0x10) & 0x3) { /* PI status: DMA or IO busy */\n"
     "    }\n"
     "    LIVING_PI_REG(0x00) = osVirtualToPhysical(dest); /* DRAM address */\n"
     "    LIVING_PI_REG(0x04) = 0x10000000 + (u32) _mainSegmentRomStart + LIVING_IPL3_LOAD_SIZE; /* cart */\n"
     "    LIVING_PI_REG(0x0C) = len - 1; /* cart -> DRAM length */\n"
     "    while (LIVING_PI_REG(0x10) & 0x3) {\n"
     "    }\n"
     "    osInvalDCache(dest, len);\n"
     "    osInvalICache(dest, len);\n"
     "}\n"
     "#endif\n"
     "\n"
     "void main_func(void) {\n    UNUSED u8 filler[64];\n\n"
     "#ifdef LIVING_ROM\n    living_load_main_tail();\n#endif\n"),

    ("src/game/game_init.c", '#include "main.h"\n', '#include "main.h"\n' + INC),
    ("src/game/game_init.c", "    save_file_load_all();\n",
     "    save_file_load_all();\n#ifdef LIVING_ROM\n    living_hook_boot();\n#endif\n"),
    ("src/game/game_init.c", "        addr = level_script_execute(addr);\n",
     "        addr = level_script_execute(addr);\n#ifdef LIVING_ROM\n        living_hook_frame();\n#endif\n"),

    ("src/game/area.c", '#include "dialog_ids.h"\n', '#include "dialog_ids.h"\n' + INC),
    ("src/game/area.c",
     "        load_obj_warp_nodes();\n"
     "        geo_call_global_function_nodes(&gCurrentArea->unk04->node, GEO_CONTEXT_AREA_LOAD);\n    }",
     "        load_obj_warp_nodes();\n"
     "        geo_call_global_function_nodes(&gCurrentArea->unk04->node, GEO_CONTEXT_AREA_LOAD);\n"
     "#ifdef LIVING_ROM\n        living_hook_area_loaded();\n#endif\n    }"),
    ("src/game/area.c",
     "void unload_area(void) {\n    if (gCurrentArea != NULL) {\n",
     "void unload_area(void) {\n    if (gCurrentArea != NULL) {\n"
     "#ifdef LIVING_ROM\n        living_hook_area_unloading(); /* objects still exist: the AI evaluates, then forgets */\n#endif\n"),
    ("src/game/area.c",
     "                      SCREEN_HEIGHT - BORDER_HEIGHT);\n        render_hud();",
     "                      SCREEN_HEIGHT - BORDER_HEIGHT);\n#ifdef LIVING_ROM\n        living_hook_render();\n#endif\n        render_hud();"),

    # Level exits (CLEAR_LEVEL) destroy objects and areas WITHOUT calling unload_area():
    # let the AI evaluate and commit before clear_objects() runs.
    ("src/engine/level_script.c", '#include "game/sound_init.h"\n',
     '#include "game/sound_init.h"\n#include "game/living_hooks_inc.h"\n'),
    ("src/engine/level_script.c",
     "static void level_cmd_clear_level(void) {\n    clear_objects();\n",
     "static void level_cmd_clear_level(void) {\n#ifdef LIVING_ROM\n    living_hook_area_unloading();\n#endif\n    clear_objects();\n"),

    ("src/game/level_update.c", '#include "rumble_init.h"\n', '#include "rumble_init.h"\n' + INC),
    ("src/game/level_update.c",
     "    area_update_objects();\n    update_hud_values();\n\n    if (gCurrentArea != NULL) {\n        update_camera(gCurrentArea->camera);\n    }\n\n    initiate_painting_warp();",
     "    area_update_objects();\n#ifdef LIVING_ROM\n    living_hook_update();\n#endif\n    update_hud_values();\n\n"
     "    if (gCurrentArea != NULL) {\n        update_camera(gCurrentArea->camera);\n    }\n\n    initiate_painting_warp();"),
    ("src/game/level_update.c",
     "s16 level_trigger_warp(struct MarioState *m, s32 warpOp) {\n    s32 val04 = TRUE;\n",
     "s16 level_trigger_warp(struct MarioState *m, s32 warpOp) {\n"
     "    s32 val04 = TRUE;\n"
     "#ifdef LIVING_ROM\n    if (warpOp == WARP_OP_DEATH || warpOp == WARP_OP_WARP_FLOOR) {\n        living_hook_death();\n    }\n#endif\n"),

    ("src/game/save_file.c", '#include "rumble_init.h"\n', '#include "rumble_init.h"\n' + INC),
    ("src/game/save_file.c",
     "    save_main_menu_data();\n}\n\nvoid save_file_erase(s32 fileIndex) {",
     "    save_main_menu_data();\n#ifdef LIVING_ROM\n    living_hook_game_saved();\n#endif\n}\n\nvoid save_file_erase(s32 fileIndex) {"),

    # libultra: accept a 16 Kbit EEPROM (status type 0xC000) and its 256 blocks.
    ("lib/src/osEepromProbe.c",
     "    ret = (ret == 0 && (status.type & CONT_EEPROM) != 0) ? EEPROM_TYPE_4K : 0;\n",
     "#ifdef LIVING_ROM\n    ret = (ret == 0 && (status.type & CONT_EEPROM) != 0)\n"
     "              ? ((status.type & CONT_EEP16K) ? EEPROM_TYPE_16K : EEPROM_TYPE_4K) : 0;\n#else\n"
     "    ret = (ret == 0 && (status.type & CONT_EEPROM) != 0) ? EEPROM_TYPE_4K : 0;\n#endif\n"),
]

# osEepromRead/Write: the US libultra rejects address > 64 and type != CONT_EEPROM exactly.
for f in ("lib/src/osEepromRead.c", "lib/src/osEepromWrite.c"):
    PATCHES.append((f, "    if (address > EEPROM_MAXBLOCKS) {\n        return -1;\n    }\n",
                    "#ifndef LIVING_ROM\n    if (address > EEPROM_MAXBLOCKS) {\n        return -1;\n    }\n#endif\n"))
    PATCHES.append((f, "sdata.type != CONT_EEPROM", "LIVING_EEP_TYPE_BAD(sdata.type)"))
    PATCHES.append((f, '#include "libultra_internal.h"\n',
                    '#include "libultra_internal.h"\n'
                    "#ifdef LIVING_ROM\n#define LIVING_EEP_TYPE_BAD(t) (((t) & CONT_EEPROM) == 0)\n#else\n"
                    "#define LIVING_EEP_TYPE_BAD(t) ((t) != CONT_EEPROM)\n#endif\n"))


def main():
    if not os.path.isfile(os.path.join(ROOT, "Makefile")):
        sys.exit(f"not a decomp checkout: {ROOT}")
    for rel, text in WRAPPERS.items():
        p = os.path.join(ROOT, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w", newline="\n") as fh:
            fh.write(text)
    done = skipped = 0
    for rel, old, new in PATCHES:
        p = os.path.join(ROOT, rel)
        with open(p, encoding="utf-8", newline="") as fh:
            s = fh.read()
        if "\r\n" in s:
            sys.exit(f"{rel} has CRLF line endings: re-checkout with core.autocrlf=false")
        if new in s:
            skipped += 1
            continue
        n = s.count(old)
        if n != 1:
            sys.exit(f"anchor not unique in {rel} ({n} matches): {old[:70]!r}")
        s = s.replace(old, new)
        with open(p, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(s)
        done += 1
    print(f"living hooks: {done} applied, {skipped} already present, root={ROOT}")


if __name__ == "__main__":
    main()
