#include "world.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    Player p;
    world_reset(&p);
    assert(p.x==14 && p.z==28 && !world_blocked(p.x,p.z));
    /* Long running input cannot tunnel through Jennifer in the center aisle. */
    for (int i=0;i<180;i++) world_move(&p,0,-1,true,1.0f/30);
    assert(p.z>18.71f && p.z<18.88f && !world_blocked(p.x,p.z));
    /* Circle sweeps stop at the two tall pillar feet and the room shell. */
    p=(Player){9,25,0,0};
    for (int i=0;i<180;i++) world_move(&p,0,-1,true,1);
    assert(p.z>21.35f && p.z<21.51f);
    p=(Player){3,16,0,0};
    for (int i=0;i<180;i++) world_move(&p,-1,-1,true,1.0f/30);
    assert(p.x>=2.36f && !world_blocked(p.x,p.z));
    /* Diagonal input is normalized; movement is independent of frame rate. */
    Player a={12,26,0,0},b=a,c=a;
    for(int i=0;i<30;i++) world_move(&a,0,-1,false,1.0f/30);
    for(int i=0;i<60;i++) world_move(&b,0,-1,false,1.0f/60);
    for(int i=0;i<30;i++) world_move(&c,1,-1,false,1.0f/30);
    assert(fabsf(a.z-b.z)<.001f);
    assert(fabsf(hypotf(c.x-12,c.z-26)-(26-a.z))<.001f);
    assert(world_blocked(12,3.4f));
    for(unsigned i=0;i<4;i++) assert(world_blocked(lightning_npcs[i].x,lightning_npcs[i].z));
    puts("World OK: walls, pillars, NPCs, sliding, swept movement, diagonal speed and 30/60 Hz movement");
}
