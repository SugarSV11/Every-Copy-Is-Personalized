/* Host-side fake SM64 world + fake 16 Kbit EEPROM with fault injection. */
#ifndef MOCK_WORLD_H
#define MOCK_WORLD_H

#include "../src/living.h"

#define MOCK_MAX_OBJ 64
#define MOCK_MAX_RECT 16

typedef struct {
    s16 x0, z0, x1, z1;
    s16 y;
    s16 ceilY;
    u8 surf;
} MockRect;

typedef struct {
    u8 used;
    u8 alive;
    u8 kind;
    u8 byAI;
    LVec3 pos;
    LVec3 home;
    u8 scale;
} MockObj;

typedef struct {
    MockRect rect[MOCK_MAX_RECT];
    int nrect;
    MockObj obj[MOCK_MAX_OBJ];
    u8 kindAvailable[LK_COUNT];
    u16 baseObjects;      /* objects of the level the AI does not see */
    LVec3 player;
    /* fx log */
    u8 tintPal, tintAlpha, music;
    u16 sounds, shakes;
    /* eeprom */
    u8 eeprom[2048];
    u8 eepromPresent;
    long writesLeft;      /* <0 = unlimited; 0 = writes fail (power loss / dead chip) */
    u32 totalWrites;
    u32 probes;
    /* passages of the current area: dest[i] = where passage i leads now, orig = original */
    LPassage passage[8];
    LPassage passageOrig[8];
    u8 npassage;
    u8 reachable[64];     /* [level] = 1 if reachable with the current save */
    u16 swaps;
    u8 tempo;
} MockWorld;

extern MockWorld gMock;

void mock_reset_world(void);
void mock_reset_eeprom(void);
void mock_add_rect(s16 x0, s16 z0, s16 x1, s16 z1, s16 y, s16 ceilY, u8 surf);
int mock_add_existing(u8 kind, s16 x, s16 y, s16 z);
void mock_unload_area(void);
int mock_count_alive(int byAI, int kind);
int mock_collect_near(s16 radius);
void mock_add_passage(u8 kind, u8 destLevel, u8 destArea);

#endif
