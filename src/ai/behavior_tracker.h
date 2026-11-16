/*
 * Behavioral Tracker: cheap per-frame edge detection + a sample every
 * LIVING_SAMPLE_PERIOD frames. Lives in RAM only; folded into LivingMemory
 * when the player leaves the area (player_memory / living.c reflection).
 */
#ifndef LIVING_BEHAVIOR_TRACKER_H
#define LIVING_BEHAVIOR_TRACKER_H

#include "player_memory.h"

/* LivingInput.flags */
#define LIF_IDLE_ACTION 0x01  /* Mario is in an idle/stationary action */
#define LIF_DYING       0x02  /* Mario is in a death action */
#define LIF_ON_GROUND   0x04
#define LIF_IN_WATER    0x08

/* LivingInput.buttons (abstracted from the N64 pad) */
#define LB_CAMERA 0x01        /* any C button or R */
#define LB_ANY    0x02        /* any button at all */

typedef struct {
    LVec3 pos;
    u8 actClass;   /* enum LivingActClass of the CURRENT action */
    u8 actChanged; /* 1 on the frame the action changed */
    u8 flags;
    u8 buttons;
    u8 stickMag;   /* 0..64 */
    s16 coins;
    s16 stars;
    s8 lives;
} LivingInput;

typedef struct {
    /* spatial memory for the current visit */
    u8 heat[LIVING_CELLS];          /* half-seconds spent per cell (saturating) */
    LVec3 cellPos[LIVING_CELLS];    /* last grounded position seen in each cell */
    u8 cellGrounded[8];             /* bitmask: cellPos valid */
    u8 visitMask[8];                /* cells touched this visit */
    u8 route[LIVING_ROUTE_LEN];     /* last cell transitions (ring) */
    u8 routeHead;
    u8 routeLen;
    u8 curCell;
    u8 deathCell;

    u16 sampleTimer;
    u16 visitSamples;
    u16 idleSamples;
    u8 stillRun;                    /* consecutive idle samples */
    u8 loopCooldown;

    /* counters for the current visit */
    u16 act[LA_COUNT];
    u16 camChanges;
    u16 loops;
    u8 newCells;
    u8 deaths;
    s16 coinsGained;
    s16 starsGained;

    /* edge-detection state */
    s16 lastCoins;
    s16 lastStars;
    s8 lastLives;
    u8 wasDying;
    u8 lastButtons;
    u8 primed;
} LivingTracker;

void lt_reset_visit(LivingTracker *t);
/* returns 1 on sample frames (cheap work happens every frame, heavier every period) */
s32 lt_frame(LivingTracker *t, const LivingInput *in, const LivingAreaMem *am);
u8 lt_hottest_cells(const LivingTracker *t, u8 *out, u8 maxOut);
u16 lt_idle_seconds(const LivingTracker *t);
u16 lt_visit_seconds(const LivingTracker *t);

#endif
