#include "../visibility.h"
#include "../../../../src/gen/constants.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void planes(void)
{
 PresentationCamera camera={{0,0,10},{0,0,0},50};AdventureFrustum f;
 adventure_frustum_init(&f,&camera,4.0f/3.0f,1.5f,90);
 assert(f.valid);
 float tangent=tanf(25*.01745329251994f),nx=sqrtf(1+tangent*tangent*16/9),ny=sqrtf(1+tangent*tangent);
 for(int direction=-1;direction<=1;direction+=2) {
  assert(adventure_frustum_sphere(&f,direction*(10*tangent*4/3+nx*.999f),0,0,1));
  assert(!adventure_frustum_sphere(&f,direction*(10*tangent*4/3+nx*1.001f),0,0,1));
  assert(adventure_frustum_sphere(&f,0,direction*(10*tangent+ny*.999f),0,1));
  assert(!adventure_frustum_sphere(&f,0,direction*(10*tangent+ny*1.001f),0,1));
 }
 assert(adventure_frustum_sphere(&f,0,0,9.5f,1));
 assert(!adventure_frustum_sphere(&f,0,0,9.51f,1));
 assert(adventure_frustum_sphere(&f,0,0,-81,1));
 assert(!adventure_frustum_sphere(&f,0,0,-81.01f,1));
 assert(!adventure_frustum_sphere(&f,0,0,20,1));
 AdventureFrustum before=f;PresentationPose pose={.x=100,.z=100,.floor=.7f,.bob=.05f};
 PresentationPose original=pose;adventure_frustum_actor(&f,&pose,.48f,.35f,90);
 assert(!memcmp(&before,&f,sizeof(f))&&!memcmp(&original,&pose,sizeof(pose)));
 camera.target[2]=10;adventure_frustum_init(&f,&camera,4.0f/3.0f,1.5f,90);
 assert(adventure_frustum_sphere(&f,1000,1000,1000,1));
}

static unsigned projection_sweep(void)
{
 unsigned visible_samples=0,culled=0;
 for(unsigned yaw=0;yaw<360;yaw+=30)for(unsigned pitch=20;pitch<=80;pitch+=30) {
  float a=yaw*.01745329251994f,b=pitch*.01745329251994f;
  float right[3]={cosf(a),0,-sinf(a)},up[3]={-sinf(a)*sinf(b),cosf(b),-cosf(a)*sinf(b)};
  float forward[3]={-sinf(a)*cosf(b),-sinf(b),-cosf(a)*cosf(b)};
  PresentationCamera camera={{-forward[0]*10,-forward[1]*10,-forward[2]*10},{0,0,0},50};
  AdventureFrustum f;adventure_frustum_init(&f,&camera,4.0f/3.0f,1.5f,90);
  float tangent=tanf(25*.01745329251994f);
  for(int depth=-4;depth<=100;depth+=4)for(int x=-70;x<=70;x+=5)for(int y=-50;y<=50;y+=5) {
   float center[3];for(unsigned i=0;i<3;i++)center[i]=camera.eye[i]+forward[i]*depth+right[i]*x+up[i]*y;
   bool shown=adventure_frustum_sphere(&f,center[0],center[1],center[2],2.2f);
   if(!shown)culled++;
   /* Independent Euler-camera projection of points on the actual sphere:
      any in-frame point requires keeping the complete actor. */
   for(unsigned latitude=0;latitude<=180;latitude+=45)for(unsigned longitude=0;longitude<360;longitude+=45) {
    float lat=latitude*.01745329251994f,lon=longitude*.01745329251994f;
    float px=x+2.2f*sinf(lat)*cosf(lon),py=y+2.2f*cosf(lat),pz=depth+2.2f*sinf(lat)*sinf(lon);
    if(pz>=1.5f&&pz<=90&&fabsf(px)<=pz*tangent*4/3&&fabsf(py)<=pz*tangent) {
     assert(shown);visible_samples++;
    }
   }
  }
 }
 assert(culled>1000&&visible_samples>1000);return visible_samples;
}

static void conversation_pairs(void)
{
 const W3Map lab={.eye={14,8.4375f,32},.target={14,0,20.1f},.fov=58,.width=28,.height=30};
 static const unsigned positions[][7]={
  {NPC_DRMASON,14,6,SOUTH,14,8,NORTH},{NPC_SAM,4,14,EAST,10,14,WEST},
  {NPC_RONALD1,12,10,EAST,14,10,WEST},{NPC_DRMASON,6,2,SOUTH,6,4,NORTH},
  {NPC_DRMASON,2,2,NORTH,2,4,NORTH},{NPC_DRMASON,26,4,WEST,24,4,EAST}
 };
 for(unsigned i=0;i<sizeof(positions)/sizeof(*positions);i++) {
  const unsigned *v=positions[i];
  OwState state={.map=MASON_LABORATORY,.x=v[4],.y=v[5],.direction=v[6],.npc_count=1,
   .npcs={{v[0],v[1],v[2],v[3],1,0}}};
  OwState original=state;Presentation p;presentation_init(&p,&state,false);
  presentation_conversation(&p,v[0]);
  for(unsigned frame=0;frame<80;frame++) {
   presentation_camera_step(&p,&state,&lab);AdventureFrustum f;
   adventure_frustum_init(&f,presentation_camera(&p),4.0f/3.0f,1.5f,90);
   assert(adventure_frustum_actor(&f,presentation_player(&p),1,0,0));
   assert(adventure_frustum_actor(&f,presentation_npc(&p,v[0]),1,0,0));
  }
  assert(!memcmp(&state,&original,sizeof(state)));
 }
}

int main(void)
{
 planes();unsigned samples=projection_sweep();conversation_pairs();
 printf("ACTOR VISIBILITY PASS: six planes; 36 orientations; %u independent visible projections preserved; real dialogue entry/settled pairs; no state changes\n",samples);
 return 0;
}
