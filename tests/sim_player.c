#include "sim_player.h"

#define SIM_LEVEL 9 /* LEVEL_BOB */
#define SIM_AREA 1

const char *sim_name(int profile) {
    static const char *const n[P_COUNT] = { "explorer", "struggler", "farmer" };
    return n[profile];
}

void sim_terrain(void) {
    mock_reset_world();
    mock_add_rect(-6000, -6000, 4000, 6000, 0, 3000, LS_SAFE);      /* main floor */
    mock_add_rect(2000, -6000, 3000, -2000, 1, 3000, LS_HAZARD);    /* lava strip */
    mock_add_rect(-6000, -6000, -4000, -4000, 600, 3000, LS_SAFE);  /* plateau */
    /* x > 4000: bottomless pit */
    mock_add_existing(LK_COIN, -500, 20, -500);
    mock_add_existing(LK_COIN, 500, 20, 1500);
    mock_add_existing(LK_COIN, -2500, 20, 2500);
    mock_add_existing(LK_COIN, 1000, 20, -3000);
    mock_add_existing(LK_COIN, -3500, 20, -1000);
    mock_add_existing(LK_GOOMBA, 0, 0, 3000);
    mock_add_existing(LK_GOOMBA, -2000, 0, -2000);
}

static const s16 sDirX[8] = { 1024, 724, 0, -724, -1024, -724, 0, 724 };
static const s16 sDirZ[8] = { 0, 724, 1024, 724, 0, -724, -1024, -724 };

void sim_input(int profile, int visit, int frame, int frames, LivingInput *in, s16 *coins, s16 *stars) {
    int dir, t;
    long x = 0, z = 0;

    in->actClass = LA_NONE;
    in->actChanged = 0;
    in->flags = LIF_ON_GROUND;
    in->buttons = LB_ANY;
    in->stickMag = 60;
    in->lives = 4;

    switch (profile) {
        case P_EXPLORER: {
            /* out-and-back along a new heading every visit, sweeping the whole map */
            int half = frames / 2;
            long d = frame < half ? frame * 22L : (frames - frame) * 22L;
            dir = (visit * 3) & 7;
            if (d > 5200) d = 5200;
            x = (sDirX[dir] * d) >> 10;
            z = (sDirZ[dir] * d) >> 10;
            if (frame % 45 == 0) { in->actClass = LA_JUMP; in->actChanged = 1; }
            if (frame % 300 == 150) { in->actClass = LA_LONGJUMP; in->actChanged = 1; }
            if (frame % 200 == 0) in->buttons |= LB_CAMERA;
            break;
        }
        case P_STRUGGLER: {
            /* hovers around one spot, idles a lot, dies at the end of every visit */
            t = frame % 240;
            x = -1000 + ((t < 120) ? t * 3 : (240 - t) * 3);
            z = 1000;
            if ((frame / 150) % 2 == 1) { in->stickMag = 0; in->buttons = 0; in->flags |= LIF_IDLE_ACTION; }
            if (frame % 90 == 0) { in->actClass = LA_JUMP; in->actChanged = 1; }
            if (frame > frames - 20) { in->flags = LIF_DYING; in->lives = 3; }
            break;
        }
        default: {
            /* farms the same triangle at speed with acrobatics, collects a star per visit */
            static const s16 px[3] = { -3000, -1000, -1000 };
            static const s16 pz[3] = { 0, 0, 3000 };
            int leg = (frame / 70) % 3;
            int k = frame % 70;
            int nx = (leg + 1) % 3;
            x = px[leg] + (long) (px[nx] - px[leg]) * k / 70;
            z = pz[leg] + (long) (pz[nx] - pz[leg]) * k / 70;
            if (frame % 25 == 0) { in->actClass = (frame % 50) ? LA_TRIPLE : LA_LONGJUMP; in->actChanged = 1; }
            if (frame % 60 == 30) { in->actClass = LA_WALLKICK; in->actChanged = 1; }
            if (frame == frames - 60) *stars += 1;
            break;
        }
    }
    /* stay on solid, safe ground (the pit starts at x=4000, lava at x 2000..3000 / z<-2000) */
    if (x > 3800) x = 3800;
    if (z < -2000 && x > 1900 && x < 3100) x = 1900;
    in->pos.x = (s16) x;
    in->pos.y = (s16) (x <= -4000 && z <= -4000 ? 600 : 0);
    in->pos.z = (s16) z;
    gMock.player = in->pos;
    *coins = (s16) (*coins + mock_collect_near(130));
    in->coins = *coins;
    in->stars = *stars;
}

int sim_visit(int profile, int visit, int seconds) {
    int frames = seconds * LIVING_FRAMES_PER_SEC;
    int f, alive;
    s16 coins = 0, stars = 0;
    LivingInput in;

    sim_terrain();
    gMock.player.x = 0; gMock.player.y = 0; gMock.player.z = 0;
    living_area_loaded(SIM_LEVEL, SIM_AREA);
    for (f = 0; f < frames; f++) {
        sim_input(profile, visit, f, frames, &in, &coins, &stars);
        living_update(&in);
        living_storage_tick();
    }
    alive = mock_count_alive(1, -1);
    living_area_unloading();
    mock_unload_area();
    /* let the trickle writer finish, as the castle/menu frames would */
    for (f = 0; f < 120; f++) {
        living_storage_tick();
    }
    return alive;
}
