#ifndef LIVING_PERSONALITY_ENGINE_H
#define LIVING_PERSONALITY_ENGINE_H

#include "behavior_tracker.h"

/* Normalised observations (0..100) derived from tracker + memory. */
enum LivingFeature {
    LF_EXPLORE,
    LF_ACROBAT,
    LF_STRUGGLE,
    LF_DOMINANCE,
    LF_IDLE,
    LF_CAMERA,
    LF_REPEAT,
    LF_AVOID,
    LF_RETURN,
    LF_FAMILIAR,
    LF_COUNT
};

typedef struct {
    u8 f[LF_COUNT];
} LivingFeatures;

void lp_compute_features(const LivingMemory *mem, const LivingTracker *t, const LivingAreaMem *am,
                         LivingFeatures *out);
/* Moves traits toward what the player's behaviour implies; runs the mood state machine.
 * Returns 1 if the mood changed. */
s32 lp_reflect(LivingMemory *mem, const LivingFeatures *f);
s32 lp_mood_score(const LivingMemory *mem, const LivingFeatures *f, u8 mood);
const char *lp_mood_name(u8 mood);

#endif
