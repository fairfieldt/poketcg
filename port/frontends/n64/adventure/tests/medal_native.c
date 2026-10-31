/* Private native gate; built by tools/review_medals.py, never game main. */
#define TCGN64_ADVENTURE
#define TCGN64_CARD_ART
#include "../../tcgn64.c"
#include "../video.c"
#include "../medal.h"
#include "../medal_flow.h"
#include "../audio.h"
#include "overworld.h"

extern surface_t *adventure_medal_preview(unsigned,unsigned,unsigned);
static volatile unsigned ticks;
static unsigned started,live_started,live_frames;
static bool live;
static surface_t *last_medal_frame;
static double paused_position;
static unsigned collection_texels;

void game_tick(void){}
void game_draw(void){assertf(false,"Medal waiting must keep its own screen visible");}
static void tick(void){ticks++;assertf(ticks-started<60*50,"Medal gate exceeded50 seconds");}
bool __wrap_joypad_is_connected(joypad_port_t port){return port==JOYPAD_PORT_1;}
joypad_inputs_t __wrap_joypad_get_inputs(joypad_port_t port)
{
 joypad_inputs_t value={0};unsigned age=ticks-live_started;
 if(live&&port==JOYPAD_PORT_1){value.btn.a=age<240;value.btn.b=age>=252;}
 return value;
}
extern void __real_n64_video_present(surface_t *frame);
void __wrap_n64_video_present(surface_t *frame)
{
 __real_n64_video_present(frame);
 if(live){last_medal_frame=frame;live_frames++;assertf(mixer_ch_get_pos(0)==paused_position,"Medal advanced the paused room song");}
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
static void check_hero(const surface_t *frame,unsigned medal)
{
 static const char *const names[]={"grass","science","fire","water","lightning","psychic","rock","fighting"};
 char path[96];snprintf(path,sizeof(path),"rom:/medals/%s.sprite",names[medal]);sprite_t *sprite=sprite_load(path);
 surface_t source=sprite_get_pixels(sprite);const uint16_t *expected=UncachedAddr(source.buffer),*actual=UncachedAddr(frame->buffer);
 for(unsigned y=0;y<24;y++)for(unsigned x=0;x<24;x++){
  unsigned px=n64_video_x(124)+(unsigned)((x+.5f)*3*n64_video_scale),py=n64_video_y(51)+(unsigned)((y+.5f)*3*n64_video_scale);
  assertf(actual[py*(frame->stride/2)+px]==expected[y*(source.stride/2)+x],"Native medal%u art mismatch at%u,%u",medal,x,y);
 }
 sprite_free(sprite);
}
static void check_collection(const surface_t *frame,unsigned flags)
{
 static const char *const names[]={"grass","science","fire","water","lightning","psychic","rock","fighting"};
 const uint16_t *actual=UncachedAddr(frame->buffer);
 for(unsigned medal=0;medal<8;medal++)if(flags&(0x80u>>medal)){
  char path[96];snprintf(path,sizeof(path),"rom:/medals/%s.sprite",names[medal]);sprite_t *sprite=sprite_load(path);
  surface_t source=sprite_get_pixels(sprite);const uint16_t *expected=UncachedAddr(source.buffer);
  for(unsigned y=0;y<24;y++)for(unsigned x=0;x<24;x++){
   /* SDK point-sampled1.5x texture rectangles round their half-texel
      origin up by one physical pixel; use a source-texel interior. */
   unsigned offset=n64_video_width>320?1:0;
   unsigned px=n64_video_x(medal_grid_x(medal)+18)+offset+(unsigned)((x+.5f)*n64_video_scale);
   unsigned py=n64_video_y(medal_grid_y(medal)+3)+offset+(unsigned)((y+.5f)*n64_video_scale);
   assertf(actual[py*(frame->stride/2)+px]==expected[y*(source.stride/2)+x],
    "Native collection flags%02x medal%u mismatch at%u,%u",flags,medal,x,y);collection_texels++;
  }
  sprite_free(sprite);
 }
}
static void wait_frames(unsigned count)
{
 while(count--){surface_t *frame=display_get();rdpq_attach(frame,NULL);rdpq_set_mode_fill(RGBA32(20,28,38,255));
  rdpq_fill_rectangle(0,0,n64_video_width,n64_video_height);rdpq_detach_wait();n64_video_present(frame);adventure_audio_poll();}
}
int main(void)
{
 debug_init_isviewer();debug_init_usblog();timer_init();dfs_init(DFS_DEFAULT_LOCATION);rdpq_init();joypad_init();adventure_audio_init();
 ticks=started=0;register_VI_handler(tick);
 bool high=get_memory_size()>=8*1024*1024;display_init(n64_video_init(high),DEPTH_16_BPP,3,GAMMA_NONE,FILTERS_RESAMPLE);
 g_raster_width=n64_video_width;g_ui_canvas=n64_video_surface();for(unsigned i=0;i<NUM_COLORS;i++)g_col[i]=graphics_make_color(kRGB[i][0],kRGB[i][1],kRGB[i][2],255);
 for(unsigned i=0;i<8;i++){
  surface_t *frame=adventure_medal_preview(EVENT_BEAT_NIKKI+i,(0xffu<<(7-i))&255,MEDAL_CELEBRATION_VI);
  check_hero(frame,i);check_collection(frame,(0xffu<<(7-i))&255);
  if(i==3)capture(frame,"water-collected");
  if(i==5)capture(frame,"psychic");
  if(i==7)capture(frame,"all-medals");
 }
 adventure_audio_event(0,MUSIC_OVERWORLD);wait_frames(12);paused_position=mixer_ch_get_pos(0);
 OwState state={0};ow_set_event(&state,EVENT_BEAT_AMY,1);OwState before=state;
 uint8_t memory[WRAM_SIZE+HRAM_SIZE];memcpy(memory,gWRAM,WRAM_SIZE);memcpy(memory+WRAM_SIZE,gHRAM,HRAM_SIZE);
 live_frames=0;live_started=ticks;live=true;adventure_show_medal(EVENT_BEAT_AMY,&state);live=false;
 assertf(ticks-live_started>=252&&live_frames>120,"Held confirm skipped medal celebration/reader");
 assertf(mixer_ch_playing(0)&&!mixer_ch_playing(4)&&adventure_audio_music_playing(),"Medal failed to finish fanfare/resume room song");
 assertf(mixer_ch_get_pos(0)==paused_position,"Medal did not restore exact room position");
 assertf(!memcmp(&state,&before,sizeof(state))&&!memcmp(gWRAM,memory,WRAM_SIZE)&&!memcmp(gHRAM,memory+WRAM_SIZE,HRAM_SIZE),"Medal presentation changed canonical state");
 check_hero(last_medal_frame,3);check_collection(last_medal_frame,0x10);adventure_audio_event(0,0);capture(last_medal_frame,"water-award");
 heap_stats_t heap;sys_get_heap_stats(&heap);
 assertf(collection_texels==21312,"Collection checks omitted a medal");
 debugf("MEDAL NATIVE PASS ram=%d width=%u designs=8 hero_texels=4608 collection_texels=%u read_only=1 held_fresh=1 frames=%u vi=%u free_heap=%d\n",get_memory_size(),n64_video_width,collection_texels,live_frames,ticks-live_started,heap.total-heap.used);
 for(;;)wait_ms(10);
}
