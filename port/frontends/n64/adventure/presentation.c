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
 a->idle_phase=fmodf(id*.7f,2*PI);
 a->pose.x=a->from_x=a->target_x=x;
 a->pose.z=a->from_z=a->target_z=z;
 a->pose.heading=a->target_heading=heading(d);
}
static bool target(PresentationActor *a,float x,float z,unsigned d,unsigned frames,bool running,bool teleport,bool tutorial)
{
 a->target_heading=heading(d);a->running=running;
 if(x==a->target_x&&z==a->target_z)return false;
 if(teleport) {
  unsigned id=a->id;snap(a,x,z,d,id);return true;
 }
 if(tutorial) {
  /* Tutorial motion runs at 70% speed. Carry the fractional rendered frame
     across steps per actor, rather than rounding every short step upward.
     This affects presentation only; source waits keep their own remainder. */
  unsigned scaled=frames*10+a->movement_remainder;
  frames=scaled/7;a->movement_remainder=scaled%7;
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
  a->idle_phase=fmodf(a->idle_phase+.1f,2*PI);
  a->pose.gait=0;a->pose.lean=0;
  /* These IDs are props rather than people. Keep cards and torches fixed. */
  a->pose.bob=a->id>=NPC_TORCH&&a->id<=NPC_LEGENDARY_CARD_RIGHT_SPARK?
   0:.008f*sinf(a->idle_phase);
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
  target(&p->world,x,z,direction,(unsigned)ceilf(distance*4),false,false,
         p->tutorial_motion&&p->world_script);
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
 p->tutorial_motion=p->cinematic&&
  (s->map==OVERWORLD_MAP||s->map==MASON_LABORATORY)&&
  ow_event(s,EVENT_MASON_LAB_STATE)<MASON_LAB_RECEIVED_STARTER_DECK;
 unsigned changes=0;bool map_change=!p->initialized||p->map!=s->map;
 if(map_change) {
  p->initialized=true;p->map=s->map;p->camera_initialized=false;
  p->conversation_anchor_npc=0;
  p->conversation_camera_locked=false;
  p->npc_count=0;p->world_count=p->world_index=0;p->world_script=0;
  snap(&p->player,coordinate(s->x),coordinate(s->y),s->direction,0);
  p->world_location=p->world_destination=s->overworld_selection;
  float x,z;world_position(p->world_location,&x,&z);snap(&p->world,x,z,SOUTH,0);
  changes|=PRESENTATION_MAP;
 } else if(s->map&&target(&p->player,coordinate(s->x),coordinate(s->y),s->direction,
                         player_duration(s,running),running||player_duration(s,false)<8,
                         magnitude(coordinate(s->x)-p->player.pose.x,coordinate(s->y)-p->player.pose.z)>5,
                         p->tutorial_motion&&presentation_scripted_player(s)))
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
                 opcode(s)==0x23||magnitude(coordinate(n->x)-next[i].pose.x,coordinate(n->y)-next[i].pose.z)>5,
                 p->tutorial_motion))
   changes|=PRESENTATION_NPC;
  /* Amy's source start is on her bench, including the short standing-up
     animation. Her subsequent (20,4) floor position has no elevation. */
  unsigned animation=s->npc_animation[i];
  next[i].pose.floor=s->map==WATER_CLUB&&n->id==NPC_AMY&&n->x==22&&n->y==4&&
    (animation==SPRITE_ANIM_CGB_AMY_LAYING||animation==SPRITE_ANIM_CGB_AMY_STAND)?
    (animation==SPRITE_ANIM_CGB_AMY_LAYING?.66f:.71f):0;
  if(s->map==HALL_OF_HONOR&&n->id>=NPC_LEGENDARY_CARD_TOP_LEFT&&
     n->id<=NPC_LEGENDARY_CARD_RIGHT_SPARK)next[i].pose.floor=2.2f;
 }
 memcpy(p->npcs,next,sizeof(next));p->npc_count=count;
 if(!s->map)world_position(s->overworld_selection,&p->cursor.x,&p->cursor.z);
 if(!s->map&&opcode(s)==0x45&&p->world_script!=s->script_pc) {
  p->world_script=s->script_pc;
  if(presentation_world_travel(p,s->overworld_selection))changes|=PRESENTATION_WORLD;
 }
 return changes;
}
void presentation_conversation(Presentation *p,unsigned npc)
{
 p->conversation_npc=npc<NUM_NPCS?(uint8_t)npc:0;
}

/* The source walk permissions also identify furniture footprints. They are
   a preference rather than a visibility veto: a camera can see over a desk. */
static bool camera_solid(const OwState *s,float x,float z)
{
 const OwMap *m=&ow_maps[s->map];
 int ix=(int)floorf(x+.5f),iz=(int)floorf(z+.5f);
 if(ix<0||iz<0||ix>=m->width||iz>=m->height)return true;
 unsigned value=m->permissions[(iz/2)*((m->width+1)/2)+ix/2];
 for(unsigned i=0;i<11;i++) {
  const OwPermissionPatch *patch=&ow_permission_patches[i];
  if(s->map_patch[i]&&patch->map==s->map&&patch->permissions&&ix>=patch->x&&iz>=patch->y&&
     ix<patch->x+patch->w&&iz<patch->y+patch->h)
   value=patch->permissions[((iz-patch->y)/2)*((patch->w+1)/2)+(ix-patch->x)/2];
 }
 return (value&128)!=0;
}

static bool conversation_fits(const PresentationCamera *camera,const PresentationPose *a,
 const PresentationPose *b)
{
 const float sine=.3420201433f,cosine=.9396926208f,tangent=.4663076582f;
 float dx=camera->eye[0]-camera->target[0],dz=camera->eye[2]-camera->target[2];
 float horizontal=magnitude(dx,dz),radius=horizontal/cosine;
 if(horizontal<3.4f)return false;
 float ux=dx/horizontal,uz=dz/horizontal;
 const PresentationPose *actors[]={a,b};
 for(unsigned actor=0;actor<2;actor++)for(unsigned corner=0;corner<8;corner++) {
  float x=actors[actor]->x+(corner&1?.7f:-.7f)-camera->target[0];
  float y=actors[actor]->floor+(corner&2?2.65f:1.05f)-camera->target[1];
  float z=actors[actor]->z+(corner&4?.5f:-.5f)-camera->target[2];
  float along=x*ux+z*uz,depth=radius-cosine*along-sine*y;
  if(depth<1.6f)return false;
  float sx=(x*uz-z*ux)/(depth*tangent*(4.0f/3.0f));
  float sy=(cosine*y-sine*along)/(depth*tangent);
  /* TV-safe sides/top and upper bodies above the dialogue name plate. */
  if(fabsf(sx)>.88f||sy>.85f||sy<-.28f)return false;
 }
 return true;
}

static bool conversation_candidate(const Presentation *p,const OwState *s,const W3Map *map,
 const PresentationActor *npc,float azimuth,PresentationCamera *camera,float *score)
{
 const PresentationPose *mark=&p->player.pose,*other=&npc->pose;
 float x=(mark->x+other->x)*.5f,z=(mark->z+other->z)*.5f;
 float ux=sinf(azimuth),uz=cosf(azimuth);
 camera->target[0]=x;camera->target[1]=1+maxf(mark->floor,other->floor)*.5f;
 camera->target[2]=z;camera->fov=50;
 for(unsigned step=0;step<29;step++) {
  float wanted=6.8f+step*.4f;
  float ex=clampf(x+ux*wanted,1.25f,map->width-1.25f);
  float ez=clampf(z+uz*wanted,1.25f,map->height-1.25f);
  float distance=magnitude(ex-x,ez-z);
  camera->eye[0]=ex;camera->eye[1]=camera->target[1]+distance*.3639702343f;
  camera->eye[2]=ez;
  if(!conversation_fits(camera,mark,other))continue;
  float actual=atan2f(ex-x,ez-z)*180/PI;
  float front=fabsf(angle_delta(actual,npc->target_heading));
  if(front>105)continue;
  float correction=magnitude(ex-(x+ux*wanted),ez-(z+uz*wanted));
  *score=fabsf(front-55)*.025f+fabsf(distance-6.8f)*.18f+correction*.15f;
  if(camera_solid(s,ex,ez))*score+=.7f;
  for(unsigned sample=1;sample<8;sample++) {
   float t=(float)sample/8;
   if(camera_solid(s,other->x+(ex-other->x)*t,other->z+(ez-other->z)*t))*score+=.12f;
  }
  /* A small preference for the existing viewing side avoids gratuitous cuts. */
  float previous=atan2f(p->camera.eye[0]-x,p->camera.eye[2]-z)*180/PI;
  *score+=fabsf(angle_delta(actual,previous))*.001f;
  return true;
 }
 return false;
}

static bool conversation_camera(Presentation *p,const OwState *s,const W3Map *map,
 PresentationCamera *camera)
{
 if(!p->conversation_npc||!s->map||map->width<4||map->height<4)return false;
 if(p->conversation_npc>=NPC_TORCH&&p->conversation_npc<=NPC_LEGENDARY_CARD_RIGHT_SPARK)return false;
 const PresentationActor *npc=NULL;
 for(unsigned i=0;i<p->npc_count;i++)
  if(p->npcs[i].id==p->conversation_npc&&p->npcs[i].visible){npc=&p->npcs[i];break;}
 if(!npc)return false;
 const PresentationPose *mark=&p->player.pose,*other=&npc->pose;
 /* Offstage callers and reclining/elevated special poses retain room framing. */
 if(other->x<0||other->z<0||other->x>=map->width||other->z>=map->height||other->floor>.4f||
    mark->x<0||mark->z<0||mark->x>=map->width||mark->z>=map->height||
    magnitude(other->x-mark->x,other->z-mark->z)>12)return false;
 float score;
 PresentationCamera cached;
 if(p->conversation_camera_locked&&p->conversation_anchor_npc==p->conversation_npc&&
    fabsf(angle_delta(p->conversation_heading,npc->target_heading))<1&&
    conversation_candidate(p,s,map,npc,p->conversation_azimuth,&cached,&score)) {
  *camera=cached;return true;
 }
 static const float offsets[]={55,-55,75,-75,35,-35,90,-90,0,105,-105};
 bool found=false;float best=1e9f,best_azimuth=0;
 for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
  PresentationCamera candidate;
  float azimuth=(npc->target_heading+offsets[i])*PI/180;
  if(conversation_candidate(p,s,map,npc,azimuth,&candidate,&score)&&score<best) {
   best=score;best_azimuth=azimuth;*camera=candidate;found=true;
  }
 }
 if(found) {
  p->conversation_camera_locked=true;p->conversation_anchor_npc=p->conversation_npc;
  p->conversation_heading=npc->target_heading;p->conversation_azimuth=best_azimuth;
 }
 return found;
}

static bool tracking_room(unsigned map)
{
 return map>OVERWORLD_MAP&&map<OW_MAP_COUNT;
}

static void exploration_camera(const Presentation *p,const OwState *s,const W3Map *map,
 PresentationCamera *camera)
{
 if(!tracking_room(s->map)||map->width<12||map->height<12)return;
 const PresentationPose *mark=&p->player.pose;
 /* Keep the authored doorway composition, then move the whole camera rig
    into the room. Aiming farther north alone makes the distant player tiny.
    Use the interpolated pose so tile boundaries do not become camera stops. */
 float shift_z=clampf(mark->z+10-map->eye[2],2-map->target[2],0);
 camera->eye[2]+=shift_z;camera->target[2]+=shift_z;
 /* Side aisles remain readable without panning for every small movement. */
 float shift_x=mark->x-map->target[0];
 if(shift_x>4)shift_x-=4;
 else if(shift_x< -4)shift_x+=4;
 else shift_x=0;
 shift_x=clampf(shift_x,5-map->eye[0],map->width-5-map->eye[0]);
 camera->eye[0]+=shift_x;camera->target[0]+=shift_x;
 /* Keep the north wall's top near its authored screen height as the eye
    moves inward. Respect the authored low walls in Lightning, the Dome foyer
    and the two open-air rooms, preserving their existing sky allowance. */
 float wall=s->map==LIGHTNING_CLUB?4.5f:
  s->map==POKEMON_DOME_ENTRANCE?3.5f:
  s->map==GRASS_CLUB||s->map==CHALLENGE_HALL?2:5;
 float old_top=atan2f(maxf(map->eye[1]-wall,0),maxf(map->eye[2]-1,1));
 float old_pitch=atan2f(map->eye[1]-map->target[1],map->eye[2]-map->target[2]);
 float half_fov=camera->fov*.5f*PI/180;
 float allowance=maxf(2*PI/180,old_top+half_fov-old_pitch);
 float top=atan2f(maxf(camera->eye[1]-wall,0),maxf(camera->eye[2]-1,1));
 float inside=camera->eye[2]-camera->eye[1]/tanf(top+half_fov-allowance);
 camera->target[2]=maxf(camera->target[2],inside);
}

void presentation_camera_step(Presentation *p,const OwState *s,const W3Map *map)
{
 PresentationCamera desired;
 memcpy(desired.eye,map->eye,sizeof(desired.eye));
 memcpy(desired.target,map->target,sizeof(desired.target));desired.fov=map->fov;
 exploration_camera(p,s,map,&desired);
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
 /* Dialogue owns a close shot only while its host explicitly requests it. */
 conversation_camera(p,s,map,&desired);
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
 presentation_camera_step(p,s,map);
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
