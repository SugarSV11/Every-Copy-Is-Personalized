#include "save_manager.h"

#define COPY_BASE(c) ((u8) (LIVING_EEP_FIRST_BLOCK + (c) * LIVING_COPY_BLOCKS))
#define PAYLOAD_V1_BYTES 640
/* v2 = v1 + 4 bytes appended (passage shifts). v1 is a strict prefix of v2, so a v1
 * copy migrates by reading it and leaving the new fields at their defaults. */
#define PAYLOAD_V2_BYTES 644
#define PAYLOAD_BYTES PAYLOAD_V2_BYTES

LIVING_STATIC_ASSERT(LIVING_EEP_FIRST_BLOCK + 2 * LIVING_COPY_BLOCKS <= 256, eeprom16k_fits);

u16 lsm_crc16(const u8 *data, u16 len, u16 crc) {
    u16 i;
    s32 b;

    for (i = 0; i < len; i++) {
        crc ^= (u16) ((u16) data[i] << 8);
        for (b = 0; b < 8; b++) {
            crc = (u16) ((crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1));
        }
    }
    return crc;
}

/* ---------------- byte stream helpers (big-endian, bounds-checked) ---------------- */

typedef struct {
    u8 *p;
    const u8 *rp;
    u16 pos;
    u16 cap;
    u8 overflow;
} LStream;

static void w8(LStream *s, u8 v) {
    if (s->pos >= s->cap) {
        s->overflow = 1;
        return;
    }
    s->p[s->pos++] = v;
}
static void w16(LStream *s, u16 v) {
    w8(s, (u8) (v >> 8));
    w8(s, (u8) v);
}
static void w32(LStream *s, u32 v) {
    w16(s, (u16) (v >> 16));
    w16(s, (u16) v);
}
static u8 r8(LStream *s) {
    if (s->pos >= s->cap) {
        s->overflow = 1;
        return 0;
    }
    return s->rp[s->pos++];
}
static u16 r16(LStream *s) {
    u16 hi = r8(s);
    return (u16) ((hi << 8) | r8(s));
}
static u32 r32(LStream *s) {
    u32 hi = r16(s);
    return (hi << 16) | r16(s);
}

u16 lsm_serialize(const LivingMemory *mem, u8 *out, u16 cap) {
    LStream s;
    s32 i, k;

    s.p = out;
    s.rp = out;
    s.pos = 0;
    s.cap = cap;
    s.overflow = 0;

    w32(&s, mem->seed);
    w16(&s, mem->sessions);
    w32(&s, mem->playSec);
    for (i = 0; i < LA_COUNT; i++) {
        w16(&s, mem->act[i]);
    }
    w16(&s, mem->coins);
    w16(&s, mem->stars);
    w16(&s, mem->deaths);
    w16(&s, mem->camChanges);
    w16(&s, mem->idleSec);
    w16(&s, mem->loops);
    w16(&s, mem->areaChanges);
    for (i = 0; i < LT_COUNT; i++) {
        w8(&s, (u8) mem->trait[i]);
    }
    w8(&s, mem->mood);
    w8(&s, mem->moodAge);
    w16(&s, mem->reflections);
    for (i = 0; i < LOP_COUNT; i++) {
        w8(&s, mem->opAffinity[i]);
    }
    w32(&s, mem->eventsSeen);
    w16(&s, mem->eventsFired);
    w16(&s, mem->mutApplied);
    w16(&s, mem->mutRejected);
    w8(&s, mem->recoveries);

    for (i = 0; i < LIVING_MAX_AREAS; i++) {
        const LivingAreaMem *a = &mem->areas[i];
        w8(&s, a->level);
        w8(&s, a->area);
        w8(&s, a->visits);
        w8(&s, a->returns);
        w16(&s, a->dwellSec);
        w8(&s, a->deaths);
        w8(&s, a->lastSession);
        for (k = 0; k < 8; k++) {
            w8(&s, a->explored[k]);
        }
        w8(&s, a->hotCell);
        w8(&s, a->deathCell);
        w8(&s, a->generation);
        w8(&s, a->flags);
        w8(&s, a->visitsSinceGen);
        for (k = 0; k < LIVING_PLAN_OPS; k++) {
            w8(&s, a->plan[k].op);
            w8(&s, a->plan[k].param);
        }
        for (k = 0; k < LIVING_ANCHORS; k++) {
            w16(&s, (u16) a->anchor[k].x);
            w16(&s, (u16) a->anchor[k].y);
            w16(&s, (u16) a->anchor[k].z);
        }
    }
    /* v2 tail */
    w8(&s, mem->shiftSession);
    w8(&s, mem->shiftCount);
    w8(&s, 0); /* reserved */
    w8(&s, 0); /* reserved */
    /* pad to the fixed payload size so the layout never shifts silently */
    while (s.pos < PAYLOAD_BYTES && !s.overflow) {
        w8(&s, 0);
    }
    return s.overflow ? 0 : s.pos;
}

static s16 clamp_coord(s16 v, u16 *fix) {
    if (v < LIVING_SAFE_MIN || v > LIVING_SAFE_MAX) {
        (*fix)++;
        return 0;
    }
    return v;
}

s32 lsm_deserialize(LivingMemory *mem, const u8 *in, u16 len, u16 *sanitized) {
    LStream s;
    s32 i, k, j;
    u16 fix = 0;

    if (len != PAYLOAD_V1_BYTES && len != PAYLOAD_V2_BYTES) {
        return 0;
    }
    s.p = 0;
    s.rp = in;
    s.pos = 0;
    s.cap = len;
    s.overflow = 0;

    lm_init_fresh(mem, 1);
    mem->seed = r32(&s);
    mem->sessions = r16(&s);
    mem->playSec = r32(&s);
    for (i = 0; i < LA_COUNT; i++) {
        mem->act[i] = r16(&s);
    }
    mem->coins = r16(&s);
    mem->stars = r16(&s);
    mem->deaths = r16(&s);
    mem->camChanges = r16(&s);
    mem->idleSec = r16(&s);
    mem->loops = r16(&s);
    mem->areaChanges = r16(&s);
    for (i = 0; i < LT_COUNT; i++) {
        s8 t = (s8) r8(&s);
        if (t < -100 || t > 100) {
            t = (s8) LIVING_CLAMP(t, -100, 100);
            fix++;
        }
        mem->trait[i] = t;
    }
    mem->mood = r8(&s);
    if (mem->mood >= LM_COUNT) {
        mem->mood = LM_OBSERVING;
        fix++;
    }
    mem->moodAge = r8(&s);
    mem->reflections = r16(&s);
    for (i = 0; i < LOP_COUNT; i++) {
        mem->opAffinity[i] = r8(&s);
    }
    mem->eventsSeen = r32(&s);
    mem->eventsFired = r16(&s);
    mem->mutApplied = r16(&s);
    mem->mutRejected = r16(&s);
    mem->recoveries = r8(&s);
    if (mem->seed == 0) {
        mem->seed = 0x5EED1234;
        fix++;
    }

    for (i = 0; i < LIVING_MAX_AREAS; i++) {
        LivingAreaMem *a = &mem->areas[i];
        a->level = r8(&s);
        a->area = r8(&s);
        a->visits = r8(&s);
        a->returns = r8(&s);
        a->dwellSec = r16(&s);
        a->deaths = r8(&s);
        a->lastSession = r8(&s);
        for (k = 0; k < 8; k++) {
            a->explored[k] = r8(&s);
        }
        a->hotCell = r8(&s);
        a->deathCell = r8(&s);
        a->generation = r8(&s);
        a->flags = r8(&s);
        a->visitsSinceGen = r8(&s);
        for (k = 0; k < LIVING_PLAN_OPS; k++) {
            a->plan[k].op = r8(&s);
            a->plan[k].param = r8(&s);
            if ((a->plan[k].op & (u8) ~LOP_REJECTED_BIT) >= LOP_COUNT) {
                a->plan[k].op = LOP_NONE;
                fix++;
            }
        }
        for (k = 0; k < LIVING_ANCHORS; k++) {
            a->anchor[k].x = clamp_coord((s16) r16(&s), &fix);
            a->anchor[k].y = clamp_coord((s16) r16(&s), &fix);
            a->anchor[k].z = clamp_coord((s16) r16(&s), &fix);
        }
        if (a->hotCell >= LIVING_CELLS && a->hotCell != LIVING_CELL_NONE) {
            a->hotCell = LIVING_CELL_NONE;
            fix++;
        }
        if (a->deathCell >= LIVING_CELLS && a->deathCell != LIVING_CELL_NONE) {
            a->deathCell = LIVING_CELL_NONE;
            fix++;
        }
        /* invalid identifiers or duplicated keys: drop the whole slot */
        if (a->level > LIVING_MAX_LEVEL || a->area > LIVING_MAX_AREA_INDEX
            || (a->level == 0 && a->visits != 0)) {
            fix++;
            a->level = 0;
        }
        for (j = 0; j < i && a->level != 0; j++) {
            if (mem->areas[j].level == a->level && mem->areas[j].area == a->area) {
                fix++;
                a->level = 0;
            }
        }
        if (a->level == 0) {
            u8 *p = (u8 *) a;
            for (k = 0; k < (s32) sizeof(LivingAreaMem); k++) {
                p[k] = 0;
            }
            a->hotCell = LIVING_CELL_NONE;
            a->deathCell = LIVING_CELL_NONE;
        }
    }
    if (len >= PAYLOAD_V2_BYTES) {
        mem->shiftSession = r8(&s);
        mem->shiftCount = r8(&s);
        (void) r8(&s);
        (void) r8(&s);
    } /* else: migrated from v1, shift fields stay 0 */
    if (sanitized) {
        *sanitized = fix;
    }
    return !s.overflow;
}

/* ---------------- load ---------------- */

enum { CS_EMPTY, CS_VALID, CS_CORRUPT, CS_NEWER, CS_OLDER };

static u8 check_copy(const u8 *img, u16 *seqOut) {
    u16 magic = (u16) ((img[0] << 8) | img[1]);
    u8 version = img[2];
    u16 len = (u16) ((img[6] << 8) | img[7]);
    u16 crc = (u16) ((img[8] << 8) | img[9]);
    u16 calc;
    s32 i, blank = 1;

    /* a copy is "empty" when its header was never written (erased chip: 0x00 or 0xFF) */
    for (i = 0; i < LSAVE_HEADER_BYTES; i++) {
        if (img[i] != img[0] || (img[0] != 0x00 && img[0] != 0xFF)) {
            blank = 0;
            break;
        }
    }
    if (blank) {
        return CS_EMPTY;
    }
    if (magic != LIVING_SAVE_MAGIC || len > LIVING_COPY_BYTES - LSAVE_HEADER_BYTES) {
        return CS_CORRUPT;
    }
    calc = lsm_crc16(img, 8, 0xFFFF);
    calc = lsm_crc16(img + LSAVE_HEADER_BYTES, len, calc);
    if (calc != crc) {
        return CS_CORRUPT;
    }
    *seqOut = (u16) ((img[4] << 8) | img[5]);
    if (version > LIVING_SAVE_VERSION) {
        return CS_NEWER;
    }
    if (version < 1) {
        return CS_OLDER; /* pre-release format: unusable */
    }
    /* versions 1..LIVING_SAVE_VERSION are all readable (migration in lsm_deserialize) */
    return CS_VALID;
}

static void read_copy(LivingSaveMgr *sm, u8 c, u8 *ok) {
    u8 b;

    *ok = 1;
    for (b = 0; b < LIVING_COPY_BLOCKS; b++) {
        if (lstore_read((u8) (COPY_BASE(c) + b), &sm->image[c][b * 8]) != 0) {
            *ok = 0;
            return;
        }
    }
}

u8 lsm_load(LivingSaveMgr *sm, LivingMemory *mem, u32 freshSeed) {
    u8 st[2];
    u16 seq[2];
    u8 ok;
    s32 c, pick = -1;
    u16 fix = 0;
    u8 *p = (u8 *) sm;
    u32 i;

    for (i = 0; i < sizeof(LivingSaveMgr); i++) {
        p[i] = 0;
    }
    sm->active = 0xFF;
    lm_init_fresh(mem, freshSeed);

    if (!lstore_present()) {
        sm->mode = LSM_RAM_ONLY;
        return sm->lastLoad = LSL_NO_STORAGE;
    }
    for (c = 0; c < 2; c++) {
        seq[c] = 0;
        read_copy(sm, (u8) c, &ok);
        st[c] = ok ? check_copy(sm->image[c], &seq[c]) : CS_CORRUPT;
    }
    if (st[0] == CS_NEWER || st[1] == CS_NEWER) {
        sm->mode = LSM_RAM_ONLY;
        return sm->lastLoad = LSL_NEWER_VERSION;
    }
    for (c = 0; c < 2; c++) {
        if (st[c] == CS_VALID) {
            if (pick < 0 || (s16) (seq[c] - seq[pick]) > 0) {
                pick = c;
            }
        }
    }
    /* try the newest valid copy, fall back to the other if its payload fails sanitising */
    while (pick >= 0) {
        u16 len = (u16) ((sm->image[pick][6] << 8) | sm->image[pick][7]);
        if (lsm_deserialize(mem, sm->image[pick] + LSAVE_HEADER_BYTES, len, &fix)) {
            break;
        }
        st[pick] = CS_CORRUPT;
        pick = (st[pick ^ 1] == CS_VALID) ? (pick ^ 1) : -1;
    }
    if (pick < 0) {
        lm_init_fresh(mem, freshSeed);
        if (st[0] == CS_EMPTY && st[1] == CS_EMPTY) {
            return sm->lastLoad = LSL_FRESH;
        }
        mem->recoveries = 1;
        return sm->lastLoad = LSL_CORRUPT_RESET;
    }
    sm->active = (u8) pick;
    sm->seq = seq[pick];
    sm->sanitized = fix;
    if (st[pick ^ 1] == CS_CORRUPT || st[pick ^ 1] == CS_OLDER) {
        mem->recoveries = lm_sat_add8(mem->recoveries, 1);
        return sm->lastLoad = LSL_RECOVERED;
    }
    return sm->lastLoad = LSL_OK;
}

/* ---------------- commit / trickle writer ---------------- */

static void begin_commit(LivingSaveMgr *sm, const LivingMemory *mem) {
    u16 len, crc;
    u16 nextSeq = (u16) (sm->seq + 1);
    s32 i;

    for (i = 0; i < LIVING_COPY_BYTES; i++) {
        sm->stage[i] = 0;
    }
    len = lsm_serialize(mem, sm->stage + LSAVE_HEADER_BYTES, LIVING_COPY_BYTES - LSAVE_HEADER_BYTES);
    if (len == 0) {
        sm->errors++;
        return;
    }
    sm->stage[0] = (u8) (LIVING_SAVE_MAGIC >> 8);
    sm->stage[1] = (u8) (LIVING_SAVE_MAGIC & 0xFF);
    sm->stage[2] = LIVING_SAVE_VERSION;
    sm->stage[3] = 0;
    sm->stage[4] = (u8) (nextSeq >> 8);
    sm->stage[5] = (u8) nextSeq;
    sm->stage[6] = (u8) (len >> 8);
    sm->stage[7] = (u8) len;
    crc = lsm_crc16(sm->stage, 8, 0xFFFF);
    crc = lsm_crc16(sm->stage + LSAVE_HEADER_BYTES, len, crc);
    sm->stage[8] = (u8) (crc >> 8);
    sm->stage[9] = (u8) crc;

    sm->writeCopy = (sm->active == 0) ? 1 : 0;
    sm->nblocks = (u8) ((LSAVE_HEADER_BYTES + len + 7) / 8);
    sm->nextBlock = 1;
    sm->headerPending = 1;
    sm->state = LSW_WRITING;
    sm->pending = 0;
}

void lsm_request_commit(LivingSaveMgr *sm, const LivingMemory *mem) {
    if (sm->mode != LSM_PERSIST) {
        return;
    }
    sm->src = mem;
    if (sm->state == LSW_WRITING) {
        sm->pending = 1; /* re-serialise when the current commit lands */
        return;
    }
    begin_commit(sm, mem);
}

static s32 block_differs(const LivingSaveMgr *sm, u8 b) {
    const u8 *a = &sm->stage[b * 8];
    const u8 *o = &sm->image[sm->writeCopy][b * 8];
    s32 i;

    for (i = 0; i < 8; i++) {
        if (a[i] != o[i]) {
            return 1;
        }
    }
    return 0;
}

s32 lsm_tick(LivingSaveMgr *sm) {
    u8 b;
    s32 i;

    if (sm->state != LSW_WRITING || sm->mode != LSM_PERSIST) {
        return 0;
    }
    /* payload blocks first, unchanged ones skipped (cheap: 8-byte compares) */
    while (sm->nextBlock < sm->nblocks && !block_differs(sm, sm->nextBlock)) {
        sm->nextBlock++;
        sm->blocksSkipped++;
    }
    if (sm->nextBlock < sm->nblocks) {
        b = sm->nextBlock;
    } else if (sm->headerPending) {
        b = 0; /* header last: this single write makes the new copy valid */
    } else {
        sm->state = LSW_IDLE;
        return 0;
    }

    if (lstore_write((u8) (COPY_BASE(sm->writeCopy) + b), &sm->stage[b * 8]) != 0) {
        sm->errors++;
        sm->state = LSW_IDLE;
        sm->pending = 1;
        if (++sm->consecutiveErrors >= 3) {
            sm->mode = LSM_RAM_ONLY; /* storage failing: stop hammering it */
        }
        return 0;
    }
    sm->consecutiveErrors = 0;
    for (i = 0; i < 8; i++) {
        sm->image[sm->writeCopy][b * 8 + i] = sm->stage[b * 8 + i];
    }
    sm->blocksWritten++;

    if (b == 0) {
        sm->headerPending = 0;
        sm->active = sm->writeCopy;
        sm->seq = (u16) (sm->seq + 1);
        sm->commits++;
        sm->state = LSW_IDLE;
        if (sm->pending && sm->src) {
            begin_commit(sm, sm->src);
        }
    } else {
        sm->nextBlock++;
    }
    return 1;
}

void lsm_flush(LivingSaveMgr *sm) {
    s32 guard = 4 * LIVING_COPY_BLOCKS;

    if (sm->pending && sm->state == LSW_IDLE && sm->src && sm->mode == LSM_PERSIST) {
        begin_commit(sm, sm->src);
    }
    while (sm->state == LSW_WRITING && guard-- > 0) {
        lsm_tick(sm);
    }
}

const char *lsm_load_name(u8 r) {
    static const char *const names[] = { "OK", "FRESH", "RECOVERED", "CORRUPT_RESET", "NEWER_VERSION",
                                         "NO_STORAGE" };
    return r <= LSL_NO_STORAGE ? names[r] : "?";
}
