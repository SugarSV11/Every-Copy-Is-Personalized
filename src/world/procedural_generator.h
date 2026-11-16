/*
 * Deterministic procedural generation: integer-only hashing, PRNG and structure
 * templates. Same (seed, area, generation, slot) -> same world, on every boot.
 */
#ifndef LIVING_PROCEDURAL_GENERATOR_H
#define LIVING_PROCEDURAL_GENERATOR_H

#include "../living_types.h"

typedef struct {
    u32 s;
} LRng;

typedef struct {
    s8 fwd;
    s8 side;
    s8 up;
} LBoxCell;

enum LivingTemplate {
    LTPL_STAIRS,
    LTPL_WALL,
    LTPL_PILLAR,
    LTPL_COUNT
};

u32 lg_hash32(u32 x);
u32 lg_hash_combine(u32 h, u32 v);
u32 lg_op_hash(u32 seed, u8 level, u8 area, u8 generation, u8 slot);
void lrng_seed(LRng *r, u32 seed);
u32 lrng_next(LRng *r);
s32 lrng_range(LRng *r, s32 lo, s32 hi); /* inclusive */

/* 8 compass directions as (dx, dz) scaled by 1024. */
void lg_dir(u8 dir, s32 *dx, s32 *dz);
/* p + dir * forward + perpendicular * side, clamped to the safe world box. */
void lg_offset(const LVec3 *p, u8 dir, s32 forward, s32 side, LVec3 *out);
const LBoxCell *lg_template(u8 tpl, u8 *count);
s32 lg_in_bounds(s32 x, s32 y, s32 z);
s32 lg_dist2_xz(const LVec3 *a, const LVec3 *b);

#endif
