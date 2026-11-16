#include "living.h"

LivingState gLiving;

#define LIVING_MIN_VISIT_SAMPLES 4 /* ignore visits shorter than ~2 s (warp transitions) */

void living_boot(u32 entropy) {
    u8 *p = (u8 *) &gLiving;
    u32 i;

    for (i = 0; i < sizeof(LivingState); i++) {
        p[i] = 0;
    }
    lsm_load(&gLiving.sm, &gLiving.mem, lg_hash32(entropy ^ 0xC0FFEEu));
    gLiving.mem.sessions = lm_sat_add16(gLiving.mem.sessions, 1);
#ifndef LIVING_DISABLE_SHIFT
    gLiving.shiftActive = (u8) lps_session_begin(&gLiving.mem);
#endif
    lt_reset_visit(&gLiving.trk);
    lme_reset(&gLiving.mut);
    gLiving.enabled = 1;
}

/*
 * The CPU counter at boot is almost constant (exactly constant in emulators), so a
 * brand-new memory takes its world seed from the first frame of real gameplay instead:
 * that moment depends on how long this player spent in the menus. Ignored once the
 * AI has seen any area, so an existing world never changes.
 */
void living_add_entropy(u32 entropy) {
    if (gLiving.mem.areaChanges == 0 && gLiving.mem.reflections == 0) {
        u32 s = lg_hash_combine(gLiving.mem.seed, entropy);
        gLiving.mem.seed = s ? s : 0x5EED1234;
    }
}

#ifdef LIVING_SELFTEST
/* Self-test only: build a structure right in front of Mario so screenshots show it. */
u8 living_debug_showcase(const LVec3 *mario, u8 dir, u8 which) {
    static const u8 sKinds[4] = { LOP_ROOM, LOP_BOX_STAIRS, LOP_BOX_BRIDGE, LOP_BOX_TOWER };
    s16 saved = gLiving.mut.minPlayerDist;
    u8 ok;

    if (gLiving.area == 0 || gLiving.pendingApply) {
        return 0xEE;
    }
    gLiving.mut.minPlayerDist = 250;
    which = 0; /* self-test: always try a room first, so screenshots show one */
    lme_begin_op(&gLiving.mut, LIVING_EVENT_SLOT, sKinds[which & 3]);
    if (sKinds[which & 3] == LOP_ROOM) {
        ok = lgeo_room(&gLiving.mut, mario, dir, which);
        if (!ok) {
            lme_rollback_op(&gLiving.mut);
            ok = lme_build_structure(&gLiving.mut, LOP_BOX_TOWER, mario, dir, which);
        }
    } else {
        ok = lme_build_structure(&gLiving.mut, sKinds[which & 3], mario, dir, which);
    }
    if (ok) {
        lme_commit_op(&gLiving.mut);
    } else {
        lme_rollback_op(&gLiving.mut);
    }
    gLiving.mut.minPlayerDist = saved;
    return ok;
}
#endif

void living_set_enabled(u8 on) {
    gLiving.enabled = on;
}

void living_area_loaded(u8 level, u8 area) {
    if (!gLiving.enabled || level == 0) {
        gLiving.area = 0;
        return;
    }
    /* Do not claim a memory slot yet: menus and the title screen load areas too.
     * The slot is acquired on the first frame of real gameplay (living_update). */
    gLiving.area = 0;
    gLiving.pendingLevel = level;
    gLiving.pendingArea = area;
    gLiving.pendingApply = 1;
}

static LivingEventCtx make_ctx(const LivingInput *in) {
    LivingEventCtx c;

    c.mem = &gLiving.mem;
    c.am = gLiving.area;
    c.t = &gLiving.trk;
    c.m = &gLiving.mut;
    c.in = in;
    return c;
}

#define LIVING_STAGE_ACQUIRE 1
#define LIVING_STAGE_FIRST_SLOT 2
#define LIVING_STAGE_EVENTS (LIVING_STAGE_FIRST_SLOT + LIVING_PLAN_OPS)

/*
 * Area entry is spread over several frames so no single frame pays for every
 * floor probe and object spawn (measured on emulated N64: applying a whole plan
 * in one frame peaked at ~5.3 ms = 16% of a 30 fps frame).
 *   stage 1        acquire the memory slot (first frame of real gameplay)
 *   stages 2..5    apply one plan slot per frame
 *   last stage     entry events + visit bookkeeping
 */
static void enter_area_step(const LivingInput *in) {
    LivingAreaMem *am;
    LivingEventCtx c;

    if (gLiving.pendingApply == LIVING_STAGE_ACQUIRE) {
        gLiving.area = lm_area_acquire(&gLiving.mem, gLiving.pendingLevel, gLiving.pendingArea);
        lt_reset_visit(&gLiving.trk);
        lme_reset(&gLiving.mut);
        gLiving.lastApplied = 0;
        gLiving.liveOps = 0;
        /* a never-planned area gets its first personal touch ~10 s in; others once a minute */
        gLiving.nextLiveSample = (u16) ((gLiving.area && (gLiving.area->flags & LAF_HAS_PLAN))
                                        ? LIVING_LIVE_PERIOD_SAMPLES : LIVING_LIVE_FIRST_SAMPLES);
        gLiving.pendingApply = gLiving.area ? LIVING_STAGE_FIRST_SLOT : 0;
        return;
    }
    am = gLiving.area;
    if (gLiving.pendingApply < LIVING_STAGE_EVENTS) {
        gLiving.lastApplied = (u8) (gLiving.lastApplied
            + lme_apply_slot(&gLiving.mut, &gLiving.mem, am, (u8) (gLiving.pendingApply - LIVING_STAGE_FIRST_SLOT)));
        gLiving.pendingApply++;
        return;
    }
    gLiving.pendingApply = 0;
    if (gLiving.shiftActive && lps_apply(&gLiving.mem, am->level, am->area)) {
        /* silent on purpose: the cartridge simply "is like this" today */
        gLiving.shiftsApplied++;
        gLiving.mem.eventsSeen |= (u32) 1 << EV_PASSAGE_SHIFT;
        gLiving.mem.eventsFired = lm_sat_add16(gLiving.mem.eventsFired, 1);
    }
    c = make_ctx(in);
    le_reset_visit(&gLiving.ev, &c);
    le_on_enter(&gLiving.ev, &c);

    /* bookkeeping after the events that look at the previous visit */
    if (am->visits > 0 && am->lastSession != (u8) gLiving.mem.sessions) {
        am->returns = lm_sat_add8(am->returns, 1);
    }
    am->visits = lm_sat_add8(am->visits, 1);
    am->lastSession = (u8) gLiving.mem.sessions;
    gLiving.mem.areaChanges = lm_sat_add16(gLiving.mem.areaChanges, 1);
}

/*
 * Anchors for an area the AI has never planned: where the player is standing most,
 * plus floor points under coins the LEVEL ITSELF placed in cells the player has not
 * walked yet. Level designers put those coins on reachable ground, so the AI can
 * personalise places the player has never visited without guessing positions.
 */
static void live_anchors(LivingAreaMem *am) {
    LVec3 pts[LIVING_ANCHORS];
    u8 hot[LIVING_ANCHORS];
    u8 n = 0;
    u8 nh, i;

    nh = lt_hottest_cells(&gLiving.trk, hot, LIVING_ANCHORS);
    if (nh > 0) {
        pts[n++] = gLiving.trk.cellPos[hot[0]];
    }
    for (i = 0; i < 12 && n < LIVING_ANCHORS; i++) {
        LHandle h;
        LVec3 p;
        LFloor fl;

        LIVING_TRACE((u8) (60 + i));
        h = lw_find_existing(LK_COIN, i);

        if (h == LH_INVALID) {
            break;
        }
        lw_get_pos(h, &p);
        lw_release(h);
        if (lm_bit_get(gLiving.trk.visitMask, lm_cell_of(p.x, p.z))) {
            continue; /* prefer ground the player has not walked */
        }
        if (!lw_probe(p.x, (s16) (p.y + 50), p.z, &fl) || fl.surf != LS_SAFE || p.y - fl.floorY > 300) {
            continue; /* coin in the air or over a hazard: not an anchor */
        }
        p.y = fl.floorY;
        pts[n++] = p;
    }
    for (i = 1; i < nh && n < LIVING_ANCHORS; i++) {
        pts[n++] = gLiving.trk.cellPos[hot[i]];
    }
    if (n == 0) {
        return;
    }
    for (i = 0; i < LIVING_ANCHORS; i++) {
        am->anchor[i] = pts[i < n ? i : 0];
    }
    am->flags |= LAF_HAS_ANCHORS;
}

/* Personalise WHILE the player is here (no need to leave the level first). */
static void live_personalize(void) {
    LivingAreaMem *am = gLiving.area;
    u8 explored[8];
    u8 frontier[8];
    s32 slot, i;

    if (gLiving.pendingApply || gLiving.mem.mood == LM_DORMANT || gLiving.liveOps >= LIVING_LIVE_MAX_OPS
        || gLiving.trk.visitSamples < gLiving.nextLiveSample) {
        return;
    }
    gLiving.nextLiveSample = (u16) (gLiving.trk.visitSamples + LIVING_LIVE_PERIOD_SAMPLES);
    LIVING_TRACE(20);
    if (!(am->flags & LAF_HAS_ANCHORS)) {
        live_anchors(am);
        if (!(am->flags & LAF_HAS_ANCHORS)) {
            return;
        }
    }
    LIVING_TRACE(21);
    lp_compute_features(&gLiving.mem, &gLiving.trk, am, &gLiving.feat);
    LIVING_TRACE(22);
    for (i = 0; i < 8; i++) {
        explored[i] = (u8) (am->explored[i] | gLiving.trk.visitMask[i]);
    }
    lm_frontier(explored, frontier);
    slot = ld_add_live_op(&gLiving.mem, am, &gLiving.feat, frontier, gLiving.liveOps);
    LIVING_TRACE(23);
    if (slot < 0) {
        return;
    }
    /* while the player is here, build around the anchor farthest from him: changes are
     * found later, around a corner, often in a part of the level he has not seen yet */
    {
        LVec3 pl;
        s32 best = 0, bestD = -1, d;
        u8 p = am->plan[slot].param;

        lw_player_pos(&pl);
        for (i = 0; i < LIVING_ANCHORS; i++) {
            d = lg_dist2_xz(&am->anchor[i], &pl);
            if (d > bestD) {
                bestD = d;
                best = i;
            }
        }
        am->plan[slot].param = LP_MAKE(best, LP_DIR(p), LP_VARIANT(p));
    }
    gLiving.mut.minPlayerDist = LIVING_LIVE_MIN_DIST;
    LIVING_TRACE((u8) (30 + am->plan[slot].op));
    if (lme_apply_slot(&gLiving.mut, &gLiving.mem, am, (u8) slot)) {
        gLiving.lastApplied++;
    }
    gLiving.mut.minPlayerDist = LIVING_MIN_PLAYER_DIST;
    gLiving.liveOps++;
    LIVING_TRACE(25);
    lsm_request_commit(&gLiving.sm, &gLiving.mem); /* remembered even if the console is switched off here */
    LIVING_TRACE(26);
}

void living_update(const LivingInput *in) {
    if (!gLiving.enabled) {
        return;
    }
    gLiving.frames++;
    if (++gLiving.secFrames >= LIVING_FRAMES_PER_SEC) {
        gLiving.secFrames = 0;
        gLiving.mem.playSec++;
    }

    if (gLiving.pendingApply) {
        enter_area_step(in);
    }
    if (gLiving.area != 0) {
        if (lt_frame(&gLiving.trk, in, gLiving.area)) {
            LivingEventCtx c = make_ctx(in);
            s32 i;

            le_sample(&gLiving.ev, &c);
            /* every 5 s, retry ops that failed only because of circumstances */
            if (gLiving.trk.visitSamples % 10 == 0) {
                gLiving.mut.minPlayerDist = LIVING_LIVE_MIN_DIST; /* the player is around: no pop-in */
                gLiving.lastApplied = (u8) (gLiving.lastApplied
                                            + lme_retry_transient(&gLiving.mut, &gLiving.mem, gLiving.area));
                gLiving.mut.minPlayerDist = LIVING_MIN_PLAYER_DIST;
            }
#ifndef LIVING_DISABLE_LIVE
            live_personalize();
#endif
            for (i = 0; i < LIVING_PLAN_OPS; i++) {
                if (gLiving.mut.slot[i].targetCell == gLiving.trk.curCell) {
                    gLiving.mut.slot[i].visitedTarget = 1;
                }
            }
        }
    }
}

static u8 count_spawned(const LivingMut *m, u8 slot) {
    u8 n = 0;
    s32 i;

    for (i = 0; i < m->count; i++) {
        if (m->e[i].slot == slot && (m->e[i].type == LJ_SPAWNED || m->e[i].type == LJ_RELOCATED)) {
            n++;
        }
    }
    return n;
}

/* Learning signal: did the player engage with what the AI built? */
static void evaluate_outcomes(void) {
    LivingMut *m = &gLiving.mut;
    LivingTracker *t = &gLiving.trk;
    u16 secs = lt_visit_seconds(t);
    u8 i;

    for (i = 0; i < LIVING_PLAN_OPS; i++) {
        LivingSlotState *s = &m->slot[i];
        u8 total, alive, gone;
        s32 outcome = LO_NEUTRAL;

        if (s->result == LAR_GEOMETRY || s->result == LAR_UNAVAILABLE) {
            ld_feedback(&gLiving.mem, s->op, LO_REJECTED);
            continue;
        }
        if (s->result != LAR_OK) {
            continue;
        }
        total = count_spawned(m, i);
        alive = (u8) (lme_slot_alive(m, i, LJ_SPAWNED) + lme_slot_alive(m, i, LJ_RELOCATED));
        gone = (u8) (total - alive);

        switch (s->op) {
            case LOP_ROOM:
            case LOP_BOX_BRIDGE:
            case LOP_BOX_STAIRS:
            case LOP_BOX_WALL:
            case LOP_BOX_TOWER:
                if (gone > 0 || s->visitedTarget) {
                    outcome = LO_SUCCESS; /* the player used or broke the structure */
                } else if (secs >= 90) {
                    outcome = LO_FAIL;
                }
                break;
            case LOP_ENEMY_AMBUSH:
                if (t->deaths > 0) {
                    outcome = LO_FAIL;
                } else if (gone > 0) {
                    outcome = LO_SUCCESS;
                }
                break;
            case LOP_ENEMY_ROUTINE:
                outcome = t->deaths > 0 ? LO_FAIL : LO_NEUTRAL;
                break;
            case LOP_GIFT:
                outcome = gone > 0 ? LO_SUCCESS : LO_NEUTRAL;
                break;
            case LOP_AMBIENCE:
                outcome = secs >= 45 ? LO_SUCCESS : (secs < 15 ? LO_FAIL : LO_NEUTRAL);
                break;
            case LOP_SECRET:
                /* the op is skipped once taken, so the flag can only have been set during this visit */
                if (gLiving.area && (gLiving.area->flags & LAF_SECRET_TAKEN)) {
                    outcome = LO_SUCCESS;
                }
                break;
            default:
                break;
        }
        ld_feedback(&gLiving.mem, s->op, outcome);
    }
}

static void merge_visit(LivingAreaMem *am) {
    LivingMemory *mem = &gLiving.mem;
    LivingTracker *t = &gLiving.trk;
    u8 hot[1];
    s32 i;

    for (i = 0; i < LA_COUNT; i++) {
        mem->act[i] = lm_sat_add16(mem->act[i], t->act[i]);
    }
    mem->coins = lm_sat_add16(mem->coins, (u16) LIVING_MAX(0, t->coinsGained));
    mem->stars = lm_sat_add16(mem->stars, (u16) LIVING_MAX(0, t->starsGained));
    mem->deaths = lm_sat_add16(mem->deaths, t->deaths);
    mem->camChanges = lm_sat_add16(mem->camChanges, t->camChanges);
    mem->idleSec = lm_sat_add16(mem->idleSec, lt_idle_seconds(t));
    mem->loops = lm_sat_add16(mem->loops, t->loops);

    am->dwellSec = lm_sat_add16(am->dwellSec, lt_visit_seconds(t));
    am->deaths = lm_sat_add8(am->deaths, t->deaths);
    for (i = 0; i < 8; i++) {
        am->explored[i] |= t->visitMask[i];
    }
    if (lt_hottest_cells(t, hot, 1) == 1) {
        am->hotCell = hot[0];
    }
    if (t->deathCell != LIVING_CELL_NONE) {
        am->deathCell = t->deathCell;
    }
    am->visitsSinceGen = lm_sat_add8(am->visitsSinceGen, 1);
}

static void refresh_anchors(LivingAreaMem *am) {
    u8 cells[LIVING_ANCHORS];
    u8 n = lt_hottest_cells(&gLiving.trk, cells, LIVING_ANCHORS);
    u8 i;

    if (n == 0) {
        return;
    }
    for (i = 0; i < LIVING_ANCHORS; i++) {
        /* fewer hot cells than anchors: repeat the hottest one */
        am->anchor[i] = gLiving.trk.cellPos[cells[i < n ? i : 0]];
    }
    am->flags |= LAF_HAS_ANCHORS;
}

void living_area_unloading(void) {
    LivingAreaMem *am = gLiving.area;
    u8 wasEntering = gLiving.pendingApply;

    gLiving.pendingApply = 0;
#ifndef LIVING_DISABLE_SHIFT
    lw_passages_restore(); /* doors and paintings always go back to normal when the area goes */
#endif
    if (am == 0) {
        return;
    }
    if (wasEntering || gLiving.trk.visitSamples < LIVING_MIN_VISIT_SAMPLES) {
        /* nothing observed: undo nothing, learn nothing */
        lme_forget_all(&gLiving.mut);
        lw_fx(LFX_RESET, 0, 0);
        gLiving.area = 0;
        return;
    }

    /* 1. learn from what happened to the AI's changes (objects still exist here) */
    evaluate_outcomes();

    /* 2. fold the visit into long-term memory */
    merge_visit(am);

    /* 3. reflect: traits drift towards the player's behaviour, mood FSM may switch */
    lp_compute_features(&gLiving.mem, &gLiving.trk, am, &gLiving.feat);
    gLiving.lastMoodChanged = (u8) lp_reflect(&gLiving.mem, &gLiving.feat);

    /* 4. rethink the area if it is due (applied on the NEXT visit) */
    if (!(am->flags & LAF_HAS_ANCHORS)) {
        refresh_anchors(am);
    }
    if ((am->flags & LAF_HAS_ANCHORS) && ld_regen_due(&gLiving.mem, am)) {
        u8 frontier[8];
        refresh_anchors(am);
        lm_frontier(am->explored, frontier);
        if (am->generation < 255) {
            am->generation++;
        }
        gLiving.lastPlanOps = ld_make_plan(&gLiving.mem, am, &gLiving.feat, frontier);
    }

    /* 5. the game destroys the objects now; just drop the handles */
    lme_forget_all(&gLiving.mut);
    lw_fx(LFX_RESET, 0, 0);
    gLiving.area = 0;

    /* 6. persist (trickled over the next frames) */
    lsm_request_commit(&gLiving.sm, &gLiving.mem);
}

/* Called once per game frame from the main loop (also in menus and transitions), so a
 * commit started at area exit finishes even if the next screen is not gameplay. */
s32 living_storage_tick(void) {
    if (!gLiving.enabled) {
        return 0;
    }
    return lsm_tick(&gLiving.sm); /* at most one 8-byte EEPROM block per frame */
}

void living_request_commit(void) {
    lsm_request_commit(&gLiving.sm, &gLiving.mem);
}
