#include "../presentation.h"
#include "../../../../overworld/gen/data.h"
#include "../../../../src/gen/constants.h"
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

int main(void)
{
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
