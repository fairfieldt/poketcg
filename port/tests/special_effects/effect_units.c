/* Original-ROM function differential checks for non-interactive attack
 * mechanics. Reuse only the existing GB bootstrap/symbol/oracle utilities. */
#define main inherited_unitdiff_main
#include "../../harness/unitdiff.c"
#undef main
#include "../../src/effects/effects.h"
#include "../../src/gen/cards.h"

typedef struct {const char *name; bool (*run)(void); bool carry;} EffectCase;
static const EffectCase effect_cases[]={
#include "effect_unit_table.inc"
};

static void legal_state(unsigned variant)
{
	memset(gWRAM,0,sizeof(gWRAM));memset(gHRAM,0,sizeof(gHRAM));
	static const u8 pokemon[]={VENUSAUR_LV67,BLASTOISE,CHARIZARD,PIKACHU_LV12,
		KADABRA,RATICATE,KABUTO,MR_MIME,MEW_LV8,DITTO,CLEFABLE,MUK};
	static const u8 energy[]={GRASS_ENERGY,FIRE_ENERGY,WATER_ENERGY,LIGHTNING_ENERGY,
		FIGHTING_ENERGY,PSYCHIC_ENERGY,DOUBLE_COLORLESS_ENERGY};
	for(unsigned side=0;side<2;side++) {
		u8 *v=side?wOpponentDuelVariables:wPlayerDuelVariables;
		u8 *deck=side?wOpponentDeck:wPlayerDeck;
		for(unsigned i=0;i<DECK_SIZE;i++) {
			deck[i]=i<6?pokemon[(variant+i+side*3)%sizeof(pokemon)]:
				i<24?energy[(i+variant+side)%sizeof(energy)]:i<50?POTION:GRASS_ENERGY;
			v[DUELVARS_DECK_CARDS+i]=(u8)i;
			v[DUELVARS_CARD_LOCATIONS+i]=i<6?(u8)(CARD_LOCATION_ARENA+i):
				i<24?(u8)(CARD_LOCATION_ARENA+(variant%2?0:(i%6))):
				i<40?CARD_LOCATION_HAND:i<50?CARD_LOCATION_DISCARD_PILE:CARD_LOCATION_DECK;
		}
		v[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]=6;
		v[DUELVARS_NUMBER_OF_CARDS_IN_HAND]=16;
		v[DUELVARS_NUMBER_OF_CARDS_IN_DISCARD_PILE]=10;
		v[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK]=50;
		v[DUELVARS_DUELIST_TYPE]=DUELIST_TYPE_PLAYER;
		v[DUELVARS_ARENA_CARD+6]=0xff;
		for(unsigned i=0;i<6;i++) {
			v[DUELVARS_ARENA_CARD+i]=(u8)i;
			unsigned hp=gCardData[deck[i]][CARD_DATA_HP];
			v[DUELVARS_ARENA_CARD_HP+i]=(u8)(hp>20?hp-10*(variant%3):10);
			v[DUELVARS_ARENA_CARD_STAGE+i]=gCardData[deck[i]][CARD_DATA_STAGE];
			v[DUELVARS_ARENA_CARD_FLAGS+i]=CAN_EVOLVE_THIS_TURN;
		}
		for(unsigned i=0;i<16;i++)v[DUELVARS_HAND+i]=(u8)(24+i);
		for(unsigned i=0;i<10;i++)v[DUELVARS_DECK_CARDS+i]=(u8)i;
		static const u8 status[]={0,ASLEEP,PARALYZED,CONFUSED,POISONED,DOUBLE_POISONED};
		v[DUELVARS_ARENA_CARD_STATUS]=status[(variant+side)%sizeof(status)];
	}
	hWhoseTurn=variant%2?OPPONENT_TURN:PLAYER_TURN;
	wWhoseTurn=hWhoseTurn;wRNG1=(u8)(37*variant+1);wRNG2=(u8)(101*variant+13);wRNGCounter=(u8)variant;
	wDamage[0]=(u8)(10*(variant%11));wDamage[1]=0;
	wLoadedAttackDamage=wDamage[0];wLoadedAttackCategory=DAMAGE_NORMAL;
	wLoadedAttackEffectParam=(u8)(variant%4);
	wTempTurnDuelistCardID=(hWhoseTurn==PLAYER_TURN?wPlayerDeck:wOpponentDeck)[0];
	wTempNonTurnDuelistCardID=(hWhoseTurn==PLAYER_TURN?wOpponentDeck:wPlayerDeck)[0];
	memset(hTempList,0xff,8);
	hTempPlayAreaLocation_ff9d=(u8)(variant%6);hTemp_ffa0=(u8)(variant%3);
	hTempPlayAreaLocation_ffa1=(u8)((variant+1)%6);
	wDuelTempList[0]=6;wDuelTempList[1]=7;wDuelTempList[2]=0xff;
	wTempPlayAreaLocation_cceb=(u8)(variant%6);
	memcpy(gb->wram,gWRAM,WRAM_SIZE);memcpy(gb->hram,gHRAM,HRAM_SIZE);
	gb->a=gb->b=gb->c=gb->d=gb->e=gb->h=gb->l=gb->f=0;
}

int main(int argc,char **argv)
{
	if(argc<3){fprintf(stderr,"usage: %s ROM SYM [variants] [function]\n",argv[0]);return 2;}
	FILE *f=fopen(argv[1],"rb");if(!f){perror(argv[1]);return 2;}
	static uint8_t rom[0x200000];size_t size=fread(rom,1,sizeof(rom),f);fclose(f);
	load_sym(argv[2]);gb=gb_new(rom,size);gb->render=0;
	trap_addr=sym("PowersOf2")->addr;gb_add_breakpoint(gb,-1,trap_addr);
	for(int i=0;i<120;i++)gb_run(gb,1<<30,1);
	unsigned variants=argc>3?(unsigned)atoi(argv[3]):24,failures=0,cases=0;
	setvbuf(stdout,NULL,_IOLBF,0);
	for(unsigned i=0;i<sizeof(effect_cases)/sizeof(effect_cases[0]);i++) {
		const EffectCase *c=&effect_cases[i];if(argc>4&&strcmp(argv[4],c->name))continue;
		unsigned failed=0;
		for(unsigned variant=0;variant<variants;variant++) {
			legal_state(variant);call_rom(c->name);bool expected=(gb->f&0x10)!=0;
			bool got=c->run();int bad=compare_memory(c->name);
			if(c->carry&&got!=expected){fprintf(stderr,"%s#%u carry port%d rom%d\n",c->name,variant,got,expected);bad++;}
			if(bad){fprintf(stderr,"FAIL %s variant%u\n",c->name,variant);failed++;break;}
		}
		printf("EFFECT_UNIT %s %s variants=%u\n",c->name,failed?"FAIL":"PASS",variants);
		cases++;failures+=failed;
	}
	printf("EFFECT_UNITS %s cases=%u variants=%u failures=%u\n",failures?"FAIL":"PASS",cases,variants,failures);
	return failures!=0;
}
