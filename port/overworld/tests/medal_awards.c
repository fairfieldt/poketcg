/* The real reward scripts must send original event IDs, with ownership
 * already set, before subsequent dialogue/packs. No renderer or save files. */
#include "../overworld.h"
#include "../gen/data.h"
#include "../../src/gen/constants.h"
#include "../../src/gen/text.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  unsigned event, map, npc, master, booster;
  const char *script;
} Award;
static const Award awards[] = {
    {EVENT_BEAT_NIKKI, GRASS_CLUB, NPC_NIKKI, 5, BOOSTER_LABORATORY_NEUTRAL, "Script_BeatNikki"},
    {EVENT_BEAT_RICK, SCIENCE_CLUB, NPC_RICK, 7, BOOSTER_LABORATORY_GRASS, "Script_BeatRick"},
    {EVENT_BEAT_KEN, FIRE_CLUB, NPC_KEN, 8, BOOSTER_MYSTERY_NEUTRAL, "Script_BeatKen"},
    {EVENT_BEAT_AMY, WATER_CLUB, NPC_AMY, 3, BOOSTER_LABORATORY_WATER, "Script_BeatAmy"},
    {EVENT_BEAT_ISAAC, LIGHTNING_CLUB, NPC_ISAAC, 4, BOOSTER_MYSTERY_LIGHTNING_COLORLESS, "Script_BeatIsaac"},
    {EVENT_BEAT_MURRAY, PSYCHIC_CLUB, NPC_MURRAY1, 6, BOOSTER_LABORATORY_PSYCHIC, "Script_BeatMurray"},
    {EVENT_BEAT_GENE, ROCK_CLUB, NPC_GENE, 2, BOOSTER_MYSTERY_FIGHTING_COLORLESS, "Script_BeatGene"},
    {EVENT_BEAT_MITCH, FIGHTING_CLUB, NPC_MITCH, 1, BOOSTER_LABORATORY_NEUTRAL, "Script_BeatMitch"},
};
typedef struct {
  const Award *award;
  unsigned medals, boosters, expected_mask, expected_count, expected_master_slot;
  unsigned mail[16], mail_count, sequence[32], sequence_count;
  bool expect_amy_after;
} Test;
enum { MEDAL_TOKEN=0x10000, BOOSTER_TOKEN=0x20000 };
static void add(Test *t,unsigned token)
{assert(t->sequence_count<32);t->sequence[t->sequence_count++]=token;}
static void text(void *ctx,OwState *s,uint16_t id,uint8_t npc)
{
  (void)npc;Test *t=ctx;add(t,id);
  if(t->expect_amy_after&&id==AmyPlayerWon1Text) {
    const OwNpc *amy=NULL;
    for(unsigned i=0;i<s->npc_count;i++)if(s->npcs[i].id==NPC_AMY)amy=&s->npcs[i];
    assert(amy&&amy->x==20&&amy->y==4&&!s->native_vars[0]);
  }
}
static bool feature(void *ctx,OwState *s,unsigned kind,unsigned arg)
{
  Test *t=ctx;
  if(kind==OW_FEATURE_MEDAL) {
    assert(arg==t->award->event&&arg>=8&&arg<=15);
    assert(ow_event(s,arg)==1);
    assert(ow_event(s,EVENT_MEDAL_FLAGS)==t->expected_mask);
    assert(ow_event(s,EVENT_MEDAL_COUNT)==t->expected_count);
    const unsigned *mail=t->mail;
    if(t->expected_count>=8)assert(*mail++==12);
    if(t->expected_count>=7)assert(*mail++==11);
    if(t->expected_count>=3)assert(*mail++==10);
    assert((unsigned)(mail-t->mail)==t->mail_count); /* milestone mail precedes ceremony */
    assert(!s->masters[t->expected_master_slot]); /* record_master_win follows */
    t->medals++;add(t,MEDAL_TOKEN+arg);return true;
  }
  if(kind==OW_FEATURE_PC_PACK) {
    assert(t->mail_count<16&&arg>=10&&arg<=12);
    t->mail[t->mail_count++]=arg;return true;
  }
  if(kind==OW_FEATURE_MAP_PATCH)return true;
  fprintf(stderr,"Unexpected medal feature %u arg %u\n",kind,arg);assert(false);return false;
}
static void booster(void *ctx,unsigned id)
{Test *t=ctx;assert(id==t->award->booster);t->boosters++;add(t,BOOSTER_TOKEN+id);}
static OwHost host(Test *t)
{return (OwHost){.ctx=t,.text=text,.feature=feature,.booster=booster};}
static void setup(OwState *s,Test *t,unsigned index)
{
  memset(s,0,sizeof(*s));memset(t,0,sizeof(*t));t->award=&awards[index];
  s->version=OW_SAVE_VERSION;s->rng=123;s->map=(uint8_t)t->award->map;
  s->x=8;s->y=6;s->direction=NORTH;
  ow_set_event(s,EVENT_MASON_LAB_STATE,MASON_LAB_RECEIVED_STARTER_DECK);
  OwHost h=host(t);ow_load_map(s,&h,s->map,s->x,s->y,s->direction);
  assert(!s->error[0]);t->sequence_count=0;t->expected_mask=0x80u>>index;t->expected_count=1;
}
static void check_save(const OwState *s,unsigned mask,unsigned count)
{
  uint8_t bytes[2048];OwState restored;
  assert(ow_save_size()<=sizeof(bytes)&&ow_save_encode(s,bytes,sizeof(bytes)));
  assert(ow_save_decode(&restored,bytes,ow_save_size()));
  assert(ow_event(&restored,EVENT_MEDAL_FLAGS)==mask&&ow_event(&restored,EVENT_MEDAL_COUNT)==count);
  assert(!memcmp(s->masters,restored.masters,sizeof(s->masters))&&s->rng==restored.rng);
}
static void isolated_awards(void)
{
  for(unsigned i=0;i<8;i++) {
    OwState s;Test t;setup(&s,&t,i);OwHost h=host(&t);
    ow_run(&s,&h,ow_script_address(t.award->script),t.award->npc);
    assert(!s.error[0]&&t.medals==1&&t.boosters==2&&!t.mail_count);
    assert(s.masters[0]==t.award->master);check_save(&s,t.expected_mask,1);
    unsigned found=0;
    for(unsigned j=0;j<t.sequence_count;j++)if(t.sequence[j]==MEDAL_TOKEN+t.award->event)found=j;
    assert(t.sequence[found+1]<MEDAL_TOKEN); /* award then follow-up speech */
    t.sequence_count=0;t.boosters=0;
    ow_run(&s,&h,ow_script_address(t.award->script),t.award->npc);
    assert(!s.error[0]&&t.medals==1&&t.boosters==2); /* no second medal ceremony */
    assert(ow_event(&s,EVENT_MEDAL_FLAGS)==t.expected_mask&&ow_event(&s,EVENT_MEDAL_COUNT)==1);
    assert(s.masters[0]==t.award->master&&!s.masters[1]);
  }
  puts("MEDAL SCRIPTS PASS all8 event IDs, owned flags, reward order, rematches, save roundtrip");
}
static void cumulative_awards(void)
{
  OwState s;Test t;setup(&s,&t,0);unsigned mask=0;
  for(unsigned i=0;i<8;i++) {
    t.award=&awards[i];t.expected_mask=mask|=0x80u>>i;t.expected_count=i+1;t.expected_master_slot=i;
    t.mail_count=t.sequence_count=0;OwHost h=host(&t);
    ow_load_map(&s,&h,t.award->map,8,6,NORTH);
    t.mail_count=t.sequence_count=0; /* isolate the reward script from map preloads */
    ow_run(&s,&h,ow_script_address(t.award->script),t.award->npc);
    assert(!s.error[0]&&t.medals==i+1&&t.boosters==(i+1)*2);
    assert(s.masters[i]==t.award->master&&ow_event(&s,EVENT_MEDAL_FLAGS)==mask);
    const unsigned *p=t.mail;
    if(i+1>=8)assert(*p++==12);
    if(i+1>=7)assert(*p++==11);
    if(i+1>=3)assert(*p++==10);
    assert((unsigned)(p-t.mail)==t.mail_count);check_save(&s,mask,i+1);
  }
  puts("MEDAL PROGRESSION PASS all8 bits/counts, 3/7/8 milestone mail order, master order");
}
static void actual_amy_after_duel(void)
{
  OwState s;Test t;setup(&s,&t,3);OwHost h=host(&t);t.expect_amy_after=true;
  s.x=20;s.y=6; /* canonical position after Joshua wakes Amy */
  s.duel_npc=NPC_AMY;s.duel_pending=1;ow_finish_duel(&s,&h,1);
  const unsigned expected[]={AmyPlayerWon1Text,AmyGivesMedal1Text,MEDAL_TOKEN+EVENT_BEAT_AMY,
    AmyGivesMedal2Text,BOOSTER_TOKEN+BOOSTER_LABORATORY_WATER,BOOSTER_TOKEN+BOOSTER_LABORATORY_WATER,AmyPlayerWon2Text};
  assert(!s.error[0]&&!s.duel_pending&&t.medals==1&&t.boosters==2);
  assert(t.sequence_count==sizeof(expected)/sizeof(*expected)&&!memcmp(t.sequence,expected,sizeof(expected)));
  assert(ow_event(&s,EVENT_BEAT_AMY)&&ow_event(&s,EVENT_MEDAL_FLAGS)==WATER_MEDAL);
  check_save(&s,WATER_MEDAL,1);
  puts("AMY AFTER DUEL PASS real preload/handler -> award -> follow-up -> two Water boosters");
}
int main(void)
{isolated_awards();cumulative_awards();actual_amy_after_duel();return 0;}
