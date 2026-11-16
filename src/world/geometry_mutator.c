/*
 * Geometry Mutator: builds structures out of existing collision modules
 * (SM64 breakable boxes, 200 x 200 x 200, origin at bottom centre).
 * Every column must stand on flat, safe floor with enough head room for the
 * whole stack plus Mario. Breakable boxes guarantee the player can never be
 * trapped by a structure.
 */
#include "mutation_engine.h"

#define LGEO_BOX 200
#define LGEO_MARIO_HEIGHT 160
#define LGEO_FLAT_TOLERANCE 40

u8 lgeo_build(LivingMut *m, u8 tpl, const LVec3 *origin, u8 dir) {
    const LBoxCell *cells;
    u8 n, i;
    LVec3 base = *origin;
    s16 baseY;
    s16 yaw;

    cells = lg_template(tpl, &n);
    if (cells == 0 || n == 0) {
        return 0;
    }
    /* the structure origin itself must be reachable from the anchor height */
    if (!lme_ground(m, &base, origin->y, 150)) {
        return 0;
    }
    baseY = base.y;
    yaw = (s16) ((u16) dir * 0x2000);

    for (i = 0; i < n; i++) {
        LVec3 p;
        LFloor fl;
        s32 top;

        lg_offset(&base, dir, cells[i].fwd * LGEO_BOX, cells[i].side * LGEO_BOX, &p);
        if (!lme_ground(m, &p, baseY, LGEO_FLAT_TOLERANCE)) {
            return 0;
        }
        /* head room for the full column at this cell */
        top = (s32) p.y + (cells[i].up + 1) * LGEO_BOX;
        if (!lw_probe(p.x, (s16) (p.y + 20), p.z, &fl) || (s32) fl.ceilY < top + LGEO_MARIO_HEIGHT) {
            m->validationFailures++;
            m->lastFail = LAR_GEOMETRY;
            return 0;
        }
        /* the column must be empty space: probing from above it must land on the same floor,
         * otherwise there is solid terrain (a ledge, a plateau) inside the column */
        if (!lw_probe(p.x, (s16) LIVING_MIN(top + 100, LIVING_SAFE_MAX), p.z, &fl)
            || fl.floorY > p.y + LGEO_FLAT_TOLERANCE) {
            m->validationFailures++;
            m->lastFail = LAR_GEOMETRY;
            return 0;
        }
        p.y = (s16) (p.y + cells[i].up * LGEO_BOX);
        if (!lg_in_bounds(p.x, top, p.z)) {
            m->validationFailures++;
            m->lastFail = LAR_GEOMETRY;
            return 0;
        }
        if (lme_spawn(m, LK_BOX, &p, yaw) == LH_INVALID) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Level-designer structures: built from what the terrain around the player   */
/* actually looks like, so they read as part of the level, not as clutter.    */
/* ------------------------------------------------------------------------- */

#define LGEO_GAP_DROP 300   /* a drop deeper than this counts as a gap */

/* Level-textured blocks when this level's textures are known, cork boxes otherwise. */
static u8 lgeo_kind(void) {
    return (u8) (lw_kind_available(LK_TBLOCK) ? LK_TBLOCK : LK_BOX);
}

/* A stack of `count` boxes on flat, safe floor at `at` (x/z), validated like lgeo_build. */
static s32 lgeo_stack(LivingMut *m, const LVec3 *at, s16 baseY, u8 count, s16 yaw) {
    LVec3 p = *at;
    LFloor fl;
    s32 top;
    u8 k;

    if (!lme_ground(m, &p, baseY, LGEO_FLAT_TOLERANCE)) {
        return 0;
    }
    top = (s32) p.y + count * LGEO_BOX;
    if (!lw_probe(p.x, (s16) (p.y + 20), p.z, &fl) || (s32) fl.ceilY < top + LGEO_MARIO_HEIGHT
        || !lw_probe(p.x, (s16) LIVING_MIN(top + 100, LIVING_SAFE_MAX), p.z, &fl)
        || fl.floorY > p.y + LGEO_FLAT_TOLERANCE || !lg_in_bounds(p.x, top, p.z)) {
        m->validationFailures++;
        m->lastFail = LAR_GEOMETRY;
        return 0;
    }
    for (k = 0; k < count; k++) {
        LVec3 q = p;
        q.y = (s16) (p.y + k * LGEO_BOX);
        if (lme_spawn(m, lgeo_kind(), &q, yaw) == LH_INVALID) {
            return 0;
        }
    }
    return 1;
}

/*
 * Staircase up to a real ledge: look around the anchor for higher, walkable ground
 * 250..800 units above, close by, and build steps that end right below its edge.
 * The result is a new route the level did not have.
 */
u8 lgeo_stairs_to_ledge(LivingMut *m, const LVec3 *anchor, u8 dirHint) {
    u8 di;

    for (di = 0; di < 8; di++) {
        u8 dir = (u8) ((dirHint + di) & 7);
        s32 d;

        for (d = 450; d <= 1050; d += 150) {
            LVec3 q;
            LFloor fl;
            s32 h, n, k;
            s16 yaw = (s16) ((u16) dir * 0x2000);

            lg_offset(anchor, dir, d, 0, &q);
            if (!lw_probe(q.x, (s16) LIVING_MIN(anchor->y + 1100, LIVING_SAFE_MAX), q.z, &fl)
                || fl.surf != LS_SAFE || fl.normalY100 < 70) {
                continue;
            }
            h = (s32) fl.floorY - anchor->y;
            if (h < 50) {
                continue; /* still at our level: keep looking further out */
            }
            if (h < 250 || h > 800) {
                break; /* a step Mario can just jump, or a cliff too tall: not this way */
            }
            n = (h + LGEO_BOX - 1) / LGEO_BOX; /* steps so the last top reaches the ledge */
            for (k = 1; k <= n; k++) {
                LVec3 c;
                lg_offset(anchor, dir, d - (n - k + 1) * LGEO_BOX, 0, &c);
                if (!lgeo_stack(m, &c, anchor->y, (u8) k, yaw)) {
                    break;
                }
            }
            if (k > n) {
                return 1;
            }
            lme_rollback_op(m); /* undo the partial stairs, try another direction */
            break;
        }
    }
    m->lastFail = LAR_GEOMETRY;
    return 0;
}

/*
 * Bridge across a gap, a pit or a hazard: walk outwards until the floor drops away,
 * then look for floor at the same height on the far side, and lay boxes whose tops
 * are flush with the ground on both sides. Boxes do not fall in SM64.
 */
u8 lgeo_bridge(LivingMut *m, const LVec3 *anchor, u8 dirHint) {
    u8 di;

    for (di = 0; di < 8; di++) {
        u8 dir = (u8) ((dirHint + di) & 7);
        s32 d, edge = -1, far = -1, b, boxes;
        s16 y = (s16) (anchor->y - LGEO_BOX);
        s16 yaw = (s16) ((u16) dir * 0x2000);
        LFloor fl;
        LVec3 q;

        for (d = 200; d <= 900; d += 100) {
            lg_offset(anchor, dir, d, 0, &q);
            if (!lw_probe(q.x, (s16) (anchor->y + 100), q.z, &fl) || fl.surf != LS_SAFE
                || fl.floorY < anchor->y - LGEO_GAP_DROP) {
                edge = d;
                break;
            }
            if (fl.floorY > anchor->y + 100) {
                break; /* a wall or a ledge, not a gap */
            }
        }
        if (edge < 0) {
            continue;
        }
        for (d = edge + 200; d <= edge + 1400; d += 100) {
            lg_offset(anchor, dir, d, 0, &q);
            if (lw_probe(q.x, (s16) (anchor->y + 100), q.z, &fl) && fl.surf == LS_SAFE
                && fl.normalY100 >= 70 && LIVING_ABS((s32) fl.floorY - anchor->y) <= 60) {
                far = d;
                break;
            }
        }
        if (far < 0) {
            continue;
        }
        boxes = (far - edge + LGEO_BOX - 1) / LGEO_BOX + 1;
        if (boxes > 8 || !lme_can_spawn(m, lgeo_kind(), (u8) boxes)) {
            continue;
        }
        for (b = 0; b < boxes; b++) {
            lg_offset(anchor, dir, edge - 100 + b * LGEO_BOX, 0, &q);
            q.y = y;
            /* the space the box fills must be empty: any floor found there must be below it */
            if ((lw_probe(q.x, (s16) (y + LGEO_BOX - 10), q.z, &fl) && fl.floorY > y - 20
                 && b > 0 && b < boxes - 1)
                || (lw_probe(q.x, (s16) (y + LGEO_BOX + 20), q.z, &fl)
                    && (s32) fl.ceilY < y + LGEO_BOX + LGEO_MARIO_HEIGHT)
                || !lg_in_bounds(q.x, y, q.z)) {
                m->validationFailures++;
                break;
            }
            if (lme_spawn(m, lgeo_kind(), &q, yaw) == LH_INVALID) {
                break;
            }
        }
        if (b == boxes) {
            return 1;
        }
        lme_rollback_op(m);
    }
    m->lastFail = LAR_GEOMETRY;
    return 0;
}

/* A 2 x 2 raised platform (2 or 3 boxes high) with a reward on top. Searches 8 directions
 * x 3 distances for a patch of flat ground big enough, instead of trusting one spot. */
u8 lgeo_tower(LivingMut *m, const LVec3 *anchor, u8 dir, u8 variant) {
    static const s8 sFoot[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
    static const s16 sDist[3] = { 500, 750, 1000 };
    u8 height = (u8) ((variant & 1) ? 3 : 2);
    u8 di, dk;

    if (!lme_can_spawn(m, lgeo_kind(), (u8) (4 * height))) {
        return 0;
    }
    for (di = 0; di < 8; di++) {
        u8 d = (u8) ((dir + di) & 7);
        s16 yaw = (s16) ((u16) d * 0x2000);

        for (dk = 0; dk < 3; dk++) {
            LVec3 base, c, top;
            s32 i;
            u8 ok = 1;

            lg_offset(anchor, d, sDist[dk], 0, &base);
            if (!lme_ground(m, &base, anchor->y, 150)) {
                continue;
            }
            for (i = 0; i < 4 && ok; i++) {
                lg_offset(&base, d, sFoot[i][0] * LGEO_BOX, sFoot[i][1] * LGEO_BOX, &c);
                c.y = base.y;
                ok = (u8) lgeo_stack(m, &c, base.y, height, yaw);
            }
            if (!ok) {
                lme_rollback_op(m);
                continue;
            }
            /* reward in the middle of the top: a 1-up if this level has it, else a coin */
            lg_offset(&base, d, LGEO_BOX / 2, LGEO_BOX / 2, &top);
            top.y = (s16) (base.y + height * LGEO_BOX + 30);
            lme_spawn(m, lw_kind_available(LK_1UP) ? LK_1UP : LK_COIN, &top, 0);
            return 1;
        }
    }
    m->lastFail = LAR_GEOMETRY;
    return 0;
}

/*
 * A new room: a small building (1040 x 1040, 640 high) in the level's own textures,
 * with a doorway facing the place the player came from and a reward inside.
 * Needs a flat, empty, open patch of ground: 9 floor probes across the footprint,
 * head room, no solid terrain inside the volume, and walkable ground in front of
 * the door. Searches 8 directions x 3 distances.
 */
#define LROOM_HALF 520
#define LROOM_HEIGHT 640

u8 lgeo_room(LivingMut *m, const LVec3 *anchor, u8 dirHint, u8 variant) {
    static const s16 sDist[3] = { 1300, 1700, 2100 };
    static const s8 sGrid[9][2] = { { 0, 0 }, { -1, -1 }, { -1, 0 }, { -1, 1 }, { 0, -1 },
                                    { 0, 1 }, { 1, -1 }, { 1, 0 }, { 1, 1 } };
    LVec3 player;
    u8 di, dk;

    if (!lme_can_spawn(m, LK_ROOM, 1)) {
        return 0;
    }
    lw_player_pos(&player);
    for (di = 0; di < 8; di++) {
        u8 dir = (u8) ((dirHint + di) & 7);

        for (dk = 0; dk < 3; dk++) {
            LVec3 c, p, door;
            LFloor fl;
            s32 g, ok = 1;
            /* local -Z (the doorway) must face back towards the anchor */
            s16 yaw = (s16) (0x4000 - (s32) dir * 0x2000);

            lg_offset(anchor, dir, sDist[dk], 0, &c);
            if (!lme_ground(m, &c, anchor->y, 250)) {
                continue;
            }
            for (g = 0; g < 9 && ok; g++) {
                lg_offset(&c, dir, sGrid[g][0] * (LROOM_HALF - 40), sGrid[g][1] * (LROOM_HALF - 40), &p);
                ok = lw_probe(p.x, (s16) (c.y + 60), p.z, &fl) && fl.surf == LS_SAFE
                     && LIVING_ABS((s32) fl.floorY - c.y) <= 40
                     && (s32) fl.ceilY >= c.y + LROOM_HEIGHT + 100
                     && lw_probe(p.x, (s16) (c.y + LROOM_HEIGHT + 60), p.z, &fl)
                     && fl.floorY <= c.y + 40;
            }
            /* walkable ground in front of the door */
            lg_offset(&c, (u8) (dir + 4), LROOM_HALF + 250, 0, &door);
            if (ok && !lme_ground(m, &door, c.y, 120)) {
                ok = 0;
            }
            if (!ok) {
                m->validationFailures++;
                continue;
            }
            if (lg_dist2_xz(&player, &c) < (s32) (LROOM_HALF + 500) * (LROOM_HALF + 500)) {
                m->lastFail = LAR_TRANSIENT; /* never drop a building on top of Mario */
                return 0;
            }
            if (lme_spawn(m, LK_ROOM, &c, yaw) == LH_INVALID) {
                return 0;
            }
            /* something worth walking in for */
            p = c;
            p.y = (s16) (c.y + 40);
            if ((variant & 3) == 0 || !lw_kind_available(LK_1UP)) {
                lobj_coin_ring(m, &p, 5, 220);
            } else {
                lme_spawn(m, LK_1UP, &p, 0);
            }
            return 1;
        }
    }
    m->lastFail = LAR_GEOMETRY;
    return 0;
}
