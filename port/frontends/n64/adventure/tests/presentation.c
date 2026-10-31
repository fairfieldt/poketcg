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
static unsigned run_commands,run_frames,walk_commands,wait_source,wait_video,first_map;
static bool initial_map_seen,world_walk_seen;
static jmp_buf at_duel;
static void close_to(float x,float expected) {assert(fabsf(x-expected)<.001f);}
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
  assert(settle()>0);world_walk_seen=true;
  close_to(presentation_world(&view)->x,ow_location_positions[1][0]/8.0f+1);
  close_to(presentation_world(&view)->z,ow_location_positions[1][1]/8.0f+1);
 }
 if((flags&PRESENTATION_PLAYER)&&presentation_scripted_player(s)) {
  unsigned count=settle();
  if(s->x==14&&s->y>=8&&!walk_commands){assert(count==4);run_commands++;run_frames+=count;}
  else {assert(count==8);walk_commands++;}
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
  assert(run_commands==9&&run_frames==36);
  float distance=presentation_camera(&view)->eye[2]-presentation_player(&view)->z;
  assert(distance>10.4f&&distance<13);
 }
}
static int choice(void *ctx,OwState *s,unsigned kind,uint16_t id,unsigned count,unsigned initial)
{(void)ctx;(void)s;(void)id;(void)count;(void)initial;return kind==OW_CHOICE_SAM_RULES?7:1;}
static int duel(void *ctx,OwState *s,const OwDuel *d)
{
 (void)ctx;current=s;settle();assert(d->npc==NPC_SAM&&d->deck==SAMS_PRACTICE_DECK_ID);
 assert(run_commands==9&&walk_commands==5&&world_walk_seen);
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
 /* Real source scripts: Dome -> island path -> nine fast steps to Mason ->
    Sam/Mason choreography -> table positions before the practice duel. */
 OwState s;memset(&view,0,sizeof(view));view.cinematic=true;
 OwHost host={.text=text,.choice=choice,.duel=duel,.feature=feature,
              .changed=changed,.wait=wait_frames,.error=error};
 if(!setjmp(at_duel)){ow_new_game(&s,&host,123);assert(!"practice duel was not reached");}
 puts("PRESENTATION PASS: all144 island routes; stable NPC identities; 60Hz timing; nine run steps to Mason; Sam table choreography");
 return 0;
}
