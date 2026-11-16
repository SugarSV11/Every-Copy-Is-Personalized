#include "decision_engine.h"
#include "../world/procedural_generator.h"
#include "../living_world.h"

/* Trait weights: CUR, PLAY, BENEV, MENACE, PATIENCE, ATTACH */
static const s8 sTraitW[LOP_COUNT][LT_COUNT] = {
    /* NONE      */ { 0, 0, 0, 0, 0, 0 },
    /* ROOM      */ { 30, 10, 5, 0, 10, 20 },
    /* BRIDGE    */ { 35, 20, 10, 0, 0, 5 },
    /* STAIRS    */ { 30, 35, 0, 5, -5, 5 },
    /* WALL      */ { -5, 10, -10, 35, 0, 5 },
    /* TOWER     */ { 15, 25, 10, 0, 10, 10 },
    /* AMBUSH    */ { -5, 0, -30, 40, -10, 0 },
    /* ROUTINE   */ { 10, 10, -10, 25, 0, 5 },
    /* GIFT      */ { 0, 0, 40, -25, 5, 5 },
    /* AMBIENCE  */ { 0, -5, 0, 10, 30, 15 },
    /* SECRET    */ { 30, 10, 5, 0, 5, 20 },
};

/* Feature weights: EXPLORE ACROBAT STRUGGLE DOMINANCE IDLE CAMERA REPEAT AVOID RETURN FAMILIAR */
static const s8 sFeatW[LOP_COUNT][LF_COUNT] = {
    /* NONE      */ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    /* ROOM      */ { 15, 0, 0, 0, 0, 0, 0, 20, 10, 10 },
    /* BRIDGE    */ { 20, 10, 0, 0, 0, 0, 0, 25, 0, 0 },
    /* STAIRS    */ { 20, 30, 0, 10, 0, 0, 0, 10, 0, 0 },
    /* WALL      */ { 0, 0, 0, 10, 0, 0, 50, 0, 0, 0 },
    /* TOWER     */ { 0, 0, 10, 0, 10, 0, 0, 0, 20, 0 },
    /* AMBUSH    */ { 0, 0, -40, 30, 0, 0, 0, 0, 0, 0 },
    /* ROUTINE   */ { 0, 0, 0, 0, 0, 0, 20, 0, 15, 0 },
    /* GIFT      */ { 0, 0, 60, -20, 0, 0, 0, 0, 0, 0 },
    /* AMBIENCE  */ { 0, 0, 0, 0, 25, 15, 0, 0, 0, 10 },
    /* SECRET    */ { 10, 0, 0, 0, 0, 0, 0, 30, 0, 15 },
};

/* Mood bias per op */
static const s8 sMoodBias[LM_COUNT][LOP_COUNT] = {
    /* DORMANT    */ { 0, -99, -99, -99, -99, -99, -99, -99, -99, -99, -99 },
    /* OBSERVING  */ { 0, -15, -15, -15, -15, -15, -15, -15, -15, 10, -15 },
    /* CURIOUS    */ { 0, 0, 20, 15, -5, 5, -10, 0, 0, 0, 15 },
    /* PLAYFUL    */ { 0, 0, 10, 20, 0, 15, -5, 5, 0, 0, 5 },
    /* PROTECTIVE */ { 0, 5, 0, 0, -15, 0, -30, -10, 25, 0, 5 },
    /* TESTING    */ { 0, -5, 0, 0, 15, 0, 20, 10, -20, 0, 0 },
    /* UNSETTLING */ { 0, 0, 0, 0, 5, 0, 0, 10, 0, 25, 5 },
};

const char *ld_op_name(u8 op) {
    static const char *const names[LOP_COUNT] = {
        "NONE", "ROOM", "BOX_BRIDGE", "BOX_STAIRS", "BOX_WALL", "BOX_TOWER",
        "ENEMY_AMBUSH", "ENEMY_ROUTINE", "GIFT", "AMBIENCE", "SECRET"
    };
    op &= (u8) ~LOP_REJECTED_BIT;
    return op < LOP_COUNT ? names[op] : "?";
}

static s32 plan_had(const LivingAreaMem *am, u8 op, s32 rejectedOnly) {
    s32 i;

    for (i = 0; i < LIVING_PLAN_OPS; i++) {
        u8 o = am->plan[i].op;
        if ((o & (u8) ~LOP_REJECTED_BIT) == op) {
            if (!rejectedOnly || (o & LOP_REJECTED_BIT)) {
                return 1;
            }
        }
    }
    return 0;
}

void ld_score_ops(const LivingMemory *mem, const LivingAreaMem *am, const LivingFeatures *f,
                  const u8 *frontier, s16 *scores) {
    s32 op, k;
    s32 hasFrontier = lm_bit_count(frontier) > 0;
    s32 hasAnchors = (am->flags & LAF_HAS_ANCHORS) != 0;
    u32 h = lg_op_hash(mem->seed, am->level, am->area, am->generation, 0xEE);
    u8 mood = mem->mood < LM_COUNT ? mem->mood : LM_OBSERVING;
    s32 boxes = lw_kind_available(LK_BOX) || lw_kind_available(LK_TBLOCK);
    s32 rooms = lw_kind_available(LK_ROOM);

    scores[LOP_NONE] = LD_SCORE_INVALID;
    for (op = 1; op < LOP_COUNT; op++) {
        s32 s = 0;

        for (k = 0; k < LT_COUNT; k++) {
            s += sTraitW[op][k] * mem->trait[k] / 100;
        }
        for (k = 0; k < LF_COUNT; k++) {
            s += sFeatW[op][k] * f->f[k] / 100;
        }
        s += sMoodBias[mood][op];
        s += ((s32) mem->opAffinity[op] - 128) / 3;
        if (plan_had(am, (u8) op, 1)) {
            s -= 30; /* it did not fit here last time */
        } else if (plan_had(am, (u8) op, 0)) {
            s -= 15; /* novelty: prefer showing something new */
        }
        s += (s32) ((h >> (op * 2)) & 3); /* deterministic tie-break, never decisive */
        s += 25; /* baseline so neutral personalities still act */
        /* when this level has the box modules loaded, building is what the AI does best:
         * structures read as level design; loose coins read as clutter */
        if (boxes && (op == LOP_BOX_BRIDGE || op == LOP_BOX_STAIRS || op == LOP_BOX_TOWER)) {
            s += 30;
        }
        /* a new building is the AI's signature piece: at most one per area plan, and only
         * where this level's textures are known */
        if (op == LOP_ROOM) {
            s += rooms ? 25 : 0;
            if (!rooms) {
                s = LD_SCORE_INVALID;
            }
        }

        /* hard preconditions */
        if (op != LOP_AMBIENCE && !hasAnchors) {
            s = LD_SCORE_INVALID;
        }
        if (op == LOP_SECRET && !hasFrontier) {
            s = LD_SCORE_INVALID;
        }
        if (op == LOP_GIFT && am->deaths == 0 && f->f[LF_STRUGGLE] < 30) {
            s = LD_SCORE_INVALID;
        }
        if (op == LOP_BOX_WALL && f->f[LF_REPEAT] < 25) {
            s = LD_SCORE_INVALID;
        }
        if (op == LOP_SECRET && (am->flags & LAF_SECRET_TAKEN)) {
            s = LD_SCORE_INVALID;
        }
        scores[op] = (s16) LIVING_CLAMP(s, LD_SCORE_INVALID, 32000);
    }
}

u8 ld_max_ops(const LivingMemory *mem) {
    switch (mem->mood) {
        case LM_DORMANT:
            return 0;
        case LM_OBSERVING:
            return 1;
        default:
            return (u8) (mem->trait[LT_ATTACHMENT] > 40 ? 3 : 2);
    }
}

s32 ld_regen_due(const LivingMemory *mem, const LivingAreaMem *am) {
    s32 interval = 2;

    if (!(am->flags & LAF_HAS_PLAN)) {
        return 1;
    }
    if (mem->trait[LT_CURIOSITY] > 40) {
        interval--;
    }
    if (mem->trait[LT_PATIENCE] > 40) {
        interval++;
    }
    interval = LIVING_CLAMP(interval, 1, 3);
    return am->visitsSinceGen >= interval;
}

u8 ld_dir_towards(s32 dx, s32 dz) {
    s32 ax = LIVING_ABS(dx);
    s32 az = LIVING_ABS(dz);

    /* tan(22.5) ~= 0.414 ~= 53/128 */
    if (az * 128 <= ax * 53) {
        return (u8) (dx >= 0 ? 0 : 4);
    }
    if (ax * 128 <= az * 53) {
        return (u8) (dz >= 0 ? 2 : 6);
    }
    if (dx >= 0) {
        return (u8) (dz >= 0 ? 1 : 7);
    }
    return (u8) (dz >= 0 ? 3 : 5);
}

/* Pick a frontier cell (deterministically) and the anchor closest to it. */
static u8 frontier_param(const LivingAreaMem *am, const u8 *frontier, LRng *rng, u8 variant) {
    u8 cells[LIVING_CELLS];
    s32 n = 0;
    s32 c, a, bestA = 0;
    s32 bestD = 0x7FFFFFFF;
    LVec3 target;

    for (c = 0; c < LIVING_CELLS; c++) {
        if (lm_bit_get(frontier, (u8) c)) {
            cells[n++] = (u8) c;
        }
    }
    if (n == 0) {
        return LP_MAKE(0, lrng_range(rng, 0, 7), variant);
    }
    c = cells[lrng_range(rng, 0, n - 1)];
    lm_cell_center((u8) c, &target.x, &target.z);
    target.y = 0;
    for (a = 0; a < LIVING_ANCHORS; a++) {
        s32 d = lg_dist2_xz(&am->anchor[a], &target);
        if (d < bestD) {
            bestD = d;
            bestA = a;
        }
    }
    return LP_MAKE(bestA, ld_dir_towards((s32) target.x - am->anchor[bestA].x,
                                         (s32) target.z - am->anchor[bestA].z), variant);
}

static u8 make_param(const LivingAreaMem *am, u8 op, const u8 *frontier, LRng *rng, u8 variant) {
    switch (op) {
        case LOP_SECRET:
            return frontier_param(am, frontier, rng, variant);
        case LOP_ROOM:
        case LOP_BOX_BRIDGE:
        case LOP_BOX_TOWER:
        case LOP_BOX_STAIRS:
            return LP_MAKE(lrng_range(rng, 0, LIVING_ANCHORS - 1), lrng_range(rng, 0, 7), variant);
        default:
            /* hot-spot ops anchor on the player's favourite place (anchor 0) */
            return LP_MAKE(0, lrng_range(rng, 0, 7), variant);
    }
}

s32 ld_add_live_op(const LivingMemory *mem, LivingAreaMem *am, const LivingFeatures *f, const u8 *frontier, u8 salt) {
    s16 scores[LOP_COUNT];
    s32 slot = -1;
    s32 best = LOP_NONE;
    s32 bestScore = LD_MIN_SCORE - 1;
    s32 i, op;
    LRng rng;

    if (ld_max_ops(mem) == 0) {
        return -1; /* dormant */
    }
    for (i = 0; i < LIVING_PLAN_OPS; i++) {
        if (am->plan[i].op == LOP_NONE) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return -1;
    }
    ld_score_ops(mem, am, f, frontier, scores);
    for (op = 1; op < LOP_COUNT; op++) {
        if (plan_had(am, (u8) op, 0)) {
            continue; /* something new, not more of the same */
        }
        if (scores[op] > bestScore) {
            bestScore = scores[op];
            best = op;
        }
    }
    if (best == LOP_NONE) {
        return -1;
    }
    lrng_seed(&rng, lg_op_hash(mem->seed, am->level, am->area, am->generation, (u8) (0xB0 + salt)));
    am->plan[slot].op = (u8) best;
    am->plan[slot].param = make_param(am, (u8) best, frontier, &rng, (u8) lrng_range(&rng, 0, 7));
    am->flags |= LAF_HAS_PLAN;
    return slot;
}

u8 ld_make_plan(const LivingMemory *mem, LivingAreaMem *am, const LivingFeatures *f, const u8 *frontier) {
    s16 scores[LOP_COUNT];
    u8 maxOps = ld_max_ops(mem);
    u8 n = 0;
    s32 i, op;
    LRng rng;

    ld_score_ops(mem, am, f, frontier, scores);
    for (i = 0; i < LIVING_PLAN_OPS; i++) {
        am->plan[i].op = LOP_NONE;
        am->plan[i].param = 0;
    }
    lrng_seed(&rng, lg_op_hash(mem->seed, am->level, am->area, am->generation, 0xA5));

    while (n < maxOps && n < LIVING_PLAN_OPS) {
        s32 best = LOP_NONE;
        s32 bestScore = LD_MIN_SCORE - 1;
        u8 variant;

        for (op = 1; op < LOP_COUNT; op++) {
            if (scores[op] > bestScore) {
                bestScore = scores[op];
                best = op;
            }
        }
        if (best == LOP_NONE) {
            break;
        }
        scores[best] = LD_SCORE_INVALID; /* one of each per plan */
        variant = (u8) lrng_range(&rng, 0, 7);
        am->plan[n].op = (u8) best;
        am->plan[n].param = make_param(am, (u8) best, frontier, &rng, variant);
        n++;
    }
    am->flags |= LAF_HAS_PLAN;
    am->visitsSinceGen = 0;
    return n;
}

void ld_feedback(LivingMemory *mem, u8 op, s32 outcome) {
    s32 a;

    op &= (u8) ~LOP_REJECTED_BIT;
    if (op == LOP_NONE || op >= LOP_COUNT) {
        return;
    }
    a = mem->opAffinity[op];
    switch (outcome) {
        case LO_SUCCESS:
            a += 6;
            break;
        case LO_FAIL:
            a -= 4;
            break;
        case LO_REJECTED:
            a -= 2;
            break;
        default:
            break;
    }
    /* keep away from the extremes so the AI never stops trying alternatives */
    mem->opAffinity[op] = (u8) LIVING_CLAMP(a, 32, 224);
}
