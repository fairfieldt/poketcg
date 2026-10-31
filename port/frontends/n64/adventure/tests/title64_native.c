/* Isolated native test, built by tools/review_title64.py. */
#include "../intro.c"
#include "../video.c"
#include <stdio.h>
#include <stdlib.h>
#include "engine.h"

extern struct surface_s *title64_reference_preview(unsigned,unsigned);
extern bool title64_reference_check(void);
static bool drive;
static unsigned input_case,button_calls,music_stops,music_starts,confirm_sounds;
static volatile unsigned vi_count;

void game_tick(void){}
void game_draw(void){assertf(false,"Title must keep drawing its own frame");}
static void tick(void){vi_count++;assertf(vi_count<60*45,"Title gate exceeded45 seconds");}
joypad_buttons_t __wrap_joypad_get_buttons_pressed(joypad_port_t port)
{
 joypad_buttons_t keys={0};if(!drive||port!=JOYPAD_PORT_1)return keys;
 button_calls++;unsigned final=input_case==2?46:40;
 if(button_calls==5){if(input_case==1)keys.start=true;else keys.a=true;}
 if(button_calls==final){if(input_case==1)keys.a=true;else keys.start=true;}
 return keys;
}
extern void __real_adventure_audio_event(unsigned,unsigned);
void __wrap_adventure_audio_event(unsigned kind,unsigned id)
{
 if(drive){if(!kind&&id==MUSIC_STOP)music_stops++;if(!kind&&id==MUSIC_TITLESCREEN)music_starts++;
  if(kind==1&&id==SFX_CONFIRM)confirm_sounds++;}
 __real_adventure_audio_event(kind,id);
}
static void capture(surface_t *frame,const char *label)
{
 const uint16_t *pixels=UncachedAddr(frame->buffer);
 debugf("FRAMEBUFFER %s-%u %u %u\n",label,frame->width,frame->width,frame->height);
 for(unsigned y=0;y<frame->height;y++)for(unsigned x=0;x<frame->width;x+=32){
  char text[129];unsigned n=frame->width-x;if(n>32)n=32;
  for(unsigned i=0;i<n;i++)snprintf(text+i*4,5,"%04x",pixels[y*(frame->stride/2)+x+i]);
  debugf("PIXELS %u %u %s\n",x,y,text);
 }
 debugf("FRAMEBUFFER END\n");
}
int main(void)
{
 debug_init_isviewer();debug_init_usblog();timer_init();dfs_init(DFS_DEFAULT_LOCATION);rdpq_init();joypad_init();adventure_audio_init();
 register_VI_handler(tick);bool high=get_memory_size()>=8*1024*1024;
 display_init(n64_video_init(high),DEPTH_16_BPP,3,GAMMA_NONE,FILTERS_RESAMPLE);
 assertf(adventure_intro_check()&&title64_reference_check(),"Original orb animation invariants changed");
 IntroAssets assets=assets_open();assertf(assets.title64->width==(high?420:280)&&assets.title64->height==(high?210:140),"Wrong native title derivative loaded");assets_close(&assets);
 uint16_t *reference=malloc((size_t)n64_video_width*n64_video_height*2);assert(reference);
 unsigned unchanged=0;
 for(unsigned scene=0;scene<3;scene++){
  surface_t *before=title64_reference_preview(scene,64);memcpy(reference,UncachedAddr(before->buffer),(size_t)before->stride*before->height);
  surface_t *after=adventure_intro_preview(scene,64);assertf(!memcmp(reference,UncachedAddr(after->buffer),(size_t)after->stride*after->height),"Original opening scene%u changed pixels",scene);
  unchanged+=n64_video_width*n64_video_height;if(!scene)capture(after,"opening-charizard");
 }
 surface_t *before=title64_reference_preview(3,64);memcpy(reference,UncachedAddr(before->buffer),(size_t)before->stride*before->height);
 surface_t *title=adventure_intro_preview(3,64);const uint16_t *pixels=UncachedAddr(title->buffer);unsigned footer=0;
 for(int y=n64_video_y(177);y<n64_video_y(228);y++)for(int x=n64_video_x(40);x<n64_video_x(280);x++){
  assertf(pixels[y*n64_video_width+x]==reference[y*n64_video_width+x],"Original copyright footer changed at%d,%d",x,y);footer++;
 }
 capture(title,"title64");title=adventure_intro_preview(3,110);capture(title,"title64-prompt-off");free(reference);
 uint8_t memory[WRAM_SIZE+HRAM_SIZE];memcpy(memory,gWRAM,WRAM_SIZE);memcpy(memory+WRAM_SIZE,gHRAM,HRAM_SIZE);
 for(input_case=0;input_case<3;input_case++){
  button_calls=music_stops=music_starts=confirm_sounds=0;drive=true;adventure_intro_play(input_case==2);drive=false;
  assertf(button_calls==(input_case==2?46u:40u)&&music_stops==1&&music_starts==1&&confirm_sounds==1,
   "Title A/Start/skip flow changed case%u polls%u stop%u start%u confirm%u",input_case,button_calls,music_stops,music_starts,confirm_sounds);
  assertf(adventure_audio_music_playing()&&mixer_ch_playing(0)&&mixer_ch_get_pos(0)>0,"Title music did not play through actual input flow");
  assertf(!memcmp(gWRAM,memory,WRAM_SIZE)&&!memcmp(gHRAM,memory+WRAM_SIZE,HRAM_SIZE),"Title changed canonical duel memory");
 }
 adventure_audio_event(0,MUSIC_STOP);adventure_audio_event(1,0);
 heap_stats_t heap;sys_get_heap_stats(&heap);
 debugf("TITLE64 NATIVE PASS ram=%d width=%u opening_pixels=%u footer_pixels=%u input_cases=3 source_orbs=1 audio=1 readonly=1 free_heap=%d\n",get_memory_size(),n64_video_width,unchanged,footer,heap.total-heap.used);
 for(;;)wait_ms(10);
}
