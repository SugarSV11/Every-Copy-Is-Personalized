/*
 * Event Generation System: contextual sequences driven by observed behaviour,
 * personality and mood. Rare events are gated by a deterministic hash of
 * (seed, session, event, area) - not by a free-running RNG - and their
 * probability grows with the AI's attachment to the player.
 */
#ifndef LIVING_EVENT_SYSTEM_H
#define LIVING_EVENT_SYSTEM_H

#include "../ai/behavior_tracker.h"
#include "mutation_engine.h"

enum LivingEvent {
    EV_FIRST_CONTACT,  /* the AI wakes up: once per save */
    EV_WELCOME_BACK,   /* returning to an area in a new session */
    EV_IDLE_STIR,      /* the player stands still too long */
    EV_MEMORIAL,       /* coins where the player keeps dying */
    EV_LOOP_BREAKER,   /* repeated route -> a wall appears ahead */
    EV_CAMERA_GAZE,    /* frantic camera -> the light reacts */
    EV_SILENCE,        /* the music drops in rarely visited places */
    EV_HUNT,           /* dominant player -> ambush */
    EV_GIFT_RAIN,      /* struggling player -> help */
    EV_SECRET_FOUND,   /* hidden 1-up collected */
    EV_PASSAGE_SHIFT,  /* a door or painting led somewhere else this session */
    EV_COUNT
};

typedef struct {
    LivingMemory *mem;
    LivingAreaMem *am;
    LivingTracker *t;
    LivingMut *m;
    const LivingInput *in;
} LivingEventCtx;

typedef struct {
    u16 cooldown[EV_COUNT];   /* in samples */
    u16 firedVisit;           /* bitmask: once-per-visit events */
    u8 camHist[10];           /* camera presses per sample, ring */
    u8 camHead;
    u16 lastCam;
    u8 tintTimer;
    u8 musicTimer;
    s8 secretEntry;           /* journal index of the secret 1-up, -1 if none */
    LVec3 secretPos;
} LivingEvents;

void le_reset_visit(LivingEvents *ev, LivingEventCtx *c);
void le_on_enter(LivingEvents *ev, LivingEventCtx *c);
void le_sample(LivingEvents *ev, LivingEventCtx *c);
s32 le_gate(const LivingMemory *mem, const LivingAreaMem *am, u8 event, s32 baseChance);
const char *le_name(u8 event);

#endif
