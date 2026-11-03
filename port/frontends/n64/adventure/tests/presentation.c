#include "../presentation.h"
#include "../../../../src/cat_duo.h"
#include "../../../../overworld/gen/data.h"
#include "../../../../src/gen/constants.h"
#include "../../../../world3d/generated/world3d_maps.h"
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static const W3Map island={.eye={10,27,12.5f},.target={10,0,9},.fov=40,.width=20,.height=18};
static const W3Map lab={.eye={14,8.4375f,32},.target={14,0,20.1f},.fov=58,.width=28,.height=30};
static Presentation view;
static OwState *current;
static unsigned run_commands,run_frames,walk_commands,walk_frames,world_frames,wait_source,wait_video,first_map;
static uint16_t tutorial_run_pc;
static bool initial_map_seen,world_walk_seen;
static jmp_buf at_duel;
static void close_to(float x,float expected) {assert(fabsf(x-expected)<.001f);}
static unsigned route_frames(unsigned from,unsigned to)
{
 uint8_t points[64][2];unsigned n=ow_world_path(from,to,points,64),frames=0;
 float x=ow_location_positions[from][0]/8.0f+1,z=ow_location_positions[from][1]/8.0f+1;
 for(unsigned i=0;i<n;i++) {
  float nx=points[i][0]/8.0f+1,nz=points[i][1]/8.0f+1;
  float distance=fmaxf(fabsf(nx-x),fabsf(nz-z));
  if(distance>.001f)frames+=(unsigned)ceilf(distance*4);
  x=nx;z=nz;
 }
 return frames;
}
static unsigned advance(Presentation *p,const OwState *s,const W3Map *map)
{
 unsigned n=0;OwState before=*s;
 while(presentation_moving(p)){assert(++n<2000);presentation_step(p,s,map);}
 assert(!memcmp(s,&before,sizeof(before)));return n;
}
static void frame(void)
{
 OwState before=*current;
 presentation_sync(&view,current,false);
 presentation_step(&view,current,current->map?&lab:&island);
 assert(!memcmp(current,&before,sizeof(before)));
}
static unsigned settle(void)
{
 unsigned n=0;while(presentation_moving(&view)){assert(++n<2000);frame();}return n;
}
static void changed(void *ctx,OwState *s)
{
 (void)ctx;current=s;
 unsigned flags=presentation_sync(&view,s,false);
 if(!initial_map_seen) {
  assert(s->map==OVERWORLD_MAP&&s->overworld_selection==OWMAP_POKEMON_DOME);
  assert(s->x==12&&s->y==12&&s->direction==SOUTH);
  first_map=s->overworld_selection;initial_map_seen=true;
 }
 if(flags&PRESENTATION_WORLD) {
  assert(first_map==OWMAP_POKEMON_DOME&&s->overworld_selection==OWMAP_MASON_LABORATORY);
  world_frames=settle();
  assert(world_frames==route_frames(first_map,OWMAP_MASON_LABORATORY)*10/7);
  world_walk_seen=true;
  close_to(presentation_world(&view)->x,ow_location_positions[1][0]/8.0f+1);
  close_to(presentation_world(&view)->z,ow_location_positions[1][1]/8.0f+1);
 }
 if((flags&PRESENTATION_PLAYER)&&presentation_scripted_player(s)) {
  bool run=s->x==14&&s->y>=8&&!walk_commands;
  unsigned source_total=run_commands*4+walk_commands*8+(run?4:8);
  unsigned expected=source_total*10/7-run_frames-walk_frames;
  assert(view.player.duration==expected);
  unsigned count=settle();
  assert(count==expected);
  if(run){tutorial_run_pc=s->script_pc;run_commands++;run_frames+=count;}
  else {walk_commands++;walk_frames+=count;}
 }
 if(flags&PRESENTATION_MAP)frame();
}
static void wait_frames(void *ctx,unsigned source)
{
 (void)ctx;wait_source+=source;
 unsigned frames=presentation_wait_frames(&view,source);wait_video+=frames;
 for(unsigned f=0;f<frames;f++)frame();
}
static void text(void *ctx,OwState *s,uint16_t id,uint8_t npc)
{
 (void)ctx;(void)id;(void)npc;current=s;settle();
 close_to(presentation_player(&view)->x,s->x);
 close_to(presentation_player(&view)->z,s->y);
 if(!walk_commands&&s->map==MASON_LABORATORY&&!view.intro_stage) {
  assert(run_commands==9&&run_frames==51);
  float distance=presentation_camera(&view)->eye[2]-presentation_player(&view)->z;
  assert(distance>10.4f&&distance<13);
 }
}
static int choice(void *ctx,OwState *s,unsigned kind,uint16_t id,unsigned count,unsigned initial)
{(void)ctx;(void)s;(void)id;(void)count;(void)initial;return kind==OW_CHOICE_SAM_RULES?7:1;}
static int duel(void *ctx,OwState *s,const OwDuel *d)
{
 (void)ctx;current=s;settle();assert(d->npc==NPC_SAM&&d->deck==SAMS_PRACTICE_DECK_ID);
 assert(run_commands==9&&run_frames==51&&walk_commands==5&&walk_frames==57&&world_walk_seen);
 close_to(presentation_player(&view)->x,10);close_to(presentation_player(&view)->z,14);
 const PresentationPose *sam=presentation_npc(&view,NPC_SAM);
 assert(sam);close_to(sam->x,4);close_to(sam->z,14);
 assert(wait_video*2+view.source_remainder==wait_source);
 longjmp(at_duel,1);
}
static bool feature(void *ctx,OwState *s,unsigned f,unsigned a)
{(void)ctx;(void)s;(void)f;(void)a;return true;}
static void error(void *ctx,const char *s) {(void)ctx;fprintf(stderr,"Presentation script: %s\n",s);assert(false);}

static void camera_settle(Presentation *p,const OwState *s,const W3Map *map)
{for(unsigned frame=0;frame<60;frame++)presentation_camera_step(p,s,map);}

/* Independent look-at projection checks the visible upper-body envelope,
   including broad heads/hair, against the actual dialogue panel boundary. */
static void conversation_visible(const PresentationCamera *c,const PresentationPose *actor)
{
 float f[3],r[3],u[3],length=0;
 for(unsigned i=0;i<3;i++){f[i]=c->target[i]-c->eye[i];length+=f[i]*f[i];}
 length=sqrtf(length);for(unsigned i=0;i<3;i++)f[i]/=length;
 length=sqrtf(f[0]*f[0]+f[2]*f[2]);r[0]=-f[2]/length;r[1]=0;r[2]=f[0]/length;
 u[0]=r[1]*f[2]-r[2]*f[1];u[1]=r[2]*f[0]-r[0]*f[2];u[2]=r[0]*f[1]-r[1]*f[0];
 float tangent=tanf(c->fov*3.14159265358979323846f/360);
 for(unsigned corner=0;corner<8;corner++) {
  float point[3]={actor->x+(corner&1?.65f:-.65f),actor->floor+(corner&2?2.5f:1.2f),
                  actor->z+(corner&4?.35f:-.35f)};
  float horizontal=0,vertical=0,depth=0;
  for(unsigned i=0;i<3;i++){float d=point[i]-c->eye[i];horizontal+=d*r[i];vertical+=d*u[i];depth+=d*f[i];}
  float x=160+horizontal/(depth*tangent*(4.0f/3.0f))*160;
  float y=120-vertical/(depth*tangent)*120;
  assert(depth>1.5f&&x>10&&x<310&&y>12&&y<155);
 }
}

static void test_conversation_cameras(void)
{
 /* Face the speaker from each source direction without changing either
    actor's direction/position, and return smoothly to the exploration view. */
 static const int dx[]={0,2,0,-2},dz[]={-2,0,2,0};
 unsigned cases=0;
 for(unsigned direction=0;direction<4;direction++) {
  OwState state={.map=MASON_LABORATORY,.x=(uint8_t)(14+dx[direction]),
   .y=(uint8_t)(14+dz[direction]),.direction=(direction+2)&3,.npc_count=1,
   .npcs={{NPC_RONALD1,14,14,0,1,0}}};
  state.npcs[0].direction=direction;
  Presentation p;presentation_init(&p,&state,false);presentation_camera_step(&p,&state,&lab);
  PresentationCamera wide=p.camera;PresentationPose player=p.player.pose,npc=p.npcs[0].pose;
  OwState unchanged=state;
  presentation_conversation(&p,NPC_RONALD1);presentation_camera_step(&p,&state,&lab);
  assert(fabsf(p.camera.fov-50)>1&&fabsf(p.camera.fov-wide.fov)>1);
  camera_settle(&p,&state,&lab);
  float horizontal=hypotf(p.camera.eye[0]-p.camera.target[0],p.camera.eye[2]-p.camera.target[2]);
  close_to(atan2f(p.camera.eye[1]-p.camera.target[1],horizontal)*180/3.14159265358979323846f,20);
  close_to(p.camera.fov,50);assert(horizontal>=6.79f&&horizontal<12);
  conversation_visible(&p.camera,&p.player.pose);conversation_visible(&p.camera,&p.npcs[0].pose);
  assert(!memcmp(&player,&p.player.pose,sizeof(player))&&!memcmp(&npc,&p.npcs[0].pose,sizeof(npc)));
  PresentationCamera held=p.camera;
  presentation_conversation(&p,0);presentation_conversation(&p,NPC_RONALD1);
  presentation_camera_step(&p,&state,&lab);
  for(unsigned i=0;i<3;i++){close_to(p.camera.eye[i],held.eye[i]);close_to(p.camera.target[i],held.target[i]);}
  presentation_conversation(&p,0);presentation_camera_step(&p,&state,&lab);
  assert(fabsf(p.camera.fov-wide.fov)>1);camera_settle(&p,&state,&lab);
  for(unsigned i=0;i<3;i++){close_to(p.camera.eye[i],wide.eye[i]);close_to(p.camera.target[i],wide.target[i]);}
  close_to(p.camera.fov,wide.fov);assert(!memcmp(&state,&unchanged,sizeof(state)));cases++;
 }
 /* Real source positions, including the wide six-unit tutorial table and a
    clerk against the far wall; both upper bodies must clear the text box. */
 static const struct {unsigned npc,nx,nz,dir,px,pz,pdir;} fixtures[]={
  {NPC_DRMASON,14,6,SOUTH,14,8,NORTH},
  {NPC_SAM,4,14,EAST,10,14,WEST},
  {NPC_RONALD1,12,10,EAST,14,10,WEST},
  {NPC_RONALD1,6,2,SOUTH,6,4,NORTH},
  {NPC_RONALD1,2,2,NORTH,2,4,NORTH},
  {NPC_RONALD1,26,4,WEST,24,4,EAST},
  {NPC_RONALD1,26,28,NORTH,26,26,SOUTH}};
 for(unsigned i=0;i<sizeof(fixtures)/sizeof(fixtures[0]);i++) {
  OwState state={.map=MASON_LABORATORY,.x=fixtures[i].px,.y=fixtures[i].pz,
   .direction=fixtures[i].pdir,.npc_count=1,
   .npcs={{fixtures[i].npc,fixtures[i].nx,fixtures[i].nz,fixtures[i].dir,1,0}}};
  OwState unchanged=state;Presentation p;presentation_init(&p,&state,false);
  presentation_conversation(&p,fixtures[i].npc);camera_settle(&p,&state,&lab);
  float horizontal=hypotf(p.camera.eye[0]-p.camera.target[0],p.camera.eye[2]-p.camera.target[2]);
  close_to(atan2f(p.camera.eye[1]-p.camera.target[1],horizontal)*180/3.14159265358979323846f,20);
  assert(p.camera.eye[0]>=1.24f&&p.camera.eye[0]<=lab.width-1.24f);
  assert(p.camera.eye[2]>=1.24f&&p.camera.eye[2]<=lab.height-1.24f);
  conversation_visible(&p.camera,&p.player.pose);conversation_visible(&p.camera,&p.npcs[0].pose);
  assert(!memcmp(&state,&unchanged,sizeof(state)));cases++;
 }
 /* Invalid speakers, props, offstage actors and the island cannot inherit a
    previous close camera. These are presentation-only fallbacks. */
 for(unsigned kind=0;kind<6;kind++) {
  OwState state={.map=MASON_LABORATORY,.x=14,.y=8,.direction=NORTH,.npc_count=1,
   .npcs={{NPC_DRMASON,14,6,SOUTH,1,0}}};
  Presentation p;presentation_init(&p,&state,false);
  presentation_conversation(&p,NPC_DRMASON);camera_settle(&p,&state,&lab);
  unsigned speaker=NPC_DRMASON;
  if(kind==0)speaker=0;
  if(kind==1)speaker=NUM_NPCS+1;
  if(kind==2)state.npcs[0].visible=0;
  if(kind==3){state.npcs[0].id=NPC_TORCH;speaker=NPC_TORCH;}
  if(kind==4)state.npcs[0].y=255;
  if(kind==5)state.npcs[0].y=28;
  presentation_sync(&p,&state,false);presentation_conversation(&p,speaker);camera_settle(&p,&state,&lab);
  Presentation exploration;presentation_init(&exploration,&state,false);camera_settle(&exploration,&state,&lab);
  for(unsigned axis=0;axis<3;axis++) {
   close_to(p.camera.eye[axis],exploration.camera.eye[axis]);close_to(p.camera.target[axis],exploration.camera.target[axis]);
  }
 }
 OwState world={.map=OVERWORLD_MAP,.overworld_selection=OWMAP_MASON_LABORATORY};Presentation p;
 presentation_init(&p,&world,false);presentation_conversation(&p,NPC_DRMASON);camera_settle(&p,&world,&island);
 for(unsigned i=0;i<3;i++){close_to(p.camera.eye[i],island.eye[i]);close_to(p.camera.target[i],island.target[i]);}
 printf("CONVERSATION CAMERA PASS: %u direction/source/edge compositions; speaker faces,20deg/FOV50,dialogue clearance,stable bubbles,exit and invalid-target fallback; canonical state unchanged\n",cases);
}

static void project_point(const PresentationCamera *c,float x,float y,float z,float *sx,float *sy)
{
 float fx=c->target[0]-c->eye[0],fy=c->target[1]-c->eye[1],fz=c->target[2]-c->eye[2];
 float length=sqrtf(fx*fx+fy*fy+fz*fz);fx/=length;fy/=length;fz/=length;
 float horizontal=hypotf(fx,fz),rx=-fz/horizontal,rz=fx/horizontal;
 float ux=-rz*fy,uy=rz*fx-rx*fz,uz=rx*fy;
 x-=c->eye[0];y-=c->eye[1];z-=c->eye[2];
 float depth=x*fx+y*fy+z*fz,tangent=tanf(c->fov*3.14159265358979323846f/360);
 assert(depth>1);
 *sx=160+(x*rx+z*rz)/(depth*tangent*(4.0f/3.0f))*160;
 *sy=120-(x*ux+y*uy+z*uz)/(depth*tangent)*120;
}

static float projected_height(const PresentationCamera *c,float x,float z)
{
 float sx,feet,head;project_point(c,x,0,z,&sx,&feet);project_point(c,x,2.2f,z,&sx,&head);
 return feet-head;
}

static void test_interior_tracking(void)
{
 unsigned compositions=0;
 for(unsigned id=1;id<W3_MAP_COUNT;id++) {
  const W3Map *map=&w3_maps[id];
  PresentationCamera authored={.fov=map->fov};
  memcpy(authored.eye,map->eye,sizeof(authored.eye));memcpy(authored.target,map->target,sizeof(authored.target));
  OwState state={.map=id,.x=map->width/2,.y=map->height-4,.direction=NORTH};
  Presentation p;presentation_init(&p,&state,false);camera_settle(&p,&state,map);
  for(unsigned axis=0;axis<3;axis++) {
   close_to(p.camera.eye[axis],map->eye[axis]);close_to(p.camera.target[axis],map->target[axis]);
  }
  float entry_height=projected_height(&p.camera,p.player.pose.x,p.player.pose.z);
  /* Walk the central aisle using real source tile interpolation. The camera
     must move through tile boundaries, with bounded per-frame displacement. */
  float last_eye=p.camera.eye[2];unsigned moving_frames=0;
  for(unsigned z=state.y;z>6;z-=2) {
   state.y-=2;OwState canonical=state;presentation_sync(&p,&state,false);
   for(unsigned frame=0;frame<8;frame++) {
    presentation_step(&p,&state,map);
    float movement=last_eye-p.camera.eye[2];assert(movement>=-.001f&&movement<.251f);
    if(movement>.01f)moving_frames++;
    last_eye=p.camera.eye[2];
   }
   assert(!memcmp(&state,&canonical,sizeof(state)));
  }
  camera_settle(&p,&state,map);
  float leader_height=projected_height(&p.camera,p.player.pose.x,p.player.pose.z);
  float old_leader=projected_height(&authored,p.player.pose.x,p.player.pose.z);
  assert(moving_frames>8&&leader_height>=entry_height*.78f&&leader_height>old_leader*1.12f);
  if(map->height>=28)assert(leader_height>old_leader*1.5f);
  assert(p.camera.eye[2]<map->eye[2]-3&&p.camera.target[2]>=2);
  /* Project every unblocked, non-warp source cell. A broad hair silhouette
     and feet stay inside the TV-safe scene at either N64 resolution. */
  const OwMap *source=&ow_maps[id];
  for(unsigned z=2;z<=map->height-4;z+=2)for(unsigned x=2;x<=map->width-4;x+=2) {
   if(source->permissions[(z/2)*((source->width+1)/2)+x/2]&128)continue;
   state.x=x;state.y=z;OwState canonical=state;Presentation sample;
   presentation_init(&sample,&state,false);camera_settle(&sample,&state,map);
   const PresentationCamera *camera=&sample.camera;
   assert(camera->eye[0]>=5&&camera->eye[0]<=map->width-5);
   assert(camera->eye[2]>=z+5.99f&&camera->eye[2]<=map->eye[2]);
   assert(camera->target[2]>=2&&camera->target[2]<=map->target[2]+.001f);
   close_to(camera->fov,map->fov);close_to(camera->eye[1],map->eye[1]);
   for(unsigned corner=0;corner<8;corner++) {
    float sx,sy;project_point(camera,x+(corner&1?.65f:-.65f),corner&2?2.4f:0,
     z+(corner&4?.35f:-.35f),&sx,&sy);
    assert(sx>10&&sx<310&&sy>16&&sy<228);
   }
   assert(projected_height(camera,x,z)>=entry_height*.73f);
   float wall=id==LIGHTNING_CLUB?4.5f:id==POKEMON_DOME_ENTRANCE?3.5f:
    id==GRASS_CLUB||id==CHALLENGE_HALL?2:5;
   float sx,sy;project_point(camera,camera->eye[0],wall,1,&sx,&sy);
   /* Test actual authored wall heights, including the two open-air rooms;
      their existing sky band can stay, but cannot grow as Mark walks north. */
   float old_x,old_y;project_point(&authored,authored.eye[0],wall,1,&old_x,&old_y);
   assert(sy<=fmaxf(10,old_y)+.1f);
   assert(!memcmp(&state,&canonical,sizeof(state)));compositions++;
  }
  /* NPC dialogue still owns the approved 20-degree shot; closing it returns
     smoothly to the tracked exploration camera, not the distant entry view. */
  state.x=map->width/2;state.y=6;state.npc_count=1;
  state.npcs[0]=(OwNpc){NPC_RONALD1,state.x,4,SOUTH,1,0};
  presentation_init(&p,&state,false);camera_settle(&p,&state,map);
  PresentationCamera exploration=p.camera;OwState canonical=state;
  presentation_conversation(&p,NPC_RONALD1);camera_settle(&p,&state,map);
  close_to(p.camera.fov,50);
  float distance=hypotf(p.camera.eye[0]-p.camera.target[0],p.camera.eye[2]-p.camera.target[2]);
  close_to(atan2f(p.camera.eye[1]-p.camera.target[1],distance)*180/3.14159265358979323846f,20);
  conversation_visible(&p.camera,&p.player.pose);conversation_visible(&p.camera,&p.npcs[0].pose);
  presentation_conversation(&p,0);presentation_camera_step(&p,&state,map);
  assert(fabsf(p.camera.fov-exploration.fov)>1);camera_settle(&p,&state,map);
  for(unsigned axis=0;axis<3;axis++) {
   close_to(p.camera.eye[axis],exploration.eye[axis]);close_to(p.camera.target[axis],exploration.target[axis]);
  }
  assert(!memcmp(&state,&canonical,sizeof(state)));
  /* Sweep all four movement directions at the rendered walking speed. This
     also checks the short follow lag while returning toward the doorway. */
  state.npc_count=0;state.x=map->width/2;state.y=map->height-4;
  presentation_init(&p,&state,false);camera_settle(&p,&state,map);
  const unsigned path[][2]={{map->width/2,2},{2,2},{2,map->height-4},
   {map->width-4,map->height-4},{map->width-4,2},{map->width/2,2}};
  for(unsigned leg=0;leg<sizeof(path)/sizeof(path[0]);leg++) {
   while(state.x!=path[leg][0]||state.y!=path[leg][1]) {
    if(state.x<path[leg][0]){state.x+=2;state.direction=EAST;}
    else if(state.x>path[leg][0]){state.x-=2;state.direction=WEST;}
    else if(state.y<path[leg][1]){state.y+=2;state.direction=SOUTH;}
    else {state.y-=2;state.direction=NORTH;}
    canonical=state;presentation_sync(&p,&state,false);
    for(unsigned frame=0;frame<8;frame++) {
     PresentationCamera previous=p.camera;presentation_step(&p,&state,map);
     assert(fabsf(p.camera.eye[0]-previous.eye[0])<.251f);
     assert(fabsf(p.camera.eye[2]-previous.eye[2])<.251f);
     float sx,head,feet;
     project_point(&p.camera,p.player.pose.x,2.4f,p.player.pose.z,&sx,&head);
     assert(sx>18&&sx<302&&head>16);
     project_point(&p.camera,p.player.pose.x,0,p.player.pose.z,&sx,&feet);
     assert(feet<228&&feet-head>entry_height*.70f);
    }
    assert(!memcmp(&state,&canonical,sizeof(state)));
   }
  }
 }
 /* The island retains its complete overview and fixed-eye visibility bake. */
 for(unsigned location=1;location<=12;location++) {
  const W3Map *map=&w3_maps[0];OwState state={.map=OVERWORLD_MAP,.overworld_selection=location};Presentation p;
  presentation_init(&p,&state,false);camera_settle(&p,&state,map);
  for(unsigned axis=0;axis<3;axis++) {
   close_to(p.camera.eye[axis],map->eye[axis]);close_to(p.camera.target[axis],map->target[axis]);
  }
 }
 printf("INTERIOR TRACKING PASS: all33 interiors, %u walkable compositions; entry view, continuous tile motion, readable far-room scale, TV-safe sides/feet, authored wall heights, dialogue priority/exit, fixed island overview and unchanged state\n",compositions);
}

static void test_cat_conversations(void)
{
 for(unsigned id=CAT_TUXEDO_NPC;id<=CAT_TABBY_NPC;id++) {
  unsigned x=id==CAT_TUXEDO_NPC?12:14;
  OwState state={.map=ISHIHARAS_HOUSE,.x=x,.y=10,.direction=NORTH,.npc_count=2,
    .npcs={{CAT_TUXEDO_NPC,12,8,SOUTH,1,0},{CAT_TABBY_NPC,14,8,SOUTH,1,0}}};
  OwState canonical=state;Presentation p;presentation_init(&p,&state,false);
  presentation_conversation(&p,id);camera_settle(&p,&state,&w3_maps[ISHIHARAS_HOUSE]);
  close_to(p.camera.fov,50);assert(p.conversation_npc==id);close_to(CAT_DUO_SCALE,.5f);
  close_to(p.npcs[0].pose.x,12.4f);close_to(p.npcs[1].pose.x,13.6f);
  for(unsigned actor=0;actor<2;actor++)for(unsigned corner=0;corner<8;corner++) {
   float scale=actor?CAT_DUO_SCALE:1;
   float actor_x=actor?p.npcs[id-CAT_TUXEDO_NPC].pose.x:x;
   float sx,sy;project_point(&p.camera,actor_x+(corner&1?.65f:-.65f)*scale,
     (corner&2?2.5f:1.2f)*scale,(actor?8:10)+(corner&4?.35f:-.35f)*scale,&sx,&sy);
   assert(sx>10&&sx<310&&sy>12&&sy<155);
  }
  assert(!memcmp(&state,&canonical,sizeof(state)));
 }
 puts("CAT CONVERSATION PASS: both half-height speakers and Mark clear the dialogue panel; state unchanged");
}
int main(void)
{
 test_conversation_cameras();
 test_cat_conversations();
 test_interior_tracking();
 /* Fractional source waits accumulate instead of doubling one-frame waits. */
 Presentation timing={0};assert(presentation_wait_frames(&timing,60)==30);
 assert(presentation_wait_frames(&timing,120)==60);
 assert(presentation_wait_frames(&timing,1)==0&&presentation_wait_frames(&timing,1)==1);
 /* Adjacent targets have no stationary frame or gait restart at tile edges.
    Stopping afterwards changes presentation alone, including the run lean. */
 for(unsigned running=0;running<2;running++) {
  OwState straight={.map=MASON_LABORATORY,.x=14,.y=26,.direction=NORTH};
  ow_set_event(&straight,EVENT_MASON_LAB_STATE,MASON_LAB_RECEIVED_STARTER_DECK);
  Presentation continuous;presentation_init(&continuous,&straight,true);
  PresentationPose previous=*presentation_player(&continuous);
  unsigned duration=running?4:8;float speed=running?.5f:.25f;
  for(unsigned tile=0;tile<4;tile++) {
   straight.y-=2;OwState canonical=straight;
   presentation_sync(&continuous,&straight,running!=0);
   for(unsigned frame=0;frame<duration;frame++) {
    presentation_step(&continuous,&straight,&lab);
    const PresentationPose *now=presentation_player(&continuous);
    close_to(previous.z-now->z,speed);close_to(now->x,14);
    float phase=previous.gait+speed*3.14159265358979323846f;
    close_to(sinf(now->gait),sinf(phase));close_to(cosf(now->gait),cosf(phase));
    if(running)close_to(now->lean,6);
    previous=*now;
   }
   assert(!presentation_moving(&continuous)&&!memcmp(&straight,&canonical,sizeof(straight)));
  }
  OwState canonical=straight;presentation_step(&continuous,&straight,&lab);
  const PresentationPose *stopped=presentation_player(&continuous);
  close_to(stopped->x,previous.x);close_to(stopped->z,previous.z);
  close_to(stopped->gait,0);close_to(stopped->lean,0);
  assert(!presentation_moving(&continuous)&&!memcmp(&straight,&canonical,sizeof(straight)));
 }
 /* Breathing is rendering state: it stays tiny, has distinct NPC phases,
    keeps props fixed, and never makes script waits think an actor is walking. */
 OwState idle={.map=1,.x=14,.y=26,.npc_count=3,
  .npcs={{NPC_SAM,10,8,SOUTH,1,0},{NPC_DRMASON,14,6,SOUTH,1,0},{NPC_TORCH,2,2,SOUTH,1,0}}};
 OwState idle_before=idle;Presentation resting;presentation_init(&resting,&idle,false);
 bool distinct=false,changed_bob=false;float previous_bob=0;
 for(unsigned i=0;i<90;i++) {
  presentation_step(&resting,&idle,&lab);
  const PresentationPose *sam=presentation_npc(&resting,NPC_SAM);
  const PresentationPose *mason=presentation_npc(&resting,NPC_DRMASON);
  assert(fabsf(sam->bob)<=.00801f&&fabsf(mason->bob)<=.00801f);
  assert(sam->gait==0&&sam->lean==0&&!presentation_moving(&resting));
  close_to(presentation_npc(&resting,NPC_TORCH)->bob,0);
  if(fabsf(sam->bob-mason->bob)>.001f)distinct=true;
  if(i&&fabsf(sam->bob-previous_bob)>.00001f)changed_bob=true;
  previous_bob=sam->bob;
 }
 assert(distinct&&changed_bob&&!memcmp(&idle,&idle_before,sizeof(idle)));
 /* The exact original routes finish at every destination without snapping
    long segments or moving the player merely because the cursor changed. */
 for(unsigned from=1;from<=12;from++)for(unsigned to=1;to<=12;to++) {
  OwState s={.map=0,.overworld_selection=from};Presentation p;
  presentation_init(&p,&s,false);float x=p.world.pose.x,z=p.world.pose.z;
  s.overworld_selection=to;presentation_sync(&p,&s,false);
  close_to(p.world.pose.x,x);close_to(p.world.pose.z,z);
  close_to(presentation_world_cursor(&p)->x,ow_location_positions[to][0]/8.0f+1);
  close_to(presentation_world_cursor(&p)->z,ow_location_positions[to][1]/8.0f+1);
  bool travel=presentation_world_travel(&p,to);assert(travel==(from!=to));
  if(travel){close_to(p.world.pose.x,x);close_to(p.world.pose.z,z);}
  unsigned n=0;while(presentation_moving(&p)){assert(++n<2000);presentation_step(&p,&s,&island);}
  close_to(p.world.pose.x,ow_location_positions[to][0]/8.0f+1);
  close_to(p.world.pose.z,ow_location_positions[to][1]/8.0f+1);
 }
 /* NPC identity survives array reordering, and scripts render their movement
    through the same state used for normal input. */
 OwState reorder={.map=1,.x=14,.y=26,.npc_count=2,
  .npcs={{NPC_SAM,10,8,SOUTH,1,0},{NPC_DRMASON,14,6,SOUTH,1,0}}};
 Presentation p;presentation_init(&p,&reorder,false);
 OwNpc tmp=reorder.npcs[0];reorder.npcs[0]=reorder.npcs[1];reorder.npcs[1]=tmp;
 reorder.npcs[1].x=12;presentation_sync(&p,&reorder,false);
 close_to(presentation_npc(&p,NPC_SAM)->x,10);
 for(unsigned i=0;i<8;i++)presentation_step(&p,&reorder,&lab);
 close_to(presentation_npc(&p,NPC_SAM)->x,12);
 /* Amy's lying/standing animation rests on the bench; beside it she returns
    to the floor. Elevation belongs only to presentation, never the save. */
 OwState amy_state={.map=WATER_CLUB,.x=20,.y=6,.npc_count=1,
  .npcs={{NPC_AMY,22,4,WEST,1,0}}};
 amy_state.npc_animation[0]=SPRITE_ANIM_CGB_AMY_LAYING;
 Presentation amy_view;presentation_init(&amy_view,&amy_state,false);
 close_to(presentation_npc(&amy_view,NPC_AMY)->floor,.66f);
 amy_state.npc_animation[0]=SPRITE_ANIM_CGB_AMY_STAND;
 presentation_sync(&amy_view,&amy_state,false);
 close_to(presentation_npc(&amy_view,NPC_AMY)->floor,.71f);
 amy_state.npcs[0].x=20;amy_state.npc_animation[0]=SPRITE_ANIM_BLUE_NPC_UP;
 OwState amy_canonical=amy_state;
 presentation_sync(&amy_view,&amy_state,false);
 close_to(presentation_npc(&amy_view,NPC_AMY)->floor,0);
 assert(!memcmp(&amy_state,&amy_canonical,sizeof(amy_state)));
 OwState honor_state={.map=HALL_OF_HONOR,.x=10,.y=14,.npc_count=1,
  .npcs={{NPC_LEGENDARY_CARD_BOTTOM_LEFT,10,10,SOUTH,1,0}}};
 Presentation honor_view;presentation_init(&honor_view,&honor_state,false);
 close_to(presentation_npc(&honor_view,NPC_LEGENDARY_CARD_BOTTOM_LEFT)->floor,2.2f);
 /* Real source scripts: Dome -> island path -> nine fast steps to Mason ->
    Sam/Mason choreography -> table positions before the practice duel. */
 OwState s;memset(&view,0,sizeof(view));view.cinematic=true;
 OwHost host={.text=text,.choice=choice,.duel=duel,.feature=feature,
              .changed=changed,.wait=wait_frames,.error=error};
 if(!setjmp(at_duel)){ow_new_game(&s,&host,123);assert(!"practice duel was not reached");}
 /* Manual island selection and ordinary exploration retain their speed even
    when the cinematic camera is enabled and the tutorial is unfinished. */
 OwState normal={.map=0,.overworld_selection=OWMAP_POKEMON_DOME};Presentation normal_view;
 presentation_init(&normal_view,&normal,true);
 assert(presentation_world_travel(&normal_view,OWMAP_MASON_LABORATORY));
 assert(advance(&normal_view,&normal,&island)==route_frames(OWMAP_POKEMON_DOME,OWMAP_MASON_LABORATORY));
 normal=(OwState){.map=MASON_LABORATORY,.x=14,.y=26,.direction=NORTH};
 presentation_init(&normal_view,&normal,true);normal.y--;
 presentation_sync(&normal_view,&normal,false);assert(advance(&normal_view,&normal,&lab)==8);
 normal.y--;presentation_sync(&normal_view,&normal,true);assert(advance(&normal_view,&normal,&lab)==4);
 /* Short tutorial NPC steps share fractional duration by identity, while
    identical post-tutorial choreography remains eight frames per tile. */
 normal.npc_count=1;normal.npcs[0]=(OwNpc){NPC_SAM,4,14,EAST,1,0};
 presentation_sync(&normal_view,&normal,false);unsigned npc_total=0;
 for(unsigned i=1;i<=7;i++) {
  normal.npcs[0].x++;presentation_sync(&normal_view,&normal,false);
  npc_total+=advance(&normal_view,&normal,&lab);assert(npc_total==i*80/7);
 }
 assert(npc_total==80);
 ow_set_event(&normal,EVENT_MASON_LAB_STATE,MASON_LAB_RECEIVED_STARTER_DECK);
 normal.npcs[0].x++;presentation_sync(&normal_view,&normal,false);
 assert(advance(&normal_view,&normal,&lab)==8);
 normal.script_pc=tutorial_run_pc;normal.y--;
 presentation_sync(&normal_view,&normal,false);assert(advance(&normal_view,&normal,&lab)==4);
 printf("PRESENTATION PASS: all144 routes; tutorial island %u frames; Mason run51/walk57 frames; fractional NPC timing; ordinary speeds; canonical source waits/positions\n",world_frames);
 return 0;
}
