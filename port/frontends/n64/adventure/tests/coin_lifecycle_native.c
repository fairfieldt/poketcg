/* Native component fixture: real engine coin/attack UI, automatic ACKs only
 * in the private copied frontend. The trailer separately uses real controls. */
#define TCGN64_ADVENTURE
#define TCGN64_CARD_ART
#define ADVENTURE_DIAGNOSTIC 9
#include "coin-lifecycle-source.c"
#include "../video.c"
#include "duel_ui.h"
#include "engine.h"
void adventure_audio_poll(void){}
void adventure_audio_event(unsigned kind,unsigned id){(void)kind;(void)id;}
static unsigned ready_acks,result_acks,damage_checks,ko_checks;
static bool capture_coin;
static uint16_t *pixels;
static void capture(const char *name)
{
 surface_t *f=g_last_ui_frame;const uint16_t *p=UncachedAddr(f->buffer);
 debugf("FRAMEBUFFER %s %u %u\n",name,f->width,f->height);
 for(unsigned y=0;y<f->height;y++)for(unsigned x=0;x<f->width;x+=32){char text[129];unsigned n=f->width-x;if(n>32)n=32;for(unsigned i=0;i<n;i++)snprintf(text+i*4,5,"%04x",p[y*(f->stride/2)+x+i]);debugf("PIXELS %u %u %s\n",x,y,text);}
 debugf("FRAMEBUFFER END\n");
}
static void exact_board(bool acknowledgement)
{
 surface_t *f=g_last_ui_frame;memcpy(pixels,UncachedAddr(f->buffer),f->stride*f->height);
 /* end_frame advances these presentation counters after painting. Repaint
    the same state fresh, then end_frame restores their original values. */
 g_frame--;if(g_dmg.timer)g_dmg.timer++;
 begin_frame();draw_board(0,-1);draw_textbox();
 if(acknowledgement&&(g_frame/16)&1)print(RIGHT-CW,TBOX_LINE(TBOX_LINES-1),C_HI,"v");
 end_frame();
 assertf(!memcmp(pixels,UncachedAddr(g_last_ui_frame->buffer),f->stride*f->height),"Coin remained over the native board");
}
static void observe(void *ctx,const TcgEvent *ev)
{
 on_event(ctx,ev);
 if(ev->type==TCG_EV_DAMAGE){assert(g_screen==TCG_SCREEN_MAIN);exact_board(false);damage_checks++;if(capture_coin)capture("damage-board");}
}
static int acknowledge(void *ctx,const TcgRequest *r)
{
 assert(r->type==TCG_REQ_ACK&&r->duelist==g_turn_holder);
 bool coin=g_screen==TCG_SCREEN_COIN_TOSS;
 int answer=on_request(ctx,r);assert(answer==0);
 if(coin){if(g_coin.ready){ready_acks++;if(capture_coin&&ready_acks==1)capture("coin-ready");}
  else{result_acks++;if(capture_coin&&result_acks==3)capture("coin-result");}}
 return answer;
}
int main(void)
{
 debug_init_isviewer();debug_init_usblog();timer_init();dfs_init(DFS_DEFAULT_LOCATION);rdpq_init();joypad_init();
 display_init(n64_video_init(get_memory_size()>=8*1024*1024),DEPTH_16_BPP,3,GAMMA_NONE,FILTERS_RESAMPLE);display_set_fps_limit(30);
 g_raster_width=n64_video_width;g_ui_canvas=n64_video_surface();pixels=malloc(n64_video_width*n64_video_height*2);assert(pixels);
 for(unsigned i=0;i<NUM_COLORS;i++)g_col[i]=graphics_make_color(kRGB[i][0],kRGB[i][1],kRGB[i][2],255);
 for(unsigned side=0;side<2;side++){
  reset_duel_ui();polish_fixture(false);g_save_ok=false;g_mode=MODE_HOTSEAT;g_kind[0]=g_kind[1]=PK_HUMAN;
  hWhoseTurn=wWhoseTurn=side?TCG_OPPONENT:TCG_PLAYER;g_turn_holder=hWhoseTurn;
  gDuelistNames[0]="MARK";gDuelistNames[1]="AMY";copy_str(g_name[0],sizeof(g_name[0]),"Mark");copy_str(g_name[1],sizeof(g_name[1]),"Amy");
  wRNG1=0x5a;wRNG2=0xc3;wRNGCounter=7;
  capture_coin=!side;ready_acks=result_acks=0;
  TcgHost host={observe,acknowledge,NULL,NULL};tcg_set_host(&host);input_begin();
  TossCoinATimes(3,0);assert(ready_acks==3&&result_acks==3&&g_screen==TCG_SCREEN_COIN_TOSS);
  uint8_t victim=side?TCG_PLAYER:TCG_OPPONENT,*vars=tcg_duelvars(victim),*deck=side?wPlayerDeck:wOpponentDeck;
  deck[0]=GOLDEEN;vars[DUELVARS_ARENA_CARD]=0;vars[DUELVARS_ARENA_CARD_HP]=40;wLoadedAttack.Animation=ATK_ANIM_HIT;
  PlayAttackAnimation_DealAttackDamageSimple(vars+DUELVARS_ARENA_CARD_HP,40,0,0);assert(!vars[DUELVARS_ARENA_CARD_HP]);
  wTempNonTurnDuelistCardID=GOLDEEN;SwapTurn();assert(PrintKnockedOutIfHLZero(vars+DUELVARS_ARENA_CARD_HP));WaitForWideTextBoxInput();
  assert(g_screen==TCG_SCREEN_MAIN);exact_board(true);ko_checks++;if(capture_coin)capture("goldeen-knockout");SwapTurn();input_end();
 }
 debugf("COIN LIFECYCLE NATIVE PASS width=%u ready_acks=6 result_acks=6 damage_checks=%u ko_checks=%u exact_board=1 owners=2\n",n64_video_width,damage_checks,ko_checks);
 for(;;)wait_ms(10);
}
