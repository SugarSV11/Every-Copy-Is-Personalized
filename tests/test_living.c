/*
 * Host test-suite for the Living ROM core. Build: build_host.bat (MSVC).
 * Everything here runs the exact C that is compiled into the ROM, against a
 * fake world (tests/mock_world.c). It proves logic, determinism, safety and
 * persistence; it says nothing about N64 timing (see benchmarks/).
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "sim_player.h"

static int gPass, gFail;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (cond) {                                                         \
            gPass++;                                                        \
        } else {                                                            \
            gFail++;                                                        \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, msg);          \
        }                                                                   \
    } while (0)

static void print_mem(const char *tag, const LivingMemory *m) {
    int i;
    printf("  [%s] mood=%-10s traits:", tag, lp_mood_name(m->mood));
    for (i = 0; i < LT_COUNT; i++) printf(" %4d", m->trait[i]);
    printf("  (cur ply ben men pat att)\n");
}

static void print_plan(const LivingAreaMem *am) {
    int i;
    printf("    gen %u plan:", am->generation);
    for (i = 0; i < LIVING_PLAN_OPS; i++) {
        if (am->plan[i].op != LOP_NONE) {
            printf(" %s%s", ld_op_name(am->plan[i].op), (am->plan[i].op & LOP_REJECTED_BIT) ? "(rejected)" : "");
        }
    }
    printf("\n");
}

static int eeprom_original_intact(void) {
    int i;
    for (i = 0; i < 512; i++) {
        if (gMock.eeprom[i] != (u8) (i * 7 + 3)) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */

static void test_sizes(void) {
    u8 buf[LIVING_COPY_BYTES];
    LivingMemory m;
    u16 n;

    printf("\n[sizes] (host x64 layout; N64 layout is equal or smaller: no pointers in persistent data)\n");
    printf("  LivingState   %6u bytes RAM total\n", (unsigned) sizeof(LivingState));
    printf("    memory      %6u\n", (unsigned) sizeof(LivingMemory));
    printf("    tracker     %6u\n", (unsigned) sizeof(LivingTracker));
    printf("    mutation    %6u\n", (unsigned) sizeof(LivingMut));
    printf("    events      %6u\n", (unsigned) sizeof(LivingEvents));
    printf("    save mgr    %6u\n", (unsigned) sizeof(LivingSaveMgr));
    lm_init_fresh(&m, 1);
    n = lsm_serialize(&m, buf, sizeof(buf) - LSAVE_HEADER_BYTES);
    printf("  serialized payload %u bytes + %u header = %u of %u per copy\n", n, LSAVE_HEADER_BYTES,
           n + LSAVE_HEADER_BYTES, LIVING_COPY_BYTES);
    CHECK(n == 644, "payload v2 is exactly 644 bytes");
    CHECK(n + LSAVE_HEADER_BYTES <= LIVING_COPY_BYTES, "copy fits its EEPROM region");
}

static void test_serialize_roundtrip(void) {
    LivingMemory a, b;
    u8 s1[700], s2[700];
    u16 n1, n2, fix = 99;
    int i;

    printf("\n[serialize roundtrip]\n");
    lm_init_fresh(&a, 0xABCDEF01);
    a.sessions = 7; a.playSec = 123456; a.trait[LT_MENACE] = -42; a.mood = LM_TESTING;
    a.eventsSeen = 0x2F5;
    for (i = 0; i < LIVING_MAX_AREAS; i++) {
        LivingAreaMem *am = &a.areas[i];
        am->level = (u8) (i + 3); am->area = 1; am->visits = (u8) (i * 3);
        am->anchor[0].x = (s16) (-7000 + i * 100); am->anchor[1].y = -300; am->anchor[2].z = 6999;
        am->plan[0].op = LOP_BOX_STAIRS; am->plan[1].op = LOP_SECRET | LOP_REJECTED_BIT;
        am->plan[1].param = 0xFF; am->explored[3] = 0x5A; am->hotCell = 17;
    }
    n1 = lsm_serialize(&a, s1, sizeof(s1));
    CHECK(lsm_deserialize(&b, s1, n1, &fix), "deserialize ok");
    CHECK(fix == 0, "no sanitising needed on valid data");
    n2 = lsm_serialize(&b, s2, sizeof(s2));
    CHECK(n1 == n2 && memcmp(s1, s2, n1) == 0, "bit-exact roundtrip");
    CHECK(b.trait[LT_MENACE] == -42 && b.areas[5].anchor[0].x == -6500, "signed fields survive");
}

static void commit_and_flush(LivingMemory *mem, LivingSaveMgr *sm) {
    lsm_request_commit(sm, mem);
    lsm_flush(sm);
}

static void test_save_system(void) {
    LivingSaveMgr sm;
    LivingMemory mem, back;
    u8 r;
    int k, okAll;
    u32 writesBefore;

    printf("\n[save manager]\n");
    mock_reset_eeprom();
    r = lsm_load(&sm, &mem, 77);
    CHECK(r == LSL_FRESH, "blank EEPROM -> FRESH");

    mem.sessions = 1; mem.trait[0] = 11;
    commit_and_flush(&mem, &sm);
    CHECK(sm.active == 0 && sm.commits == 1, "first commit lands in copy A");
    printf("  first commit: %u blocks written\n", sm.blocksWritten);
    mem.trait[0] = 22;
    writesBefore = gMock.totalWrites;
    commit_and_flush(&mem, &sm);
    CHECK(sm.active == 1, "second commit lands in copy B");
    mem.trait[0] = 33;
    writesBefore = gMock.totalWrites;
    commit_and_flush(&mem, &sm);
    printf("  third commit (1 field changed, vs copy written 2 commits ago): %lu blocks\n",
           (unsigned long) (gMock.totalWrites - writesBefore));
    CHECK(gMock.totalWrites - writesBefore <= 3, "unchanged blocks are skipped");

    r = lsm_load(&sm, &back, 1);
    CHECK(r == LSL_OK && back.trait[0] == 33, "reload picks newest copy");

    /* corrupt newest copy -> recover the older one */
    gMock.eeprom[(LIVING_EEP_FIRST_BLOCK + sm.active * LIVING_COPY_BLOCKS) * 8 + 40] ^= 0x5A;
    r = lsm_load(&sm, &back, 1);
    CHECK(r == LSL_RECOVERED && back.trait[0] == 22, "corrupt newest -> RECOVERED older copy");

    /* corrupt both */
    gMock.eeprom[(LIVING_EEP_FIRST_BLOCK + sm.active * LIVING_COPY_BLOCKS) * 8 + 40] ^= 0x33;
    r = lsm_load(&sm, &back, 1);
    CHECK(r == LSL_CORRUPT_RESET && back.recoveries == 1, "both corrupt -> clean reset");
    CHECK(eeprom_original_intact(), "original SM64 save bytes 0..511 untouched");

    /* power loss at every possible point of a commit: previous state always loads */
    mock_reset_eeprom();
    lsm_load(&sm, &mem, 5);
    mem.trait[1] = 50;
    commit_and_flush(&mem, &sm);
    okAll = 1;
    for (k = 0; k < 90; k++) {
        LivingSaveMgr sm2;
        u8 snapshot[2048];
        memcpy(snapshot, gMock.eeprom, sizeof(snapshot));
        lsm_load(&sm2, &mem, 5);
        mem.trait[1] = 60;
        mem.areas[0].level = 9; mem.areas[0].area = 1; mem.areas[0].visits = (u8) k;
        gMock.writesLeft = k;                 /* the console dies after k block writes */
        commit_and_flush(&mem, &sm2);
        gMock.writesLeft = -1;
        r = lsm_load(&sm2, &back, 5);
        /* the half-written copy is detected as corrupt (RECOVERED) or the commit completed (OK) */
        if (!((r == LSL_OK || r == LSL_RECOVERED) && (back.trait[1] == 50 || back.trait[1] == 60))) {
            okAll = 0;
            printf("  power-loss after %d writes -> %s trait=%d\n", k, lsm_load_name(r), back.trait[1]);
        }
        memcpy(gMock.eeprom, snapshot, sizeof(snapshot));
    }
    CHECK(okAll, "power loss at any block never loses the last good save");

    /* a newer save format is never overwritten */
    {
        u8 *img = &gMock.eeprom[LIVING_EEP_FIRST_BLOCK * 8];
        u16 len = (u16) ((img[6] << 8) | img[7]);
        u16 crc;
        u8 before[2048];
        img[2] = LIVING_SAVE_VERSION + 1;
        crc = lsm_crc16(img, 8, 0xFFFF);
        crc = lsm_crc16(img + LSAVE_HEADER_BYTES, len, crc);
        img[8] = (u8) (crc >> 8); img[9] = (u8) crc;
        r = lsm_load(&sm, &mem, 1);
        CHECK(r == LSL_NEWER_VERSION && sm.mode == LSM_RAM_ONLY, "newer version -> RAM-only");
        memcpy(before, gMock.eeprom, sizeof(before));
        commit_and_flush(&mem, &sm);
        CHECK(memcmp(before, gMock.eeprom, sizeof(before)) == 0, "RAM-only mode writes nothing");
    }

    /* no EEPROM 16K -> RAM-only */
    gMock.eepromPresent = 0;
    r = lsm_load(&sm, &mem, 1);
    CHECK(r == LSL_NO_STORAGE && sm.mode == LSM_RAM_ONLY, "no 16K EEPROM -> RAM-only, game unaffected");
    gMock.eepromPresent = 1;

    /* dead chip: repeated failures stop the writer */
    mock_reset_eeprom();
    lsm_load(&sm, &mem, 1);
    gMock.writesLeft = 0;
    for (k = 0; k < 4; k++) commit_and_flush(&mem, &sm);
    CHECK(sm.mode == LSM_RAM_ONLY, "3 consecutive write errors -> stop writing");
    gMock.writesLeft = -1;

    /* firewall */
    {
        u8 blk[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        mock_reset_eeprom();
        CHECK(lstore_write(10, blk) == LSTORE_ERR_RANGE, "write into original save area refused");
        CHECK(lstore_write(63, blk) == LSTORE_ERR_RANGE, "block 63 refused");
        CHECK(eeprom_original_intact(), "original save still intact");
    }
}

static void test_sanitize(void) {
    LivingMemory a, b;
    u8 buf[700];
    u16 n, fix = 0;

    printf("\n[validation of stored data]\n");
    lm_init_fresh(&a, 3);
    a.areas[0].level = 9; a.areas[0].area = 1; a.areas[0].visits = 2;
    a.areas[1].level = 9; a.areas[1].area = 1; a.areas[1].visits = 1; /* duplicate key */
    n = lsm_serialize(&a, buf, sizeof(buf));
    buf[44] = 0x7F;           /* trait[0] = 127, out of range */
    buf[50] = 200;            /* mood out of range */
    buf[76 + 47 * 2] = 99;    /* area 2 level 99 */
    buf[76 + 21] = 0x3F;      /* area 0 plan[0].op = 63 invalid */
    CHECK(lsm_deserialize(&b, buf, n, &fix), "sanitised load succeeds");
    printf("  fields repaired: %u\n", fix);
    CHECK(fix >= 4, "all injected faults detected");
    CHECK(b.trait[0] == 100 && b.mood < LM_COUNT, "values clamped into range");
    CHECK(b.areas[1].level == 0 && b.areas[2].level == 0, "duplicate / invalid area ids dropped");
    CHECK(b.areas[0].plan[0].op == LOP_NONE, "invalid op id dropped");
}

static void test_mutation_safety(void) {
    LivingMut m;
    LVec3 a;
    u8 ok;
    int before;
    LHandle c0, c1;
    LVec3 p0, p1, t;

    printf("\n[mutation safety + rollback]\n");
    sim_terrain();
    gMock.player.x = -5000; gMock.player.z = 5000;
    lme_reset(&m);

    a.x = 1500; a.y = 0; a.z = -4000;
    lme_begin_op(&m, 0, LOP_ROOM);
    ok = lobj_coin_trail(&m, &a, 0, 8);
    if (!ok) lme_rollback_op(&m);
    CHECK(!ok && mock_count_alive(1, -1) == 0, "trail towards lava stops, too short -> rolled back");

    a.x = 3300; a.z = 3000;
    lme_begin_op(&m, 0, LOP_ROOM);
    ok = lobj_coin_trail(&m, &a, 0, 8);
    if (!ok) lme_rollback_op(&m);
    CHECK(!ok && mock_count_alive(1, -1) == 0, "trail towards pit -> rolled back");

    a.x = 0; a.z = 0;
    lme_begin_op(&m, 0, LOP_ROOM);
    ok = lobj_coin_trail(&m, &a, 2, 6);
    if (ok) lme_commit_op(&m);
    CHECK(ok && mock_count_alive(1, LK_COIN) == 6, "trail on safe ground places 6 coins");

    before = mock_count_alive(1, -1);
    a.x = -3750; a.z = -4500; /* third column (x=-4150) is on the 600-high plateau: not flat */
    lme_begin_op(&m, 1, LOP_BOX_STAIRS);
    ok = lgeo_build(&m, LTPL_STAIRS, &a, 4);
    if (!ok) lme_rollback_op(&m);
    CHECK(!ok && mock_count_alive(1, -1) == before, "stairs over uneven floor rejected, world unchanged");

    a.x = -1000; a.z = -1000;
    lme_begin_op(&m, 1, LOP_BOX_STAIRS);
    ok = lgeo_build(&m, LTPL_STAIRS, &a, 0);
    if (ok) lme_commit_op(&m);
    CHECK(ok && mock_count_alive(1, LK_BOX) == 6, "stairs on flat floor: 6 boxes");

    mock_add_rect(500, 500, 1500, 1500, 0, 450, LS_SAFE); /* low ceiling room */
    before = mock_count_alive(1, -1);
    a.x = 800; a.z = 800;
    lme_begin_op(&m, 2, LOP_BOX_TOWER);
    ok = lgeo_build(&m, LTPL_PILLAR, &a, 0);
    if (!ok) lme_rollback_op(&m);
    CHECK(!ok && mock_count_alive(1, -1) == before, "pillar under low ceiling rejected");

    gMock.player.x = 0; gMock.player.z = 3000; gMock.player.y = 0;
    t.x = 100; t.y = 0; t.z = 3100;
    CHECK(lme_spawn(&m, LK_COIN, &t, 0) == LH_INVALID, "never spawn on top of Mario");

    gMock.baseObjects = 185;
    t.x = -3000; t.z = 3000;
    CHECK(!lme_can_spawn(&m, LK_BOX, 6), "object pool soft limit enforced");
    gMock.baseObjects = 60;

    gMock.kindAvailable[LK_GOOMBA] = 0;
    CHECK(!lme_can_spawn(&m, LK_GOOMBA, 1), "unloaded model never spawned");
    gMock.kindAvailable[LK_GOOMBA] = 1;

    /* relocation rollback restores exact positions */
    c0 = lw_find_existing(LK_COIN, 0);
    c1 = lw_find_existing(LK_COIN, 1);
    lw_get_pos(c0, &p0);
    lw_get_pos(c1, &p1);
    lme_begin_op(&m, 3, LOP_BOX_BRIDGE);
    t.x = 111; t.y = 0; t.z = 222;
    lme_relocate(&m, c0, LK_COIN, &t);
    lme_relocate(&m, c1, LK_COIN, &t);
    lme_rollback_op(&m);
    CHECK(gMock.obj[c0].pos.x == p0.x && gMock.obj[c1].pos.z == p1.z, "relocation rollback exact");

    t.x = 9000; t.y = 0; t.z = 0;
    CHECK(lme_spawn(&m, LK_COIN, &t, 0) == LH_INVALID, "coordinates outside safe box refused");
    printf("  validation failures counted: %u, rollbacks: %u\n", m.validationFailures, m.rollbacks);
}

static void test_structures(void) {
    LivingMut m;
    LVec3 a;
    int before, i, maxY = -9999;

    printf("\n[level-designer structures]\n");
    sim_terrain();                                  /* plateau y=600 at x<-4000, z<-4000; pit x>4000 */
    mock_add_rect(4800, -2000, 6000, 2000, 0, 3000, LS_SAFE);  /* far side of the pit */
    gMock.player.x = 0; gMock.player.z = 5000;
    lme_reset(&m);

    a.x = -3300; a.y = 0; a.z = -4600;
    lme_begin_op(&m, 0, LOP_BOX_STAIRS);
    CHECK(lgeo_stairs_to_ledge(&m, &a, 0) == 1, "finds the 600-high plateau and builds stairs to it");
    lme_commit_op(&m);
    for (i = 0; i < MOCK_MAX_OBJ; i++) {
        if (gMock.obj[i].used && gMock.obj[i].byAI && gMock.obj[i].kind == LK_TBLOCK && gMock.obj[i].pos.y > maxY) maxY = gMock.obj[i].pos.y;
    }
    printf("  stairs: %d boxes, top box base at y=%d (ledge at 600)\n", mock_count_alive(1, LK_TBLOCK), maxY);
    CHECK(mock_count_alive(1, LK_TBLOCK) == 6 && maxY + 200 >= 600, "3 steps (1+2+3 boxes) reaching the ledge");

    before = mock_count_alive(1, LK_TBLOCK);
    a.x = 3500; a.y = 0; a.z = 0;
    lme_begin_op(&m, 1, LOP_BOX_BRIDGE);
    CHECK(lgeo_bridge(&m, &a, 0) == 1, "finds the pit and the far side, builds a bridge");
    lme_commit_op(&m);
    printf("  bridge: %d boxes, flush with the floor (top at y=0)\n", mock_count_alive(1, LK_TBLOCK) - before);
    CHECK(mock_count_alive(1, LK_TBLOCK) - before >= 4, "bridge spans the 800-unit gap");

    before = mock_count_alive(1, -1);
    a.x = -1500; a.y = 0; a.z = 2500;
    lme_begin_op(&m, 2, LOP_BOX_TOWER);
    CHECK(lgeo_tower(&m, &a, 2, 0) == 1, "tower on flat ground");
    lme_commit_op(&m);
    CHECK(mock_count_alive(1, -1) - before == 9, "2x2x2 boxes + a reward on top");

    before = mock_count_alive(1, -1);
    a.x = -1000; a.y = 0; a.z = -1000;              /* flat everywhere around: no ledge, no gap */
    lme_begin_op(&m, 3, LOP_BOX_BRIDGE);
    CHECK(lgeo_bridge(&m, &a, 0) == 0, "no gap -> no bridge (never random boxes)");
    lme_rollback_op(&m);
    CHECK(mock_count_alive(1, -1) == before, "nothing left behind");
}

static void test_rooms_and_scale(void) {
    LivingMut m;
    LVec3 a;
    int i, rooms = 0, before;

    printf("\n[new rooms, textured blocks, tree scale]\n");
    sim_terrain();
    gMock.player.x = -5500; gMock.player.z = 5500;
    lme_reset(&m);
    a.x = -1500; a.y = 0; a.z = 0;
    lme_begin_op(&m, 0, LOP_ROOM);
    CHECK(lgeo_room(&m, &a, 2, 1) == 1, "a room fits on open flat ground");
    lme_commit_op(&m);
    for (i = 0; i < MOCK_MAX_OBJ; i++) if (gMock.obj[i].used && gMock.obj[i].kind == LK_ROOM) rooms++;
    CHECK(rooms == 1 && mock_count_alive(1, LK_1UP) == 1, "one building with a reward inside");

    /* textured blocks are preferred over cork boxes when available */
    a.x = -1500; a.z = -2500;
    lme_begin_op(&m, 1, LOP_BOX_TOWER);
    lgeo_tower(&m, &a, 0, 0);
    lme_commit_op(&m);
    CHECK(mock_count_alive(1, LK_TBLOCK) == 8 && mock_count_alive(1, LK_BOX) == 0, "towers use level-textured blocks");

    /* no room where the ground is too broken: plateau corner + lava + pit everywhere */
    sim_terrain();
    mock_reset_world();
    mock_add_rect(-300, -300, 300, 300, 0, 3000, LS_SAFE);   /* a tiny island */
    gMock.player.x = 0; gMock.player.z = 0;
    lme_reset(&m);
    a.x = 0; a.y = 0; a.z = 0;
    before = mock_count_alive(1, -1);
    lme_begin_op(&m, 0, LOP_ROOM);
    CHECK(lgeo_room(&m, &a, 0, 0) == 0, "no flat open space -> no room");
    lme_rollback_op(&m);
    CHECK(mock_count_alive(1, -1) == before, "nothing left behind");

    /* trees: rescaled, never extreme, restored on rollback */
    sim_terrain();
    i = mock_add_existing(LK_TREE, 1000, 0, 1000);
    lme_reset(&m);
    lme_begin_op(&m, 0, LOP_AMBIENCE);
    CHECK(lobj_rescale_trees(&m, 250, 4) == 1 && gMock.obj[i].scale == 140, "tree scale clamped to 140%");
    lme_rollback_op(&m);
    CHECK(gMock.obj[i].scale == 100, "tree scale restored");
}

static void run_profile(int profile, int visits, LivingMemory *out, int verbose) {
    int v, alive;
    mock_reset_eeprom();
    living_boot(1234);
    gLiving.mem.playSec = LIVING_DORMANT_SEC; /* skip the dormant period for the comparison */
    for (v = 0; v < visits; v++) {
        alive = sim_visit(profile, v, 90);
        if (verbose && (v % 3 == 2)) {
            LivingAreaMem *am = lm_area_find(&gLiving.mem, 9, 1);
            printf("  visit %2d: AI objects alive at exit %2d, applied ops %u ", v + 1, alive, gLiving.lastApplied);
            print_plan(am);
        }
    }
    *out = gLiving.mem;
}

static void test_personalities(void) {
    LivingMemory res[P_COUNT];
    int p;

    printf("\n[adaptive personality: same game, three players, 12 visits each]\n");
    for (p = 0; p < P_COUNT; p++) {
        printf(" %s\n", sim_name(p));
        run_profile(p, 12, &res[p], 1);
        print_mem(sim_name(p), &res[p]);
        printf("    affinity:");
        {
            int i;
            for (i = 1; i < LOP_COUNT; i++) printf(" %s=%u", ld_op_name((u8) i), res[p].opAffinity[i]);
        }
        printf("\n    events seen 0x%03X, mutations applied %u rejected %u\n", (unsigned) res[p].eventsSeen,
               res[p].mutApplied, res[p].mutRejected);
    }
    CHECK(res[P_EXPLORER].trait[LT_CURIOSITY] > res[P_STRUGGLER].trait[LT_CURIOSITY], "explorer -> more curious AI");
    CHECK(res[P_STRUGGLER].trait[LT_BENEVOLENT] > res[P_EXPLORER].trait[LT_BENEVOLENT]
          && res[P_STRUGGLER].trait[LT_BENEVOLENT] > res[P_FARMER].trait[LT_BENEVOLENT], "struggler -> most benevolent AI");
    CHECK(res[P_FARMER].trait[LT_MENACE] > res[P_STRUGGLER].trait[LT_MENACE]
          && res[P_FARMER].trait[LT_MENACE] > res[P_EXPLORER].trait[LT_MENACE], "farmer -> most menacing AI");
    CHECK(res[P_FARMER].trait[LT_PLAYFUL] > res[P_STRUGGLER].trait[LT_PLAYFUL], "acrobat -> more playful AI");
    CHECK(res[P_STRUGGLER].trait[LT_PATIENCE] > res[P_FARMER].trait[LT_PATIENCE], "idle player -> more patient AI");
    CHECK(res[P_STRUGGLER].mood != res[P_FARMER].mood, "different players end in different moods");
    CHECK(res[P_EXPLORER].mutApplied > 0 && res[P_FARMER].mutApplied > 0, "the world actually mutates");
}

static void test_determinism_and_persistence(void) {
    u8 s1[700], s2[700], e1[2048];
    LivingMemory before;
    int v;

    printf("\n[determinism + persistence across power cycles]\n");
    mock_reset_eeprom();
    living_boot(42);
    for (v = 0; v < 8; v++) sim_visit(P_EXPLORER, v, 90);
    lsm_serialize(&gLiving.mem, s1, sizeof(s1));
    memcpy(e1, gMock.eeprom, sizeof(e1));

    mock_reset_eeprom();
    living_boot(42);
    for (v = 0; v < 8; v++) sim_visit(P_EXPLORER, v, 90);
    lsm_serialize(&gLiving.mem, s2, sizeof(s2));
    CHECK(memcmp(s1, s2, 640) == 0, "same inputs -> identical AI memory");
    CHECK(memcmp(e1, gMock.eeprom, sizeof(e1)) == 0, "same inputs -> identical EEPROM image");

    /* power off / on */
    lsm_flush(&gLiving.sm);
    before = gLiving.mem;
    living_boot(999); /* different entropy: must be ignored, the stored seed wins */
    CHECK(gLiving.sm.lastLoad == LSL_OK, "reboot loads the stored AI");
    CHECK(gLiving.mem.seed == before.seed, "world seed persists");
    CHECK(gLiving.mem.sessions == before.sessions + 1, "session counter advances");
    CHECK(memcmp(gLiving.mem.trait, before.trait, LT_COUNT) == 0 && gLiving.mem.mood == before.mood,
          "personality persists");
    CHECK(memcmp(gLiving.mem.areas, before.areas, sizeof(before.areas)) == 0, "area plans + mutations persist");

    /* the persisted plan rebuilds the same world after reboot */
    {
        LivingAreaMem *am = lm_area_find(&gLiving.mem, 9, 1);
        LVec3 pos1[MOCK_MAX_OBJ], pos2[MOCK_MAX_OBJ];
        int n1 = 0, n2 = 0, i;
        LivingMemory tmp = gLiving.mem;
        LivingAreaMem *tam = lm_area_find(&tmp, 9, 1);
        LivingMut m;

        sim_terrain(); gMock.player.x = -5500; gMock.player.z = 5500;
        lme_reset(&m);
        lme_apply_plan(&m, &tmp, tam);
        for (i = 0; i < MOCK_MAX_OBJ; i++) if (gMock.obj[i].used && gMock.obj[i].byAI) pos1[n1++] = gMock.obj[i].pos;
        tmp = gLiving.mem; tam = lm_area_find(&tmp, 9, 1);
        sim_terrain(); gMock.player.x = -5500; gMock.player.z = 5500;
        lme_reset(&m);
        lme_apply_plan(&m, &tmp, tam);
        for (i = 0; i < MOCK_MAX_OBJ; i++) if (gMock.obj[i].used && gMock.obj[i].byAI) pos2[n2++] = gMock.obj[i].pos;
        printf("  area gen %u rebuilds %d AI objects\n", am ? am->generation : 0, n1);
        CHECK(n1 == n2 && memcmp(pos1, pos2, sizeof(LVec3) * n1) == 0, "rebuild is bit-identical");
    }
    CHECK(eeprom_original_intact(), "original save untouched after full simulation");
    {
        /* dump for tools/read_living_eep.py */
        FILE *f = fopen("builds/host/mock.eep", "wb");
        if (f) {
            fwrite(gMock.eeprom, 1, sizeof(gMock.eeprom), f);
            fclose(f);
        }
    }
}

static void test_learning(void) {
    LivingMemory m;
    int i;
    u8 a0;

    printf("\n[learning signal]\n");
    lm_init_fresh(&m, 1);
    a0 = m.opAffinity[LOP_ROOM];
    for (i = 0; i < 5; i++) ld_feedback(&m, LOP_ROOM, LO_SUCCESS);
    for (i = 0; i < 5; i++) ld_feedback(&m, LOP_ENEMY_AMBUSH, LO_FAIL);
    for (i = 0; i < 100; i++) ld_feedback(&m, LOP_GIFT, LO_FAIL);
    printf("  coin_trail %u -> %u, ambush 128 -> %u, gift floor %u\n", a0, m.opAffinity[LOP_ROOM],
           m.opAffinity[LOP_ENEMY_AMBUSH], m.opAffinity[LOP_GIFT]);
    CHECK(m.opAffinity[LOP_ROOM] > a0, "engagement raises affinity");
    CHECK(m.opAffinity[LOP_ENEMY_AMBUSH] < 128, "deaths lower affinity");
    CHECK(m.opAffinity[LOP_GIFT] == 32, "affinity floor keeps exploration alive");
}

static void test_dormant(void) {
    printf("\n[dormant start]\n");
    mock_reset_eeprom();
    living_boot(7);
    /* fresh memory takes its seed from gameplay timing; an existing world never changes seed */
    {
        u32 s0 = gLiving.mem.seed;
        living_add_entropy(12345);
        CHECK(gLiving.mem.seed != s0, "fresh AI is reseeded from player timing");
    }
    /* a menu / title screen loads an area but never runs gameplay: no memory slot used */
    living_area_loaded(1, 1);
    living_area_unloading();
    CHECK(lm_area_find(&gLiving.mem, 1, 1) == 0, "menu areas never consume a memory slot");
    sim_visit(P_FARMER, 0, 30);
    sim_visit(P_FARMER, 1, 30);
    CHECK(gLiving.mem.mood == LM_DORMANT && gLiving.mem.mutApplied == 0, "first minutes: observe only, world untouched");
    sim_visit(P_FARMER, 2, 90);
    sim_visit(P_FARMER, 3, 90);
    CHECK(gLiving.mem.mood != LM_DORMANT, "AI wakes up after the observation period");
    {
        u32 s1 = gLiving.mem.seed;
        living_add_entropy(999);
        CHECK(gLiving.mem.seed == s1, "seed is frozen once the AI has seen the world");
    }
}

static void test_passage_shift(void) {
    LivingMemory m;
    int s, shifted = 0, firstShift = 0, minGap = 1000, last = -1000;
    LivingAreaMem *a;

    printf("\n[passage shifts: rare, safe, temporary]\n");
    lm_init_fresh(&m, 0xBADC0DE);
    m.mood = LM_CURIOUS;
    m.trait[LT_ATTACHMENT] = 60;
    for (s = 1; s <= 200; s++) {
        m.sessions = (u16) s;
        if (lps_session_begin(&m)) {
            shifted++;
            if (!firstShift) firstShift = s;
            if (s - last < minGap) minGap = s - last;
            last = s;
        }
    }
    printf("  200 sessions -> %d shifted sessions (first at %d, min gap %d)\n", shifted, firstShift, minGap);
    CHECK(firstShift >= LIVING_SHIFT_MIN_SESSIONS, "never in the first sessions");
    CHECK(minGap >= LIVING_SHIFT_COOLDOWN, "cooldown between shifts respected");
    CHECK(shifted > 0 && shifted < 200 / 5, "rare: well under one session in five");

    lm_init_fresh(&m, 7);
    m.mood = LM_OBSERVING;
    m.sessions = 50;
    CHECK(!lps_session_begin(&m), "an AI that is still observing never shifts");

    /* safety: only destinations the player has really reached */
    lm_init_fresh(&m, 0x1234);
    m.sessions = 9;
    mock_reset_world();
    mock_add_passage(LPK_PAINTING, 9, 1);   /* BoB: visited */
    mock_add_passage(LPK_PAINTING, 24, 1);  /* WF: visited */
    mock_add_passage(LPK_PAINTING, 12, 1);  /* JRB: never visited */
    mock_add_passage(LPK_DOOR, 6, 3);       /* basement: never visited */
    a = lm_area_acquire(&m, 9, 1); a->visits = 2;
    a = lm_area_acquire(&m, 24, 1); a->visits = 1;
    CHECK(lps_apply(&m, 6, 1) == 1, "two known paintings can be swapped");
    CHECK(gMock.passage[0].destLevel == 24 && gMock.passage[1].destLevel == 9, "BoB painting now leads to WF and back");
    CHECK(gMock.passage[2].destLevel == 12 && gMock.passage[3].destArea == 3, "unknown destinations untouched");
    lw_passages_restore();
    CHECK(gMock.passage[0].destLevel == 9 && gMock.passage[1].destLevel == 24, "restored when the area unloads");

    mock_reset_world();
    mock_add_passage(LPK_PAINTING, 9, 1);
    mock_add_passage(LPK_PAINTING, 12, 1);
    CHECK(lps_apply(&m, 6, 1) == 0 && gMock.swaps == 0, "unvisited AND unreachable destinations are never used");

    /* a NEW destination is allowed when the player could already get there (e.g. JRB with 3 stars) */
    mock_reset_world();
    mock_add_passage(LPK_PAINTING, 9, 1);   /* BoB: visited */
    mock_add_passage(LPK_PAINTING, 12, 1);  /* JRB: never visited, but reachable */
    gMock.reachable[12] = 1;
    CHECK(lps_apply(&m, 6, 1) == 1 && gMock.passage[0].destLevel == 12, "can lead somewhere never visited but reachable");
    lw_passages_restore();

    /* same session + same area -> same swap every time (consistent within a session) */
    {
        u8 d1, d2;
        mock_reset_world();
        mock_add_passage(LPK_DOOR, 6, 2); mock_add_passage(LPK_DOOR, 6, 3); mock_add_passage(LPK_DOOR, 16, 1);
        a = lm_area_acquire(&m, 6, 2); a->visits = 1;
        a = lm_area_acquire(&m, 6, 3); a->visits = 1;
        a = lm_area_acquire(&m, 16, 1); a->visits = 1;
        lps_apply(&m, 6, 1); d1 = gMock.passage[0].destArea; lw_passages_restore();
        lps_apply(&m, 6, 1); d2 = gMock.passage[0].destArea; lw_passages_restore();
        CHECK(d1 == d2, "deterministic within a session");
    }
}

static void test_live_first_visit(void) {
    int f, frames = 40 * LIVING_FRAMES_PER_SEC, minDist2 = 0x7FFFFFFF, spawnedDuring = 0;
    s16 coins = 0, stars = 0;
    LivingInput in;
    LivingAreaMem *am;
    int seen[MOCK_MAX_OBJ];

    printf("\n[live personalization on a FIRST visit, without leaving]\n");
    mock_reset_eeprom();
    living_boot(31);
    gLiving.mem.mood = LM_CURIOUS;               /* an AI that already knows the player */
    gLiving.mem.playSec = LIVING_DORMANT_SEC;
    gLiving.mem.trait[LT_CURIOSITY] = 50;
    sim_terrain();
    for (f = 0; f < MOCK_MAX_OBJ; f++) seen[f] = gMock.obj[f].used;
    living_area_loaded(9, 1);
    for (f = 0; f < frames; f++) {
        int i;
        sim_input(P_STRUGGLER, 0, f, frames + 1000, &in, &coins, &stars); /* stays in one corner */
        living_update(&in);
        living_storage_tick();
        for (i = 0; i < MOCK_MAX_OBJ; i++) {
            if (gMock.obj[i].used && gMock.obj[i].byAI && !seen[i]) {
                long dx = gMock.obj[i].pos.x - gMock.player.x, dz = gMock.obj[i].pos.z - gMock.player.z;
                long d2 = dx * dx + dz * dz;
                seen[i] = 1;
                spawnedDuring++;
                if (d2 < minDist2) minDist2 = (int) d2;
            }
        }
    }
    am = lm_area_find(&gLiving.mem, 9, 1);
    printf("  AI objects created during the first visit: %d, live ops %u, closest to Mario at spawn %.0f units\n",
           spawnedDuring, gLiving.liveOps, spawnedDuring ? sqrt((double) minDist2) : 0.0);
    if (am) print_plan(am);
    CHECK(gLiving.liveOps >= 1 && spawnedDuring > 0, "the world changes during the very first visit");
    CHECK(minDist2 >= LIVING_LIVE_MIN_DIST * LIVING_LIVE_MIN_DIST, "live changes never pop in near Mario");
    CHECK(am && (am->flags & LAF_HAS_PLAN), "the live change is part of the area's remembered plan");
    living_area_unloading();
    mock_unload_area();
}

static void test_migration_v1(void) {
    LivingMemory a, b;
    u8 buf[700];
    u16 n, fix;

    printf("\n[save migration v1 -> v2]\n");
    lm_init_fresh(&a, 0x77);
    a.sessions = 5; a.trait[LT_CURIOSITY] = 46; a.areas[0].level = 9; a.areas[0].area = 1; a.areas[0].visits = 2;
    n = lsm_serialize(&a, buf, sizeof(buf));
    /* a v1 payload is the v2 payload without its 4-byte tail */
    CHECK(lsm_deserialize(&b, buf, 640, &fix), "v1 payload (640 B) still loads");
    CHECK(b.sessions == 5 && b.trait[LT_CURIOSITY] == 46 && b.areas[0].visits == 2, "v1 data preserved");
    CHECK(b.shiftSession == 0 && b.shiftCount == 0, "new v2 fields start at defaults");
    (void) n;
}

int main(void) {
    printf("SM64 Living ROM - host test suite\n");
    test_sizes();
    test_serialize_roundtrip();
    test_save_system();
    test_sanitize();
    test_mutation_safety();
    test_learning();
    test_dormant();
    test_personalities();
    test_determinism_and_persistence();
    test_passage_shift();
    test_migration_v1();
    test_structures();
    test_rooms_and_scale();
    test_live_first_visit();
    printf("\nRESULT: %d passed, %d failed\n", gPass, gFail);
    return gFail ? 1 : 0;
}
