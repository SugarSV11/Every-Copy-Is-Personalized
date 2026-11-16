/*
 * Passage Shifter: very rarely, a door or a painting in this cartridge leads
 * somewhere else for one whole session ("every copy of SM64 is personal").
 *
 * Rules (all deterministic from the save's seed and session counter):
 *  - never in the first LIVING_SHIFT_MIN_SESSIONS-1 sessions, never while the AI is
 *    DORMANT/OBSERVING, at least LIVING_SHIFT_COOLDOWN sessions between shifts;
 *  - chance per eligible session: LIVING_SHIFT_BASE_CHANCE% + attachment/8;
 *  - destinations the player has visited OR could already reach with the stars and
 *    keys they own (so a passage can lead somewhere new, but no star door or key is
 *    skipped and Mario can never end up locked in a room he cannot open);
 *  - one swap per area, the same swap every time that area loads in the session;
 *  - RAM only: originals are restored when the area unloads.
 */
#ifndef LIVING_PASSAGE_SHIFTER_H
#define LIVING_PASSAGE_SHIFTER_H

#include "../living_world.h"
#include "../ai/player_memory.h"

#define LPS_MAX_PASSAGES 16

/* Called once per boot after the memory is loaded. Returns 1 if this session shifts. */
s32 lps_session_begin(LivingMemory *mem);
/* Called when an area becomes playable. Returns 1 if a swap was applied. */
s32 lps_apply(const LivingMemory *mem, u8 level, u8 area);
s32 lps_destination_known(const LivingMemory *mem, u8 level, u8 area);

#endif
