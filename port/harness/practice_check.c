/* Drive the original eight-turn tutorial through the public host interface.
 * In particular, Waterfall requires the specified Psychic Energy on turn 2;
 * another Water Energy can pay its cost, but must still restart the lesson. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
#include "tcg.h"

static struct {
	bool started, wrong_energy, retry_seen, retry_restored;
	unsigned player_turns, requests;
	u8 selected_card;
	bool speech_waiting,finally_seen,recap_seen;
	unsigned speeches,speech_acks,checklist_updates;
	u16 speech_text;
	char checklist[1024];
} test;
static jmp_buf finished;

static unsigned long long state_hash(void)
{
	unsigned long long hash=1469598103934665603ULL;
	for(unsigned i=0;i<8192;i++)hash=(hash^tcg_wram()[i])*1099511628211ULL;
	for(unsigned i=0;i<128;i++)hash=(hash^tcg_hram()[i])*1099511628211ULL;
	return hash;
}

void tcg_fatal(const char *message)
{
	fprintf(stderr, "Practice fatal: %s\n", message);
	abort();
}

static unsigned attached(u8 id, u8 location)
{
	unsigned n = 0;
	for (u8 i = 0; i < DECK_SIZE; i++)
		if (wPlayerCardLocations[i] == (CARD_LOCATION_PLAY_AREA | location) &&
		    tcg_card_id(TCG_PLAYER, i) == id)
			n++;
	return n;
}

static u8 next_card(void)
{
	u8 active = tcg_card_id(TCG_PLAYER, wPlayerArenaCard);
	switch (wDuelTurns >> 1) {
	case 0: return wAlreadyPlayedEnergy ? 0 : WATER_ENERGY;
	case 1:
		if (active == GOLDEEN) return SEAKING;
		return wAlreadyPlayedEnergy ? 0 : test.wrong_energy ? WATER_ENERGY : PSYCHIC_ENERGY;
	case 2: return wAlreadyPlayedEnergy ? 0 : WATER_ENERGY;
	case 3:
		if (wPlayerNumberOfPokemonInPlayArea < 3) return DROWZEE;
		return wAlreadyPlayedEnergy ? 0 : WATER_ENERGY;
	case 4: return wAlreadyPlayedEnergy ? 0 : WATER_ENERGY;
	case 5:
		if (wPlayerArenaCardHP < 40) return POTION;
		return wAlreadyPlayedEnergy ? 0 : WATER_ENERGY;
	case 6: return active == STARYU ? STARMIE : 0;
	case 7: return 0;
	default: assert(!"unexpected practice turn"); return 0;
	}
}

static void event(void *ctx, const TcgEvent *ev)
{
	(void)ctx;
	if(ev->type==TCG_EV_TEXT) {
		if(ev->arg[1]==TCG_TEXT_PRACTICE_CHECKLIST) {
			assert(!ev->arg[0]&&!test.speech_waiting&&ev->str);
			assert(strlen(ev->str)<sizeof(test.checklist)-1);
			snprintf(test.checklist,sizeof(test.checklist),"%s",ev->str);
			if(ev->str[0])test.checklist_updates++;
		} else {
			/* Numbered lines are persistent panel redraws, never transient
			 * messages that replace Mason's speech for one rendered frame. */
			assert(ev->text<Turn1Instr1PracticeDuelText||ev->text>SamTurn4Instr2PracticeDuelText);
			if(ev->arg[1]==TCG_TEXT_PRACTICE_SPEECH) {
				assert(ev->arg[0]==1&&!test.speech_waiting);
				assert(rd16(wTextBoxLabel)==DrMasonText);
				test.speech_waiting=true;test.speech_text=ev->text;test.speeches++;
				if(ev->text==Turn1DrMason3PracticeDuelText) {
					assert(strstr(test.checklist,"1. Choose Hand")&&strstr(test.checklist,"2. Attach"));
					assert(!strstr(test.checklist,"3. Choose Attack"));
					test.finally_seen=true;
				}
				if(ev->text==LetsPlayTheGamePracticeDuelText&&wDuelTurns==0) {
					assert(strstr(test.checklist,"3. Choose Attack"));test.recap_seen=true;
				}
			}
		}
	}
	if (ev->type == TCG_EV_TURN) {
		test.started = true;
		if (ev->arg[0] == TCG_PLAYER) test.player_turns++;
	}
	if (ev->type == TCG_EV_TEXT && ev->text == FollowMyGuidancePracticeDuelText) {
		assert(test.wrong_energy && wDuelTurns == 2 && !test.retry_seen);
		assert(wTempCardID_ccc2 == SEAKING && wSelectedAttack == SECOND_ATTACK);
		assert(attached(WATER_ENERGY, PLAY_AREA_ARENA) == 2);
		assert(!attached(PSYCHIC_ENERGY, PLAY_AREA_ARENA));
		test.retry_seen = true;
	}
}

static int request(void *ctx, const TcgRequest *r)
{
	(void)ctx;
	assert(++test.requests < 1000);
	if(test.speech_waiting) {
		assert(r->type==TCG_REQ_ACK);
		test.speech_waiting=false;test.speech_acks++;
	}
	switch (r->type) {
	case TCG_REQ_ACK: case TCG_REQ_CONFIRM: return 0;
	case TCG_REQ_YES_NO: return 1;
	case TCG_REQ_DUEL_MENU:
		if (test.retry_seen) {
			assert(wDuelTurns == 2 && tcg_card_id(TCG_PLAYER, wPlayerArenaCard) == GOLDEEN);
			assert(!wAlreadyPlayedEnergy && attached(WATER_ENERGY, PLAY_AREA_ARENA) == 1);
			assert(!attached(PSYCHIC_ENERGY, PLAY_AREA_ARENA));
			test.retry_restored = true;
			assert(strstr(test.checklist,"Psychic Energy")&&strstr(test.checklist,"Waterfall"));
			longjmp(finished, 1);
		}
		return next_card() ? 0 : 1; /* Hand or Attack */
	case TCG_REQ_CARD_LIST: {
		if (!test.started && wPlayerNumberOfPokemonInPlayArea >= 2) return TCG_CANCEL;
		u8 wanted = test.started ? next_card() :
			wPlayerNumberOfPokemonInPlayArea ? STARYU : GOLDEEN;
		if (!wanted) return TCG_CANCEL;
		for (u8 i = 0; i < r->num_items; i++)
			if (tcg_card_id(r->shown, r->items[i]) == wanted) {
				test.selected_card = wanted;
				return i;
			}
		assert(!"tutorial card missing from hand");
		return TCG_CANCEL;
	}
	case TCG_REQ_CARD_ITEM_MENU: return 0;
	case TCG_REQ_PLAY_AREA:
		if (wDuelTurns == 7) return PLAY_AREA_BENCH_1;
		if (test.selected_card == WATER_ENERGY && wDuelTurns == 4) return PLAY_AREA_BENCH_1;
		if (test.selected_card == WATER_ENERGY && wDuelTurns == 6) return PLAY_AREA_BENCH_2;
		return PLAY_AREA_ARENA;
	case TCG_REQ_MENU:
		if (r->menu == TCG_MENU_ATTACKS) {
			static const u8 attacks[8] = {0, 1, 0, 1, 0, 0, 1, 1};
			return attacks[wDuelTurns >> 1];
		}
		assert(r->menu == TCG_MENU_PLAY_AREA);
		return 0; /* first bench slot replaces Seaking after its knockout */
	case TCG_REQ_PRIZE:
		for (u8 i = 0; i < 6; i++) if (r->arg[0] & (1 << i)) return i;
		assert(!"no prize available");
		return TCG_CANCEL;
	default: assert(!"unexpected tutorial request"); return TCG_CANCEL;
	}
}

static void run(bool wrong_energy)
{
	memset(&test, 0, sizeof(test));
	test.wrong_energy = wrong_energy;
	TcgHost host = {event, request, NULL, NULL};
	tcg_set_host(&host);
	TcgDuelSetup setup = {0};
	setup.duelist[0].kind = TCG_HUMAN;
	setup.duelist[0].deck_id = PRACTICE_PLAYER_DECK_ID;
	setup.duelist[1].kind = TCG_AI;
	setup.duelist[1].deck_id = SAMS_PRACTICE_DECK_ID;
	setup.prizes = 2;
	setup.animations_disabled = 1;
	setup.skip_delay_allowed = 1;
	if (!setjmp(finished)) {
		assert(tcg_run_duel(&setup) == 0);
		assert(!wrong_energy && test.player_turns == 8 && !test.retry_seen);
	} else {
		assert(wrong_energy && test.retry_seen && test.retry_restored);
	}
	assert(test.finally_seen&&test.recap_seen&&test.checklist_updates>0);
	assert(!test.speech_waiting&&test.speeches==test.speech_acks);
	/* Recorded before the presentation fix, including all8KB WRAM+128B HRAM.
	 * The correct duel and the wrong-Energy rewind retain exact source state. */
	assert(test.requests==(wrong_energy?63u:200u));
	assert(state_hash()==(wrong_energy?0x14b6a98d0ce934ecULL:0x0d4027b58c8a5c77ULL));
	printf("PRACTICE TRACE wrong=%d requests=%u memory=%llx\n",wrong_energy,test.requests,state_hash());
}

int main(void)
{
	run(false);
	run(true);
	puts("PRACTICE PASS: all eight turns; persistent checklist and fresh Mason ACKs; Finally/recap ordering; exact original memory/RNG/request traces; Psychic advances and wrong Water rewinds");
	return 0;
}
