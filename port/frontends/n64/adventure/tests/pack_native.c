#include "pack_native.h"
#include "../game.h"
#include "../audio.h"
#include "../duel_bridge.h"
#include "../video.h"
#include "gen/constants.h"
#include <string.h>

#if ADVENTURE_DIAGNOSTIC != 24
#error "Pack integration fixture is diagnostic24 only"
#endif

static volatile unsigned vi_count,detail_start;
static volatile uint16_t virtual_held;
static volatile bool in_detail;
static unsigned started,booster_id,case_index,calls,phase_mask,last_phase,phase_started;
static unsigned receipt_stage,check_stage,list_stage,action_started,details,opening_frames;
static unsigned shown[PACK_DONE+1];
static GpState expected;
static OwState expected_story;
static uint8_t expected_cards[10];
static double world_position,pc_position;

static void tick(void)
{
 vi_count++;
 if(in_detail) {
  unsigned age=vi_count-detail_start;
  virtual_held=age<6?INPUT_A:age<12?0:INPUT_B;
 }
 assertf(vi_count-started<60*60,"Pack integration exceeded60 seconds");
}

/* These symbols replace external SDK reads only in the diagnostic link.
   Both pack input and the unchanged duel card viewer keep their own VI
   capture, held-button suppression and fresh-edge consumption. */
bool __wrap_joypad_is_connected(joypad_port_t port)
{return port==JOYPAD_PORT_1;}
joypad_inputs_t __wrap_joypad_get_inputs(joypad_port_t port)
{
 joypad_inputs_t inputs={0};uint16_t held=port==JOYPAD_PORT_1?virtual_held:0;
 inputs.btn.a=(held&INPUT_A)!=0;inputs.btn.b=(held&INPUT_B)!=0;
 inputs.btn.d_up=(held&INPUT_UP)!=0;inputs.btn.d_down=(held&INPUT_DOWN)!=0;
 return inputs;
}

extern void __real_n64_card_detail(unsigned id);
void __wrap_n64_card_detail(unsigned id)
{
 assertf(!case_index&&list_stage==5&&!details&&id==expected_cards[0],"Wrong pack card inspected");
 disable_interrupts();detail_start=vi_count;in_detail=true;virtual_held=INPUT_A;enable_interrupts();
 __real_n64_card_detail(id);
 disable_interrupts();in_detail=false;virtual_held=0;enable_interrupts();
 assertf(vi_count-detail_start>=12&&n64_ui_frame(),"Card detail skipped its fresh input");
 details++;
}

static void state_unchanged(void)
{
 assertf(!memcmp(&game.cards,&expected,sizeof(expected)),"Pack grant/inventory/RNG changed twice");
 assertf(!memcmp(&game.ow,&expected_story,sizeof(expected_story)),"Pack presentation changed story state");
}

void adventure_pack_native_begin(unsigned booster,const PackFlow *flow)
{
 assertf(booster==booster_id&&flow->phase==PACK_SEALED&&
  !memcmp(flow->cards,expected_cards,10),"Pack wrapper changed generated order");
 state_unchanged();calls++;phase_mask=0;last_phase=PACK_DONE;
 receipt_stage=check_stage=list_stage=details=opening_frames=0;memset(shown,0,sizeof(shown));
 action_started=vi_count;virtual_held=INPUT_A;pc_position=mixer_ch_get_pos(4);
 assertf(adventure_audio_music_playing()&&mixer_ch_playing(4),"PC music not active before pack");
}

void adventure_pack_native_frame(const PackFlow *flow,const surface_t *display)
{
 assertf(display&&display->width==n64_video_width&&display->height==n64_video_height,
  "Pack did not render its owned native frame");
 assertf(flow->phase<=PACK_LIST&&!memcmp(flow->cards,expected_cards,10),"Pack phase/card order changed");
 assertf(mixer_ch_get_pos(0)==world_position,"Nested pack advanced world music");
 if(last_phase!=flow->phase) {
  debugf("PACK NATIVE phase=%u set=%u vi=%u\n",flow->phase,case_index,vi_count);
  last_phase=flow->phase;phase_started=vi_count;
 }
 phase_mask|=1u<<flow->phase;shown[flow->phase]++;
 unsigned age=vi_count-action_started;
 switch(flow->phase) {
 case PACK_SEALED:
  assertf(mixer_ch_get_pos(4)==pc_position,"Pack fanfare advanced paused PC music");
  if(receipt_stage<2)assertf(!flow->requested,"Held A acknowledged pack receipt");
  if(!receipt_stage&&age>=6){virtual_held=0;receipt_stage=1;action_started=vi_count;}
  else if(receipt_stage==1&&age>=6){virtual_held=INPUT_A;receipt_stage=2;}
  break;
 case PACK_CHECK:
  assertf(receipt_stage==2&&flow->requested&&mixer_ch_playing(4)&&!mixer_ch_playing(6),
   "Check page preceded receipt ACK/fanfare/PC restoration");
  if(!check_stage&&vi_count-phase_started>=8){virtual_held=0;check_stage=1;action_started=vi_count;}
  else if(check_stage==1&&age>=6){virtual_held=INPUT_A;check_stage=2;}
  break;
 case PACK_OPENING:
  assertf(check_stage==2&&shown[PACK_CHECK]>=4,"Held receipt A skipped checked-cards page");
  virtual_held=0;opening_frames++;
  break;
 case PACK_LIST:
  assertf(check_stage==2&&opening_frames>=PACK_OPEN_FRAMES-1,"Opening/list phase skipped");
  if(!list_stage&&vi_count-phase_started>=6){virtual_held=INPUT_DOWN;list_stage=1;}
  else if(list_stage==1&&flow->index==1){virtual_held=0;list_stage=2;action_started=vi_count;}
  else if(list_stage==2&&age>=6){virtual_held=INPUT_UP;list_stage=3;}
  else if(list_stage==3&&flow->index==0){virtual_held=0;list_stage=4;action_started=vi_count;}
  else if(list_stage==4&&age>=6){virtual_held=case_index?INPUT_B:INPUT_A;list_stage=case_index?7:5;}
  else if(list_stage==5&&details){virtual_held=0;list_stage=6;action_started=vi_count;}
  else if(list_stage==6&&age>=6){virtual_held=INPUT_B;list_stage=7;}
  break;
 default:assertf(false,"Unexpected pack phase");break;
 }
}

void adventure_pack_native_end(const PackFlow *flow)
{
 assertf(flow->phase==PACK_DONE&&phase_mask==15&&list_stage==7&&flow->index==0&&
  details==(!case_index?1u:0u),"Pack input phases/list/detail incomplete");
 assertf(mixer_ch_playing(4)&&adventure_audio_music_playing()&&
  mixer_ch_get_pos(4)>pc_position&&mixer_ch_get_pos(0)==world_position,
  "Nested PC music failed to resume");
 state_unchanged();virtual_held=0;
 debugf("PACK NATIVE CASE PASS booster=%u receipt_frames=%u check_frames=%u opening_frames=%u list_frames=%u detail=%u\n",
  booster_id,shown[0],shown[1],shown[2],shown[3],details);
}

static void wait_frames(unsigned count)
{
 while(count--) {
  surface_t *display=display_get();rdpq_attach(display,NULL);rdpq_mode_push();
  rdpq_set_mode_fill(RGBA32(20,28,38,255));rdpq_fill_rectangle(0,0,n64_video_width,n64_video_height);
  rdpq_mode_pop();rdpq_detach_wait();n64_video_present(display);adventure_audio_poll();
 }
}

void adventure_pack_native_regression(void)
{
 static const unsigned boosters[]={0,7,14,20};const uint8_t seed[]={0x57,0x31,0x29};
 memset(&game,0,sizeof(game));strcpy(game.name,"MARK");gp_init(&game.cards,seed);
 game.ow.map=MASON_LABORATORY;game.ow.x=14;game.ow.y=20;game.ow.rng=0x293157;
 vi_count=started=0;register_VI_handler(tick);
 adventure_audio_event(0,MUSIC_OVERWORLD);wait_frames(6);
 adventure_audio_event(0x4a,0);adventure_audio_event(0,MUSIC_PC_MAIN_MENU);wait_frames(6);
 world_position=mixer_ch_get_pos(0);
 for(case_index=0;case_index<4;case_index++) {
  booster_id=boosters[case_index];expected=game.cards;
  for(unsigned i=0;i<3;i++)expected.rng[i]=game.ow.rng>>(8*i);
  assertf(gp_open_booster(&expected,booster_id,expected_cards),"Expected pack generation failed");
  expected_story=game.ow;expected_story.rng=(unsigned)expected.rng[0]|
   (unsigned)expected.rng[1]<<8|(unsigned)expected.rng[2]<<16;
  game_show_booster(booster_id);state_unchanged();
 }
 assertf(calls==4&&gp_total_cards(&game.cards)==40,"Pack wrapper did not grant exactly40 cards");
 adventure_audio_event(0x4b,0);
 assertf(mixer_ch_playing(0)&&mixer_ch_get_pos(0)==world_position,"PC failed to restore world position");
 wait_frames(6);assertf(mixer_ch_get_pos(0)>world_position,"World playback remained frozen");
 adventure_audio_event(0,0);adventure_audio_event(1,0);
 unregister_VI_handler(tick);
 heap_stats_t heap;sys_get_heap_stats(&heap);
 debugf("PACK NATIVE PASS sets=4 cards=40 grants=4 frames=%u ram=%d width=%u free_heap=%d\n",
  vi_count,get_memory_size(),n64_video_width,heap.total-heap.used);
}
