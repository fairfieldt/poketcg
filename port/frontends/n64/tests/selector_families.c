/* Real engine UI shims: list positions, card deck indexes, absolute slots,
 * attack pairs, colors and a controller choosing the other player's cards. */
#ifndef TCGN64_HOST_TEST
#define TCGN64_HOST_TEST
#endif
#define main tcgn64_frontend_main
#include "../tcgn64.c"
#undef main
#include "engine.h"
#include "effects/effects.h"

enum { ATTACK_PICK,ATTACK_CANCEL,ENERGY_PICK,ENERGY_CANCEL,CARD_PICK,CARD_CANCEL,COLOR_PICK,COLOR_CANCEL,TWO_ITEM,PEEK_DECK,PEEK_BACK,CONFIRM_A,CONFIRM_B,BENCH_PICK,BENCH_CANCEL,PROPHECY,GIGASHOCK,GIGA_UNDO,CASES };
static const char *const names[]={"attack","attack-cancel","energy","energy-cancel","card","card-cancel","color","color-cancel","two-item","peek-deck","peek-back","confirm","confirm-cancel","bench","bench-cancel","prophecy","gigashock","giga-undo"};
static const int answers[CASES][8]={{1},{-1},{1},{-1},{1,0},{-1},{WATER},{-1},{1},{1,7,0},{1,-1,0,2,0},{0},{-1},{1},{-1},{0,1,2,0,1,1},{0,0,0,1,0},{0,0,-1,1,0,0}};
static const unsigned lengths[]={1,1,1,1,2,1,1,1,1,3,5,1,1,1,1,6,5,6};
typedef struct {uint8_t wram[WRAM_SIZE],hram[HRAM_SIZE];uint32_t digest;unsigned requests;} Outcome;
static Outcome result;
static unsigned kind,side,answer_index;
static bool interactive,saw_color_text;
static void fold(unsigned n){result.digest=(result.digest^n)*16777619u;}
static void event(void *ctx,const TcgEvent *e)
{
 (void)ctx;fold(e->type);fold(e->text);for(unsigned i=0;i<4;i++)fold(e->arg[i]);
 if((kind==COLOR_PICK||kind==COLOR_CANCEL)&&e->type==TCG_EV_TEXT&&e->text==ChooseWeaknessYouWishToChangeText) {
  assert(e->str&&e->str[0]&&strstr(e->str,"Weakness"));saw_color_text=true;
 }
 if((kind==PEEK_DECK||kind==PEEK_BACK)&&e->type==TCG_EV_SCREEN&&e->arg[0]==TCG_SCREEN_PLAY_AREA)assert(e->arg[3]==TCG_PLAY_AREA_PEEK);
 if(interactive)on_event(NULL,e);
}
static int request(void *ctx,const TcgRequest *r)
{
 (void)ctx;assert(answer_index<lengths[kind]);int expected=answers[kind][answer_index++];
 fold(r->type);fold(r->menu);fold(r->shown);fold(r->duelist);fold(r->cursor);fold(r->num_items);fold(r->text);fold(r->text2);
 for(unsigned i=0;i<4;i++)fold(r->arg[i]);result.requests++;
 assert(r->duelist==(side?TCG_OPPONENT:TCG_PLAYER));
 if(r->type==TCG_REQ_MENU&&r->menu==TCG_MENU_ATTACKS)assert(r->shown!=(side?TCG_OPPONENT:TCG_PLAYER)&&r->num_items==2&&r->items[0]==0&&r->items[1]==0&&r->items[2]==0&&r->items[3]==1);
 if(r->type==TCG_REQ_ENERGY_DISCARD)assert(r->items[0]==9&&r->items[1]==27&&r->items[2]==48&&r->num_items==3&&r->arg[2]==1);
 if(r->type==TCG_REQ_CARD_LIST&&kind==CARD_PICK)assert(r->items[0]==27&&r->items[1]==9&&r->items[2]==48&&r->arg[1]==SELECT_CHECK);
 if(r->type==TCG_REQ_MENU&&r->menu==TCG_MENU_PLAY_AREA)assert(r->arg[0]==1&&r->shown!=(side?TCG_OPPONENT:TCG_PLAYER));
 if(interactive&&kind==PROPHECY&&r->type==TCG_REQ_CARD_LIST&&answer_index>=4) {
  uint8_t *order=g_card_order[side^1];assert(order[5]==1);if(answer_index>=5)assert(order[3]==2);
 }
 if(interactive&&(kind==GIGASHOCK||kind==GIGA_UNDO)&&answer_index==3)assert(g_play_area_marks[side^1][1]==TCG_PLAY_AREA_MARK_TARGET);
 if(interactive&&kind==GIGA_UNDO&&answer_index==4)assert(!g_play_area_marks[side^1][1]);
 if(interactive&&(kind==PEEK_DECK||kind==PEEK_BACK)&&r->type==TCG_REQ_MENU)assert(g_play_area_mode==TCG_PLAY_AREA_PEEK);
 if(interactive)debugf("FAMILY_ASK %s P%u step%u type%u menu%u expected%d\n",names[kind],side+1,answer_index,r->type,r->menu,expected);
 int answer=interactive&&r->type!=TCG_REQ_ACK?on_request(NULL,r):expected;assert(answer==expected);fold((unsigned)answer);return answer;
}
static void callbacks(bool ui)
{static const TcgHost h={event,request,NULL,NULL};interactive=ui;saw_color_text=false;answer_index=0;result=(Outcome){.digest=2166136261u};tcg_set_host(&h);}
static Outcome snapshot(void)
{memcpy(result.wram,gWRAM,sizeof(gWRAM));memcpy(result.hram,gHRAM,sizeof(gHRAM));return result;}
static void fixture(void)
{
 memset(gWRAM,0,sizeof(gWRAM));memset(gHRAM,0,sizeof(gHRAM));reset_duel_ui();set_mode(MODE_HOTSEAT);g_save_ok=false;
 copy_str(g_name[0],sizeof(g_name[0]),"P1");copy_str(g_name[1],sizeof(g_name[1]),"P2");gDuelistNames[0]="P1";gDuelistNames[1]="P2";
 hWhoseTurn=gUIDecider=wWhoseTurn=g_turn_holder=side?TCG_OPPONENT:TCG_PLAYER;
 for(unsigned d=0;d<2;d++) {
  uint8_t *v=tcg_duelvars(d?TCG_OPPONENT:TCG_PLAYER),*deck=d?wOpponentDeck:wPlayerDeck;
  memset(deck,GRASS_ENERGY,DECK_SIZE);memset(v+DUELVARS_ARENA_CARD,0xff,MAX_PLAY_AREA_POKEMON);
  deck[0]=CHARMANDER;deck[1]=STARYU;deck[2]=DROWZEE;deck[9]=FIRE_ENERGY;deck[27]=GRASS_ENERGY;deck[48]=WATER_ENERGY;
  for(unsigned i=0;i<3;i++){v[DUELVARS_ARENA_CARD+i]=i;v[DUELVARS_ARENA_CARD_HP+i]=card_data(deck[i])->HP;v[i]=CARD_LOCATION_ARENA+i;}
  v[9]=v[27]=v[48]=CARD_LOCATION_ARENA+1;v[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]=3;v[DUELVARS_DUELIST_TYPE]=DUELIST_TYPE_PLAYER;
  v[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK]=6;v[DUELVARS_PRIZES]=0x25;
  for(unsigned i=0;i<6;i++)v[DUELVARS_PRIZE_CARDS+i]=35+i;
  const unsigned out[]={0,1,2,9,27,48};unsigned p=0;
  for(unsigned i=0;i<6;i++)v[DUELVARS_DECK_CARDS+p++]=out[i];
  for(unsigned i=0;i<DECK_SIZE;i++)if(i!=0&&i!=1&&i!=2&&i!=9&&i!=27&&i!=48)v[DUELVARS_DECK_CARDS+p++]=i;
 }
 wRNG1=0x5a;wRNG2=0xc3;wRNGCounter=7;log_clear();log_message("Select a Pokemon to retreat.");
}
static void run_case(void)
{
 u8 card=0xff,attack=0xff,color=0xff;bool cancel=kind==ATTACK_CANCEL||kind==ENERGY_CANCEL||kind==CARD_CANCEL||kind==COLOR_CANCEL||kind==CONFIRM_B||kind==BENCH_CANCEL;
 if(kind<=ATTACK_CANCEL){assert(HandleDefendingPokemonAttackSelection(&card,&attack)==cancel);if(!cancel)assert(card==0&&attack==SECOND_ATTACK);}
 else if(kind<=ENERGY_CANCEL){SwapTurn();assert(!CreateArenaOrBenchEnergyCardList(1));DisplayEnergyDiscardScreen(1);assert(HandleEnergyDiscardMenuInput()==cancel);if(!cancel)assert(hTempCardIndex_ff98==27);SwapTurn();}
 else if(kind<=CARD_CANCEL){SwapTurn();wDuelTempList[0]=27;wDuelTempList[1]=9;wDuelTempList[2]=48;wDuelTempList[3]=0xff;InitAndDrawCardListScreenLayout_WithSelectCheckMenu();assert(DisplayCardList(&card)==cancel);if(!cancel)assert(card==9&&hTempCardIndex_ff98==9);SwapTurn();}
 else if(kind<=COLOR_CANCEL){assert(HandleColorChangeScreen(0,ChooseWeaknessYouWishToChangeText,&color)==cancel);assert(color==(cancel?MENU_CANCEL:WATER)&&saw_color_text);}
 else if(kind==TWO_ITEM){assert(TwoItemHorizontalMenu(PleaseSelectTheDeckText));assert(hCurMenuItem==1);}
 else if(kind==PEEK_DECK)assert(HandlePeekSelection()==0xff);
 else if(kind==PEEK_BACK)assert(HandlePeekSelection()==0x42);
 else if(kind<=CONFIRM_B){SwapTurn();wCurPlayAreaSlot=1;tcg_ui_screen(TCG_SCREEN_PLAY_AREA,0,TCG_PLAY_AREA_NORMAL);assert(WaitForButtonAorB()==cancel);SwapTurn();}
 else if(kind<=BENCH_CANCEL){SwapTurn();static const struct MenuParams p={0,3,3,2,SYM_CURSOR_R,SYM_SPACE,0,TCG_MENU_PLAY_AREA};wExcludeArenaPokemon=0;InitializeMenuParameters(0,&p);assert(HandleMenuInput());assert(hCurMenuItem==(cancel?MENU_CANCEL:1));SwapTurn();}
 else if(kind==PROPHECY){assert(!Prophecy_PlayerSelectEffect());assert(hTempList[0]==1&&hTempList[1]==5&&hTempList[2]==3&&hTempList[3]==4&&hTempList[4]==0xff);Prophecy_ReorderDeckEffect();assert(NonTurnDuelistVars()[DUELVARS_DECK_CARDS+6]==5&&NonTurnDuelistVars()[DUELVARS_DECK_CARDS+7]==3&&NonTurnDuelistVars()[DUELVARS_DECK_CARDS+8]==4);}
 else {assert(!Gigashock_PlayerSelectEffect());assert(hTempList[0]==(kind==GIGA_UNDO?2:1)&&hTempList[1]==(kind==GIGA_UNDO?1:2)&&hTempList[2]==0xff);}
 assert(hWhoseTurn==(side?TCG_OPPONENT:TCG_PLAYER)&&answer_index==lengths[kind]);
}
static void presentation_pixels(void)
{
 fixture();callbacks(true);begin_frame();
 size_t bytes=(size_t)g_fb->stride*g_fb->height;void *marked=malloc(bytes),*plain=malloc(bytes);assert(marked&&plain);
 uint8_t ram[WRAM_SIZE],hram[HRAM_SIZE];memcpy(ram,gWRAM,sizeof(ram));memcpy(hram,gHRAM,sizeof(hram));
 DrawSymbolOnPlayAreaCursor(1,SYM_HP_NOK);
 assert(!memcmp(ram,gWRAM,sizeof(ram))&&!memcmp(hram,gHRAM,sizeof(hram)));
 draw_board(TCG_PLAYER,1);draw_textbox();memcpy(marked,g_fb->buffer,bytes);
 draw_card_page(CHARMANDER,TCG_PLAYER,0,NULL);draw_board(TCG_PLAYER,1);draw_textbox();assert(!memcmp(marked,g_fb->buffer,bytes));
 tcg_ui_screen(TCG_SCREEN_PLAY_AREA,0,TCG_PLAY_AREA_DAMAGE_TO);draw_board(TCG_PLAYER,1);draw_textbox();assert(!memcmp(marked,g_fb->buffer,bytes));
 DrawSymbolOnPlayAreaCursor(1,SYM_SPACE);draw_board(TCG_PLAYER,1);draw_textbox();memcpy(plain,g_fb->buffer,bytes);assert(memcmp(marked,plain,bytes));
 DrawSymbolOnPlayAreaCursor(1,SYM_HP_NOK);tcg_ui_screen(TCG_SCREEN_MAIN,1,0);assert(g_play_area_marks[0][1]);
 tcg_ui_screen(TCG_SCREEN_EMPTY,0,0);draw_board(TCG_PLAYER,1);draw_textbox();assert(!memcmp(plain,g_fb->buffer,bytes));
 DrawSymbolOnPlayAreaCursor(5,SYM_LIGHTNING);draw_board(TCG_PLAYER,1);draw_textbox();assert(!memcmp(plain,g_fb->buffer,bytes)); /* empty slot */
 wDuelTempList[0]=27;wDuelTempList[1]=9;wDuelTempList[2]=48;wDuelTempList[3]=0xff;
 wDuelTempList[10]=2;wDuelTempList[11]=0;wDuelTempList[12]=1;PrintSortNumberInCardList_SetPointer();
 memcpy(ram,gWRAM,sizeof(ram));memcpy(hram,gHRAM,sizeof(hram));PrintSortNumberInCardList_CallFromPointer();
 assert(!memcmp(ram,gWRAM,sizeof(ram))&&!memcmp(hram,gHRAM,sizeof(hram)));
 assert(g_card_order[0][27]==2&&!g_card_order[0][9]&&g_card_order[0][48]==1);
 int top=0;draw_card_list_screen("Order",NULL,wDuelTempList,3,TCG_PLAYER,0,&top);memcpy(marked,g_fb->buffer,bytes);
 draw_card_page(CHARMANDER,TCG_PLAYER,0,NULL);draw_card_list_screen("Order",NULL,wDuelTempList,3,TCG_PLAYER,0,&top);assert(!memcmp(marked,g_fb->buffer,bytes));
 tcg_ui_screen(TCG_SCREEN_CARD_LIST,3,0);assert(g_card_order[0][27]==2);
 tcg_ui_screen(TCG_SCREEN_MAIN,0,0);assert(!g_card_order[0][27]&&!g_card_order[0][48]);
 draw_card_list_screen("Order",NULL,wDuelTempList,3,TCG_PLAYER,0,&top);assert(memcmp(marked,g_fb->buffer,bytes));
 free(marked);free(plain);end_frame();debugf("PRESENTATION_MARKS_PASS read_only_nested_cached_empty_redraw_order=1\n");
}
int main(void)
{
 display_init(getenv("MOCK_HIGH_RES")?(resolution_t){480,360,0}:RESOLUTION_320x240,DEPTH_16_BPP,3,GAMMA_NONE,FILTERS_RESAMPLE);joypad_init();
 for(unsigned i=0;i<NUM_COLORS;i++)g_col[i]=graphics_make_color(kRGB[i][0],kRGB[i][1],kRGB[i][2],255);
 presentation_pixels();
 for(kind=0;kind<CASES;kind++)for(side=0;side<2;side++) {
  fixture();callbacks(false);run_case();Outcome reference=snapshot();fixture();callbacks(true);
  debugf("FAMILY_BEGIN %s P%u\n",names[kind],side+1);run_case();Outcome actual=snapshot();
  assert(reference.requests==actual.requests&&reference.digest==actual.digest&&!memcmp(reference.wram,actual.wram,sizeof(gWRAM))&&!memcmp(reference.hram,actual.hram,sizeof(gHRAM)));
  debugf("FAMILY_RETURN %s P%u exact_engine_requests_answers_rng=1\n",names[kind],side+1);
 }
 debugf("SELECTOR_FAMILIES_PASS cases36\n");for(;;){begin_frame();draw_board(0,-1);draw_textbox();end_frame_poll();}
}
