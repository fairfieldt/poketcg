#include "presentation.h"
#include "../../../overworld/gen/data.h"
#include "../../../src/gen/constants.h"
#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846f

static float magnitude(float x,float z) {return sqrtf(x*x+z*z);}
static float maxf(float x,float y) {return x>y?x:y;}
static float clampf(float x,float lo,float hi) {return x<lo?lo:x>hi?hi:x;}
static float coordinate(unsigned n) {return n>=240?(float)((int)n-256):(float)n;}
static float heading(unsigned direction) {return 180-(direction&3)*90.0f;}
static float angle_delta(float target,float current)
{
 float d=fmodf(target-current,360);
 if(d>180)d-=360;
 if(d< -180)d+=360;
 return d;
}
static unsigned opcode(const OwState *s)
{return s->script_pc>=0x4000&&s->script_pc<0x8000?ow_script_bank[s->script_pc-0x4000]:255;}
bool presentation_scripted_player(const OwState *s) {return opcode(s)==0x1e;}
static unsigned player_duration(const OwState *s,bool running)
{
 unsigned speed=running?2:1;
 if(presentation_scripted_player(s)) {
  speed=ow_script_bank[(s->script_pc+2-0x4000)&0x3fff];
  if(!speed)speed=1;
 }
 /* StartScriptedMovement moves 16 source pixels at speed pixels/GB frame.
    Two 60 Hz source frames are one 30 Hz rendered frame. */
 return (8+speed-1)/speed;
}
static void snap(PresentationActor *a,float x,float z,unsigned d,unsigned id)
{
 memset(a,0,sizeof(*a));a->id=id;a->visible=1;
 a->pose.x=a->from_x=a->target_x=x;
 a->pose.z=a->from_z=a->target_z=z;
 a->pose.heading=a->target_heading=heading(d);
}
static bool target(PresentationActor *a,float x,float z,unsigned d,unsigned frames,bool running,bool teleport)
{
 a->target_heading=heading(d);a->running=running;
 if(x==a->target_x&&z==a->target_z)return false;
 if(teleport) {
  unsigned id=a->id;snap(a,x,z,d,id);return true;
 }
 a->from_x=a->pose.x;a->from_z=a->pose.z;
 a->target_x=x;a->target_z=z;a->elapsed=0;a->duration=frames?frames:1;
 return true;
}
static bool moving(const PresentationActor *a)
{return a->elapsed<a->duration||fabsf(angle_delta(a->target_heading,a->pose.heading))>.01f;}
static void step_actor(PresentationActor *a)
{
 float old_x=a->pose.x,old_z=a->pose.z;
 if(a->elapsed<a->duration) {
  float t=(float)++a->elapsed/a->duration;
  a->pose.x=a->from_x+(a->target_x-a->from_x)*t;
  a->pose.z=a->from_z+(a->target_z-a->from_z)*t;
 }
 float distance=magnitude(a->pose.x-old_x,a->pose.z-old_z);
 a->pose.heading+=clampf(angle_delta(a->target_heading,a->pose.heading),-45,45);
 if(distance>.0001f) {
  /* A cycle per two source map tiles keeps the feet tied to distance,
     including fast scripted movement, rather than arbitrary render count. */
  a->phase=fmodf(a->phase+distance*PI,2*PI);
  a->pose.gait=a->phase;
  a->pose.bob=fabsf(sinf(a->phase))*(a->running?.065f:.035f);
  a->pose.lean=a->running?6:0;
 } else {
  a->pose.gait=0;a->pose.bob=0;a->pose.lean=0;
 }
}
static void world_position(unsigned location,float *x,float *z)
{
 if(location<1||location>12)location=1;
 *x=ow_location_positions[location][0]/8.0f+1;
 *z=ow_location_positions[location][1]/8.0f+1;
}
static void world_segment(Presentation *p)
{
 while(p->world_index<p->world_count) {
  const uint8_t *point=p->world_points[p->world_index++];
  float x=point[0]/8.0f+1,z=point[1]/8.0f+1;
  float dx=x-p->world.pose.x,dz=z-p->world.pose.z;
  float distance=maxf(fabsf(dx),fabsf(dz));
  if(distance<.001f)continue;
  unsigned direction=fabsf(dx)>=fabsf(dz)?(dx>=0?EAST:WEST):(dz>=0?SOUTH:NORTH);
  /* The source island path walks its dominant axis one pixel/GB frame. */
  target(&p->world,x,z,direction,(unsigned)ceilf(distance*4),false,false);
  return;
 }
 p->world_count=p->world_index=0;p->world_location=p->world_destination;
}
bool presentation_world_travel(Presentation *p,unsigned destination)
{
 if(p->map||destination<1||destination>12||p->world_count)return false;
 p->world_destination=destination;
 unsigned n=ow_world_path(p->world_location,destination,p->world_points,64);
 if(n>64)n=64;
 p->world_count=n;p->world_index=0;
 if(!n){p->world_location=destination;return false;}
 world_segment(p);
 return p->world_count!=0;
}
void presentation_init(Presentation *p,const OwState *s,bool cinematic)
{
 memset(p,0,sizeof(*p));p->cinematic=cinematic;
 if(s)presentation_sync(p,s,false);
}
unsigned presentation_sync(Presentation *p,const OwState *s,bool running)
{
 unsigned changes=0;bool map_change=!p->initialized||p->map!=s->map;
 if(map_change) {
  p->initialized=true;p->map=s->map;p->camera_initialized=false;
  p->npc_count=0;p->world_count=p->world_index=0;p->world_script=0;
  snap(&p->player,coordinate(s->x),coordinate(s->y),s->direction,0);
  p->world_location=p->world_destination=s->overworld_selection;
  float x,z;world_position(p->world_location,&x,&z);snap(&p->world,x,z,SOUTH,0);
  changes|=PRESENTATION_MAP;
 } else if(s->map&&target(&p->player,coordinate(s->x),coordinate(s->y),s->direction,
                         player_duration(s,running),running||player_duration(s,false)<8,
                         magnitude(coordinate(s->x)-p->player.pose.x,coordinate(s->y)-p->player.pose.z)>5))
  changes|=PRESENTATION_PLAYER;
 PresentationActor next[OW_NPC_LIMIT];memset(next,0,sizeof(next));
 unsigned count=s->npc_count<OW_NPC_LIMIT?s->npc_count:OW_NPC_LIMIT;
 for(unsigned i=0;i<count;i++) {
  const OwNpc *n=&s->npcs[i];const PresentationActor *old=NULL;
  for(unsigned j=0;j<p->npc_count;j++)if(p->npcs[j].id==n->id){old=&p->npcs[j];break;}
  if(old&&!map_change)next[i]=*old;
  else snap(&next[i],coordinate(n->x),coordinate(n->y),n->direction,n->id);
  next[i].visible=n->visible;
  if(old&&target(&next[i],coordinate(n->x),coordinate(n->y),n->direction,8,false,
                 opcode(s)==0x23||magnitude(coordinate(n->x)-next[i].pose.x,coordinate(n->y)-next[i].pose.z)>5))
   changes|=PRESENTATION_NPC;
 }
 memcpy(p->npcs,next,sizeof(next));p->npc_count=count;
 if(!s->map)world_position(s->overworld_selection,&p->cursor.x,&p->cursor.z);
 if(!s->map&&opcode(s)==0x45&&p->world_script!=s->script_pc) {
  p->world_script=s->script_pc;
  if(presentation_world_travel(p,s->overworld_selection))changes|=PRESENTATION_WORLD;
 }
 return changes;
}
static void update_camera(Presentation *p,const OwState *s,const W3Map *map)
{
 PresentationCamera desired;
 memcpy(desired.eye,map->eye,sizeof(desired.eye));
 memcpy(desired.target,map->target,sizeof(desired.target));desired.fov=map->fov;
 if(p->cinematic&&s->map==MASON_LABORATORY&&ow_event(s,EVENT_MASON_LAB_STATE)<MASON_LAB_RECEIVED_STARTER_DECK) {
  const PresentationPose *mark=&p->player.pose;
  if(s->active_npc==NPC_SAM)p->intro_stage=1;
  if(!p->intro_stage&&mark->x>=13.5f&&mark->z>=8) {
   /* Track the run toward Mason; the old fixed entrance camera loses Mark
      at the far end of this long room. Keep the destination ahead in shot. */
   desired.eye[0]=mark->x+4.5f;desired.eye[1]=6.8f;desired.eye[2]=mark->z+10.5f;
   desired.target[0]=mark->x;desired.target[1]=1;desired.target[2]=mark->z-2;
   desired.fov=52;
  } else {
   const PresentationPose *sam=presentation_npc(p,NPC_SAM);
   float center_x=sam?(mark->x+sam->x)*.5f:mark->x;
   float center_z=sam?(mark->z+sam->z)*.5f:mark->z;
   desired.eye[0]=center_x+.5f;desired.eye[1]=10;desired.eye[2]=center_z+10;
   desired.target[0]=center_x;desired.target[1]=1;desired.target[2]=center_z;
   desired.fov=50;
  }
 }
 if(!p->camera_initialized) {p->camera=desired;p->camera_initialized=true;return;}
 for(unsigned i=0;i<3;i++) {
  p->camera.eye[i]+=(desired.eye[i]-p->camera.eye[i])*.24f;
  p->camera.target[i]+=(desired.target[i]-p->camera.target[i])*.24f;
 }
 p->camera.fov+=(desired.fov-p->camera.fov)*.24f;
}
void presentation_step(Presentation *p,const OwState *s,const W3Map *map)
{
 step_actor(&p->player);
 for(unsigned i=0;i<p->npc_count;i++)step_actor(&p->npcs[i]);
 step_actor(&p->world);
 p->cursor.bob=.08f*sinf(++p->render_frames*.12f);
 if(p->world_count&&p->world.elapsed>=p->world.duration)world_segment(p);
 update_camera(p,s,map);
}
bool presentation_moving(const Presentation *p)
{
 if(moving(&p->player)||p->world_count||moving(&p->world))return true;
 for(unsigned i=0;i<p->npc_count;i++)if(p->npcs[i].visible&&moving(&p->npcs[i]))return true;
 return false;
}
unsigned presentation_wait_frames(Presentation *p,unsigned source_frames)
{
 unsigned n=source_frames+p->source_remainder;
 p->source_remainder=n&1;return n/2;
}
const PresentationPose *presentation_player(const Presentation *p) {return &p->player.pose;}
const PresentationPose *presentation_npc(const Presentation *p,unsigned id)
{
 for(unsigned i=0;i<p->npc_count;i++)if(p->npcs[i].id==id&&p->npcs[i].visible)return &p->npcs[i].pose;
 return NULL;
}
const PresentationPose *presentation_world(const Presentation *p) {return &p->world.pose;}
const PresentationPose *presentation_world_cursor(const Presentation *p) {return &p->cursor;}
const PresentationCamera *presentation_camera(const Presentation *p) {return &p->camera;}
