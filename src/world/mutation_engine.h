/*
 * World Mutation Engine: applies an area plan through the world interface.
 * Every change is journaled; any op that fails validation is rolled back
 * completely, so the world is either "op fully applied" or "untouched".
 */
#ifndef LIVING_MUTATION_ENGINE_H
#define LIVING_MUTATION_ENGINE_H

#include "../living_world.h"
#include "../ai/player_memory.h"
#include "procedural_generator.h"

#define LJ_SPAWNED   0x01
#define LJ_RELOCATED 0x02
#define LJ_REHOMED   0x04
#define LJ_SCALED    0x08  /* orig.x = original scale percent */

typedef struct {
    LHandle h;
    u8 kind;
    u8 slot;
    u8 type;    /* LJ_* */
    LVec3 orig; /* original position/home for relocations */
} LivingJournalEntry;

/* Why an op failed: only geometric failures are permanent (LOP_REJECTED_BIT). */
enum LivingApplyResult {
    LAR_OK,
    LAR_GEOMETRY,     /* no valid floor / hazard / ceiling / bounds -> permanent */
    LAR_UNAVAILABLE,  /* model not loaded in this level -> permanent */
    LAR_BUDGET,       /* object pool / journal full -> retry next visit */
    LAR_TRANSIENT,    /* e.g. Mario standing on the spot -> retry next visit */
    LAR_NONE
};

typedef struct {
    u8 op;
    u8 result;
    u8 spawned;
    u8 targetCell;
    u8 visitedTarget; /* filled by living.c from the tracker */
} LivingSlotState;

#define LIVING_EVENT_SLOT LIVING_PLAN_OPS /* journal slot used by runtime events */

typedef struct {
    LivingJournalEntry e[LIVING_MAX_HANDLES];
    u8 count;
    u8 spawnedThisArea;
    u8 curSlot;
    u8 opStart;
    LivingSlotState slot[LIVING_PLAN_OPS + 1];
    u8 ambPal;   /* ambience currently applied (events restore to it) */
    u8 ambAlpha;
    u8 ambMusic;
    u8 lastFail;
    s16 minPlayerDist; /* 400 at area entry; larger while the player is in the area (no pop-in) */      /* enum LivingApplyResult of the last failed primitive */
    u16 validationFailures;
    u16 rollbacks;
} LivingMut;

void lme_reset(LivingMut *m);
void lme_begin_op(LivingMut *m, u8 slot, u8 op);
void lme_commit_op(LivingMut *m);
void lme_rollback_op(LivingMut *m);

/* Validated primitives (all return 1 on success). */
s32 lme_ground(LivingMut *m, LVec3 *pos, s16 refY, s16 maxStep);
LHandle lme_spawn(LivingMut *m, u8 kind, const LVec3 *pos, s16 yaw);
s32 lme_relocate(LivingMut *m, LHandle h, u8 kind, const LVec3 *to);
s32 lme_rehome(LivingMut *m, LHandle h, u8 kind, const LVec3 *home);
s32 lme_rescale(LivingMut *m, LHandle h, u8 kind, u8 percent);
s32 lme_can_spawn(LivingMut *m, u8 kind, u8 count);

/* Applies all non-rejected ops of am->plan. Returns ops applied. Marks permanent failures. */
u8 lme_apply_plan(LivingMut *m, LivingMemory *mem, LivingAreaMem *am);
s32 lme_apply_slot(LivingMut *m, LivingMemory *mem, LivingAreaMem *am, u8 slot);
u8 lme_retry_transient(LivingMut *m, LivingMemory *mem, LivingAreaMem *am);
/* Counts journal entries of a slot that are still alive (e.g. coins not collected). */
u8 lme_slot_alive(const LivingMut *m, u8 slot, u8 type);
/* Area unload: objects are being destroyed by the game; drop all handles. */
void lme_forget_all(LivingMut *m);

/* geometry_mutator.c */
u8 lgeo_build(LivingMut *m, u8 tpl, const LVec3 *origin, u8 dir);
u8 lgeo_stairs_to_ledge(LivingMut *m, const LVec3 *anchor, u8 dirHint);
u8 lgeo_bridge(LivingMut *m, const LVec3 *anchor, u8 dirHint);
u8 lgeo_tower(LivingMut *m, const LVec3 *anchor, u8 dir, u8 variant);
u8 lme_build_structure(LivingMut *m, u8 first, const LVec3 *anchor, u8 dir, u8 variant);
u8 lgeo_room(LivingMut *m, const LVec3 *anchor, u8 dirHint, u8 variant);
u8 lobj_rescale_trees(LivingMut *m, u8 percent, u8 maxTrees);
/* object_mutator.c */
u8 lobj_coin_trail(LivingMut *m, const LVec3 *from, u8 dir, u8 maxCoins);
u8 lobj_relocate_coins(LivingMut *m, const LVec3 *from, u8 dir, u8 maxCoins);
u8 lobj_ambush(LivingMut *m, const LVec3 *at, u8 dir, u8 variant);
u8 lobj_reroute_enemies(LivingMut *m, const LVec3 *home, u8 maxEnemies);
u8 lobj_gift(LivingMut *m, const LVec3 *at, u8 dir, u8 variant);
u8 lobj_secret(LivingMut *m, const LVec3 *from, u8 dir);
u8 lobj_coin_ring(LivingMut *m, const LVec3 *center, u8 count, s16 radius);

#endif
