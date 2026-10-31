#include <libdragon.h>
#include <string.h>
#include "../audio.h"
#include "../game.h"
#include "overworld.h"
#include "gameplay.h"
#include "gen/constants.h"
#include "gen/data.h"
#include "generated/world3d_maps.h"

/* Run the original tutorial VM through ow_finish_duel and its complete reward
   script. Only dialogue acknowledgements and the duel outcome are automated;
   the sound opcodes, starter inventory and save serialization are real. */
typedef struct {
 GpState cards;
 unsigned duels,starters,saves,phase;
 void (*before_fanfare)(void);
} Reward;
static void text(void *ctx,OwState *s,uint16_t id,uint8_t npc)
{
 (void)s;(void)npc;Reward *r=ctx;
 if(id==0x5f4){assertf(r->duels==1,"Reward ran before practice duel");r->before_fanfare();}
}
static int choice(void *ctx,OwState *s,unsigned kind,uint16_t id,unsigned count,unsigned initial)
{
 (void)ctx;(void)s;(void)id;(void)count;(void)initial;
 return kind==OW_CHOICE_SAM_RULES?7:kind==OW_CHOICE_YES_NO?1:0;
}
static int duel(void *ctx,OwState *s,const OwDuel *d)
{
 Reward *r=ctx;
 assertf(s->duel_pending&&d->npc==NPC_SAM&&d->deck==SAMS_PRACTICE_DECK_ID,
  "Reward fixture did not reach source practice duel");
 r->duels++;adventure_audio_event(0,d->music);return 1;
}
static bool feature(void *ctx,OwState *s,unsigned id,unsigned arg)
{(void)ctx;(void)s;(void)id;(void)arg;return true;}
static void sound(void *ctx,unsigned kind,unsigned id)
{
 Reward *r=ctx;
 if(kind==0x4a){assertf(r->phase==0,"Unexpected nested source pause");r->phase=1;}
 else if(kind==0&&id==MUSIC_BOOSTER_PACK){assertf(r->phase==1,"Fanfare before pause");r->phase=2;}
 else if(kind==0x4d){assertf(r->phase==2,"Song wait before fanfare");r->phase=3;}
 else if(kind==0x4b){assertf(r->phase==3,"Resume before song wait");r->phase=4;}
 adventure_audio_event(kind,id);
}
static void starter(void *ctx,unsigned choice)
{
 Reward *r=ctx;assertf(gp_give_starter(&r->cards,choice),"Could not grant starter deck");r->starters++;
}
static bool save(void *ctx,const OwState *s)
{
 Reward *r=ctx;uint8_t bytes[2048];OwState decoded;
 assertf(ow_save_encode(s,bytes,sizeof(bytes))&&ow_save_decode(&decoded,bytes,ow_save_size()),
  "Reward save did not round-trip");r->saves++;return true;
}
bool adventure_audio_reward_diagnostic(void (*before_fanfare)(void))
{
 Reward r={.before_fanfare=before_fanfare};OwState s;
 const uint8_t seed[]={53,117,0};gp_init(&r.cards,seed);
 OwHost h={.ctx=&r,.text=text,.choice=choice,.duel=duel,.feature=feature,
  .sound=sound,.starter=starter,.save=save};
 ow_new_game(&s,&h,0x7535);
 assertf(!s.error[0],"Source reward: %s",s.error);
 assertf(r.duels==1&&r.starters==1&&r.saves&&r.phase==4,
  "Source reward incomplete: duels%u starters%u saves%u phase%u",r.duels,r.starters,r.saves,r.phase);
 assertf(ow_event(&s,EVENT_MASON_LAB_STATE)==MASON_LAB_RECEIVED_STARTER_DECK&&
  gp_total_cards(&r.cards)>=60,"Source reward did not grant/save starter deck");
 debugf("AUDIO SOURCE REWARD PASS: practice duel, music09 pause/fanfare/wait/resume, %u cards, save\n",gp_total_cards(&r.cards));
 return true;
}

unsigned adventure_audio_memory_diagnostic(void)
{
 unsigned minimum=~0u;
 /* The production full-tour population warms every actor cache and includes
    the known worst-memory Dome/Hall scenes. Both music decoder samplebuffers
    and SFX have already been allocated by diagnostic17. */
 for(unsigned map=0;map<34;map++){
  memset(&game,0,sizeof(game));game.ow.map=map;
  game.ow.x=w3_maps[map].width/2;game.ow.y=w3_maps[map].height-4;
  for(const OwNpcDef *n=ow_map_npcs[map];n&&n->id;n++){
   if(n->id==255||n->x>=w3_maps[map].width||n->y>=w3_maps[map].height)continue;
   unsigned i=game.ow.npc_count++;assertf(i<OW_NPC_LIMIT,"Audio tour NPC overflow");
   game.ow.npcs[i]=(OwNpc){n->id,n->x,n->y,n->dir,1,ow_npc_headers[n->id].sprite};
   game.ow.npc_flags[i]=ow_npc_headers[n->id].flags;
   game.ow.npc_animation[i]=n->id==NPC_AMY?SPRITE_ANIM_CGB_AMY_LAYING:ow_npc_headers[n->id].animation;
  }
  game_draw();heap_stats_t stats;sys_get_heap_stats(&stats);
  unsigned available=stats.total-stats.used;if(available<minimum)minimum=available;
  debugf("AUDIO MEMORY map=%u free=%u\n",map,available);
 }
 debugf("AUDIO MEMORY REGRESSION PASS: 34 maps, both music slots and SFX allocated, minimum_free=%u\n",minimum);
 return minimum;
}
