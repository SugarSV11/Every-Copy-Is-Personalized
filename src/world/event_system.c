#include "event_system.h"
#include "../ai/decision_engine.h"

const char *le_name(u8 event) {
    static const char *const names[EV_COUNT] = {
        "FIRST_CONTACT", "WELCOME_BACK", "IDLE_STIR", "MEMORIAL", "LOOP_BREAKER",
        "CAMERA_GAZE", "SILENCE", "HUNT", "GIFT_RAIN", "SECRET_FOUND", "PASSAGE_SHIFT"
    };
    return event < EV_COUNT ? names[event] : "?";
}

s32 le_gate(const LivingMemory *mem, const LivingAreaMem *am, u8 event, s32 baseChance) {
    u32 h = lg_hash32(mem->seed ^ 0xE7E7u);
    s32 chance = baseChance + (mem->trait[LT_ATTACHMENT] > 0 ? mem->trait[LT_ATTACHMENT] / 4 : 0);

    h = lg_hash_combine(h, mem->sessions);
    h = lg_hash_combine(h, event);
    h = lg_hash_combine(h, am ? (((u32) am->level << 8) | am->area) : 0);
    h = lg_hash_combine(h, am ? am->visits : 0);
    return (s32) (h % 100) < chance;
}

static void fired(LivingEvents *ev, LivingEventCtx *c, u8 event, u16 cooldown) {
    c->mem->eventsSeen |= (u32) 1 << event;
    c->mem->eventsFired = lm_sat_add16(c->mem->eventsFired, 1);
    ev->firedVisit |= (u16) (1 << event);
    ev->cooldown[event] = cooldown;
}

static s32 can_fire(const LivingEvents *ev, u8 event) {
    return ev->cooldown[event] == 0 && !(ev->firedVisit & (1 << event));
}


/* Where the player stands now, as a validated grounded point (cellPos of current cell). */
static s32 player_ground(const LivingTracker *t, LVec3 *out) {
    if (t->curCell == LIVING_CELL_NONE || !lm_bit_get(t->cellGrounded, t->curCell)) {
        return 0;
    }
    *out = t->cellPos[t->curCell];
    return 1;
}

static u8 travel_dir(const LivingTracker *t) {
    u8 a, b;
    s16 ax, az, bx, bz;

    if (t->routeLen < 2) {
        return 0;
    }
    a = t->route[(t->routeHead + LIVING_ROUTE_LEN - 2) % LIVING_ROUTE_LEN];
    b = t->route[(t->routeHead + LIVING_ROUTE_LEN - 1) % LIVING_ROUTE_LEN];
    lm_cell_center(a, &ax, &az);
    lm_cell_center(b, &bx, &bz);
    return ld_dir_towards((s32) bx - ax, (s32) bz - az);
}

static s32 event_op(LivingMut *m, u8 ok) {
    if (ok) {
        lme_commit_op(m);
    } else {
        lme_rollback_op(m);
    }
    return ok;
}

void le_reset_visit(LivingEvents *ev, LivingEventCtx *c) {
    s32 i;

    ev->firedVisit = 0;
    ev->camHead = 0;
    ev->lastCam = 0;
    ev->tintTimer = 0;
    ev->musicTimer = 0;
    ev->secretEntry = -1;
    for (i = 0; i < 10; i++) {
        ev->camHist[i] = 0;
    }
    /* locate the secret 1-up spawned by a LOP_SECRET plan op, if any */
    for (i = 0; i < c->m->count; i++) {
        LivingJournalEntry *e = &c->m->e[i];
        if (e->kind == LK_1UP && e->type == LJ_SPAWNED && e->slot < LIVING_PLAN_OPS
            && c->m->slot[e->slot].op == LOP_SECRET) {
            ev->secretEntry = (s8) i;
            ev->secretPos = e->orig;
            break;
        }
    }
}

void le_on_enter(LivingEvents *ev, LivingEventCtx *c) {
    LivingMemory *mem = c->mem;
    LivingAreaMem *am = c->am;
    s32 i;

    if (mem->mood == LM_DORMANT || am == 0 || !(am->flags & LAF_HAS_ANCHORS)) {
        return;
    }
    /* returning in a new session: a small coin ring where the player used to linger */
    if (am->visits > 0 && am->lastSession != (u8) mem->sessions && can_fire(ev, EV_WELCOME_BACK)
        && le_gate(mem, am, EV_WELCOME_BACK, 35)) {
        lme_begin_op(c->m, LIVING_EVENT_SLOT, LOP_NONE);
        if (event_op(c->m, (u8) (lobj_coin_ring(c->m, &am->anchor[0], 6, 260) >= 3))) {
            lw_fx(LFX_SOUND, LSND_CHIME, 0);
            fired(ev, c, EV_WELCOME_BACK, 0);
        }
    }
    /* memorial where the player keeps dying */
    if (am->deaths >= 3 && am->deathCell != LIVING_CELL_NONE && can_fire(ev, EV_MEMORIAL)) {
        for (i = 0; i < LIVING_ANCHORS; i++) {
            if (lm_cell_of(am->anchor[i].x, am->anchor[i].z) == am->deathCell) {
                lme_begin_op(c->m, LIVING_EVENT_SLOT, LOP_NONE);
                if (event_op(c->m, (u8) (lobj_coin_ring(c->m, &am->anchor[i], 5, 200) >= 3))) {
                    fired(ev, c, EV_MEMORIAL, 0);
                }
                break;
            }
        }
    }
}

void le_sample(LivingEvents *ev, LivingEventCtx *c) {
    LivingMemory *mem = c->mem;
    LivingAreaMem *am = c->am;
    LivingTracker *t = c->t;
    LVec3 p;
    s32 i, camSum;
    u16 idleLimit;

    for (i = 0; i < EV_COUNT; i++) {
        if (ev->cooldown[i] > 0) {
            ev->cooldown[i]--;
        }
    }
    /* camera presses per sample, 5 second window */
    ev->camHist[ev->camHead] = (u8) LIVING_MIN(255, t->camChanges - ev->lastCam);
    ev->lastCam = t->camChanges;
    ev->camHead = (u8) ((ev->camHead + 1) % 10);

    /* restore transient effects */
    if (ev->tintTimer > 0 && --ev->tintTimer == 0) {
    }
    if (ev->musicTimer > 0 && --ev->musicTimer == 0) {
        lw_fx(LFX_MUSIC, c->m->ambMusic, 0);
    }

    if (mem->mood == LM_DORMANT) {
        return;
    }

    if (!(mem->eventsSeen & (1u << EV_FIRST_CONTACT))) {
        lw_fx(LFX_SOUND, LSND_CHIME, 0);
        fired(ev, c, EV_FIRST_CONTACT, 60);
        return;
    }

    /* secret collected? */
    if (ev->secretEntry >= 0 && ev->secretEntry < c->m->count) {
        LivingJournalEntry *e = &c->m->e[ev->secretEntry];
        LVec3 pl;
        lw_player_pos(&pl);
        if (!lw_alive(e->h) && lg_dist2_xz(&pl, &ev->secretPos) < 800 * 800) {
            lw_fx(LFX_SOUND, LSND_SECRET, 0);
            if (am) {
                am->flags |= LAF_SECRET_TAKEN;
            }
            fired(ev, c, EV_SECRET_FOUND, 0);
            ev->secretEntry = -1;
        }
    }

    /* standing still: the world stirs (patient AIs wait longer) */
    idleLimit = (u16) (mem->trait[LT_PATIENCE] > 30 ? 60 : 40);
    if (t->stillRun >= idleLimit && ev->cooldown[EV_IDLE_STIR] == 0) {
        lw_fx(LFX_SHAKE, 1, 0);
        lw_fx(LFX_SOUND, mem->mood == LM_UNSETTLING ? LSND_LAUGH : LSND_WIND, 0);
        fired(ev, c, EV_IDLE_STIR, 120);
        ev->firedVisit &= (u16) ~(1 << EV_IDLE_STIR); /* repeatable, cooldown only */
    }

    /* repeated route: a breakable wall appears ahead */
    if (t->loops >= 3 && can_fire(ev, EV_LOOP_BREAKER)
        && (mem->mood == LM_TESTING || mem->mood == LM_PLAYFUL || mem->mood == LM_UNSETTLING)
        && player_ground(t, &p)) {
        LVec3 o;
        u8 dir = travel_dir(t);
        lg_offset(&p, dir, 700, 0, &o);
        lme_begin_op(c->m, LIVING_EVENT_SLOT, LOP_BOX_WALL);
        if (event_op(c->m, lgeo_build(c->m, LTPL_WALL, &o, dir))) {
            lw_fx(LFX_SOUND, LSND_RUMBLE, 0);
            fired(ev, c, EV_LOOP_BREAKER, 0);
        } else {
            ev->cooldown[EV_LOOP_BREAKER] = 20; /* try again later from elsewhere */
        }
    }

    /* frantic camera */
    camSum = 0;
    for (i = 0; i < 10; i++) {
        camSum += ev->camHist[i];
    }
    if (camSum >= 12 && ev->cooldown[EV_CAMERA_GAZE] == 0
        && (mem->mood == LM_UNSETTLING || mem->mood == LM_PLAYFUL)) {
        lw_fx(LFX_SOUND, mem->mood == LM_UNSETTLING ? LSND_LAUGH : LSND_CLOCK, 0);
        fired(ev, c, EV_CAMERA_GAZE, 60);
        ev->firedVisit &= (u16) ~(1 << EV_CAMERA_GAZE);
    }

    /* silence in rarely visited places */
    if (mem->mood == LM_UNSETTLING && am && am->visits <= 3 && t->visitSamples == 20
        && can_fire(ev, EV_SILENCE) && le_gate(mem, am, EV_SILENCE, 40)) {
        lw_fx(LFX_MUSIC, 30, 0);
        lw_fx(LFX_SOUND, LSND_CLOCK, 0);
        ev->musicTimer = 30;
        fired(ev, c, EV_SILENCE, 0);
    }

    /* hunt a dominant player */
    if (mem->mood == LM_TESTING && t->visitSamples == 40 && t->deaths == 0 && can_fire(ev, EV_HUNT)
        && player_ground(t, &p) && le_gate(mem, am, EV_HUNT, 50)) {
        lme_begin_op(c->m, LIVING_EVENT_SLOT, LOP_ENEMY_AMBUSH);
        if (event_op(c->m, lobj_ambush(c->m, &p, (u8) (lg_hash32(mem->seed + t->visitSamples) & 7), 1))) {
            fired(ev, c, EV_HUNT, 0);
        }
    }

    /* help a player who keeps dying here (an SM64 death ends the visit, so use area memory) */
    if (am && am->deaths >= 2 && t->visitSamples >= 6 && can_fire(ev, EV_GIFT_RAIN)
        && (mem->mood == LM_PROTECTIVE || mem->trait[LT_BENEVOLENT] > 30) && player_ground(t, &p)) {
        lme_begin_op(c->m, LIVING_EVENT_SLOT, LOP_GIFT);
        if (event_op(c->m, lobj_gift(c->m, &p, travel_dir(t), 7))) {
            lw_fx(LFX_SOUND, LSND_CHIME, 0);
            fired(ev, c, EV_GIFT_RAIN, 0);
        }
    }
}
