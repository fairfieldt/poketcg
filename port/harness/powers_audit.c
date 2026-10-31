/* Outcome regressions for all 26 original Pokemon Power mechanisms. The
 * fixture uses the actual command dispatcher, UI request shims and source RNG;
 * only human answers are supplied by the host. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Expose the source's private hand-play functions so Rain Dance and the four
 * entry triggers are also tested through their real gameplay callers. */
#include "../src/duel_core.c"
#include "../src/effects/effects.h"
extern bool ParalysisEffect(void);

static unsigned cases, answers_used, answers_count, coins, heads, cards_seen;
static int answers[32];
static const char *label;
static u8 side;

void tcg_fatal(const char *s) { fprintf(stderr,"Power fixture %s: %s\n",label,s); abort(); }
static void event(void *ctx,const TcgEvent *e)
{
 (void)ctx;
 if(e->type==TCG_EV_COIN){coins++;heads+=e->arg[0]!=0;}
 if(e->type==TCG_EV_CARD)cards_seen++;
}
static int request(void *ctx,const TcgRequest *r)
{
 (void)ctx;
 if(r->type==TCG_REQ_ACK)return 0;
 if(r->type==TCG_REQ_CONFIRM)return 0;
 if(r->type==TCG_REQ_CARD_LIST&&r->arg[0])return TCG_CANCEL;
 if(answers_used>=answers_count){fprintf(stderr,"%s: unexpected request type %u menu %u\n",label,r->type,r->menu);abort();}
 return answers[answers_used++];
}
static const TcgHost host={event,request,NULL,NULL};
static u8 *v(u8 who){return DuelVarsOf(who);}
static u8 *deck(u8 who){return who==PLAYER_TURN?wPlayerDeck:wOpponentDeck;}
static u8 other(void){return side==PLAYER_TURN?OPPONENT_TURN:PLAYER_TURN;}
static void place(u8 who,unsigned slot,u8 id,unsigned hp)
{
 u8 *p=v(who);deck(who)[slot]=id;p[slot]=(u8)(CARD_LOCATION_PLAY_AREA|slot);
 p[DUELVARS_ARENA_CARD+slot]=(u8)slot;p[DUELVARS_ARENA_CARD_HP+slot]=(u8)hp;
 p[DUELVARS_ARENA_CARD_STAGE+slot]=tcg_card_data(id)[CARD_DATA_STAGE];
 p[DUELVARS_ARENA_CARD_FLAGS+slot]=CAN_EVOLVE_THIS_TURN;
 if(p[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]<=slot)p[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]=(u8)(slot+1);
}
static void setup(const char *name,u8 who)
{
 label=name;side=who;memset(gWRAM,0,WRAM_SIZE);memset(gHRAM,0,HRAM_SIZE);gUIDecider=0;gTextID=0;
 memset(wPlayerDeck,WATER_ENERGY,60);memset(wOpponentDeck,WATER_ENERGY,60);
 for(unsigned p=0;p<2;p++){
  u8 turn=p?OPPONENT_TURN:PLAYER_TURN,*vars=v(turn);
  memset(vars+DUELVARS_ARENA_CARD,0xff,7);vars[DUELVARS_DUELIST_TYPE]=DUELIST_TYPE_PLAYER;
  for(unsigned i=0;i<60;i++)vars[DUELVARS_DECK_CARDS+i]=(u8)i;
  place(turn,0,CHANSEY,120);place(turn,1,CHANSEY,120);place(turn,2,CHANSEY,120);
  vars[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK]=3;
 }
 hWhoseTurn=wWhoseTurn=who;wDuelTurns=6;wRNG1=0x57;wRNG2=0x31;wRNGCounter=0xa9;
 answers_used=answers_count=coins=heads=cards_seen=0;tcg_set_host(&host);
}
static void script(const int *a,unsigned n){assert(n<=32);memcpy(answers,a,n*sizeof(*a));answers_used=0;answers_count=n;}
#define ANSWERS(...) do { const int a[]={__VA_ARGS__};script(a,sizeof(a)/sizeof(a[0])); } while(0)
static void done(void){assert(answers_used==answers_count);assert(hWhoseTurn==side&&wWhoseTurn==side);cases++;}
static void unchanged_rng(void){assert(wRNG1==0x57&&wRNG2==0x31&&wRNGCounter==0xa9);}
static void load_power(unsigned slot)
{
 hTempPlayAreaLocation_ff9d=(u8)slot;hTempCardIndex_ff98=v(side)[DUELVARS_ARENA_CARD+slot];
 CopyAttackDataAndDamage_FromDeckIndex(hTempCardIndex_ff98,FIRST_ATTACK_OR_PKMN_POWER);
 assert(wLoadedAttack.Category==POKEMON_POWER);
}
static void attach(unsigned index,u8 id,unsigned slot)
{deck(side)[index]=id;v(side)[index]=(u8)(CARD_LOCATION_PLAY_AREA|slot);}

static void gates(void)
{
 static const u8 statuses[]={NO_STATUS,POISONED,DOUBLE_POISONED,ASLEEP,CONFUSED,PARALYZED,POISONED|ASLEEP};
 static const u8 manual[]={VENUSAUR_LV64,VENUSAUR_LV67,VILEPLUME,VENOMOTH,TENTACOOL,MANKEY,ALAKAZAM,SLOWBRO,GENGAR,DRAGONITE_LV45};
 for(unsigned p=0;p<2;p++)for(unsigned i=0;i<sizeof(manual);i++)for(unsigned s=0;s<sizeof(statuses);s++){
  setup("status gate",p?OPPONENT_TURN:PLAYER_TURN);place(side,0,manual[i],60);v(side)[DUELVARS_ARENA_CARD_STATUS]=statuses[s];
  bool blocked=CheckIsIncapableOfUsingPkmnPower(0);assert(blocked==!!(statuses[s]&CNF_SLP_PRZ));
  assert(!CheckIsIncapableOfUsingPkmnPower(1));unchanged_rng();done();
 }
 for(unsigned p=0;p<2;p++)for(unsigned s=0;s<sizeof(statuses);s++){
  setup("Toxic Gas",p?OPPONENT_TURN:PLAYER_TURN);place(other(),0,MUK,70);v(other())[DUELVARS_ARENA_CARD_STATUS]=statuses[s];
  assert(CheckIsIncapableOfUsingPkmnPower(1)==!(statuses[s]&CNF_SLP_PRZ));
  place(other(),1,MUK,70);assert(CheckIsIncapableOfUsingPkmnPower(1));unchanged_rng();done();
 }
}

static void command_gates(void)
{
 static const u8 manual[]={VENUSAUR_LV64,VENUSAUR_LV67,VILEPLUME,VENOMOTH,TENTACOOL,MANKEY,ALAKAZAM,SLOWBRO,GENGAR,DRAGONITE_LV45};
 static const u8 once[]={VENUSAUR_LV64,VILEPLUME,VENOMOTH,MANKEY,GENGAR,DRAGONITE_LV45};
 static const u8 passive[]={MUK,CHARIZARD,BLASTOISE,OMANYTE,MACHAMP,KABUTO,AERODACTYL,HAUNTER_LV17,MR_MIME,MEW_LV8,DODRIO,SNORLAX};
 for(unsigned p=0;p<2;p++)for(unsigned i=0;i<sizeof(manual);i++)for(unsigned restriction=0;restriction<5;restriction++){
  setup("manual command capability",p?OPPONENT_TURN:PLAYER_TURN);
  unsigned slot=manual[i]==DRAGONITE_LV45?1:0;place(side,slot,manual[i],20);
  v(other())[DUELVARS_ARENA_CARD_HP+1]=100;v(side)[DUELVARS_ARENA_CARD_STATUS]=POISONED;attach(20,GRASS_ENERGY,2);
  if(restriction==1)v(side)[DUELVARS_ARENA_CARD_STATUS]=POISONED|ASLEEP;
  if(restriction==2)place(other(),2,MUK,70);
  if(restriction==3){place(other(),0,MUK,70);v(other())[DUELVARS_ARENA_CARD_STATUS]=ASLEEP;}
  if(restriction==4){place(other(),0,MUK,70);v(other())[DUELVARS_ARENA_CARD_STATUS]=POISONED;}
  load_power(slot);bool blocked=TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_2);
  bool expected=!!((restriction==1&&!slot)||restriction==2||restriction==4);
  if(blocked!=expected)fprintf(stderr,"manual id %u restriction %u slot %u blocked %u text %u\n",manual[i],restriction,slot,blocked,gTextID);
  assert(blocked==expected);unchanged_rng();done();
 }
 for(unsigned i=0;i<sizeof(once);i++){
  setup("once-per-turn reset",PLAYER_TURN);place(side,1,once[i],20);v(side)[DUELVARS_ARENA_CARD_STATUS]=POISONED;v(other())[DUELVARS_ARENA_CARD_HP]=100;
  load_power(1);v(side)[DUELVARS_ARENA_CARD_FLAGS+1]|=USED_PKMN_POWER_THIS_TURN;
  assert(TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_2)&&gTextID==OnlyOncePerTurnText);
  SetAllPlayAreaPokemonCanEvolve();assert(!TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_2));
  assert(v(side)[DUELVARS_ARENA_CARD_FLAGS+1]==CAN_EVOLVE_THIS_TURN);unchanged_rng();done();
 }
 for(unsigned i=0;i<sizeof(passive);i++){
  setup("passive not manually selectable",PLAYER_TURN);place(side,1,passive[i],20);load_power(1);
  assert(TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_1));unchanged_rng();done();
 }
 setup("Toxic Gas clears Shift colors",PLAYER_TURN);
 for(unsigned who=0;who<2;who++)memset(v(who?OPPONENT_TURN:PLAYER_TURN)+DUELVARS_ARENA_CARD_CHANGED_TYPE,HAS_CHANGED_COLOR|FIRE,6);
 deck(side)[20]=MUK;ClearChangedTypesIfMuk(20);
 for(unsigned who=0;who<2;who++)for(unsigned slot=0;slot<6;slot++)assert(v(who?OPPONENT_TURN:PLAYER_TURN)[DUELVARS_ARENA_CARD_CHANGED_TYPE+slot]==0);
 unchanged_rng();done();
}

static void hand(unsigned index,u8 id)
{
 deck(side)[index]=id;u8 *vars=v(side);vars[index]=CARD_LOCATION_HAND;
 vars[DUELVARS_HAND+vars[DUELVARS_NUMBER_OF_CARDS_IN_HAND]++]=(u8)index;hTempCardIndex_ff98=(u8)index;
 LoadCardDataToBuffer1_FromDeckIndex((u8)index);
}

static void hand_play(void)
{
 for(unsigned p=0;p<2;p++){
  setup("Rain Dance repeated hand attachments",p?OPPONENT_TURN:PLAYER_TURN);place(side,1,BLASTOISE,100);
  hand(20,WATER_ENERGY);ANSWERS(1);assert(PlayEnergyCard(TYPE_ENERGY_WATER)==NEXT_DUEL_MAIN_INTERFACE);
  assert(v(side)[20]==(CARD_LOCATION_PLAY_AREA|1)&&!wAlreadyPlayedEnergy);
  hand(21,WATER_ENERGY);ANSWERS(1);assert(PlayEnergyCard(TYPE_ENERGY_WATER)==NEXT_DUEL_MAIN_INTERFACE);
  assert(v(side)[21]==(CARD_LOCATION_PLAY_AREA|1)&&!wAlreadyPlayedEnergy);
  hand(22,FIRE_ENERGY);ANSWERS(0);assert(PlayEnergyCard(TYPE_ENERGY_FIRE)==NEXT_DUEL_MAIN_INTERFACE);
  assert(v(side)[22]==CARD_LOCATION_ARENA&&wAlreadyPlayedEnergy);
  hand(23,WATER_ENERGY);ANSWERS(1);assert(PlayEnergyCard(TYPE_ENERGY_WATER)==NEXT_DUEL_MAIN_INTERFACE);
  assert(v(side)[23]==(CARD_LOCATION_PLAY_AREA|1)&&wAlreadyPlayedEnergy);
  hand(24,WATER_ENERGY);ANSWERS(0,TCG_CANCEL);assert(PlayEnergyCard(TYPE_ENERGY_WATER)==NEXT_DUEL_MAIN_INTERFACE);
  assert(v(side)[24]==CARD_LOCATION_HAND&&v(side)[DUELVARS_NUMBER_OF_CARDS_IN_HAND]==1);unchanged_rng();done();
 }
 static const u8 basics[]={MOLTRES_LV37,ARTICUNO_LV37,ZAPDOS_LV68};
 for(unsigned i=0;i<3;i++){
  setup("basic hand-play trigger",PLAYER_TURN);hand(20,basics[i]);assert(!PlayPokemonCard());
  assert(v(side)[20]==(CARD_LOCATION_PLAY_AREA|3)&&v(side)[DUELVARS_ARENA_CARD+3]==20&&v(side)[DUELVARS_NUMBER_OF_CARDS_IN_HAND]==0);
  if(i==1)assert(coins==1);done();
 }
 setup("evolved Healing Wind hand-play",PLAYER_TURN);place(side,1,DRAGONAIR,40);v(side)[DUELVARS_ARENA_CARD_HP]=100;
 hand(20,DRAGONITE_LV41);ANSWERS(1);assert(!PlayPokemonCard());
 assert(v(side)[DUELVARS_ARENA_CARD+1]==20&&v(side)[DUELVARS_ARENA_CARD_HP+1]==80&&v(side)[DUELVARS_ARENA_CARD_HP]==120);
 assert(v(side)[DUELVARS_NUMBER_OF_CARDS_IN_HAND]==0);unchanged_rng();done();
}

static void power_menu_and_failures(void)
{
 for(unsigned p=0;p<2;p++){
  setup("actual Power menu source deck index",p?OPPONENT_TURN:PLAYER_TURN);place(side,2,VENUSAUR_LV64,100);
  deck(side)[30]=VENUSAUR_LV64;v(side)[30]=(CARD_LOCATION_PLAY_AREA|2);v(side)[2]=CARD_LOCATION_DECK;v(side)[DUELVARS_ARENA_CARD+2]=30;
  v(side)[DUELVARS_ARENA_CARD_STATUS]=ASLEEP;v(other())[DUELVARS_ARENA_CARD_STATUS]=POISONED;
  ANSWERS(2,1);assert(DuelMenu_PkmnPower()==NEXT_DUEL_MAIN_INTERFACE);
  assert(v(side)[DUELVARS_ARENA_CARD_FLAGS+2]&USED_PKMN_POWER_THIS_TURN);assert(!v(side)[DUELVARS_ARENA_CARD_FLAGS+1]||v(side)[DUELVARS_ARENA_CARD_FLAGS+1]==CAN_EVOLVE_THIS_TURN);
  assert(!v(side)[DUELVARS_ARENA_CARD_STATUS]&&!v(other())[DUELVARS_ARENA_CARD_STATUS]);unchanged_rng();done();
  setup("Power menu declined then cancelled",p?OPPONENT_TURN:PLAYER_TURN);place(side,1,VENUSAUR_LV64,100);v(other())[DUELVARS_ARENA_CARD_STATUS]=POISONED;
  ANSWERS(1,0,TCG_CANCEL);assert(DuelMenu_PkmnPower()==NEXT_DUEL_MAIN_INTERFACE);
  assert(v(other())[DUELVARS_ARENA_CARD_STATUS]==POISONED&&!(v(side)[DUELVARS_ARENA_CARD_FLAGS+1]&USED_PKMN_POWER_THIS_TURN));unchanged_rng();done();
 }
 static const u8 requires_damage[]={VILEPLUME,ALAKAZAM,SLOWBRO,GENGAR};
 for(unsigned i=0;i<sizeof(requires_damage);i++){
  setup("no damage counters",PLAYER_TURN);place(side,1,requires_damage[i],tcg_card_data(requires_damage[i])[CARD_DATA_HP]);load_power(1);
  assert(TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_2)&&gTextID==NoPokemonWithDamageCountersText);unchanged_rng();done();
 }
 setup("Curse only one opposing Pokemon",PLAYER_TURN);place(side,1,GENGAR,80);v(other())[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]=1;
 v(other())[DUELVARS_ARENA_CARD_HP]=100;load_power(1);assert(TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_2)&&gTextID==CannotUseSinceTheresOnly1PkmnText);unchanged_rng();done();
 setup("Cowardice only one Pokemon",PLAYER_TURN);place(side,0,TENTACOOL,40);v(side)[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]=1;load_power(0);
 assert(TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_2)&&gTextID==EffectNoPokemonOnTheBenchText);unchanged_rng();done();
}

static void manual_powers(u8 who)
{
 setup("Solar Power",who);place(side,1,VENUSAUR_LV64,100);load_power(1);
 assert(TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_2));
 assert(gTextID==NotAffectedByPoisonSleepParalysisOrConfusionText);
 v(side)[DUELVARS_ARENA_CARD_STATUS]=POISONED;v(other())[DUELVARS_ARENA_CARD_STATUS]=PARALYZED;
 assert(!UsePokemonPower());assert(!v(side)[DUELVARS_ARENA_CARD_STATUS]&&!v(other())[DUELVARS_ARENA_CARD_STATUS]);
 assert(v(side)[DUELVARS_ARENA_CARD_FLAGS+1]&USED_PKMN_POWER_THIS_TURN);
 assert(TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_2)&&gTextID==OnlyOncePerTurnText);unchanged_rng();done();

 setup("Energy Trans",who);place(side,1,VENUSAUR_LV67,100);load_power(1);
 assert(TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_2)&&gTextID==NoGrassEnergyText);
 attach(20,GRASS_ENERGY,0);attach(21,FIRE_ENERGY,0);attach(22,GRASS_ENERGY,2);
 ANSWERS(0,1,2,TCG_CANCEL,TCG_CANCEL);assert(!UsePokemonPower());
 assert(v(side)[20]==(CARD_LOCATION_PLAY_AREA|1)&&v(side)[22]==(CARD_LOCATION_PLAY_AREA|2));
 assert(v(side)[21]==CARD_LOCATION_ARENA&&v(side)[DUELVARS_NUMBER_OF_CARDS_IN_HAND]==0);
 assert(!(v(side)[DUELVARS_ARENA_CARD_FLAGS+1]&USED_PKMN_POWER_THIS_TURN));unchanged_rng();done();

 setup("Shift",who);place(side,1,VENOMOTH,70);place(other(),2,PIKACHU_LV12,40);load_power(1);
 ANSWERS(FIGHTING,LIGHTNING);assert(!UsePokemonPower());
 assert(v(side)[DUELVARS_ARENA_CARD_CHANGED_TYPE+1]==(HAS_CHANGED_COLOR|LIGHTNING));
 assert(TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_2)&&gTextID==OnlyOncePerTurnText);unchanged_rng();done();
 setup("Shift cancel",who);place(side,1,VENOMOTH,70);load_power(1);ANSWERS(TCG_CANCEL);
 assert(UsePokemonPower());assert(!v(side)[DUELVARS_ARENA_CARD_CHANGED_TYPE+1]);
 assert(!(v(side)[DUELVARS_ARENA_CARD_FLAGS+1]&USED_PKMN_POWER_THIS_TURN));unchanged_rng();done();

 setup("Damage Swap",who);place(side,1,ALAKAZAM,80);v(side)[DUELVARS_ARENA_CARD_HP]=100;v(side)[DUELVARS_ARENA_CARD_HP+2]=10;load_power(1);
 ANSWERS(0,2,1,TCG_CANCEL);assert(!UsePokemonPower());
 assert(v(side)[DUELVARS_ARENA_CARD_HP]==110&&v(side)[DUELVARS_ARENA_CARD_HP+1]==70&&v(side)[DUELVARS_ARENA_CARD_HP+2]==10);
 assert(!(v(side)[DUELVARS_ARENA_CARD_FLAGS+1]&USED_PKMN_POWER_THIS_TURN));unchanged_rng();done();
 setup("Damage Swap cancel transfer",who);place(side,1,ALAKAZAM,80);v(side)[DUELVARS_ARENA_CARD_HP]=100;load_power(1);
 ANSWERS(0,TCG_CANCEL,TCG_CANCEL);assert(!UsePokemonPower());assert(v(side)[DUELVARS_ARENA_CARD_HP]==100);unchanged_rng();done();

 setup("Strange Behavior",who);place(side,1,SLOWBRO,30);v(side)[DUELVARS_ARENA_CARD_HP]=90;load_power(1);
 ANSWERS(1,0,0,0,TCG_CANCEL);assert(!UsePokemonPower());
 assert(v(side)[DUELVARS_ARENA_CARD_HP]==110&&v(side)[DUELVARS_ARENA_CARD_HP+1]==10);
 assert(TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_2)&&gTextID==CannotUseBecauseItWillBeKnockedOutText);unchanged_rng();done();

 setup("Step In",who);place(side,1,DRAGONITE_LV45,100);load_power(1);assert(!UsePokemonPower());
 assert(v(side)[DUELVARS_ARENA_CARD]==1&&v(side)[DUELVARS_BENCH]==0);
 assert(v(side)[DUELVARS_ARENA_CARD_FLAGS]&USED_PKMN_POWER_THIS_TURN);
 load_power(0);assert(TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_2)&&gTextID==CanOnlyBeUsedOnTheBenchText);unchanged_rng();done();

 for(unsigned active=0;active<2;active++){
  unsigned slot=active?0:1;setup("Cowardice",who);place(side,slot,TENTACOOL,40);attach(20,WATER_ENERGY,slot);load_power(slot);
  if(active){ANSWERS(TCG_CANCEL);assert(UsePokemonPower());assert(v(side)[DUELVARS_ARENA_CARD]==0);ANSWERS(1);}
  assert(!UsePokemonPower());assert(v(side)[slot]==CARD_LOCATION_HAND&&v(side)[20]==CARD_LOCATION_DISCARD_PILE);
  assert(v(side)[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]==2&&v(side)[DUELVARS_NUMBER_OF_CARDS_IN_HAND]==1);unchanged_rng();done();
 }
 setup("Cowardice newly placed",who);place(side,1,TENTACOOL,40);v(side)[DUELVARS_ARENA_CARD_FLAGS+1]=0;load_power(1);
 assert(TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_2)&&gTextID==CannotBeUsedInTurnWhichWasPlayedText);unchanged_rng();done();

 setup("Curse",who);place(side,0,GENGAR,80);v(other())[DUELVARS_ARENA_CARD_HP]=100;load_power(0);
 ANSWERS(0,0,1);assert(!UsePokemonPower());
 assert(v(other())[DUELVARS_ARENA_CARD_HP]==110&&v(other())[DUELVARS_ARENA_CARD_HP+1]==110);
 assert(v(side)[DUELVARS_ARENA_CARD_FLAGS]&USED_PKMN_POWER_THIS_TURN);unchanged_rng();done();
 setup("Curse cancel",who);place(side,1,GENGAR,80);v(other())[DUELVARS_ARENA_CARD_HP]=100;load_power(1);
 ANSWERS(0,TCG_CANCEL,TCG_CANCEL);assert(UsePokemonPower());assert(v(other())[DUELVARS_ARENA_CARD_HP]==100);
 assert(!(v(side)[DUELVARS_ARENA_CARD_FLAGS+1]&USED_PKMN_POWER_THIS_TURN));unchanged_rng();done();

 setup("Peek top deck",who);place(side,1,MANKEY,30);load_power(1);ANSWERS(TCG_CANCEL,0,7);assert(!UsePokemonPower());
 assert(cards_seen&&hAIPkmnPowerEffectParam==0x7f);assert(v(side)[DUELVARS_ARENA_CARD_FLAGS+1]&USED_PKMN_POWER_THIS_TURN);
 assert(TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_2)&&gTextID==OnlyOncePerTurnText);unchanged_rng();done();
}

static void random_powers(void)
{
 unsigned heal_heads=0,freeze_heads=0;
 for(unsigned seed=1;seed<=32;seed++){
  setup("Heal coin",seed&1?PLAYER_TURN:OPPONENT_TURN);place(side,1,VILEPLUME,80);v(side)[DUELVARS_ARENA_CARD_HP]=100;load_power(1);wRNG1=(u8)seed;
  /* Predict only which answers will be requested using the real RNG, then
   * restore all memory before executing the actual command chain. */
  u8 ram[WRAM_SIZE],hram[HRAM_SIZE];memcpy(ram,gWRAM,sizeof(ram));memcpy(hram,gHRAM,sizeof(hram));
  bool will_head=TossCoin(0);memcpy(gWRAM,ram,sizeof(ram));memcpy(gHRAM,hram,sizeof(hram));coins=heads=0;
  if(will_head){ANSWERS(TCG_CANCEL,1,0);}assert(!UsePokemonPower());assert(coins==1&&heads==will_head);
  assert(v(side)[DUELVARS_ARENA_CARD_HP]==100+10*will_head);assert(v(side)[DUELVARS_ARENA_CARD_FLAGS+1]&USED_PKMN_POWER_THIS_TURN);
  heal_heads+=will_head;done();
  setup("Quickfreeze on play",seed&1?PLAYER_TURN:OPPONENT_TURN);place(side,1,ARTICUNO_LV37,100);wRNG1=(u8)seed;
  hTempCardIndex_ff98=1;hTempPlayAreaLocation_ff9d=1;ProcessPlayedPokemonCard();assert(coins==1);
  assert(v(other())[DUELVARS_ARENA_CARD_STATUS]==(heads?PARALYZED:NO_STATUS));freeze_heads+=heads;done();
 }
 assert(heal_heads&&heal_heads<32&&freeze_heads&&freeze_heads<32);
}

static void edge_outcomes(void)
{
 for(unsigned p=0;p<2;p++){
  setup("Curse Bench KO and prize",p?OPPONENT_TURN:PLAYER_TURN);place(side,1,GENGAR,80);
  v(other())[DUELVARS_ARENA_CARD_HP]=100;v(other())[DUELVARS_ARENA_CARD_HP+2]=10;
  v(side)[DUELVARS_PRIZES]=3;v(side)[DUELVARS_PRIZE_CARDS]=40;v(side)[DUELVARS_PRIZE_CARDS+1]=41;
  v(side)[40]=v(side)[41]=CARD_LOCATION_PRIZE;load_power(1);ANSWERS(0,2,0);assert(!UsePokemonPower());
  assert(v(other())[2]==CARD_LOCATION_DISCARD_PILE&&v(other())[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]==2);
  assert(v(other())[DUELVARS_ARENA_CARD_HP]==110&&v(side)[40]==CARD_LOCATION_HAND&&v(side)[DUELVARS_PRIZES]==2);
  assert(v(side)[DUELVARS_ARENA_CARD_FLAGS+1]&USED_PKMN_POWER_THIS_TURN);assert(!wDuelFinished);unchanged_rng();done();
  setup("Peek prize and opponent turn restoration",p?OPPONENT_TURN:PLAYER_TURN);place(side,1,MANKEY,30);
  v(other())[DUELVARS_PRIZES]=2;v(other())[DUELVARS_PRIZE_CARDS+1]=40;v(other())[40]=CARD_LOCATION_PRIZE;
  load_power(1);ANSWERS(1,0,1);assert(!UsePokemonPower());
  assert(hAIPkmnPowerEffectParam==(0x80|0x41)&&v(other())[40]==CARD_LOCATION_PRIZE&&v(other())[DUELVARS_PRIZES]==2);
  assert(cards_seen==1);unchanged_rng();done();
  setup("Peek opponent hand",p?OPPONENT_TURN:PLAYER_TURN);place(side,1,MANKEY,30);load_power(1);
  v(other())[DUELVARS_NUMBER_OF_CARDS_IN_HAND]=1;v(other())[DUELVARS_HAND]=40;v(other())[40]=CARD_LOCATION_HAND;
  ANSWERS(1,6);assert(!UsePokemonPower());assert(hAIPkmnPowerEffectParam==(0x80|40));assert(v(other())[40]==CARD_LOCATION_HAND&&cards_seen==1);done();
  setup("Firegiver no Fire",p?OPPONENT_TURN:PLAYER_TURN);place(side,1,MOLTRES_LV37,100);hTempCardIndex_ff98=1;hTempPlayAreaLocation_ff9d=1;
  ProcessPlayedPokemonCard();assert(!v(side)[DUELVARS_NUMBER_OF_CARDS_IN_HAND]);done();
  setup("Firegiver single remaining Fire",p?OPPONENT_TURN:PLAYER_TURN);place(side,1,MOLTRES_LV37,100);deck(side)[20]=FIRE_ENERGY;
  hTempCardIndex_ff98=1;hTempPlayAreaLocation_ff9d=1;ProcessPlayedPokemonCard();
  assert(v(side)[DUELVARS_NUMBER_OF_CARDS_IN_HAND]==1&&v(side)[20]==CARD_LOCATION_HAND);done();
 }
 for(unsigned seed=1;seed<=16;seed++){
  setup("Quickfreeze versus Thick Skinned",PLAYER_TURN);place(side,1,ARTICUNO_LV37,100);place(other(),0,SNORLAX,90);wRNG1=(u8)seed;
  hTempCardIndex_ff98=1;hTempPlayAreaLocation_ff9d=1;ProcessPlayedPokemonCard();assert(coins==1&&!v(other())[DUELVARS_ARENA_CARD_STATUS]);done();
  setup("Peal bypasses Invisible Wall",PLAYER_TURN);for(unsigned slot=0;slot<3;slot++){place(side,slot,MR_MIME,40);place(other(),slot,MR_MIME,40);}
  place(side,1,ZAPDOS_LV68,100);wRNG1=(u8)seed;wRNG2=(u8)(seed*37);wRNGCounter=(u8)(seed*19);
  hTempCardIndex_ff98=1;hTempPlayAreaLocation_ff9d=1;ProcessPlayedPokemonCard();unsigned total=0;
  for(unsigned slot=0;slot<3;slot++){total+=(slot==1?100:40)-v(side)[DUELVARS_ARENA_CARD_HP+slot];total+=40-v(other())[DUELVARS_ARENA_CARD_HP+slot];}
  assert(total==30);done();
 }
 setup("Strikes Back can KO",PLAYER_TURN);place(side,0,MACHAMP,100);v(other())[DUELVARS_ARENA_CARD_HP]=10;
 wTempNonTurnDuelistCardID=MACHAMP;HandleStrikesBack_AgainstDamagingAttack(30);assert(!v(other())[DUELVARS_ARENA_CARD_HP]);unchanged_rng();done();
}

static void passive_powers(void)
{
 for(unsigned p=0;p<2;p++){
  u8 who=p?OPPONENT_TURN:PLAYER_TURN;
  setup("Clairvoyance/Rain Dance/Prehistoric",who);assert(!IsClairvoyanceActive()&&!IsRainDanceActive()&&!IsPrehistoricPowerActive());
  place(side,1,OMANYTE,40);place(side,2,BLASTOISE,100);place(other(),1,AERODACTYL,60);
  assert(IsClairvoyanceActive()&&IsRainDanceActive()&&IsPrehistoricPowerActive());
  place(other(),2,MUK,70);assert(!IsClairvoyanceActive()&&!IsRainDanceActive()&&!IsPrehistoricPowerActive());unchanged_rng();done();
  setup("Rain Dance water target",who);place(side,1,BLASTOISE,100);attach(20,WATER_ENERGY,1);hTempCardIndex_ff98=20;hTempPlayAreaLocation_ff9d=1;
  assert(CheckRainDanceScenario());hTempPlayAreaLocation_ff9d=0;assert(!CheckRainDanceScenario());attach(20,FIRE_ENERGY,1);hTempPlayAreaLocation_ff9d=1;assert(!CheckRainDanceScenario());unchanged_rng();done();
  setup("Retreat Aid",who);place(side,0,DODRIO,70);LoadCardDataToBuffer1_FromCardID(CHANSEY);assert(GetLoadedCard1RetreatCost()==1);
  place(side,1,DODRIO,70);assert(GetLoadedCard1RetreatCost()==0);place(side,2,DODRIO,70);assert(GetLoadedCard1RetreatCost()==0);
  place(other(),1,MUK,70);assert(GetLoadedCard1RetreatCost()==1);unchanged_rng();done();
  setup("Energy Burn",who);place(side,0,CHARIZARD,120);wAttachedEnergies[GRASS]=2;wAttachedEnergies[WATER]=1;wAttachedEnergies[COLORLESS]=2;wTotalAttachedEnergies=5;
  HandleEnergyBurn();assert(wAttachedEnergies[FIRE]==5&&wAttachedEnergies[COLORLESS]==2&&!wAttachedEnergies[GRASS]&&!wAttachedEnergies[WATER]);unchanged_rng();done();
 }
 for(unsigned poison=0;poison<2;poison++)for(unsigned muk=0;muk<2;muk++){
  setup("Thick Skinned",PLAYER_TURN);place(other(),0,SNORLAX,90);v(other())[DUELVARS_ARENA_CARD_STATUS]=poison?ASLEEP:NO_STATUS;
  if(muk)place(side,1,MUK,70);wTempNonTurnDuelistCardID=SNORLAX;assert(ParalysisEffect()==!!(poison||muk));
  assert(wStatusConditionQueueIndex==(poison||muk?3:0));unchanged_rng();done();
 }
 static const u8 defenders[]={MR_MIME,KABUTO};
 for(unsigned i=0;i<2;i++)for(unsigned damage=0;damage<=100;damage+=10)for(unsigned disabled=0;disabled<3;disabled++){
  setup("Invisible Wall/Kabuto Armor",PLAYER_TURN);place(side,0,defenders[i],30);wTempNonTurnDuelistCardID=defenders[i];wLoadedAttack.Category=0;
  if(disabled==1)v(side)[DUELVARS_ARENA_CARD_STATUS]=PARALYZED;
  if(disabled==2)place(other(),1,MUK,70);
  unsigned want=disabled?damage:(i?damage/20*10:(damage>=30?0:damage));assert(HandleDamageReductionExceptSubstatus2((u16)damage)==want);
  wLoadedAttack.Category=POKEMON_POWER;assert(HandleDamageReductionExceptSubstatus2((u16)damage)==damage);unchanged_rng();done();
 }
 for(unsigned stage=0;stage<2;stage++)for(unsigned self=0;self<2;self++)for(unsigned disabled=0;disabled<3;disabled++){
  setup("Neutralizing Shield",PLAYER_TURN);place(side,0,MEW_LV8,40);wTempNonTurnDuelistCardID=MEW_LV8;wTempTurnDuelistCardID=stage?WARTORTLE:SQUIRTLE;
  wIsDamageToSelf=(u8)self;wLoadedAttack.Category=0;if(disabled==1)v(side)[DUELVARS_ARENA_CARD_STATUS]=ASLEEP;if(disabled==2)place(other(),1,MUK,70);
  bool blocked=HandleNoDamageOrEffectSubstatus();assert(blocked==!!(stage&&!self&&!disabled));
  unchanged_rng();done();
 }
 for(unsigned disabled=0;disabled<4;disabled++){
  setup("Strikes Back",PLAYER_TURN);place(side,0,MACHAMP,100);wTempNonTurnDuelistCardID=MACHAMP;wTempPlayAreaLocation_cceb=0;
  if(disabled==1)v(side)[DUELVARS_ARENA_CARD_STATUS]=ASLEEP;if(disabled==2)place(side,1,MUK,70);if(disabled==3)wLoadedAttack.Category=POKEMON_POWER;
  HandleStrikesBack_AgainstDamagingAttack(10);assert(v(other())[DUELVARS_ARENA_CARD_HP]==(disabled?120:110));unchanged_rng();done();
 }
 unsigned prevented=0;
 for(unsigned seed=1;seed<=32;seed++){
  setup("Transparency",PLAYER_TURN);place(side,0,HAUNTER_LV17,50);wTempNonTurnDuelistCardID=HAUNTER_LV17;wRNG1=(u8)seed;
  bool blocked=HandleTransparency();assert(coins==1&&blocked==!!heads);prevented+=blocked;
  wNoDamageOrEffect=0;wLoadedAttack.Category=POKEMON_POWER;assert(!HandleTransparency()&&coins==1);done();
 }assert(prevented&&prevented<32);
}

static void trigger_powers(void)
{
 static const u8 trigger[]={MOLTRES_LV37,ARTICUNO_LV37,ZAPDOS_LV68,DRAGONITE_LV41};
 for(unsigned i=0;i<4;i++)for(unsigned p=0;p<2;p++){
  setup("on-play Toxic Gas suppression",p?OPPONENT_TURN:PLAYER_TURN);place(side,1,trigger[i],100);place(other(),2,MUK,70);load_power(1);
  assert(TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_1));hTempCardIndex_ff98=1;ProcessPlayedPokemonCard();
  assert(!coins&&v(side)[DUELVARS_NUMBER_OF_CARDS_IN_HAND]==0&&v(other())[DUELVARS_ARENA_CARD_HP]==120);unchanged_rng();done();
 }
 for(unsigned p=0;p<2;p++){
  setup("Healing Wind on play",p?OPPONENT_TURN:PLAYER_TURN);place(side,1,DRAGONITE_LV41,100);v(side)[DUELVARS_ARENA_CARD_HP]=90;v(side)[DUELVARS_ARENA_CARD_HP+2]=110;
  hTempCardIndex_ff98=1;hTempPlayAreaLocation_ff9d=1;ProcessPlayedPokemonCard();
  assert(v(side)[DUELVARS_ARENA_CARD_HP]==110&&v(side)[DUELVARS_ARENA_CARD_HP+1]==100&&v(side)[DUELVARS_ARENA_CARD_HP+2]==120);unchanged_rng();done();
 }
 unsigned drew=0,own=0,enemy=0;
 for(unsigned seed=1;seed<=32;seed++){
  setup("Firegiver on play",PLAYER_TURN);place(side,1,MOLTRES_LV37,100);deck(side)[20]=FIRE_ENERGY;deck(side)[21]=FIRE_ENERGY;deck(side)[22]=FIRE_ENERGY;wRNG1=(u8)seed;wRNG2=(u8)(seed*37);wRNGCounter=(u8)(seed*19);
  hTempCardIndex_ff98=1;hTempPlayAreaLocation_ff9d=1;ProcessPlayedPokemonCard();unsigned n=v(side)[DUELVARS_NUMBER_OF_CARDS_IN_HAND];assert(n>=1&&n<=3);
  for(unsigned i=0;i<n;i++)assert(v(side)[20+i]==CARD_LOCATION_HAND);assert(v(side)[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK]==3+n);drew|=1u<<n;done();
  setup("Peal of Thunder on play",seed&1?PLAYER_TURN:OPPONENT_TURN);place(side,1,ZAPDOS_LV68,100);wRNG1=(u8)seed;wRNG2=(u8)(seed*37);wRNGCounter=(u8)(seed*19);
  hTempCardIndex_ff98=1;hTempPlayAreaLocation_ff9d=1;ProcessPlayedPokemonCard();unsigned lost=0;
  assert(v(side)[DUELVARS_ARENA_CARD_HP+1]==100);
  unsigned own_lost=0;
  for(unsigned s=0;s<3;s++){unsigned a=s==1?100:120;own_lost+=a-v(side)[DUELVARS_ARENA_CARD_HP+s];lost+=120-v(other())[DUELVARS_ARENA_CARD_HP+s];}
  assert(own_lost+lost==30);if(own_lost)own++;else enemy++;done();
 }
 assert((drew&(1u<<1))&&(drew&(1u<<3))&&own&&enemy);
}

int main(void)
{
 gates();command_gates();manual_powers(PLAYER_TURN);manual_powers(OPPONENT_TURN);random_powers();edge_outcomes();passive_powers();trigger_powers();hand_play();power_menu_and_failures();
 printf("POWER AUDIT PASS: %u real-engine outcome cases; all 26 Power mechanisms, both sides, source RNG, status/Muk, once-per-turn, cancellation, damage/energy conservation and on-play suppression\n",cases);return 0;
}
