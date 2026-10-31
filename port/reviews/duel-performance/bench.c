#define TCGN64_ADVENTURE
#define TCGN64_CARD_ART
#define ADVENTURE_DIAGNOSTIC 9
#define display_get measured_display_get
#include "source.c"
#undef display_get
extern surface_t *display_get(void);
static uint32_t acquire_ticks;
surface_t *measured_display_get(void) {uint32_t start=timer_ticks();surface_t *surface=display_get();acquire_ticks=timer_ticks()-start;return surface;}
#undef ADVENTURE_DIAGNOSTIC
#define ADVENTURE_DIAGNOSTIC 0
#include "/Users/fairfieldt/dev/poketcg/port/frontends/n64/adventure/video.c"
void adventure_audio_poll(void) {}
void adventure_audio_event(unsigned kind,unsigned id) {(void)kind;(void)id;}
typedef struct {unsigned acquire,clear,board,overlay,present,total;} Measurement;
static const char *cases[]={"full-board-ack","full-actions-idle","full-actions-cursor","full-slot-cursor","sparse-actions-idle","hand-card-list","card-detail","generic-popup-list","thinking-ack"};
static Measurement draw(unsigned which,unsigned frame) {
 uint32_t a=timer_ticks();begin_frame();uint32_t b=timer_ticks();
 if(which==1||which==2||which==4)draw_duel_choices(TCG_PLAYER,which==2?frame%6:0);
 else if(which==5){int top=0;const uint8_t *v=tcg_duelvars(TCG_PLAYER);draw_card_list_screen("Mark's Hand",NULL,v+DUELVARS_HAND,14,TCG_PLAYER,3,&top);}
 else if(which==6)draw_card_page_at(VENUSAUR_LV64,TCG_PLAYER,-1,NULL,0);
 else if(which==7){int top=0;static const char *const labels[]={"Mark","Sam","Dr. Mason","Ronald","Hand","Attack","Check","Pokemon Power","Retreat","Done","Save","Cancel"};draw_list(10,8,300,11,"Choose",12,0,&top,label_text,(void *)labels);}
 else draw_board(which==3?TCG_PLAYER:0,which==3?(int)(frame%6):-1);
 uint32_t c=timer_ticks();
 if(which==0||which==8){draw_textbox();if((g_frame/16)&1)print(RIGHT-CW,TBOX_LINE(TBOX_LINES-1),C_HI,"v");}
 if(which==3){slot_hint(TCG_PLAYER,frame%6);draw_textbox_hint("A: choose  B: back  START: info");}
 if(which>=5&&which<8)draw_textbox_hint("A: select  B: back  START: info");
 uint32_t d=timer_ticks();end_frame();uint32_t e=timer_ticks();
 return (Measurement){TICKS_TO_US(acquire_ticks),TICKS_TO_US((b-a)-acquire_ticks),TICKS_TO_US(c-b),TICKS_TO_US(d-c),TICKS_TO_US(e-d),TICKS_TO_US(e-a)};
}

static void reference_overlay(unsigned frame) {
 if(frame%3==0)draw_duel_banner("Mark's turn");
 else if(frame%3==1)slot_hint(TCG_OPPONENT,5);
 draw_textbox_hint("A: choose  B: back  START: info");
}
static void pixels_check(void) {
 surface_t allocated=n64_video_surface(),reference=allocated;reference.buffer=CachedAddr(reference.buffer);
 unsigned checks=0;
 for(unsigned scenario=0;scenario<4;scenario++) {
  polish_fixture(scenario==1);g_thinking=0;g_denied=0;g_dmg.timer=0;
  for(unsigned frame=0;frame<16;frame++) {
   uint8_t side=(frame&1)?TCG_OPPONENT:TCG_PLAYER;int loc=(frame/2)%6;
   if(frame>=12){side=0;loc=-1;}
   if(scenario==1) {
    uint8_t *vars=tcg_duelvars(side?side:TCG_PLAYER);
    for(unsigned i=6;i<59;i++)vars[DUELVARS_CARD_LOCATIONS+i]=CARD_LOCATION_ARENA+(loc<0?0:loc);
   }
   if(scenario==2) {
    if(frame==0){g_dmg.timer=9;g_dmg.side=TCG_OPPONENT;g_dmg.loc=0;g_dmg.amount=999;g_denied=3;}
    if(frame==6){wPlayerDeck[0]=SEAKING;tcg_duelvars(TCG_PLAYER)[DUELVARS_ARENA_CARD_HP]=20;}
    if(frame==10){copy_str(g_name[1],sizeof(g_name[1]),"RONALD");g_turn_holder=TCG_OPPONENT;}
   }
   if(scenario==3) {
    g_thinking=TCG_OPPONENT;loc=frame<8?-1:loc;
    if(frame==8)tcg_duelvars(TCG_PLAYER)[DUELVARS_ARENA_CARD+0]=0xff;
    if(frame==12)tcg_duelvars(TCG_OPPONENT)[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]=2;
   }
   /* A nested card page overwrites the shared canvas while keeping the board backing. */
   if(frame==4){begin_frame();draw_card_page(COMPUTER_SEARCH,TCG_PLAYER,-1,NULL);draw_textbox();end_frame();}
   begin_frame();draw_board(side,loc);reference_overlay(frame);surface_t *actual=g_fb;
   g_fb=&reference;graphics_fill_screen(g_fb,g_col[C_BG]);draw_board_uncached(side,loc);reference_overlay(frame);
   if(g_denied){rect(0,0,SCREEN_W,SCREEN_H,C_RED);rect(1,1,SCREEN_W-2,SCREEN_H-2,C_RED);rect(2,2,SCREEN_W-4,SCREEN_H-4,C_RED);}
   g_fb=actual;end_frame();
   const uint16_t *a=actual->buffer,*b=reference.buffer;
   for(unsigned y=0;y<actual->height;y++)for(unsigned x=0;x<actual->width;x++)
    assertf(a[y*(actual->stride/2)+x]==b[y*(reference.stride/2)+x],
     "Board pixel mismatch scenario%u frame%u at%u,%u: %04x !=%04x",scenario,frame,x,y,a[y*(actual->stride/2)+x],b[y*(reference.stride/2)+x]);
   checks++;
  }
 }
 surface_free(&allocated);g_dmg.timer=g_denied=g_thinking=0;
 debugf("BOARD PIXELS width=%u frames=%u exact=1 nested=1 selection=1 state=1 effects=1\n",n64_video_width,checks);
}
static void capture(const char *label) {
 surface_t *frame=g_last_ui_frame;const uint16_t *pixels=UncachedAddr(frame->buffer);
 debugf("FRAMEBUFFER %s-%u %u %u\n",label,frame->width,frame->width,frame->height);
 for(unsigned y=0;y<frame->height;y++)for(unsigned x=0;x<frame->width;x+=32) {
  char text[129];unsigned count=frame->width-x;if(count>32)count=32;
  for(unsigned i=0;i<count;i++)snprintf(text+i*4,5,"%04x",pixels[y*(frame->stride/2)+x+i]);
  debugf("PIXELS %u %u %s\n",x,y,text);
 }
 debugf("FRAMEBUFFER END\n");
}

int main(void) {
 debug_init_isviewer();debug_init_usblog();timer_init();dfs_init(DFS_DEFAULT_LOCATION);rdpq_init();joypad_init();
 for(unsigned high=0;high<2;high++) {
  display_init(n64_video_init(high),DEPTH_16_BPP,3,GAMMA_NONE,FILTERS_RESAMPLE);display_set_fps_limit(30);
  g_raster_width=n64_video_width;g_ui_canvas=n64_video_surface();
  for(unsigned i=0;i<NUM_COLORS;i++)g_col[i]=graphics_make_color(kRGB[i][0],kRGB[i][1],kRGB[i][2],255);
  copy_str(g_name[0],sizeof(g_name[0]),"MARK");copy_str(g_name[1],sizeof(g_name[1]),"SAM");
  g_kind[0]=PK_HUMAN;g_kind[1]=PK_AI;g_turn_holder=TCG_PLAYER;g_turn=1;g_mode=MODE_VS_AI;
  for(unsigned which=0;which<9;which++) {
   polish_fixture(false);g_screen=TCG_SCREEN_EMPTY;g_denied=g_dmg.timer=g_thinking=0;log_clear();log_message("Mark's Turn.");
   if(which==4)for(unsigned side=0;side<2;side++)tcg_duelvars(side?TCG_OPPONENT:TCG_PLAYER)[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]=2;
   if(which==8)g_thinking=TCG_OPPONENT;
   unsigned samples=which<5?32:16,warmup=which<5?8:4;
   for(unsigned frame=0;frame<warmup;frame++)draw(which,frame);
   Measurement timings[32];uint32_t begun=timer_ticks();
   for(unsigned frame=0;frame<samples;frame++)timings[frame]=draw(which,frame);
   unsigned elapsed=TICKS_TO_US((uint32_t)(timer_ticks()-begun));
   for(unsigned frame=0;frame<samples;frame++)debugf("BOARD PERF case=%s width=%u frame=%u acquire_us=%u clear_us=%u board_us=%u overlay_us=%u present_us=%u total_us=%u\n",cases[which],n64_video_width,frame,timings[frame].acquire,timings[frame].clear,timings[frame].board,timings[frame].overlay,timings[frame].present,timings[frame].total);
   debugf("BOARD DURATION case=%s width=%u frames=%u elapsed_us=%u\n",cases[which],n64_video_width,samples,elapsed);
  }

  pixels_check();polish_fixture(false);g_turn_holder=TCG_PLAYER;g_thinking=0;
  copy_str(g_name[0],sizeof(g_name[0]),"MARK");copy_str(g_name[1],sizeof(g_name[1]),"SAM");
  for(unsigned frame=0;frame<40;frame++){draw(1,0);}
  capture("actions");
  heap_stats_t a,b;sys_get_heap_stats(&a);unsigned bytes=g_board_cache.image.stride*g_board_cache.image.height;
  board_cache_release();sys_get_heap_stats(&b);assertf(b.used<=a.used-bytes,"Board cache not released");
  debugf("BOARD MEMORY width=%u backing_bytes=%u free_before_release=%d free_after_release=%d retained_image=0 key_bytes=%u\n",n64_video_width,bytes,a.total-a.used,b.total-b.used,(unsigned)sizeof(BoardCache));
  rspq_wait();surface_free(&g_ui_canvas);display_close();
 }
 debugf("BOARD PERF PASS\n");for(;;)wait_ms(10);
}
