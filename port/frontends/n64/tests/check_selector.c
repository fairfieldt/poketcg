/* Sam turn 4 after Seaking is knocked out: Check/inspection must not answer
 * the forced replacement request, move its cursor or consume engine RNG. */
#ifndef TCGN64_HOST_TEST
#define TCGN64_HOST_TEST
#endif
#define main tcgn64_frontend_main
#include "../tcgn64.c"
#undef main
#include "engine.h"

typedef struct {uint8_t wram[WRAM_SIZE],hram[HRAM_SIZE];unsigned requests;uint32_t digest;} Outcome;
static Outcome result;
static bool interactive;
static unsigned stage;
static const char *const names[]={"postko-P1","postko-P2","menu-P1","menu-P2","lesson","evolution-P1","evolution-P2"};
static void fold(unsigned n){result.digest=(result.digest^n)*16777619u;}
static void event(void *ctx,const TcgEvent *e)
{
 (void)ctx;fold(e->type);fold(e->text);for(unsigned i=0;i<4;i++)fold(e->arg[i]);
 if(interactive)on_event(NULL,e);
}
static int request(void *ctx,const TcgRequest *r)
{
 (void)ctx;fold(r->type);fold(r->menu);fold(r->duelist);fold(r->shown);fold(r->num_items);fold(r->cursor);
 for(unsigned i=0;i<4;i++)fold(r->arg[i]);result.requests++;
 if(stage>=5)assert(r->type==TCG_REQ_PLAY_AREA&&r->cursor==1);
 int answer=interactive?on_request(NULL,r):(stage<2||stage>=5)?1:0;
 fold((unsigned)answer);return answer;
}
static void callbacks(bool ui)
{static const TcgHost h={event,request,NULL,NULL};interactive=ui;result=(Outcome){.digest=2166136261u};tcg_set_host(&h);}
static Outcome snapshot(void)
{memcpy(result.wram,gWRAM,sizeof(gWRAM));memcpy(result.hram,gHRAM,sizeof(gHRAM));return result;}
static void fixture(unsigned side)
{
 memset(gWRAM,0,sizeof(gWRAM));memset(gHRAM,0,sizeof(gHRAM));reset_duel_ui();set_mode(stage==4?MODE_PRACTICE:MODE_HOTSEAT);
 copy_str(g_name[0],sizeof(g_name[0]),"P1");copy_str(g_name[1],sizeof(g_name[1]),"P2");gDuelistNames[0]="P1";gDuelistNames[1]="P2";
 g_save_ok=false;hWhoseTurn=gUIDecider=side?TCG_OPPONENT:TCG_PLAYER;wWhoseTurn=g_turn_holder=side?TCG_PLAYER:TCG_OPPONENT;
 wDuelTurns=7;wDuelType=stage==4||!side?DUELTYPE_PRACTICE:0;
 for(unsigned d=0;d<2;d++){
  uint8_t *v=tcg_duelvars(d?TCG_OPPONENT:TCG_PLAYER),*deck=d?wOpponentDeck:wPlayerDeck;
  deck[0]=SEAKING;deck[1]=STARYU;deck[2]=DROWZEE;
  v[DUELVARS_DUELIST_TYPE]=DUELIST_TYPE_PLAYER;v[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]=3;
  for(unsigned loc=0;loc<3;loc++){v[DUELVARS_ARENA_CARD+loc]=loc;v[DUELVARS_CARD_LOCATIONS+loc]=CARD_LOCATION_ARENA+loc;}
  v[DUELVARS_ARENA_CARD_HP]=0;v[DUELVARS_ARENA_CARD_HP+1]=40;v[DUELVARS_ARENA_CARD_HP+2]=50;
 }
 wPlayAreaSelectAction=1;wExcludeArenaPokemon=0;hTempCardIndex_ff98=0;
 wRNG1=0x5a;wRNG2=0xc3;wRNGCounter=7;g_screen=TCG_SCREEN_PLAY_AREA;
}
static int select_case(unsigned side)
{
 if(stage<2){assert(!OpenPlayAreaScreenForSelection());assert(hTempPlayAreaLocation_ff9d==1);return 1;}
 if(stage>=5){assert(!OpenPlayAreaScreenForEvolutionSelection(1));assert(hTempPlayAreaLocation_ff9d==1);return 1;}
 if(stage<4){TcgRequest r={.type=TCG_REQ_MENU,.menu=TCG_MENU_PLAY_AREA,.duelist=side?TCG_OPPONENT:TCG_PLAYER,.shown=side?TCG_OPPONENT:TCG_PLAYER,.num_items=2,.cursor=0,.arg={1}};return request(NULL,&r);}
 tcg_ui_practice_speech(SamTurn4DrMason2PracticeDuelText);return 0;
}
int main(void)
{
 display_init(getenv("MOCK_HIGH_RES")?(resolution_t){480,360,0}:RESOLUTION_320x240,DEPTH_16_BPP,3,GAMMA_NONE,FILTERS_RESAMPLE);joypad_init();
 for(unsigned i=0;i<NUM_COLORS;i++)g_col[i]=graphics_make_color(kRGB[i][0],kRGB[i][1],kRGB[i][2],255);
 for(stage=0;stage<sizeof(names)/sizeof(names[0]);stage++){
  unsigned side=stage>=5?(stage-5)&1:stage<4?stage&1:0;fixture(side);callbacks(false);int expected=select_case(side);Outcome reference=snapshot();
  fixture(side);callbacks(true);debugf("CHECK_FLOW_BEGIN %s\n",names[stage]);int answer=select_case(side);Outcome actual=snapshot();
  assert(answer==expected&&reference.requests==actual.requests&&reference.digest==actual.digest);
  assert(!memcmp(reference.wram,actual.wram,sizeof(gWRAM))&&!memcmp(reference.hram,actual.hram,sizeof(gHRAM)));
  if(stage==4)assert(g_practice_reading_on);
  debugf("CHECK_FLOW_RETURN %s answer=%d unchanged_engine_requests_rng=1\n",names[stage],answer);
 }
 debugf("CHECK_SELECTOR_PASS postko_P1_P2_START_Z_B_cursor_Z_A_priority_lesson_engine_rng=1 evolution_default_P1_P2=1\n");
 for(;;){begin_frame();draw_board(0,-1);draw_textbox();end_frame_poll();}
}
