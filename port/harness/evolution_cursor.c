/* Exercise the real hand-play evolution branch and selection UI shims.
 * Including this translation unit exposes its static PlayPokemonCard entry;
 * the target links all other engine sources normally. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/duel_core.c"

enum { EVOLUTION_INDEX=30 };
static unsigned cases,requests,expected_cursor,expected_side;
static int answers[4];
static unsigned answer_count;
static u16 last_text;
static bool capture_request;
static u8 request_wram[WRAM_SIZE],request_hram[HRAM_SIZE];

void tcg_fatal(const char *message)
{fprintf(stderr,"Evolution fixture fatal: %s\n",message);abort();}

static void event(void *ctx,const TcgEvent *ev)
{
 (void)ctx;if(ev->type==TCG_EV_TEXT&&ev->text)last_text=ev->text;
}

static int request(void *ctx,const TcgRequest *r)
{
 (void)ctx;
 if(r->type==TCG_REQ_ACK)return 0;
 assert(r->type==TCG_REQ_PLAY_AREA&&requests<answer_count);
 assert(r->cursor==expected_cursor&&r->shown==expected_side&&r->duelist==expected_side);
 assert(r->arg[0]==0&&r->arg[1]==0&&r->num_items>=1&&r->num_items<=6);
 if(capture_request){memcpy(request_wram,gWRAM,sizeof(request_wram));memcpy(request_hram,gHRAM,sizeof(request_hram));}
 return answers[requests++];
}

static const TcgHost host={event,request,NULL,NULL};

static void place(u8 side,unsigned slot,u8 id,unsigned hp,unsigned flags)
{
 u8 *vars=DuelVarsOf(side),*deck=side==PLAYER_TURN?wPlayerDeck:wOpponentDeck;
 assert(slot<6);deck[slot]=id;vars[slot]=(u8)(CARD_LOCATION_PLAY_AREA|slot);
 vars[DUELVARS_ARENA_CARD+slot]=(u8)slot;
 vars[DUELVARS_ARENA_CARD_HP+slot]=(u8)hp;
 vars[DUELVARS_ARENA_CARD_FLAGS+slot]=(u8)flags;
 vars[DUELVARS_ARENA_CARD_STAGE+slot]=id==MYSTERIOUS_FOSSIL?BASIC:tcg_card_data(id)[CARD_DATA_STAGE];
 if(vars[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]<=slot)
  vars[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]=(u8)(slot+1);
}

static void setup(u8 side,unsigned count,u8 evolution)
{
 memset(gWRAM,0,sizeof(gWRAM));memset(gHRAM,0,sizeof(gHRAM));gUIDecider=0;gTextID=0;
 memset(wPlayerDeck,WATER_ENERGY,60);memset(wOpponentDeck,WATER_ENERGY,60);
 for(unsigned who=0;who<2;who++) {
  u8 *vars=DuelVarsOf(who?OPPONENT_TURN:PLAYER_TURN);
  /* The source Bench array has five slots plus its mandatory FF terminator. */
  memset(vars+DUELVARS_ARENA_CARD,0xff,7);vars[DUELVARS_DUELIST_TYPE]=DUELIST_TYPE_PLAYER;
 }
 hWhoseTurn=wWhoseTurn=side;wDuelTurns=4;wRNG1=0x57;wRNG2=0x31;wRNGCounter=0xa9;
 for(unsigned slot=0;slot<count;slot++)place(side,slot,PIKACHU_LV12,40,CAN_EVOLVE_THIS_TURN);
 place(side==PLAYER_TURN?OPPONENT_TURN:PLAYER_TURN,0,PIKACHU_LV12,40,CAN_EVOLVE_THIS_TURN);
 u8 *vars=TurnDuelistVars(),*deck=side==PLAYER_TURN?wPlayerDeck:wOpponentDeck;
 deck[EVOLUTION_INDEX]=evolution;vars[EVOLUTION_INDEX]=CARD_LOCATION_HAND;
 vars[DUELVARS_HAND]=EVOLUTION_INDEX;vars[DUELVARS_NUMBER_OF_CARDS_IN_HAND]=1;
 vars[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK]=(u8)(count+1);
 vars[DUELVARS_ARENA_CARD_STATUS]=POISONED;
 hTempCardIndex_ff98=EVOLUTION_INDEX;LoadCardDataToBuffer1_FromDeckIndex(EVOLUTION_INDEX);
 expected_side=side;requests=answer_count=0;expected_cursor=0;last_text=0;capture_request=false;
 tcg_set_host(&host);
}

static void select_answers(unsigned cursor,int first,int second)
{
 expected_cursor=cursor;answers[0]=first;answers[1]=second;
 answer_count=second==999?1:2;
}

static void rng_unchanged(void)
{assert(wRNG1==0x57&&wRNG2==0x31&&wRNGCounter==0xa9);}

static void evolve(unsigned preferred,unsigned chosen)
{
 u8 *vars=TurnDuelistVars(),*deck=hWhoseTurn==PLAYER_TURN?wPlayerDeck:wOpponentDeck;
 u8 evolution=deck[EVOLUTION_INDEX],base=deck[vars[DUELVARS_ARENA_CARD+chosen]];
 unsigned base_hp=base==MYSTERIOUS_FOSSIL?10:tcg_card_data(base)[CARD_DATA_HP];
 unsigned expected_hp=tcg_card_data(evolution)[CARD_DATA_HP]-base_hp+vars[DUELVARS_ARENA_CARD_HP+chosen];
 unsigned status=vars[DUELVARS_ARENA_CARD_STATUS];
 select_answers(preferred,(int)chosen,999);assert(!PlayPokemonCard());
 assert(requests==1&&vars[DUELVARS_ARENA_CARD+chosen]==EVOLUTION_INDEX);
 assert(vars[DUELVARS_ARENA_CARD_HP+chosen]==expected_hp);
 assert(vars[DUELVARS_ARENA_CARD_STAGE+chosen]==tcg_card_data(evolution)[CARD_DATA_STAGE]);
 assert(vars[DUELVARS_ARENA_CARD_FLAGS+chosen]==0);
 assert(vars[DUELVARS_ARENA_CARD_STATUS]==(chosen?status:NO_STATUS));
 assert(vars[EVOLUTION_INDEX]==(CARD_LOCATION_PLAY_AREA|chosen));
 assert(vars[DUELVARS_NUMBER_OF_CARDS_IN_HAND]==0&&hOppActionTableIndex==OPPACTION_EVOLVE_PKMN);
 rng_unchanged();cases++;
}

static void normal_evolutions(void)
{
 /* First legal slot wins in every position, with real incompatible cards
    before it; this also covers a full six-Pokemon play area. */
 for(unsigned target=0;target<6;target++) {
  setup(PLAYER_TURN,6,SEAKING);place(PLAYER_TURN,target,GOLDEEN,30,CAN_EVOLVE_THIS_TURN);
  evolve(target,target);
 }
 setup(PLAYER_TURN,3,SEAKING);
 for(unsigned slot=0;slot<3;slot++)place(PLAYER_TURN,slot,GOLDEEN,30,CAN_EVOLVE_THIS_TURN);
 evolve(0,0); /* Active before eligible Bench */
 setup(PLAYER_TURN,5,SEAKING);place(PLAYER_TURN,2,GOLDEEN,30,CAN_EVOLVE_THIS_TURN);
 place(PLAYER_TURN,4,GOLDEEN,30,CAN_EVOLVE_THIS_TURN);evolve(2,2);
 setup(PLAYER_TURN,2,SEAKING);place(PLAYER_TURN,0,GOLDEEN,30,0);
 place(PLAYER_TURN,1,GOLDEEN,30,CAN_EVOLVE_THIS_TURN);evolve(1,1); /* newly placed Active */
 setup(PLAYER_TURN,2,BLASTOISE);place(PLAYER_TURN,0,WARTORTLE,40,0);
 place(PLAYER_TURN,1,WARTORTLE,40,CAN_EVOLVE_THIS_TURN);evolve(1,1); /* already evolved this turn */
 setup(PLAYER_TURN,1,BLASTOISE);place(PLAYER_TURN,0,WARTORTLE,40,CAN_EVOLVE_THIS_TURN);evolve(0,0);
 setup(OPPONENT_TURN,4,SEAKING);place(OPPONENT_TURN,3,GOLDEEN,20,CAN_EVOLVE_THIS_TURN);evolve(3,3);
 setup(PLAYER_TURN,2,SEAKING);place(PLAYER_TURN,0,GOLDEEN,0,CAN_EVOLVE_THIS_TURN);
 place(PLAYER_TURN,1,GOLDEEN,30,CAN_EVOLVE_THIS_TURN);evolve(1,1); /* KO'd match is not selectable */
 setup(PLAYER_TURN,1,OMANYTE);place(PLAYER_TURN,0,MYSTERIOUS_FOSSIL,10,CAN_EVOLVE_THIS_TURN);evolve(0,0);
}

static void rejection_and_choices(void)
{
 setup(PLAYER_TURN,2,SEAKING);assert(PlayPokemonCard());
 assert(!requests&&last_text==NoPokemonCapableOfEvolvingText);rng_unchanged();cases++;
 setup(PLAYER_TURN,2,SEAKING);wDuelTurns=0;
 place(PLAYER_TURN,0,GOLDEEN,30,0);assert(PlayPokemonCard());
 assert(!requests&&last_text==CantEvolvePokemonInSameTurnItsPlacedText);rng_unchanged();cases++;
 setup(PLAYER_TURN,2,SEAKING);place(PLAYER_TURN,0,GOLDEEN,0,0);assert(PlayPokemonCard());
 assert(!requests&&last_text==NoPokemonCapableOfEvolvingText);rng_unchanged();cases++;
 setup(PLAYER_TURN,2,SEAKING);place(PLAYER_TURN,0,GOLDEEN,0,CAN_EVOLVE_THIS_TURN);assert(PlayPokemonCard());
 assert(!requests&&last_text==NoPokemonCapableOfEvolvingText);rng_unchanged();cases++;
 setup(PLAYER_TURN,2,SEAKING);place(PLAYER_TURN,1,GOLDEEN,30,CAN_EVOLVE_THIS_TURN);
 place(OPPONENT_TURN,1,AERODACTYL,60,CAN_EVOLVE_THIS_TURN);assert(PlayPokemonCard());
 assert(!requests&&last_text==UnableToEvolveDueToPrehistoricPowerText);rng_unchanged();cases++;
 setup(PLAYER_TURN,2,SEAKING);place(PLAYER_TURN,1,GOLDEEN,30,CAN_EVOLVE_THIS_TURN);
 place(OPPONENT_TURN,1,AERODACTYL,60,CAN_EVOLVE_THIS_TURN);
 place(OPPONENT_TURN,2,MUK,70,CAN_EVOLVE_THIS_TURN);evolve(1,1); /* Toxic Gas suppresses the ban */
 setup(PLAYER_TURN,1,SEAKING);place(PLAYER_TURN,0,GOLDEEN,30,CAN_EVOLVE_THIS_TURN);
 place(OPPONENT_TURN,0,AERODACTYL,60,CAN_EVOLVE_THIS_TURN);
 DuelVarsOf(OPPONENT_TURN)[DUELVARS_ARENA_CARD_STATUS]=ASLEEP;evolve(0,0);
 setup(PLAYER_TURN,2,SEAKING);place(PLAYER_TURN,1,GOLDEEN,30,CAN_EVOLVE_THIS_TURN);
 select_answers(1,TCG_CANCEL,999);assert(!PlayPokemonCard());
 assert(requests==1&&TurnDuelistVars()[EVOLUTION_INDEX]==CARD_LOCATION_HAND&&
  TurnDuelistVars()[DUELVARS_NUMBER_OF_CARDS_IN_HAND]==1);
 rng_unchanged();cases++;
 setup(PLAYER_TURN,3,SEAKING);place(PLAYER_TURN,1,GOLDEEN,30,CAN_EVOLVE_THIS_TURN);
 place(PLAYER_TURN,2,GOLDEEN,20,CAN_EVOLVE_THIS_TURN);evolve(1,2); /* user may pick another legal target */
}

static void wrapper_parity(void)
{
 static u8 initial_wram[WRAM_SIZE],initial_hram[HRAM_SIZE],after_wram[WRAM_SIZE],after_hram[HRAM_SIZE];
 static u8 before_wram[WRAM_SIZE],before_hram[HRAM_SIZE];
 setup(PLAYER_TURN,3,SEAKING);HasAlivePokemonInPlayArea(NULL);
 memcpy(initial_wram,gWRAM,sizeof(gWRAM));memcpy(initial_hram,gHRAM,sizeof(gHRAM));
 capture_request=true;select_answers(0,2,999);assert(!OpenPlayAreaScreenForSelection());
 memcpy(before_wram,request_wram,sizeof(before_wram));memcpy(before_hram,request_hram,sizeof(before_hram));
 memcpy(after_wram,gWRAM,sizeof(after_wram));memcpy(after_hram,gHRAM,sizeof(after_hram));rng_unchanged();
 memcpy(gWRAM,initial_wram,sizeof(gWRAM));memcpy(gHRAM,initial_hram,sizeof(gHRAM));
 requests=0;select_answers(2,2,999);assert(!OpenPlayAreaScreenForEvolutionSelection(2));
 assert(!memcmp(before_wram,request_wram,sizeof(before_wram))&&!memcmp(before_hram,request_hram,sizeof(before_hram)));
 assert(!memcmp(after_wram,gWRAM,sizeof(after_wram))&&!memcmp(after_hram,gHRAM,sizeof(after_hram)));rng_unchanged();
 /* An out-of-range preferred cursor falls back to the ordinary default. */
 memcpy(gWRAM,initial_wram,sizeof(gWRAM));memcpy(gHRAM,initial_hram,sizeof(gHRAM));
 requests=0;select_answers(0,2,999);assert(!OpenPlayAreaScreenForEvolutionSelection(255));
 assert(!memcmp(after_wram,gWRAM,sizeof(after_wram))&&!memcmp(after_hram,gHRAM,sizeof(after_hram)));
 cases+=3;
}

int main(void)
{
 normal_evolutions();rejection_and_choices();wrapper_parity();
 printf("EVOLUTION CURSOR PASS: %u real-engine cases; all6 slots, Active/Bench priority, placement/evolution flags, HP0, Stage2, Fossil, P2, Prehistoric/Muk/status, cancel/manual choice, unchanged RNG and exact wrapper WRAM/HRAM parity\n",cases);
 return 0;
}
