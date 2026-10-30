/*
 * Port of src/home/ai.asm: the entry points of the duel AI (declared in engine.h).
 * They replace the weak stubs of src/ai_stubs.c.
 */
#include "ai/ai.h"

/* loads opponent deck at wOpponentDeckID to wOpponentDeck, and initializes wPlayerDuelistType.
 * on a duel against Sam, also loads PRACTICE_PLAYER_DECK to wPlayerDeck.
 * also, sets wRNG1, wRNG2, and wRNGCounter to $57. */
void LoadOpponentDeck(void)
{
	TRACE_FN();
	u8 a;
	wIsPracticeDuel = 0;
	a = wOpponentDeckID;
	if (a == SAMS_NORMAL_DECK_ID)
		goto normal_sam_duel;
	if (a != SAMS_PRACTICE_DECK_ID) /* or a */
		goto not_practice_duel;
	/* only practice duels will display help messages, but
	 * any duel with Sam will force the PRACTICE_PLAYER_DECK */
	/* .practice_sam_duel */
	a++;
	wIsPracticeDuel = a;
normal_sam_duel:
	a = 0;
	wOpponentDeckID = a;
	SwapTurn();
	LoadDeck(PRACTICE_PLAYER_DECK);
	SwapTurn();
	wRNG1 = 0x57;
	wRNG2 = 0x57;
	wRNGCounter = 0x57;
	a = 0;
not_practice_duel:
	/* convert from *_DECK_ID constant read from wOpponentDeckID to *_DECK constant */
	a = (u8)(a + 2);
	LoadDeck(a);
	if (wOpponentDeckID >= NUM_DECK_IDS + 1)
		wOpponentDeckID = PRACTICE_PLAYER_DECK_ID;
	/* .valid_deck: set opponent as controlled by AI */
	TurnDuelistVars()[DUELVARS_DUELIST_TYPE] = (u8)(wOpponentDeckID | DUELIST_TYPE_AI_OPP);
}

u8 AIDoAction_Turn(void)
{
	TRACE_FN();
	return AIDoAction(AIACTION_DO_TURN);
}

u8 AIDoAction_StartDuel(void)
{
	TRACE_FN();
	return AIDoAction(AIACTION_START_DUEL);
}

u8 AIDoAction_ForcedSwitch(void)
{
	TRACE_FN();
	u8 a = AIDoAction(AIACTION_FORCED_SWITCH);
	hTempPlayAreaLocation_ff9d = a;
	return a;
}

u8 AIDoAction_KOSwitch(void)
{
	TRACE_FN();
	u8 a = AIDoAction(AIACTION_KO_SWITCH);
	hTemp_ffa0 = a;
	return a;
}

u8 AIDoAction_TakePrize(void)
{
	TRACE_FN();
	return AIDoAction(AIACTION_TAKE_PRIZE); /* this line is not needed */
}

/* calls the appropriate AI routine to handle action,
 * depending on the deck ID (see engine/duel/ai/deck_ai.asm)
 * input:
 *	- a = AIACTION_* constant
 *
 * AI-vs-AI mode (gPerSideAI, port only): the original never runs two AI duelists.
 * Like the reference test harness does with the original ROM, each side then uses
 * its own deck AI: wOpponentDeckID is set to the turn duelist's deck ID
 * (DUELVARS_DUELIST_TYPE & $7f) for the duration of the action and restored afterwards. */
u8 AIDoAction(u8 action)
{
	TRACE_FN();
	bool per_side = gPerSideAI;
	u8 saved_deck_id = wOpponentDeckID;
	if (per_side)
		wOpponentDeckID = (u8)(TurnDuelistVars()[DUELVARS_DUELIST_TYPE] & 0x7f);

	/* (the bank switch to BANK(DeckAIPointerTable) and back is not modelled) */
	if (action == 0)
		/* if input was 0, copy deck data of turn player from the table's first word:
		 * never used by the original */
		tcg_fatal("AIDoAction(0)");
	if (action > AIACTION_TAKE_PRIZE)
		tcg_fatal("AIDoAction: action out of range");
	if (wOpponentDeckID > NUM_DECK_IDS)
		tcg_fatal("AIDoAction: wOpponentDeckID out of range");

	/* jump to corresponding AI routine related to input */
	u8 a = DeckAIPointerTable[wOpponentDeckID][action]();

	if (per_side)
		wOpponentDeckID = saved_deck_id;
	return a;
}
