/* Real engine presentation functions with buffered versus fresh confirmation.
 * Equal answers must leave engine memory, event order and RNG unchanged. */
#ifndef TCGN64_HOST_TEST
#define TCGN64_HOST_TEST
#endif
#define main tcgn64_frontend_main
#include "../tcgn64.c"
#undef main
#include "duel_ui.h"
#include "engine.h"

typedef struct {uint8_t wram[WRAM_SIZE],hram[HRAM_SIZE];uint32_t digest;unsigned requests;} Outcome;
static bool interactive;
static unsigned stage;
static const char *const names[]={"draw","placement","forced","actions","intentional-hand"};
static Outcome observed;
static void fold(unsigned value){observed.digest=(observed.digest^value)*16777619u;}
static void event(void *ctx,const TcgEvent *e)
{
 (void)ctx;fold(e->type);for(unsigned i=0;i<4;i++)fold(e->arg[i]);fold(e->text);
 if(interactive)on_event(NULL,e);
}
static int request(void *ctx,const TcgRequest *r)
{
 (void)ctx;fold(r->type);fold(r->menu);fold(r->num_items);fold(r->text);fold(r->text2);observed.requests++;
 if(interactive&&stage==1)assert(g_screen==TCG_SCREEN_MAIN&&!g_overlay.on&&g_placement_ack);
 int answer=interactive?on_request(NULL,r):0;fold((unsigned)answer);
 if(interactive)debugf("FLOW_RETURN %s answer=%d\n",names[stage],answer);
 return answer;
}
static void host(bool ui)
{
 interactive=ui;observed=(Outcome){.digest=2166136261u};
 TcgHost callbacks={event,request,NULL,NULL};tcg_set_host(&callbacks);
}
static Outcome outcome(void)
{
 memcpy(observed.wram,gWRAM,sizeof(gWRAM));memcpy(observed.hram,gHRAM,sizeof(gHRAM));return observed;
}
static void equal(Outcome reference)
{
 Outcome actual=outcome();assert(reference.digest==actual.digest&&reference.requests==actual.requests);
 assert(!memcmp(reference.wram,actual.wram,sizeof(actual.wram))&&!memcmp(reference.hram,actual.hram,sizeof(actual.hram)));
}
static void fixture(void)
{
 memset(gWRAM,0,sizeof(gWRAM));memset(gHRAM,0,sizeof(gHRAM));reset_duel_ui();set_mode(MODE_VS_BOT);
 gDuelistNames[0]="PLAYER";gDuelistNames[1]="BOT";g_save_ok=false;
 hWhoseTurn=TCG_PLAYER;wWhoseTurn=TCG_PLAYER;g_turn_holder=TCG_PLAYER;
 uint8_t *v=tcg_duelvars(TCG_PLAYER);
 wPlayerDeck[0]=GOLDEEN;wPlayerDeck[1]=STARYU;wPlayerDeck[2]=PLUSPOWER;wPlayerDeck[3]=WATER_ENERGY;
 v[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]=2;v[DUELVARS_ARENA_CARD]=0;v[DUELVARS_ARENA_CARD+1]=1;
 v[DUELVARS_ARENA_CARD_HP]=40;v[DUELVARS_ARENA_CARD_HP+1]=40;
 v[DUELVARS_CARD_LOCATIONS]=CARD_LOCATION_ARENA;v[DUELVARS_CARD_LOCATIONS+1]=CARD_LOCATION_ARENA+1;
 v[DUELVARS_CARD_LOCATIONS+2]=v[DUELVARS_CARD_LOCATIONS+3]=CARD_LOCATION_HAND;
 v[DUELVARS_NUMBER_OF_CARDS_IN_HAND]=2;v[DUELVARS_HAND]=2;v[DUELVARS_HAND+1]=3;
 wRNG1=0x5a;wRNG2=0xc3;wRNGCounter=7;hTempCardIndex_ff98=0;
}
static void queued_a(void)
{
 /* A complete tap from before presentation, already released. */
 g_input_capture.pending[0]|=INPUT_A;g_input_capture.held[0]=0;g_keys[0].a=true;
}
static void placement(void)
{
 g_screen=TCG_SCREEN_CARD_LIST;g_screen_arg=2;wDuelTempList[0]=2;wDuelTempList[1]=3;wDuelTempList[2]=0xff;
 hTempCardIndex_ff98=1;hTempPlayAreaLocation_ff9d=1;
 LoadCardDataToBuffer1_FromDeckIndex(1);CopyCardNameAndLevel(20);LoadTxRam2(0);
 DrawWideTextBox_WaitForInput(PlacedOnTheBenchText);
}
static void action(void){DrawDuelMainScene();assert(HandleDuelMenuInput(0)==0);}
static void select_list(bool ordinary)
{
 uint8_t items[]={2,3};TcgRequest r={0};r.type=TCG_REQ_CARD_LIST;r.duelist=r.shown=TCG_PLAYER;
 r.num_items=2;r.items=items;r.text=ordinary?DuelistHandText:ChooseTheCardYouWishToExamineText;
 r.text2=ordinary?PleaseSelectHandText:0;r.arg[1]=ordinary?PLAY_CHECK:SELECT_CHECK;
 assert(request(NULL,&r)==0);
}
int main(void)
{
 display_init(getenv("MOCK_HIGH_RES")?(resolution_t){480,360,0}:RESOLUTION_320x240,DEPTH_16_BPP,3,GAMMA_NONE,FILTERS_RESAMPLE);joypad_init();
 for(unsigned i=0;i<NUM_COLORS;i++)g_col[i]=graphics_make_color(kRGB[i][0],kRGB[i][1],kRGB[i][2],255);
 stage=0;fixture();host(false);DisplayPlayerDrawCardScreen();Outcome reference=outcome();
 fixture();host(true);queued_a();DisplayPlayerDrawCardScreen();equal(reference);
 stage=1;fixture();host(false);placement();reference=outcome();
 fixture();host(true);queued_a();placement();equal(reference);assert(g_actions_fresh_owner==TCG_PLAYER);
 /* A power's intervening forced selector must remain an ordinary choice. */
 stage=2;uint8_t wram[WRAM_SIZE],hram[HRAM_SIZE];memcpy(wram,gWRAM,sizeof(wram));memcpy(hram,gHRAM,sizeof(hram));
 host(false);select_list(false);reference=outcome();memcpy(gWRAM,wram,sizeof(wram));memcpy(gHRAM,hram,sizeof(hram));
 host(true);select_list(false);equal(reference);assert(g_actions_fresh_owner==TCG_PLAYER);
 stage=3;memcpy(wram,gWRAM,sizeof(wram));memcpy(hram,gHRAM,sizeof(hram));
 host(false);action();reference=outcome();memcpy(gWRAM,wram,sizeof(wram));memcpy(gHRAM,hram,sizeof(hram));
 host(true);queued_a();action();equal(reference);assert(!g_actions_fresh_owner);
 stage=4;host(false);select_list(true);reference=outcome();host(true);select_list(true);equal(reference);
 debugf("ACK_FLOW_PASS engine_memory_requests_answers_rng_unchanged=1 draw_placement_actions_fresh=1 forced_and_intentional_hand_preserved=1\n");
 for(;;){begin_frame();draw_board(0,-1);draw_textbox();end_frame_poll();}
}
