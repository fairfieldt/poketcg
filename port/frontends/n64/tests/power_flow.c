/* Real Venusaur Power selection and Energy Trans, including a stale Retreat
 * screen. Frontend choices must select the displayed absolute play-area slot. */
#ifndef TCGN64_HOST_TEST
#define TCGN64_HOST_TEST
#endif
#define main tcgn64_frontend_main
#include "../tcgn64.c"
#undef main
#include "engine.h"

enum { TRANSFER, DONOR_CANCEL, RECIPIENT_CANCEL, INVALID_DONOR, POWER_CANCEL, NO_GRASS, STATUS_BLOCKED, MUK_BLOCKED, CASES };
static const char *const case_names[]={"transfer","donor-cancel","recipient-cancel","invalid-donor","power-cancel","no-grass","status-blocked","muk-blocked"};
typedef struct {uint8_t wram[WRAM_SIZE],hram[HRAM_SIZE];uint32_t digest;unsigned requests;} Outcome;
static Outcome observed;
static unsigned kind,side,mode,donor_requests,recipient_requests;
static bool interactive;
static void fold(unsigned n){observed.digest=(observed.digest^n)*16777619u;}
static void event(void *ctx,const TcgEvent *e)
{
 (void)ctx;fold(e->type);fold(e->text);for(unsigned i=0;i<4;i++)fold(e->arg[i]);
 if(e->type==TCG_EV_SCREEN)mode=e->arg[0]==TCG_SCREEN_PLAY_AREA?e->arg[3]:TCG_PLAY_AREA_NORMAL;
 if(interactive)on_event(NULL,e);
}
static int request(void *ctx,const TcgRequest *r)
{
 (void)ctx;fold(r->type);fold(r->menu);fold(r->shown);fold(r->duelist);fold(r->cursor);fold(r->num_items);fold(r->text);
 for(unsigned i=0;i<4;i++)fold(r->arg[i]);observed.requests++;
 int expected=0;const char *phase="ack";
 if(r->type==TCG_REQ_MENU&&r->menu==TCG_MENU_PLAY_AREA) {
  assert(r->shown==(side?TCG_OPPONENT:TCG_PLAYER)&&r->duelist==r->shown&&r->arg[0]==0&&r->num_items==3);
  if(mode==TCG_PLAY_AREA_POWER){phase="power";expected=kind==POWER_CANCEL?TCG_CANCEL:0;}
  else if(mode==TCG_PLAY_AREA_ENERGY_FROM) {
   phase="from";unsigned n=donor_requests++;
   expected=kind==DONOR_CANCEL?TCG_CANCEL:kind==INVALID_DONOR?(n==0?2:n==1?1:TCG_CANCEL):n==0?1:TCG_CANCEL;
  } else {assert(mode==TCG_PLAY_AREA_ENERGY_TO);phase="to";recipient_requests++;expected=kind==RECIPIENT_CANCEL?TCG_CANCEL:2;}
 } else if(r->type==TCG_REQ_YES_NO){assert(r->text==UseThisPokemonPowerText);phase="confirm";expected=1;}
 else assert(r->type==TCG_REQ_ACK);
 if(interactive)debugf("POWER_ASK %s P%u %s answer=%d\n",case_names[kind],side+1,phase,expected);
 /* Procedure/error ACKs are still answered through the real engine, but the
  * targeted native UI coverage is the Power, confirmation and transfer picks. */
 int answer=interactive&&r->type!=TCG_REQ_ACK?on_request(NULL,r):expected;
 assert(answer==expected);fold((unsigned)answer);return answer;
}
static void callbacks(bool ui)
{
 static const TcgHost host={event,request,NULL,NULL};tcg_set_host(&host);interactive=ui;
 observed=(Outcome){.digest=2166136261u};mode=TCG_PLAY_AREA_NORMAL;donor_requests=recipient_requests=0;
}
static Outcome snapshot(void)
{memcpy(observed.wram,gWRAM,sizeof(gWRAM));memcpy(observed.hram,gHRAM,sizeof(gHRAM));return observed;}
static void fixture(void)
{
 memset(gWRAM,0,sizeof(gWRAM));memset(gHRAM,0,sizeof(gHRAM));reset_duel_ui();set_mode(MODE_HOTSEAT);
 copy_str(g_name[0],sizeof(g_name[0]),"P1");copy_str(g_name[1],sizeof(g_name[1]),"P2");gDuelistNames[0]="P1";gDuelistNames[1]="P2";
 hWhoseTurn=gUIDecider=wWhoseTurn=g_turn_holder=side?TCG_OPPONENT:TCG_PLAYER;g_save_ok=false;
 for(unsigned d=0;d<2;d++) {
  uint8_t *v=tcg_duelvars(d?TCG_OPPONENT:TCG_PLAYER),*deck=d?wOpponentDeck:wPlayerDeck;
  memset(deck,GRASS_ENERGY,DECK_SIZE);memset(v+DUELVARS_ARENA_CARD,0xff,MAX_PLAY_AREA_POKEMON);
  v[DUELVARS_DUELIST_TYPE]=DUELIST_TYPE_PLAYER;v[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]=3;
  deck[0]=VENUSAUR_LV67;deck[1]=BULBASAUR;deck[2]=TANGELA_LV8;deck[8]=WATER_ENERGY;
  for(unsigned i=0;i<3;i++){v[DUELVARS_ARENA_CARD+i]=i;v[DUELVARS_ARENA_CARD_HP+i]=card_data(deck[i])->HP;v[i]=CARD_LOCATION_ARENA+i;}
  v[6]=v[7]=v[8]=CARD_LOCATION_ARENA+1;v[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK]=6;
 }
 uint8_t *v=TurnDuelistVars();
 if(kind==NO_GRASS)v[6]=v[7]=CARD_LOCATION_DECK;
 if(kind==STATUS_BLOCKED)v[DUELVARS_ARENA_CARD_STATUS]=ASLEEP;
 if(kind==MUK_BLOCKED)(side?wPlayerDeck:wOpponentDeck)[0]=MUK;
 wRNG1=0x5a;wRNG2=0xc3;wRNGCounter=7;wExcludeArenaPokemon=1;
 log_clear();log_message("Select a Pokemon to retreat.");
}
static void run_case(void)
{
 bool cancel=DisplayPlayAreaScreenToUsePkmnPower();assert(cancel==(kind==POWER_CANCEL));
 if(!cancel){assert(hTemp_ffa0==0&&hTempPlayAreaLocation_ff9d==0);bool blocked=UseAttackOrPokemonPower();if(blocked!=(kind>=NO_GRASS))debugf("POWER unexpected kind=%u blocked=%d text=%u %s\n",kind,blocked,gTextID,tcg_text(gTextID));assert(blocked==(kind>=NO_GRASS));}
 const uint8_t *v=TurnDuelistVars();
 bool moved=kind==TRANSFER||kind==INVALID_DONOR;
 assert(v[6]==(kind==NO_GRASS?CARD_LOCATION_DECK:CARD_LOCATION_ARENA+(moved?2:1)));
 assert(v[7]==(kind==NO_GRASS?CARD_LOCATION_DECK:CARD_LOCATION_ARENA+1));
 assert(v[8]==CARD_LOCATION_ARENA+1&&v[DUELVARS_NUMBER_OF_CARDS_IN_HAND]==0);
 if(kind>=NO_GRASS)assert(!donor_requests&&!recipient_requests);
 if(kind==NO_GRASS)assert(gTextID==NoGrassEnergyText);
 if(kind==STATUS_BLOCKED)assert(gTextID==CannotUseDueToStatusText);
 if(kind==MUK_BLOCKED)assert(gTextID==UnableDueToToxicGasText);
}
static void footer(unsigned context)
{
 if(context!=0xff)draw_play_area_textbox_hint("A: pick  B: back  Z: check");
 else {char prompt[128];text_of(UseThisPokemonPowerText,prompt,sizeof(prompt));draw_prompt_textbox_hint(prompt,"A: choose  B: no");}
}
static void footer_pixels(void)
{
 begin_frame();size_t bytes=(size_t)g_fb->stride*g_fb->height;void *actual=malloc(bytes);assert(actual);
 char saved_log[sizeof(g_log)];memcpy(saved_log,g_log,sizeof(g_log));int saved_n=g_log_n,saved_newest=g_log_newest;
 const unsigned contexts[]={TCG_PLAY_AREA_POWER,TCG_PLAY_AREA_PEEK,TCG_PLAY_AREA_ENERGY_FROM,TCG_PLAY_AREA_ENERGY_TO,TCG_PLAY_AREA_DAMAGE_FROM,TCG_PLAY_AREA_DAMAGE_TO,TCG_PLAY_AREA_CURSE_FROM,TCG_PLAY_AREA_CURSE_TO,TCG_PLAY_AREA_STRANGE_BEHAVIOR,TCG_PLAY_AREA_BENCH_MULTI,0xff};
 for(unsigned i=0;i<sizeof(contexts)/sizeof(contexts[0]);i++) {unsigned context=contexts[i];
  g_play_area_mode=context;graphics_fill_screen(g_fb,g_col[C_BG]);footer(context);memcpy(actual,g_fb->buffer,bytes);
  assert(g_log_n==saved_n&&g_log_newest==saved_newest&&!memcmp(saved_log,g_log,sizeof(g_log)));
  g_log_n=g_log_newest=0;graphics_fill_screen(g_fb,g_col[C_BG]);footer(context);
  assert(!memcmp(actual,g_fb->buffer,bytes));
  memcpy(g_log,saved_log,sizeof(g_log));g_log_n=saved_n;g_log_newest=saved_newest;
 }
 g_play_area_mode=TCG_PLAY_AREA_NORMAL;graphics_fill_screen(g_fb,g_col[C_BG]);draw_play_area_textbox_hint("A: pick  B: back  Z: check");
 assert(memcmp(actual,g_fb->buffer,bytes));free(actual);end_frame();
}

int main(void)
{
 bool high=getenv("MOCK_HIGH_RES")!=NULL;display_init(high?(resolution_t){480,360,0}:RESOLUTION_320x240,DEPTH_16_BPP,3,GAMMA_NONE,FILTERS_RESAMPLE);joypad_init();g_raster_width=high?480:320;
 for(unsigned i=0;i<NUM_COLORS;i++)g_col[i]=graphics_make_color(kRGB[i][0],kRGB[i][1],kRGB[i][2],255);
 for(kind=0;kind<CASES;kind++)for(side=0;side<2;side++) {
  fixture();footer_pixels();callbacks(false);run_case();Outcome reference=snapshot();
  fixture();callbacks(true);debugf("POWER_BEGIN %s P%u\n",case_names[kind],side+1);run_case();Outcome actual=snapshot();
  assert(reference.requests==actual.requests&&reference.digest==actual.digest&&!memcmp(reference.wram,actual.wram,sizeof(gWRAM))&&!memcmp(reference.hram,actual.hram,sizeof(gHRAM)));
  debugf("POWER_RETURN %s P%u state_requests_answers_rng_equal=1\n",case_names[kind],side+1);
 }
 debugf("POWER_FLOW_PASS cases16 stale_retreat_exclude_slot_transfer_cancel_invalid_blocked_noGrass_rng_footer=1\n");
 for(;;){begin_frame();draw_board(0,-1);draw_textbox();end_frame_poll();}
}
