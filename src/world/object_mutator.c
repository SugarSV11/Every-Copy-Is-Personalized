/*
 * Object / NPC Mutator: coins, enemies, gifts and secrets.
 * Positions come from anchors the player has physically stood on, offset
 * deterministically and re-validated against the real collision every time.
 */
#include "mutation_engine.h"

#define LOBJ_COIN_LIFT 20
#define LOBJ_TRAIL_STEP 170
#define LOBJ_TRAIL_MAX_STEP 220 /* max height change between consecutive coins */

u8 lobj_coin_trail(LivingMut *m, const LVec3 *from, u8 dir, u8 maxCoins) {
    s16 prevY = from->y;
    u8 placed = 0;
    u8 i;

    for (i = 1; i <= maxCoins; i++) {
        LVec3 p;

        lg_offset(from, dir, (s32) i * LOBJ_TRAIL_STEP + 200, 0, &p);
        if (!lme_ground(m, &p, prevY, LOBJ_TRAIL_MAX_STEP)) {
            break; /* the path ends where the floor ends: never lure into a pit */
        }
        prevY = p.y;
        p.y = (s16) (p.y + LOBJ_COIN_LIFT);
        if (lme_spawn(m, LK_COIN, &p, 0) == LH_INVALID) {
            break;
        }
        placed++;
    }
    return (u8) (placed >= 3);
}

u8 lobj_relocate_coins(LivingMut *m, const LVec3 *from, u8 dir, u8 maxCoins) {
    s16 prevY = from->y;
    u8 moved = 0;
    u8 nth = 0;
    u8 i;

    for (i = 1; i <= maxCoins; i++) {
        LVec3 p;
        LHandle h;

        lg_offset(from, dir, (s32) i * LOBJ_TRAIL_STEP + 200, 0, &p);
        if (!lme_ground(m, &p, prevY, LOBJ_TRAIL_MAX_STEP)) {
            break;
        }
        prevY = p.y;
        p.y = (s16) (p.y + LOBJ_COIN_LIFT);
        h = lw_find_existing(LK_COIN, nth++);
        if (h == LH_INVALID || !lme_relocate(m, h, LK_COIN, &p)) {
            break;
        }
        moved++;
    }
    return (u8) (moved >= 2);
}

u8 lobj_ambush(LivingMut *m, const LVec3 *at, u8 dir, u8 variant) {
    u8 spawned = 0;
    LVec3 p;

    if ((variant & 3) == 0 && lw_kind_available(LK_GOOMBA_HUGE)) {
        lg_offset(at, dir, 450, 0, &p);
        if (lme_ground(m, &p, at->y, 150) && lme_spawn(m, LK_GOOMBA_HUGE, &p, 0) != LH_INVALID) {
            spawned++;
        }
    } else {
        s32 side;
        for (side = -250; side <= 250; side += 500) {
            lg_offset(at, dir, 400, side, &p);
            if (lme_ground(m, &p, at->y, 150)
                && lme_spawn(m, (variant & 4) ? LK_GOOMBA_TINY : LK_GOOMBA, &p, 0) != LH_INVALID) {
                spawned++;
            }
        }
    }
    return (u8) (spawned > 0);
}

/* NPC routine change: existing enemies adopt the player's favourite place as their patrol home. */
u8 lobj_reroute_enemies(LivingMut *m, const LVec3 *home, u8 maxEnemies) {
    u8 moved = 0;
    u8 i;

    for (i = 0; i < maxEnemies; i++) {
        LHandle h = lw_find_existing(LK_GOOMBA, i);
        LVec3 p;

        if (h == LH_INVALID) {
            break;
        }
        lg_offset(home, (u8) (i * 3), 300, 0, &p);
        if (!lme_ground(m, &p, home->y, 200) || !lme_rehome(m, h, LK_GOOMBA, &p)) {
            lw_release(h);
            continue;
        }
        moved++;
    }
    return (u8) (moved > 0);
}

u8 lobj_gift(LivingMut *m, const LVec3 *at, u8 dir, u8 variant) {
    u8 kind = (variant >= 5 && lw_kind_available(LK_1UP)) ? LK_1UP : LK_HEART;
    LVec3 p;

    lg_offset(at, dir, 300, 0, &p);
    if (!lme_ground(m, &p, at->y, 150)) {
        return 0;
    }
    p.y = (s16) (p.y + (kind == LK_HEART ? 80 : 0));
    return (u8) (lme_spawn(m, kind, &p, 0) != LH_INVALID);
}

/* A 1-up hidden deep in avoided space (bhv1Up is only visible within 3000 units),
 * with a single coin half-way as a breadcrumb. */
u8 lobj_secret(LivingMut *m, const LVec3 *from, u8 dir) {
    static const s16 sDist[5] = { 1500, 1200, 1800, 900, 2100 };
    LVec3 p, mid;
    s32 i;

    for (i = 0; i < 5; i++) {
        lg_offset(from, dir, sDist[i], 0, &p);
        if (lme_ground(m, &p, from->y, 600)) {
            if (lme_spawn(m, LK_1UP, &p, 0) == LH_INVALID) {
                return 0;
            }
            lg_offset(from, dir, sDist[i] / 2, 0, &mid);
            if (lme_ground(m, &mid, from->y, 600)) {
                mid.y = (s16) (mid.y + LOBJ_COIN_LIFT);
                lme_spawn(m, LK_COIN, &mid, 0); /* optional hint */
            }
            return 1;
        }
    }
    return 0;
}

u8 lobj_coin_ring(LivingMut *m, const LVec3 *center, u8 count, s16 radius) {
    u8 placed = 0;
    u8 i;

    if (count > 8) {
        count = 8;
    }
    for (i = 0; i < count; i++) {
        LVec3 p;

        lg_offset(center, i, radius, 0, &p);
        if (lme_ground(m, &p, center->y, 200)) {
            p.y = (s16) (p.y + LOBJ_COIN_LIFT);
            if (lme_spawn(m, LK_COIN, &p, 0) != LH_INVALID) {
                placed++;
            }
        }
    }
    return placed;
}

/* Rescale a few of the level's own trees (never more than 4, never extreme). */
u8 lobj_rescale_trees(LivingMut *m, u8 percent, u8 maxTrees) {
    u8 n = 0;
    u8 i;

    percent = (u8) LIVING_CLAMP(percent, 70, 140);
    for (i = 0; i < maxTrees; i++) {
        LHandle h = lw_find_existing(LK_TREE, i);
        if (h == LH_INVALID) {
            break;
        }
        if (lme_rescale(m, h, LK_TREE, (u8) (percent + (i & 1) * 10))) {
            n++;
        }
    }
    return n;
}
