#include <libdragon.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/gl_integration.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "naming.h"
#include "scene.h"
#include "audio.h"
#include "video.h"
#include "duel_bridge.h"
#include "generated/world3d_maps.h"
#include "gen/constants.h"

typedef struct {char value[7];unsigned length,row,column,limit;} NameState;
typedef struct {
 Scene *player;
 unsigned model;
 surface_t depth,canvas;
 NameState painted;
 unsigned painted_blink;
 bool canvas_valid;
} NameAssets;
#if ADVENTURE_DIAGNOSTIC == 20
#include "tests/fps_sweep.h"
static bool naming_uncached;
#define NAME_CLOCK(name) uint64_t name=get_ticks()
#define NAME_NOTE(name,start) fps_sweep_note(name,get_ticks()-(start))
#else
#define NAME_CLOCK(name)
#define NAME_NOTE(name,start)
#endif
static const char *const keys[]={"ABCDEFGHI","JKLMNOPQR","STUVWXYZ!","?&+-.0123","456789"};
static const unsigned row_lengths[]={9,9,9,9,6,3};

static NameAssets assets_open(void)
{
 NameAssets assets={0};
 assets.model=w3_actor_scenes[W3_PLAYER_MODEL]?W3_PLAYER_MODEL:1;
 assets.player=scene_open(w3_actor_scenes[W3_PLAYER_MODEL]?w3_actor_scenes[W3_PLAYER_MODEL]:W3_ACTOR_SCENE,W3_TEXTURE_ROOT);
 assets.depth=n64_video_surface();
 assets.canvas=n64_video_surface();
 assert(assets.player&&assets.depth.buffer&&assets.canvas.buffer);
#if ADVENTURE_DIAGNOSTIC == 10
 static bool measured;
 if(!measured) {
  heap_stats_t stats;sys_get_heap_stats(&stats);
  debugf("VIDEO HEAP stage=naming total=%d used=%d free=%d\n",stats.total,stats.used,stats.total-stats.used);
  measured=true;
 }
#endif
 return assets;
}
static void assets_close(NameAssets *assets)
{
 rspq_wait();scene_close(assets->player);surface_free(&assets->depth);surface_free(&assets->canvas);
}

static void append(NameState *state,char value)
{
 if(!state->limit)return;
 if(state->length<state->limit)state->value[state->length++]=value;
 else state->value[state->length-1]=value; /* Original full-name key behavior. */
 state->value[state->length]=0;
}
static void erase(NameState *state)
{if(state->length)state->value[--state->length]=0;}
static void finalize(NameState *state)
{
 if(!state->length) {
  snprintf(state->value,state->limit+1,"%s","MARK");state->length=(unsigned)strlen(state->value);
 }
}
static void move(NameState *state,int direction)
{
 if(direction==1||direction==3) {
  unsigned length=row_lengths[state->row];
  state->column=(state->column+length+(direction==1?1:-1))%length;
 } else {
  unsigned column=state->column;
  if(state->row==5)column*=3;
  state->row=(state->row+6+(direction==2?1:-1))%6;
  if(state->row==5)column/=3;
  state->column=column<row_lengths[state->row]?column:row_lengths[state->row]-1;
 }
}
static bool choose(NameState *state)
{
 if(state->row<5){append(state,keys[state->row][state->column]);return false;}
 if(state->column==0)append(state,' ');
 else if(state->column==1)erase(state);
 else {finalize(state);return true;}
 return false;
}

static uint32_t color(unsigned r,unsigned g,unsigned b)
{return graphics_make_color(r,g,b,255);}
static void box(surface_t *canvas,int x,int y,int w,int h,uint32_t foreground)
{n64_video_box(canvas,x,y,w,h,foreground);}
static void text(surface_t *canvas,int x,int y,uint32_t foreground,const char *value)
{
 uint32_t shadow=foreground==color(24,48,80)?color(185,199,199):color(16,40,68);
 n64_ui_draw_text(canvas,x+1,y+1,shadow,value,N64_UI_FONT_COMPACT,320-x-1);
 n64_ui_draw_text(canvas,x,y,foreground,value,N64_UI_FONT_COMPACT,320-x);
}
static void border(surface_t *canvas,int x,int y,int w,int h,uint32_t foreground)
{
 box(canvas,x,y,w,2,foreground);box(canvas,x,y+h-2,w,2,foreground);
 box(canvas,x,y,2,h,foreground);box(canvas,x+w-2,y,2,h,foreground);
 if(w>40&&h>30) {
  box(canvas,x+2,y+2,w-4,1,color(255,255,247));box(canvas,x+2,y+2,1,h-4,color(255,255,247));
  box(canvas,x+2,y+h-3,w-4,1,color(169,188,204));box(canvas,x+w-3,y+2,1,h-4,color(169,188,204));
 }
}
static void paint_keyboard(NameAssets *assets,const NameState *state,unsigned frame)
{
 surface_t canvas=assets->canvas;canvas.buffer=CachedAddr(canvas.buffer);
 uint32_t ink=color(24,48,80),paper=color(247,247,222),blue=color(74,115,156),white=color(255,255,247),selected=color(32,72,120);
 graphics_fill_screen(&canvas,0);
 box(&canvas,0,0,320,34,paper);box(&canvas,0,208,320,32,paper);
 text(&canvas,16,18,ink,"What is your name?");
 border(&canvas,12,38,108,160,blue);
 box(&canvas,128,36,180,32,paper);border(&canvas,128,36,180,32,blue);
 for(unsigned i=0;i<6;i++) {
  int x=138+(int)i*26;
  box(&canvas,x,44,18,16,white);
  box(&canvas,x,59,18,1,blue);
  if(i<state->length){char glyph[]={state->value[i],0};text(&canvas,x+5,47,ink,glyph);}
  unsigned insertion=state->length<6?state->length:5;
  if(i==insertion&&(frame/16)%2==0)border(&canvas,x-1,42,20,20,selected);
 }
 box(&canvas,128,76,180,126,paper);border(&canvas,128,76,180,126,blue);
 for(unsigned row=0;row<5;row++)for(unsigned column=0;column<row_lengths[row];column++) {
  int x=138+(int)column*18,y=88+(int)row*18;
  bool current=state->row==row&&state->column==column;
  if(current)box(&canvas,x-4,y-4,16,16,selected);
  char glyph[]={keys[row][column],0};text(&canvas,x,y,current?white:ink,glyph);
 }
 static const char *const actions[]={"Space","Delete","End"};
 for(unsigned i=0;i<3;i++) {
  int x=134+(int)i*56;
  bool current=state->row==5&&state->column==i;
  if(current)box(&canvas,x,182,54,16,selected);
  text(&canvas,x+4,186,current?white:ink,actions[i]);
 }
 text(&canvas,20,219,ink,"A: choose  B: delete  START: End");
}
static void paint_name_strip(surface_t *canvas,const NameState *state,unsigned frame)
{
 uint32_t ink=color(24,48,80),paper=color(247,247,222),blue=color(74,115,156),white=color(255,255,247),selected=color(32,72,120);
 box(canvas,128,36,180,32,paper);border(canvas,128,36,180,32,blue);
 for(unsigned i=0;i<6;i++){
  int x=138+(int)i*26;box(canvas,x,44,18,16,white);box(canvas,x,59,18,1,blue);
  if(i<state->length){char glyph[]={state->value[i],0};text(canvas,x+5,47,ink,glyph);}
  unsigned insertion=state->length<6?state->length:5;
  if(i==insertion&&(frame/16)%2==0)border(canvas,x-1,42,20,20,selected);
 }
}
static void paint_key(surface_t *canvas,unsigned row,unsigned column,bool current)
{
 uint32_t paper=color(247,247,222),ink=color(24,48,80),white=color(255,255,247),selected=color(32,72,120);
 if(row<5){
  int x=138+(int)column*18,y=88+(int)row*18;
  box(canvas,x-4,y-4,16,16,current?selected:paper);
  char glyph[]={keys[row][column],0};text(canvas,x,y,current?white:ink,glyph);
 }else{
  static const char *const actions[]={"Space","Delete","End"};int x=134+(int)column*56;
  box(canvas,x,182,54,16,current?selected:paper);
  text(canvas,x+4,186,current?white:ink,actions[column]);
 }
}
static bool update_keyboard(NameAssets *assets,const NameState *state,unsigned frame)
{
 bool full=!assets->canvas_valid;
#if ADVENTURE_DIAGNOSTIC == 20
 if(naming_uncached)full=true;
#endif
 bool name=full||state->length!=assets->painted.length||
  strcmp(state->value,assets->painted.value)||assets->painted_blink!=(frame/16)%2;
 bool cursor=full||state->row!=assets->painted.row||state->column!=assets->painted.column;
 if(full)paint_keyboard(assets,state,frame);
 else{
  surface_t canvas=assets->canvas;canvas.buffer=CachedAddr(canvas.buffer);
  if(name)paint_name_strip(&canvas,state,frame);
  if(cursor){paint_key(&canvas,assets->painted.row,assets->painted.column,false);
   paint_key(&canvas,state->row,state->column,true);}
 }
 assets->painted=*state;assets->painted_blink=(frame/16)%2;assets->canvas_valid=true;
 return name||cursor;
}
static void draw_keyboard(NameAssets *assets,const NameState *state,unsigned frame)
{
 NAME_CLOCK(start);bool changed=update_keyboard(assets,state,frame);NAME_NOTE("naming_cpu",start);
 NAME_CLOCK(blitting);surface_t canvas=assets->canvas;
 if(changed)data_cache_hit_writeback_invalidate(CachedAddr(canvas.buffer),(unsigned long)canvas.stride*canvas.height);
 rdpq_mode_push();rdpq_set_mode_standard();rdpq_mode_filter(FILTER_POINT);
 rdpq_mode_alphacompare(1);n64_video_blit_physical(&canvas,0,0);rdpq_mode_pop();
 NAME_NOTE("naming_blit",blitting);
}

static surface_t *render_frame(NameAssets *assets,const NameState *state,unsigned frame)
{
 surface_t *display=display_get();NAME_CLOCK(scene_started);
 rdpq_attach(display,&assets->depth);gl_context_begin();scene_begin_opaque();
 glClearColor(.69f,.82f,.86f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 glViewport(n64_video_x(14),n64_video_y(44),n64_video_x(104),n64_video_y(154));
 glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();gluPerspective(34,104.0/154.0,.1,20);
 glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();gluLookAt(0,1.28,4.7,0,1.08,0,0,1,0);
 scene_camera(34,104.0/154.0,.1,20,(const float[]){0,1.28f,4.7f},(const float[]){0,1.08f,0},
  n64_video_x(14),n64_video_y(44),n64_video_x(104),n64_video_y(154));
 glDisable(GL_TEXTURE_2D);glColor4f(.24f,.36f,.46f,1);
 glBegin(GL_TRIANGLE_FAN);glVertex3f(0,-.025f,0);
 for(unsigned i=0;i<=24;i++){float angle=i*6.28318530718f/24;glVertex3f(cosf(angle)*.78f,-.025f,sinf(angle)*.6f);}
 glEnd();
 scene_draw_actor(assets->player,assets->model,0,0,-12+sinf(frame*.025f)*5,0);
 glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);
 glViewport(0,0,n64_video_width,n64_video_height);gl_context_end();scene_frame_end();
 NAME_NOTE("naming_scene_submit",scene_started);
 draw_keyboard(assets,state,frame);NAME_CLOCK(fencing);rdpq_detach_wait();NAME_NOTE("naming_fence",fencing);
 return display;
}
static surface_t *draw_frame(NameAssets *assets,const NameState *state,unsigned frame)
{
 surface_t *display=render_frame(assets,state,frame);n64_video_present(display);
 adventure_audio_poll();return display;
}

void adventure_name_player(char *name,unsigned capacity)
{
 if(!name||!capacity)return;
 if(capacity==1){name[0]=0;return;}
 NameState state={.limit=capacity>7?6:capacity-1};NameAssets assets=assets_open();
 unsigned repeat=0;int previous=-1;
 for(unsigned frame=0;;frame++) {
  joypad_poll();joypad_buttons_t pressed=joypad_get_buttons_pressed(JOYPAD_PORT_1),held=joypad_get_buttons_held(JOYPAD_PORT_1);
  joypad_inputs_t input=joypad_get_inputs(JOYPAD_PORT_1);
  int direction=held.d_up||input.stick_y>35?0:held.d_right||input.stick_x>35?1:held.d_down||input.stick_y< -35?2:held.d_left||input.stick_x< -35?3:-1;
  if(direction!=previous){repeat=0;previous=direction;}
  if(repeat)repeat--;
  if(direction>=0&&!repeat){move(&state,direction);repeat=7;adventure_audio_event(1,SFX_CURSOR);}
  if(pressed.start){state.row=5;state.column=2;adventure_audio_event(1,SFX_CONFIRM);}
  if(pressed.b&&state.length){erase(&state);adventure_audio_event(1,SFX_CANCEL);}
  if(pressed.a) {
   adventure_audio_event(1,SFX_CONFIRM);
   if(choose(&state)) {snprintf(name,capacity,"%s",state.value);assets_close(&assets);return;}
  }
  draw_frame(&assets,&state,frame);
 }
}

struct surface_s *adventure_name_player_preview(void)
{
 NameAssets assets=assets_open();NameState state={.value="MARK",.length=4,.row=1,.column=3,.limit=6};
 surface_t *frame=draw_frame(&assets,&state,0);assets_close(&assets);return frame;
}
#if ADVENTURE_DIAGNOSTIC == 20
#include "tests/fps_sweep.h"
typedef struct {NameAssets assets;NameState state;unsigned activity;} NameSurvey;
static void check_keyboard_pixels(NameAssets *assets,NameAssets *reference,const NameState *state,unsigned frame,unsigned step)
{
 paint_keyboard(reference,state,frame);update_keyboard(assets,state,frame);
 assertf(!memcmp(CachedAddr(reference->canvas.buffer),CachedAddr(assets->canvas.buffer),
  (size_t)assets->canvas.stride*assets->canvas.height),"Naming cache pixels changed at step%u",step);
 assertf(!update_keyboard(assets,state,frame),"Unchanged keyboard repainted");
}
static void naming_cache_diagnostic(NameAssets *assets)
{
 NameAssets reference=*assets;reference.canvas=n64_video_surface();assert(reference.canvas.buffer);
 NameState state={.limit=6};unsigned checks=0;naming_uncached=false;
 for(unsigned length=0;length<=6;length++){
  memcpy(state.value,"ABCDEF",length);state.value[length]=0;state.length=length;
  for(unsigned row=0;row<6;row++)for(unsigned column=0;column<row_lengths[row];column++){
   state.row=row;state.column=column;
   for(unsigned blink=0;blink<2;blink++)check_keyboard_pixels(assets,&reference,&state,blink*16,checks++);
  }
 }
 /* Exercise the actual edit helpers, including full-name replacement,
    deletion, space, default naming, and each supported capacity. */
 for(unsigned limit=1;limit<=6;limit++){
  state=(NameState){.limit=limit};
  for(unsigned i=0;i<12;i++){
   append(&state,(char)('A'+i));check_keyboard_pixels(assets,&reference,&state,i,checks++);
  }
  for(unsigned i=0;i<8;i++){
   erase(&state);check_keyboard_pixels(assets,&reference,&state,16+i,checks++);
  }
  state.row=5;state.column=0;choose(&state);check_keyboard_pixels(assets,&reference,&state,0,checks++);
  state.column=1;choose(&state);check_keyboard_pixels(assets,&reference,&state,16,checks++);
  state.column=2;choose(&state);check_keyboard_pixels(assets,&reference,&state,0,checks++);
 }
 size_t bytes=(size_t)n64_video_width*n64_video_height*2;void *pixels=malloc(bytes);assert(pixels);
 for(unsigned i=0;i<24;i++){
  state=(NameState){.value="MARK",.length=4,.row=i%6,.limit=6};
  state.column=(i*7)%row_lengths[state.row];unsigned frame=i*7;
  if(i%3==0){state.length=6;memcpy(state.value,"ABCDEF",7);}
  else if(i%3==1){erase(&state);}
  for(unsigned mode=0;mode<2;mode++){
   naming_uncached=mode==0;
   surface_t *display=render_frame(mode?assets:&reference,&state,frame);
   if(!mode)memcpy(pixels,UncachedAddr(display->buffer),bytes);
   else assertf(!memcmp(pixels,UncachedAddr(display->buffer),bytes),"Naming RDP frame changed at step%u",i);
   n64_video_present(display);adventure_audio_poll();
  }
 }
 naming_uncached=false;free(pixels);surface_free(&reference.canvas);
 assertf(adventure_naming_check(),"Naming input/anchor rules changed");
 debugf("NAMING CACHE PIXELS PASS cpu_states=%u native_pairs=24\n",checks);
}
static surface_t *naming_survey_draw(void *opaque,unsigned frame)
{
 NameSurvey *ctx=opaque;
 if(ctx->activity&&!(frame%7)){
  static const uint8_t positions[][2]={{0,0},{1,8},{4,5},{5,1},{5,0},{5,2},{3,6},{2,8}};
  unsigned at=(frame/7+4)%8;ctx->state.row=positions[at][0];ctx->state.column=positions[at][1];
  if(ctx->activity==2){if(ctx->state.length==6)erase(&ctx->state);else append(&ctx->state,(char)('A'+frame%26));}
 }
 return draw_frame(&ctx->assets,&ctx->state,frame);
}
void adventure_naming_fps_survey(void)
{
 NameSurvey ctx={.assets=assets_open(),.state={.value="MARK",.length=4,.row=1,.column=3,.limit=6}};
 naming_cache_diagnostic(&ctx.assets);adventure_audio_event(0,MUSIC_TITLESCREEN);
 naming_uncached=true;fps_sweep_case("naming-uncached-idle",naming_survey_draw,&ctx);
 naming_uncached=false;fps_sweep_case("naming-cached-idle",naming_survey_draw,&ctx);
 ctx.activity=1;naming_uncached=true;fps_sweep_case("naming-uncached-busy",naming_survey_draw,&ctx);
 naming_uncached=false;fps_sweep_case("naming-cached-busy",naming_survey_draw,&ctx);
 ctx.activity=2;fps_sweep_case("naming-cached-editing",naming_survey_draw,&ctx);
 assets_close(&ctx.assets);
}
#endif

bool adventure_naming_check(void)
{
 NameState state={.limit=6};finalize(&state);if(strcmp(state.value,"MARK"))return false;
 state=(NameState){.limit=6};for(unsigned i=0;i<7;i++)append(&state,(char)('A'+i));
 if(strcmp(state.value,"ABCDEG"))return false;
 erase(&state);if(strcmp(state.value,"ABCDE"))return false;
 state.row=5;state.column=2;move(&state,0);
 if(state.row!=4||state.column!=5)return false;
 state.row=5;state.column=2;if(!choose(&state)||strcmp(state.value,"ABCDE"))return false;
 return state.length==5&&state.value[6]==0;
}
