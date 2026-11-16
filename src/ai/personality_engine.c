#include "personality_engine.h"

static u8 clamp100(s32 v) {
    return (u8) LIVING_CLAMP(v, 0, 100);
}

void lp_compute_features(const LivingMemory *mem, const LivingTracker *t, const LivingAreaMem *am,
                         LivingFeatures *out) {
    s32 secs = lt_visit_seconds(t);
    s32 acro;
    u8 frontier[8];
    u8 explored[8];
    s32 i, fr, ex;

    if (secs < 1) {
        secs = 1;
    }
    acro = t->act[LA_TRIPLE] + t->act[LA_BACKFLIP] + t->act[LA_LONGJUMP] + t->act[LA_WALLKICK]
           + t->act[LA_DIVE] + t->act[LA_GROUNDPOUND];

    /* breadth of movement this visit + genuinely new ground */
    out->f[LF_EXPLORE] = clamp100((s32) lm_bit_count(t->visitMask) * 12 + (s32) t->newCells * 20);
    out->f[LF_ACROBAT] = clamp100(acro * 300 / secs);
    out->f[LF_STRUGGLE] = clamp100((s32) t->deaths * 35
                                   + (mem->sessions ? (s32) mem->deaths * 10 / mem->sessions : 0));
    out->f[LF_DOMINANCE] = clamp100((s32) t->starsGained * 3000 / secs - (s32) t->deaths * 20);
    out->f[LF_IDLE] = clamp100(t->visitSamples ? (s32) t->idleSamples * 100 / t->visitSamples : 0);
    out->f[LF_CAMERA] = clamp100((s32) t->camChanges * 240 / secs);
    out->f[LF_REPEAT] = clamp100((s32) t->loops * 25);

    for (i = 0; i < 8; i++) {
        explored[i] = (u8) ((am ? am->explored[i] : 0) | t->visitMask[i]);
    }
    lm_frontier(explored, frontier);
    fr = lm_bit_count(frontier);
    ex = lm_bit_count(explored);
    out->f[LF_AVOID] = clamp100((fr + ex) ? fr * 100 / (fr + ex) : 0);
    out->f[LF_RETURN] = clamp100((am && am->visits) ? (s32) am->returns * 100 / am->visits : 0);
    out->f[LF_FAMILIAR] = clamp100((s32) mem->sessions * 8 + (s32) (mem->playSec / 600));
}

static s8 approach(s8 cur, s32 target, s32 shift) {
    s32 c = cur;
    s32 d = target - c;

    /* move a fraction of the distance, but at least 1 so traits never stall */
    if (d > 0) {
        c += LIVING_MAX(1, d >> shift);
    } else if (d < 0) {
        c -= LIVING_MAX(1, (-d) >> shift);
    }
    return (s8) LIVING_CLAMP(c, -100, 100);
}

s32 lp_mood_score(const LivingMemory *mem, const LivingFeatures *f, u8 mood) {
    const s8 *tr = mem->trait;

    switch (mood) {
        case LM_OBSERVING:
            return 40 + tr[LT_PATIENCE] / 4;
        case LM_CURIOUS:
            return 50 + tr[LT_CURIOSITY] / 2 + f->f[LF_AVOID] / 4;
        case LM_PLAYFUL:
            return 50 + tr[LT_PLAYFUL] / 2 + f->f[LF_ACROBAT] / 4;
        case LM_PROTECTIVE:
            return 50 + tr[LT_BENEVOLENT] / 2 + f->f[LF_STRUGGLE] / 2;
        case LM_TESTING:
            return 50 + tr[LT_MENACE] / 2 + f->f[LF_DOMINANCE] / 4 - f->f[LF_STRUGGLE] / 2;
        case LM_UNSETTLING:
            if (tr[LT_ATTACHMENT] < 20) {
                return 0;
            }
            return 30 + tr[LT_ATTACHMENT] / 4 + tr[LT_PATIENCE] / 4 + f->f[LF_IDLE] / 4 + tr[LT_MENACE] / 4;
        default:
            return 0;
    }
}

s32 lp_reflect(LivingMemory *mem, const LivingFeatures *f) {
    const u8 *x = f->f;
    s32 target[LT_COUNT];
    s32 i, best, bestScore, curScore;

    target[LT_CURIOSITY] = (x[LF_EXPLORE] * 2 + x[LF_AVOID] + (100 - x[LF_REPEAT])) / 4;
    target[LT_PLAYFUL] = (x[LF_ACROBAT] * 2 + x[LF_CAMERA] + (100 - x[LF_IDLE])) / 4;
    target[LT_BENEVOLENT] = (x[LF_STRUGGLE] * 3 + (100 - x[LF_DOMINANCE])) / 4;
    target[LT_MENACE] = (x[LF_DOMINANCE] * 2 + x[LF_REPEAT] + (100 - x[LF_STRUGGLE])) / 4;
    target[LT_PATIENCE] = (x[LF_IDLE] * 2 + (100 - x[LF_ACROBAT]) + (100 - x[LF_CAMERA])) / 4;
    target[LT_ATTACHMENT] = (x[LF_FAMILIAR] * 3 + x[LF_RETURN]) / 4;

    for (i = 0; i < LT_COUNT; i++) {
        mem->trait[i] = approach(mem->trait[i], target[i] * 2 - 100, i == LT_ATTACHMENT ? 3 : 2);
    }
    mem->reflections = lm_sat_add16(mem->reflections, 1);
    if (mem->moodAge < 255) {
        mem->moodAge++;
    }

    /* state machine */
    if (mem->mood == LM_DORMANT) {
#if LIVING_DORMANT_SEC > 0
        if (mem->playSec < LIVING_DORMANT_SEC) {
            return 0;
        }
#endif
        mem->mood = LM_OBSERVING;
        mem->moodAge = 0;
        return 1;
    }

    best = mem->mood;
    curScore = lp_mood_score(mem, f, mem->mood);
    bestScore = curScore;
    for (i = LM_OBSERVING; i < LM_COUNT; i++) {
        s32 s = lp_mood_score(mem, f, (u8) i);
        if (s > bestScore) {
            bestScore = s;
            best = i;
        }
    }
    if (best != mem->mood && mem->moodAge >= LIVING_MOOD_MIN_AGE
        && bestScore > curScore + LIVING_MOOD_HYSTERESIS) {
        mem->mood = (u8) best;
        mem->moodAge = 0;
        return 1;
    }
    return 0;
}

const char *lp_mood_name(u8 mood) {
    static const char *const names[LM_COUNT] = {
        "DORMANT", "OBSERVING", "CURIOUS", "PLAYFUL", "PROTECTIVE", "TESTING", "UNSETTLING"
    };
    return mood < LM_COUNT ? names[mood] : "?";
}
