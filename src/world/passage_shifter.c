#include "passage_shifter.h"
#include "procedural_generator.h"

s32 lps_session_begin(LivingMemory *mem) {
    u32 h;
    s32 chance;
    u8 since = (u8) ((u8) mem->sessions - mem->shiftSession);

    if (mem->sessions < LIVING_SHIFT_MIN_SESSIONS) {
        return 0;
    }
    if (mem->mood == LM_DORMANT || mem->mood == LM_OBSERVING) {
        return 0; /* the AI has to know the player before it dares */
    }
#if LIVING_SHIFT_COOLDOWN > 0
    if (mem->shiftCount > 0 && since < LIVING_SHIFT_COOLDOWN) {
        return 0;
    }
#else
    (void) since;
#endif
    chance = LIVING_SHIFT_BASE_CHANCE + (mem->trait[LT_ATTACHMENT] > 0 ? mem->trait[LT_ATTACHMENT] / 8 : 0);
    h = lg_hash_combine(lg_hash32(mem->seed ^ 0x5A55A6E5u), mem->sessions);
    if ((s32) (h % 100) >= chance) {
        return 0;
    }
    mem->shiftSession = (u8) mem->sessions;
    mem->shiftCount = lm_sat_add8(mem->shiftCount, 1);
    return 1;
}

s32 lps_destination_known(const LivingMemory *mem, u8 level, u8 area) {
    s32 i;

    for (i = 0; i < LIVING_MAX_AREAS; i++) {
        if (mem->areas[i].level == level && mem->areas[i].area == area && level != 0 && mem->areas[i].visits > 0) {
            return 1;
        }
    }
    return 0;
}

/* Collect eligible passages of one kind with distinct, known destinations. */
static u8 eligible(const LivingMemory *mem, const LPassage *all, u8 n, u8 kind, u8 *out) {
    u8 m = 0;
    u8 i, j;

    for (i = 0; i < n; i++) {
        s32 dup = 0;

        if (all[i].kind != kind) {
            continue;
        }
        /* visited before, or new but already within the player's reach */
        if (!lps_destination_known(mem, all[i].destLevel, all[i].destArea)
            && !lw_destination_reachable(all[i].destLevel, all[i].destArea)) {
            continue;
        }
        for (j = 0; j < m; j++) {
            if (all[out[j]].destLevel == all[i].destLevel && all[out[j]].destArea == all[i].destArea) {
                dup = 1; /* two doors to the same place: swapping them changes nothing */
                break;
            }
        }
        if (!dup) {
            out[m++] = i;
        }
    }
    return m;
}

s32 lps_apply(const LivingMemory *mem, u8 level, u8 area) {
    LPassage all[LPS_MAX_PASSAGES];
    u8 pick[LPS_MAX_PASSAGES];
    u8 n, m, first, k;
    LRng rng;
    s32 a, b;

    n = lw_passages(all, LPS_MAX_PASSAGES);
    if (n < 2) {
        return 0;
    }
    lrng_seed(&rng, lg_op_hash(mem->seed, level, area, (u8) mem->sessions, 0x5E));
    first = (u8) (lrng_next(&rng) & 1); /* paintings or doors first, per area and session */
    for (k = 0; k < 2; k++) {
        u8 kind = (u8) ((first + k) & 1) ? LPK_PAINTING : LPK_DOOR;

        m = eligible(mem, all, n, kind, pick);
        if (m < 2) {
            continue;
        }
        a = lrng_range(&rng, 0, m - 1);
        b = lrng_range(&rng, 0, m - 2);
        if (b >= a) {
            b++;
        }
        return lw_passage_swap(&all[pick[a]], &all[pick[b]]);
    }
    return 0;
}
