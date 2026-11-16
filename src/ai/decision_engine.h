/*
 * Decision Engine: utility scoring over whitelisted mutation ops.
 *   score = traits x W + features x C + mood bias + learned affinity
 *           - novelty penalty - failure memory + tiny deterministic tie-break
 * Hard preconditions filter ops that make no sense in context.
 * Outcomes of applied ops feed back into opAffinity (a tiny bandit learner).
 */
#ifndef LIVING_DECISION_ENGINE_H
#define LIVING_DECISION_ENGINE_H

#include "personality_engine.h"

#define LD_SCORE_INVALID (-32000)
#define LD_MIN_SCORE 20

enum LivingOutcome {
    LO_REJECTED = -2,
    LO_FAIL = -1,
    LO_NEUTRAL = 0,
    LO_SUCCESS = 1
};

void ld_score_ops(const LivingMemory *mem, const LivingAreaMem *am, const LivingFeatures *f,
                  const u8 *frontier, s16 *scores);
u8 ld_max_ops(const LivingMemory *mem);
s32 ld_regen_due(const LivingMemory *mem, const LivingAreaMem *am);
/* Builds a new plan into am->plan (generation must already be incremented). Returns op count. */
u8 ld_make_plan(const LivingMemory *mem, LivingAreaMem *am, const LivingFeatures *f, const u8 *frontier);
void ld_feedback(LivingMemory *mem, u8 op, s32 outcome);
/* Live personalization: picks one op not yet in the plan, writes it into a free slot.
 * Returns the slot or -1. */
s32 ld_add_live_op(const LivingMemory *mem, LivingAreaMem *am, const LivingFeatures *f, const u8 *frontier, u8 salt);
u8 ld_dir_towards(s32 dx, s32 dz);
const char *ld_op_name(u8 op);

#endif
