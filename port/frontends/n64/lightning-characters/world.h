#ifndef LIGHTNING_WORLD_H
#define LIGHTNING_WORLD_H
#include <stdbool.h>
typedef struct { float x, z, heading, gait; } Player;
typedef struct { unsigned model; float x, z, heading; } Actor;
extern const Actor *const lightning_npcs;
void world_reset(Player *player);
bool world_blocked(float x, float z);
void world_move(Player *player, float x_axis, float z_axis, bool run, float dt);
#endif
