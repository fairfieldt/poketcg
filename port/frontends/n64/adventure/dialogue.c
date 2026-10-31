#include <libdragon.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dialogue.h"
#include "duel_bridge.h"
#include "audio.h"
#include "video.h"
#include "game.h"
#include "gen/constants.h"

#define DIALOGUE_COLUMNS 36
#define PAGE_LINES 4
#define PANEL_TOP 160
#define PANEL_HEIGHT 80
typedef struct {char (*line)[37];unsigned count;} TextLines;
typedef struct {unsigned page,visible;} TextPage;
typedef struct {
 const void *lines;unsigned count,page,visible;char speaker[35],hint[32];bool valid,speaker_present;
} PanelPaint;
typedef struct {const void *items;unsigned count,selected;bool valid;} ChoicePaint;
#if ADVENTURE_DIAGNOSTIC == 20
#include "tests/fps_sweep.h"
static bool survey_uncached;
#define PAINT_CLOCK(name) uint64_t name=get_ticks()
#define PAINT_NOTE(name,start) fps_sweep_note(name,get_ticks()-(start))
#else
#define PAINT_CLOCK(name)
#define PAINT_NOTE(name,start)
#endif
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
 n64_video_box(canvas,x,y,width,2,value);n64_video_box(canvas,x,y+height-2,width,2,value);
 n64_video_box(canvas,x,y,2,height,value);n64_video_box(canvas,x+width-2,y,2,height,value);
 n64_video_box(canvas,x+2,y+2,width-4,1,color(255,255,247));
 n64_video_box(canvas,x+2,y+2,1,height-4,color(255,255,247));
 n64_video_box(canvas,x+2,y+height-3,width-4,1,color(169,188,204));
 n64_video_box(canvas,x+width-3,y+2,1,height-4,color(169,188,204));
}
static void print(surface_t *canvas,int x,int y,uint32_t value,const char *text)
{
 uint32_t shadow=value==color(24,48,80)?color(185,199,199):color(16,40,68);
 n64_ui_draw_text(canvas,x+1,y+1,shadow,text,N64_UI_FONT_COMPACT,320-x-1);
 n64_ui_draw_text(canvas,x,y,value,text,N64_UI_FONT_COMPACT,320-x);
}
static void compose(surface_t *frame,surface_t *canvas,int x,int y,bool changed)
{
 if(changed)data_cache_hit_writeback_invalidate(CachedAddr(canvas->buffer),(unsigned long)canvas->stride*canvas->height);
 rdpq_attach(frame,NULL);rdpq_mode_push();rdpq_set_mode_standard();
 rdpq_mode_filter(FILTER_POINT);rdpq_mode_alphacompare(1);n64_video_blit_physical(canvas,x,y);
 rdpq_mode_pop();rdpq_detach_wait();
}
static void paint_panel(surface_t *storage,const char *speaker,
 const TextLines *lines,const TextPage *page,const char *hint)
{
 surface_t canvas=*storage;canvas.buffer=CachedAddr(canvas.buffer);
 uint32_t ink=color(24,48,80),paper=color(247,247,222),edge=color(74,115,156);
 graphics_fill_screen(&canvas,0);
 n64_video_box(&canvas,8,18,304,58,paper);border(&canvas,8,18,304,58,edge);
 if(speaker&&speaker[0]) {
  char clean[35];n64_ui_sanitize_text(speaker,clean,sizeof(clean));
  unsigned width=(unsigned)strlen(clean)*8+16;
  n64_video_box(&canvas,16,0,width,17,paper);border(&canvas,16,0,width,17,edge);
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
}
static bool update_panel(surface_t *storage,const char *speaker,const TextLines *lines,
 const TextPage *page,const char *hint,PanelPaint *paint)
{
 char clean[35];n64_ui_sanitize_text(speaker?speaker:"",clean,sizeof(clean));
 bool reset=!paint->valid||paint->lines!=lines->line||paint->count!=lines->count||
  paint->page!=page->page||paint->visible>page->visible||
  paint->speaker_present!=(speaker&&speaker[0])||
  strcmp(paint->speaker,clean)||strcmp(paint->hint,hint?hint:"");
#if ADVENTURE_DIAGNOSTIC == 20
 if(survey_uncached)reset=true;
#endif
 bool changed=reset||paint->visible!=page->visible;
 if(reset)paint_panel(storage,speaker,lines,page,hint);
 else if(changed){
  surface_t canvas=*storage;canvas.buffer=CachedAddr(canvas.buffer);
  unsigned offset=0;uint32_t ink=color(24,48,80);
  /* Compact glyphs advance eight logical pixels. Their one-pixel shade
     extends right/down only, so appending a run cannot overwrite old ink.
     The four rows have a ten-pixel pitch and never overlap each other. */
  for(unsigned row=0;row<PAGE_LINES&&page->page+row<lines->count;row++){
   const char *source=lines->line[page->page+row];unsigned length=(unsigned)strlen(source);
   unsigned before=paint->visible>offset?paint->visible-offset:0;
   unsigned after=page->visible>offset?page->visible-offset:0;
   if(before>length)before=length;
   if(after>length)after=length;
   if(after>before){char text[37];memcpy(text,source+before,after-before);text[after-before]=0;
    print(&canvas,16+(int)before*8,24+(int)row*10,ink,text);}
   offset+=length;
  }
 }
 paint->lines=lines->line;paint->count=lines->count;paint->page=page->page;
 paint->visible=page->visible;snprintf(paint->speaker,sizeof(paint->speaker),"%s",clean);
 paint->speaker_present=speaker&&speaker[0];
 snprintf(paint->hint,sizeof(paint->hint),"%s",hint?hint:"");paint->valid=true;
 return changed;
}
static void draw_panel(surface_t *frame,surface_t *storage,const char *speaker,
 const TextLines *lines,const TextPage *page,const char *hint,PanelPaint *paint)
{
 PAINT_CLOCK(start);bool changed=update_panel(storage,speaker,lines,page,hint,paint);
 PAINT_NOTE("panel_cpu",start);PAINT_CLOCK(composing);
 compose(frame,storage,0,PANEL_TOP,changed);PAINT_NOTE("panel_compose",composing);
}

void adventure_story_message(const char *speaker,const char *text,unsigned speed)
{
 if(!text||!text[0])return;
 TextLines lines=wrap_all(text,DIALOGUE_COLUMNS);TextPage page={0};PanelPaint paint={0};
 surface_t canvas=surface_alloc(FMT_RGBA16,n64_video_width,n64_video_y(PANEL_HEIGHT));assert(canvas.buffer);
 for(;;) {
  joypad_poll();joypad_buttons_t pressed=joypad_get_buttons_pressed(JOYPAD_PORT_1);
  if((pressed.a||pressed.b)&&advance(&lines,&page))break;
  reveal(&lines,&page,speed);
  surface_t *frame=game_render_frame();assert(frame);
  draw_panel(frame,&canvas,speaker,&lines,&page,"A/B: continue",&paint);n64_video_present(frame);adventure_audio_poll();
 }
 surface_free(&canvas);free(lines.line);
}

typedef struct {TextLines text;unsigned height;} ChoiceItem;
static void paint_choices(surface_t *storage,const ChoiceItem *items,unsigned count,unsigned selected)
{
 surface_t canvas=*storage;canvas.buffer=CachedAddr(canvas.buffer);
 uint32_t ink=color(24,48,80),paper=color(247,247,222),edge=color(74,115,156),highlight=color(32,72,120),white=color(255,255,247);
 unsigned logical_height=12;
 for(unsigned item=0;item<count;item++)logical_height+=items[item].height;
 graphics_fill_screen(&canvas,paper);border(&canvas,0,0,184,(int)logical_height,edge);
 unsigned y=6;
 for(unsigned item=0;item<count;item++) {
  if(item==selected)n64_video_box(&canvas,4,y-2,176,items[item].height,highlight);
  if(item==selected)print(&canvas,7,y+2,white,">");
  for(unsigned row=0;row<items[item].text.count;row++)
   print(&canvas,18,y+2+(int)row*12,item==selected?white:ink,items[item].text.line[row]);
  y+=items[item].height;
 }
}
static bool update_choices(surface_t *storage,const ChoiceItem *items,
 unsigned count,unsigned selected,ChoicePaint *paint)
{
 bool changed=!paint->valid||paint->items!=items||paint->count!=count||paint->selected!=selected;
#if ADVENTURE_DIAGNOSTIC == 20
 if(survey_uncached)changed=true;
#endif
 if(changed)paint_choices(storage,items,count,selected);
 paint->items=items;paint->count=count;paint->selected=selected;paint->valid=true;
 return changed;
}
static void draw_choices(surface_t *frame,surface_t *storage,const ChoiceItem *items,
 unsigned count,unsigned selected,unsigned bottom,ChoicePaint *paint)
{
 PAINT_CLOCK(start);bool changed=update_choices(storage,items,count,selected,paint);
 unsigned logical_height=12;for(unsigned i=0;i<count;i++)logical_height+=items[i].height;
 PAINT_NOTE("choices_cpu",start);PAINT_CLOCK(composing);
 compose(frame,storage,128,(int)bottom-(int)logical_height,changed);PAINT_NOTE("choices_compose",composing);
}

int adventure_story_choice(const char *question,const char *const *items,
 unsigned count,int initial,bool cancel)
{
 assert(count>=1&&count<=4&&items);
 TextLines lines=wrap_all(question,DIALOGUE_COLUMNS);TextPage page={0};PanelPaint paint={0};ChoicePaint choice_paint={0};
 ChoiceItem choices[4]={0};unsigned height=12;
 for(unsigned i=0;i<count;i++) {
  choices[i].text=wrap_all(items[i],20);choices[i].height=choices[i].text.count*12+4;
  if(choices[i].height<16)choices[i].height=16;
  height+=choices[i].height;
 }
 assertf(height<=136,"Story choice is too tall");
 surface_t canvas=surface_alloc(FMT_RGBA16,n64_video_width,n64_video_y(PANEL_HEIGHT)),popup=surface_alloc(FMT_RGBA16,n64_video_x(184),n64_video_y(height));
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
  if(lines.count)draw_panel(frame,&canvas,NULL,&lines,&page,open?(cancel?"A: choose  B: back":"A: choose"):"A/B: continue",&paint);
  if(open)draw_choices(frame,&popup,choices,count,current,lines.count?166:222,&choice_paint);
  n64_video_present(frame);adventure_audio_poll();
 }
 surface_free(&popup);surface_free(&canvas);free(lines.line);
 for(unsigned i=0;i<count;i++)free(choices[i].text.line);
 return result;
}

void adventure_duel_transition(bool entering)
{
 for(unsigned step=0;step<16;step++) {
  surface_t *frame=game_render_frame();assert(frame);
  unsigned progress=entering?(step<12?step:11):(step<4?0:step-4);
  float t=progress/11.0f,inverse=1-t;
  unsigned extent=(unsigned)(120*(entering?1-inverse*inverse:inverse*inverse)+.5f);
  rdpq_attach(frame,NULL);rdpq_mode_push();rdpq_set_mode_fill(RGBA32(0,0,0,255));
  if(extent){rdpq_fill_rectangle(0,0,n64_video_width,n64_video_y(extent));rdpq_fill_rectangle(0,n64_video_y(240-extent),n64_video_width,n64_video_height);}
  rdpq_mode_pop();rdpq_detach_wait();n64_video_present(frame);adventure_audio_poll();
 }
}

struct surface_s *adventure_story_message_preview(const char *speaker,const char *text)
{
 TextLines lines=wrap_all(text,DIALOGUE_COLUMNS);TextPage page={.visible=page_characters(&lines,0)};PanelPaint paint={0};
 surface_t canvas=surface_alloc(FMT_RGBA16,n64_video_width,n64_video_y(PANEL_HEIGHT));assert(canvas.buffer);
 surface_t *frame=game_render_frame();assert(frame);
#if ADVENTURE_DIAGNOSTIC == 10
 static bool measured;
 if(!measured) {
  heap_stats_t stats;sys_get_heap_stats(&stats);
  debugf("VIDEO HEAP stage=dialogue total=%d used=%d free=%d\n",stats.total,stats.used,stats.total-stats.used);
  measured=true;
 }
#endif
 draw_panel(frame,&canvas,speaker,&lines,&page,"A/B: continue",&paint);n64_video_present(frame);
 adventure_audio_poll();surface_free(&canvas);free(lines.line);return frame;
}

#if ADVENTURE_DIAGNOSTIC == 12
/* Camera trials use the production panel over a controlled scene frame. */
void adventure_camera_dialogue_overlay(surface_t *frame,const char *speaker,const char *text)
{
 TextLines lines=wrap_all(text,DIALOGUE_COLUMNS);
 TextPage page={.visible=page_characters(&lines,0)};
 PanelPaint paint={0};
 surface_t canvas=surface_alloc(FMT_RGBA16,n64_video_width,n64_video_y(PANEL_HEIGHT));
 assert(canvas.buffer);
 draw_panel(frame,&canvas,speaker,&lines,&page,"A/B: continue",&paint);
 surface_free(&canvas);free(lines.line);
}
#endif

#if ADVENTURE_DIAGNOSTIC == 20
#include "tests/fps_sweep.h"
typedef struct {
 surface_t canvas,popup;TextLines lines;TextPage page;
 PanelPaint paint;ChoicePaint choice_paint;
 ChoiceItem items[3];unsigned count;bool revealing;const char *speaker;
} DialogueSurvey;
static surface_t *dialogue_survey_draw(void *opaque,unsigned frame)
{
 DialogueSurvey *ctx=opaque;surface_t *display=game_render_frame();assert(display);
 if(ctx->revealing)ctx->page.visible=(frame*3)% (page_characters(&ctx->lines,0)+1);
 draw_panel(display,&ctx->canvas,ctx->speaker,&ctx->lines,&ctx->page,
  ctx->count?"A: choose  B: back":"A/B: continue",&ctx->paint);
 if(ctx->count)draw_choices(display,&ctx->popup,ctx->items,ctx->count,(frame/24)%ctx->count,166,&ctx->choice_paint);
 return display;
}
void adventure_dialogue_fps_survey(const char *prefix)
{
 survey_uncached=strstr(prefix,"uncached")!=NULL;
 /* The caller establishes each real room, actor pair and conversation camera.
    Use source dialogue and the production wrappers; allocations stay outside
    the sampled frame callback, just as in the interactive functions above. */
 char text[1024],name[96];game_story_format(0x05dd,text,sizeof(text));
 DialogueSurvey ctx={.lines=wrap_all(text,DIALOGUE_COLUMNS),
  .canvas=surface_alloc(FMT_RGBA16,n64_video_width,n64_video_y(PANEL_HEIGHT)),
  .speaker=strstr(prefix,"ronald")?"Ronald":strstr(prefix,"sam")?"Sam":"Dr. Mason"};
 assert(ctx.canvas.buffer);ctx.page.visible=page_characters(&ctx.lines,0);
 snprintf(name,sizeof(name),"%s-dialogue-full",prefix);fps_sweep_case(name,dialogue_survey_draw,&ctx);
 ctx.revealing=true;snprintf(name,sizeof(name),"%s-dialogue-reveal",prefix);
 fps_sweep_case(name,dialogue_survey_draw,&ctx);ctx.revealing=false;ctx.speaker=NULL;
 ctx.page.visible=page_characters(&ctx.lines,0);
 static const char *const two[]={"Yes","No"};
 static const char *const three[]={"Charmander & Friends","Squirtle & Friends","Bulbasaur & Friends"};
 for(unsigned count=2;count<=3;count++) {
  unsigned height=12;ctx.count=count;
  for(unsigned i=0;i<count;i++) {
   ctx.items[i].text=wrap_all(count==2?two[i]:three[i],20);
   ctx.items[i].height=ctx.items[i].text.count*12+4;
   if(ctx.items[i].height<16)ctx.items[i].height=16;
   height+=ctx.items[i].height;
  }
  ctx.popup=surface_alloc(FMT_RGBA16,n64_video_x(184),n64_video_y(height));assert(ctx.popup.buffer);
  snprintf(name,sizeof(name),"%s-choice-%u",prefix,count);fps_sweep_case(name,dialogue_survey_draw,&ctx);
  surface_free(&ctx.popup);for(unsigned i=0;i<count;i++)free(ctx.items[i].text.line);
 }
 surface_free(&ctx.canvas);free(ctx.lines.line);
 survey_uncached=false;
}

static void cache_test_copy(surface_t *dest,const surface_t *source)
{
 rdpq_attach(dest,NULL);rdpq_mode_push();rdpq_set_mode_copy(false);
 rdpq_set_scissor(0,0,n64_video_width,n64_video_height);
 rdpq_tex_blit(source,0,0,NULL);rdpq_mode_pop();rdpq_detach_wait();
}
bool adventure_dialogue_cache_diagnostic(void)
{
 survey_uncached=false;
 TextLines lines=wrap_all("ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789\n"
  "Wavy glyphs: gypq jill! MARK?\nThird line has words and spaces.\n"
  "Fourth line.\nA second page begins here.\nIts rows also reveal gradually.\n"
  "Letters WMWM, punctuation?!\nThe last line ends here.",DIALOGUE_COLUMNS);
 surface_t expected=surface_alloc(FMT_RGBA16,n64_video_width,n64_video_y(PANEL_HEIGHT));
 surface_t actual=surface_alloc(FMT_RGBA16,n64_video_width,n64_video_y(PANEL_HEIGHT));
 assert(expected.buffer&&actual.buffer);PanelPaint cached={0};TextPage page={0};
 unsigned checks=0;
 for(unsigned i=0;i<320;i++){
  char speaker[35],hint[32];snprintf(speaker,sizeof(speaker),"%s",i%70<35?"Dr. Mason":"Sam");
  snprintf(hint,sizeof(hint),"%s",i%50<25?"A/B: continue":"A: choose  B: back");
  page.page=(i/80%2)*4;unsigned total=page_characters(&lines,page.page);
  page.visible=i%80<60?(i%80)*3:total;if(page.visible>total)page.visible=total;
  paint_panel(&expected,speaker,&lines,&page,hint);
  update_panel(&actual,speaker,&lines,&page,hint,&cached);
  assertf(!memcmp(CachedAddr(expected.buffer),CachedAddr(actual.buffer),
   (size_t)expected.stride*expected.height),"Panel cache pixels changed at step%u",i);
  assertf(!update_panel(&actual,speaker,&lines,&page,hint,&cached),"Unchanged panel repainted");checks++;
 }
 surface_free(&expected);surface_free(&actual);
 ChoiceItem items[4]={0};static const char *const options[]={"Yes","No", "Charmander & Friends","A longer option wraps onto another line"};
 for(unsigned i=0;i<4;i++){
  items[i].text=wrap_all(options[i],20);items[i].height=items[i].text.count*12+4;
  if(items[i].height<16)items[i].height=16;
 }
 expected=surface_alloc(FMT_RGBA16,n64_video_x(184),n64_video_y(136));
 actual=surface_alloc(FMT_RGBA16,n64_video_x(184),n64_video_y(136));
 ChoicePaint choice={0};assert(expected.buffer&&actual.buffer);
 for(unsigned i=0;i<160;i++){
  unsigned count=1+(i/20)%4,selected=(i/3)%count;
  paint_choices(&expected,items,count,selected);update_choices(&actual,items,count,selected,&choice);
  assertf(!memcmp(CachedAddr(expected.buffer),CachedAddr(actual.buffer),
   (size_t)expected.stride*expected.height),"Choice cache pixels changed at step%u",i);
  assertf(!update_choices(&actual,items,count,selected,&choice),"Unchanged choice repainted");checks++;
 }
 surface_free(&expected);surface_free(&actual);

 /* Compare actual completed RDP framebuffers over one immutable 3D snapshot.
    Each displayed frame is acquired, fenced, compared, and only then shown. */
 surface_t still=n64_video_surface();surface_t *world=game_render_frame();
 cache_test_copy(&still,world);n64_video_present(world);adventure_audio_poll();
 size_t bytes=(size_t)still.stride*still.height;void *reference=malloc(bytes);assert(reference);
 surface_t panels[2],popups[2];PanelPaint panel_state[2]={{0}};ChoicePaint popup_state[2]={{0}};
 unsigned popup_height=12;for(unsigned i=0;i<3;i++)popup_height+=items[i].height;
 for(unsigned i=0;i<2;i++){
  panels[i]=surface_alloc(FMT_RGBA16,n64_video_width,n64_video_y(PANEL_HEIGHT));
  popups[i]=surface_alloc(FMT_RGBA16,n64_video_x(184),n64_video_y(popup_height));
  assert(panels[i].buffer&&popups[i].buffer);
 }
 for(unsigned i=0;i<24;i++){
  page.page=(i/12)*4;unsigned total=page_characters(&lines,page.page);
  page.visible=(i%12)*15;if(page.visible>total)page.visible=total;
  for(unsigned mode=0;mode<2;mode++){
   survey_uncached=mode==0;surface_t *frame=display_get();cache_test_copy(frame,&still);
   draw_panel(frame,&panels[mode],"Sam",&lines,&page,"A: choose  B: back",&panel_state[mode]);
   draw_choices(frame,&popups[mode],items,3,(i/4)%3,166,&popup_state[mode]);
   if(!mode)memcpy(reference,UncachedAddr(frame->buffer),bytes);
   else assertf(!memcmp(reference,UncachedAddr(frame->buffer),bytes),"RDP dialogue cache frame changed at step%u",i);
   n64_video_present(frame);adventure_audio_poll();
  }
 }
 survey_uncached=false;
 for(unsigned i=0;i<2;i++){surface_free(&panels[i]);surface_free(&popups[i]);}
 surface_free(&still);free(reference);free(lines.line);
 for(unsigned i=0;i<4;i++)free(items[i].text.line);
 debugf("DIALOGUE CACHE PIXELS PASS cpu_states=%u native_pairs=24\n",checks);
 return adventure_dialogue_check();
}
#endif

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
