#include <libdragon.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fps_sweep.h"
#include "../game.h"
#include "../audio.h"
#include "../video.h"
#include "gen/constants.h"
#include "gen/data.h"
#include "generated/world3d_maps.h"

enum { WARMUP=32,SAMPLES=96,STAGES=6 };
static const char *const stage_names[]={"cadence","work","draw","acquire","present","audio"};
static uint32_t observations[STAGES][SAMPLES];
static bool measuring;
static uint64_t acquire_ticks,present_ticks,audio_ticks;
static unsigned presents,audio_polls;
static unsigned cases;
static unsigned samples=SAMPLES;
static const char *extra_names[4];
static unsigned extra_count;
static uint64_t extra_ticks[4];
static uint32_t extra_observations[4][SAMPLES];
void fps_sweep_note(const char *name,uint64_t ticks)
{
 if(!measuring)return;
 unsigned i=0;for(;i<extra_count;i++)if(!strcmp(name,extra_names[i]))break;
 assertf(i<4,"Too many FPS substages");
 if(i==extra_count)extra_names[extra_count++]=name;
 extra_ticks[i]+=ticks;
}

/* These wrappers exist only in the diagnostic20 link. They observe the actual
   production draw functions, including ones which present/poll internally. */
extern surface_t *__real_display_get(void);
surface_t *__wrap_display_get(void)
{
 uint64_t start=get_ticks();surface_t *frame=__real_display_get();
 if(measuring)acquire_ticks+=get_ticks()-start;
 return frame;
}
extern void __real_n64_video_present(surface_t *);
void __wrap_n64_video_present(surface_t *frame)
{
 uint64_t start=get_ticks();__real_n64_video_present(frame);
 if(measuring){present_ticks+=get_ticks()-start;presents++;}
}
extern void __real_adventure_audio_poll(void);
void __wrap_adventure_audio_poll(void)
{
 uint64_t start=get_ticks();__real_adventure_audio_poll();
 if(measuring){audio_ticks+=get_ticks()-start;audio_polls++;}
}
static int compare(const void *a,const void *b)
{uint32_t x=*(const uint32_t *)a,y=*(const uint32_t *)b;return (x>y)-(x<y);}
static void stats(const char *name,uint32_t *values)
{
 uint64_t total=0;for(unsigned i=0;i<samples;i++)total+=values[i];
 qsort(values,samples,sizeof(*values),compare);
 debugf("\"%s\":{\"mean_us\":%u,\"median_us\":%u,\"p95_us\":%u,\"min_us\":%u,\"max_us\":%u}",
  name,(unsigned)((total+samples/2)/samples),(unsigned)((values[samples/2-1]+values[samples/2])/2),
  (unsigned)values[(samples*95+99)/100-1],(unsigned)values[0],(unsigned)values[samples-1]);
}
void fps_sweep_case(const char *name,FpsSweepDraw draw,void *ctx)
{
 assertf(!measuring,"Nested FPS case");
 extra_count=0;
 for(unsigned i=0;i<WARMUP+samples;i++){
  acquire_ticks=present_ticks=audio_ticks=0;presents=audio_polls=0;
  memset(extra_ticks,0,sizeof(extra_ticks));
  measuring=true;uint64_t start=get_ticks();surface_t *frame=draw(ctx,i);
  if(!presents)n64_video_present(frame);
  if(!audio_polls)adventure_audio_poll();
  uint64_t elapsed=get_ticks()-start;measuring=false;
  assertf(presents==1,"FPS callback presented%u frames in%s",presents,name);
  assertf(elapsed>=acquire_ticks+present_ticks+audio_ticks,"Overlapping FPS stage clocks");
  if(i<WARMUP)continue;
  unsigned sample=i-WARMUP;
  observations[0][sample]=TICKS_TO_US(elapsed);
  observations[1][sample]=TICKS_TO_US(elapsed-acquire_ticks);
  observations[2][sample]=TICKS_TO_US(elapsed-acquire_ticks-present_ticks-audio_ticks);
  observations[3][sample]=TICKS_TO_US(acquire_ticks);
  observations[4][sample]=TICKS_TO_US(present_ticks);
  observations[5][sample]=TICKS_TO_US(audio_ticks);
  for(unsigned e=0;e<extra_count;e++)extra_observations[e][sample]=TICKS_TO_US(extra_ticks[e]);
 }
 heap_stats_t heap;sys_get_heap_stats(&heap);
 debugf("FPS_CASE {\"case\":\"%s\",\"width\":%u,\"height\":%u,\"ram_mib\":%u,\"map\":%u,\"x\":%u,\"y\":%u,\"npc_count\":%u,\"warmup\":%u,\"samples\":%u,\"free_heap\":%u,",
  name,n64_video_width,n64_video_height,get_memory_size()/1048576,game.ow.map,game.ow.x,game.ow.y,
  game.ow.npc_count,WARMUP,samples,heap.total-heap.used);
 for(unsigned s=0;s<STAGES;s++){if(s)debugf(",");stats(stage_names[s],observations[s]);}
 for(unsigned e=0;e<extra_count;e++){debugf(",");stats(extra_names[e],extra_observations[e]);}
 debugf("}\n");cases++;
}
static surface_t *world(void *ctx,unsigned frame)
{(void)ctx;(void)frame;return game_render_frame();}
static void map_fixture(unsigned map)
{
 memset(&game,0,sizeof(game));strcpy(game.name,"MARK");game.text_speed=2;
 game.ow.map=map;game.ow.x=w3_maps[map].width/2;game.ow.y=w3_maps[map].height-4;
 game.ow.direction=NORTH;game.ow.overworld_selection=OWMAP_MASON_LABORATORY;
 ow_set_event(&game.ow,EVENT_MASON_LAB_STATE,MASON_LAB_RECEIVED_STARTER_DECK);
 for(const OwNpcDef *n=ow_map_npcs[map];n&&n->id;n++){
  if(n->id==255||n->x>=w3_maps[map].width||n->y>=w3_maps[map].height)continue;
  unsigned i=game.ow.npc_count++;assertf(i<OW_NPC_LIMIT,"FPS fixture NPC overflow");
  game.ow.npcs[i]=(OwNpc){n->id,n->x,n->y,n->dir,1,ow_npc_headers[n->id].sprite};
  game.ow.npc_flags[i]=ow_npc_headers[n->id].flags;
  game.ow.npc_animation[i]=n->id==NPC_AMY?SPRITE_ANIM_CGB_AMY_LAYING:ow_npc_headers[n->id].animation;
 }
 adventure_audio_event(0,ow_maps[map].song);
 game_conversation(0);
}
static jmp_buf reached_practice;
static OwState source_mason,source_sam;
static bool saw_mason,saw_sam;
static void source_text(void *ctx,OwState *s,uint16_t id,uint8_t npc)
{
 (void)ctx;(void)id;(void)npc;
 if(!saw_mason&&s->map==MASON_LABORATORY&&s->x==14&&s->y==8){source_mason=*s;saw_mason=true;}
}
static int source_choice(void *ctx,OwState *s,unsigned kind,uint16_t id,unsigned count,unsigned initial)
{(void)ctx;(void)s;(void)id;(void)count;(void)initial;return kind==OW_CHOICE_SAM_RULES?7:1;}
static bool source_feature(void *ctx,OwState *s,unsigned id,unsigned arg)
{(void)ctx;(void)s;(void)id;(void)arg;return true;}
static int source_duel(void *ctx,OwState *s,const OwDuel *d)
{
 (void)ctx;assertf(d->npc==NPC_SAM&&d->deck==SAMS_PRACTICE_DECK_ID,"FPS source fixture missed practice duel");
 source_sam=*s;saw_sam=true;longjmp(reached_practice,1);
}
static void opening_fixtures(void)
{
 OwHost host={.text=source_text,.choice=source_choice,.feature=source_feature,.duel=source_duel};
 memset(&game,0,sizeof(game));strcpy(game.name,"MARK");game.text_speed=2;
 if(!setjmp(reached_practice))ow_new_game(&game.ow,&host,123);
 assertf(saw_mason&&saw_sam&&!game.ow.error[0],"FPS source opening failed:%s",game.ow.error);
}
void adventure_fps_survey(void)
{
 bool focus=FPS_SWEEP_FOCUS!=0;samples=focus?32:SAMPLES;
 if(focus){
  if(FPS_SWEEP_FOCUS==2){
   debugf("FPS_NAMING_BEGIN samples=%u queue=wake-fixed\n",samples);
   adventure_naming_fps_survey();debugf("FPS_SURVEY_PASS cases=%u\n",cases);return;
  }
  extern bool adventure_dialogue_cache_diagnostic(void);
  debugf("FPS_FOCUS_BEGIN samples=%u queue=wake-fixed\n",samples);
  opening_fixtures();adventure_audio_event(0,MUSIC_OVERWORLD);
  game.ow=source_sam;game_conversation(NPC_SAM);
  assertf(adventure_dialogue_cache_diagnostic(),"Dialogue cache pixel regression failed");
  fps_sweep_case("source_sam_conversation_world",world,NULL);
  adventure_dialogue_fps_survey("source_sam_uncached");
  adventure_dialogue_fps_survey("source_sam_cached");
  adventure_naming_fps_survey();
  debugf("FPS_SURVEY_PASS cases=%u\n",cases);return;
 }
 debugf("FPS_SURVEY_BEGIN width=%u height=%u ram=%u target_fps=30 warmup=%u samples=%u timing=emulated_CP0_ticks\n",
  n64_video_width,n64_video_height,get_memory_size()/1048576,WARMUP,SAMPLES);
 display_set_fps_limit(30);adventure_audio_event(0,MUSIC_OVERWORLD);
 for(unsigned map=0;map<34;map++){
  char name[64];map_fixture(map);snprintf(name,sizeof(name),"map_%02u_entrance",map);
  fps_sweep_case(name,world,NULL);
 }
 static const unsigned center_maps[]={MASON_LABORATORY,WATER_CLUB,LIGHTNING_CLUB,GRASS_CLUB,POKEMON_DOME,HALL_OF_HONOR};
 for(unsigned n=0;n<sizeof(center_maps)/sizeof(*center_maps);n++){
  unsigned map=center_maps[n];char name[64];map_fixture(map);
  game.ow.y=w3_maps[map].height/2;snprintf(name,sizeof(name),"map_%02u_center",map);
  fps_sweep_case(name,world,NULL);
 }
 opening_fixtures();adventure_audio_event(0,MUSIC_OVERWORLD);
 game.ow=source_mason;game_conversation(NPC_DRMASON);
 fps_sweep_case("source_mason_conversation_world",world,NULL);
 adventure_dialogue_fps_survey("source_mason");
 game.ow=source_sam;game_conversation(0);fps_sweep_case("source_sam_table_exploration",world,NULL);
 game_conversation(NPC_SAM);fps_sweep_case("source_sam_table_conversation_world",world,NULL);
 adventure_dialogue_fps_survey("source_sam");
 map_fixture(LIGHTNING_CLUB_LOBBY);game.ow.x=14;game.ow.y=10;game.ow.direction=WEST;
 unsigned rival=game.ow.npc_count;
 for(unsigned i=0;i<game.ow.npc_count;i++)if(game.ow.npcs[i].id==NPC_RONALD1)rival=i;
 if(rival==game.ow.npc_count){assertf(rival<OW_NPC_LIMIT,"Rival FPS fixture NPC overflow");game.ow.npc_count++;}
 game.ow.npcs[rival]=(OwNpc){NPC_RONALD1,12,10,EAST,1,ow_npc_headers[NPC_RONALD1].sprite};
 game.ow.npc_flags[rival]=ow_npc_headers[NPC_RONALD1].flags;
 game.ow.npc_animation[rival]=ow_npc_headers[NPC_RONALD1].animation;
 game_conversation(NPC_RONALD1);fps_sweep_case("ronald_lobby_conversation_world",world,NULL);
 adventure_dialogue_fps_survey("ronald_lobby");game_conversation(0);
 adventure_intro_fps_survey();adventure_naming_fps_survey();
 debugf("FPS_SURVEY_PASS cases=%u\n",cases);
}
