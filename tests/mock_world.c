#include <string.h>
#include "mock_world.h"

MockWorld gMock;

void mock_reset_world(void) {
    u8 eep[2048];
    u8 present = gMock.eepromPresent;
    long wl = gMock.writesLeft;
    u32 tw = gMock.totalWrites;
    int i;

    memcpy(eep, gMock.eeprom, sizeof(eep));
    memset(&gMock, 0, sizeof(gMock));
    memcpy(gMock.eeprom, eep, sizeof(eep));
    gMock.eepromPresent = present;
    gMock.writesLeft = wl;
    gMock.totalWrites = tw;
    for (i = 0; i < LK_COUNT; i++) {
        gMock.kindAvailable[i] = 1;
    }
    gMock.baseObjects = 60;
    gMock.music = 100;
    gMock.tempo = 100;
    gMock.kindAvailable[LK_TREE] = 0; /* trees exist, but are never spawned */
}

void mock_reset_eeprom(void) {
    memset(gMock.eeprom, 0, sizeof(gMock.eeprom));
    /* original SM64 save area gets a recognisable pattern so tests can prove it is untouched */
    {
        int i;
        for (i = 0; i < 512; i++) {
            gMock.eeprom[i] = (u8) (i * 7 + 3);
        }
    }
    gMock.eepromPresent = 1;
    gMock.writesLeft = -1;
    gMock.totalWrites = 0;
}

void mock_add_rect(s16 x0, s16 z0, s16 x1, s16 z1, s16 y, s16 ceilY, u8 surf) {
    MockRect *r = &gMock.rect[gMock.nrect++];
    r->x0 = x0; r->z0 = z0; r->x1 = x1; r->z1 = z1;
    r->y = y; r->ceilY = ceilY; r->surf = surf;
}

int mock_add_existing(u8 kind, s16 x, s16 y, s16 z) {
    int i;
    for (i = 0; i < MOCK_MAX_OBJ; i++) {
        if (!gMock.obj[i].used) {
            MockObj *o = &gMock.obj[i];
            o->used = 1; o->alive = 1; o->kind = kind; o->byAI = 0;
            o->pos.x = x; o->pos.y = y; o->pos.z = z;
            o->home = o->pos;
            o->scale = 100;
            return i;
        }
    }
    return -1;
}

void mock_unload_area(void) {
    memset(gMock.obj, 0, sizeof(gMock.obj));
}

int mock_count_alive(int byAI, int kind) {
    int i, n = 0;
    for (i = 0; i < MOCK_MAX_OBJ; i++) {
        MockObj *o = &gMock.obj[i];
        if (o->used && o->alive && (byAI < 0 || o->byAI == byAI) && (kind < 0 || o->kind == kind)) {
            n++;
        }
    }
    return n;
}

/* the player touches collectibles and smashes boxes it walks into */
int mock_collect_near(s16 radius) {
    int i, n = 0;
    for (i = 0; i < MOCK_MAX_OBJ; i++) {
        MockObj *o = &gMock.obj[i];
        long dx, dz;
        if (!o->used || !o->alive) {
            continue;
        }
        dx = o->pos.x - gMock.player.x;
        dz = o->pos.z - gMock.player.z;
        if (dx * dx + dz * dz <= (long) radius * radius && o->kind != LK_GOOMBA_HUGE) {
            o->alive = 0;
            n++;
        }
    }
    return n;
}

/* ---------------- world interface ---------------- */

s32 lw_probe(s16 x, s16 y, s16 z, LFloor *out) {
    int i, best = -1;
    gMock.probes++;
    for (i = 0; i < gMock.nrect; i++) {
        MockRect *r = &gMock.rect[i];
        if (x >= r->x0 && x <= r->x1 && z >= r->z0 && z <= r->z1 && r->y <= y) {
            if (best < 0 || r->y >= gMock.rect[best].y) { /* later rects override (rooms inside the map) */
                best = i;
            }
        }
    }
    if (best < 0) {
        return 0;
    }
    out->floorY = gMock.rect[best].y;
    out->ceilY = gMock.rect[best].ceilY;
    out->surf = gMock.rect[best].surf;
    out->normalY100 = 100;
    return 1;
}

LHandle lw_spawn(u8 kind, const LVec3 *pos, s16 yaw) {
    int i = mock_add_existing(kind, pos->x, pos->y, pos->z);
    (void) yaw;
    if (i < 0) {
        return LH_INVALID;
    }
    gMock.obj[i].byAI = 1;
    return (LHandle) i;
}

void lw_despawn(LHandle h) {
    if (h < MOCK_MAX_OBJ) {
        memset(&gMock.obj[h], 0, sizeof(MockObj));
    }
}

s32 lw_alive(LHandle h) {
    return h < MOCK_MAX_OBJ && gMock.obj[h].used && gMock.obj[h].alive;
}

void lw_get_pos(LHandle h, LVec3 *out) {
    if (h < MOCK_MAX_OBJ) *out = gMock.obj[h].pos;
}

void lw_set_pos(LHandle h, const LVec3 *p) {
    if (h < MOCK_MAX_OBJ) gMock.obj[h].pos = *p;
}

void lw_get_home(LHandle h, LVec3 *out) {
    if (h < MOCK_MAX_OBJ) *out = gMock.obj[h].home;
}

void lw_set_home(LHandle h, const LVec3 *p) {
    if (h < MOCK_MAX_OBJ) gMock.obj[h].home = *p;
}

s32 lw_kind_available(u8 kind) {
    return kind < LK_COUNT && gMock.kindAvailable[kind];
}

u16 lw_object_count(void) {
    return (u16) (gMock.baseObjects + mock_count_alive(-1, -1));
}

LHandle lw_find_existing(u8 kind, u8 nth) {
    int i, n = 0;
    for (i = 0; i < MOCK_MAX_OBJ; i++) {
        MockObj *o = &gMock.obj[i];
        if (o->used && o->alive && !o->byAI && o->kind == kind) {
            if (n++ == nth) {
                return (LHandle) i;
            }
        }
    }
    return LH_INVALID;
}

void lw_release(LHandle h) { (void) h; }
void lw_release_all(void) {}

void lw_fx(u8 fx, u8 a, u8 b) {
    switch (fx) {
        case LFX_RESET: gMock.tintAlpha = 0; gMock.music = 100; break;
        case LFX_TINT: gMock.tintPal = a; gMock.tintAlpha = b; break;
        case LFX_MUSIC: gMock.music = a; break;
        case LFX_SOUND: gMock.sounds++; break;
        case LFX_SHAKE: gMock.shakes++; break;
        case LFX_TEMPO: gMock.tempo = a; break;
    }
}

u8 lw_get_scale(LHandle h) {
    return h < MOCK_MAX_OBJ ? gMock.obj[h].scale : 100;
}

void lw_set_scale(LHandle h, u8 percent) {
    if (h < MOCK_MAX_OBJ) gMock.obj[h].scale = percent;
}

void lw_player_pos(LVec3 *out) {
    *out = gMock.player;
}

/* ---------------- storage backend ---------------- */

s32 lstore_backend_present(void) {
    return gMock.eepromPresent;
}

s32 lstore_backend_read(u8 block, u8 *buf8) {
    memcpy(buf8, &gMock.eeprom[block * 8], 8);
    return 0;
}

s32 lstore_backend_write(u8 block, const u8 *buf8) {
    if (gMock.writesLeft == 0) {
        return -1;
    }
    if (gMock.writesLeft > 0) {
        gMock.writesLeft--;
    }
    memcpy(&gMock.eeprom[block * 8], buf8, 8);
    gMock.totalWrites++;
    return 0;
}

/* ---------------- passages ---------------- */

void mock_add_passage(u8 kind, u8 destLevel, u8 destArea) {
    LPassage *p = &gMock.passage[gMock.npassage];
    p->kind = kind;
    p->index = gMock.npassage;
    p->destLevel = destLevel;
    p->destArea = destArea;
    gMock.passageOrig[gMock.npassage] = *p;
    gMock.npassage++;
}

u8 lw_passages(LPassage *out, u8 max) {
    u8 i;
    for (i = 0; i < gMock.npassage && i < max; i++) {
        out[i] = gMock.passage[i];
    }
    return i;
}

s32 lw_passage_swap(const LPassage *a, const LPassage *b) {
    u8 la = gMock.passage[a->index].destLevel, aa = gMock.passage[a->index].destArea;
    gMock.passage[a->index].destLevel = gMock.passage[b->index].destLevel;
    gMock.passage[a->index].destArea = gMock.passage[b->index].destArea;
    gMock.passage[b->index].destLevel = la;
    gMock.passage[b->index].destArea = aa;
    gMock.swaps++;
    return 1;
}

void lw_passages_restore(void) {
    u8 i;
    for (i = 0; i < gMock.npassage; i++) {
        gMock.passage[i] = gMock.passageOrig[i];
    }
}

s32 lw_destination_reachable(u8 level, u8 area) {
    (void) area;
    return level < 64 && gMock.reachable[level];
}
