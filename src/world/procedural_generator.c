#include "procedural_generator.h"

u32 lg_hash32(u32 x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

u32 lg_hash_combine(u32 h, u32 v) {
    return lg_hash32(h ^ (v + 0x9E3779B9u + (h << 6) + (h >> 2)));
}

u32 lg_op_hash(u32 seed, u8 level, u8 area, u8 generation, u8 slot) {
    u32 h = lg_hash32(seed);
    h = lg_hash_combine(h, ((u32) level << 8) | area);
    h = lg_hash_combine(h, generation);
    h = lg_hash_combine(h, slot);
    return h;
}

void lrng_seed(LRng *r, u32 seed) {
    r->s = lg_hash32(seed) | 1u;
}

u32 lrng_next(LRng *r) {
    u32 x = r->s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->s = x;
    return x;
}

s32 lrng_range(LRng *r, s32 lo, s32 hi) {
    u32 span;

    if (hi <= lo) {
        return lo;
    }
    span = (u32) (hi - lo + 1);
    return lo + (s32) (lrng_next(r) % span);
}

/* cos/sin * 1024 for 0, 45, 90 ... 315 degrees */
static const s16 sDirX[8] = { 1024, 724, 0, -724, -1024, -724, 0, 724 };
static const s16 sDirZ[8] = { 0, 724, 1024, 724, 0, -724, -1024, -724 };

void lg_dir(u8 dir, s32 *dx, s32 *dz) {
    *dx = sDirX[dir & 7];
    *dz = sDirZ[dir & 7];
}

void lg_offset(const LVec3 *p, u8 dir, s32 forward, s32 side, LVec3 *out) {
    s32 fx, fz, sx, sz, x, z;

    lg_dir(dir, &fx, &fz);
    lg_dir((u8) (dir + 2), &sx, &sz); /* +90 degrees */
    x = p->x + ((fx * forward + sx * side) >> 10);
    z = p->z + ((fz * forward + sz * side) >> 10);
    out->x = (s16) LIVING_CLAMP(x, LIVING_SAFE_MIN, LIVING_SAFE_MAX);
    out->z = (s16) LIVING_CLAMP(z, LIVING_SAFE_MIN, LIVING_SAFE_MAX);
    out->y = p->y;
}

/* Structure modules built from 200-unit breakable boxes (SM64 breakable_box collision is 200^3). */
static const LBoxCell sStairs[] = {
    { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 2, 0, 0 }, { 2, 0, 1 }, { 2, 0, 2 }
};
static const LBoxCell sWall[] = {
    { 0, -1, 0 }, { 0, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }
};
static const LBoxCell sPillar[] = {
    { 0, 0, 0 }, { 0, 0, 1 }, { 0, 0, 2 }
};

const LBoxCell *lg_template(u8 tpl, u8 *count) {
    switch (tpl) {
        case LTPL_STAIRS:
            *count = sizeof(sStairs) / sizeof(sStairs[0]);
            return sStairs;
        case LTPL_WALL:
            *count = sizeof(sWall) / sizeof(sWall[0]);
            return sWall;
        case LTPL_PILLAR:
            *count = sizeof(sPillar) / sizeof(sPillar[0]);
            return sPillar;
        default:
            *count = 0;
            return 0;
    }
}

s32 lg_in_bounds(s32 x, s32 y, s32 z) {
    return x >= LIVING_SAFE_MIN && x <= LIVING_SAFE_MAX && z >= LIVING_SAFE_MIN && z <= LIVING_SAFE_MAX
           && y >= LIVING_SAFE_MIN && y <= LIVING_SAFE_MAX;
}

s32 lg_dist2_xz(const LVec3 *a, const LVec3 *b) {
    s32 dx = (s32) a->x - b->x;
    s32 dz = (s32) a->z - b->z;
    /* stay in s32: distances are clamped to the 16000-unit world */
    return dx * dx + dz * dz;
}
