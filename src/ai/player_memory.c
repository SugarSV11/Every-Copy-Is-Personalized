#include "player_memory.h"

u16 lm_sat_add16(u16 a, u16 b) {
    u32 s = (u32) a + b;
    return (u16) (s > 0xFFFF ? 0xFFFF : s);
}

u8 lm_sat_add8(u8 a, u8 b) {
    u16 s = (u16) a + b;
    return (u8) (s > 0xFF ? 0xFF : s);
}

void lm_init_fresh(LivingMemory *mem, u32 seed) {
    u8 *p = (u8 *) mem;
    u32 i;

    for (i = 0; i < sizeof(LivingMemory); i++) {
        p[i] = 0;
    }
    mem->seed = seed ? seed : 0x5EED1234;
    mem->mood = LM_DORMANT;
    for (i = 0; i < LOP_COUNT; i++) {
        mem->opAffinity[i] = 128;
    }
    for (i = 0; i < LIVING_MAX_AREAS; i++) {
        mem->areas[i].hotCell = LIVING_CELL_NONE;
        mem->areas[i].deathCell = LIVING_CELL_NONE;
    }
}

LivingAreaMem *lm_area_find(LivingMemory *mem, u8 level, u8 area) {
    s32 i;

    for (i = 0; i < LIVING_MAX_AREAS; i++) {
        if (mem->areas[i].level == level && mem->areas[i].area == area && level != 0) {
            return &mem->areas[i];
        }
    }
    return 0;
}

/*
 * Find or allocate the slot for (level, area). When full, evict the least valuable
 * memory: few visits and long ago. Deterministic (ties -> lowest index).
 */
LivingAreaMem *lm_area_acquire(LivingMemory *mem, u8 level, u8 area) {
    LivingAreaMem *am = lm_area_find(mem, level, area);
    s32 i;
    s32 best = 0;
    s32 bestScore = 0x7FFFFFFF;

    if (am != 0 || level == 0) {
        return am;
    }
    for (i = 0; i < LIVING_MAX_AREAS; i++) {
        s32 score;
        u8 age;

        if (mem->areas[i].level == 0) {
            best = i;
            break;
        }
        age = (u8) ((u8) mem->sessions - mem->areas[i].lastSession);
        score = (s32) mem->areas[i].visits * 4 + mem->areas[i].generation * 2 - (s32) age * 8;
        if (score < bestScore) {
            bestScore = score;
            best = i;
        }
    }
    am = &mem->areas[best];
    {
        u8 *p = (u8 *) am;
        u32 k;
        for (k = 0; k < sizeof(LivingAreaMem); k++) {
            p[k] = 0;
        }
    }
    am->level = level;
    am->area = area;
    am->hotCell = LIVING_CELL_NONE;
    am->deathCell = LIVING_CELL_NONE;
    am->lastSession = (u8) mem->sessions;
    return am;
}

u8 lm_cell_of(s16 x, s16 z) {
    s32 cx = ((s32) x - LIVING_WORLD_MIN) >> LIVING_GRID_SHIFT;
    s32 cz = ((s32) z - LIVING_WORLD_MIN) >> LIVING_GRID_SHIFT;

    cx = LIVING_CLAMP(cx, 0, LIVING_GRID_DIM - 1);
    cz = LIVING_CLAMP(cz, 0, LIVING_GRID_DIM - 1);
    return (u8) (cz * LIVING_GRID_DIM + cx);
}

void lm_cell_center(u8 cell, s16 *x, s16 *z) {
    s32 cx = cell % LIVING_GRID_DIM;
    s32 cz = cell / LIVING_GRID_DIM;

    *x = (s16) (LIVING_WORLD_MIN + (cx << LIVING_GRID_SHIFT) + (1 << (LIVING_GRID_SHIFT - 1)));
    *z = (s16) (LIVING_WORLD_MIN + (cz << LIVING_GRID_SHIFT) + (1 << (LIVING_GRID_SHIFT - 1)));
}

s32 lm_bit_get(const u8 *mask, u8 bit) {
    return (mask[bit >> 3] >> (bit & 7)) & 1;
}

void lm_bit_set(u8 *mask, u8 bit) {
    mask[bit >> 3] |= (u8) (1 << (bit & 7));
}

u8 lm_bit_count(const u8 *mask) {
    u8 n = 0;
    s32 i;

    for (i = 0; i < 8; i++) {
        u8 v = mask[i];
        while (v) {
            v &= (u8) (v - 1);
            n++;
        }
    }
    return n;
}

/* Frontier = unexplored cells 4-adjacent to explored cells: the "rooms the player avoids". */
void lm_frontier(const u8 *explored, u8 *outMask) {
    s32 c;

    for (c = 0; c < 8; c++) {
        outMask[c] = 0;
    }
    for (c = 0; c < LIVING_CELLS; c++) {
        s32 cx = c % LIVING_GRID_DIM;
        s32 cz = c / LIVING_GRID_DIM;

        if (lm_bit_get(explored, (u8) c)) {
            continue;
        }
        if ((cx > 0 && lm_bit_get(explored, (u8) (c - 1)))
            || (cx < LIVING_GRID_DIM - 1 && lm_bit_get(explored, (u8) (c + 1)))
            || (cz > 0 && lm_bit_get(explored, (u8) (c - LIVING_GRID_DIM)))
            || (cz < LIVING_GRID_DIM - 1 && lm_bit_get(explored, (u8) (c + LIVING_GRID_DIM)))) {
            lm_bit_set(outMask, (u8) c);
        }
    }
}
