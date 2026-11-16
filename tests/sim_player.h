/* Deterministic synthetic players used by tests and benchmarks. */
#ifndef SIM_PLAYER_H
#define SIM_PLAYER_H

#include "mock_world.h"

enum SimProfile { P_EXPLORER, P_STRUGGLER, P_FARMER, P_COUNT };

void sim_terrain(void);
void sim_input(int profile, int visit, int frame, int frames, LivingInput *in, s16 *coins, s16 *stars);
/* One full area visit: load -> frames of play -> unload. Returns AI objects alive at the end. */
int sim_visit(int profile, int visit, int seconds);
const char *sim_name(int profile);

#endif
