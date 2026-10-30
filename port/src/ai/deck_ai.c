/*
 * Port of src/engine/duel/ai/deck_ai.asm and every decks/*.asm it INCLUDEs (in INCLUDE
 * order), plus decks/unreferenced.asm (INCLUDEd at the top of engine/duel/ai/core.asm).
 *
 * deck AIs are specialized to work on a given deck ID.
 * they decide what happens during a turn, what Pokemon cards
 * to pick during the start of the duel, etc.
 * the different scenarios these are used are listed in AIACTION_* constants.
 * each of these have a pointer table with the following structure:
 * dw .do_turn       : never called;
 *
 * dw .do_turn       : called to handle the main turn logic, from the beginning
 *                     of the turn up to the attack (or lack thereof);
 *
 * dw .start_duel    : called at the start of the duel to initialize some
 *                     variables and optionally set up CPU hand and deck;
 *
 * dw .forced_switch : logic to determine what Pokemon to pick when there's
 *                     an effect that forces AI to switch to Bench card;
 *
 * dw .ko_switch     : logic for picking which card to use after a KO;
 *
 * dw .take_prize    : logic to decide which prize card to pick.
 *
 * optionally, decks can also declare card lists that will add
 * more specialized logic during various generic AI routines,
 * and read during the .start_duel routines.
 * the pointers to these lists are stored in memory:
 * wAICardListAvoidPrize    : list of cards to avoid being placed as prize;
 * wAICardListArenaPriority : priority list of Arena card at duel start;
 * wAICardListBenchPriority : priority list of Bench cards at duel start;
 * wAICardListPlayFromHandPriority : priority list of cards to play from hand;
 * wAICardListRetreatBonus  : scores given to certain cards for retreat;
 * wAICardListEnergyBonus   : max number of energy cards and card scores.
 *
 * AI card retreat score bonus (MACRO ai_retreat: db card ID, db $80 + retreat score)
 * when the AI retreat routine runs through the Bench to choose
 * a Pokemon to switch to, it looks up in this list and if
 * a card ID matches, applies a retreat score bonus to this card.
 * positive (negative) means more (less) likely to switch to this card.
 *
 * AI card energy attach score bonus (MACRO ai_energy: db card ID,
 * db maximum number of attached cards, db $80 + energy score)
 * when the AI energy attachment routine runs through the Play Area to choose
 * a Pokemon to attach an energy card, it looks up in this list and if
 * a card ID matches, skips this card if the maximum number of energy
 * cards attached has been reached. If it hasn't been reached, additionally
 * applies a positive (or negative) AI score to attach energy to this card.
 *
 * store_list_pointer wSomeListPointer, SomeData stores in WRAM the (bank $05) address of
 * the list: StoreListPointer() (ai/ai.h). The lists are raw ROM blobs (tools/rom_blobs.txt).
 *
 * The .do_turn/.start_duel/... locals are static functions returning register a
 * (AIActionFunc): only the .forced_switch/.ko_switch results are used by the callers.
 */
#include "ai/ai.h"

/* ---- decks/unreferenced.asm (INCLUDEd at the top of core.asm, right after DeckAIPointerTable) */

static u8 AIActionTable_Unreferenced_do_turn(void);
static u8 AIActionTable_Unreferenced_star_duel(void);
static u8 AIActionTable_Unreferenced_forced_switch(void);
static u8 AIActionTable_Unreferenced_ko_switch(void);
static u8 AIActionTable_Unreferenced_take_prize(void);

/* unreferenced (the original reads its first word as DeckAIPointerTable[NUM_DECK_IDS]) */
const AIActionFunc AIActionTable_Unreferenced[AI_ACTION_TABLE_SIZE + 1] = {
	NULL, /* dw $406c (= AIActionTable_Unreferenced + 2; not code) */
	AIActionTable_Unreferenced_do_turn,
	AIActionTable_Unreferenced_do_turn,
	AIActionTable_Unreferenced_star_duel,
	AIActionTable_Unreferenced_forced_switch,
	AIActionTable_Unreferenced_ko_switch,
	AIActionTable_Unreferenced_take_prize,
};

static u8 AIActionTable_Unreferenced_do_turn(void)
{
	TRACE_FN();
	u8 a;
	AIDecidePlayPokemonCard();
	if (!AIDecideWhetherToRetreat())
		goto try_attack;
	AIDecideBenchPokemonToSwitchTo(&a);
	AITryToRetreat(a);
	if (!AIDecideWhetherToRetreat())
		goto try_attack;
	AIDecideBenchPokemonToSwitchTo(&a);
	AITryToRetreat(a);
try_attack:
	AIProcessAndTryToPlayEnergy();
	if (AIProcessAndTryToUseAttack())
		return 0;
	AIMakeDecision(OPPACTION_FINISH_NO_ATTACK);
	return 0;
}

static u8 AIActionTable_Unreferenced_star_duel(void)
{
	TRACE_FN();
	AIPlayInitialBasicCards();
	return 0;
}

static u8 AIActionTable_Unreferenced_forced_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_Unreferenced_ko_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_Unreferenced_take_prize(void)
{
	TRACE_FN();
	AIPickPrizeCards();
	return 0;
}

/* ---- decks/general.asm */

/* AI logic used by general decks */
static u8 AIActionTable_GeneralDecks_do_turn(void);
static u8 AIActionTable_GeneralDecks_start_duel(void);
static u8 AIActionTable_GeneralDecks_forced_switch(void);
static u8 AIActionTable_GeneralDecks_ko_switch(void);
static u8 AIActionTable_GeneralDecks_take_prize(void);

const AIActionFunc AIActionTable_GeneralDecks[AI_ACTION_TABLE_SIZE] = {
	AIActionTable_GeneralDecks_do_turn, /* unused */
	AIActionTable_GeneralDecks_do_turn,
	AIActionTable_GeneralDecks_start_duel,
	AIActionTable_GeneralDecks_forced_switch,
	AIActionTable_GeneralDecks_ko_switch,
	AIActionTable_GeneralDecks_take_prize,
};

static u8 AIActionTable_GeneralDecks_do_turn(void)
{
	TRACE_FN();
	AIMainTurnLogic();
	return 0;
}

static u8 AIActionTable_GeneralDecks_start_duel(void)
{
	TRACE_FN();
	InitAIDuelVars();
	AIPlayInitialBasicCards();
	return 0;
}

static u8 AIActionTable_GeneralDecks_forced_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_GeneralDecks_ko_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_GeneralDecks_take_prize(void)
{
	TRACE_FN();
	AIPickPrizeCards();
	return 0;
}

/* handle AI routines for a whole turn */
bool AIMainTurnLogic(void)
{
	TRACE_FN();
	/* initialize variables */
	InitAITurnVars();
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_01);
	if (!HandleAIAntiMewtwoDeckStrategy())
		goto try_attack;
	/* handle Pkmn Powers */
	HandleAIGoGoRainDanceEnergy();
	HandleAIDamageSwap();
	if (HandleAIPkmnPowers())
		return true; /* return if turn ended */
	HandleAICowardice();
	/* process Trainer cards
	 * phase 2 through 4. */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_02);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_03);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_04);
	/* play Pokemon from hand */
	if (AIDecidePlayPokemonCard())
		return true; /* return if turn ended */
	/* process Trainer cards
	 * phase 5 through 12. */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_05);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_06);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_07);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_08);
	AIProcessRetreat();
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_10);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_11);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_12);
	/* play Energy card if possible */
	if (wAlreadyPlayedEnergy == 0)
		AIProcessAndTryToPlayEnergy();
	/* .skip_energy_attach_1
	 * play Pokemon from hand again */
	AIDecidePlayPokemonCard();
	/* handle Pkmn Powers again */
	HandleAIDamageSwap();
	if (HandleAIPkmnPowers())
		return true; /* return if turn ended */
	HandleAIGoGoRainDanceEnergy();
	HandleAIEnergyTrans(AI_ENERGY_TRANS_ATTACK);
	/* process Trainer cards phases 13 and 15 */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_13);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_15);
	/* if used Professor Oak, process new hand
	 * if not, then proceed to attack. */
	if (!(wPreviousAIFlags & AI_FLAG_USED_PROFESSOR_OAK))
		goto try_attack;
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_01);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_02);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_03);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_04);
	if (AIDecidePlayPokemonCard())
		return true; /* return if turn ended */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_05);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_06);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_07);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_08);
	AIProcessRetreat();
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_10);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_11);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_12);
	if (wAlreadyPlayedEnergy == 0)
		AIProcessAndTryToPlayEnergy();
	/* .skip_energy_attach_2 */
	AIDecidePlayPokemonCard();
	HandleAIDamageSwap();
	if (HandleAIPkmnPowers())
		return true; /* return if turn ended */
	HandleAIGoGoRainDanceEnergy();
	HandleAIEnergyTrans(AI_ENERGY_TRANS_ATTACK);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_13);
	/* skip AI_TRAINER_CARD_PHASE_15 */
try_attack:
	HandleAIEnergyTrans(AI_ENERGY_TRANS_TO_BENCH);
	/* attack if possible, if not,
	 * finish turn without attacking. */
	if (AIProcessAndTryToUseAttack())
		return true; /* return if AI attacked */
	return AIMakeDecision(OPPACTION_FINISH_NO_ATTACK);
}

/* handles AI retreating logic */
void AIProcessRetreat(void)
{
	TRACE_FN();
	u8 a;
	if (wAIRetreatedThisTurn)
		return; /* return, already retreated this turn */

	if (!AIDecideWhetherToRetreat())
		return; /* return if not retreating */

	if (AIDecideBenchPokemonToSwitchTo(&a))
		return; /* return if no Bench Pokemon */

	/* store Play Area to retreat to and
	 * set wAIRetreatedThisTurn to true */
	wAIPlayAreaCardToSwitch = a;
	wAIRetreatedThisTurn = TRUE;

	/* if AI can use Switch from hand, use it instead... */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_09);
	if (!(wPreviousAIFlags & AI_FLAG_USED_SWITCH)) {
		/* ... else try retreating normally. */
		AITryToRetreat(wAIPlayAreaCardToSwitch);
		return;
	}

	/* .used_switch
	 * if AI used switch, unset its AI flag */
	wPreviousAIFlags = (u8)(wPreviousAIFlags & ~AI_FLAG_USED_SWITCH); /* clear Switch flag */

	HandleAIEnergyTrans(AI_ENERGY_TRANS_RETREAT);
}

/* ---- decks/sams_practice.asm */

/* AI for Sam's practice duel, which handles his scripted actions.
 * will act as a normal duelist AI after turn 7. */
static u8 AIActionTable_SamPractice_do_turn(void);
static u8 AIActionTable_SamPractice_start_duel(void);
static u8 AIActionTable_SamPractice_forced_switch(void);
static u8 AIActionTable_SamPractice_ko_switch(void);
static u8 AIActionTable_SamPractice_take_prize(void);

const AIActionFunc AIActionTable_SamPractice[AI_ACTION_TABLE_SIZE] = {
	AIActionTable_SamPractice_do_turn, /* unused */
	AIActionTable_SamPractice_do_turn,
	AIActionTable_SamPractice_start_duel,
	AIActionTable_SamPractice_forced_switch,
	AIActionTable_SamPractice_ko_switch,
	AIActionTable_SamPractice_take_prize,
};

static u8 AIActionTable_SamPractice_do_turn(void)
{
	TRACE_FN();
	if (!IsAIPracticeScriptedTurn())
		goto scripted_1;
	/* not scripted, use AI main turn logic */
	AIMainTurnLogic();
	return 0;
scripted_1: /* use scripted actions instead */
	AIPerformScriptedTurn();
	return 0;
}

static u8 AIActionTable_SamPractice_start_duel(void)
{
	TRACE_FN();
	SetSamsStartingPlayArea();
	return 0;
}

static u8 AIActionTable_SamPractice_forced_switch(void)
{
	TRACE_FN();
	u8 a;
	if (!IsAIPracticeScriptedTurn())
		goto scripted_2;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
scripted_2:
	return PickRandomBenchPokemon();
}

static u8 AIActionTable_SamPractice_ko_switch(void)
{
	TRACE_FN();
	u8 a;
	if (!IsAIPracticeScriptedTurn())
		goto scripted_3;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
scripted_3:
	return GetPlayAreaLocationOfRaticateOrRattata();
}

static u8 AIActionTable_SamPractice_take_prize(void)
{
	TRACE_FN();
	AIPickPrizeCards();
	return 0;
}

/* returns carry if number of turns
 * the AI has taken >= 7.
 * used to know whether AI Sam is still
 * doing scripted turns. */
bool IsAIPracticeScriptedTurn(void)
{
	TRACE_FN();
	/* srl a / cp 7 / ccf */
	return (u8)(wDuelTurns >> 1) >= 7;
}

/* places one Machop from the hand to the Play Area
 * and sets the number of prizes to 2. */
void SetSamsStartingPlayArea(void)
{
	TRACE_FN();
	CreateHandCardList(NULL);
	const u8 *hl = wDuelTempList;
	for (;;) {
		/* .loop_hand */
		u8 a = *hl++;
		hTempCardIndex_ff98 = a;
		if (a == 0xff)
			return;
		if (LoadCardDataToBuffer1_FromDeckIndex(a) == MACHOP)
			break;
	}
	PutHandPokemonCardInPlayArea(hTempCardIndex_ff98, NULL);
	wDuelInitialPrizes = 2;
}

/* outputs in a Play Area location of Raticate or Rattata
 * in the Bench. If neither is found, just output PLAY_AREA_BENCH_1. */
u8 GetPlayAreaLocationOfRaticateOrRattata(void)
{
	TRACE_FN();
	u8 a;
	/* bug (original): LookForCardIDInPlayArea_Bank5 returns MAX_PLAY_AREA_POKEMON (not $ff)
	 * when it checked every slot without finding the card (full bench), which counts as found */
	LookForCardIDInPlayArea_Bank5(RATICATE, PLAY_AREA_BENCH_1, &a);
	if (a != 0xff)
		goto found;
	LookForCardIDInPlayArea_Bank5(RATTATA, PLAY_AREA_BENCH_1, &a);
	if (a != 0xff)
		goto found;
	a = PLAY_AREA_BENCH_1;
found:
	hTempPlayAreaLocation_ff9d = a;
	return a;
}

static void AIPerformScriptedTurn_turn_1(void);
static void AIPerformScriptedTurn_turn_2(void);
static void AIPerformScriptedTurn_turn_3(void);
static void AIPerformScriptedTurn_turn_4(void);
static void AIPerformScriptedTurn_turn_5(void);
static void AIPerformScriptedTurn_turn_6(void);
static void AIPerformScriptedTurn_turn_7(void);

static void (*const AIPerformScriptedTurn_scripted_actions_list[7])(void) = {
	AIPerformScriptedTurn_turn_1,
	AIPerformScriptedTurn_turn_2,
	AIPerformScriptedTurn_turn_3,
	AIPerformScriptedTurn_turn_4,
	AIPerformScriptedTurn_turn_5,
	AIPerformScriptedTurn_turn_6,
	AIPerformScriptedTurn_turn_7,
};

/* has AI execute some scripted actions depending on Duel turn. */
bool AIPerformScriptedTurn(void)
{
	TRACE_FN();
	u8 a = (u8)(wDuelTurns >> 1);
	if (a >= 7)
		tcg_fatal("AIPerformScriptedTurn: turn out of the scripted_actions_list");
	AIPerformScriptedTurn_scripted_actions_list[a]();

	/* always attack with Arena card's first attack.
	 * if it's unusable end turn without attacking. */
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	wSelectedAttack = FIRST_ATTACK_OR_PKMN_POWER;
	if (CheckIfSelectedAttackIsUnusable())
		goto unusable;
	return AITryUseAttack();

unusable:
	return AIMakeDecision(OPPACTION_FINISH_NO_ATTACK);
}

static void AIPerformScriptedTurn_turn_1(void)
{
	AIAttachEnergyInHandToCardInPlayArea(MACHOP, FIGHTING_ENERGY);
}

static void AIPerformScriptedTurn_turn_2(void)
{
	u8 a;
	LookForCardIDInHandList_Bank5(RATTATA, &a);
	hTemp_ffa0 = a;
	AIMakeDecision(OPPACTION_PLAY_BASIC_PKMN);
	AIAttachEnergyInHandToCardInPlayArea(RATTATA, FIGHTING_ENERGY);
}

static void AIPerformScriptedTurn_turn_3(void)
{
	u8 a;
	LookForCardIDInPlayArea_Bank5(RATTATA, PLAY_AREA_ARENA, &a);
	hTempPlayAreaLocation_ffa1 = a;
	LookForCardIDInHandList_Bank5(RATICATE, &a);
	hTemp_ffa0 = a;
	AIMakeDecision(OPPACTION_EVOLVE_PKMN);
	AIAttachEnergyInHandToCardInPlayArea(RATICATE, LIGHTNING_ENERGY);
}

static void AIPerformScriptedTurn_turn_4(void)
{
	AIAttachEnergyInHandToCardInPlayArea(RATICATE, LIGHTNING_ENERGY);
}

static void AIPerformScriptedTurn_turn_5(void)
{
	u8 a;
	LookForCardIDInHandList_Bank5(MACHOP, &a);
	hTemp_ffa0 = a;
	AIMakeDecision(OPPACTION_PLAY_BASIC_PKMN);
	AIAttachEnergyInHandToCardInBench(MACHOP, FIGHTING_ENERGY);

	a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD);
	/* cp MACHOP ; wrong (compares a deck index with a card ID) */
	if (a != MACHOP)
		a = PLAY_AREA_BENCH_1;
	else
		a = PLAY_AREA_BENCH_1 + 1; /* inc a: PLAY_AREA_BENCH_2 */

	/* .retreat */
	AITryToRetreat(a);
}

static void AIPerformScriptedTurn_turn_6(void)
{
	AIAttachEnergyInHandToCardInPlayArea(MACHOP, FIGHTING_ENERGY);
}

static void AIPerformScriptedTurn_turn_7(void)
{
	AIAttachEnergyInHandToCardInPlayArea(MACHOP, FIGHTING_ENERGY);
}

/* ---- decks/general_no_retreat.asm */

/* acts just like a general deck AI except never retreats */
static u8 AIActionTable_GeneralNoRetreat_do_turn(void);
static u8 AIActionTable_GeneralNoRetreat_start_duel(void);
static u8 AIActionTable_GeneralNoRetreat_forced_switch(void);
static u8 AIActionTable_GeneralNoRetreat_ko_switch(void);
static u8 AIActionTable_GeneralNoRetreat_take_prize(void);

const AIActionFunc AIActionTable_GeneralNoRetreat[AI_ACTION_TABLE_SIZE] = {
	AIActionTable_GeneralNoRetreat_do_turn, /* unused */
	AIActionTable_GeneralNoRetreat_do_turn,
	AIActionTable_GeneralNoRetreat_start_duel,
	AIActionTable_GeneralNoRetreat_forced_switch,
	AIActionTable_GeneralNoRetreat_ko_switch,
	AIActionTable_GeneralNoRetreat_take_prize,
};

static u8 AIActionTable_GeneralNoRetreat_do_turn(void)
{
	TRACE_FN();
	AIDoTurn_GeneralNoRetreat();
	return 0;
}

static u8 AIActionTable_GeneralNoRetreat_start_duel(void)
{
	TRACE_FN();
	InitAIDuelVars();
	AIPlayInitialBasicCards();
	return 0;
}

static u8 AIActionTable_GeneralNoRetreat_forced_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_GeneralNoRetreat_ko_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_GeneralNoRetreat_take_prize(void)
{
	TRACE_FN();
	AIPickPrizeCards();
	return 0;
}

bool AIDoTurn_GeneralNoRetreat(void)
{
	TRACE_FN();
	/* initialize variables */
	InitAITurnVars();
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_01);
	if (!HandleAIAntiMewtwoDeckStrategy())
		goto try_attack;
	/* handle Pkmn Powers */
	HandleAIGoGoRainDanceEnergy();
	HandleAIDamageSwap();
	if (HandleAIPkmnPowers())
		return true; /* return if turn ended */
	HandleAICowardice();
	/* process Trainer cards
	 * phase 2 through 4. */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_02);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_03);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_04);
	/* play Pokemon from hand */
	if (AIDecidePlayPokemonCard())
		return true; /* return if turn ended */
	/* process Trainer cards
	 * phase 5 through 12. */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_05);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_06);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_07);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_08);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_10);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_11);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_12);
	/* play Energy card if possible */
	if (wAlreadyPlayedEnergy == 0)
		AIProcessAndTryToPlayEnergy();
	/* .skip_energy_attach_1
	 * play Pokemon from hand again */
	AIDecidePlayPokemonCard();
	/* handle Pkmn Powers again */
	HandleAIDamageSwap();
	if (HandleAIPkmnPowers())
		return true; /* return if turn ended */
	HandleAIGoGoRainDanceEnergy();
	HandleAIEnergyTrans(AI_ENERGY_TRANS_ATTACK);
	/* process Trainer cards phases 13 and 15 */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_13);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_15);
	/* if used Professor Oak, process new hand
	 * if not, then proceed to attack. */
	if (!(wPreviousAIFlags & AI_FLAG_USED_PROFESSOR_OAK))
		goto try_attack;
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_01);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_02);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_03);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_04);
	if (AIDecidePlayPokemonCard())
		return true; /* return if turn ended */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_05);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_06);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_07);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_08);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_10);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_11);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_12);
	if (wAlreadyPlayedEnergy == 0)
		AIProcessAndTryToPlayEnergy();
	/* .skip_energy_attach_2 */
	AIDecidePlayPokemonCard();
	HandleAIDamageSwap();
	if (HandleAIPkmnPowers())
		return true; /* return if turn ended */
	HandleAIGoGoRainDanceEnergy();
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_13);
	/* skip AI_TRAINER_CARD_PHASE_15 */
try_attack:
	/* attack if possible, if not,
	 * finish turn without attacking. */
	if (AIProcessAndTryToUseAttack())
		return true; /* return if turn ended */
	return AIMakeDecision(OPPACTION_FINISH_NO_ATTACK);
}

/* ---- decks/legendary_moltres.asm */

static u8 AIActionTable_LegendaryMoltres_do_turn(void);
static u8 AIActionTable_LegendaryMoltres_start_duel(void);
static u8 AIActionTable_LegendaryMoltres_forced_switch(void);
static u8 AIActionTable_LegendaryMoltres_ko_switch(void);
static u8 AIActionTable_LegendaryMoltres_take_prize(void);
static void AIActionTable_LegendaryMoltres_store_list_pointers(void);

const AIActionFunc AIActionTable_LegendaryMoltres[AI_ACTION_TABLE_SIZE] = {
	AIActionTable_LegendaryMoltres_do_turn, /* unused */
	AIActionTable_LegendaryMoltres_do_turn,
	AIActionTable_LegendaryMoltres_start_duel,
	AIActionTable_LegendaryMoltres_forced_switch,
	AIActionTable_LegendaryMoltres_ko_switch,
	AIActionTable_LegendaryMoltres_take_prize,
};

static u8 AIActionTable_LegendaryMoltres_do_turn(void)
{
	TRACE_FN();
	AIDoTurn_LegendaryMoltres();
	return 0;
}

static u8 AIActionTable_LegendaryMoltres_start_duel(void)
{
	TRACE_FN();
	InitAIDuelVars();
	AIActionTable_LegendaryMoltres_store_list_pointers();
	SetUpBossStartingHandAndDeck();
	if (!TrySetUpBossStartingPlayArea())
		return 0; /* Play Area set up was successful */
	AIPlayInitialBasicCards();
	return 0;
}

static u8 AIActionTable_LegendaryMoltres_forced_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_LegendaryMoltres_ko_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_LegendaryMoltres_take_prize(void)
{
	TRACE_FN();
	AIPickPrizeCards();
	return 0;
}

/* (the lists are raw ROM blobs, see tools/rom_blobs.txt; they are reached through
 * the wAICardList* pointers)
 * .list_arena
 *	db MAGMAR_LV31, GROWLITHE, VULPIX, MAGMAR_LV24, MOLTRES_LV35, MOLTRES_LV37, $00
 * .list_bench
 *	db MOLTRES_LV35, VULPIX, GROWLITHE, MAGMAR_LV31, MAGMAR_LV24, $00
 * .list_play_hand
 *	db MOLTRES_LV37, MOLTRES_LV35, VULPIX, GROWLITHE, MAGMAR_LV31, MAGMAR_LV24, $00
 * .list_retreat
 *	ai_retreat GROWLITHE, -5
 *	ai_retreat VULPIX, -5
 *	db $00
 * .list_energy
 *	ai_energy VULPIX, 3, +0
 *	ai_energy NINETALES_LV35, 3, +1
 *	ai_energy GROWLITHE, 3, +1
 *	ai_energy ARCANINE_LV45, 4, +1
 *	ai_energy MAGMAR_LV24, 4, -1
 *	ai_energy MAGMAR_LV31, 1, -1
 *	ai_energy MOLTRES_LV37, 3, +2
 *	ai_energy MOLTRES_LV35, 4, +2
 *	db $00
 * .list_prize
 *	db ENERGY_REMOVAL, MOLTRES_LV37, $00
 */

static void AIActionTable_LegendaryMoltres_store_list_pointers(void)
{
	StoreListPointer(wAICardListAvoidPrize, "AIActionTable_LegendaryMoltres.list_prize");
	StoreListPointer(wAICardListArenaPriority, "AIActionTable_LegendaryMoltres.list_arena");
	StoreListPointer(wAICardListBenchPriority, "AIActionTable_LegendaryMoltres.list_bench");
	StoreListPointer(wAICardListPlayFromHandPriority, "AIActionTable_LegendaryMoltres.list_play_hand");
	StoreListPointer(wAICardListRetreatBonus, "AIActionTable_LegendaryMoltres.list_retreat");
	StoreListPointer(wAICardListEnergyBonus, "AIActionTable_LegendaryMoltres.list_energy");
}

bool AIDoTurn_LegendaryMoltres(void)
{
	TRACE_FN();
	u8 a;
	/* initialize variables */
	InitAITurnVars();
	if (!HandleAIAntiMewtwoDeckStrategy())
		goto try_attack;
	/* process Trainer cards
	 * phase 2 through 4. */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_02);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_04);

	/* check if AI can play MoltresLv37
	 * from hand and if so, play it. */
	if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) >= MAX_PLAY_AREA_POKEMON)
		goto skip_moltres; /* skip if bench is full */
	if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK) >= DECK_SIZE - 9)
		goto skip_moltres; /* skip if cards in deck <= 9 */
	if (CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK))
		goto skip_moltres; /* skip if Muk in play */
	if (!LookForCardIDInHandList_Bank5(MOLTRES_LV37, &a))
		goto skip_moltres; /* skip if no MoltresLv37 in hand */
	hTemp_ffa0 = a;
	AIMakeDecision(OPPACTION_PLAY_BASIC_PKMN);

skip_moltres:
	/* play Pokemon from hand */
	if (AIDecidePlayPokemonCard())
		return true; /* return if turn ended */
	/* process Trainer cards */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_05);
	AIProcessRetreat();
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_10);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_11);
	/* play Energy card if possible */
	if (wAlreadyPlayedEnergy != 0)
		goto skip_attach_energy;

	/* if MagmarLv31 is the Arena card and has no energy attached,
	 * try attaching an energy card to it from the hand.
	 * otherwise, run normal AI energy attach routine. */
	if (GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD)) != MAGMAR_LV31)
		goto attach_normally;
	/* MagmarLv31 is the Arena card */
	if (CreateEnergyCardListFromHand())
		goto skip_attach_energy;
	if (CountNumberOfEnergyCardsAttached(PLAY_AREA_ARENA) != 0)
		goto attach_normally;
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	if (AITryToPlayEnergyCard())
		goto skip_attach_energy;

attach_normally:
	/* play Energy card if possible */
	AIProcessAndTryToPlayEnergy();
skip_attach_energy:
	/* try playing Pokemon cards from hand again */
	AIDecidePlayPokemonCard();
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_13);

try_attack:
	/* attack if possible, if not,
	 * finish turn without attacking. */
	if (AIProcessAndTryToUseAttack())
		return true;
	return AIMakeDecision(OPPACTION_FINISH_NO_ATTACK);
}

/* ---- decks/legendary_zapdos.asm */

static u8 AIActionTable_LegendaryZapdos_do_turn(void);
static u8 AIActionTable_LegendaryZapdos_start_duel(void);
static u8 AIActionTable_LegendaryZapdos_forced_switch(void);
static u8 AIActionTable_LegendaryZapdos_ko_switch(void);
static u8 AIActionTable_LegendaryZapdos_take_prize(void);
static void AIActionTable_LegendaryZapdos_store_list_pointers(void);

const AIActionFunc AIActionTable_LegendaryZapdos[AI_ACTION_TABLE_SIZE] = {
	AIActionTable_LegendaryZapdos_do_turn, /* unused */
	AIActionTable_LegendaryZapdos_do_turn,
	AIActionTable_LegendaryZapdos_start_duel,
	AIActionTable_LegendaryZapdos_forced_switch,
	AIActionTable_LegendaryZapdos_ko_switch,
	AIActionTable_LegendaryZapdos_take_prize,
};

static u8 AIActionTable_LegendaryZapdos_do_turn(void)
{
	TRACE_FN();
	AIDoTurn_LegendaryZapdos();
	return 0;
}

static u8 AIActionTable_LegendaryZapdos_start_duel(void)
{
	TRACE_FN();
	InitAIDuelVars();
	AIActionTable_LegendaryZapdos_store_list_pointers();
	SetUpBossStartingHandAndDeck();
	if (!TrySetUpBossStartingPlayArea())
		return 0;
	AIPlayInitialBasicCards();
	return 0;
}

static u8 AIActionTable_LegendaryZapdos_forced_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_LegendaryZapdos_ko_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_LegendaryZapdos_take_prize(void)
{
	TRACE_FN();
	AIPickPrizeCards();
	return 0;
}

/* (the lists are raw ROM blobs, see tools/rom_blobs.txt; they are reached through
 * the wAICardList* pointers)
 * .list_arena
 *	db ELECTABUZZ_LV35, VOLTORB, EEVEE, ZAPDOS_LV40, ZAPDOS_LV64, ZAPDOS_LV68, $00
 * .list_bench
 *	db ZAPDOS_LV64, ZAPDOS_LV40, EEVEE, VOLTORB, ELECTABUZZ_LV35, $00
 * .list_retreat
 *	ai_retreat EEVEE, -5
 *	ai_retreat VOLTORB, -5
 *	ai_retreat ELECTABUZZ_LV35, -5
 *	db $00
 * .list_energy
 *	ai_energy VOLTORB, 1, -1
 *	ai_energy ELECTRODE_LV35, 3, +0
 *	ai_energy ELECTABUZZ_LV35, 2, -1
 *	ai_energy JOLTEON_LV29, 3, +1
 *	ai_energy ZAPDOS_LV40, 4, +2
 *	ai_energy ZAPDOS_LV64, 4, +2
 *	ai_energy ZAPDOS_LV68, 3, +1
 *	ai_energy EEVEE, 3, +0
 *	db $00
 * .list_prize
 *	db GAMBLER, ZAPDOS_LV68, $00
 */

static void AIActionTable_LegendaryZapdos_store_list_pointers(void)
{
	StoreListPointer(wAICardListAvoidPrize, "AIActionTable_LegendaryZapdos.list_prize");
	StoreListPointer(wAICardListArenaPriority, "AIActionTable_LegendaryZapdos.list_arena");
	StoreListPointer(wAICardListBenchPriority, "AIActionTable_LegendaryZapdos.list_bench");
	StoreListPointer(wAICardListPlayFromHandPriority, "AIActionTable_LegendaryZapdos.list_bench");
	/* missing store_list_pointer wAICardListRetreatBonus, .list_retreat */
	StoreListPointer(wAICardListEnergyBonus, "AIActionTable_LegendaryZapdos.list_energy");
}

bool AIDoTurn_LegendaryZapdos(void)
{
	TRACE_FN();
	u8 a, e;
	/* initialize variables */
	InitAITurnVars();
	if (!HandleAIAntiMewtwoDeckStrategy())
		goto try_attack;
	/* process Trainer cards */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_01);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_04);
	/* play Pokemon from hand */
	if (AIDecidePlayPokemonCard())
		return true; /* return if turn ended */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_07);
	AIProcessRetreat();
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_10);
	/* play Energy card if possible. */
	if (wAlreadyPlayedEnergy != 0)
		goto skip_energy_attach;

	/* if Arena card is Voltorb and there's ElectrodeLv35 in hand,
	 * or if it's Electabuzz, try attaching Energy card
	 * to the Arena card if it doesn't have any energy attached.
	 * Otherwise if Energy card is not needed,
	 * go through normal AI energy attach routine. */
	e = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	if (e != VOLTORB)
		goto check_electabuzz;
	if (!LookForCardIDInHandList_Bank5(ELECTRODE_LV35, &a))
		goto attach_normally;
	goto voltorb_or_electabuzz;
check_electabuzz:
	if (e != ELECTABUZZ_LV35)
		goto attach_normally;

voltorb_or_electabuzz:
	if (CreateEnergyCardListFromHand())
		goto skip_energy_attach;
	if (CountNumberOfEnergyCardsAttached(PLAY_AREA_ARENA) != 0)
		goto attach_normally;
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	if (AITryToPlayEnergyCard())
		goto skip_energy_attach;

attach_normally:
	AIProcessAndTryToPlayEnergy();

skip_energy_attach:
	/* play Pokemon from hand again */
	if (AIDecidePlayPokemonCard())
		return true; /* return if turn ended */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_13);
try_attack:
	/* attack if possible, if not,
	 * finish turn without attacking. */
	if (AIProcessAndTryToUseAttack())
		return true; /* return if turn ended */
	return AIMakeDecision(OPPACTION_FINISH_NO_ATTACK);
}

/* ---- decks/legendary_articuno.asm */

static u8 AIActionTable_LegendaryArticuno_do_turn(void);
static u8 AIActionTable_LegendaryArticuno_start_duel(void);
static u8 AIActionTable_LegendaryArticuno_forced_switch(void);
static u8 AIActionTable_LegendaryArticuno_ko_switch(void);
static u8 AIActionTable_LegendaryArticuno_take_prize(void);
static void AIActionTable_LegendaryArticuno_store_list_pointers(void);

const AIActionFunc AIActionTable_LegendaryArticuno[AI_ACTION_TABLE_SIZE] = {
	AIActionTable_LegendaryArticuno_do_turn, /* unused */
	AIActionTable_LegendaryArticuno_do_turn,
	AIActionTable_LegendaryArticuno_start_duel,
	AIActionTable_LegendaryArticuno_forced_switch,
	AIActionTable_LegendaryArticuno_ko_switch,
	AIActionTable_LegendaryArticuno_take_prize,
};

static u8 AIActionTable_LegendaryArticuno_do_turn(void)
{
	TRACE_FN();
	AIDoTurn_LegendaryArticuno();
	return 0;
}

static u8 AIActionTable_LegendaryArticuno_start_duel(void)
{
	TRACE_FN();
	InitAIDuelVars();
	AIActionTable_LegendaryArticuno_store_list_pointers();
	SetUpBossStartingHandAndDeck();
	if (!TrySetUpBossStartingPlayArea())
		return 0;
	AIPlayInitialBasicCards();
	return 0;
}

static u8 AIActionTable_LegendaryArticuno_forced_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_LegendaryArticuno_ko_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_LegendaryArticuno_take_prize(void)
{
	TRACE_FN();
	AIPickPrizeCards();
	return 0;
}

/* (the lists are raw ROM blobs, see tools/rom_blobs.txt; they are reached through
 * the wAICardList* pointers)
 * .list_arena
 *	db CHANSEY, LAPRAS, DITTO, SEEL, ARTICUNO_LV35, ARTICUNO_LV37, $00
 * .list_bench
 *	db ARTICUNO_LV35, SEEL, LAPRAS, CHANSEY, DITTO, $00
 * .list_retreat
 *	ai_retreat SEEL, -3
 *	ai_retreat DITTO, -3
 *	db $00
 * .list_energy
 *	ai_energy SEEL, 3, +1
 *	ai_energy DEWGONG, 4, +0
 *	ai_energy LAPRAS, 3, +0
 *	ai_energy ARTICUNO_LV35, 4, +1
 *	ai_energy ARTICUNO_LV37, 3, +0
 *	ai_energy CHANSEY, 0, -8
 *	ai_energy DITTO, 3, +0
 *	db $00
 * .list_prize
 *	db GAMBLER, ARTICUNO_LV37, $00
 */

static void AIActionTable_LegendaryArticuno_store_list_pointers(void)
{
	StoreListPointer(wAICardListAvoidPrize, "AIActionTable_LegendaryArticuno.list_prize");
	StoreListPointer(wAICardListArenaPriority, "AIActionTable_LegendaryArticuno.list_arena");
	StoreListPointer(wAICardListBenchPriority, "AIActionTable_LegendaryArticuno.list_bench");
	StoreListPointer(wAICardListPlayFromHandPriority, "AIActionTable_LegendaryArticuno.list_bench");
	/* missing store_list_pointer wAICardListRetreatBonus, .list_retreat */
	StoreListPointer(wAICardListEnergyBonus, "AIActionTable_LegendaryArticuno.list_energy");
}

/* this routine handles how Legendary Articuno
 * prioritizes playing energy cards to each Pokémon.
 * first, it makes sure that all Lapras have at least
 * 3 energy cards before moving on to Articuno,
 * and then to Dewgong and Seel */
void ScoreLegendaryArticunoCards(void)
{
	TRACE_FN();
	u8 a;
	SwapTurn();
	a = CountPrizes();
	SwapTurn();
	if (a < 3)
		return;

	/* player prizes >= 3
	 * if Lapras has more than half HP and
	 * can use second attack, check next for Articuno
	 * otherwise, check if Articuno or Dewgong
	 * have more than half HP and can use second attack
	 * and if so, the next Pokémon to check is Lapras */
	if (CheckForBenchIDAtHalfHPAndCanUseSecondAttack(LAPRAS))
		goto articuno;
	if (CheckForBenchIDAtHalfHPAndCanUseSecondAttack(ARTICUNO_LV35))
		goto lapras;
	if (CheckForBenchIDAtHalfHPAndCanUseSecondAttack(DEWGONG))
		goto lapras;
	goto articuno;

	/* the following routines check for certain card IDs in bench
	 * and call RaiseAIScoreToAllMatchingIDsInBench if these are found.
	 * for Lapras, an additional check is made to its
	 * attached energy count, which skips calling the routine
	 * if this count is >= 3 */
lapras:
	if (!LookForCardIDInPlayArea_Bank5(LAPRAS, PLAY_AREA_BENCH_1, &a))
		goto articuno;
	if (CountNumberOfEnergyCardsAttached(a) >= 3)
		goto articuno;
	RaiseAIScoreToAllMatchingIDsInBench(LAPRAS);
	return;

articuno:
	if (!LookForCardIDInPlayArea_Bank5(ARTICUNO_LV35, PLAY_AREA_BENCH_1, &a))
		goto dewgong;
	RaiseAIScoreToAllMatchingIDsInBench(ARTICUNO_LV35);
	return;

dewgong:
	if (!LookForCardIDInPlayArea_Bank5(DEWGONG, PLAY_AREA_BENCH_1, &a))
		goto seel;
	RaiseAIScoreToAllMatchingIDsInBench(DEWGONG);
	return;

seel:
	if (!LookForCardIDInPlayArea_Bank5(SEEL, PLAY_AREA_BENCH_1, &a))
		return;
	RaiseAIScoreToAllMatchingIDsInBench(SEEL);
}

bool AIDoTurn_LegendaryArticuno(void)
{
	TRACE_FN();
	/* initialize variables */
	InitAITurnVars();
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_01);
	if (!HandleAIAntiMewtwoDeckStrategy())
		goto try_attack;
	/* process Trainer cards */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_02);
	/* play Pokemon from hand */
	if (AIDecidePlayPokemonCard())
		return true; /* return if turn ended */
	AIProcessRetreat();
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_10);
	/* play Energy card if possible */
	if (wAlreadyPlayedEnergy == 0)
		AIProcessAndTryToPlayEnergy();
	/* .skip_energy_attach_1
	 * play Pokemon from hand again */
	AIDecidePlayPokemonCard();
	/* process Trainer cards phases 13 and 15 */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_13);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_15);
	/* if used Professor Oak, process new hand */
	if (!(wPreviousAIFlags & AI_FLAG_USED_PROFESSOR_OAK))
		goto try_attack;
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_01);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_02);
	if (AIDecidePlayPokemonCard())
		return true; /* return if turn ended */
	AIProcessRetreat();
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_10);
	if (wAlreadyPlayedEnergy == 0)
		AIProcessAndTryToPlayEnergy();
	/* .skip_energy_attach_2 */
	AIDecidePlayPokemonCard();
try_attack:
	/* attack if possible, if not,
	 * finish turn without attacking. */
	if (AIProcessAndTryToUseAttack())
		return true; /* return if turn ended */
	return AIMakeDecision(OPPACTION_FINISH_NO_ATTACK);
}

/* ---- decks/legendary_dragonite.asm */

static u8 AIActionTable_LegendaryDragonite_do_turn(void);
static u8 AIActionTable_LegendaryDragonite_start_duel(void);
static u8 AIActionTable_LegendaryDragonite_forced_switch(void);
static u8 AIActionTable_LegendaryDragonite_ko_switch(void);
static u8 AIActionTable_LegendaryDragonite_take_prize(void);
static void AIActionTable_LegendaryDragonite_store_list_pointers(void);

const AIActionFunc AIActionTable_LegendaryDragonite[AI_ACTION_TABLE_SIZE] = {
	AIActionTable_LegendaryDragonite_do_turn, /* unused */
	AIActionTable_LegendaryDragonite_do_turn,
	AIActionTable_LegendaryDragonite_start_duel,
	AIActionTable_LegendaryDragonite_forced_switch,
	AIActionTable_LegendaryDragonite_ko_switch,
	AIActionTable_LegendaryDragonite_take_prize,
};

static u8 AIActionTable_LegendaryDragonite_do_turn(void)
{
	TRACE_FN();
	AIDoTurn_LegendaryDragonite();
	return 0;
}

static u8 AIActionTable_LegendaryDragonite_start_duel(void)
{
	TRACE_FN();
	InitAIDuelVars();
	AIActionTable_LegendaryDragonite_store_list_pointers();
	SetUpBossStartingHandAndDeck();
	if (!TrySetUpBossStartingPlayArea())
		return 0;
	AIPlayInitialBasicCards();
	return 0;
}

static u8 AIActionTable_LegendaryDragonite_forced_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_LegendaryDragonite_ko_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_LegendaryDragonite_take_prize(void)
{
	TRACE_FN();
	AIPickPrizeCards();
	return 0;
}

/* (the lists are raw ROM blobs, see tools/rom_blobs.txt; they are reached through
 * the wAICardList* pointers)
 * .list_arena
 *	db KANGASKHAN, LAPRAS, CHARMANDER, DRATINI, MAGIKARP, $00
 * .list_bench
 *	db CHARMANDER, MAGIKARP, DRATINI, LAPRAS, KANGASKHAN, $00
 * .list_retreat
 *	ai_retreat CHARMANDER, -1
 *	ai_retreat MAGIKARP, -5
 *	db $00
 * .list_energy
 *	ai_energy CHARMANDER, 3, +1
 *	ai_energy CHARMELEON, 4, +1
 *	ai_energy CHARIZARD, 5, +0
 *	ai_energy MAGIKARP, 3, +1
 *	ai_energy GYARADOS, 4, -1
 *	ai_energy DRATINI, 2, +0
 *	ai_energy DRAGONAIR, 4, +0
 *	ai_energy DRAGONITE_LV41, 3, -1
 *	ai_energy KANGASKHAN, 2, -2
 *	ai_energy LAPRAS, 3, +0
 *	db $00
 * .list_prize
 *	db GAMBLER, DRAGONITE_LV41, KANGASKHAN, $00
 */

static void AIActionTable_LegendaryDragonite_store_list_pointers(void)
{
	StoreListPointer(wAICardListAvoidPrize, "AIActionTable_LegendaryDragonite.list_prize");
	StoreListPointer(wAICardListArenaPriority, "AIActionTable_LegendaryDragonite.list_arena");
	StoreListPointer(wAICardListBenchPriority, "AIActionTable_LegendaryDragonite.list_bench");
	StoreListPointer(wAICardListPlayFromHandPriority, "AIActionTable_LegendaryDragonite.list_bench");
	/* missing store_list_pointer wAICardListRetreatBonus, .list_retreat */
	StoreListPointer(wAICardListEnergyBonus, "AIActionTable_LegendaryDragonite.list_energy");
}

bool AIDoTurn_LegendaryDragonite(void)
{
	TRACE_FN();
	/* initialize variables */
	InitAITurnVars();
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_01);
	if (!HandleAIAntiMewtwoDeckStrategy())
		goto try_attack;
	/* process Trainer cards */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_02);
	/* play Pokemon from hand */
	if (AIDecidePlayPokemonCard())
		return true; /* return if turn ended */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_07);
	AIProcessRetreat();
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_10);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_11);
	/* play Energy card if possible */
	if (wAlreadyPlayedEnergy != 0)
		goto skip_energy_attach_1;

	/* if Arena card is Kangaskhan and doesn't
	 * have Energy cards attached, try attaching from hand.
	 * otherwise run normal AI energy attach routine. */
	if (GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD)) != KANGASKHAN)
		goto attach_normally;
	if (CreateEnergyCardListFromHand())
		goto skip_energy_attach_1;
	if (CountNumberOfEnergyCardsAttached(PLAY_AREA_ARENA) != 0)
		goto attach_normally;
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	if (AITryToPlayEnergyCard())
		goto skip_energy_attach_1;
attach_normally:
	AIProcessAndTryToPlayEnergy();

skip_energy_attach_1:
	/* play Pokemon from hand again */
	AIDecidePlayPokemonCard();
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_15);
	/* if used Professor Oak, process new hand
	 * if not, then proceed to attack. */
	if (!(wPreviousAIFlags & AI_FLAG_USED_PROFESSOR_OAK))
		goto try_attack;
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_01);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_02);
	if (AIDecidePlayPokemonCard())
		return true; /* return if turn ended */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_07);
	AIProcessRetreat();
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_10);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_11);
	if (wAlreadyPlayedEnergy == 0)
		AIProcessAndTryToPlayEnergy();
	/* .skip_energy_attach_2 */
	AIDecidePlayPokemonCard();
try_attack:
	/* attack if possible, if not,
	 * finish turn without attacking. */
	if (AIProcessAndTryToUseAttack())
		return true; /* return if turn ended */
	return AIMakeDecision(OPPACTION_FINISH_NO_ATTACK);
}

/* ---- decks/first_strike.asm */

static u8 AIActionTable_FirstStrike_do_turn(void);
static u8 AIActionTable_FirstStrike_start_duel(void);
static u8 AIActionTable_FirstStrike_forced_switch(void);
static u8 AIActionTable_FirstStrike_ko_switch(void);
static u8 AIActionTable_FirstStrike_take_prize(void);
static void AIActionTable_FirstStrike_store_list_pointers(void);

const AIActionFunc AIActionTable_FirstStrike[AI_ACTION_TABLE_SIZE] = {
	AIActionTable_FirstStrike_do_turn, /* unused */
	AIActionTable_FirstStrike_do_turn,
	AIActionTable_FirstStrike_start_duel,
	AIActionTable_FirstStrike_forced_switch,
	AIActionTable_FirstStrike_ko_switch,
	AIActionTable_FirstStrike_take_prize,
};

static u8 AIActionTable_FirstStrike_do_turn(void)
{
	TRACE_FN();
	AIMainTurnLogic();
	return 0;
}

static u8 AIActionTable_FirstStrike_start_duel(void)
{
	TRACE_FN();
	InitAIDuelVars();
	AIActionTable_FirstStrike_store_list_pointers();
	SetUpBossStartingHandAndDeck();
	if (!TrySetUpBossStartingPlayArea())
		return 0;
	AIPlayInitialBasicCards();
	return 0;
}

static u8 AIActionTable_FirstStrike_forced_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_FirstStrike_ko_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_FirstStrike_take_prize(void)
{
	TRACE_FN();
	AIPickPrizeCards();
	return 0;
}

/* (the lists are raw ROM blobs, see tools/rom_blobs.txt; they are reached through
 * the wAICardList* pointers)
 * .list_arena
 *	db HITMONCHAN, MACHOP, HITMONLEE, MANKEY, $00
 * .list_bench
 *	db MACHOP, HITMONLEE, HITMONCHAN, MANKEY, $00
 * .list_retreat
 *	ai_retreat MACHOP, -1
 *	ai_retreat MACHOKE, -1
 *	ai_retreat MANKEY, -2
 *	db $00
 * .list_energy
 *	ai_energy MACHOP, 3, +0
 *	ai_energy MACHOKE, 4, +0
 *	ai_energy MACHAMP, 4, -1
 *	ai_energy HITMONCHAN, 3, +0
 *	ai_energy HITMONLEE, 3, +0
 *	ai_energy MANKEY, 2, -1
 *	ai_energy PRIMEAPE, 3, -1
 *	db $00
 * .list_prize
 *	db HITMONLEE, HITMONCHAN, $00
 */

static void AIActionTable_FirstStrike_store_list_pointers(void)
{
	StoreListPointer(wAICardListAvoidPrize, "AIActionTable_FirstStrike.list_prize");
	StoreListPointer(wAICardListArenaPriority, "AIActionTable_FirstStrike.list_arena");
	StoreListPointer(wAICardListBenchPriority, "AIActionTable_FirstStrike.list_bench");
	StoreListPointer(wAICardListPlayFromHandPriority, "AIActionTable_FirstStrike.list_bench");
	/* missing store_list_pointer wAICardListRetreatBonus, .list_retreat */
	StoreListPointer(wAICardListEnergyBonus, "AIActionTable_FirstStrike.list_energy");
}

/* ---- decks/rock_crusher.asm */

static u8 AIActionTable_RockCrusher_do_turn(void);
static u8 AIActionTable_RockCrusher_start_duel(void);
static u8 AIActionTable_RockCrusher_forced_switch(void);
static u8 AIActionTable_RockCrusher_ko_switch(void);
static u8 AIActionTable_RockCrusher_take_prize(void);
static void AIActionTable_RockCrusher_store_list_pointers(void);

const AIActionFunc AIActionTable_RockCrusher[AI_ACTION_TABLE_SIZE] = {
	AIActionTable_RockCrusher_do_turn, /* unused */
	AIActionTable_RockCrusher_do_turn,
	AIActionTable_RockCrusher_start_duel,
	AIActionTable_RockCrusher_forced_switch,
	AIActionTable_RockCrusher_ko_switch,
	AIActionTable_RockCrusher_take_prize,
};

static u8 AIActionTable_RockCrusher_do_turn(void)
{
	TRACE_FN();
	AIMainTurnLogic();
	return 0;
}

static u8 AIActionTable_RockCrusher_start_duel(void)
{
	TRACE_FN();
	InitAIDuelVars();
	AIActionTable_RockCrusher_store_list_pointers();
	SetUpBossStartingHandAndDeck();
	if (!TrySetUpBossStartingPlayArea())
		return 0;
	AIPlayInitialBasicCards();
	return 0;
}

static u8 AIActionTable_RockCrusher_forced_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_RockCrusher_ko_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_RockCrusher_take_prize(void)
{
	TRACE_FN();
	AIPickPrizeCards();
	return 0;
}

/* (the lists are raw ROM blobs, see tools/rom_blobs.txt; they are reached through
 * the wAICardList* pointers)
 * .list_arena
 *	db RHYHORN, ONIX, GEODUDE, DIGLETT, $00
 * .list_bench
 *	db DIGLETT, GEODUDE, RHYHORN, ONIX, $00
 * .list_retreat
 *	ai_retreat DIGLETT, -1
 *	db $00
 * .list_energy
 *	ai_energy DIGLETT, 3, +1
 *	ai_energy DUGTRIO, 4, +0
 *	ai_energy GEODUDE, 2, +1
 *	ai_energy GRAVELER, 3, +0
 *	ai_energy GOLEM, 4, +0
 *	ai_energy ONIX, 2, -1
 *	ai_energy RHYHORN, 3, +0
 *	db $00
 * .list_prize
 *	db ENERGY_REMOVAL, RHYHORN, $00
 */

static void AIActionTable_RockCrusher_store_list_pointers(void)
{
	StoreListPointer(wAICardListAvoidPrize, "AIActionTable_RockCrusher.list_prize");
	StoreListPointer(wAICardListArenaPriority, "AIActionTable_RockCrusher.list_arena");
	StoreListPointer(wAICardListBenchPriority, "AIActionTable_RockCrusher.list_bench");
	StoreListPointer(wAICardListPlayFromHandPriority, "AIActionTable_RockCrusher.list_bench");
	/* missing store_list_pointer wAICardListRetreatBonus, .list_retreat */
	StoreListPointer(wAICardListEnergyBonus, "AIActionTable_RockCrusher.list_energy");
}

/* ---- decks/go_go_rain_dance.asm */

static u8 AIActionTable_GoGoRainDance_do_turn(void);
static u8 AIActionTable_GoGoRainDance_start_duel(void);
static u8 AIActionTable_GoGoRainDance_forced_switch(void);
static u8 AIActionTable_GoGoRainDance_ko_switch(void);
static u8 AIActionTable_GoGoRainDance_take_prize(void);
static void AIActionTable_GoGoRainDance_store_list_pointers(void);

const AIActionFunc AIActionTable_GoGoRainDance[AI_ACTION_TABLE_SIZE] = {
	AIActionTable_GoGoRainDance_do_turn, /* unused */
	AIActionTable_GoGoRainDance_do_turn,
	AIActionTable_GoGoRainDance_start_duel,
	AIActionTable_GoGoRainDance_forced_switch,
	AIActionTable_GoGoRainDance_ko_switch,
	AIActionTable_GoGoRainDance_take_prize,
};

static u8 AIActionTable_GoGoRainDance_do_turn(void)
{
	TRACE_FN();
	AIMainTurnLogic();
	return 0;
}

static u8 AIActionTable_GoGoRainDance_start_duel(void)
{
	TRACE_FN();
	InitAIDuelVars();
	AIActionTable_GoGoRainDance_store_list_pointers();
	SetUpBossStartingHandAndDeck();
	if (!TrySetUpBossStartingPlayArea())
		return 0;
	AIPlayInitialBasicCards();
	return 0;
}

static u8 AIActionTable_GoGoRainDance_forced_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_GoGoRainDance_ko_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_GoGoRainDance_take_prize(void)
{
	TRACE_FN();
	AIPickPrizeCards();
	return 0;
}

/* (the lists are raw ROM blobs, see tools/rom_blobs.txt; they are reached through
 * the wAICardList* pointers)
 * .list_arena
 *	db LAPRAS, HORSEA, GOLDEEN, SQUIRTLE, $00
 * .list_bench
 *	db SQUIRTLE, HORSEA, GOLDEEN, LAPRAS, $00
 * .list_retreat
 *	ai_retreat SQUIRTLE, -3
 *	ai_retreat WARTORTLE, -2
 *	ai_retreat HORSEA, -1
 *	db $00
 * .list_energy
 *	ai_energy SQUIRTLE, 2, +0
 *	ai_energy WARTORTLE, 3, +0
 *	ai_energy BLASTOISE, 5, +0
 *	ai_energy GOLDEEN, 1, +0
 *	ai_energy SEAKING, 2, +0
 *	ai_energy HORSEA, 2, +0
 *	ai_energy SEADRA, 3, +0
 *	ai_energy LAPRAS, 3, +0
 *	db $00
 * .list_prize
 *	db GAMBLER, ENERGY_RETRIEVAL, SUPER_ENERGY_RETRIEVAL, BLASTOISE, $00
 */

static void AIActionTable_GoGoRainDance_store_list_pointers(void)
{
	StoreListPointer(wAICardListAvoidPrize, "AIActionTable_GoGoRainDance.list_prize");
	StoreListPointer(wAICardListArenaPriority, "AIActionTable_GoGoRainDance.list_arena");
	StoreListPointer(wAICardListBenchPriority, "AIActionTable_GoGoRainDance.list_bench");
	StoreListPointer(wAICardListPlayFromHandPriority, "AIActionTable_GoGoRainDance.list_bench");
	/* missing store_list_pointer wAICardListRetreatBonus, .list_retreat */
	StoreListPointer(wAICardListEnergyBonus, "AIActionTable_GoGoRainDance.list_energy");
}

/* ---- decks/zapping_selfdestruct.asm */

static u8 AIActionTable_ZappingSelfdestruct_do_turn(void);
static u8 AIActionTable_ZappingSelfdestruct_start_duel(void);
static u8 AIActionTable_ZappingSelfdestruct_forced_switch(void);
static u8 AIActionTable_ZappingSelfdestruct_ko_switch(void);
static u8 AIActionTable_ZappingSelfdestruct_take_prize(void);
static void AIActionTable_ZappingSelfdestruct_store_list_pointers(void);

const AIActionFunc AIActionTable_ZappingSelfdestruct[AI_ACTION_TABLE_SIZE] = {
	AIActionTable_ZappingSelfdestruct_do_turn, /* unused */
	AIActionTable_ZappingSelfdestruct_do_turn,
	AIActionTable_ZappingSelfdestruct_start_duel,
	AIActionTable_ZappingSelfdestruct_forced_switch,
	AIActionTable_ZappingSelfdestruct_ko_switch,
	AIActionTable_ZappingSelfdestruct_take_prize,
};

static u8 AIActionTable_ZappingSelfdestruct_do_turn(void)
{
	TRACE_FN();
	AIMainTurnLogic();
	return 0;
}

static u8 AIActionTable_ZappingSelfdestruct_start_duel(void)
{
	TRACE_FN();
	InitAIDuelVars();
	AIActionTable_ZappingSelfdestruct_store_list_pointers();
	SetUpBossStartingHandAndDeck();
	if (!TrySetUpBossStartingPlayArea())
		return 0;
	AIPlayInitialBasicCards();
	return 0;
}

static u8 AIActionTable_ZappingSelfdestruct_forced_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_ZappingSelfdestruct_ko_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_ZappingSelfdestruct_take_prize(void)
{
	TRACE_FN();
	AIPickPrizeCards();
	return 0;
}

/* (the lists are raw ROM blobs, see tools/rom_blobs.txt; they are reached through
 * the wAICardList* pointers)
 * .list_arena
 *	db KANGASKHAN, ELECTABUZZ_LV35, TAUROS, MAGNEMITE_LV13, VOLTORB, $00
 * .list_bench
 *	db MAGNEMITE_LV13, VOLTORB, ELECTABUZZ_LV35, TAUROS, KANGASKHAN, $00
 * .list_retreat
 *	ai_retreat VOLTORB, -1
 *	db $00
 * .list_energy
 *	ai_energy MAGNEMITE_LV13, 3, +1
 *	ai_energy MAGNETON_LV28, 4, +0
 *	ai_energy VOLTORB, 3, +1
 *	ai_energy ELECTRODE_LV35, 3, +0
 *	ai_energy ELECTABUZZ_LV35, 1, +0
 *	ai_energy KANGASKHAN, 2, -2
 *	ai_energy TAUROS, 3, +0
 *	db $00
 * .list_prize
 *	db KANGASKHAN, $00
 */

static void AIActionTable_ZappingSelfdestruct_store_list_pointers(void)
{
	StoreListPointer(wAICardListAvoidPrize, "AIActionTable_ZappingSelfdestruct.list_prize");
	StoreListPointer(wAICardListArenaPriority, "AIActionTable_ZappingSelfdestruct.list_arena");
	StoreListPointer(wAICardListBenchPriority, "AIActionTable_ZappingSelfdestruct.list_bench");
	StoreListPointer(wAICardListPlayFromHandPriority, "AIActionTable_ZappingSelfdestruct.list_bench");
	/* missing store_list_pointer wAICardListRetreatBonus, .list_retreat */
	StoreListPointer(wAICardListEnergyBonus, "AIActionTable_ZappingSelfdestruct.list_energy");
}

/* ---- decks/flower_power.asm */

static u8 AIActionTable_FlowerPower_do_turn(void);
static u8 AIActionTable_FlowerPower_start_duel(void);
static u8 AIActionTable_FlowerPower_forced_switch(void);
static u8 AIActionTable_FlowerPower_ko_switch(void);
static u8 AIActionTable_FlowerPower_take_prize(void);
static void AIActionTable_FlowerPower_store_list_pointers(void);

const AIActionFunc AIActionTable_FlowerPower[AI_ACTION_TABLE_SIZE] = {
	AIActionTable_FlowerPower_do_turn, /* unused */
	AIActionTable_FlowerPower_do_turn,
	AIActionTable_FlowerPower_start_duel,
	AIActionTable_FlowerPower_forced_switch,
	AIActionTable_FlowerPower_ko_switch,
	AIActionTable_FlowerPower_take_prize,
};

static u8 AIActionTable_FlowerPower_do_turn(void)
{
	TRACE_FN();
	AIMainTurnLogic();
	return 0;
}

static u8 AIActionTable_FlowerPower_start_duel(void)
{
	TRACE_FN();
	InitAIDuelVars();
	AIActionTable_FlowerPower_store_list_pointers();
	SetUpBossStartingHandAndDeck();
	if (!TrySetUpBossStartingPlayArea())
		return 0;
	AIPlayInitialBasicCards();
	return 0;
}

static u8 AIActionTable_FlowerPower_forced_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_FlowerPower_ko_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_FlowerPower_take_prize(void)
{
	TRACE_FN();
	AIPickPrizeCards();
	return 0;
}

/* (the lists are raw ROM blobs, see tools/rom_blobs.txt; they are reached through
 * the wAICardList* pointers)
 * .list_arena
 *	db ODDISH, EXEGGCUTE, BULBASAUR, $00
 * .list_bench
 *	db BULBASAUR, EXEGGCUTE, ODDISH, $00
 * .list_retreat
 *	ai_retreat GLOOM, -2
 *	ai_retreat VILEPLUME, -2
 *	ai_retreat BULBASAUR, -2
 *	ai_retreat IVYSAUR, -2
 *	db $00
 * .list_energy
 *	ai_energy BULBASAUR, 3, +0
 *	ai_energy IVYSAUR, 4, +0
 *	ai_energy VENUSAUR_LV67, 4, +0
 *	ai_energy ODDISH, 2, +0
 *	ai_energy GLOOM, 3, -1
 *	ai_energy VILEPLUME, 3, -1
 *	ai_energy EXEGGCUTE, 3, +0
 *	ai_energy EXEGGUTOR, 22, +0
 *	db $00
 * .list_prize
 *	db VENUSAUR_LV67, $00
 */

static void AIActionTable_FlowerPower_store_list_pointers(void)
{
	StoreListPointer(wAICardListAvoidPrize, "AIActionTable_FlowerPower.list_prize");
	StoreListPointer(wAICardListArenaPriority, "AIActionTable_FlowerPower.list_arena");
	StoreListPointer(wAICardListBenchPriority, "AIActionTable_FlowerPower.list_bench");
	StoreListPointer(wAICardListPlayFromHandPriority, "AIActionTable_FlowerPower.list_bench");
	/* missing store_list_pointer wAICardListRetreatBonus, .list_retreat */
	StoreListPointer(wAICardListEnergyBonus, "AIActionTable_FlowerPower.list_energy");
}

/* ---- decks/strange_psyshock.asm */

static u8 AIActionTable_StrangePsyshock_do_turn(void);
static u8 AIActionTable_StrangePsyshock_start_duel(void);
static u8 AIActionTable_StrangePsyshock_forced_switch(void);
static u8 AIActionTable_StrangePsyshock_ko_switch(void);
static u8 AIActionTable_StrangePsyshock_take_prize(void);
static void AIActionTable_StrangePsyshock_store_list_pointers(void);

const AIActionFunc AIActionTable_StrangePsyshock[AI_ACTION_TABLE_SIZE] = {
	AIActionTable_StrangePsyshock_do_turn, /* unused */
	AIActionTable_StrangePsyshock_do_turn,
	AIActionTable_StrangePsyshock_start_duel,
	AIActionTable_StrangePsyshock_forced_switch,
	AIActionTable_StrangePsyshock_ko_switch,
	AIActionTable_StrangePsyshock_take_prize,
};

static u8 AIActionTable_StrangePsyshock_do_turn(void)
{
	TRACE_FN();
	AIMainTurnLogic();
	return 0;
}

static u8 AIActionTable_StrangePsyshock_start_duel(void)
{
	TRACE_FN();
	InitAIDuelVars();
	AIActionTable_StrangePsyshock_store_list_pointers();
	SetUpBossStartingHandAndDeck();
	if (!TrySetUpBossStartingPlayArea())
		return 0;
	AIPlayInitialBasicCards();
	return 0;
}

static u8 AIActionTable_StrangePsyshock_forced_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_StrangePsyshock_ko_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_StrangePsyshock_take_prize(void)
{
	TRACE_FN();
	AIPickPrizeCards();
	return 0;
}

/* (the lists are raw ROM blobs, see tools/rom_blobs.txt; they are reached through
 * the wAICardList* pointers)
 * .list_arena
 *	db KANGASKHAN, CHANSEY, SNORLAX, MR_MIME, ABRA, $00
 * .list_bench
 *	db ABRA, MR_MIME, KANGASKHAN, SNORLAX, CHANSEY, $00
 * .list_retreat
 *	ai_retreat ABRA, -3
 *	ai_retreat SNORLAX, -3
 *	ai_retreat KANGASKHAN, -1
 *	ai_retreat CHANSEY, -1
 *	db $00
 * .list_energy
 *	ai_energy ABRA, 3, +1
 *	ai_energy KADABRA, 3, +0
 *	ai_energy ALAKAZAM, 3, +0
 *	ai_energy MR_MIME, 2, +0
 *	ai_energy CHANSEY, 2, -2
 *	ai_energy KANGASKHAN, 4, -2
 *	ai_energy SNORLAX, 0, -8
 *	db $00
 * .list_prize
 *	db GAMBLER, MR_MIME, ALAKAZAM, SWITCH, $00
 */

static void AIActionTable_StrangePsyshock_store_list_pointers(void)
{
	StoreListPointer(wAICardListAvoidPrize, "AIActionTable_StrangePsyshock.list_prize");
	StoreListPointer(wAICardListArenaPriority, "AIActionTable_StrangePsyshock.list_arena");
	StoreListPointer(wAICardListBenchPriority, "AIActionTable_StrangePsyshock.list_bench");
	StoreListPointer(wAICardListPlayFromHandPriority, "AIActionTable_StrangePsyshock.list_bench");
	/* missing store_list_pointer wAICardListRetreatBonus, .list_retreat */
	StoreListPointer(wAICardListEnergyBonus, "AIActionTable_StrangePsyshock.list_energy");
}

/* ---- decks/wonders_of_science.asm */

static u8 AIActionTable_WondersOfScience_do_turn(void);
static u8 AIActionTable_WondersOfScience_start_duel(void);
static u8 AIActionTable_WondersOfScience_forced_switch(void);
static u8 AIActionTable_WondersOfScience_ko_switch(void);
static u8 AIActionTable_WondersOfScience_take_prize(void);
static void AIActionTable_WondersOfScience_store_list_pointers(void);

const AIActionFunc AIActionTable_WondersOfScience[AI_ACTION_TABLE_SIZE] = {
	AIActionTable_WondersOfScience_do_turn, /* unused */
	AIActionTable_WondersOfScience_do_turn,
	AIActionTable_WondersOfScience_start_duel,
	AIActionTable_WondersOfScience_forced_switch,
	AIActionTable_WondersOfScience_ko_switch,
	AIActionTable_WondersOfScience_take_prize,
};

static u8 AIActionTable_WondersOfScience_do_turn(void)
{
	TRACE_FN();
	AIMainTurnLogic();
	return 0;
}

static u8 AIActionTable_WondersOfScience_start_duel(void)
{
	TRACE_FN();
	InitAIDuelVars();
	AIActionTable_WondersOfScience_store_list_pointers();
	SetUpBossStartingHandAndDeck();
	if (!TrySetUpBossStartingPlayArea())
		return 0;
	AIPlayInitialBasicCards();
	return 0;
}

static u8 AIActionTable_WondersOfScience_forced_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_WondersOfScience_ko_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_WondersOfScience_take_prize(void)
{
	TRACE_FN();
	AIPickPrizeCards();
	return 0;
}

/* (the lists are raw ROM blobs, see tools/rom_blobs.txt; they are reached through
 * the wAICardList* pointers)
 * .list_arena
 *	db MEWTWO_LV53, MEWTWO_ALT_LV60, MEWTWO_LV60, GRIMER, KOFFING, PORYGON, $00
 * .list_bench
 *	db GRIMER, KOFFING, MEWTWO_ALT_LV60, MEWTWO_LV60, MEWTWO_LV53, PORYGON, $00
 * .list_retreat
 *	db $00
 * .list_energy
 *	ai_energy GRIMER, 3, +0
 *	ai_energy MUK, 4, +0
 *	ai_energy KOFFING, 2, +0
 *	ai_energy WEEZING, 3, +0
 *	ai_energy MEWTWO_LV53, 2, -1
 *	ai_energy MEWTWO_ALT_LV60, 2, -1
 *	ai_energy MEWTWO_LV60, 2, -1
 *	ai_energy PORYGON, 2, -1
 *	db $00
 * .list_prize
 *	db MUK, $00
 */

static void AIActionTable_WondersOfScience_store_list_pointers(void)
{
	StoreListPointer(wAICardListAvoidPrize, "AIActionTable_WondersOfScience.list_prize");
	StoreListPointer(wAICardListArenaPriority, "AIActionTable_WondersOfScience.list_arena");
	StoreListPointer(wAICardListBenchPriority, "AIActionTable_WondersOfScience.list_bench");
	StoreListPointer(wAICardListPlayFromHandPriority, "AIActionTable_WondersOfScience.list_bench");
	/* missing store_list_pointer wAICardListRetreatBonus, .list_retreat */
	StoreListPointer(wAICardListEnergyBonus, "AIActionTable_WondersOfScience.list_energy");
}

/* ---- decks/fire_charge.asm */

static u8 AIActionTable_FireCharge_do_turn(void);
static u8 AIActionTable_FireCharge_start_duel(void);
static u8 AIActionTable_FireCharge_forced_switch(void);
static u8 AIActionTable_FireCharge_ko_switch(void);
static u8 AIActionTable_FireCharge_take_prize(void);
static void AIActionTable_FireCharge_store_list_pointers(void);

const AIActionFunc AIActionTable_FireCharge[AI_ACTION_TABLE_SIZE] = {
	AIActionTable_FireCharge_do_turn, /* unused */
	AIActionTable_FireCharge_do_turn,
	AIActionTable_FireCharge_start_duel,
	AIActionTable_FireCharge_forced_switch,
	AIActionTable_FireCharge_ko_switch,
	AIActionTable_FireCharge_take_prize,
};

static u8 AIActionTable_FireCharge_do_turn(void)
{
	TRACE_FN();
	AIMainTurnLogic();
	return 0;
}

static u8 AIActionTable_FireCharge_start_duel(void)
{
	TRACE_FN();
	InitAIDuelVars();
	AIActionTable_FireCharge_store_list_pointers();
	SetUpBossStartingHandAndDeck();
	if (!TrySetUpBossStartingPlayArea())
		return 0;
	AIPlayInitialBasicCards();
	return 0;
}

static u8 AIActionTable_FireCharge_forced_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_FireCharge_ko_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_FireCharge_take_prize(void)
{
	TRACE_FN();
	AIPickPrizeCards();
	return 0;
}

/* (the lists are raw ROM blobs, see tools/rom_blobs.txt; they are reached through
 * the wAICardList* pointers)
 * .list_arena
 *	db JIGGLYPUFF_LV14, CHANSEY, TAUROS, MAGMAR_LV24, JIGGLYPUFF_LV12, GROWLITHE, $00
 * .list_bench
 *	db JIGGLYPUFF_LV14, CHANSEY, GROWLITHE, MAGMAR_LV24, JIGGLYPUFF_LV12, TAUROS, $00
 * .list_retreat
 *	ai_retreat JIGGLYPUFF_LV12, -1
 *	ai_retreat CHANSEY, -1
 *	ai_retreat GROWLITHE, -1
 *	db $00
 * .list_energy
 *	ai_energy GROWLITHE, 3, +0
 *	ai_energy ARCANINE_LV45, 4, +0
 *	ai_energy MAGMAR_LV24, 3, +0
 *	ai_energy JIGGLYPUFF_LV12, 3, +0
 *	ai_energy JIGGLYPUFF_LV14, 2, +0
 *	ai_energy WIGGLYTUFF, 3, +0
 *	ai_energy CHANSEY, 4, +0
 *	ai_energy TAUROS, 3, +0
 *	db $00
 * .list_prize
 *	db GAMBLER, $00
 */

static void AIActionTable_FireCharge_store_list_pointers(void)
{
	StoreListPointer(wAICardListAvoidPrize, "AIActionTable_FireCharge.list_prize");
	StoreListPointer(wAICardListArenaPriority, "AIActionTable_FireCharge.list_arena");
	StoreListPointer(wAICardListBenchPriority, "AIActionTable_FireCharge.list_bench");
	StoreListPointer(wAICardListPlayFromHandPriority, "AIActionTable_FireCharge.list_bench");
	/* missing store_list_pointer wAICardListRetreatBonus, .list_retreat */
	StoreListPointer(wAICardListEnergyBonus, "AIActionTable_FireCharge.list_energy");
}

/* ---- decks/im_ronald.asm */

static u8 AIActionTable_ImRonald_do_turn(void);
static u8 AIActionTable_ImRonald_start_duel(void);
static u8 AIActionTable_ImRonald_forced_switch(void);
static u8 AIActionTable_ImRonald_ko_switch(void);
static u8 AIActionTable_ImRonald_take_prize(void);
static void AIActionTable_ImRonald_store_list_pointers(void);

const AIActionFunc AIActionTable_ImRonald[AI_ACTION_TABLE_SIZE] = {
	AIActionTable_ImRonald_do_turn, /* unused */
	AIActionTable_ImRonald_do_turn,
	AIActionTable_ImRonald_start_duel,
	AIActionTable_ImRonald_forced_switch,
	AIActionTable_ImRonald_ko_switch,
	AIActionTable_ImRonald_take_prize,
};

static u8 AIActionTable_ImRonald_do_turn(void)
{
	TRACE_FN();
	AIMainTurnLogic();
	return 0;
}

static u8 AIActionTable_ImRonald_start_duel(void)
{
	TRACE_FN();
	InitAIDuelVars();
	AIActionTable_ImRonald_store_list_pointers();
	SetUpBossStartingHandAndDeck();
	if (!TrySetUpBossStartingPlayArea())
		return 0;
	AIPlayInitialBasicCards();
	return 0;
}

static u8 AIActionTable_ImRonald_forced_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_ImRonald_ko_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_ImRonald_take_prize(void)
{
	TRACE_FN();
	AIPickPrizeCards();
	return 0;
}

/* (the lists are raw ROM blobs, see tools/rom_blobs.txt; they are reached through
 * the wAICardList* pointers)
 * .list_arena
 *	db LAPRAS, SEEL, CHARMANDER, CUBONE, SQUIRTLE, GROWLITHE, $00
 * .list_bench
 *	db CHARMANDER, SQUIRTLE, SEEL, CUBONE, GROWLITHE, LAPRAS, $00
 * .list_retreat
 *	db $00
 * .list_energy
 *	ai_energy CHARMANDER, 3, +0
 *	ai_energy CHARMELEON, 5, +0
 *	ai_energy GROWLITHE, 2, +0
 *	ai_energy ARCANINE_LV45, 4, +0
 *	ai_energy SQUIRTLE, 2, +0
 *	ai_energy WARTORTLE, 3, +0
 *	ai_energy SEEL, 3, +0
 *	ai_energy DEWGONG, 4, +0
 *	ai_energy LAPRAS, 3, +0
 *	ai_energy CUBONE, 3, +0
 *	ai_energy MAROWAK_LV26, 3, +0
 *	db $00
 * .list_prize
 *	db LAPRAS, $00
 */

static void AIActionTable_ImRonald_store_list_pointers(void)
{
	StoreListPointer(wAICardListAvoidPrize, "AIActionTable_ImRonald.list_prize");
	StoreListPointer(wAICardListArenaPriority, "AIActionTable_ImRonald.list_arena");
	StoreListPointer(wAICardListBenchPriority, "AIActionTable_ImRonald.list_bench");
	StoreListPointer(wAICardListPlayFromHandPriority, "AIActionTable_ImRonald.list_bench");
	/* missing store_list_pointer wAICardListRetreatBonus, .list_retreat */
	StoreListPointer(wAICardListEnergyBonus, "AIActionTable_ImRonald.list_energy");
}

/* ---- decks/powerful_ronald.asm */

static u8 AIActionTable_PowerfulRonald_do_turn(void);
static u8 AIActionTable_PowerfulRonald_start_duel(void);
static u8 AIActionTable_PowerfulRonald_forced_switch(void);
static u8 AIActionTable_PowerfulRonald_ko_switch(void);
static u8 AIActionTable_PowerfulRonald_take_prize(void);
static void AIActionTable_PowerfulRonald_store_list_pointers(void);

const AIActionFunc AIActionTable_PowerfulRonald[AI_ACTION_TABLE_SIZE] = {
	AIActionTable_PowerfulRonald_do_turn, /* unused */
	AIActionTable_PowerfulRonald_do_turn,
	AIActionTable_PowerfulRonald_start_duel,
	AIActionTable_PowerfulRonald_forced_switch,
	AIActionTable_PowerfulRonald_ko_switch,
	AIActionTable_PowerfulRonald_take_prize,
};

static u8 AIActionTable_PowerfulRonald_do_turn(void)
{
	TRACE_FN();
	AIMainTurnLogic();
	return 0;
}

static u8 AIActionTable_PowerfulRonald_start_duel(void)
{
	TRACE_FN();
	InitAIDuelVars();
	AIActionTable_PowerfulRonald_store_list_pointers();
	SetUpBossStartingHandAndDeck();
	if (!TrySetUpBossStartingPlayArea())
		return 0;
	AIPlayInitialBasicCards();
	return 0;
}

static u8 AIActionTable_PowerfulRonald_forced_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_PowerfulRonald_ko_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_PowerfulRonald_take_prize(void)
{
	TRACE_FN();
	AIPickPrizeCards();
	return 0;
}

/* (the lists are raw ROM blobs, see tools/rom_blobs.txt; they are reached through
 * the wAICardList* pointers)
 * .list_arena
 *	db KANGASKHAN, ELECTABUZZ_LV35, HITMONCHAN, MR_MIME, LICKITUNG, HITMONLEE, TAUROS, JYNX, MEWTWO_LV53, DODUO, $00
 * .list_bench
 *	db KANGASKHAN, HITMONLEE, HITMONCHAN, TAUROS, DODUO, JYNX, MEWTWO_LV53, ELECTABUZZ_LV35, MR_MIME, LICKITUNG, $00
 * .list_retreat
 *	ai_retreat KANGASKHAN, -1
 *	ai_retreat DODUO, -1
 *	ai_retreat DODRIO, -1
 *	db $00
 * .list_energy
 *	ai_energy ELECTABUZZ_LV35, 2, +1
 *	ai_energy HITMONLEE, 3, +1
 *	ai_energy HITMONCHAN, 3, +1
 *	ai_energy MR_MIME, 2, +0
 *	ai_energy JYNX, 3, +0
 *	ai_energy MEWTWO_LV53, 2, +0
 *	ai_energy DODUO, 3, -1
 *	ai_energy DODRIO, 3, -1
 *	ai_energy LICKITUNG, 2, +0
 *	ai_energy KANGASKHAN, 4, -1
 *	ai_energy TAUROS, 3, +0
 *	db $00
 * .list_prize
 *	db GAMBLER, ENERGY_REMOVAL, $00
 */

static void AIActionTable_PowerfulRonald_store_list_pointers(void)
{
	StoreListPointer(wAICardListAvoidPrize, "AIActionTable_PowerfulRonald.list_prize");
	StoreListPointer(wAICardListArenaPriority, "AIActionTable_PowerfulRonald.list_arena");
	StoreListPointer(wAICardListBenchPriority, "AIActionTable_PowerfulRonald.list_bench");
	StoreListPointer(wAICardListPlayFromHandPriority, "AIActionTable_PowerfulRonald.list_bench");
	/* missing store_list_pointer wAICardListRetreatBonus, .list_retreat */
	StoreListPointer(wAICardListEnergyBonus, "AIActionTable_PowerfulRonald.list_energy");
}

/* ---- decks/invincible_ronald.asm */

static u8 AIActionTable_InvincibleRonald_do_turn(void);
static u8 AIActionTable_InvincibleRonald_start_duel(void);
static u8 AIActionTable_InvincibleRonald_forced_switch(void);
static u8 AIActionTable_InvincibleRonald_ko_switch(void);
static u8 AIActionTable_InvincibleRonald_take_prize(void);
static void AIActionTable_InvincibleRonald_store_list_pointers(void);

const AIActionFunc AIActionTable_InvincibleRonald[AI_ACTION_TABLE_SIZE] = {
	AIActionTable_InvincibleRonald_do_turn, /* unused */
	AIActionTable_InvincibleRonald_do_turn,
	AIActionTable_InvincibleRonald_start_duel,
	AIActionTable_InvincibleRonald_forced_switch,
	AIActionTable_InvincibleRonald_ko_switch,
	AIActionTable_InvincibleRonald_take_prize,
};

static u8 AIActionTable_InvincibleRonald_do_turn(void)
{
	TRACE_FN();
	AIMainTurnLogic();
	return 0;
}

static u8 AIActionTable_InvincibleRonald_start_duel(void)
{
	TRACE_FN();
	InitAIDuelVars();
	AIActionTable_InvincibleRonald_store_list_pointers();
	SetUpBossStartingHandAndDeck();
	if (!TrySetUpBossStartingPlayArea())
		return 0;
	AIPlayInitialBasicCards();
	return 0;
}

static u8 AIActionTable_InvincibleRonald_forced_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_InvincibleRonald_ko_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_InvincibleRonald_take_prize(void)
{
	TRACE_FN();
	AIPickPrizeCards();
	return 0;
}

/* (the lists are raw ROM blobs, see tools/rom_blobs.txt; they are reached through
 * the wAICardList* pointers)
 * .list_arena
 *	db KANGASKHAN, MAGMAR_LV31, CHANSEY, GEODUDE, SCYTHER, GRIMER, $00
 * .list_bench
 *	db GRIMER, SCYTHER, GEODUDE, CHANSEY, MAGMAR_LV31, KANGASKHAN, $00
 * .list_retreat
 *	ai_retreat GRIMER, -1
 *	db $00
 * .list_energy
 *	ai_energy GRIMER, 1, -1
 *	ai_energy MUK, 3, -1
 *	ai_energy SCYTHER, 4, +1
 *	ai_energy MAGMAR_LV31, 2, +0
 *	ai_energy GEODUDE, 2, +0
 *	ai_energy GRAVELER, 3, +0
 *	ai_energy CHANSEY, 4, +0
 *	ai_energy KANGASKHAN, 4, -1
 *	db $00
 * .list_prize
 *	db GAMBLER, $00
 */

static void AIActionTable_InvincibleRonald_store_list_pointers(void)
{
	StoreListPointer(wAICardListAvoidPrize, "AIActionTable_InvincibleRonald.list_prize");
	StoreListPointer(wAICardListArenaPriority, "AIActionTable_InvincibleRonald.list_arena");
	StoreListPointer(wAICardListBenchPriority, "AIActionTable_InvincibleRonald.list_bench");
	StoreListPointer(wAICardListPlayFromHandPriority, "AIActionTable_InvincibleRonald.list_bench");
	/* missing store_list_pointer wAICardListRetreatBonus, .list_retreat */
	StoreListPointer(wAICardListEnergyBonus, "AIActionTable_InvincibleRonald.list_energy");
}

/* ---- decks/legendary_ronald.asm */

static u8 AIActionTable_LegendaryRonald_do_turn(void);
static u8 AIActionTable_LegendaryRonald_start_duel(void);
static u8 AIActionTable_LegendaryRonald_forced_switch(void);
static u8 AIActionTable_LegendaryRonald_ko_switch(void);
static u8 AIActionTable_LegendaryRonald_take_prize(void);
static void AIActionTable_LegendaryRonald_store_list_pointers(void);

const AIActionFunc AIActionTable_LegendaryRonald[AI_ACTION_TABLE_SIZE] = {
	AIActionTable_LegendaryRonald_do_turn, /* unused */
	AIActionTable_LegendaryRonald_do_turn,
	AIActionTable_LegendaryRonald_start_duel,
	AIActionTable_LegendaryRonald_forced_switch,
	AIActionTable_LegendaryRonald_ko_switch,
	AIActionTable_LegendaryRonald_take_prize,
};

static u8 AIActionTable_LegendaryRonald_do_turn(void)
{
	TRACE_FN();
	AIDoTurn_LegendaryRonald();
	return 0;
}

static u8 AIActionTable_LegendaryRonald_start_duel(void)
{
	TRACE_FN();
	InitAIDuelVars();
	AIActionTable_LegendaryRonald_store_list_pointers();
	SetUpBossStartingHandAndDeck();
	if (!TrySetUpBossStartingPlayArea())
		return 0;
	AIPlayInitialBasicCards();
	return 0;
}

static u8 AIActionTable_LegendaryRonald_forced_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_LegendaryRonald_ko_switch(void)
{
	TRACE_FN();
	u8 a;
	AIDecideBenchPokemonToSwitchTo(&a);
	return a;
}

static u8 AIActionTable_LegendaryRonald_take_prize(void)
{
	TRACE_FN();
	AIPickPrizeCards();
	return 0;
}

/* (the lists are raw ROM blobs, see tools/rom_blobs.txt; they are reached through
 * the wAICardList* pointers)
 * .list_arena
 *	db KANGASKHAN, DRATINI, EEVEE, ZAPDOS_LV68, ARTICUNO_LV37, MOLTRES_LV37, $00
 * .list_bench
 *	db KANGASKHAN, DRATINI, EEVEE, $00
 * .list_play_hand
 *	db MOLTRES_LV37, ZAPDOS_LV68, KANGASKHAN, DRATINI, EEVEE, ARTICUNO_LV37, $00
 * .list_retreat
 *	ai_retreat EEVEE, -2
 *	db $00
 * .list_energy
 *	ai_energy FLAREON_LV22, 3, +0
 *	ai_energy MOLTRES_LV37, 3, +0
 *	ai_energy VAPOREON_LV29, 3, +0
 *	ai_energy ARTICUNO_LV37, 0, -8
 *	ai_energy JOLTEON_LV24, 4, +0
 *	ai_energy ZAPDOS_LV68, 0, -8
 *	ai_energy KANGASKHAN, 4, -1
 *	ai_energy EEVEE, 3, +0
 *	ai_energy DRATINI, 3, +0
 *	ai_energy DRAGONAIR, 4, +0
 *	ai_energy DRAGONITE_LV41, 3, +0
 *	db $00
 * .list_prize
 *	db MOLTRES_LV37, ARTICUNO_LV37, ZAPDOS_LV68, DRAGONITE_LV41, GAMBLER, $00
 */

static void AIActionTable_LegendaryRonald_store_list_pointers(void)
{
	StoreListPointer(wAICardListAvoidPrize, "AIActionTable_LegendaryRonald.list_prize");
	StoreListPointer(wAICardListArenaPriority, "AIActionTable_LegendaryRonald.list_arena");
	StoreListPointer(wAICardListBenchPriority, "AIActionTable_LegendaryRonald.list_bench");
	StoreListPointer(wAICardListPlayFromHandPriority, "AIActionTable_LegendaryRonald.list_play_hand");
	/* missing store_list_pointer wAICardListRetreatBonus, .list_retreat */
	StoreListPointer(wAICardListEnergyBonus, "AIActionTable_LegendaryRonald.list_energy");
}

bool AIDoTurn_LegendaryRonald(void)
{
	TRACE_FN();
	u8 a;
	/* initialize variables */
	InitAITurnVars();
	/* process Trainer cards */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_01);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_02);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_04);

	/* check if AI can play MoltresLv37
	 * from hand and if so, play it. */
	if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) >= MAX_PLAY_AREA_POKEMON)
		goto skip_moltres_1; /* skip if bench is full */
	if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK) >= DECK_SIZE - 9)
		goto skip_moltres_1; /* skip if cards in deck <= 9 */
	if (CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK))
		goto skip_moltres_1; /* skip if Muk in play */
	if (!LookForCardIDInHandList_Bank5(MOLTRES_LV37, &a))
		goto skip_moltres_1; /* skip if no MoltresLv37 in hand */
	hTemp_ffa0 = a;
	AIMakeDecision(OPPACTION_PLAY_BASIC_PKMN);

skip_moltres_1:
	/* play Pokemon from hand */
	if (AIDecidePlayPokemonCard())
		return true; /* return if turn ended */
	/* process Trainer cards */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_05);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_07);
	AIProcessRetreat();
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_10);
	/* play Energy card if possible */
	if (wAlreadyPlayedEnergy == 0)
		AIProcessAndTryToPlayEnergy();
	/* .skip_attach_energy_1
	 * try playing Pokemon cards from hand again */
	if (AIDecidePlayPokemonCard())
		return true; /* return if turn ended */
	/* if used Professor Oak, process new hand
	 * if not, then proceed to attack. */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_15);
	if (!(wPreviousAIFlags & AI_FLAG_USED_PROFESSOR_OAK))
		goto try_attack;
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_01);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_02);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_04);

	/* check if AI can play MoltresLv37
	 * from hand and if so, play it. */
	if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) >= MAX_PLAY_AREA_POKEMON)
		goto skip_moltres_2; /* skip if bench is full */
	if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK) >= DECK_SIZE - 9)
		goto skip_moltres_2; /* skip if cards in deck <= 9 */
	if (CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK))
		goto skip_moltres_2; /* skip if Muk in play */
	if (!LookForCardIDInHandList_Bank5(MOLTRES_LV37, &a))
		goto skip_moltres_2; /* skip if no MoltresLv37 in hand */
	hTemp_ffa0 = a;
	AIMakeDecision(OPPACTION_PLAY_BASIC_PKMN);

skip_moltres_2:
	if (AIDecidePlayPokemonCard())
		return true; /* return if turn ended */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_05);
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_07);
	AIProcessRetreat();
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_10);
	if (wAlreadyPlayedEnergy == 0)
		AIProcessAndTryToPlayEnergy();
	/* .skip_attach_energy_2 */
	if (AIDecidePlayPokemonCard())
		return true; /* return if turn ended */
try_attack:
	/* attack if possible, if not,
	 * finish turn without attacking. */
	if (AIProcessAndTryToUseAttack())
		return true; /* return if turn ended */
	return AIMakeDecision(OPPACTION_FINISH_NO_ATTACK);
}
