#include <libdragon.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "intro.h"
#include "audio.h"
#include "gen/constants.h"
#include "assets/intro/intro_assets.h"

/* Original 160x144 composition, centered without changing its aspect ratio. */
#define INTRO_SCALE 1.5f
#define INTRO_LEFT 40
#define INTRO_TOP 12
#define INTRO_RIGHT 280
#define INTRO_BOTTOM 228
#define TITLE_MUSIC_TICKS 2044u
#define SCATTER_SOUND_TICKS 61u

typedef struct {
 sprite_t *background[4], *orbs[7], *press_start;
} IntroAssets;
typedef struct { int x,y; bool visible,done; } OrbFrame;

static IntroAssets assets_open(void)
{
 static const char *const backgrounds[]={"charizard","scyther","aerodactyl","title"};
 static const char *const orbs[]={"grass","fire","water","colorless","lightning","psychic","fighting"};
 IntroAssets assets={0};char path[96];
 for(unsigned i=0;i<4;i++) {
  snprintf(path,sizeof(path),"rom:/intro/%s.sprite",backgrounds[i]);
  assets.background[i]=sprite_load(path);assert(assets.background[i]);
 }
 for(unsigned i=0;i<7;i++) {
  snprintf(path,sizeof(path),"rom:/intro/orb_%s.sprite",orbs[i]);
  assets.orbs[i]=sprite_load(path);assert(assets.orbs[i]);
 }
 assets.press_start=sprite_load("rom:/intro/press_start.sprite");assert(assets.press_start);
 return assets;
}

static void assets_close(IntroAssets *assets)
{
 rspq_wait();
 for(unsigned i=0;i<4;i++)sprite_free(assets->background[i]);
 for(unsigned i=0;i<7;i++)sprite_free(assets->orbs[i]);
 sprite_free(assets->press_start);
}

/* Read the disassembly's literal frame_data records. Translation is applied
   when a frame starts, and coordinates wrap as the original 8-bit OAM values.
   Duration -1 freezes a frame; image -1 hides it; duration 0 loops the table. */
static OrbFrame animation_frame(unsigned id,int x,int y,unsigned time)
{
 assert(id>=191&&id<=216);
 const IntroAnimation *animation=&intro_animations[id-191];
 bool revealed=id!=216;
 for(unsigned cursor=0,guard=0;guard<8192;guard++,cursor++) {
  if(cursor>=animation->count)break;
  const IntroAnimFrame *frame=&animation->frames[cursor];
  if(!frame->duration){cursor=(unsigned)-1;continue;}
  x=(x+frame->dx)&255;y=(y+frame->dy)&255;
  if(x==152)revealed=true;
  OrbFrame result={x-8,y-16,frame->frame>=0&&revealed,frame->duration<0};
  if(frame->duration<0||time<(unsigned)frame->duration)return result;
  time-=(unsigned)frame->duration;
 }
 return (OrbFrame){.done=true};
}

static unsigned animation_length(unsigned id)
{
 const IntroAnimation *animation=&intro_animations[id-191];unsigned total=0;
 for(unsigned i=0;i<animation->count;i++) {
  if(animation->frames[i].duration<=0)return total;
  total+=(unsigned)animation->frames[i].duration;
 }
 return total;
}

static unsigned title_arrival_ticks(void)
{
 unsigned longest=0;
 for(unsigned i=0;i<7;i++) {
  unsigned length=animation_length(intro_orb_animation_ids[3][i]);
  if(length>longest)longest=length;
 }
 return longest;
}

static unsigned white_fade(unsigned time,unsigned begin,unsigned end)
{
 if(time<begin)return 255;
 if(time<begin+16)return (begin+16-time)*255/16;
 if(time<end)return 0;
 if(time<end+16)return (time-end)*255/16;
 return 255;
}

static void blit(sprite_t *sprite,int x,int y)
{
 rdpq_sprite_blit(sprite,INTRO_LEFT+x*INTRO_SCALE,INTRO_TOP+y*INTRO_SCALE,
  &(rdpq_blitparms_t){.scale_x=INTRO_SCALE,.scale_y=INTRO_SCALE});
}

static void draw_orbs(const IntroAssets *assets,unsigned stage,unsigned time)
{
 for(unsigned i=0;i<7;i++) {
  OrbFrame frame=animation_frame(intro_orb_animation_ids[stage][i],
   (intro_orb_coordinates[stage][i][0]+8)&255,
   (intro_orb_coordinates[stage][i][1]+16)&255,time);
  if(frame.visible)blit(assets->orbs[i],frame.x,frame.y);
 }
}

static void draw_white(unsigned alpha)
{
 if(alpha==255)rdpq_set_mode_fill(RGBA32(255,255,255,255));
 else {
  rdpq_mode_combiner(RDPQ_COMBINER_FLAT);rdpq_set_prim_color(RGBA32(255,255,255,alpha));
  rdpq_mode_alphacompare(0);rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
 }
 rdpq_fill_rectangle(0,0,320,240);
 rdpq_set_mode_standard();rdpq_mode_filter(FILTER_BILINEAR);
 rdpq_mode_combiner(RDPQ_COMBINER_TEX);rdpq_mode_alphacompare(1);
}

/* All drawing goes through RDP, including the fades and prompt. No CPU writes
   race an RDP framebuffer or depend on VI hidden coverage after a transition. */
static surface_t *draw_frame(const IntroAssets *assets,unsigned scene,unsigned time,
 bool idle,bool prompt,const int idle_started[7],unsigned extra_white)
{
 surface_t *frame=display_get();rdpq_attach(frame,NULL);rdpq_mode_push();
 rdpq_set_mode_fill(RGBA32(intro_clear_rgb[scene][0],intro_clear_rgb[scene][1],intro_clear_rgb[scene][2],255));
 rdpq_fill_rectangle(0,0,320,240);
 rdpq_set_mode_standard();rdpq_mode_filter(FILTER_BILINEAR);
 rdpq_mode_combiner(RDPQ_COMBINER_TEX);rdpq_mode_alphacompare(1);
 blit(assets->background[scene],0,0);
 if(scene==3) {
  /* The logo is filtered artwork; its small copyright lettering stays crisp. */
  rdpq_mode_filter(FILTER_POINT);
  rdpq_sprite_blit(assets->background[scene],INTRO_LEFT,INTRO_TOP+110*INTRO_SCALE,
   &(rdpq_blitparms_t){.s0=0,.t0=110,.width=160,.height=34,.scale_x=INTRO_SCALE,.scale_y=INTRO_SCALE});
 }
 unsigned fade=0;
 unsigned arrival=title_arrival_ticks();
 if(!idle)fade=scene<3?white_fade(time,44,scene==2?144:88):white_fade(time,arrival,UINT32_MAX-16);
 if(fade)draw_white(fade);
 rdpq_mode_filter(FILTER_BILINEAR);
 rdpq_set_scissor(INTRO_LEFT,INTRO_TOP,INTRO_RIGHT,INTRO_BOTTOM);
 if(!idle) {
  if(scene<3)draw_orbs(assets,scene,time);
  else if(time<arrival+16)draw_orbs(assets,3,time);
  else draw_orbs(assets,4,time-arrival-16);
 } else if(idle_started) {
  for(unsigned i=0;i<7;i++)if(idle_started[i]>=0) {
   OrbFrame orb=animation_frame(216,248,14,time-(unsigned)idle_started[i]);
   if(orb.visible)blit(assets->orbs[i],orb.x,orb.y);
  }
 }
 /* The original delays the prompt 60 ticks, then alternates 37 on / 26 off. */
 if(prompt&&time>=60&&(time-60)%63<37) {
  rdpq_mode_filter(FILTER_POINT);blit(assets->press_start,35,94);
 }
 rdpq_set_scissor(0,0,320,240);if(extra_white)draw_white(extra_white);
 rdpq_mode_pop();rdpq_detach_wait();display_show(frame);
 adventure_audio_poll();return frame;
}

static bool confirmed(void)
{
 joypad_poll();joypad_buttons_t buttons=joypad_get_buttons_pressed(JOYPAD_PORT_1);
 return buttons.a||buttons.start;
}

static bool play_opening(IntroAssets *assets)
{
 static const unsigned lengths[]={118,118,204};
 for(unsigned scene=0;scene<3;scene++) {
  adventure_audio_event(1,scene==2?SFX_INTRO_ORB_SWOOP:SFX_INTRO_ORB);
  for(unsigned time=0;time<lengths[scene];time+=2) {
   if(confirmed())return false;
   draw_frame(assets,scene,time,false,false,NULL,0);
  }
 }
 adventure_audio_event(1,SFX_INTRO_ORB_TITLE);
 unsigned arrival=title_arrival_ticks();
 unsigned scatter=arrival+16,music=scatter+SCATTER_SOUND_TICKS;
 for(unsigned time=0;time<music+60;time+=2) {
  if(confirmed())return false;
  if(time>=scatter&&time<scatter+2)adventure_audio_event(1,SFX_INTRO_ORB_SCATTER);
  if(time>=music&&time<music+2)adventure_audio_event(0,MUSIC_TITLESCREEN);
  draw_frame(assets,3,time,false,false,NULL,0);
 }
 return true;
}

void adventure_intro_play(bool opening)
{
 IntroAssets assets=assets_open();
 for(;;) {
  adventure_audio_event(0,MUSIC_STOP);
  bool completed=opening&&play_opening(&assets);
  if(!completed) {adventure_audio_event(1,0);adventure_audio_event(0,MUSIC_TITLESCREEN);}
  int started[7];for(unsigned i=0;i<7;i++)started[i]=-1;
  for(unsigned time=0;time<TITLE_MUSIC_TICKS-(completed?60:0);time+=2) {
   if(confirmed()&&time>=60) {
    adventure_audio_event(1,SFX_CONFIRM);
    for(unsigned fade=0;fade<=16;fade+=2)draw_frame(&assets,3,time,true,true,started,fade*255/16);
    assets_close(&assets);return;
   }
   if(!(time%64)) {
    unsigned orb_next=((unsigned)get_ticks()>>8)%7;
    for(unsigned i=0;i<7;i++) {
     unsigned orb=(orb_next+i)%7;
     if(started[orb]<0||time-(unsigned)started[orb]>=animation_length(216)) {
      started[orb]=(int)time;break;
     }
    }
   }
   draw_frame(&assets,3,time,true,true,started,0);
  }
  for(unsigned fade=0;fade<=16;fade+=2)draw_frame(&assets,3,0,true,false,NULL,fade*255/16);
  opening=true;
 }
}

struct surface_s *adventure_intro_preview(unsigned scene,unsigned source_frame)
{
 assert(scene<4);IntroAssets assets=assets_open();
 surface_t *frame=draw_frame(&assets,scene,source_frame,scene==3,scene==3,NULL,0);
 assets_close(&assets);return frame;
}

bool adventure_intro_check(void)
{
 /* These distinguish source wraparound, stopped vs hidden frames, delayed
    title arrival, and the CGB idle-orb reveal condition from estimated paths. */
 OrbFrame entry=animation_frame(192,248,44,4);
 OrbFrame stopped=animation_frame(195,120,160,1000);
 OrbFrame hidden=animation_frame(216,248,14,1000);
 return entry.x==-6&&entry.y==28&&entry.visible&&
  stopped.x==112&&stopped.y==52&&stopped.done&&stopped.visible&&
  hidden.done&&!hidden.visible&&title_arrival_ticks()==78&&
  white_fade(44,44,88)==255&&white_fade(60,44,88)==0&&white_fade(104,44,88)==255;
}
