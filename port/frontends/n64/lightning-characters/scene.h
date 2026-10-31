#ifndef LIGHTNING_SCENE_H
#define LIGHTNING_SCENE_H
#include <GL/gl.h>

void scene_load(void);
void scene_draw_room(void);
/* model: 1 Ronald, 2 Nicholas/Brandon, 3 Isaac, 4 Dr. Mason. */
void scene_draw_actor(unsigned model, float x, float z, float heading, float gait);
#endif
