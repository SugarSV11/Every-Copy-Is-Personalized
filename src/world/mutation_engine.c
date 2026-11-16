#include "mutation_engine.h"

void lme_reset(LivingMut *m) {
    u8 *p = (u8 *) m;
    u32 i;

    for (i = 0; i < sizeof(LivingMut); i++) {
        p[i] = 0;
    }
    for (i = 0; i <= LIVING_PLAN_OPS; i++) {
        m->slot[i].targetCell = LIVING_CELL_NONE;
        m->slot[i].result = LAR_NONE;
    }
    m->ambMusic = 100;
    m->minPlayerDist = LIVING_MIN_PLAYER_DIST;
}

void lme_begin_op(LivingMut *m, u8 slot, u8 op) {
    m->curSlot = slot;
    m->opStart = m->count;
    m->slot[slot].op = op;
    m->slot[slot].spawned = 0;
    m->slot[slot].result = LAR_NONE;
    m->lastFail = LAR_GEOMETRY;
}

void lme_commit_op(LivingMut *m) {
    m->slot[m->curSlot].result = LAR_OK;
}

void lme_rollback_op(LivingMut *m) {
    while (m->count > m->opStart) {
        LivingJournalEntry *e = &m->e[--m->count];

        if (e->type == LJ_SPAWNED) {
            lw_despawn(e->h);
            if (m->spawnedThisArea > 0) {
                m->spawnedThisArea--;
            }
        } else if (e->type == LJ_RELOCATED) {
            lw_set_pos(e->h, &e->orig);
            lw_release(e->h);
        } else if (e->type == LJ_REHOMED) {
            lw_set_home(e->h, &e->orig);
            lw_release(e->h);
        } else if (e->type == LJ_SCALED) {
            lw_set_scale(e->h, (u8) e->orig.x);
            lw_release(e->h);
        }
    }
    m->slot[m->curSlot].spawned = 0;
    m->rollbacks++;
}

void lme_forget_all(LivingMut *m) {
    m->count = 0;
    m->opStart = 0;
    lw_release_all();
}

/*
 * Ground validation: probe from slightly above refY + maxStep. The floor must exist,
 * be safe (no lava/death plane/quicksand), be walkable (normal.y >= 0.7), be within
 * maxStep of refY, leave head room, and lie in the safe world box.
 */
s32 lme_ground(LivingMut *m, LVec3 *pos, s16 refY, s16 maxStep) {
    LFloor fl;
    s32 probeY = (s32) refY + maxStep + 50;

    m->lastFail = LAR_GEOMETRY;
    if (!lg_in_bounds(pos->x, probeY, pos->z)) {
        m->validationFailures++;
        return 0;
    }
    if (!lw_probe(pos->x, (s16) probeY, pos->z, &fl) || fl.surf != LS_SAFE || fl.normalY100 < 70
        || fl.floorY < refY - maxStep || fl.floorY > refY + maxStep
        || (s32) fl.ceilY - fl.floorY < 240) {
        m->validationFailures++;
        return 0;
    }
    pos->y = fl.floorY;
    return 1;
}

s32 lme_can_spawn(LivingMut *m, u8 kind, u8 count) {
    if (kind >= LK_COUNT || !lw_kind_available(kind)) {
        m->lastFail = LAR_UNAVAILABLE;
        return 0;
    }
    if (m->count + count > LIVING_MAX_HANDLES || m->spawnedThisArea + count > LIVING_MAX_SPAWN_PER_AREA
        || lw_object_count() + count > LIVING_OBJ_SOFT_LIMIT) {
        m->lastFail = LAR_BUDGET;
        return 0;
    }
    return 1;
}

LHandle lme_spawn(LivingMut *m, u8 kind, const LVec3 *pos, s16 yaw) {
    LivingJournalEntry *e;
    LVec3 player;
    LHandle h;

    if (!lme_can_spawn(m, kind, 1) || !lg_in_bounds(pos->x, pos->y, pos->z)) {
        return LH_INVALID;
    }
    lw_player_pos(&player);
    if (lg_dist2_xz(&player, pos) < (s32) m->minPlayerDist * m->minPlayerDist
        && LIVING_ABS((s32) player.y - pos->y) < 400) {
        m->lastFail = LAR_TRANSIENT; /* Mario is standing there: try again next visit */
        return LH_INVALID;
    }
    h = lw_spawn(kind, pos, yaw);
    if (h == LH_INVALID) {
        m->lastFail = LAR_BUDGET;
        return LH_INVALID;
    }
    e = &m->e[m->count++];
    e->h = h;
    e->kind = kind;
    e->slot = m->curSlot;
    e->type = LJ_SPAWNED;
    e->orig = *pos;
    m->spawnedThisArea++;
    m->slot[m->curSlot].spawned++;
    return h;
}

s32 lme_relocate(LivingMut *m, LHandle h, u8 kind, const LVec3 *to) {
    LivingJournalEntry *e;

    if (h == LH_INVALID || m->count >= LIVING_MAX_HANDLES || !lg_in_bounds(to->x, to->y, to->z)) {
        return 0;
    }
    e = &m->e[m->count++];
    e->h = h;
    e->kind = kind;
    e->slot = m->curSlot;
    e->type = LJ_RELOCATED;
    lw_get_pos(h, &e->orig);
    lw_set_pos(h, to);
    return 1;
}

s32 lme_rehome(LivingMut *m, LHandle h, u8 kind, const LVec3 *home) {
    LivingJournalEntry *e;

    if (h == LH_INVALID || m->count >= LIVING_MAX_HANDLES || !lg_in_bounds(home->x, home->y, home->z)) {
        return 0;
    }
    e = &m->e[m->count++];
    e->h = h;
    e->kind = kind;
    e->slot = m->curSlot;
    e->type = LJ_REHOMED;
    lw_get_home(h, &e->orig);
    lw_set_home(h, home);
    return 1;
}

s32 lme_rescale(LivingMut *m, LHandle h, u8 kind, u8 percent) {
    LivingJournalEntry *e;

    if (h == LH_INVALID || m->count >= LIVING_MAX_HANDLES) {
        return 0;
    }
    e = &m->e[m->count++];
    e->h = h;
    e->kind = kind;
    e->slot = m->curSlot;
    e->type = LJ_SCALED;
    e->orig.x = lw_get_scale(h);
    e->orig.y = 0;
    e->orig.z = 0;
    lw_set_scale(h, percent);
    return 1;
}

u8 lme_slot_alive(const LivingMut *m, u8 slot, u8 type) {
    u8 n = 0;
    s32 i;

    for (i = 0; i < m->count; i++) {
        if (m->e[i].slot == slot && m->e[i].type == type && lw_alive(m->e[i].h)) {
            n++;
        }
    }
    return n;
}

/* Structures adapt to the terrain: if the chosen one has no place here (no gap for a
 * bridge, no ledge for stairs), fall back to the next, ending with a tower, which only
 * needs flat ground. Each attempt starts from a clean slate. */
u8 lme_build_structure(LivingMut *m, u8 first, const LVec3 *anchor, u8 dir, u8 variant) {
    static const u8 sOrder[3] = { LOP_BOX_BRIDGE, LOP_BOX_STAIRS, LOP_BOX_TOWER };
    u8 start = 0;
    u8 k;

    while (start < 3 && sOrder[start] != first) {
        start++;
    }
    for (k = start; k < 3; k++) {
        u8 ok;
        if (k > start) {
            lme_rollback_op(m);
        }
        if (sOrder[k] == LOP_BOX_BRIDGE) {
            ok = lgeo_bridge(m, anchor, dir);
        } else if (sOrder[k] == LOP_BOX_STAIRS) {
            ok = lgeo_stairs_to_ledge(m, anchor, dir);
        } else {
            ok = lgeo_tower(m, anchor, dir, variant);
        }
        if (ok) {
            return 1;
        }
    }
    return 0;
}

static u8 apply_one(LivingMut *m, LivingMemory *mem, LivingAreaMem *am, u8 slot, u8 *reason) {
    u8 op = am->plan[slot].op;
    u8 param = am->plan[slot].param;
    const LVec3 *anchor = &am->anchor[LP_ANCHOR(param) % LIVING_ANCHORS];
    u8 dir = LP_DIR(param);
    u8 variant = LP_VARIANT(param);
    u8 done = 0;
    LVec3 tgt;

    *reason = LAR_GEOMETRY;
    switch (op) {
        case LOP_ROOM:
            if (!lw_kind_available(LK_ROOM)) {
                *reason = LAR_UNAVAILABLE;
                return 0;
            }
            done = lgeo_room(m, anchor, dir, variant);
            break;
        case LOP_BOX_BRIDGE:
            if (!lme_can_spawn(m, LK_BOX, 3) && !lme_can_spawn(m, LK_TBLOCK, 3)) {
                *reason = (lw_kind_available(LK_BOX) || lw_kind_available(LK_TBLOCK)) ? LAR_BUDGET : LAR_UNAVAILABLE;
                return 0;
            }
            done = lme_build_structure(m, LOP_BOX_BRIDGE, anchor, dir, variant);
            break;
        case LOP_BOX_STAIRS:
            if (!lme_can_spawn(m, LK_BOX, 10) && !lme_can_spawn(m, LK_TBLOCK, 10)) {
                *reason = (lw_kind_available(LK_BOX) || lw_kind_available(LK_TBLOCK)) ? LAR_BUDGET : LAR_UNAVAILABLE;
                return 0;
            }
            done = lme_build_structure(m, LOP_BOX_STAIRS, anchor, dir, variant);
            break;
        case LOP_BOX_WALL:
            if (!lme_can_spawn(m, LK_BOX, 4)) {
                *reason = lw_kind_available(LK_BOX) ? LAR_BUDGET : LAR_UNAVAILABLE;
                return 0;
            }
            lg_offset(anchor, dir, 500, 0, &tgt);
            done = lgeo_build(m, LTPL_WALL, &tgt, dir);
            break;
        case LOP_BOX_TOWER:
            if (!lme_can_spawn(m, LK_BOX, 8) && !lme_can_spawn(m, LK_TBLOCK, 8)) {
                *reason = (lw_kind_available(LK_BOX) || lw_kind_available(LK_TBLOCK)) ? LAR_BUDGET : LAR_UNAVAILABLE;
                return 0;
            }
            done = lgeo_tower(m, anchor, dir, variant);
            break;
        case LOP_ENEMY_AMBUSH:
            if (!lw_kind_available(LK_GOOMBA)) {
                *reason = LAR_UNAVAILABLE;
                return 0;
            }
            if (!lme_can_spawn(m, LK_GOOMBA, 2)) {
                *reason = LAR_BUDGET;
                return 0;
            }
            done = lobj_ambush(m, anchor, dir, variant);
            break;
        case LOP_ENEMY_ROUTINE:
            done = lobj_reroute_enemies(m, anchor, 3);
            if (!done) {
                *reason = LAR_UNAVAILABLE;
            }
            break;
        case LOP_GIFT:
            done = lobj_gift(m, anchor, dir, variant);
            break;
        case LOP_SECRET:
            if (am->flags & LAF_SECRET_TAKEN) {
                *reason = LAR_NONE; /* already found: nothing to hide any more */
                return 0;
            }
            done = lobj_secret(m, anchor, dir);
            break;
        case LOP_AMBIENCE:
            /* palette: variant 0-2 dusk, 3-5 cold, 6-7 deep */
            m->ambPal = (u8) (variant / 3);
            m->ambAlpha = (u8) (40 + (mem->trait[LT_PATIENCE] > 0 ? mem->trait[LT_PATIENCE] / 2 : 0));
            /* subtle on purpose: a slightly quieter, slightly slower song, and/or the
             * trees of this place a bit taller or shorter than they should be */
            m->ambMusic = (u8) (mem->mood == LM_UNSETTLING ? 70 : 90);
            lw_fx(LFX_MUSIC, m->ambMusic, 0);
            if (variant & 1) {
                lw_fx(LFX_TEMPO, (u8) (mem->mood == LM_UNSETTLING ? 82 : 90), 0);
            }
            if (variant & 2) {
                lobj_rescale_trees(m, (u8) ((variant & 4) ? 125 : 80), 4);
            }
            done = 1;
            break;
        default:
            *reason = LAR_NONE;
            return 0;
    }
    if (done) {
        lg_offset(anchor, dir, 1200, 0, &tgt);
        m->slot[slot].targetCell = lm_cell_of(tgt.x, tgt.z);
    } else if (*reason == LAR_GEOMETRY) {
        *reason = m->lastFail; /* the primitive knows whether it was geometry or circumstance */
    }
    return done;
}

s32 lme_apply_slot(LivingMut *m, LivingMemory *mem, LivingAreaMem *am, u8 i) {
    u8 op = am->plan[i].op;
    u8 reason;

    if (op == LOP_NONE || (op & LOP_REJECTED_BIT) || op >= LOP_COUNT) {
        return 0;
    }
    lme_begin_op(m, i, op);
    if (apply_one(m, mem, am, i, &reason)) {
        lme_commit_op(m);
        mem->mutApplied = lm_sat_add16(mem->mutApplied, 1);
        return 1;
    }
    lme_rollback_op(m);
    m->slot[i].result = reason;
    if (reason == LAR_GEOMETRY || reason == LAR_UNAVAILABLE) {
        am->plan[i].op |= LOP_REJECTED_BIT; /* persisted: never retried */
        mem->mutRejected = lm_sat_add16(mem->mutRejected, 1);
    }
    return 0;
}

u8 lme_apply_plan(LivingMut *m, LivingMemory *mem, LivingAreaMem *am) {
    u8 applied = 0;
    u8 i;

    for (i = 0; i < LIVING_PLAN_OPS; i++) {
        applied = (u8) (applied + lme_apply_slot(m, mem, am, i));
    }
    return applied;
}

/* Ops that failed for circumstantial reasons (Mario on the spot, pool full) are retried
 * while the player plays, rate-limited by the caller. Returns ops applied. */
u8 lme_retry_transient(LivingMut *m, LivingMemory *mem, LivingAreaMem *am) {
    u8 applied = 0;
    u8 i;

    for (i = 0; i < LIVING_PLAN_OPS; i++) {
        if (m->slot[i].result == LAR_TRANSIENT || m->slot[i].result == LAR_BUDGET) {
            applied = (u8) (applied + lme_apply_slot(m, mem, am, i));
        }
    }
    return applied;
}
