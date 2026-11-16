/*
 * SM64 Living ROM Engine - top level.
 *
 * Lifecycle (called by the integration layer):
 *   living_boot()            once, after the original save file is loaded
 *   living_area_loaded()     when SM64 loads an area (objects not ready yet)
 *   living_update()          every frame of normal gameplay
 *   living_area_unloading()  right before SM64 destroys the area's objects
 *   living_request_commit()  when SM64 itself saves (star, key, etc.)
 */
#ifndef LIVING_H
#define LIVING_H

#include "ai/behavior_tracker.h"
#include "ai/personality_engine.h"
#include "ai/decision_engine.h"
#include "world/mutation_engine.h"
#include "world/event_system.h"
#include "world/passage_shifter.h"
#include "persistence/save_manager.h"

typedef struct {
    LivingMemory mem;
    LivingTracker trk;
    LivingMut mut;
    LivingEvents ev;
    LivingSaveMgr sm;
    LivingFeatures feat;
    LivingAreaMem *area;
    u32 frames;
    u8 secFrames;
    u8 pendingApply;   /* 0 idle, 1 acquire area, 2.. apply one plan slot per frame, then events */
    u8 pendingLevel;
    u8 pendingArea;
    u8 enabled;
    u8 lastApplied;
    u8 lastPlanOps;
    u8 lastMoodChanged;
    u8 shiftActive;    /* this session's doors/paintings may lead elsewhere */
    u8 shiftsApplied;
    u8 liveOps;        /* changes made while the player is in the area */
    u16 nextLiveSample;
} LivingState;

extern LivingState gLiving;

void living_boot(u32 entropy);
void living_area_loaded(u8 level, u8 area);
void living_update(const LivingInput *in);
void living_area_unloading(void);
void living_request_commit(void);
s32 living_storage_tick(void);
void living_set_enabled(u8 on);
void living_add_entropy(u32 entropy);
#ifdef LIVING_SELFTEST
u8 living_debug_showcase(const LVec3 *mario, u8 dir, u8 which);
#endif

#endif
