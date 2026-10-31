#include "world.h"
#include <math.h>

#include "assets/world_data.h"
const Actor *const lightning_npcs = lightning_npc_data;
void world_reset(Player *p) { *p = (Player){14,28,180,0}; }

bool world_blocked(float x, float z)
{
    const float radius = .36f;
    if (x < 2+radius || x > 26-radius || z < 2+radius || z > 29.5f-radius) return true;
    for (unsigned i=0; i<sizeof(lightning_solids)/sizeof(lightning_solids[0]); i++) {
        const float *s=lightning_solids[i];
        float near_x=fmaxf(s[0],fminf(x,s[2])), near_z=fmaxf(s[1],fminf(z,s[3]));
        float dx=x-near_x, dz=z-near_z;
        if (dx*dx+dz*dz < radius*radius) return true;
    }
    for (unsigned i=0;i<4;i++) {
        float dx=x-lightning_npcs[i].x, dz=z-lightning_npcs[i].z;
        if (dx*dx+dz*dz < .72f*.72f) return true;
    }
    return false;
}

void world_move(Player *p, float x, float z, bool run, float dt)
{
    float mag=sqrtf(x*x+z*z);
    if (mag < .12f) { p->gait=0; return; }
    if (mag>1) { x/=mag; z/=mag; }
    dt=fmaxf(0,fminf(dt,.1f));
    float speed=run ? 6.5f : 3.5f;
    /* Substep circle collision, then slide independently on both wall axes. */
    unsigned steps=(unsigned)ceilf(speed*dt/.15f);
    if (!steps) return;
    float dx=x*speed*dt/steps, dz=z*speed*dt/steps;
    float start_x=p->x, start_z=p->z;
    for (unsigned i=0;i<steps;i++) {
        if (!world_blocked(p->x+dx,p->z)) p->x+=dx;
        if (!world_blocked(p->x,p->z+dz)) p->z+=dz;
    }
    p->heading=atan2f(x,z)*57.2957795f;
    float walked=hypotf(p->x-start_x,p->z-start_z);
    p->gait=fmodf(p->gait+walked*4.8f,6.2831853f);
}
