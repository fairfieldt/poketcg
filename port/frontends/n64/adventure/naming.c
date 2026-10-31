#include <libdragon.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/gl_integration.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "naming.h"
#include "scene.h"
#include "audio.h"
#include "generated/world3d_maps.h"
#include "gen/constants.h"

typedef struct {
 Scene *player;
 unsigned model;
 surface_t depth,canvas;
} NameAssets;
typedef struct {char value[7];unsigned length,row,column,limit;} NameState;
static const char *const keys[]={"ABCDEFGHI","JKLMNOPQR","STUVWXYZ!","?&+-.0123","456789"};
static const unsigned row_lengths[]={9,9,9,9,6,3};

static NameAssets assets_open(void)
{
 NameAssets assets={0};
 assets.model=w3_actor_scenes[W3_PLAYER_MODEL]?W3_PLAYER_MODEL:1;
 assets.player=scene_open(w3_actor_scenes[W3_PLAYER_MODEL]?w3_actor_scenes[W3_PLAYER_MODEL]:W3_ACTOR_SCENE,W3_TEXTURE_ROOT);
 assets.depth=surface_alloc(FMT_RGBA16,320,240);
 assets.canvas=surface_alloc(FMT_RGBA16,320,240);
 assert(assets.player&&assets.depth.buffer&&assets.canvas.buffer);return assets;
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
{graphics_draw_box(canvas,x,y,w,h,foreground);}
static void text(surface_t *canvas,int x,int y,uint32_t foreground,const char *value)
{graphics_set_color(foreground,0);graphics_draw_text(canvas,x,y,value);}
static void border(surface_t *canvas,int x,int y,int w,int h,uint32_t foreground)
{
 box(canvas,x,y,w,2,foreground);box(canvas,x,y+h-2,w,2,foreground);
 box(canvas,x,y,2,h,foreground);box(canvas,x+w-2,y,2,h,foreground);
}
static void draw_keyboard(NameAssets *assets,const NameState *state,unsigned frame)
{
 surface_t canvas=assets->canvas;canvas.buffer=CachedAddr(canvas.buffer);
 uint32_t ink=color(24,48,80),paper=color(247,247,222),blue=color(74,115,156),white=color(255,255,247),selected=color(32,72,120);
 graphics_fill_screen(&canvas,0);
 box(&canvas,0,0,320,34,paper);box(&canvas,0,208,320,32,paper);
 text(&canvas,16,14,ink,"What is your name?");
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
 data_cache_hit_writeback_invalidate(canvas.buffer,(unsigned long)canvas.stride*canvas.height);
 rdpq_mode_push();rdpq_set_mode_standard();rdpq_mode_filter(FILTER_POINT);
 rdpq_mode_alphacompare(1);rdpq_tex_blit(&canvas,0,0,NULL);rdpq_mode_pop();
}

static surface_t *draw_frame(NameAssets *assets,const NameState *state,unsigned frame)
{
 surface_t *display=display_get();rdpq_attach(display,&assets->depth);gl_context_begin();
 glClearColor(.69f,.82f,.86f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 glViewport(14,44,104,154);
 glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();gluPerspective(34,104.0/154.0,.1,20);
 glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();gluLookAt(0,1.28,4.7,0,1.08,0,0,1,0);
 glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS_INTERPENETRATING_N64);glDisable(GL_LIGHTING);glDisable(GL_CULL_FACE);
 glDisable(GL_TEXTURE_2D);glColor4f(.24f,.36f,.46f,1);
 glBegin(GL_TRIANGLE_FAN);glVertex3f(0,-.025f,0);
 for(unsigned i=0;i<=24;i++){float angle=i*6.28318530718f/24;glVertex3f(cosf(angle)*.78f,-.025f,sinf(angle)*.6f);}
 glEnd();
 scene_draw_actor(assets->player,assets->model,0,0,-12+sinf(frame*.025f)*5,0);
 glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);
 glViewport(0,0,320,240);gl_context_end();
 draw_keyboard(assets,state,frame);rdpq_detach_wait();display_show(display);
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
