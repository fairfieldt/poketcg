#include <libdragon.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pack.h"
#include "pack_flow.h"
#include "audio.h"
#include "video.h"
#include "duel_bridge.h"
#include "gameplay.h"
#include "gen/constants.h"
#include "gen/text.h"
#include "cardart_table.h"
#include "../card_back.h"
#if ADVENTURE_DIAGNOSTIC == 24
#include "tests/pack_native.h"
#endif

/* This module owns presentation only. Card IDs arrive after the existing
   GenerateBoosterPack port has granted them, in its original order. */
typedef struct {sprite_t *pack,*cards[10];surface_t canvas,back;} PackAssets;
static volatile DuelInputCapture input;

static void input_sample(void)
{
 joypad_poll();joypad_inputs_t state=joypad_get_inputs(JOYPAD_PORT_1);joypad_buttons_t b=state.btn;
 uint16_t held=(b.a?INPUT_A:0)|(b.b?INPUT_B:0)|(b.start?INPUT_START:0)|
  (b.d_up||state.stick_y>40?INPUT_UP:0)|(b.d_down||state.stick_y< -40?INPUT_DOWN:0);
 duel_input_sample(&input,0,held,joypad_is_connected(JOYPAD_PORT_1));
}
static void input_begin(void)
{
 disable_interrupts();input=(DuelInputCapture){0};input_sample();input.pending[0]=0;
 register_VI_handler(input_sample);enable_interrupts();
}
static void input_end(void)
{disable_interrupts();unregister_VI_handler(input_sample);input=(DuelInputCapture){0};enable_interrupts();}
static uint16_t input_take(void)
{disable_interrupts();uint16_t pressed=duel_input_take(&input,0);enable_interrupts();return pressed;}

static uint32_t color(unsigned r,unsigned g,unsigned b){return graphics_make_color(r,g,b,255);}
static void box(surface_t *canvas,int x,int y,int width,int height,uint32_t value)
{n64_video_box(canvas,x,y,width,height,value);}
static void text(surface_t *canvas,int x,int y,uint32_t value,const char *str,int font,int width)
{n64_ui_draw_text(canvas,x,y,value,str,font,width);}
static void border(surface_t *canvas,int x,int y,int width,int height,uint32_t value)
{
 box(canvas,x,y,width,1,value);box(canvas,x,y+height-1,width,1,value);
 box(canvas,x,y,1,height,value);box(canvas,x+width-1,y,1,height,value);
}
static void lines(surface_t *canvas,int x,int y,const char *str,unsigned columns)
{
 char wrapped[12][37];int count=n64_ui_wrap_columns(str,columns,wrapped,12);
 for(int row=0;row<count;row++)text(canvas,x,y+row*11,color(24,48,80),wrapped[row],N64_UI_FONT_COMPACT,(int)columns*8);
}
static const char *set_name(unsigned booster)
{
 static const uint16_t names[]={ColosseumBoosterText,EvolutionBoosterText,MysteryBoosterText,LaboratoryBoosterText};
 return tcg_text(names[pack_set(booster)]);
}
static sprite_t *card_open(unsigned id)
{
 const uint8_t *card=tcg_card_data((uint8_t)id);unsigned gfx=card[CARD_DATA_GFX]|(unsigned)card[CARD_DATA_GFX+1]<<8;
 for(unsigned i=0;i<sizeof(kCardArt)/sizeof(*kCardArt);i++)if(kCardArt[i].gfx==gfx) {
  char path[96];snprintf(path,sizeof(path),"rom:/cards/%s.sprite",kCardArt[i].name);return sprite_load(path);
 }
 return NULL;
}
static PackAssets assets_open(unsigned booster,const uint8_t cards[10])
{
 static const char *const names[]={"colosseum","evolution","mystery","laboratory"};
 PackAssets assets={0};char path[96];snprintf(path,sizeof(path),"rom:/packs/%s.sprite",names[pack_set(booster)]);
 assets.pack=sprite_load(path);assets.canvas=n64_video_surface();assets.back=surface_alloc(FMT_RGBA16,32,44);
 assert(assets.pack&&assets.canvas.buffer&&assets.back.buffer);
 surface_t back=assets.back;back.buffer=CachedAddr(back.buffer);
 for(unsigned y=0;y<44;y++)memcpy((uint8_t *)back.buffer+y*back.stride,card_back_standard+y*32,64);
 data_cache_hit_writeback_invalidate(back.buffer,(unsigned long)back.stride*back.height);
 for(unsigned i=0;i<10;i++){assets.cards[i]=card_open(cards[i]);assert(assets.cards[i]);}
#if ADVENTURE_DIAGNOSTIC
 heap_stats_t heap;sys_get_heap_stats(&heap);
 debugf("PACK HEAP width=%u used=%d free=%d\n",n64_video_width,heap.used,heap.total-heap.used);
#endif
 return assets;
}
static void assets_close(PackAssets *assets)
{
 rspq_wait();sprite_free(assets->pack);for(unsigned i=0;i<10;i++)sprite_free(assets->cards[i]);
 surface_free(&assets->canvas);surface_free(&assets->back);
}
static void rarity(surface_t *canvas,int x,int y,unsigned card,uint32_t value)
{
 unsigned r=gp_cards[card].rarity;
 if(r==DIAMOND)for(int i=-3;i<=3;i++)box(canvas,x-(3-abs(i)),y+i,1+2*(3-abs(i)),1,value);
 else if(r==STAR||r==PROMOSTAR) {
  box(canvas,x,y-4,1,9,value);box(canvas,x-4,y,9,1,value);
  for(int i=-2;i<=2;i++){box(canvas,x+i,y+i,1,1,value);box(canvas,x-i,y+i,1,1,value);}
 } else {
  box(canvas,x-2,y-2,5,5,value);box(canvas,x-3,y-1,7,3,value);
 }
}
static void compose(PackAssets *assets,const PackFlow *flow,unsigned booster,const char *recipient)
{
 surface_t canvas=assets->canvas;canvas.buffer=CachedAddr(canvas.buffer);
 uint32_t paper=color(247,247,222),ink=color(24,48,80),edge=color(74,115,156),shade=color(185,199,199),selected=color(32,72,120),white=color(255,255,247);
 graphics_fill_screen(&canvas,color(20,28,38));box(&canvas,8,8,304,224,paper);
 border(&canvas,8,8,304,224,edge);border(&canvas,9,9,302,222,edge);
 box(&canvas,16,36,288,1,shade);box(&canvas,16,207,288,1,shade);
 text(&canvas,24,16,ink,"Booster Pack",N64_UI_FONT_HEADING,272);
 if(flow->phase==PACK_LIST) {
  for(unsigned i=0;i<10;i++) {
   int y=47+(int)i*15;bool current=i==flow->index;uint32_t foreground=current?white:ink;
   if(current)box(&canvas,18,y-2,191,14,selected);
   char number[8],name[48];snprintf(number,sizeof(number),"%u",i+1);n64_card_name(flow->cards[i],name,sizeof(name));
   text(&canvas,23,y,foreground,number,N64_UI_FONT_BODY,20);text(&canvas,43,y,foreground,name,N64_UI_FONT_BODY,160);
  }
  unsigned id=flow->cards[flow->index];const uint8_t *card=tcg_card_data((uint8_t)id);char label[32];
  border(&canvas,218,60,86,126,edge);snprintf(label,sizeof(label),"%u/10",flow->index+1);
  text(&canvas,233,65,ink,label,N64_UI_FONT_BODY,64);rarity(&canvas,293,71,id,ink);
  if(card[CARD_DATA_TYPE]<TYPE_ENERGY) {
   snprintf(label,sizeof(label),"HP %u",card[CARD_DATA_HP]);text(&canvas,230,140,ink,label,N64_UI_FONT_BODY,68);
   snprintf(label,sizeof(label),"Lv %u",card[CARD_DATA_LEVEL]);text(&canvas,230,156,ink,label,N64_UI_FONT_BODY,68);
  }
  text(&canvas,24,216,ink,"A/START: card   B: close",N64_UI_FONT_COMPACT,272);
 } else {
  char receipt[256];
  if(flow->phase==PACK_SEALED)snprintf(receipt,sizeof(receipt),"%s received a Booster Pack: %s.",recipient?recipient:"",set_name(booster));
  else snprintf(receipt,sizeof(receipt),"%s checked the cards in the Booster Pack!",recipient?recipient:"");
  lines(&canvas,152,61,receipt,18);
  text(&canvas,24,216,ink,flow->phase==PACK_SEALED?"A/B: continue":"A: open",N64_UI_FONT_COMPACT,272);
 }
 data_cache_hit_writeback_invalidate(canvas.buffer,(unsigned long)canvas.stride*canvas.height);
 rdpq_attach(&assets->canvas,NULL);rdpq_mode_push();rdpq_set_mode_standard();rdpq_mode_filter(FILTER_POINT);rdpq_mode_alphacompare(1);
 if(flow->phase==PACK_LIST)rdpq_sprite_blit(assets->cards[flow->index],n64_video_x(229),n64_video_y(85),
  &(rdpq_blitparms_t){.scale_x=n64_video_scale,.scale_y=n64_video_scale});
 else {
  rdpq_sprite_blit(assets->pack,n64_video_x(48),n64_video_y(56),
   &(rdpq_blitparms_t){.scale_x=1.25f*n64_video_scale,.scale_y=1.25f*n64_video_scale});
  if(flow->phase!=PACK_SEALED)for(int i=2;i>=0;i--)rdpq_tex_blit(&assets->back,n64_video_x(177+i*4),n64_video_y(140-i*3),
   &(rdpq_blitparms_t){.scale_x=n64_video_scale,.scale_y=n64_video_scale});
 }
 rdpq_mode_pop();rdpq_detach_wait();
}
static surface_t *draw(PackAssets *assets,unsigned white_alpha)
{
 surface_t *display=display_get();rdpq_attach(display,NULL);rdpq_mode_push();rdpq_set_mode_copy(false);
 rdpq_tex_blit(&assets->canvas,0,0,NULL);
 if(white_alpha) {
  rdpq_set_mode_standard();rdpq_mode_combiner(RDPQ_COMBINER_FLAT);rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
  rdpq_set_prim_color(RGBA32(255,255,255,white_alpha));rdpq_fill_rectangle(0,0,n64_video_width,n64_video_height);
 }
 rdpq_mode_pop();rdpq_detach_wait();n64_video_present(display);adventure_audio_poll();return display;
}

void adventure_pack_open(unsigned booster,const uint8_t cards[10],const char *recipient)
{
 if(booster>=29||!cards)return;
 PackFlow flow;pack_flow_init(&flow,cards);PackAssets assets=assets_open(booster,cards);
#if ADVENTURE_DIAGNOSTIC == 24
 adventure_pack_native_begin(booster,&flow);
#endif
 adventure_audio_event(0x4a,0);adventure_audio_event(0,MUSIC_BOOSTER_PACK);
 input_begin();compose(&assets,&flow,booster,recipient);
 unsigned repeats[2]={0};
 for(;;) {
  unsigned half=PACK_OPEN_FRAMES/2;
  unsigned alpha=flow.phase==PACK_OPENING?(flow.frame<half?flow.frame:PACK_OPEN_FRAMES-1-flow.frame)*255/(half-1):0;
#if ADVENTURE_DIAGNOSTIC == 24
  surface_t *display=draw(&assets,alpha);adventure_pack_native_frame(&flow,display);
#else
  draw(&assets,alpha);
#endif
  uint16_t pressed=input_take();bool changed=false;
  disable_interrupts();unsigned held=input.held[0];enable_interrupts();
  for(unsigned i=0;i<2;i++) {
   unsigned bit=i?INPUT_DOWN:INPUT_UP;
   if(!(held&bit))repeats[i]=0;
   else if(++repeats[i]>=18&&(repeats[i]-18)%4==0)pressed|=(uint16_t)bit;
  }
  if(flow.phase==PACK_LIST&&(pressed&(INPUT_A|INPUT_START))) {
   input_end();adventure_audio_event(1,SFX_CONFIRM);n64_card_detail(flow.cards[flow.index]);input_begin();
   compose(&assets,&flow,booster,recipient);continue;
  }
  changed=pack_flow_input(&flow,pressed);
  if(pack_flow_ready(&flow,!adventure_audio_music_playing())) {
   adventure_audio_event(0x4b,0);changed=true;
  }
  if(flow.phase==PACK_DONE)break;
  if(pack_flow_step(&flow))changed=true;
  if(flow.phase==PACK_OPENING&&flow.frame==half) {
   PackFlow preview=flow;preview.phase=PACK_LIST;compose(&assets,&preview,booster,recipient);
  }
  if(changed){adventure_audio_event(1,SFX_CONFIRM);compose(&assets,&flow,booster,recipient);}
 }
 input_end();assets_close(&assets);
#if ADVENTURE_DIAGNOSTIC == 24
 adventure_pack_native_end(&flow);
#endif
}

#if ADVENTURE_DIAGNOSTIC
surface_t *adventure_pack_preview(unsigned booster,const uint8_t cards[10],const char *recipient,unsigned phase,unsigned index)
{
 PackFlow flow;pack_flow_init(&flow,cards);flow.phase=phase;flow.index=index<10?index:0;
 PackAssets assets=assets_open(booster,cards);compose(&assets,&flow,booster,recipient);surface_t *display=draw(&assets,0);
 assets_close(&assets);return display;
}
#endif
