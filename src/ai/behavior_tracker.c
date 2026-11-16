#include "behavior_tracker.h"

void lt_reset_visit(LivingTracker *t) {
    u8 *p = (u8 *) t;
    u32 i;

    for (i = 0; i < sizeof(LivingTracker); i++) {
        p[i] = 0;
    }
    t->curCell = LIVING_CELL_NONE;
    t->deathCell = LIVING_CELL_NONE;
}

/* Detect a short loop: the last k transitions equal the k before them (k = 3..5). */
static s32 lt_detect_loop(const LivingTracker *t) {
    s32 k;

    for (k = 3; k <= 5; k++) {
        s32 j;
        s32 same = 1;

        if (t->routeLen < 2 * k) {
            break;
        }
        for (j = 0; j < k; j++) {
            u8 a = t->route[(t->routeHead + LIVING_ROUTE_LEN - 1 - j) % LIVING_ROUTE_LEN];
            u8 b = t->route[(t->routeHead + LIVING_ROUTE_LEN - 1 - j - k) % LIVING_ROUTE_LEN];
            if (a != b) {
                same = 0;
                break;
            }
        }
        if (same) {
            return 1;
        }
    }
    return 0;
}

static void lt_sample(LivingTracker *t, const LivingInput *in, const LivingAreaMem *am) {
    u8 cell = lm_cell_of(in->pos.x, in->pos.z);
    s32 idle;

    t->visitSamples = lm_sat_add16(t->visitSamples, 1);
    t->heat[cell] = lm_sat_add8(t->heat[cell], 1);

    if (!lm_bit_get(t->visitMask, cell)) {
        lm_bit_set(t->visitMask, cell);
        if (am == 0 || !lm_bit_get(am->explored, cell)) {
            t->newCells = lm_sat_add8(t->newCells, 1);
        }
    }
    if ((in->flags & LIF_ON_GROUND) && !(in->flags & (LIF_DYING | LIF_IN_WATER))) {
        t->cellPos[cell] = in->pos;
        lm_bit_set(t->cellGrounded, cell);
    }

    if (cell != t->curCell) {
        t->route[t->routeHead] = cell;
        t->routeHead = (u8) ((t->routeHead + 1) % LIVING_ROUTE_LEN);
        if (t->routeLen < LIVING_ROUTE_LEN) {
            t->routeLen++;
        }
        t->curCell = cell;
        if (t->loopCooldown == 0 && lt_detect_loop(t)) {
            t->loops = lm_sat_add16(t->loops, 1);
            t->loopCooldown = 8;
        }
    }
    if (t->loopCooldown > 0) {
        t->loopCooldown--;
    }

    /* idle: idle action, or no stick and no buttons for the whole period */
    idle = (in->flags & LIF_IDLE_ACTION) || (in->stickMag < 8 && !(in->buttons & LB_ANY));
    if (idle) {
        t->idleSamples = lm_sat_add16(t->idleSamples, 1);
        if (t->stillRun < 255) {
            t->stillRun++;
        }
    } else {
        t->stillRun = 0;
    }
}

s32 lt_frame(LivingTracker *t, const LivingInput *in, const LivingAreaMem *am) {
    u8 newButtons;

    if (!t->primed) {
        t->lastCoins = in->coins;
        t->lastStars = in->stars;
        t->lastLives = in->lives;
        t->lastButtons = in->buttons;
        t->primed = 1;
    }

    /* per-frame: only comparisons and increments */
    if (in->actChanged && in->actClass != LA_NONE && in->actClass < LA_COUNT) {
        t->act[in->actClass] = lm_sat_add16(t->act[in->actClass], 1);
    }
    newButtons = (u8) (in->buttons & ~t->lastButtons);
    if (newButtons & LB_CAMERA) {
        t->camChanges = lm_sat_add16(t->camChanges, 1);
    }
    t->lastButtons = in->buttons;

    if (in->coins > t->lastCoins) {
        t->coinsGained = (s16) (t->coinsGained + (in->coins - t->lastCoins));
    }
    t->lastCoins = in->coins;
    if (in->stars > t->lastStars) {
        t->starsGained = (s16) (t->starsGained + (in->stars - t->lastStars));
    }
    t->lastStars = in->stars;
    t->lastLives = in->lives;

    if ((in->flags & LIF_DYING) && !t->wasDying) {
        t->deaths = lm_sat_add8(t->deaths, 1);
        t->deathCell = lm_cell_of(in->pos.x, in->pos.z);
    }
    t->wasDying = (u8) ((in->flags & LIF_DYING) != 0);

    /* periodic sample */
    if (++t->sampleTimer >= LIVING_SAMPLE_PERIOD) {
        t->sampleTimer = 0;
        lt_sample(t, in, am);
        return 1;
    }
    return 0;
}

u8 lt_hottest_cells(const LivingTracker *t, u8 *out, u8 maxOut) {
    u8 n = 0;
    u8 used[8];
    s32 i, c;

    for (i = 0; i < 8; i++) {
        used[i] = 0;
    }
    for (i = 0; i < maxOut; i++) {
        s32 best = -1;
        u8 bestHeat = 0;
        for (c = 0; c < LIVING_CELLS; c++) {
            if (!lm_bit_get(t->cellGrounded, (u8) c) || lm_bit_get(used, (u8) c)) {
                continue;
            }
            if (t->heat[c] > bestHeat) {
                bestHeat = t->heat[c];
                best = c;
            }
        }
        if (best < 0) {
            break;
        }
        lm_bit_set(used, (u8) best);
        out[n++] = (u8) best;
    }
    return n;
}

u16 lt_idle_seconds(const LivingTracker *t) {
    return (u16) (((u32) t->idleSamples * LIVING_SAMPLE_PERIOD) / LIVING_FRAMES_PER_SEC);
}

u16 lt_visit_seconds(const LivingTracker *t) {
    return (u16) (((u32) t->visitSamples * LIVING_SAMPLE_PERIOD) / LIVING_FRAMES_PER_SEC);
}
