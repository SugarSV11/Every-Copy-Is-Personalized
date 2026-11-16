/*
 * SM64 Living ROM Engine - shared types and compile-time configuration.
 *
 * Portable C89 (IDO 5.3 / GCC MIPS / MSVC host). No malloc, no libc on N64.
 * Build for N64: included from sm64/src/living/living_unity.c (uses PR/ultratypes.h).
 * Build for host tests: define LIVING_HOST.
 */
#ifndef LIVING_TYPES_H
#define LIVING_TYPES_H

#ifdef LIVING_HOST
#include <stdint.h>
typedef uint8_t  u8;
typedef int8_t   s8;
typedef uint16_t u16;
typedef int16_t  s16;
typedef uint32_t u32;
typedef int32_t  s32;
typedef float    f32;
#else
#include <PR/ultratypes.h>
#endif

#define LIVING_STATIC_ASSERT(cond, name) typedef char living_sa_##name[(cond) ? 1 : -1]

#define LIVING_MIN(a, b) ((a) < (b) ? (a) : (b))
#define LIVING_MAX(a, b) ((a) > (b) ? (a) : (b))
#define LIVING_CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#define LIVING_ABS(v) ((v) < 0 ? -(v) : (v))

typedef struct {
    s16 x, y, z;
} LVec3;

/* Self-test builds record the last step reached (EEPROM block 251) to locate hangs. */
#if defined(LIVING_SELFTEST) && !defined(LIVING_HOST)
void living_trace(u8 step);
#define LIVING_TRACE(step) living_trace(step)
#else
#define LIVING_TRACE(step)
#endif

/* ---------------- configuration ---------------- */

/* Tracker sampling: once every N game frames (SM64 runs its logic at 30 Hz). */
#define LIVING_SAMPLE_PERIOD      15
#define LIVING_FRAMES_PER_SEC     30

/* Spatial grid: SM64 levels live inside +-8192 units. 8x8 cells of 2048 units. */
#define LIVING_WORLD_MIN          (-8192)
#define LIVING_WORLD_MAX          8191
#define LIVING_SAFE_MIN           (-8000)
#define LIVING_SAFE_MAX           8000
#define LIVING_GRID_SHIFT         11
#define LIVING_GRID_DIM           8
#define LIVING_CELLS              64
#define LIVING_CELL_NONE          0xFF

#define LIVING_ROUTE_LEN          16
#define LIVING_MAX_AREAS          12   /* remembered (level, area) pairs, LRU */
#define LIVING_ANCHORS            3    /* player-proven floor points per area */
#define LIVING_PLAN_OPS           4    /* mutation slots per area */
#define LIVING_MAX_LEVEL          40
#define LIVING_MAX_AREA_INDEX     7

/* Mutation budgets (objects are the expensive resource on N64). */
#define LIVING_MAX_HANDLES        40   /* journal capacity per area visit */
#define LIVING_MAX_SPAWN_PER_AREA 32 /* structures are made of up to 12 boxes */
#define LIVING_OBJ_SOFT_LIMIT     190  /* SM64 object pool holds 240 */
#define LIVING_MIN_PLAYER_DIST    400  /* never spawn on top of Mario */
#ifdef LIVING_SELFTEST
#define LIVING_LIVE_MIN_DIST      700  /* self-test: close enough to show up in screenshots */
#else
#define LIVING_LIVE_MIN_DIST      1500 /* mid-visit changes appear out of the player's way */
#endif
#define LIVING_LIVE_FIRST_SAMPLES 20   /* first live change ~10 s into a never-seen area */
#define LIVING_LIVE_PERIOD_SAMPLES 120 /* then at most one per minute */
#define LIVING_LIVE_MAX_OPS       2    /* per visit */

/* AI timing */
#ifdef LIVING_SELFTEST
#define LIVING_DORMANT_SEC        0    /* self-test build: act immediately */
#else
#define LIVING_DORMANT_SEC        120  /* observe only during the first 2 minutes of life */
#endif
#define LIVING_MOOD_HYSTERESIS    12
#define LIVING_MOOD_MIN_AGE       2    /* reflections before a mood may change */

/* Persistence: EEPROM 16 Kbit = 256 blocks x 8 bytes. Blocks 0..63 = original SM64 save. */
#define LIVING_EEP_FIRST_BLOCK    64
#define LIVING_COPY_BLOCKS        96
#define LIVING_COPY_BYTES         (LIVING_COPY_BLOCKS * 8)
#define LIVING_SAVE_MAGIC         0x4C56 /* 'LV' */
#define LIVING_SAVE_VERSION       2

/* Passage shifts ("this cartridge's castle is different"): rare by design. */
#ifdef LIVING_SELFTEST
#define LIVING_SHIFT_MIN_SESSIONS 2
#define LIVING_SHIFT_COOLDOWN     0
#define LIVING_SHIFT_BASE_CHANCE  100
#else
#define LIVING_SHIFT_MIN_SESSIONS 3    /* never in the first two sessions */
#define LIVING_SHIFT_COOLDOWN     3    /* at least 3 sessions between shifts */
#define LIVING_SHIFT_BASE_CHANCE  12   /* % per eligible session, + up to 12 with attachment */
#endif

#endif
