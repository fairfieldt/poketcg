#include <libdragon.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "intro.h"
#include "audio.h"
#include "video.h"
#include "gen/constants.h"
#include "assets/intro/intro_assets.h"
#include "assets/title64/title64_assets.h"

/* Original 160x144 composition, centered without changing its aspect ratio. */
#define INTRO_SCALE 1.5f
#define INTRO_LEFT 40
#define INTRO_TOP 12
#define INTRO_RIGHT 280
#define INTRO_BOTTOM 228
#define TITLE_MUSIC_TICKS 2044u
#define SCATTER_SOUND_TICKS 61u

typedef struct {
 sprite_t *background[4], *press_start, *title64;
 surface_t orbs[7];
} IntroAssets;
typedef struct { int x,y; bool visible,done; } OrbFrame;

static surface_t orb_surface(sprite_t *sprite)
{
 surface_t source=sprite_get_pixels(sprite);
 assert(surface_get_format(&source)==FMT_RGBA16);
 surface_t padded=surface_alloc(FMT_RGBA16,source.width+2,source.height+2);
 assert(padded.buffer);
 /* The original circles share one solid outline color. Bleed its RGB into
    transparent pixels so straight-alpha filtering cannot add a black halo. */
 uint16_t outline=0;bool found=false;
 for(unsigned y=0;y<source.height&&!found;y++) {
  const uint16_t *row=(uint16_t *)((uint8_t *)source.buffer+y*source.stride);
  for(unsigned x=0;x<source.width;x++)if(row[x]&1) {
   outline=row[x]&~1u;found=true;break;
  }
 }
 assert(found);
 for(unsigned y=0;y<padded.height;y++) {
  uint16_t *row=(uint16_t *)((uint8_t *)padded.buffer+y*padded.stride);
  for(unsigned x=0;x<padded.width;x++)row[x]=outline;
 }
 for(unsigned y=0;y<source.height;y++) {
  const uint16_t *row=(uint16_t *)((uint8_t *)source.buffer+y*source.stride);
  uint16_t *destination=(uint16_t *)((uint8_t *)padded.buffer+(y+1)*padded.stride)+1;
  for(unsigned x=0;x<source.width;x++)if(row[x]&1)destination[x]=row[x];
 }
 return padded;
}

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
  sprite_t *orb=sprite_load(path);assert(orb);
  assets.orbs[i]=orb_surface(orb);sprite_free(orb);
 }
 assets.press_start=sprite_load("rom:/intro/press_start.sprite");assert(assets.press_start);
 assets.title64=sprite_load(n64_video_width>320?TITLE64_LOGO_480:TITLE64_LOGO_320);assert(assets.title64);
 return assets;
}

static void assets_close(IntroAssets *assets)
{
 rspq_wait();
 for(unsigned i=0;i<4;i++)sprite_free(assets->background[i]);
 for(unsigned i=0;i<7;i++)surface_free(&assets->orbs[i]);
 sprite_free(assets->press_start);
 sprite_free(assets->title64);
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
 rdpq_sprite_blit(sprite,(INTRO_LEFT+x*INTRO_SCALE)*n64_video_scale,(INTRO_TOP+y*INTRO_SCALE)*n64_video_scale,
  &(rdpq_blitparms_t){.scale_x=INTRO_SCALE*n64_video_scale,.scale_y=INTRO_SCALE*n64_video_scale});
}

static void blit_orb(const surface_t *orb,int x,int y)
{
 /* Preserve the source origin while giving bilinear edge samples a
    transparent texel outside the original 16x16 circle. */
 rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
 rdpq_tex_blit(orb,(INTRO_LEFT+(x-1)*INTRO_SCALE)*n64_video_scale,
  (INTRO_TOP+(y-1)*INTRO_SCALE)*n64_video_scale,
  &(rdpq_blitparms_t){.scale_x=INTRO_SCALE*n64_video_scale,.scale_y=INTRO_SCALE*n64_video_scale});
 rdpq_mode_blender(0);
}

static void draw_orbs(const IntroAssets *assets,unsigned stage,unsigned time)
{
 for(unsigned i=0;i<7;i++) {
  OrbFrame frame=animation_frame(intro_orb_animation_ids[stage][i],
   (intro_orb_coordinates[stage][i][0]+8)&255,
   (intro_orb_coordinates[stage][i][1]+16)&255,time);
  if(frame.visible)blit_orb(&assets->orbs[i],frame.x,frame.y);
 }
}

static void draw_white(unsigned alpha)
{
 if(alpha==255)rdpq_set_mode_fill(RGBA32(255,255,255,255));
 else {
  rdpq_mode_combiner(RDPQ_COMBINER_FLAT);rdpq_set_prim_color(RGBA32(255,255,255,alpha));
  rdpq_mode_alphacompare(0);rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
 }
 rdpq_fill_rectangle(0,0,n64_video_width,n64_video_height);
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
 rdpq_fill_rectangle(0,0,n64_video_width,n64_video_height);
 rdpq_set_mode_standard();rdpq_mode_filter(FILTER_BILINEAR);
 rdpq_mode_combiner(RDPQ_COMBINER_TEX);rdpq_mode_alphacompare(1);
 if(scene<3)blit(assets->background[scene],0,0);
 else {
  /* Native-size title art has its own alpha edge colors. Only the chosen
     resolution is resident; the original title remains the footer source. */
  rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
  rdpq_sprite_blit(assets->title64,n64_video_x(TITLE64_LEFT),n64_video_y(TITLE64_TOP),NULL);
  rdpq_mode_blender(0);
  /* Keep the original copyright lettering and its exact source crop. */
  rdpq_mode_filter(FILTER_POINT);
  rdpq_sprite_blit(assets->background[scene],INTRO_LEFT*n64_video_scale,(INTRO_TOP+110*INTRO_SCALE)*n64_video_scale,
   &(rdpq_blitparms_t){.s0=0,.t0=110,.width=160,.height=34,.scale_x=INTRO_SCALE*n64_video_scale,.scale_y=INTRO_SCALE*n64_video_scale});
 }
 unsigned fade=0;
 unsigned arrival=title_arrival_ticks();
 if(!idle)fade=scene<3?white_fade(time,44,scene==2?144:88):white_fade(time,arrival,UINT32_MAX-16);
 if(fade)draw_white(fade);
 rdpq_mode_filter(FILTER_BILINEAR);
 rdpq_set_scissor(n64_video_x(INTRO_LEFT),n64_video_y(INTRO_TOP),n64_video_x(INTRO_RIGHT),n64_video_y(INTRO_BOTTOM));
 if(!idle) {
  if(scene<3)draw_orbs(assets,scene,time);
  else if(time<arrival+16)draw_orbs(assets,3,time);
  else draw_orbs(assets,4,time-arrival-16);
 } else if(idle_started) {
  for(unsigned i=0;i<7;i++)if(idle_started[i]>=0) {
   OrbFrame orb=animation_frame(216,248,14,time-(unsigned)idle_started[i]);
   if(orb.visible)blit_orb(&assets->orbs[i],orb.x,orb.y);
  }
 }
 /* The original delays the prompt 60 ticks, then alternates 37 on / 26 off. */
 if(prompt&&time>=60&&(time-60)%63<37) {
  rdpq_mode_filter(FILTER_POINT);blit(assets->press_start,35,94);
 }
 rdpq_set_scissor(0,0,n64_video_width,n64_video_height);if(extra_white)draw_white(extra_white);
 rdpq_mode_pop();rdpq_detach_wait();n64_video_present(frame);
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

#if ADVENTURE_DIAGNOSTIC == 20
#include "tests/fps_sweep.h"
typedef struct {IntroAssets *assets;unsigned scene;bool idle;int started[7];} IntroSurvey;
static surface_t *intro_survey_draw(void *opaque,unsigned frame)
{
 IntroSurvey *ctx=opaque;unsigned time=60+(frame*2)%28;
 if(ctx->idle) {
  time=64+frame*2;
  if(!(frame%32))ctx->started[(frame/32)%7]=(int)time;
 }
 return draw_frame(ctx->assets,ctx->scene,time,ctx->idle,ctx->idle,
  ctx->idle?ctx->started:NULL,0);
}
void adventure_intro_fps_survey(void)
{
 IntroAssets assets=assets_open();IntroSurvey ctx={.assets=&assets};
 static const char *const cases[]={"intro-charizard","intro-scyther","intro-aerodactyl","intro-title-arrival"};
 adventure_audio_event(0,MUSIC_STOP);
 for(unsigned scene=0;scene<4;scene++) {
  if(scene==3)adventure_audio_event(0,MUSIC_TITLESCREEN);
  adventure_audio_event(1,scene==3?SFX_INTRO_ORB_TITLE:scene==2?SFX_INTRO_ORB_SWOOP:SFX_INTRO_ORB);
  ctx.scene=scene;fps_sweep_case(cases[scene],intro_survey_draw,&ctx);
 }
 ctx.idle=true;for(unsigned i=0;i<7;i++)ctx.started[i]=-1;
 fps_sweep_case("title-idle",intro_survey_draw,&ctx);assets_close(&assets);
}
#endif

#if ADVENTURE_DIAGNOSTIC == 14
void adventure_audio_review(void)
{
 IntroAssets assets=assets_open();
 adventure_audio_review_reset();
 debugf("AUDIO REVIEW opening BEGIN\n");
 adventure_audio_event(0,MUSIC_STOP);play_opening(&assets);
 adventure_audio_review_report("opening");
 int started[7];for(unsigned i=0;i<7;i++)started[i]=-1;
 /* The finite original title lasts34 seconds. Exercise its entire sample,
    including the former17.025-second asset dropout, without debugger stops. */
 for(unsigned time=0;time<2100;time+=2)
  draw_frame(&assets,3,time,true,true,started,0);
 adventure_audio_review_report("title");assets_close(&assets);
 debugf("AUDIO REVIEW PASS\n");
}
#endif

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
