/* Whole-canvas reuse must match fresh drawing as controls, engine state,
 * transient effects and nested screens change. */
#include <stdlib.h>
static void *cache_test_malloc(size_t size);
#define malloc cache_test_malloc
#ifndef TCGN64_HOST_TEST
#define TCGN64_HOST_TEST
#endif
#define TCGN64_RETAINED_UI_TEST
#define display_get cache_test_display_get
#define display_show cache_test_display_show
#define graphics_make_color cache_test_color
#define graphics_fill_screen cache_test_clear
#define graphics_draw_box cache_test_box
#define graphics_set_color cache_test_set_color
#define graphics_draw_text cache_test_text
#define main tcgn64_frontend_main
#include "../tcgn64.c"
#undef main
#undef malloc

static void *cache_test_malloc(size_t size) {return malloc(size);}

static uint32_t foreground;
uint32_t cache_test_color(int r,int g,int b,int a)
{
 uint32_t color=((r>>3)<<11)|((g>>3)<<6)|((b>>3)<<1)|(a>>7);return color|(color<<16);
}
void cache_test_box(surface_t *surface,int x,int y,int width,int height,uint32_t color)
{
 assert(x>=0&&y>=0&&x+width<=surface->width&&y+height<=surface->height);
 for(int row=y;row<y+height;row++) {
  uint16_t *out=(uint16_t *)((uint8_t *)surface->buffer+row*surface->stride);
  for(int col=x;col<x+width;col++)out[col]=(uint16_t)color;
 }
}
void cache_test_clear(surface_t *surface,uint32_t color)
{cache_test_box(surface,0,0,surface->width,surface->height,color);}
void cache_test_set_color(uint32_t color,uint32_t background)
{(void)background;foreground=color;}
void cache_test_text(surface_t *surface,int x,int y,const char *text)
{
 for(;*text;text++,x+=8)for(unsigned row=0;row<8;row++) {
  unsigned bits=mock_font_row((unsigned char)*text,row);
  for(unsigned col=0;col<8;col++)if(bits&(0x80>>col))cache_test_box(surface,x+col,y+row,1,1,foreground);
 }
}


static surface_t *display_target;
surface_t *cache_test_display_get(void){return display_target;}
void cache_test_display_show(surface_t *surface){(void)surface;}
static surface_t actual,reference;
static unsigned checks,draws,reuses;
static void page(unsigned kind,unsigned cursor)
{
 if(kind==0)draw_card_view_page(ELECTABUZZ_LV35,TCG_PLAYER,-1,(int)cursor,true);
 else if(kind==1) {
  static const uint8_t cards[]={0,1,2,3,4,5,6,7,8,9};int top=0;
  draw_card_list_screen("Mark's Hand",NULL,cards,10,TCG_PLAYER,(int)cursor,&top);
  draw_textbox_hint("A: select  B: back  START: info");
 } else if(kind==2) {draw_practice_reading((int)cursor);draw_practice_continue((int)cursor);}
 else {
  static const char *const labels[]={"Hand","Attack","Check","Pokemon Power","Retreat","Done"};int top=0;
  draw_list(10,8,300,10,"Choose",6,(int)cursor,&top,label_text,(void *)labels);draw_textbox();
 }
}
static void frame(UiCanvasReuse *reuse,unsigned kind,unsigned cursor,bool expected_redraw)
{
 uint64_t key=((uint64_t)kind<<32)|cursor;display_target=&actual;
 bool redraw=begin_reused_frame(reuse,key);assert(redraw==expected_redraw);
 if(redraw){page(kind,cursor);draws++;}else reuses++;
 ui_canvas_reuse_commit(reuse);
 unsigned frame_before=g_frame;int denied_before=g_denied,damage_before=g_dmg.timer;
 end_frame();unsigned frame_after=g_frame;int denied_after=g_denied,damage_after=g_dmg.timer;
 /* The same real end_frame applies transient decorations to the fresh
    reference. Restore its clock first so timers advance exactly once. */
 g_frame=frame_before;g_denied=denied_before;g_dmg.timer=damage_before;
 g_fb=&reference;g_disp=&reference;graphics_fill_screen(g_fb,g_col[C_BG]);page(kind,cursor);end_frame();
 assert(g_frame==frame_after&&g_denied==denied_after&&g_dmg.timer==damage_after);
 if(memcmp(actual.buffer,reference.buffer,(size_t)actual.stride*actual.height)) {
  fprintf(stderr,"Retained UI mismatch width%u kind%u cursor%u frame%u redraw%d\n",actual.width,kind,cursor,checks,redraw);abort();
 }
 checks++;
}
static void mode(unsigned width,unsigned height)
{
 actual=(surface_t){.width=width,.height=height,.stride=width*2,.buffer=malloc(width*height*2)};
 reference=actual;reference.buffer=malloc(width*height*2);assert(actual.buffer&&reference.buffer);g_raster_width=width;
 memset(gWRAM,0,sizeof(gWRAM));memset(gHRAM,0,sizeof(gHRAM));
 uint8_t *deck=wPlayerDeck;static const uint8_t cards[]={VENUSAUR_LV64,CHANSEY,ALAKAZAM,BLASTOISE,RAICHU_LV40,SCYTHER,PLUSPOWER,DEFENDER,COMPUTER_SEARCH,GRASS_ENERGY};
 memcpy(deck,cards,sizeof(cards));copy_str(g_name[0],sizeof(g_name[0]),"MARK");copy_str(g_name[1],sizeof(g_name[1]),"SAM");
 g_save_ok=false;g_thinking=g_denied=g_dmg.timer=0;g_frame=0;log_clear();log_message("Choose a card.");
 g_practice_reading_count=22;
 for(int i=0;i<g_practice_reading_count;i++)snprintf(g_practice_reading[i],RULE_COLS,"Mason's instruction %d.",i+1);
 for(unsigned kind=0;kind<4;kind++) {
  UiCanvasReuse reuse={0};frame(&reuse,kind,0,true);frame(&reuse,kind,0,false);
  frame(&reuse,kind,kind==2?11:1,true);frame(&reuse,kind,kind==2?11:1,false);
  frame(&reuse,kind,0,true);frame(&reuse,kind,0,false);
  gWRAM[WRAM_SIZE-1]^=1;frame(&reuse,kind,0,true);frame(&reuse,kind,0,false);
  gHRAM[HRAM_SIZE-1]^=1;frame(&reuse,kind,0,true);
  copy_str(g_name[1],sizeof(g_name[1]),kind&1?"SAM":"RONALD");frame(&reuse,kind,0,true);
  log_message("A new message.");frame(&reuse,kind,0,true);frame(&reuse,kind,0,false);
  g_dmg.timer=3;g_dmg.side=TCG_PLAYER;g_dmg.loc=0;g_dmg.amount=10;
  frame(&reuse,kind,0,true);frame(&reuse,kind,0,false);frame(&reuse,kind,0,false);frame(&reuse,kind,0,true);
  g_denied=2;frame(&reuse,kind,0,true);frame(&reuse,kind,0,false);frame(&reuse,kind,0,true);
  /* Both a legacy screen and another retained owner invalidate the parent. */
  display_target=&actual;begin_frame();page((kind+1)%4,0);end_frame();frame(&reuse,kind,0,true);
  UiCanvasReuse nested={0};frame(&nested,(kind+1)%4,0,true);frame(&reuse,kind,0,true);frame(&reuse,kind,0,false);
 }
 printf("retained UI %ux%u: exact across pages, controls, state, logs, expired effects and nested screens\n",width,height);
 free(actual.buffer);free(reference.buffer);
}
int main(void)
{
 for(unsigned i=0;i<NUM_COLORS;i++)g_col[i]=graphics_make_color(kRGB[i][0],kRGB[i][1],kRGB[i][2],255);
 mode(320,240);mode(480,360);mode(320,240);
 printf("RETAINED UI PASS frames%u draws%u reuses%u snapshot_bytes%zu no_image_allocation\n",checks,draws,reuses,sizeof(g_retained_ui));
 return 0;
}
