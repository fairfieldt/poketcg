#include <libdragon.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dialogue.h"
#include "duel_bridge.h"
#include "audio.h"
#include "game.h"
#include "gen/constants.h"

#define DIALOGUE_COLUMNS 36
#define PAGE_LINES 4
#define PANEL_TOP 160
#define PANEL_HEIGHT 80
typedef struct {char (*line)[37];unsigned count;} TextLines;
typedef struct {unsigned page,visible;} TextPage;
/* Root owns camera and actor presentation. This frame has completed rendering
   but remains reserved and unshown until our overlay is complete. */
extern surface_t *game_render_frame(void);

static TextLines wrap_all(const char *text,unsigned columns)
{
 size_t bound=(text?strlen(text):0)+1;unsigned capacity=bound<32?(unsigned)bound:32;
 TextLines lines={0};
 for(;;) {
  char (*resized)[37]=realloc(lines.line,(size_t)capacity*sizeof(*lines.line));
  assertf(resized,"Dialogue text allocation failed");lines.line=resized;
  lines.count=(unsigned)n64_ui_wrap_columns(text,columns,lines.line,capacity);
  if(lines.count<capacity)return lines;
  unsigned next=capacity*2;if(next>bound)next=(unsigned)bound;
  assertf(next>capacity,"Dialogue text did not fit");capacity=next;
 }
}
static unsigned page_characters(const TextLines *lines,unsigned page)
{
 unsigned result=0;
 for(unsigned i=0;i<PAGE_LINES&&page+i<lines->count;i++)result+=(unsigned)strlen(lines->line[page+i]);
 return result;
}
static bool last_page(const TextLines *lines,const TextPage *page)
{return page->page+PAGE_LINES>=lines->count;}
static void reveal(const TextLines *lines,TextPage *page,unsigned speed)
{
 static const unsigned characters_per_frame[]={1,2,3,6,DIALOGUE_COLUMNS*PAGE_LINES};
 unsigned total=page_characters(lines,page->page);
 page->visible+=characters_per_frame[speed<5?speed:2];
 if(page->visible>total)page->visible=total;
}
static bool advance(const TextLines *lines,TextPage *page)
{
 unsigned total=page_characters(lines,page->page);
 if(page->visible<total){page->visible=total;return false;}
 if(last_page(lines,page))return true;
 page->page+=PAGE_LINES;page->visible=0;return false;
}

static uint32_t color(unsigned r,unsigned g,unsigned b)
{return graphics_make_color(r,g,b,255);}
static void border(surface_t *canvas,int x,int y,int width,int height,uint32_t value)
{
 graphics_draw_box(canvas,x,y,width,2,value);graphics_draw_box(canvas,x,y+height-2,width,2,value);
 graphics_draw_box(canvas,x,y,2,height,value);graphics_draw_box(canvas,x+width-2,y,2,height,value);
}
static void print(surface_t *canvas,int x,int y,uint32_t value,const char *text)
{graphics_set_color(value,0);graphics_draw_text(canvas,x,y,text);}
static void compose(surface_t *frame,surface_t *canvas,int x,int y)
{
 data_cache_hit_writeback_invalidate(canvas->buffer,(unsigned long)canvas->stride*canvas->height);
 rdpq_attach(frame,NULL);rdpq_mode_push();rdpq_set_mode_standard();
 rdpq_mode_filter(FILTER_POINT);rdpq_mode_alphacompare(1);rdpq_tex_blit(canvas,x,y,NULL);
 rdpq_mode_pop();rdpq_detach_wait();
}
static void draw_panel(surface_t *frame,surface_t *storage,const char *speaker,
 const TextLines *lines,const TextPage *page,const char *hint)
{
 surface_t canvas=*storage;canvas.buffer=CachedAddr(canvas.buffer);
 uint32_t ink=color(24,48,80),paper=color(247,247,222),edge=color(74,115,156);
 graphics_fill_screen(&canvas,0);
 graphics_draw_box(&canvas,8,10,304,66,paper);border(&canvas,8,10,304,66,edge);
 if(speaker&&speaker[0]) {
  char clean[35];n64_ui_sanitize_text(speaker,clean,sizeof(clean));
  unsigned width=(unsigned)strlen(clean)*8+16;
  graphics_draw_box(&canvas,16,0,width,18,paper);border(&canvas,16,0,width,18,edge);
  print(&canvas,24,5,ink,clean);
 }
 unsigned remaining=page->visible;
 for(unsigned row=0;row<PAGE_LINES&&page->page+row<lines->count;row++) {
  const char *source=lines->line[page->page+row];unsigned length=(unsigned)strlen(source);
  unsigned visible=remaining<length?remaining:length;char text[37];memcpy(text,source,visible);text[visible]=0;
  print(&canvas,16,24+(int)row*10,ink,text);
  remaining=remaining>length?remaining-length:0;
 }
 if(hint)print(&canvas,304-(int)strlen(hint)*8,64,ink,hint);
 compose(frame,&canvas,0,PANEL_TOP);
}

void adventure_story_message(const char *speaker,const char *text,unsigned speed)
{
 if(!text||!text[0])return;
 TextLines lines=wrap_all(text,DIALOGUE_COLUMNS);TextPage page={0};
 surface_t canvas=surface_alloc(FMT_RGBA16,320,PANEL_HEIGHT);assert(canvas.buffer);
 for(;;) {
  joypad_poll();joypad_buttons_t pressed=joypad_get_buttons_pressed(JOYPAD_PORT_1);
  if((pressed.a||pressed.b)&&advance(&lines,&page))break;
  reveal(&lines,&page,speed);
  surface_t *frame=game_render_frame();assert(frame);
  draw_panel(frame,&canvas,speaker,&lines,&page,"A/B: continue");display_show(frame);adventure_audio_poll();
 }
 surface_free(&canvas);free(lines.line);
}

typedef struct {TextLines text;unsigned height;} ChoiceItem;
static void draw_choices(surface_t *frame,surface_t *storage,const ChoiceItem *items,
 unsigned count,unsigned selected,unsigned bottom)
{
 surface_t canvas=*storage;canvas.buffer=CachedAddr(canvas.buffer);
 uint32_t ink=color(24,48,80),paper=color(247,247,222),edge=color(74,115,156),highlight=color(32,72,120),white=color(255,255,247);
 graphics_fill_screen(&canvas,paper);border(&canvas,0,0,canvas.width,canvas.height,edge);
 unsigned y=6;
 for(unsigned item=0;item<count;item++) {
  if(item==selected)graphics_draw_box(&canvas,4,y-2,canvas.width-8,items[item].height,highlight);
  if(item==selected)print(&canvas,7,y+2,white,">");
  for(unsigned row=0;row<items[item].text.count;row++)
   print(&canvas,18,y+2+(int)row*12,item==selected?white:ink,items[item].text.line[row]);
  y+=items[item].height;
 }
 compose(frame,&canvas,128,(int)bottom-(int)canvas.height);
}

int adventure_story_choice(const char *question,const char *const *items,
 unsigned count,int initial,bool cancel)
{
 assert(count>=1&&count<=4&&items);
 TextLines lines=wrap_all(question,DIALOGUE_COLUMNS);TextPage page={0};
 ChoiceItem choices[4]={0};unsigned height=12;
 for(unsigned i=0;i<count;i++) {
  choices[i].text=wrap_all(items[i],20);choices[i].height=choices[i].text.count*12+4;
  if(choices[i].height<16)choices[i].height=16;
  height+=choices[i].height;
 }
 assertf(height<=136,"Story choice is too tall");
 surface_t canvas=surface_alloc(FMT_RGBA16,320,PANEL_HEIGHT),popup=surface_alloc(FMT_RGBA16,184,height);
 assert(canvas.buffer&&popup.buffer);
 unsigned current=initial>=0&&(unsigned)initial<count?(unsigned)initial:0,cooldown=0;
 int result=-1,previous=-1;bool open=false;
 for(;;) {
  joypad_poll();joypad_buttons_t pressed=joypad_get_buttons_pressed(JOYPAD_PORT_1),held=joypad_get_buttons_held(JOYPAD_PORT_1);
  joypad_inputs_t input=joypad_get_inputs(JOYPAD_PORT_1);
  if(open) {
   if(pressed.a){result=(int)current;adventure_audio_event(1,SFX_CONFIRM);break;}
   if(pressed.b&&cancel){adventure_audio_event(1,SFX_CANCEL);break;}
   int direction=held.d_up||input.stick_y>35?-1:held.d_down||input.stick_y< -35?1:0;
   if(direction!=previous){cooldown=0;previous=direction;}
   if(cooldown)cooldown--;
   if(direction&&!cooldown){current=(current+count+direction)%count;cooldown=7;adventure_audio_event(1,SFX_CURSOR);}
  } else {
   if(pressed.a||pressed.b)advance(&lines,&page);
   reveal(&lines,&page,game.text_speed);
   if(last_page(&lines,&page)&&page.visible==page_characters(&lines,page.page))open=true;
  }
  surface_t *frame=game_render_frame();assert(frame);
  if(lines.count)draw_panel(frame,&canvas,NULL,&lines,&page,open?(cancel?"A: choose  B: back":"A: choose"):"A/B: continue");
  if(open)draw_choices(frame,&popup,choices,count,current,lines.count?166:222);
  display_show(frame);adventure_audio_poll();
 }
 surface_free(&popup);surface_free(&canvas);free(lines.line);
 for(unsigned i=0;i<count;i++)free(choices[i].text.line);
 return result;
}

void adventure_duel_transition(bool entering)
{
 for(unsigned step=0;step<12;step++) {
  surface_t *frame=game_render_frame();assert(frame);
  unsigned extent=(entering?step:11-step)*120/11;
  rdpq_attach(frame,NULL);rdpq_mode_push();rdpq_set_mode_fill(RGBA32(0,0,0,255));
  if(extent){rdpq_fill_rectangle(0,0,320,extent);rdpq_fill_rectangle(0,240-extent,320,240);}
  rdpq_mode_pop();rdpq_detach_wait();display_show(frame);adventure_audio_poll();
 }
}

struct surface_s *adventure_story_message_preview(const char *speaker,const char *text)
{
 TextLines lines=wrap_all(text,DIALOGUE_COLUMNS);TextPage page={.visible=page_characters(&lines,0)};
 surface_t canvas=surface_alloc(FMT_RGBA16,320,PANEL_HEIGHT);assert(canvas.buffer);
 surface_t *frame=game_render_frame();assert(frame);
 draw_panel(frame,&canvas,speaker,&lines,&page,"A/B: continue");display_show(frame);
 adventure_audio_poll();surface_free(&canvas);free(lines.line);return frame;
}

bool adventure_dialogue_check(void)
{
 char *long_text=malloc(10004);if(!long_text)return false;
 memset(long_text,'A',10000);memcpy(long_text+10000,"END",4);
 TextLines lines=wrap_all(long_text,DIALOGUE_COLUMNS);free(long_text);
 bool valid=lines.count>256&&strstr(lines.line[lines.count-1],"END");free(lines.line);
 lines=wrap_all("One\nTwo\nThree\nFour\nFive",DIALOGUE_COLUMNS);TextPage page={0};
 reveal(&lines,&page,0);valid=valid&&page.visible==1&&!advance(&lines,&page)&&page.visible==15;
 valid=valid&&!advance(&lines,&page)&&page.page==4&&page.visible==0;
 reveal(&lines,&page,4);valid=valid&&page.visible==4&&advance(&lines,&page);
 free(lines.line);return valid;
}
