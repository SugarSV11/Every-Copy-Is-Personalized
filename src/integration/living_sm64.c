/*
 * SM64 integration layer: implements the world interface (living_world.h) and the
 * EEPROM backend (memory_storage.h) on top of the real engine, and exposes the hooks.
 *
 * Compiled as its own translation unit (sm64/src/game/living_sm64.c includes this
 * file) so SM64 headers never mix with the portable core.
 *
 * STATUS: written against n64decomp/sm64 @ 9921382 (US). Not yet compiled: the MIPS
 * toolchain is not installed on this machine (see docs/FASES.md).
 */
#ifdef LIVING_ROM

#include <ultra64.h>
#include "sm64.h"
#include "types.h"
#include "behavior_data.h"
#include "model_ids.h"
#include "object_constants.h"
#include "surface_terrains.h"
#include "audio/external.h"
#include "engine/surface_collision.h"
#include "engine/surface_load.h"
#include "game/area.h"
#include "game/camera.h"
#include "game/game_init.h"
#include "game/level_update.h"
#include "game/main.h"
#include "game/memory.h"
#include "game/object_helpers.h"
#include "game/object_list_processor.h"
#include "game/save_file.h"
#include "engine/graph_node.h"
#include "audio/load.h"
#include "level_table.h"

#include "../living.h"
#include "../living_world.h"
#include "../persistence/memory_storage.h"
#include "living_hooks.h"
#include "buffers/framebuffers.h"

/* ---------------- whitelisted kinds ---------------- */

typedef struct {
    u16 model;
    const BehaviorScript *beh;
    u8 bp2;
} LivingKindDef;

/* ---------------- the AI's own level pieces ---------------- */
/* Models live in RAM (living_models.inc.c, generated). Their textures point at segment
 * 0x09, i.e. the texture bank of whatever level is loaded, picked per bank below. */

#include "living_models.inc.c"

#define LIVING_MODEL_BLOCK 0xF0 /* free model ids (vanilla uses up to 0xE1) */
#define LIVING_MODEL_ROOM 0xF1

enum { LB_GENERIC, LB_GRASS, LB_INSIDE, LB_OUTSIDE, LB_SNOW, LB_SPOOKY, LB_WATER, LB_CAVE,
       LB_FIRE, LB_MOUNTAIN, LB_SKY, LB_MACHINE, LB_COUNT };

/* wall, floor, roof: 32x32 RGBA16 textures inside each bank (offsets checked visually
 * against the extracted textures, see builds/texture_banks.png) */
static const u16 sBankTex[LB_COUNT][3] = {
    { 0x3000, 0x2800, 0x0800 }, /* generic : grey stone, sand, yellow brick */
    { 0x0800, 0x4000, 0x3000 }, /* grass   : brown brick, grey tiles, cobbles */
    { 0x3000, 0x5000, 0x3800 }, /* inside  : castle brick, red carpet, wood */
    { 0x0800, 0xA000, 0x7800 }, /* outside : grey stone, flagstones, roof tiles */
    { 0x0000, 0x6000, 0x8000 }, /* snow    : logs, snowy ground, planks */
    { 0x4800, 0xA000, 0x8800 }, /* spooky  : purple stone, checker, panel */
    { 0x0000, 0xA000, 0x0000 }, /* water   : blue stone, planks, blue stone */
    { 0x5800, 0x9000, 0x6800 }, /* cave    : dark rock, tan rock, rock */
    { 0x6000, 0x4000, 0x1000 }, /* fire    : grey brick, diamond tiles, dark stone */
    { 0x0000, 0x4000, 0x7000 }, /* mountain: stone brick, brown rock, brown brick */
    { 0x1000, 0x1800, 0x7800 }, /* sky     : grey brick, stone, metal */
    { 0x7000, 0x0800, 0x0000 }, /* machine : metal panel, tan, frame */
};

static s32 level_bank(s16 level) {
    switch (level) {
        case LEVEL_BOB: case LEVEL_SSL: return LB_GENERIC;
        case LEVEL_WF: case LEVEL_THI: case LEVEL_WDW: return LB_GRASS;
        case LEVEL_CASTLE: case LEVEL_SA: return LB_INSIDE;
        case LEVEL_CASTLE_GROUNDS: case LEVEL_CASTLE_COURTYARD: case LEVEL_VCUTM: return LB_OUTSIDE;
        case LEVEL_CCM: case LEVEL_SL: return LB_SNOW;
        case LEVEL_BBH: return LB_SPOOKY;
        case LEVEL_JRB: case LEVEL_DDD: return LB_WATER;
        case LEVEL_HMC: case LEVEL_COTMC: return LB_CAVE;
        case LEVEL_LLL: case LEVEL_BOWSER_2: return LB_FIRE;
        case LEVEL_TTM: case LEVEL_PSS: return LB_MOUNTAIN;
        case LEVEL_BITDW: case LEVEL_BITFS: case LEVEL_BITS: case LEVEL_RR: case LEVEL_TOTWC:
        case LEVEL_WMOTR: return LB_SKY;
        case LEVEL_TTC: return LB_MACHINE;
        default: return -1; /* menus, Bowser 1/3, ending: no texture bank in segment 9 */
    }
}

static struct GraphNodeDisplayList sBlockNode;
static struct GraphNodeDisplayList sRoomNode;
static u8 sPiecesReady;

static void set_tex(Gfx *dl, s32 cmd, u16 off) {
    if (cmd >= 0) {
        dl[cmd].words.w1 = 0x09000000 + off;
    }
}

/* Area load: point the pieces at this level's textures and register them as models. */
static void living_register_pieces(void) {
    s32 b = level_bank(gCurrLevelNum);

    sPiecesReady = FALSE;
    if (b < 0) {
        return;
    }
    set_tex(living_block_dl, LIVING_BLOCK_TEXCMD_WALL, sBankTex[b][0]);
    set_tex(living_block_dl, LIVING_BLOCK_TEXCMD_FLOOR, sBankTex[b][1]);
    set_tex(living_room_dl, LIVING_ROOM_TEXCMD_WALL, sBankTex[b][0]);
    set_tex(living_room_dl, LIVING_ROOM_TEXCMD_FLOOR, sBankTex[b][1]);
    set_tex(living_room_dl, LIVING_ROOM_TEXCMD_ROOF, sBankTex[b][2]);
    init_graph_node_display_list(NULL, &sBlockNode, LAYER_OPAQUE, living_block_dl);
    init_graph_node_display_list(NULL, &sRoomNode, LAYER_OPAQUE, living_room_dl);
    gLoadedGraphNodes[LIVING_MODEL_BLOCK] = (struct GraphNode *) &sBlockNode;
    gLoadedGraphNodes[LIVING_MODEL_ROOM] = (struct GraphNode *) &sRoomNode;
    sPiecesReady = TRUE;
}

static void living_block_init(void) {
    gCurrentObject->collisionData = (void *) living_block_collision;
    gCurrentObject->oCollisionDistance = 600.0f;
    gCurrentObject->oDrawingDistance = 9000.0f;
    gCurrentObject->oFlags |= OBJ_FLAG_UPDATE_GFX_POS_AND_ANGLE;
}

static void living_room_init(void) {
    gCurrentObject->collisionData = (void *) living_room_collision;
    gCurrentObject->oCollisionDistance = 1800.0f;
    gCurrentObject->oDrawingDistance = 12000.0f;
    gCurrentObject->oFlags |= OBJ_FLAG_UPDATE_GFX_POS_AND_ANGLE;
}

/* Behaviour scripts written as raw commands (the macros live in behavior_data.c):
 * BEGIN(OBJ_LIST_SURFACE), CALL_NATIVE(init), BEGIN_LOOP(),
 * CALL_NATIVE(load_object_collision_model), END_LOOP() */
static const BehaviorScript bhvLivingBlock[] = {
    (0x00 << 24) | (OBJ_LIST_SURFACE << 16), 0x0C000000, (uintptr_t) living_block_init,
    0x08000000, 0x0C000000, (uintptr_t) load_object_collision_model, 0x09000000,
};
static const BehaviorScript bhvLivingRoom[] = {
    (0x00 << 24) | (OBJ_LIST_SURFACE << 16), 0x0C000000, (uintptr_t) living_room_init,
    0x08000000, 0x0C000000, (uintptr_t) load_object_collision_model, 0x09000000,
};

static const LivingKindDef sKinds[LK_COUNT] = {
    { MODEL_YELLOW_COIN, bhvYellowCoin, 0 },
    { MODEL_1UP, bhv1Up, 0 },
    { MODEL_HEART, bhvRecoveryHeart, 0 },
    { MODEL_GOOMBA, bhvGoomba, GOOMBA_SIZE_REGULAR },
    { MODEL_GOOMBA, bhvGoomba, GOOMBA_SIZE_HUGE },
    { MODEL_GOOMBA, bhvGoomba, GOOMBA_SIZE_TINY },
    { MODEL_BREAKABLE_BOX, bhvBreakableBox, 0 },
    { LIVING_MODEL_BLOCK, bhvLivingBlock, 0 },
    { LIVING_MODEL_ROOM, bhvLivingRoom, 0 },
    { MODEL_NONE, bhvTree, 0 },
};

/* Behaviour pointer as the engine wants it. Vanilla behaviours are segmented (0x13...);
 * ours live in RAM, so they are given as segment 0 (which maps to physical 0). */
static const BehaviorScript *kind_beh(u8 kind) {
    if (kind == LK_TBLOCK || kind == LK_ROOM) {
        return (const BehaviorScript *) ((uintptr_t) sKinds[kind].beh & 0x00FFFFFF);
    }
    return sKinds[kind].beh;
}

/* ---------------- handle table ---------------- */
/* A handle remembers the object AND its behaviour: SM64 recycles object slots, so a
 * handle is only "alive" while the slot is active and still runs the same behaviour. */

static struct Object *sObj[LIVING_MAX_HANDLES];
static const BehaviorScript *sBeh[LIVING_MAX_HANDLES];
static u8 sOurs[LIVING_MAX_HANDLES];

static LHandle handle_alloc(struct Object *o, const BehaviorScript *beh, u8 ours) {
    s32 i;

    for (i = 0; i < LIVING_MAX_HANDLES; i++) {
        if (sObj[i] == NULL) {
            sObj[i] = o;
            sBeh[i] = beh;
            sOurs[i] = ours;
            return (LHandle) i;
        }
    }
    return LH_INVALID;
}

static struct Object *handle_obj(LHandle h) {
    struct Object *o;

    if (h >= LIVING_MAX_HANDLES || sObj[h] == NULL) {
        return NULL;
    }
    o = sObj[h];
    if (!(o->activeFlags & ACTIVE_FLAG_ACTIVE) || !obj_has_behavior(o, sBeh[h])) {
        return NULL;
    }
    return o;
}

static s32 is_ours(struct Object *o) {
    s32 i;

    for (i = 0; i < LIVING_MAX_HANDLES; i++) {
        if (sObj[i] == o && sOurs[i]) {
            return TRUE;
        }
    }
    return FALSE;
}

void lw_release(LHandle h) {
    if (h < LIVING_MAX_HANDLES && !sOurs[h]) {
        sObj[h] = NULL;
    }
}

void lw_release_all(void) {
    s32 i;

    for (i = 0; i < LIVING_MAX_HANDLES; i++) {
        sObj[i] = NULL;
        sOurs[i] = 0;
    }
}

/* ---------------- world interface ---------------- */

s32 lw_probe(s16 x, s16 y, s16 z, LFloor *out) {
    struct Surface *floor = NULL;
    struct Surface *ceil = NULL;
    f32 floorY, ceilY, waterY;
    s16 type;

    floorY = find_floor(x, y, z, &floor);
    if (floor == NULL || floorY <= FLOOR_LOWER_LIMIT_MISC) {
        return FALSE;
    }
    ceilY = find_ceil(x, floorY + 80.0f, z, &ceil);
    waterY = find_water_level(x, z);
    type = floor->type;

    out->floorY = (s16) floorY;
    out->ceilY = (ceil == NULL || ceilY > 32000.0f) ? 0x7FFF : (s16) ceilY;
    out->normalY100 = (u8) (floor->normal.y > 0.0f ? floor->normal.y * 100.0f : 0);
    if (type == SURFACE_BURNING || type == SURFACE_DEATH_PLANE || type == SURFACE_INSTANT_QUICKSAND
        || type == SURFACE_INSTANT_MOVING_QUICKSAND || type == SURFACE_VERTICAL_WIND
        || type == SURFACE_QUICKSAND || type == SURFACE_DEEP_QUICKSAND || type == SURFACE_DEEP_MOVING_QUICKSAND
        || waterY > floorY + 50.0f) {
        out->surf = LS_HAZARD; /* also: never build under water */
    } else {
        out->surf = LS_SAFE;
    }
    return TRUE;
}

s32 lw_kind_available(u8 kind) {
    if (kind == LK_TBLOCK || kind == LK_ROOM) {
        return sPiecesReady && gMarioObject != NULL;
    }
    if (kind == LK_TREE) {
        return FALSE; /* never spawned */
    }
    if (kind >= LK_COUNT || gMarioObject == NULL || gLoadedGraphNodes[sKinds[kind].model] == NULL) {
        return FALSE;
    }
    /* bhvBreakableBox switches its model to MODEL_BREAKABLE_BOX_SMALL every frame */
    return kind != LK_BOX || gLoadedGraphNodes[MODEL_BREAKABLE_BOX_SMALL] != NULL;
}

u16 lw_object_count(void) {
    return (u16) gObjectCounter;
}

LHandle lw_spawn(u8 kind, const LVec3 *pos, s16 yaw) {
    struct Object *o;
    LHandle h;

    if (!lw_kind_available(kind)) {
        return LH_INVALID;
    }
    o = spawn_object_abs_with_rot(gMarioObject, 0, sKinds[kind].model, kind_beh(kind), pos->x, pos->y,
                                  pos->z, 0, yaw, 0);
    if (o == NULL) {
        return LH_INVALID;
    }
    /* behaviour parameters are read by the init routine on the object's first update */
    o->oBhvParams = (u32) sKinds[kind].bp2 << 16;
    o->oBhvParams2ndByte = sKinds[kind].bp2;
    h = handle_alloc(o, kind_beh(kind), TRUE);
    if (h == LH_INVALID) {
        obj_mark_for_deletion(o);
    }
    return h;
}

void lw_despawn(LHandle h) {
    struct Object *o = handle_obj(h);

    if (o != NULL) {
        obj_mark_for_deletion(o);
    }
    if (h < LIVING_MAX_HANDLES) {
        sObj[h] = NULL;
        sOurs[h] = 0;
    }
}

s32 lw_alive(LHandle h) {
    return handle_obj(h) != NULL;
}

void lw_get_pos(LHandle h, LVec3 *out) {
    struct Object *o = handle_obj(h);

    if (o != NULL) {
        out->x = (s16) o->oPosX;
        out->y = (s16) o->oPosY;
        out->z = (s16) o->oPosZ;
    }
}

void lw_set_pos(LHandle h, const LVec3 *p) {
    struct Object *o = handle_obj(h);

    if (o != NULL) {
        obj_set_pos(o, p->x, p->y, p->z);
    }
}

void lw_get_home(LHandle h, LVec3 *out) {
    struct Object *o = handle_obj(h);

    if (o != NULL) {
        out->x = (s16) o->oHomeX;
        out->y = (s16) o->oHomeY;
        out->z = (s16) o->oHomeZ;
    }
}

void lw_set_home(LHandle h, const LVec3 *p) {
    struct Object *o = handle_obj(h);

    if (o != NULL) {
        o->oHomeX = p->x;
        o->oHomeY = p->y;
        o->oHomeZ = p->z;
    }
}

LHandle lw_find_existing(u8 kind, u8 nth) {
    const BehaviorScript *beh = kind_beh(kind);
    const BehaviorScript *virt = segmented_to_virtual(beh);
    struct ObjectNode *listHead = &gObjectLists[get_object_list_from_behavior(virt)]; /* needs the RAM address, not the segmented one */
    struct Object *o = (struct Object *) listHead->next;
    u8 n = 0;

    while (o != (struct Object *) listHead) {
        if (o->behavior == virt && (o->activeFlags & ACTIVE_FLAG_ACTIVE) && !is_ours(o)) {
            if (n++ == nth) {
                return handle_alloc(o, beh, FALSE);
            }
        }
        o = (struct Object *) o->header.next;
    }
    return LH_INVALID;
}

u8 lw_get_scale(LHandle h) {
    struct Object *obj = handle_obj(h);
    f32 s;

    if (obj == NULL) {
        return 100;
    }
    s = obj->header.gfx.scale[0] * 100.0f;
    return (u8) (s < 1.0f ? 1 : (s > 255.0f ? 255 : s));
}

void lw_set_scale(LHandle h, u8 percent) {
    struct Object *obj = handle_obj(h);

    if (obj != NULL) {
        obj_scale(obj, percent / 100.0f);
    }
}

void lw_player_pos(LVec3 *out) {
    out->x = (s16) gMarioState->pos[0];
    out->y = (s16) gMarioState->pos[1];
    out->z = (s16) gMarioState->pos[2];
}

/* ---------------- passages (doors / paintings) ---------------- */

#define LIVING_MAX_SAVED_NODES 24

static struct WarpNode *sSavedNode[LIVING_MAX_SAVED_NODES];
static struct WarpNode sSavedOrig[LIVING_MAX_SAVED_NODES];
static u8 sSavedCount;

static s32 save_node(struct WarpNode *n) {
    s32 i;

    for (i = 0; i < sSavedCount; i++) {
        if (sSavedNode[i] == n) {
            return TRUE; /* already holds the original */
        }
    }
    if (sSavedCount >= LIVING_MAX_SAVED_NODES) {
        return FALSE;
    }
    sSavedNode[sSavedCount] = n;
    sSavedOrig[sSavedCount] = *n;
    sSavedCount++;
    return TRUE;
}

void lw_passages_restore(void) {
    while (sSavedCount > 0) {
        sSavedCount--;
        *sSavedNode[sSavedCount] = sSavedOrig[sSavedCount];
    }
}

/* Door passages: warp nodes of this area that lead to ANOTHER area of the SAME level and
 * belong to a real door/pipe object. Special nodes (>= 0xF0: death, success, etc.) never. */
static struct ObjectWarpNode *door_node(u8 index, u8 *outIdx) {
    struct ObjectWarpNode *wn;
    u8 i = 0;

    for (wn = gCurrentArea->warpNodes; wn != NULL; wn = wn->next, i++) {
        if (outIdx == NULL && i == index) {
            return wn;
        }
    }
    return NULL;
}

u8 lw_passages(LPassage *out, u8 max) {
    struct ObjectWarpNode *wn;
    u8 n = 0;
    u8 i = 0;
    s32 k, j;

    if (gCurrentArea == NULL) {
        return 0;
    }
    for (wn = gCurrentArea->warpNodes; wn != NULL && n < max; wn = wn->next, i++) {
        u8 lvl = wn->node.destLevel & 0x7F;

        if (wn->node.id < 0xF0 && wn->object != NULL && lvl == gCurrLevelNum
            && wn->node.destArea != gCurrAreaIndex) {
            out[n].kind = LPK_DOOR;
            out[n].index = i;
            out[n].destLevel = lvl;
            out[n].destArea = wn->node.destArea;
            n++;
        }
    }
    if (gCurrentArea->paintingWarpNodes != NULL) {
        for (k = 0; k < 45 && n < max; k++) {
            struct WarpNode *pn = &gCurrentArea->paintingWarpNodes[k];
            u8 lvl = pn->destLevel & 0x7F;
            s32 seen = FALSE;

            if (pn->id == 0 || lvl == gCurrLevelNum) {
                continue;
            }
            for (j = 0; j < n; j++) {
                if (out[j].kind == LPK_PAINTING && out[j].destLevel == lvl && out[j].destArea == pn->destArea) {
                    seen = TRUE;
                    break;
                }
            }
            if (!seen) {
                out[n].kind = LPK_PAINTING;
                out[n].index = (u8) k;
                out[n].destLevel = lvl;
                out[n].destArea = pn->destArea;
                n++;
            }
        }
    }
    return n;
}

s32 lw_passage_swap(const LPassage *a, const LPassage *b) {
    struct WarpNode ta, tb;
    s32 k;

    if (gCurrentArea == NULL || a->kind != b->kind) {
        return FALSE;
    }
    if (a->kind == LPK_DOOR) {
        struct ObjectWarpNode *na = door_node(a->index, NULL);
        struct ObjectWarpNode *nb = door_node(b->index, NULL);

        if (na == NULL || nb == NULL || !save_node(&na->node) || !save_node(&nb->node)) {
            lw_passages_restore();
            return FALSE;
        }
        ta = na->node;
        na->node.destLevel = nb->node.destLevel;
        na->node.destArea = nb->node.destArea;
        na->node.destNode = nb->node.destNode;
        nb->node.destLevel = ta.destLevel;
        nb->node.destArea = ta.destArea;
        nb->node.destNode = ta.destNode;
        return TRUE;
    }
    if (gCurrentArea->paintingWarpNodes == NULL || a->index >= 45 || b->index >= 45) {
        return FALSE;
    }
    /* every painting node of destination A now leads to B and vice versa */
    ta = gCurrentArea->paintingWarpNodes[a->index];
    tb = gCurrentArea->paintingWarpNodes[b->index];
    for (k = 0; k < 45; k++) {
        struct WarpNode *pn = &gCurrentArea->paintingWarpNodes[k];
        u8 lvl = pn->destLevel & 0x7F;
        const struct WarpNode *src = NULL;

        if (pn->id == 0) {
            continue;
        }
        if (lvl == a->destLevel && pn->destArea == a->destArea) {
            src = &tb;
        } else if (lvl == b->destLevel && pn->destArea == b->destArea) {
            src = &ta;
        }
        if (src != NULL) {
            if (!save_node(pn)) {
                lw_passages_restore();
                return FALSE;
            }
            pn->destLevel = src->destLevel;
            pn->destArea = src->destArea;
            pn->destNode = src->destNode;
        }
    }
    return TRUE;
}

/*
 * Reachable = the room holding that destination's entrance is already open in THIS save
 * file (the game records opened star/key doors as save flags). Mario returns to that
 * room when he leaves the course, so it must be a room he can walk out of.
 * Bowser stages, cap courses, secret levels and the courtyard are never "reachable" here
 * (they can still be used once actually visited).
 */
s32 lw_destination_reachable(u8 level, u8 area) {
    u32 f = save_file_get_flags();

    switch (level) {
        case LEVEL_CASTLE_GROUNDS:
        case LEVEL_BOB:
            return TRUE;
        case LEVEL_CASTLE:
            if (area == 1) {
                return TRUE;
            }
            if (area == 2) {
                return (f & SAVE_FLAG_UNLOCKED_UPSTAIRS_DOOR) != 0;
            }
            if (area == 3) {
                return (f & SAVE_FLAG_UNLOCKED_BASEMENT_DOOR) != 0;
            }
            return FALSE;
        case LEVEL_WF:
            return (f & SAVE_FLAG_UNLOCKED_WF_DOOR) != 0;
        case LEVEL_JRB:
            return (f & SAVE_FLAG_UNLOCKED_JRB_DOOR) != 0;
        case LEVEL_CCM:
            return (f & SAVE_FLAG_UNLOCKED_CCM_DOOR) != 0;
        case LEVEL_PSS:
            return (f & SAVE_FLAG_UNLOCKED_PSS_DOOR) != 0;
        case LEVEL_HMC:
        case LEVEL_LLL:
        case LEVEL_SSL:
            return (f & SAVE_FLAG_UNLOCKED_BASEMENT_DOOR) != 0;
        case LEVEL_DDD:
            return (f & SAVE_FLAG_UNLOCKED_BASEMENT_DOOR) && (f & SAVE_FLAG_UNLOCKED_BITFS_DOOR);
        case LEVEL_SL:
        case LEVEL_WDW:
        case LEVEL_TTM:
        case LEVEL_THI:
            return (f & SAVE_FLAG_UNLOCKED_UPSTAIRS_DOOR) != 0;
        case LEVEL_TTC:
        case LEVEL_RR:
            return (f & SAVE_FLAG_UNLOCKED_UPSTAIRS_DOOR) && (f & SAVE_FLAG_UNLOCKED_50_STAR_DOOR);
        default:
            return FALSE;
    }
}

/* ---------------- ambience ---------------- */

static const u8 sTintPalette[3][3] = {
    { 70, 30, 10 }, /* dusk */
    { 10, 30, 80 }, /* cold */
    { 0, 0, 0 },    /* deep */
};
static u8 sTintPal;
static u8 sTintTarget;
static u8 sTintCur;
static u8 sMusicLowered;
static u8 sTempoPct = 100;  /* 100 = untouched */
static u16 sTempoSet;       /* tempo we wrote; if the song changes it, we follow */

static const s32 sSounds[LSND_COUNT] = {
    SOUND_GENERAL_QUIET_POUND1,  /* LSND_WIND (soft thud) */
    SOUND_GENERAL_BIG_CLOCK,     /* LSND_CLOCK */
    SOUND_OBJ_BOO_LAUGH_SHORT,   /* LSND_LAUGH */
    SOUND_MENU_COLLECT_SECRET,   /* LSND_SECRET */
    SOUND_GENERAL_QUIET_POUND1,  /* LSND_RUMBLE */
    SOUND_GENERAL2_RIGHT_ANSWER, /* LSND_CHIME */
};

void lw_fx(u8 fx, u8 a, u8 b) {
    (void) b;
    switch (fx) {
        case LFX_RESET:
            sTintTarget = 0;
            sTintCur = 0;
            if (sTempoPct != 100 && gSequencePlayers[SEQ_PLAYER_LEVEL].tempo == sTempoSet && sTempoSet) {
                gSequencePlayers[SEQ_PLAYER_LEVEL].tempo = (u16) ((u32) sTempoSet * 100 / sTempoPct);
            }
            sTempoPct = 100;
            if (sMusicLowered) {
                seq_player_unlower_volume(SEQ_PLAYER_LEVEL, 30);
                sMusicLowered = FALSE;
            }
            break;
        case LFX_TINT:
            break; /* removed: a full-screen tint read as a corrupted ROM */
        case LFX_MUSIC:
            if (a >= 100) {
                if (sMusicLowered) {
                    seq_player_unlower_volume(SEQ_PLAYER_LEVEL, 60);
                    sMusicLowered = FALSE;
                }
            } else {
                seq_player_lower_volume(SEQ_PLAYER_LEVEL, 60, a);
                sMusicLowered = TRUE;
            }
            break;
        case LFX_SOUND:
            if (a < LSND_COUNT) {
                play_sound(sSounds[a], gGlobalSoundSource);
            }
            break;
        case LFX_TEMPO:
            sTempoPct = (u8) (a < 70 ? 70 : (a > 100 ? 100 : a)); /* never more than 30% slower */
            sTempoSet = 0; /* living_tempo_tick applies it */
            break;
        case LFX_SHAKE:
            set_camera_shake_from_point(SHAKE_POS_SMALL, gMarioState->pos[0], gMarioState->pos[1],
                                        gMarioState->pos[2]);
            break;
    }
}

void living_hook_render(void) {
    const u8 *c;

#ifdef LIVING_NO_TINT
    return;
#endif

    if (sTintCur < sTintTarget) {
        sTintCur = (u8) (sTintCur + 2 > sTintTarget ? sTintTarget : sTintCur + 2);
    } else if (sTintCur > sTintTarget) {
        sTintCur = (u8) (sTintCur < sTintTarget + 2 ? sTintTarget : sTintCur - 2);
    }
    if (sTintCur == 0) {
        return;
    }
    c = sTintPalette[sTintPal];
    gDPPipeSync(gDisplayListHead++);
    gDPSetCycleType(gDisplayListHead++, G_CYC_1CYCLE);
    gDPSetRenderMode(gDisplayListHead++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gDisplayListHead++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gDisplayListHead++, 0, 0, c[0], c[1], c[2], sTintCur);
    gDPFillRectangle(gDisplayListHead++, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
    gDPPipeSync(gDisplayListHead++);
    gDPSetRenderMode(gDisplayListHead++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetCombineMode(gDisplayListHead++, G_CC_SHADE, G_CC_SHADE);
}

/* Keeps the level music at sTempoPct even when the sequence re-sets its tempo. */
static void living_tempo_tick(void) {
    struct SequencePlayer *sp = &gSequencePlayers[SEQ_PLAYER_LEVEL];

    if (sTempoPct >= 100 || sp->tempo == 0) {
        return;
    }
    if (sp->tempo != sTempoSet) {
        sTempoSet = (u16) ((u32) sp->tempo * sTempoPct / 100);
        sp->tempo = sTempoSet;
    }
}

/* ---------------- EEPROM backend ---------------- */

s32 lstore_backend_present(void) {
    return gEepromProbe == EEPROM_TYPE_16K;
}

s32 lstore_backend_read(u8 block, u8 *buf8) {
    return osEepromRead(&gSIEventMesgQueue, block, buf8);
}

s32 lstore_backend_write(u8 block, const u8 *buf8) {
    return osEepromWrite(&gSIEventMesgQueue, block, (u8 *) buf8);
}

/* ---------------- hooks ---------------- */

static u32 sLastAction;
static u8 sDeathPending;
static u8 sSeeded;

#ifdef LIVING_SELFTEST
static struct {
    u32 frames;
    u32 sumUpdate;
    u32 maxUpdate;
    u32 maxEnter;
    u16 enters;
    u32 mainFrames; /* heartbeat: every main-loop iteration, gameplay or not */
} sDiag;

/* Breadcrumbs for crashes during the first seconds: the last hook entered is written to
 * EEPROM block 251 (self-test builds only, first 600 main-loop frames only). */
static u8 sCrumb;
static void crumb(u8 id) {
    u8 b[8];

    if (id == sCrumb || sDiag.mainFrames > 600 || !lstore_present()) {
        return;
    }
    sCrumb = id;
    b[0] = 'C';
    b[1] = 'R';
    b[2] = id;
    b[3] = (u8) gCurrLevelNum;
    b[4] = (u8) (sDiag.mainFrames >> 24);
    b[5] = (u8) (sDiag.mainFrames >> 16);
    b[6] = (u8) (sDiag.mainFrames >> 8);
    b[7] = (u8) sDiag.mainFrames;
    lstore_write(251, b);
}
#define CRUMB(id) crumb(id)

void living_trace(u8 step) {
    u8 b[8];

    if (step == sCrumb || !lstore_present()) {
        return;
    }
    sCrumb = step;
    b[0] = 'C';
    b[1] = 'R';
    b[2] = step;
    b[3] = (u8) gCurrLevelNum;
    b[4] = (u8) (sDiag.mainFrames >> 24);
    b[5] = (u8) (sDiag.mainFrames >> 16);
    b[6] = (u8) (sDiag.mainFrames >> 8);
    b[7] = (u8) sDiag.mainFrames;
    lstore_write(251, b);
}
#else
#define CRUMB(id)
#endif

static u8 classify_action(u32 action) {
    switch (action) {
        case ACT_JUMP:
        case ACT_DOUBLE_JUMP:
            return LA_JUMP;
        case ACT_TRIPLE_JUMP:
        case ACT_SPECIAL_TRIPLE_JUMP:
            return LA_TRIPLE;
        case ACT_BACKFLIP:
            return LA_BACKFLIP;
        case ACT_LONG_JUMP:
            return LA_LONGJUMP;
        case ACT_WALL_KICK_AIR:
            return LA_WALLKICK;
        case ACT_GROUND_POUND:
            return LA_GROUNDPOUND;
        case ACT_DIVE:
            return LA_DIVE;
        case ACT_FLYING:
            return LA_FLY;
    }
    if ((action & ACT_GROUP_MASK) == ACT_GROUP_SUBMERGED) {
        return LA_SWIM;
    }
    return LA_NONE;
}

static s32 is_death_action(u32 action) {
    return action == ACT_STANDING_DEATH || action == ACT_DEATH_ON_STOMACH || action == ACT_DEATH_ON_BACK
           || action == ACT_QUICKSAND_DEATH || action == ACT_ELECTROCUTION || action == ACT_SUFFOCATION
           || action == ACT_DROWNING || action == ACT_WATER_DEATH;
}

#ifndef LIVING_SELFTEST
/*
 * Vortex64 only (1.1 and later, and Vortex64 Living). The emulator answers a signature at an address 64 words past
 * any cartridge (PI 0x1FB00000..0x1FB000FF); a console or any other emulator answers the open-bus
 * pattern there instead. Without the signature the game does not start: a screen says where it runs.
 * (Self-test builds skip this: they run in the test copy of the old emulator.)
 */
static u32 living_sig(u32 paddr) {
    u32 x = paddr * 0x9E3779B1U;

    x ^= x >> 15;
    x *= 0x85EBCA77U;
    x ^= x >> 13;
    return x ^ 0x56583634U; /* "VX64" */
}

static s32 living_on_vortex64_living(void) {
    s32 attempt, i;

    for (attempt = 0; attempt < 3; attempt++) {
        for (i = 0; i < 4; i++) {
            u32 dev = 0x0FB00000 + (u32) i * 0x24;
            u32 got = 0;

            osPiRawReadIo(dev, &got); /* (osRomBase | dev: an address the recompiler cannot fold) */
            if (got != living_sig((u32) (0x10000000 | dev))) {
                break;
            }
        }
        if (i == 4) {
            return TRUE;
        }
    }
    return FALSE;
}

/* 5x7 capitals, one byte per row (bit 4 = left column) */
static const char sLockChars[] = "ACEFGILMNOPRSTUVX46";
static const u8 sLockFont[19][7] = {
    { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, /* A */
    { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, /* C */
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, /* E */
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 }, /* F */
    { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F }, /* G */
    { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, /* I */
    { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F }, /* L */
    { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, /* M */
    { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 }, /* N */
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, /* O */
    { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 }, /* P */
    { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 }, /* R */
    { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }, /* S */
    { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, /* T */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, /* U */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 }, /* V */
    { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 }, /* X */
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, /* 4 */
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, /* 6 */
};

static void living_lock_text(u16 *fb, s32 y, const char *text, u16 color) {
    s32 len = 0, x, c, row, col, k;

    while (text[len] != '\0') {
        len++;
    }
    x = (SCREEN_WIDTH - len * 12) / 2;
    for (c = 0; c < len; c++, x += 12) {
        for (k = 0; sLockChars[k] != '\0' && sLockChars[k] != text[c]; k++) {
        }
        if (sLockChars[k] == '\0') {
            continue; /* a space */
        }
        for (row = 0; row < 14; row++) {
            for (col = 0; col < 10; col++) {
                if (sLockFont[k][row >> 1] & (0x10 >> (col >> 1))) {
                    fb[(y + row) * SCREEN_WIDTH + x + col] = color;
                }
            }
        }
    }
}

static void living_lock_screen(void) {
    u16 *fb = (u16 *) ((uintptr_t) gFramebuffer0 | 0xA0000000); /* uncached: no write-back needed */
    OSMesgQueue queue;
    OSMesg msg;
    s32 i;

    for (i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        fb[i] = (2 << 11) | (2 << 6) | (6 << 1) | 1; /* night blue */
    }
    living_lock_text(fb, 70, "SM64 LIVING ROM", (31 << 11) | (26 << 6) | (4 << 1) | 1);
    living_lock_text(fb, 112, "ESTA COPIA SOLO FUNCIONA", 0xFFFF);
    living_lock_text(fb, 136, "EN VORTEX64", 0xFFFF);
    osViSwapBuffer(gFramebuffer0);
    osViBlack(FALSE);
    osCreateMesgQueue(&queue, &msg, 1);
    osRecvMesg(&queue, NULL, OS_MESG_BLOCK); /* nothing ever arrives: the game stops here */
}
#endif

void living_hook_boot(void) {
#ifndef LIVING_SELFTEST
    if (!living_on_vortex64_living()) {
        living_lock_screen();
    }
#endif
    living_boot(osGetCount());
    CRUMB(1);
}

void living_hook_area_loaded(void) {
    CRUMB(2);
    living_register_pieces();
    lw_release_all();
    living_area_loaded((u8) gCurrLevelNum, (u8) gCurrAreaIndex);
    CRUMB(3);
}

void living_hook_area_unloading(void) {
    CRUMB(4);
    living_area_unloading(); /* also restores any shifted passage */
    CRUMB(5);
    lw_passages_restore();   /* belt and braces: the AI may be disabled */
    lw_release_all();
    CRUMB(6);
}

void living_hook_death(void) {
    sDeathPending = TRUE;
}

void living_hook_game_saved(void) {
    living_request_commit();
}

void living_hook_update(void) {
    struct MarioState *m = gMarioState;
    struct Controller *c = gPlayer1Controller;
    LivingInput in;
    u16 camButtons = U_CBUTTONS | D_CBUTTONS | L_CBUTTONS | R_CBUTTONS | R_TRIG;

    CRUMB(7);
    if (m == NULL || gMarioObject == NULL || c == NULL) {
        return;
    }
#ifndef LIVING_SELFTEST
    if (gCurrDemoInput != NULL) {
        return; /* never learn from the attract-mode demo (the self-test build does, on purpose) */
    }
#endif
    if (!sSeeded) {
        sSeeded = TRUE;
        living_add_entropy(osGetCount() ^ (gGlobalTimer << 16)); /* human timing from the menus */
    }
    in.pos.x = (s16) m->pos[0];
    in.pos.y = (s16) m->pos[1];
    in.pos.z = (s16) m->pos[2];
    in.actChanged = (u8) (m->action != sLastAction);
    in.actClass = classify_action(m->action);
    sLastAction = m->action;

    in.flags = 0;
    if (m->action & ACT_FLAG_IDLE) {
        in.flags |= LIF_IDLE_ACTION;
    }
    if (!(m->action & ACT_FLAG_AIR) && m->floor != NULL && m->pos[1] <= m->floorHeight + 4.0f) {
        in.flags |= LIF_ON_GROUND;
    }
    if ((m->action & ACT_GROUP_MASK) == ACT_GROUP_SUBMERGED) {
        in.flags |= LIF_IN_WATER;
    }
    if (sDeathPending || is_death_action(m->action) || m->health < 0x100) {
        in.flags |= LIF_DYING;
        sDeathPending = FALSE;
    }
    in.buttons = 0;
    if (c->buttonDown & camButtons) {
        in.buttons |= LB_CAMERA;
    }
    if (c->buttonDown != 0) {
        in.buttons |= LB_ANY;
    }
    in.stickMag = (u8) c->stickMag;
    in.coins = m->numCoins;
    in.stars = m->numStars;
    in.lives = m->numLives;

#ifdef LIVING_SELFTEST
    {
        static u8 sShowcased;
        static u16 sLastVisitSamples;
        /* 5 s into every visit: build in front of Mario (SM64 yaw 0 = +Z, 0x4000 = +X) */
        if (gLiving.trk.visitSamples < sLastVisitSamples) {
            sShowcased = FALSE;
        }
        sLastVisitSamples = gLiving.trk.visitSamples;
        if (!sShowcased && gLiving.trk.visitSamples >= 3 && (in.flags & LIF_ON_GROUND)) {
            u8 dir = (u8) (((u16) (0x4000 - m->faceAngle[1] + 0x1000) >> 13) & 7);
            static u8 sShowIdx;
            u8 res[8];
            u16 vf0 = gLiving.mut.validationFailures;
            u8 c0 = gLiving.mut.spawnedThisArea;

            sShowcased = TRUE;
            res[2] = living_debug_showcase(&in.pos, dir, (u8) gCurrLevelNum);
            /* record WHY: level, dir, ok, lastFail, box model available, boxes kept, failures */
            res[0] = (u8) gCurrLevelNum;
            res[1] = dir;
            res[3] = gLiving.mut.lastFail;
            res[4] = (u8) lw_kind_available(LK_BOX);
            res[5] = (u8) (gLiving.mut.spawnedThisArea - c0);
            res[6] = (u8) (gLiving.mut.validationFailures - vf0);
            res[7] = (u8) lw_object_count();
            lstore_write((u8) (247 + (sShowIdx++ & 3)), res);
        }
    }
    {
        u8 entering = gLiving.pendingApply;
        u32 t0 = osGetCount();
        u32 dt;

        living_update(&in);
        dt = osGetCount() - t0;
        if (entering) {
            sDiag.enters++;
            if (dt > sDiag.maxEnter) {
                sDiag.maxEnter = dt;
            }
        } else {
            sDiag.frames++;
            sDiag.sumUpdate += dt;
            if (dt > sDiag.maxUpdate) {
                sDiag.maxUpdate = dt;
            }
        }
    }
#else
    living_update(&in);
#endif
}

/*
 * Main loop, once per frame (also outside gameplay): trickles the EEPROM commit.
 * Self-test builds also publish measurements in blocks 253-255 (beyond the 82
 * blocks a copy uses), so a run in an emulator can be read back from the .eep file.
 */
void living_hook_frame(void) {
#ifdef LIVING_SELFTEST
    static u16 sDiagTimer;
    static u8 sDiagBlock;
    u8 blk[8];
    u32 avg;
    s32 i;

    sDiag.mainFrames++;
    living_tempo_tick();
    CRUMB(8);
    if (living_storage_tick() || !lstore_present()) {
        return; /* one SI write per frame at most */
    }
    if (++sDiagTimer < 150 && sDiagBlock == 0) {
        return;
    }
    sDiagTimer = 0;
    for (i = 0; i < 8; i++) {
        blk[i] = 0;
    }
    avg = sDiag.frames ? sDiag.sumUpdate / sDiag.frames : 0;
    /* if/else on purpose: a 4-case switch becomes a jump table, and Vortex64's
     * recompiler mis-handled that jump table in this per-frame function (see docs) */
    if (sDiagBlock == 0) { /* 'D' 'G' version loadResult | maxUpdate (ticks of osGetCount) */
        blk[0] = 'D'; blk[1] = 'G'; blk[2] = 1; blk[3] = gLiving.sm.lastLoad;
        blk[4] = (u8) (sDiag.maxUpdate >> 24); blk[5] = (u8) (sDiag.maxUpdate >> 16);
        blk[6] = (u8) (sDiag.maxUpdate >> 8); blk[7] = (u8) sDiag.maxUpdate;
    } else if (sDiagBlock == 1) { /* avgUpdate | maxEnter */
        blk[0] = (u8) (avg >> 24); blk[1] = (u8) (avg >> 16); blk[2] = (u8) (avg >> 8); blk[3] = (u8) avg;
        blk[4] = (u8) (sDiag.maxEnter >> 24); blk[5] = (u8) (sDiag.maxEnter >> 16);
        blk[6] = (u8) (sDiag.maxEnter >> 8); blk[7] = (u8) sDiag.maxEnter;
    } else if (sDiagBlock == 3) { /* heartbeat: main-loop frames | level | area | entry stage | play mode */
        blk[0] = (u8) (sDiag.mainFrames >> 24); blk[1] = (u8) (sDiag.mainFrames >> 16);
        blk[2] = (u8) (sDiag.mainFrames >> 8); blk[3] = (u8) sDiag.mainFrames;
        blk[4] = (u8) gCurrLevelNum; blk[5] = (u8) gCurrAreaIndex;
        blk[6] = gLiving.pendingApply; blk[7] = (u8) sCurrPlayMode;
    } else { /* frames measured | enters | mood | applied ops (low byte) */
        blk[0] = (u8) (sDiag.frames >> 24); blk[1] = (u8) (sDiag.frames >> 16);
        blk[2] = (u8) (sDiag.frames >> 8); blk[3] = (u8) sDiag.frames;
        blk[4] = (u8) (sDiag.enters >> 8); blk[5] = (u8) sDiag.enters;
        blk[6] = gLiving.mem.mood; blk[7] = (u8) gLiving.mem.mutApplied;
    }
    lstore_write((u8) (sDiagBlock == 3 ? 252 : 253 + sDiagBlock), blk);
    sDiagBlock = (u8) ((sDiagBlock + 1) % 4);
#else
    living_tempo_tick();
    living_storage_tick();
#endif
}

#endif /* LIVING_ROM */
