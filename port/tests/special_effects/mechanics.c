/* Focused real attack-pipeline regressions for bespoke mechanics. These are
 * assertion fixtures, complementing (not replacing) original-ROM lockstep. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
#include "effects/effects.h"
#include "gen/cards.h"

enum { NORMAL, METRONOME_CANCEL, WILDFIRE };
static unsigned policy, attack_choice, energy_answers, requests, cases;
static u16 last_text;
void tcg_fatal(const char *s){fprintf(stderr,"Attack fixture fatal: %s\n",s);abort();}
static void event(void *ctx,const TcgEvent *e)
{(void)ctx;if(e->type==TCG_EV_TEXT)last_text=e->text;}
static int request(void *ctx,const TcgRequest *r)
{
	(void)ctx;assert(++requests<100);
	if(r->type==TCG_REQ_ACK)return 0;
	if(r->type==TCG_REQ_MENU&&r->menu==TCG_MENU_ATTACKS) {
		if(policy==METRONOME_CANCEL)return TCG_CANCEL;
		assert(attack_choice<r->num_items);return (int)attack_choice;
	}
	if(r->type==TCG_REQ_ENERGY_DISCARD) {
		energy_answers++;
		return policy==WILDFIRE&&energy_answers==3?TCG_CANCEL:0;
	}
	fprintf(stderr,"Unexpected request %u menu%u\n",r->type,r->menu);abort();
}
static const TcgHost host={event,request,NULL,NULL};
static u8 *vars(unsigned side){return side?wOpponentDuelVariables:wPlayerDuelVariables;}
static u8 *deck(unsigned side){return side?wOpponentDeck:wPlayerDeck;}
static void rebuild(unsigned side)
{
	u8 *v=vars(side);unsigned at=0,hand=0;
	for(unsigned i=0;i<60;i++)if(v[i]==CARD_LOCATION_DISCARD_PILE)v[DUELVARS_DECK_CARDS+at++]=(u8)i;
	v[DUELVARS_NUMBER_OF_CARDS_IN_DISCARD_PILE]=(u8)at;
	for(unsigned i=0;i<60;i++)if(v[i]!=CARD_LOCATION_DISCARD_PILE&&v[i]!=CARD_LOCATION_DECK)
		v[DUELVARS_DECK_CARDS+at++]=(u8)i;
	v[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK]=(u8)at;
	for(unsigned i=0;i<60;i++)if(v[i]==CARD_LOCATION_DECK)v[DUELVARS_DECK_CARDS+at++]=(u8)i;
	assert(at==60);
	for(unsigned i=0;i<60;i++)if(v[i]==CARD_LOCATION_HAND)v[DUELVARS_HAND+hand++]=(u8)i;
	v[DUELVARS_NUMBER_OF_CARDS_IN_HAND]=(u8)hand;
}
static void place(unsigned side,unsigned loc,unsigned index,u8 id)
{
	u8 *v=vars(side);deck(side)[index]=id;v[index]=(u8)(CARD_LOCATION_ARENA+loc);
	v[DUELVARS_ARENA_CARD+loc]=(u8)index;v[DUELVARS_ARENA_CARD_HP+loc]=gCardData[id][CARD_DATA_HP];
	v[DUELVARS_ARENA_CARD_STAGE+loc]=gCardData[id][CARD_DATA_STAGE];
	v[DUELVARS_ARENA_CARD_FLAGS+loc]=CAN_EVOLVE_THIS_TURN;
}
static void evolution_stack(unsigned side)
{
	u8 id=deck(side)[0];unsigned index=10;
	while(gCardData[id][CARD_DATA_STAGE]) {
		const u8 *p=gCardData[id]+CARD_DATA_PREEVO_NAME;
		u8 found=0;
		for(unsigned n=1;n<=NUM_CARDS;n++)if(gCardData[n][CARD_DATA_TYPE]<TYPE_ENERGY&&
			gCardData[n][CARD_DATA_STAGE]+1==gCardData[id][CARD_DATA_STAGE]&&
			gCardData[n][CARD_DATA_NAME]==p[0]&&gCardData[n][CARD_DATA_NAME+1]==p[1]){found=(u8)n;break;}
		assert(found&&index<=11);deck(side)[index]=found;vars(side)[index]=CARD_LOCATION_ARENA;index++;id=found;
	}
}
static void setup(unsigned side,u8 attacker,u8 defender)
{
	memset(gWRAM,0,sizeof(gWRAM));memset(gHRAM,0,sizeof(gHRAM));gUIDecider=0;gTextID=0;
	policy=NORMAL;attack_choice=1;energy_answers=requests=0;last_text=0;
	for(unsigned d=0;d<2;d++) {
		memset(deck(d),GRASS_ENERGY,60);u8 *v=vars(d);
		memset(v+DUELVARS_ARENA_CARD,0xff,7);v[DUELVARS_DUELIST_TYPE]=DUELIST_TYPE_PLAYER;
		v[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]=3;
		place(d,0,0,d==side?attacker:defender);place(d,1,1,BULBASAUR);place(d,2,2,PIKACHU_LV12);
		evolution_stack(d);
		for(unsigned i=3;i<10;i++)v[i]=CARD_LOCATION_ARENA;
		for(unsigned i=26;i<30;i++){deck(d)[i]=POTION;v[i]=CARD_LOCATION_HAND;}
		rebuild(d);
	}
	hWhoseTurn=wWhoseTurn=side?OPPONENT_TURN:PLAYER_TURN;wDuelTurns=4;
	wRNG1=0x57;wRNG2=0x31;wRNGCounter=0xa9;tcg_set_host(&host);
}
static bool attack(unsigned slot)
{
	CopyAttackDataAndDamage_FromDeckIndex(0,(u8)slot);
	return UseAttackOrPokemonPower();
}
static unsigned count_location(unsigned side,unsigned location)
{unsigned n=0;for(unsigned i=0;i<60;i++)n+=vars(side)[i]==location;return n;}

static void metronome(unsigned side)
{
	setup(side,CLEFABLE,CHARMELEON);assert(attack(0));
	assert(last_text==NotEnoughFireEnergyText&&!energy_answers);cases++;
	setup(side,CLEFABLE,CHARIZARD);attack_choice=0;assert(!attack(0));
	assert(energy_answers==2&&count_location(side,CARD_LOCATION_DISCARD_PILE)==2);
	assert(vars(!side)[DUELVARS_ARENA_CARD_HP]==20); /* copied Fire Spin:100 */
	assert(wMetronomeEnergyCost==1&&hWhoseTurn==(side?OPPONENT_TURN:PLAYER_TURN));cases++;
	setup(side,CLEFABLE,CLEFABLE);attack_choice=0;assert(attack(0));
	assert(last_text==UnableToSelectText&&!energy_answers);cases++;
	setup(side,CLEFABLE,CHARIZARD);policy=METRONOME_CANCEL;assert(attack(0));
	assert(!energy_answers&&!count_location(side,CARD_LOCATION_DISCARD_PILE));cases++;
}
static void mirror(unsigned side)
{
	setup(side,SPEAROW,LAPRAS);assert(attack(1));
	assert(last_text==YouDidNotReceiveAnAttackToMirrorMoveText);cases++;
	setup(side,SPEAROW,LAPRAS);u8 *v=vars(side),*other=vars(!side);
	v[DUELVARS_ARENA_CARD_LAST_TURN_DAMAGE]=20;
	v[DUELVARS_ARENA_CARD_LAST_TURN_STATUS]=POISONED;
	v[DUELVARS_ARENA_CARD_LAST_TURN_SUBSTATUS2]=SUBSTATUS2_GROWL;
	assert(!attack(1));assert(other[DUELVARS_ARENA_CARD_HP]==60);
	assert((other[DUELVARS_ARENA_CARD_STATUS]&PSN_DBLPSN)==POISONED);
	assert(other[DUELVARS_ARENA_CARD_SUBSTATUS2]==SUBSTATUS2_GROWL);cases++;
}
static void amnesia_cancel(unsigned side)
{
	static const u8 ids[]={POLIWHIRL,SLOWPOKE_LV9};
	for(unsigned id=0;id<2;id++) {
		setup(side,ids[id],LAPRAS);policy=METRONOME_CANCEL;
		for(unsigned i=3;i<10;i++)deck(side)[i]=id?PSYCHIC_ENERGY:WATER_ENERGY;
		u8 before[0x200];memcpy(before,wPlayerDuelVariables,sizeof(before));
		u8 rng_before[]={wRNG1,wRNG2,wRNGCounter};
		assert(attack(id?1:0));
		assert(!memcmp(before,wPlayerDuelVariables,DUELVARS_ARENA_CARD_LAST_TURN_DAMAGE));
		assert(!memcmp(before+0x100,wOpponentDuelVariables,DUELVARS_ARENA_CARD_LAST_TURN_DAMAGE));
		assert(wRNG1==rng_before[0]&&wRNG2==rng_before[1]&&wRNGCounter==rng_before[2]);
		assert(!vars(!side)[DUELVARS_ARENA_CARD_SUBSTATUS2]);
		policy=NORMAL;attack_choice=1;assert(!attack(id?1:0));
		assert(vars(!side)[DUELVARS_ARENA_CARD_SUBSTATUS2]==SUBSTATUS2_AMNESIA);
		assert(vars(!side)[DUELVARS_ARENA_CARD_DISABLED_ATTACK_INDEX]==1);
		cases++;
	}
}
static void morph(unsigned side)
{
	setup(side,DITTO,LAPRAS);deck(side)[30]=BULBASAUR;
	u8 *v=vars(side);v[DUELVARS_ARENA_CARD_HP]=20;
	v[DUELVARS_ARENA_CARD_CHANGED_TYPE]=HAS_CHANGED_COLOR|FIRE;v[DUELVARS_ARENA_CARD_STATUS]=POISONED;
	assert(!attack(1));assert(deck(side)[0]==BULBASAUR&&v[DUELVARS_ARENA_CARD_HP]==40);
	assert(!v[DUELVARS_ARENA_CARD_CHANGED_TYPE]&&!v[DUELVARS_ARENA_CARD_STATUS]);
	assert(v[30]==CARD_LOCATION_DECK); /* Morph copies the ID; it does not draw. */
	cases++;
}
static void mixup(unsigned side)
{
	setup(side,NINETALES_LV35,LAPRAS);u8 *v=vars(!side),*d=deck(!side);
	d[26]=PIKACHU_LV12;d[27]=GOLDEEN;d[28]=FIRE_ENERGY;d[29]=POTION;
	d[30]=BULBASAUR;d[31]=CHARMANDER;
	assert(!attack(0));assert(v[DUELVARS_NUMBER_OF_CARDS_IN_HAND]==4);
	unsigned pokemon=0;
	for(unsigned i=0;i<4;i++)pokemon+=gCardData[d[v[DUELVARS_HAND+i]]][CARD_DATA_TYPE]<TYPE_ENERGY;
	assert(pokemon==2&&v[28]==CARD_LOCATION_HAND&&v[29]==CARD_LOCATION_HAND);
	assert(v[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK]==count_location(!side,CARD_LOCATION_HAND)+
		count_location(!side,CARD_LOCATION_ARENA)+count_location(!side,CARD_LOCATION_ARENA+1)+count_location(!side,CARD_LOCATION_ARENA+2));
	cases++;
}
static void wildfire(unsigned side)
{
	setup(side,MOLTRES_LV35,LAPRAS);policy=WILDFIRE;
	for(unsigned i=3;i<10;i++){deck(side)[i]=FIRE_ENERGY;vars(side)[i]=i<6?CARD_LOCATION_ARENA:CARD_LOCATION_DECK;}
	for(unsigned i=0;i<60;i++)if(vars(!side)[i]==CARD_LOCATION_DECK&&i!=59)vars(!side)[i]=CARD_LOCATION_DISCARD_PILE;
	rebuild(side);rebuild(!side);unsigned before=vars(!side)[DUELVARS_NUMBER_OF_CARDS_IN_DISCARD_PILE];
	assert(!attack(0));assert(energy_answers==3);
	assert(count_location(side,CARD_LOCATION_DISCARD_PILE)==2&&vars(side)[5]==CARD_LOCATION_ARENA);
	assert(vars(!side)[DUELVARS_NUMBER_OF_CARDS_IN_DISCARD_PILE]==before+1);
	assert(vars(!side)[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK]==60);cases++;
}
static void devolution(unsigned side)
{
	for(unsigned own=0;own<2;own++)for(unsigned ko=0;ko<2;ko++) {
		setup(side,MEW_LV23,LAPRAS);unsigned target=own?side:!side;u8 *v=vars(target);
		place(target,1,1,RAICHU_LV40);deck(target)[12]=PIKACHU_LV12;v[12]=CARD_LOCATION_ARENA+1;
		v[DUELVARS_ARENA_CARD_HP+1]=ko?20:60;
		v[DUELVARS_ARENA_CARD_CHANGED_TYPE+1]=HAS_CHANGED_COLOR|FIRE;rebuild(target);
		hTemp_ffa0=own?0:1;hTempPlayAreaLocation_ffa1=1;
		assert(!DevolutionBeam_DevolveEffect());
		assert(v[DUELVARS_ARENA_CARD+1]==12&&v[DUELVARS_ARENA_CARD_HP+1]==(ko?0:20));
		assert(v[1]==CARD_LOCATION_HAND&&v[DUELVARS_NUMBER_OF_CARDS_IN_HAND]==5);
		assert(!v[DUELVARS_ARENA_CARD_STAGE+1]&&!v[DUELVARS_ARENA_CARD_FLAGS+1]&&!v[DUELVARS_ARENA_CARD_CHANGED_TYPE+1]);
		assert(hWhoseTurn==(side?OPPONENT_TURN:PLAYER_TURN));cases++;
	}
}
int main(void)
{
	for(unsigned side=0;side<2;side++) {
		metronome(side);mirror(side);amnesia_cancel(side);morph(side);mixup(side);wildfire(side);devolution(side);
	}
	printf("SPECIAL MECHANICS PASS cases=%u both_turn_sides=1\n",cases);return 0;
}
