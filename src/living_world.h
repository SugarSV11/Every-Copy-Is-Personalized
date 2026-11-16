/*
 * World interface: the ONLY way the AI touches the game.
 * Implemented by src/integration/living_sm64.c on N64 and by tests/mock_world.c on host.
 * The AI never writes code or arbitrary memory: it requests whitelisted operations
 * on whitelisted object kinds, and every request is validated before and after.
 */
#ifndef LIVING_WORLD_H
#define LIVING_WORLD_H

#include "living_types.h"

typedef u8 LHandle;
#define LH_INVALID 0xFF

/* Whitelisted object kinds the AI may spawn or adjust. */
enum LivingKind {
    LK_COIN,
    LK_1UP,
    LK_HEART,
    LK_GOOMBA,
    LK_GOOMBA_HUGE,
    LK_GOOMBA_TINY,
    LK_BOX,          /* breakable box: solid, climbable, and always destructible (no softlocks) */
    LK_TBLOCK,       /* solid 200^3 block wearing the CURRENT LEVEL's wall/floor textures */
    LK_ROOM,         /* small building (floor, walls, doorway, roof) in the level's textures */
    LK_TREE,         /* existing trees: only found and rescaled, never spawned */
    LK_COUNT
};

/* Floor classification returned by lw_probe. */
enum LivingSurf {
    LS_NONE,
    LS_SAFE,
    LS_HAZARD        /* lava, death plane, quicksand, deadly wind */
};

typedef struct {
    s16 floorY;
    s16 ceilY;       /* 0x7FFF when no ceiling */
    u8 surf;
    u8 normalY100;   /* floor normal Y * 100 (100 = flat) */
} LFloor;

/* Ambience effects. */
enum LivingFx {
    LFX_RESET,       /* restore tint + music */
    LFX_TINT,        /* a = palette index, b = alpha 0..255 */
    LFX_MUSIC,       /* a = volume percent */
    LFX_SOUND,       /* a = whitelisted sound index */
    LFX_SHAKE,       /* a = strength 1..3 */
    LFX_TEMPO        /* a = music tempo percent (100 = normal); re-applied if the song resets it */
};

enum LivingSound {
    LSND_WIND,
    LSND_CLOCK,
    LSND_LAUGH,
    LSND_SECRET,
    LSND_RUMBLE,
    LSND_CHIME,
    LSND_COUNT
};

s32 lw_probe(s16 x, s16 y, s16 z, LFloor *out);
LHandle lw_spawn(u8 kind, const LVec3 *pos, s16 yaw);
void lw_despawn(LHandle h);
s32 lw_alive(LHandle h);
void lw_get_pos(LHandle h, LVec3 *out);
void lw_set_pos(LHandle h, const LVec3 *p);
void lw_get_home(LHandle h, LVec3 *out);
void lw_set_home(LHandle h, const LVec3 *p);
s32 lw_kind_available(u8 kind);
u16 lw_object_count(void);
LHandle lw_find_existing(u8 kind, u8 nth);
void lw_release(LHandle h);           /* forget a handle without touching the object */
void lw_release_all(void);            /* area unload: objects are going away anyway */
void lw_fx(u8 fx, u8 a, u8 b);
void lw_player_pos(LVec3 *out);
u8 lw_get_scale(LHandle h);            /* percent */
void lw_set_scale(LHandle h, u8 percent);

/*
 * Passages: where the current area's doors and paintings lead.
 * A door passage is one warp node to another area of the same level; a painting
 * passage is the group of painting warp nodes sharing one destination.
 * lw_passage_swap exchanges two destinations in RAM only; lw_passages_restore puts
 * every original back (called when the area unloads, and the level script rebuilds
 * the nodes on the next level load anyway), so nothing is ever written to the ROM.
 */
enum LivingPassageKind {
    LPK_DOOR,
    LPK_PAINTING
};

typedef struct {
    u8 kind;
    u8 index;      /* door: position in the area's warp list; painting: first node id */
    u8 destLevel;  /* without the checkpoint flag */
    u8 destArea;
} LPassage;

u8 lw_passages(LPassage *out, u8 max);
s32 lw_passage_swap(const LPassage *a, const LPassage *b);
void lw_passages_restore(void);
/* 1 if the player could already get to (level, area) on their own with the stars and keys
 * of the current save file, even if they never went there. Used so a shifted passage can
 * lead somewhere NEW without skipping progression or trapping Mario behind a star door. */
s32 lw_destination_reachable(u8 level, u8 area);

#endif
