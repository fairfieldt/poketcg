/* Compare cached board frames against fresh production drawing while engine
 * state, selections and transient effects change. */
#include <stdlib.h>
static void *cache_test_malloc(size_t size);
#define malloc cache_test_malloc
#ifndef TCGN64_HOST_TEST
#define TCGN64_HOST_TEST
#endif
#define graphics_make_color cache_test_color
#define graphics_fill_screen cache_test_clear
#define graphics_draw_box cache_test_box
#define graphics_set_color cache_test_set_color
#define graphics_draw_text cache_test_text
#define main tcgn64_frontend_main
#include "../tcgn64.c"
#undef main
#undef malloc

static size_t failed_allocation_size;
static void *cache_test_malloc(size_t size)
{return size==failed_allocation_size?NULL:malloc(size);}

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

static surface_t actual,reference;
static unsigned checked,hits,misses;
static void fixture(void)
{
 memset(gWRAM,0,sizeof(gWRAM));memset(gHRAM,0,sizeof(gHRAM));
 static const uint8_t cards[]={VENUSAUR_LV64,CHANSEY,ALAKAZAM,BLASTOISE,RAICHU_LV40,SCYTHER};
 for(unsigned side=0;side<2;side++) {
  uint8_t *vars=tcg_duelvars(side?TCG_OPPONENT:TCG_PLAYER),*deck=side?wOpponentDeck:wPlayerDeck;
  for(unsigned i=0;i<DECK_SIZE;i++){deck[i]=GRASS_ENERGY;vars[DUELVARS_CARD_LOCATIONS+i]=CARD_LOCATION_DECK;}
  vars[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]=6;vars[DUELVARS_PRIZES]=0x2b;
  for(unsigned i=0;i<6;i++) {
   deck[i]=cards[i];vars[DUELVARS_ARENA_CARD+i]=i;vars[DUELVARS_ARENA_CARD_HP+i]=card_data(cards[i])->HP;
   vars[DUELVARS_CARD_LOCATIONS+i]=CARD_LOCATION_ARENA+i;
   vars[DUELVARS_CARD_LOCATIONS+i+6]=CARD_LOCATION_ARENA+i;
  }
  vars[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK]=12;
 }
 copy_str(g_name[0],sizeof(g_name[0]),"MARK");copy_str(g_name[1],sizeof(g_name[1]),"SAM");
 g_turn_holder=TCG_PLAYER;g_thinking=0;g_dmg.timer=0;g_frame=0;log_clear();log_message("Mark's Turn.");
}
static void check(uint8_t side,int loc,bool expected_hit)
{
 g_fb=&actual;
 bool hit=board_cache_matches(side,loc);assert(hit==expected_hit);
 if(hit)hits++;else misses++;
 /* Nested pages are allowed to have overwritten every canvas pixel. */
 memset(actual.buffer,0x72,(size_t)actual.stride*actual.height);
 graphics_fill_screen(&actual,g_col[C_BG]);draw_board(side,loc);draw_textbox();
 g_fb=&reference;graphics_fill_screen(&reference,g_col[C_BG]);draw_board_uncached(side,loc);draw_textbox();
 if(memcmp(actual.buffer,reference.buffer,(size_t)actual.stride*actual.height)) {
  for(unsigned y=0;y<actual.height;y++)for(unsigned x=0;x<actual.width;x++) {
   uint16_t a=((uint16_t *)actual.buffer)[y*actual.width+x],b=((uint16_t *)reference.buffer)[y*reference.width+x];
   if(a!=b){fprintf(stderr,"Board frame%u hit%d side%u loc%d first mismatch %u,%u: %04x != %04x\n",checked,hit,side,loc,x,y,a,b);abort();}
  }
 }
 checked++;
}
static void mode(unsigned width,unsigned height)
{
 g_raster_width=width;
 actual=(surface_t){.width=width,.height=height,.stride=width*2,.buffer=malloc(width*height*2)};
 reference=actual;reference.buffer=malloc(width*height*2);assert(actual.buffer&&reference.buffer);
 fixture();check(0,-1,false);check(0,-1,true);
 g_frame=16;check(0,-1,true); /* no blinking AI caption */
 for(unsigned side=0;side<2;side++)for(int loc=0;loc<6;loc++) {
  check(side?TCG_OPPONENT:TCG_PLAYER,loc,true);check(side?TCG_OPPONENT:TCG_PLAYER,loc,true);
 }
 check(TCG_OPPONENT,5,true);
 wPlayerDeck[0]=SEAKING;check(0,-1,false);check(0,-1,true);
 wOpponentDeck[0]=GOLDEEN;check(0,-1,false);
 tcg_duelvars(TCG_PLAYER)[DUELVARS_ARENA_CARD_HP]=10;check(0,-1,false);
 tcg_duelvars(TCG_OPPONENT)[DUELVARS_ARENA_CARD_STATUS]=CONFUSED|POISONED;check(0,-1,false);
 tcg_duelvars(TCG_PLAYER)[DUELVARS_CARD_LOCATIONS+6]=CARD_LOCATION_ARENA+1;check(0,-1,false);
 gWRAM[WRAM_SIZE-1]^=1;check(0,-1,false);gHRAM[HRAM_SIZE-1]^=1;check(0,-1,false);
 copy_str(g_name[1],sizeof(g_name[1]),"RONALD");check(0,-1,false);
 g_turn_holder=TCG_OPPONENT;check(0,-1,false);
 g_dmg.timer=3;g_dmg.side=TCG_PLAYER;g_dmg.loc=0;g_dmg.amount=20;check(0,-1,false);
 g_dmg.timer=2;check(0,-1,true);g_dmg.timer=0;check(0,-1,false);
 g_thinking=TCG_OPPONENT;check(0,-1,true);g_frame+=16;check(0,-1,true);check(0,-1,true);
 check(TCG_PLAYER,0,true);g_frame+=16;check(TCG_PLAYER,1,true);
 g_thinking=0;check(0,-1,true);
 /* Selection borders share pixels with energy squares, portrait frames
  * and damage glyphs. Compare all slots on both sides in those states. */
 for(unsigned side=0;side<2;side++)for(int loc=0;loc<6;loc++) {
  uint8_t duelist=side?TCG_OPPONENT:TCG_PLAYER,*vars=tcg_duelvars(duelist);
  for(unsigned i=6;i<DECK_SIZE;i++)vars[DUELVARS_CARD_LOCATIONS+i]=CARD_LOCATION_ARENA+loc;
  g_dmg.timer=3;g_dmg.side=duelist;g_dmg.loc=loc;g_dmg.amount=999;
  check(duelist,loc,false);check(duelist,loc,true);
  g_dmg.amount=65535;check(duelist,loc,false);check(duelist,loc,true);
  g_dmg.timer=0;vars[DUELVARS_ARENA_CARD+loc]=0xff;check(duelist,loc,false);check(duelist,loc,true);
 }
 assert(g_board_cache.image.width==width&&g_board_cache.image.height==height*BOARD_H/SCREEN_H);
 unsigned backing_bytes=g_board_cache.image.stride*g_board_cache.image.height;
 /* Adventure card inspection must not inherit the completed duel's log. */
 log_clear();log_message("all the Prizes");log_message("Decision");log_message("You won the Duel");
 char saved_log[sizeof(g_log)];memcpy(saved_log,g_log,sizeof(g_log));int saved_n=g_log_n,saved_newest=g_log_newest;
 g_fb=&actual;graphics_fill_screen(g_fb,g_col[C_BG]);draw_card_view_page(ELECTABUZZ_LV35,TCG_PLAYER,-1,0,false);
 assert(g_log_n==saved_n&&g_log_newest==saved_newest&&!memcmp(saved_log,g_log,sizeof(g_log)));
 log_clear();g_fb=&reference;graphics_fill_screen(g_fb,g_col[C_BG]);draw_card_view_page(ELECTABUZZ_LV35,TCG_PLAYER,-1,0,false);
 assert(!memcmp(actual.buffer,reference.buffer,(size_t)actual.stride*actual.height));
 memcpy(g_log,saved_log,sizeof(g_log));g_log_n=saved_n;g_log_newest=saved_newest;
 g_fb=&actual;graphics_fill_screen(g_fb,g_col[C_BG]);draw_card_view_page(ELECTABUZZ_LV35,TCG_PLAYER,-1,0,true);
 assert(memcmp(actual.buffer,reference.buffer,(size_t)actual.stride*actual.height));
 assert(g_log_n==saved_n&&g_log_newest==saved_newest&&!memcmp(saved_log,g_log,sizeof(g_log)));
 /* A speed-cache allocation failure must preserve selected and thinking
  * frames through the original renderer, rather than abort the game. */
 board_cache_release();fixture();failed_allocation_size=backing_bytes;
 check(TCG_PLAYER,0,false);assert(!g_board_cache.image.buffer&&!g_board_cache.valid);
 g_thinking=TCG_OPPONENT;check(0,-1,false);assert(!g_board_cache.image.buffer&&!g_board_cache.valid);
 failed_allocation_size=0;check(0,-1,false);assert(g_board_cache.valid);check(0,-1,true);
 printf("board cache %ux%u: exact, allocation %u bytes; allocation failure falls back\n",width,height,backing_bytes);
 free(actual.buffer);free(reference.buffer);g_fb=NULL;
}
int main(void)
{
	display_init(RESOLUTION_320x240,DEPTH_16_BPP,3,GAMMA_NONE,FILTERS_RESAMPLE);
 for(unsigned i=0;i<NUM_COLORS;i++)g_col[i]=graphics_make_color(kRGB[i][0],kRGB[i][1],kRGB[i][2],255);
 mode(320,240);mode(480,360);mode(320,240);
 board_cache_release();assert(!g_board_cache.image.buffer&&!g_board_cache.valid);
 free(g_felt.rows);g_felt=(FeltCache){0};
 printf("BOARD CACHE PASS: %u frames (%u hits/%u invalidations), decks/state/names/selection/effects/resizing/release; adventure card footer isolated from duel log\n",checked,hits,misses);
 return 0;
}
