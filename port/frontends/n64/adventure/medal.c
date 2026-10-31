#include <libdragon.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "medal.h"
#include "medal_flow.h"
#include "audio.h"
#include "video.h"
#include "duel_bridge.h"

typedef struct {sprite_t *medals[MEDAL_COUNT];surface_t canvas;} MedalAssets;
static volatile DuelInputCapture input;
static volatile unsigned reward_vi;

static void input_sample(void)
{
 joypad_poll();joypad_buttons_t b=joypad_get_inputs(JOYPAD_PORT_1).btn;
 duel_input_sample(&input,0,(b.a?INPUT_A:0)|(b.b?INPUT_B:0),joypad_is_connected(JOYPAD_PORT_1));reward_vi++;
}
static void input_begin(void)
{
 disable_interrupts();input=(DuelInputCapture){0};input_sample();input.pending[0]=0;reward_vi=0;
 register_VI_handler(input_sample);enable_interrupts();
}
static void input_end(void)
{disable_interrupts();unregister_VI_handler(input_sample);input=(DuelInputCapture){0};enable_interrupts();}
static uint16_t input_take(unsigned *elapsed,uint16_t *held)
{disable_interrupts();uint16_t pressed=duel_input_take(&input,0);*elapsed=reward_vi;*held=input.held[0];enable_interrupts();return pressed;}
static uint32_t color(unsigned r,unsigned g,unsigned b){return graphics_make_color(r,g,b,255);}
static void box(surface_t *canvas,int x,int y,int width,int height,uint32_t value)
{n64_video_box(canvas,x,y,width,height,value);}
static void border(surface_t *canvas,int x,int y,int width,int height,uint32_t value)
{
 box(canvas,x,y,width,1,value);box(canvas,x,y+height-1,width,1,value);
 box(canvas,x,y,1,height,value);box(canvas,x+width-1,y,1,height,value);
}
static void text(surface_t *canvas,int x,int y,uint32_t value,const char *str,int font,int width)
{n64_ui_draw_text(canvas,x,y,value,str,font,width);}

static MedalAssets assets_open(void)
{
 static const char *const names[]={"grass","science","fire","water","lightning","psychic","rock","fighting"};
 MedalAssets assets={0};assets.canvas=n64_video_surface();assert(assets.canvas.buffer);
 for(unsigned i=0;i<MEDAL_COUNT;i++){
  char path[80];snprintf(path,sizeof(path),"rom:/medals/%s.sprite",names[i]);assets.medals[i]=sprite_load(path);assert(assets.medals[i]);
 }
 return assets;
}
static void assets_close(MedalAssets *assets)
{rspq_wait();for(unsigned i=0;i<MEDAL_COUNT;i++)sprite_free(assets->medals[i]);surface_free(&assets->canvas);}

static void compose(MedalAssets *assets,const MedalFlow *flow,unsigned elapsed)
{
 static const char *const names[]={"Grass Medal","Science Medal","Fire Medal","Water Medal","Lightning Medal","Psychic Medal","Rock Medal","Fighting Medal"};
 surface_t canvas=assets->canvas;canvas.buffer=CachedAddr(canvas.buffer);
 uint32_t paper=color(247,247,222),cream=color(231,231,198),ink=color(24,48,80),edge=color(74,115,156),gold=color(218,164,47),dim=color(185,188,173);
 graphics_fill_screen(&canvas,color(20,28,38));box(&canvas,8,8,304,224,paper);
 border(&canvas,8,8,304,224,edge);border(&canvas,9,9,302,222,edge);
 text(&canvas,24,17,ink,names[flow->index],N64_UI_FONT_HEADING,272);
 box(&canvas,16,36,288,1,dim);
 /* The24px source designs keep their exact four-color cream-backed art.
    A3x hero makes the new medal legible while the source4x2 order stays intact. */
 box(&canvas,116,43,88,88,cream);border(&canvas,116,43,88,88,edge);
 if(medal_highlight(elapsed)){border(&canvas,114,41,92,92,gold);border(&canvas,113,40,94,94,gold);}
 text(&canvas,24,133,ink,"Master Medals",N64_UI_FONT_BODY,272);
 for(unsigned i=0;i<MEDAL_COUNT;i++){
  int x=medal_grid_x(i),y=medal_grid_y(i);box(&canvas,x,y,60,30,medal_owned(flow,i)?cream:paper);
  border(&canvas,x,y,60,30,i==flow->index&&medal_highlight(elapsed)?gold:dim);
  if(!medal_owned(flow,i))border(&canvas,x+18,y+3,24,24,dim);
 }
 box(&canvas,16,210,288,1,dim);
 if(elapsed>=MEDAL_CELEBRATION_VI)text(&canvas,24,216,ink,"A/B: continue",N64_UI_FONT_COMPACT,272);
 data_cache_hit_writeback_invalidate(canvas.buffer,(unsigned long)canvas.stride*canvas.height);
 rdpq_attach(&assets->canvas,NULL);rdpq_mode_push();rdpq_set_mode_standard();rdpq_mode_filter(FILTER_POINT);
 rdpq_sprite_blit(assets->medals[flow->index],n64_video_x(124),n64_video_y(51),
  &(rdpq_blitparms_t){.scale_x=3*n64_video_scale,.scale_y=3*n64_video_scale});
 for(unsigned i=0;i<MEDAL_COUNT;i++)if(medal_owned(flow,i))rdpq_sprite_blit(assets->medals[i],
  n64_video_x(medal_grid_x(i)+18),n64_video_y(medal_grid_y(i)+3),
  &(rdpq_blitparms_t){.scale_x=n64_video_scale,.scale_y=n64_video_scale});
 rdpq_mode_pop();rdpq_detach_wait();
}
static surface_t *draw(MedalAssets *assets)
{
 surface_t *frame=display_get();rdpq_attach(frame,NULL);rdpq_mode_push();rdpq_set_mode_copy(false);
 rdpq_tex_blit(&assets->canvas,0,0,NULL);rdpq_mode_pop();rdpq_detach_wait();
 n64_video_present(frame);adventure_audio_poll();return frame;
}

void adventure_show_medal(unsigned event,const OwState *state)
{
 MedalFlow flow;if(!state||!medal_flow_init(&flow,event,ow_event(state,EVENT_MEDAL_FLAGS)))return;
 adventure_audio_poll();MedalAssets assets=assets_open();adventure_audio_poll();
 adventure_audio_event(0x4a,0);adventure_audio_event(0,MUSIC_MEDAL);input_begin();
 unsigned elapsed=0,painted=~0u;
 for(;;){
  unsigned phase=(elapsed>=MEDAL_CELEBRATION_VI?2u:0u)|(medal_highlight(elapsed)?1u:0u);
  if(phase!=painted){compose(&assets,&flow,elapsed);painted=phase;}
  unsigned presented_elapsed=elapsed;draw(&assets);
  uint16_t held,pressed=input_take(&elapsed,&held);
  if(medal_flow_input(&flow,presented_elapsed,pressed,held,!adventure_audio_music_playing()))break;
 }
 input_end();adventure_audio_event(0x4b,0);assets_close(&assets);
}

#if ADVENTURE_DIAGNOSTIC
/* Private review harness uses the same composer and owned, fenced frame. */
surface_t *adventure_medal_preview(unsigned event,unsigned flags,unsigned elapsed)
{
 MedalFlow flow;assert(medal_flow_init(&flow,event,flags));MedalAssets assets=assets_open();
 compose(&assets,&flow,elapsed);surface_t *frame=draw(&assets);assets_close(&assets);return frame;
}
#endif
