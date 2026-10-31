/* Exercise real engine toss events through the production frontend, keeping
 * deterministic RNG snapshots independent of animation settings. */
#ifndef TCGN64_HOST_TEST
#define TCGN64_HOST_TEST
#endif
#define main tcgn64_frontend_main
#include "../tcgn64.c"
#undef main
#include "duel_ui.h"

static uint8_t observed[32];
static unsigned observed_count;
static void observe(void *context,const TcgEvent *event)
{
	if(event->type==TCG_EV_COIN) {
		assert(observed_count<sizeof(observed));
		assert(event->arg[1]==observed_count+1);
		observed[observed_count++]=event->arg[0];
	}
	on_event(context,event);
}
static int acknowledge(void *context,const TcgRequest *request)
{(void)context;assert(request->type==TCG_REQ_ACK);return 0;}

static void setup_coin(uint8_t owner,bool ai,bool disabled)
{
	memset(gWRAM,0,sizeof(gWRAM));memset(gHRAM,0,sizeof(gHRAM));
	reset_duel_ui();observed_count=0;
	hWhoseTurn=owner;wRNG1=0x5a;wRNG2=0xc3;wRNGCounter=7;
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
	puts("COIN ANIMATION PASS: event order, results, RNG, disabled option, controller ownership");return 0;
}
