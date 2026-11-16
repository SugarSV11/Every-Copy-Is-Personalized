/*
 * Save Data Manager
 *  - Two copies (A: blocks 64..159, B: blocks 160..255), each:
 *      [magic u16][version u8][flags u8][seq u16][len u16][crc16 u16][payload...]
 *  - Commit writes the INACTIVE copy, payload blocks first and the header block
 *    last, so an interrupted write leaves the previous copy intact (atomic swap).
 *  - Trickle writer: one 8-byte block per game frame (an EEPROM block write needs
 *    ~12-15 ms; a frame is 33 ms), skipping blocks that did not change.
 *  - Validation + sanitising on load; newer unknown versions put the AI in
 *    RAM-only mode so it never overwrites data it does not understand.
 */
#ifndef LIVING_SAVE_MANAGER_H
#define LIVING_SAVE_MANAGER_H

#include "../ai/player_memory.h"
#include "memory_storage.h"

#define LSAVE_HEADER_BYTES 10

enum LivingSaveMode {
    LSM_PERSIST,
    LSM_RAM_ONLY
};

enum LivingLoadResult {
    LSL_OK,
    LSL_FRESH,           /* nothing stored yet */
    LSL_RECOVERED,       /* one copy corrupt, the other used */
    LSL_CORRUPT_RESET,   /* both copies unusable: AI memory restarted */
    LSL_NEWER_VERSION,   /* stored by a newer build: RAM-only, data preserved */
    LSL_NO_STORAGE       /* no 16 Kbit EEPROM: RAM-only */
};

enum LivingWriteState {
    LSW_IDLE,
    LSW_WRITING
};

typedef struct {
    u8 image[2][LIVING_COPY_BYTES]; /* what each copy holds on the chip right now */
    u8 stage[LIVING_COPY_BYTES];    /* commit being written */
    u16 seq;                        /* seq of the active copy */
    u8 active;                      /* 0/1, 0xFF = none valid */
    u8 mode;
    u8 state;
    u8 writeCopy;
    u8 nextBlock;
    u8 nblocks;
    u8 headerPending;
    u8 pending;
    u8 consecutiveErrors;
    u8 lastLoad;
    u16 blocksWritten;
    u16 blocksSkipped;
    u16 commits;
    u16 errors;
    u16 sanitized;                  /* fields fixed by validation on load */
    const LivingMemory *src;
} LivingSaveMgr;

u16 lsm_crc16(const u8 *data, u16 len, u16 crc);
u16 lsm_serialize(const LivingMemory *mem, u8 *out, u16 cap);
s32 lsm_deserialize(LivingMemory *mem, const u8 *in, u16 len, u16 *sanitized);
u8 lsm_load(LivingSaveMgr *sm, LivingMemory *mem, u32 freshSeed);
void lsm_request_commit(LivingSaveMgr *sm, const LivingMemory *mem);
/* Call once per frame. Returns 1 if a block was written this frame. */
s32 lsm_tick(LivingSaveMgr *sm);
void lsm_flush(LivingSaveMgr *sm);
const char *lsm_load_name(u8 r);

#endif
