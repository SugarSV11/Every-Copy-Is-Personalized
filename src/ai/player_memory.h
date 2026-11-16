/*
 * Persistent Player Memory: everything that survives power-off.
 * Serialized explicitly (big-endian byte stream) by persistence/save_manager.c,
 * so the in-RAM layout is free to differ between N64 and host.
 */
#ifndef LIVING_PLAYER_MEMORY_H
#define LIVING_PLAYER_MEMORY_H

#include "../living_types.h"

/* Action classes counted by the tracker (classified by the integration layer). */
enum LivingActClass {
    LA_NONE,
    LA_JUMP,
    LA_TRIPLE,
    LA_BACKFLIP,
    LA_LONGJUMP,
    LA_WALLKICK,
    LA_GROUNDPOUND,
    LA_DIVE,
    LA_SWIM,
    LA_FLY,
    LA_COUNT
};

enum LivingTrait {
    LT_CURIOSITY,   /* wants to show the player new places */
    LT_PLAYFUL,     /* toys, structures, acrobatic invitations */
    LT_BENEVOLENT,  /* helps a struggling player */
    LT_MENACE,      /* challenges a dominant or farming player */
    LT_PATIENCE,    /* slow, quiet, contemplative pacing */
    LT_ATTACHMENT,  /* familiarity with this player: unlocks rare behaviour */
    LT_COUNT
};

enum LivingMood {
    LM_DORMANT,
    LM_OBSERVING,
    LM_CURIOUS,
    LM_PLAYFUL,
    LM_PROTECTIVE,
    LM_TESTING,
    LM_UNSETTLING,
    LM_COUNT
};

enum LivingOp {
    LOP_NONE,
    LOP_ROOM,           /* geometry: a new small building in the level's textures (id was COIN_TRAIL) */
    LOP_BOX_BRIDGE,     /* geometry: bridge of boxes across a gap/pit/hazard (id was RELOCATE_COINS) */
    LOP_BOX_STAIRS,     /* geometry: climbable structure -> new vertical route */
    LOP_BOX_WALL,       /* geometry: blocks a repeated route (breakable, never a softlock) */
    LOP_BOX_TOWER,      /* geometry: raised 2x2 platform with a reward on top (id was PILLAR) */
    LOP_ENEMY_AMBUSH,   /* NPC: goombas at the player's favourite spot */
    LOP_ENEMY_ROUTINE,  /* NPC: existing enemies patrol a different home */
    LOP_GIFT,           /* objects: heart / 1-up near where the player keeps dying */
    LOP_AMBIENCE,       /* environment: tint, music level, distant sounds */
    LOP_SECRET,         /* event: hidden 1-up in avoided space */
    LOP_COUNT
};

#define LOP_REJECTED_BIT 0x80 /* set on plan.op when validation failed: never retried */

/* plan.param layout */
#define LP_ANCHOR(p)  ((p) & 0x03)
#define LP_DIR(p)     (((p) >> 2) & 0x07)
#define LP_VARIANT(p) (((p) >> 5) & 0x07)
#define LP_MAKE(anchor, dir, variant) ((u8) (((anchor) & 3) | (((dir) & 7) << 2) | (((variant) & 7) << 5)))

typedef struct {
    u8 op;
    u8 param;
} LivingPlanOp;

#define LAF_HAS_ANCHORS 0x01
#define LAF_HAS_PLAN    0x02
#define LAF_SECRET_TAKEN 0x04

typedef struct {
    u8 level;            /* 0 = empty slot */
    u8 area;
    u8 visits;           /* saturating */
    u8 returns;          /* visits in a later session than the previous one */
    u16 dwellSec;        /* saturating */
    u8 deaths;
    u8 lastSession;      /* low 8 bits of memory.sessions */
    u8 explored[8];      /* 64-cell bitmask */
    u8 hotCell;
    u8 deathCell;
    u8 generation;       /* how many times the AI rethought this area */
    u8 flags;
    u8 visitsSinceGen;
    LivingPlanOp plan[LIVING_PLAN_OPS];
    LVec3 anchor[LIVING_ANCHORS];
} LivingAreaMem;

typedef struct {
    u32 seed;
    u16 sessions;
    u32 playSec;
    u16 act[LA_COUNT];
    u16 coins;
    u16 stars;
    u16 deaths;
    u16 camChanges;
    u16 idleSec;
    u16 loops;
    u16 areaChanges;

    s8 trait[LT_COUNT];  /* -100..100 */
    u8 mood;
    u8 moodAge;
    u16 reflections;

    u8 opAffinity[LOP_COUNT]; /* learned: 128 = neutral */

    u32 eventsSeen;      /* discovered events bitmask */
    u16 eventsFired;
    u16 mutApplied;
    u16 mutRejected;
    u8 recoveries;       /* times a corrupt save copy was recovered */
    u8 shiftSession;     /* v2: low 8 bits of the session of the last passage shift */
    u8 shiftCount;       /* v2: how many sessions had shifted passages */

    LivingAreaMem areas[LIVING_MAX_AREAS];
} LivingMemory;

void lm_init_fresh(LivingMemory *mem, u32 seed);
LivingAreaMem *lm_area_find(LivingMemory *mem, u8 level, u8 area);
LivingAreaMem *lm_area_acquire(LivingMemory *mem, u8 level, u8 area);
u8 lm_cell_of(s16 x, s16 z);
void lm_cell_center(u8 cell, s16 *x, s16 *z);
s32 lm_bit_get(const u8 *mask, u8 bit);
void lm_bit_set(u8 *mask, u8 bit);
u8 lm_bit_count(const u8 *mask);
void lm_frontier(const u8 *explored, u8 *outMask);
u16 lm_sat_add16(u16 a, u16 b);
u8 lm_sat_add8(u8 a, u8 b);

#endif
