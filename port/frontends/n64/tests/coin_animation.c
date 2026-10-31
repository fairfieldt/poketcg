/* Exercise real engine toss events through the production frontend, keeping
 * deterministic RNG snapshots independent of animation settings. */
#ifndef TCGN64_HOST_TEST
#define TCGN64_HOST_TEST
#endif
#define TCGN64_RETAINED_UI_TEST
#define main tcgn64_frontend_main
#include "../tcgn64.c"
#undef main
#include "duel_ui.h"
#include "engine.h"

static uint8_t observed[32];
static unsigned observed_count;
static bool frontend=true,coin_ack=true;
static uint8_t expected_owner;
static unsigned ready_acks,result_acks,ordinary_acks;
static uint32_t trace;
static void fold(unsigned value){trace=(trace^value)*16777619u;}
static void observe(void *context,const TcgEvent *event)
{
	fold(event->type);fold(event->text);for(unsigned i=0;i<4;i++)fold(event->arg[i]);
	if(event->type==TCG_EV_COIN) {
		assert(observed_count<sizeof(observed));
		assert(event->arg[1]==observed_count+1);
		observed[observed_count++]=event->arg[0];
	}
	if(frontend)on_event(context,event);
	if(frontend&&(event->type==TCG_EV_DAMAGE||(event->type==TCG_EV_ANIM&&!event->arg[1])))assert(g_screen!=TCG_SCREEN_COIN_TOSS);
}
static int acknowledge(void *context,const TcgRequest *request)
{
	(void)context;assert(request->type==TCG_REQ_ACK);assert(request->duelist==expected_owner);
	fold(request->type);fold(request->duelist);fold(request->shown);
	if(frontend){assert((g_screen==TCG_SCREEN_COIN_TOSS)==coin_ack);
		if(coin_ack){if(g_coin.ready)ready_acks++;else result_acks++;}else ordinary_acks++;}
	return 0;
}

static void setup_coin(uint8_t owner,bool ai,bool disabled)
{
	memset(gWRAM,0,sizeof(gWRAM));memset(gHRAM,0,sizeof(gHRAM));
	reset_duel_ui();observed_count=ready_acks=result_acks=ordinary_acks=0;
	expected_owner=owner;coin_ack=true;trace=2166136261u;
	hWhoseTurn=wWhoseTurn=owner;wRNG1=0x5a;wRNG2=0xc3;wRNGCounter=7;
	wAnimationsDisabled=disabled;
	tcg_duelvars(owner)[DUELVARS_DUELIST_TYPE]=ai?DUELIST_TYPE_AI_OPP:DUELIST_TYPE_PLAYER;
	memset(g_keys,0,sizeof(g_keys));
}

static void context_check(const char *name,uint8_t owner,unsigned count,bool ai)
{
	uint8_t expected[3],rng[3];unsigned first_heads=0;
	for(unsigned disabled=0;disabled<2;disabled++) {
		setup_coin(owner,ai,disabled);
		unsigned heads=count==1?TossCoin(0):TossCoinATimes((uint8_t)count,0);
		assert(observed_count==count&&g_coin.n==(int)count&&g_coin.number==(int)count);
		assert(g_screen==TCG_SCREEN_COIN_TOSS);
		assert(ready_acks==(ai?0:count)&&result_acks==(ai?1:count));
		unsigned actual_heads=0;
		for(unsigned i=0;i<count;i++){assert(g_coin.res[i]==observed[i]);actual_heads+=observed[i];}
		assert(heads==actual_heads);
		if(!disabled) {
			memcpy(expected,observed,count);rng[0]=wRNG1;rng[1]=wRNG2;rng[2]=wRNGCounter;first_heads=heads;
		} else {
			assert(!memcmp(expected,observed,count)&&heads==first_heads);
			assert(rng[0]==wRNG1&&rng[1]==wRNG2&&rng[2]==wRNGCounter);
		}
	}
	printf("COIN CONTEXT PASS %s coins=%u owner=%u animations preserve RNG/result\n",name,count,owner);
}

static void attack_after_coin(unsigned count,bool damage)
{
	if(count==1)TossCoin(0);else TossCoinATimes((uint8_t)count,0);
	if(frontend)assert(g_screen==TCG_SCREEN_COIN_TOSS);
	coin_ack=false;wLoadedAttack.Animation=ATK_ANIM_HIT;
	PlayAttackAnimation(damage?40:0,0,0,expected_owner==TCG_PLAYER?TCG_OPPONENT:TCG_PLAYER);
	WaitForWideTextBoxInput();
	if(frontend){assert(g_screen==TCG_SCREEN_MAIN&&ordinary_acks==1);assert(g_coin.n==(int)count);}
}
static void sleep_after_coin(void)
{
	bool heads=TossCoin(PokemonsSleepCheckText);coin_ack=false;
	/* The production HandleSleepCheck path explicitly redraws MAIN before
	 * its sleep/heal animation and message acknowledgement. */
	RedrawTurnDuelistsMainSceneOrDuelHUD();
	wTempNonTurnDuelistCardID=GOLDEEN;
	PrintCardNameFromCardIDInTextBox(heads?IsCuredOfSleepText:IsStillAsleepText);
	PlayBetweenTurnsAnimation(heads?DUEL_ANIM_HEAL:DUEL_ANIM_SLEEP);
	WaitForWideTextBoxInput();
	if(frontend)assert(g_screen==TCG_SCREEN_MAIN&&ordinary_acks==1);
}
static void lifecycle(uint8_t owner,unsigned count,bool damage,bool sleep)
{
	uint8_t wram[WRAM_SIZE],hram[HRAM_SIZE];uint32_t expected_trace;
	for(unsigned ui=0;ui<2;ui++){
		frontend=ui!=0;setup_coin(owner,false,false);
		if(sleep)sleep_after_coin();else attack_after_coin(count,damage);
		if(!ui){memcpy(wram,gWRAM,sizeof(wram));memcpy(hram,gHRAM,sizeof(hram));expected_trace=trace;}
		else{assert(trace==expected_trace);assert(!memcmp(wram,gWRAM,sizeof(wram))&&!memcmp(hram,gHRAM,sizeof(hram)));}
	}
}

int main(void)
{
	resolution_t resolution=getenv("MOCK_HIGH_RES")?(resolution_t){480,360,0}:RESOLUTION_320x240;
	display_init(resolution,DEPTH_16_BPP,3,GAMMA_NONE,FILTERS_RESAMPLE);joypad_init();
	for(int i=0;i<NUM_COLORS;i++)g_col[i]=graphics_make_color(kRGB[i][0],kRGB[i][1],kRGB[i][2],255);
	set_mode(MODE_HOTSEAT);
	TcgHost host={observe,acknowledge,NULL,NULL};tcg_set_host(&host);
	context_check("opening",TCG_PLAYER,1,false);
	context_check("attack-multiple",TCG_PLAYER,3,false);
	context_check("opponent-attack",TCG_OPPONENT,1,true);
	context_check("between-turn-status",TCG_OPPONENT,1,false);
	for(unsigned side=0;side<2;side++){
		uint8_t owner=side?TCG_OPPONENT:TCG_PLAYER;
		lifecycle(owner,1,true,false);lifecycle(owner,3,true,false);
		lifecycle(owner,3,false,false);lifecycle(owner,1,false,true);
	}
	/* A stale coin canvas must not survive a no-damage attack event; a new
	 * coin-spin event must be able to present a later source toss again. */
	setup_coin(TCG_PLAYER,false,true);g_screen=TCG_SCREEN_COIN_TOSS;
	UiCanvasReuse reuse={0};assert(ui_canvas_reuse_prepare(&reuse,1));ui_canvas_reuse_commit(&reuse);
	assert(!ui_canvas_reuse_prepare(&reuse,1));
	TcgEvent attack={.type=TCG_EV_ANIM,.arg={ATK_ANIM_HIT,0,TCG_OPPONENT,0}};on_event(NULL,&attack);
	assert(g_screen==TCG_SCREEN_MAIN&&ui_canvas_reuse_prepare(&reuse,1));
	TcgEvent spin={.type=TCG_EV_ANIM,.arg={DUEL_ANIM_COIN_SPIN,1,TCG_OPPONENT,0}};on_event(NULL,&spin);
	assert(g_screen==TCG_SCREEN_COIN_TOSS&&g_coin.ready&&g_coin.owner==TCG_OPPONENT);
	TcgEvent damage={.type=TCG_EV_DAMAGE,.arg={TCG_OPPONENT,0,40,0}};on_event(NULL,&damage);
	assert(g_screen==TCG_SCREEN_MAIN);
	/* History remains ordered even for a long flip-until-tails sequence. */
	memset(&g_coin,0,sizeof(g_coin));
	for(unsigned i=0;i<20;i++) {
		TcgEvent e={.type=TCG_EV_COIN,.arg={(uint8_t)(i&1),(uint8_t)(i+1),0,TCG_PLAYER}};coin_record(&e);
	}
	assert(g_coin.n==16&&g_coin.number==20);
	for(unsigned i=0;i<16;i++)assert(g_coin.res[i]==((i+4)&1));
	/* Only the tossing player's controller can skip the animation. */
	memset(g_keys,0,sizeof(g_keys));g_keys[0].a=true;
	assert(!coin_fast_forward(2));g_keys[1].a=true;assert(coin_fast_forward(2));
	setup_coin(TCG_PLAYER,false,true);
	unsigned frame=g_frame;TcgEvent e={.type=TCG_EV_COIN,.arg={HEADS,1,1,TCG_PLAYER}};on_event(NULL,&e);
	assert(g_frame==frame&&g_coin.res[0]==HEADS);
	free(g_felt.rows);g_felt=(FeltCache){0};
	puts("COIN ANIMATION PASS: event order, results, RNG, disabled option, ownership, flip ACK lifetime, attack/status dismissal, retained invalidation");return 0;
}
