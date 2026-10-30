/*
 * Port of src/engine/duel/ai/trainer_cards.asm (part B of the AI port, see notes/AI_BRIEF.md).
 *
 * Conventions specific to this file:
 *   - Every AIDecide_* (AITrainerDecideFunc) writes *a_out on every path. With carry it is
 *     exactly register a at `ret` (it becomes wAITrainerCardParameter). Without carry no
 *     caller reads it; when the asm's a there comes from a routine whose C version does not
 *     expose it, 0 is written.
 *   - `u8 a` is register a; other registers keep their asm names (b, c, d, e).
 *   - Asm locals that are `call`ed become static helpers <Function>_<label>; locals that are
 *     only jumped to stay as labels.
 */
#include "ai/ai.h"

/* INCLUDE "data/duel/ai_trainer_card_logic.asm": AITrainerCardLogic is in
 * ai/ai_trainer_card_logic.c (part A). */

void _AIProcessHandTrainerCards(u8 phase)
{
	TRACE_FN();
	const u8 *hl;
	const struct AITrainerCardLogicEntry *row;
	u8 a, d;

	wAITrainerCardPhase = phase;
	/* create hand list in wDuelTempList and wTempHandCardList. */
	CreateHandCardList(NULL);
	CopyListWithFFTerminatorFromHLToDE_Bank8(wDuelTempList, wTempHandCardList);
	hl = wTempHandCardList;

loop_hand:
	a = *hl++;
	wAITrainerCardToPlay = a;
	if (a == 0xff)
		return;

	/* push hl (hand pointer); ld hl, AITrainerCardLogic */
	d = wAITrainerCardPhase;
	row = AITrainerCardLogic;
loop_data:
	wCurrentAIFlags = 0;
	a = row->phase;
	if (a == 0xff)
		goto loop_hand; /* .pop_hl */

	/* compare input to first byte in data and continue if equal. */
	if (a != d)
		goto next_row; /* .inc_hl_by_5 */

	wAITrainerLogicCard = row->card_id;
	a = LoadCardDataToBuffer1_FromDeckIndex(wAITrainerCardToPlay);

	if (a == SWITCH) {
		if (wPreviousAIFlags & AI_FLAG_USED_SWITCH)
			goto next_row; /* .inc_hl_by_4 */
	}

	/* .skip_switch_check */
	/* compare hand card to second byte in data and continue if equal. */
	if (wAITrainerLogicCard != a)
		goto next_row; /* .inc_hl_by_4 */

	/* found Trainer card */
	hTempCardIndex_ff9f = wAITrainerCardToPlay;

	/* if Headache effects prevent playing card
	 * move on to the next item in list. */
	if (CheckCantUseTrainerDueToEffect())
		goto next_row; /* .next_in_data */

	LoadNonPokemonCardEffectCommands();
	if (TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_1))
		goto next_row; /* .next_in_data */

	/* AI can randomly choose not to play card. */
	if (AIChooseRandomlyNotToDoAction())
		goto next_row; /* .next_in_data */

	/* call routine to decide whether to play Trainer card */
	if (!row->decide(&a))
		goto next_row; /* .inc_hl_by_4 */

	/* routine returned carry, which means
	 * this card should be played. */
	wAITrainerCardParameter = a;

	/* show Play Trainer Card screen */
	hTempCardIndex_ff9f = wAITrainerCardToPlay;
	if (AIMakeDecision(OPPACTION_PLAY_TRAINER))
		goto next_row; /* .inc_hl_by_2 */

	/* execute the effects of the Trainer card */
	row->play();

	a = wPreviousAIFlags | wCurrentAIFlags;
	wPreviousAIFlags = a;
	/* pop hl (hand pointer) */
	if (!(a & AI_FLAG_MODIFIED_HAND))
		goto loop_hand;

	/* the hand was modified during the Trainer effect
	 * so it needs to be re-listed again and
	 * looped from the top. */
	CreateHandCardList(NULL);
	CopyListWithFFTerminatorFromHLToDE_Bank8(wDuelTempList, wTempHandCardList);
	hl = wTempHandCardList;
	/* clear the AI_FLAG_MODIFIED_HAND flag */
	wPreviousAIFlags &= (u8)~AI_FLAG_MODIFIED_HAND;
	goto loop_hand;

next_row:
	/* .inc_hl_by_5 / .inc_hl_by_4 / .inc_hl_by_2 / .next_in_data all end at the next row */
	row++;
	goto loop_data;
}

/* makes AI use Potion card. */
void AIPlay_Potion(void)
{
	TRACE_FN();
	hTempCardIndex_ff9f = wAITrainerCardToPlay;
	u8 a = wAITrainerCardParameter;
	hTemp_ffa0 = a;
	a = GetCardDamageAndMaxHP(a, NULL);
	if (a >= 20)
		a = 20;
	/* .play_card */
	hTempPlayAreaLocation_ffa1 = a;
	AIMakeDecision(OPPACTION_EXECUTE_TRAINER_EFFECTS);
}

/* if AI doesn't decide to retreat this card,
 * check if defending Pokémon can KO active card
 * next turn after using Potion.
 * if it cannot, return carry.
 * also take into account whether attack is high recoil. */
bool AIDecide_Potion_Phase07(u8 *a_out)
{
	TRACE_FN();
	u8 a, d, h, l;
	bool cy;

	if (AIDecideWhetherToRetreat())
		goto no_carry;
	if (AICheckIfAttackIsHighRecoil())
		goto no_carry;
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	if (!CheckIfDefendingPokemonCanKnockOut(&d))
		goto no_carry;

	h = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);
	a = GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL);
	if (a >= 20 + 1) /* if damage <= 20 */
		a = 20; /* amount of Potion HP healing */

	/* if damage done by defending Pokémon next turn will still
	 * KO this card after healing, return no carry. */
	/* .calculate_hp */
	l = a;
	a = (u8)(h + l);
	cy = a < d;
	a = (u8)(a - d);
	if (cy || a == 0) {
		*a_out = a;
		return false;
	}

	/* return carry. */
	*a_out = 0;
	return true;

no_carry:
	*a_out = 0;
	return false;
}

/* return carry if either of the attacks are usable
 * and have the BOOST_IF_TAKEN_DAMAGE effect. */
/* bug (original): CheckIfSelectedAttackIsUnusable checks the card at
 * [hTempPlayAreaLocation_ff9d] (the Arena card, set by the caller), not the
 * Play Area card in e that AIDecide_Potion_Phase10 is looping over. */
static bool AIDecide_Potion_Phase10_CheckIfHasAttackWithBoostIfTakenDamageFlag(void)
{
	wSelectedAttack = FIRST_ATTACK_OR_PKMN_POWER;
	if (!CheckIfSelectedAttackIsUnusable()) {
		if (CheckLoadedAttackFlag(ATTACK_FLAG3_ADDRESS | BOOST_IF_TAKEN_DAMAGE_F))
			return true;
	}
	/* .second_attack */
	wSelectedAttack = SECOND_ATTACK;
	if (CheckIfSelectedAttackIsUnusable())
		return false;
	if (CheckLoadedAttackFlag(ATTACK_FLAG3_ADDRESS | BOOST_IF_TAKEN_DAMAGE_F))
		return true;
	/* .false */
	return false;
}

/* finds a card in Play Area to use Potion on.
 * output:
 *	a = card to use Potion on;
 *	carry set if Potion should be used. */
bool AIDecide_Potion_Phase10(u8 *a_out)
{
	TRACE_FN();
	u8 a, d, e, h, l;
	bool cy;

	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	if (!CheckIfDefendingPokemonCanKnockOut(&d))
		goto start_from_active;
	/* can KO */
	h = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);
	a = GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL);
	if (a >= 20 + 1) /* if damage <= 20 */
		a = 20;
	/* return if using healing prevents KO. */
	/* .calculate_hp */
	l = a;
	a = (u8)(h + l);
	cy = a < d;
	a = (u8)(a - d);
	if (cy || a == 0)
		goto count_prizes;
	/* avoids Arena KO, don't play it on this Trainer Phase */
	*a_out = a;
	return false;

count_prizes:
	/* using Potion on active card does not prevent a KO.
	 * if player is at last prize, start loop with active card.
	 * otherwise start loop at first bench Pokémon. */
	SwapTurn();
	a = CountPrizes();
	SwapTurn();
	a--;
	if (a == 0)
		goto start_from_active;
	e = PLAY_AREA_BENCH_1;
	goto loop;

	/* find Play Area Pokémon with more than 10 damage.
	 * skip Pokémon if it has a BOOST_IF_TAKEN_DAMAGE attack. */
start_from_active:
	e = PLAY_AREA_ARENA;
loop:
	a = GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + e));
	if (a == 0xff) {
		*a_out = a;
		return false;
	}
	if (AIDecide_Potion_Phase10_CheckIfHasAttackWithBoostIfTakenDamageFlag())
		goto has_boost_damage;
	a = GetCardDamageAndMaxHP(e, NULL);
	if (a >= 20) /* if damage >= 20 */
		goto found;
has_boost_damage:
	e++;
	goto loop;

	/* a card was found, now to check if it's active or benched. */
found:
	if (e == 0)
		goto active_card;

	/* bench card
	 * if player is on last prize card then use Potion
	 * otherwise only 70% chance to use Potion */
	SwapTurn();
	a = CountPrizes();
	SwapTurn();
	a--;
	if (a == 0) {
		cy = false; /* or a */
	} else {
		/* 70% chance to use Potion */
		a = Random(10);
		cy = a < 3;
	}
	/* .no_random_chance */
	if (cy)
		goto no_carry;
	*a_out = e;
	return true;

	/* return carry for active card if not High Recoil. */
active_card:
	if (AICheckIfAttackIsHighRecoil())
		goto no_carry;
	*a_out = e;
	return true;

no_carry:
	*a_out = a;
	return false;
}

/* makes AI use Super Potion card. */
void AIPlay_SuperPotion(void)
{
	TRACE_FN();
	hTempCardIndex_ff9f = wAITrainerCardToPlay;
	hTempPlayAreaLocation_ffa1 = wAITrainerCardParameter;
	hTemp_ffa0 = AIPickEnergyCardToDiscard(wAITrainerCardParameter);
	u8 a = GetCardDamageAndMaxHP(wAITrainerCardParameter, NULL);
	if (a >= 40)
		a = 40;
	/* .play_card */
	hTempRetreatCostCards[0] = a;
	AIMakeDecision(OPPACTION_EXECUTE_TRAINER_EFFECTS);
}

/* returns carry if card has energies attached. */
static bool AIDecide_SuperPotion_Phase08_CheckIfHasEnergies(u8 e)
{
	GetPlayAreaCardAttachedEnergies(e);
	if (wTotalAttachedEnergies == 0)
		return false;
	return true;
}

/* if AI doesn't decide to retreat this card and card has
 * any energy cards attached, check if defending Pokémon can KO
 * active card next turn after using Super Potion.
 * if it cannot, return carry.
 * also take into account whether attack is high recoil. */
bool AIDecide_SuperPotion_Phase08(u8 *a_out)
{
	TRACE_FN();
	u8 a, d, e, h, l;
	bool cy;

	if (AIDecideWhetherToRetreat())
		goto no_carry;
	if (AICheckIfAttackIsHighRecoil())
		goto no_carry;
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	e = PLAY_AREA_ARENA;
	if (!AIDecide_SuperPotion_Phase08_CheckIfHasEnergies(e)) {
		*a_out = wTotalAttachedEnergies;
		return false;
	}
	if (!CheckIfDefendingPokemonCanKnockOut(&d))
		goto no_carry;

	h = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);
	e = PLAY_AREA_ARENA;
	a = GetCardDamageAndMaxHP(e, NULL);
	if (a >= 40 + 1) /* if damage < 40 */
		a = 40;
	/* .calculate_hp */
	l = a;
	a = (u8)(h + l);
	cy = a < d;
	a = (u8)(a - d);
	if (cy || a == 0) {
		*a_out = a;
		return false;
	}

	/* return carry */
	*a_out = e;
	return true;

no_carry:
	*a_out = 0;
	return false;
}

/* returns carry if card has energies attached. */
static bool AIDecide_SuperPotion_Phase11_CheckIfHasEnergies(u8 e)
{
	GetPlayAreaCardAttachedEnergies(e);
	if (wTotalAttachedEnergies == 0)
		return false;
	return true;
}

/* return carry if either of the attacks are usable
 * and have the BOOST_IF_TAKEN_DAMAGE effect. */
/* bug (original): checks the card at [hTempPlayAreaLocation_ff9d], not e
 * (on the first iteration the Arena card, then the previous e, which
 * .CheckIfDiscardingMakesAttacksUnusable stores there). */
static bool AIDecide_SuperPotion_Phase11_CheckIfHasAttackWithBoostIfTakenDamageFlag(void)
{
	wSelectedAttack = FIRST_ATTACK_OR_PKMN_POWER;
	if (!CheckIfSelectedAttackIsUnusable()) {
		if (CheckLoadedAttackFlag(ATTACK_FLAG3_ADDRESS | BOOST_IF_TAKEN_DAMAGE_F))
			return true;
	}
	/* .second_attack_1 */
	wSelectedAttack = SECOND_ATTACK;
	if (CheckIfSelectedAttackIsUnusable())
		return false;
	if (CheckLoadedAttackFlag(ATTACK_FLAG3_ADDRESS | BOOST_IF_TAKEN_DAMAGE_F))
		return true;
	/* .false_1 */
	return false;
}

/* returns carry if discarding energy card renders any attack unusable,
 * given that they have enough energy to be used before discarding. */
static bool AIDecide_SuperPotion_Phase11_CheckIfDiscardingMakesAttacksUnusable(u8 e)
{
	u8 b, c, e2;

	wSelectedAttack = FIRST_ATTACK_OR_PKMN_POWER;
	hTempPlayAreaLocation_ff9d = e;
	if (!CheckEnergyNeededForAttack(&b, &c, &e2)) {
		if (CheckEnergyNeededForAttackAfterDiscard(&b, &c, &e2))
			return true;
	}

	/* .second_attack_2 */
	wSelectedAttack = SECOND_ATTACK;
	hTempPlayAreaLocation_ff9d = e;
	if (CheckEnergyNeededForAttack(&b, &c, &e2))
		return false;
	if (CheckEnergyNeededForAttackAfterDiscard(&b, &c, &e2))
		return true;

	/* .false_2 */
	return false;
}

/* finds a card in Play Area to use Super Potion on.
 * output:
 *	a = card to use Super Potion on;
 *	carry set if Super Potion should be used. */
bool AIDecide_SuperPotion_Phase11(u8 *a_out)
{
	TRACE_FN();
	u8 a, d, e, h, l;
	bool cy;

	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	if (!CheckIfDefendingPokemonCanKnockOut(&d))
		goto start_from_active;
	/* can KO */
	h = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);
	a = GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL);
	if (a >= 40 + 1) /* if damage < 40 */
		a = 40;
	/* return if using healing prevents KO. */
	/* .calculate_hp */
	l = a;
	a = (u8)(h + l);
	cy = a < d;
	a = (u8)(a - d);
	if (cy || a == 0)
		goto count_prizes;
	*a_out = a;
	return false;

count_prizes:
	/* using Super Potion on active card does not prevent a KO.
	 * if player is at last prize, start loop with active card.
	 * otherwise start loop at first bench Pokémon. */
	SwapTurn();
	a = CountPrizes();
	SwapTurn();
	a--;
	if (a == 0)
		goto start_from_active;
	e = PLAY_AREA_BENCH_1;
	goto loop;

	/* find Play Area Pokémon with more than 30 damage.
	 * skip Pokémon if it doesn't have any energy attached,
	 * has a BOOST_IF_TAKEN_DAMAGE attack,
	 * or if discarding makes any of its attacks unusable. */
start_from_active:
	e = PLAY_AREA_ARENA;
loop:
	a = GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + e));
	if (a == 0xff) {
		*a_out = a;
		return false;
	}
	d = a;
	if (!AIDecide_SuperPotion_Phase11_CheckIfHasEnergies(e))
		goto next;
	if (AIDecide_SuperPotion_Phase11_CheckIfHasAttackWithBoostIfTakenDamageFlag())
		goto next;
	if (AIDecide_SuperPotion_Phase11_CheckIfDiscardingMakesAttacksUnusable(e))
		goto next;
	a = GetCardDamageAndMaxHP(e, NULL);
	if (a >= 40) /* if damage >= 40 */
		goto found;
next:
	e++;
	goto loop;

	/* a card was found, now to check if it's active or benched. */
found:
	if (e == 0)
		goto active_card;

	/* bench card
	 * if player is on last prize card then use Super Potion
	 * otherwise only 70% chance to use Super Potion */
	SwapTurn();
	a = CountPrizes();
	SwapTurn();
	a--;
	if (a == 0) {
		cy = false; /* or a */
	} else {
		/* 70% chance to use Super Potion */
		a = Random(10);
		cy = a < 3;
	}
	/* .check_random */
	if (cy)
		goto no_carry;
	*a_out = e;
	return true;

	/* return carry for active card if not Hgh Recoil. */
active_card:
	if (AICheckIfAttackIsHighRecoil())
		goto no_carry;
	*a_out = e;
	return true;

no_carry:
	*a_out = a;
	return false;
}

void AIPlay_Defender(void)
{
	TRACE_FN();
	hTempCardIndex_ff9f = wAITrainerCardToPlay;
	/* AI always attaches a Defender card to the Active Pokémon. */
	hTemp_ffa0 = PLAY_AREA_ARENA;
	AIMakeDecision(OPPACTION_EXECUTE_TRAINER_EFFECTS);
}

/* returns carry if using Defender can prevent a KO
 * by the defending Pokémon.
 * this takes into account both attacks and whether they're useable. */
bool AIDecide_Defender_Phase13(u8 *a_out)
{
	TRACE_FN();
	u8 a, d;
	bool cy;

	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	if (!CheckIfAnyAttackKnocksOutDefendingCard())
		goto cannot_ko;
	if (!CheckIfSelectedAttackIsUnusable())
		goto no_carry;
	if (LookForEnergyNeededForAttackInHand())
		goto no_carry;

cannot_ko:
	/* check if any of the defending Pokémon's attacks deal
	 * damage exactly equal to current HP, and if so,
	 * only continue if that attack is useable. */
	if (!CheckIfAnyDefendingPokemonAttackDealsSameDamageAsHP())
		goto no_carry;
	SwapTurn();
	cy = CheckIfSelectedAttackIsUnusable();
	SwapTurn();
	if (cy)
		goto no_carry;

	EstimateDamage_FromDefendingPokemon(wSelectedAttack);
	a = wDamage[0];
	wce06 = a;
	d = a;

	/* load in a the attack that was not selected,
	 * and check if it is useable. */
	wSelectedAttack = (u8)(SECOND_ATTACK - wSelectedAttack);
	SwapTurn();
	cy = CheckIfSelectedAttackIsUnusable();
	SwapTurn();
	if (cy)
		goto switch_back;

	/* the other attack is useable.
	 * compare its damage to the selected attack. */
	EstimateDamage_FromDefendingPokemon(wSelectedAttack);
	if (wDamage[0] >= d)
		goto subtract;

	/* in case the non-selected attack is useable
	 * and deals less damage than the selected attack,
	 * switch back to the other attack. */
switch_back:
	wSelectedAttack = (u8)(SECOND_ATTACK - wSelectedAttack);
	wDamage[0] = wce06;

	/* now the selected attack is the one that deals
	 * the most damage of the two (and is useable).
	 * if subtracting damage by using Defender
	 * still prevents a KO, return carry. */
subtract:
	d = (u8)(wDamage[0] - 20);
	a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);
	cy = a < d;
	a = (u8)(a - d);
	if (cy || a == 0) {
		*a_out = a;
		return false;
	}
	*a_out = a;
	return true;

no_carry:
	*a_out = 0;
	return false;
}

/* return carry if using Defender prevents Pokémon
 * from being knocked out by an attack with recoil. */
bool AIDecide_Defender_Phase14(u8 *a_out)
{
	TRACE_FN();
	u8 a, b, d;
	bool cy;

	if (CheckLoadedAttackFlag(ATTACK_FLAG1_ADDRESS | HIGH_RECOIL_F))
		goto recoil;
	if (CheckLoadedAttackFlag(ATTACK_FLAG1_ADDRESS | LOW_RECOIL_F))
		goto recoil;
	*a_out = 0;
	return false;

recoil:
	LoadCardDataToBuffer2_FromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	if (wSelectedAttack == 0)
		a = wLoadedCard2Atk1EffectParam; /* first attack */
	else
		a = wLoadedCard2Atk2EffectParam; /* .second_attack */

	/* double recoil damage if card is weak to its own color. */
	/* .check_weak */
	d = a;
	b = TranslateColorToWR(GetArenaCardColor());
	if (GetArenaCardWeakness() & b)
		d = (u8)(d << 1);

	/* subtract 30 from recoil damage if card resists its own color.
	 * if this yields a negative number, return no carry. */
	/* .check_resist */
	b = TranslateColorToWR(GetArenaCardColor());
	a = GetArenaCardResistance() & b;
	if (a) {
		a = d;
		cy = a < 30;
		a = (u8)(a - 30);
		if (cy)
			goto no_carry;
		d = a;
	}

	/* subtract damage prevented by Defender.
	 * if damage still knocks out card, return no carry.
	 * if damage does not knock out, return carry. */
	/* .subtract */
	a = d;
	if (a == 0)
		goto no_carry;
	a = (u8)(a - 20);
	d = a;
	a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);
	cy = a < d;
	a = (u8)(a - d);
	if (cy || a == 0)
		goto no_carry;
	*a_out = a;
	return true;

no_carry:
	*a_out = a;
	return false;
}

void AIPlay_PlusPower(void)
{
	TRACE_FN();
	wCurrentAIFlags |= AI_FLAG_USED_PLUSPOWER;
	wAIPlusPowerAttack = wAITrainerCardParameter;
	hTempCardIndex_ff9f = wAITrainerCardToPlay;
	AIMakeDecision(OPPACTION_EXECUTE_TRAINER_EFFECTS);
}

/* return carry if attack is useable and KOs
 * defending Pokémon with PlusPower boost. */
static bool AIDecide_PlusPower_Phase13_CheckAttackWithPluspower(void)
{
	u8 a, b, c;
	bool cy;

	if (CheckIfSelectedAttackIsUnusable())
		return false; /* .unusable */
	EstimateDamage_VersusDefendingCard(wSelectedAttack);
	a = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);
	b = a;
	cy = a < wDamage[0];
	a = (u8)(a - wDamage[0]);
	if (cy || a == 0)
		return false; /* .no_carry */
	a = (u8)(wDamage[0] + 10); /* add PlusPower boost */
	c = a;
	a = b;
	cy = a < c;
	a = (u8)(a - c);
	if (cy)
		return true; /* return carry if damage > HP left */
	if (a != 0)
		return false; /* does not KO */
	return true; /* KOs with PlusPower boost */
}

/* returns carry if PlusPower boost does
 * not exceed 30 damage when facing Mr. Mime. */
static bool AIDecide_PlusPower_Phase13_MrMimeDamageCheck(void)
{
	u8 a = (u8)(wDamage[0] + 10); /* add PlusPower boost */
	if (a < 30) /* no danger in preventing damage */
		return true;
	SwapTurn();
	u8 e = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	SwapTurn();
	if (e == MR_MIME)
		return false;
	/* damage is >= 30 but not Mr. Mime */
	return true;
}

/* returns carry if using a PlusPower can KO defending Pokémon
 * if active card cannot KO without the boost.
 * outputs in a the attack to use. */
bool AIDecide_PlusPower_Phase13(u8 *a_out)
{
	TRACE_FN();
	bool cy;

	/* this is mistakenly duplicated */
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;

	/* continue if no attack can knock out.
	 * if there's an attack that can, only continue
	 * if it's unusable and there's no card in hand
	 * to fulfill its energy cost. */
	if (!CheckIfAnyAttackKnocksOutDefendingCard())
		goto cannot_ko;
	if (!CheckIfSelectedAttackIsUnusable())
		goto no_carry;
	if (LookForEnergyNeededForAttackInHand())
		goto no_carry;

	/* cannot use an attack that knocks out. */
cannot_ko:
	/* get active Pokémon's info. */
	wTempTurnDuelistCardID = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));

	/* get defending Pokémon's info and check
	 * its No Damage or Effect substatus.
	 * if substatus is active, return. */
	SwapTurn();
	wTempNonTurnDuelistCardID = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	cy = HandleNoDamageOrEffectSubstatus();
	SwapTurn();
	if (cy)
		goto no_carry;

	/* check both attacks and decide which one
	 * can KO with PlusPower boost.
	 * if neither can KO, return no carry. */
	wSelectedAttack = FIRST_ATTACK_OR_PKMN_POWER;
	if (AIDecide_PlusPower_Phase13_CheckAttackWithPluspower())
		goto first_atk_kos_with_pluspower;
	wSelectedAttack = SECOND_ATTACK;
	if (AIDecide_PlusPower_Phase13_CheckAttackWithPluspower())
		goto second_atk_kos_with_pluspower;

no_carry:
	*a_out = 0;
	return false;

first_atk_kos_with_pluspower:
	if (!AIDecide_PlusPower_Phase13_MrMimeDamageCheck())
		goto no_carry;
	*a_out = FIRST_ATTACK_OR_PKMN_POWER;
	return true;

second_atk_kos_with_pluspower:
	if (!AIDecide_PlusPower_Phase13_MrMimeDamageCheck())
		goto no_carry;
	*a_out = SECOND_ATTACK;
	return true;
}

/* returns carry if PlusPower boost does
 * not exceed 30 damage when facing Mr. Mime.
 * *a = register a at ret: [wDamage] + 10 when < 30, else the defending card's ID. */
static bool AIDecide_PlusPower_Phase14_MrMimeDamageCheck(u8 *a)
{
	*a = (u8)(wDamage[0] + 10); /* add PlusPower boost */
	if (*a < 30) /* no danger in preventing damage */
		return true;
	SwapTurn();
	u8 e = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	SwapTurn();
	*a = e;
	if (e == MR_MIME)
		return false;
	/* damage is >= 30 but not Mr. Mime */
	return true;
}

/* return carry if attack is useable but cannot KO. */
static bool AIDecide_PlusPower_Phase14_CheckAttackDoesntKO(void)
{
	u8 a;

	if (CheckIfSelectedAttackIsUnusable())
		return false; /* .unusable */
	EstimateDamage_VersusDefendingCard(wSelectedAttack);
	a = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);
	if (a <= wDamage[0])
		return false; /* .no_carry */
	/* can't KO. */
	return true;
}

/* return carry 30% of the time if
 * attack is useable and minimum damage > 0. */
static bool AIDecide_PlusPower_Phase14_check_random(void)
{
	if (CheckIfSelectedAttackIsUnusable())
		return false; /* .unusable */
	EstimateDamage_VersusDefendingCard(wSelectedAttack);
	if (wAIMinDamage < 10)
		return false; /* .unusable */
	/* 30% chance to return carry */
	return Random(10) < 3;
}

/* returns carry 30% of the time if:
 * - selected attack is useable
 * - can't KO without PlusPower boost
 * - can damage Mr. Mime even with PlusPower boost
 * - has a minimum damage > 0.
 * outputs in a the attack to use. */
/* bug (original): a is not the attack index but what .MrMimeDamageCheck leaves in it
 * ([wDamage] + 10, or the defending card's ID), and AIPlay_PlusPower stores it in
 * wAIPlusPowerAttack. */
bool AIDecide_PlusPower_Phase14(u8 *a_out)
{
	TRACE_FN();
	u8 a;

	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	if (!AIDecide_PlusPower_Phase14_CheckAttackDoesntKO())
		goto no_carry;
	if (!AIDecide_PlusPower_Phase14_check_random())
		goto no_carry;
	if (!AIDecide_PlusPower_Phase14_MrMimeDamageCheck(&a))
		goto no_carry;
	*a_out = a;
	return true;

no_carry:
	*a_out = 0;
	return false;
}

void AIPlay_Switch(void)
{
	TRACE_FN();
	wCurrentAIFlags |= AI_FLAG_USED_SWITCH;
	hTempCardIndex_ff9f = wAITrainerCardToPlay;
	hTemp_ffa0 = wAITrainerCardParameter;
	AIMakeDecision(OPPACTION_EXECUTE_TRAINER_EFFECTS);
	wAIRetreatScore = 0;
}

/* returns carry if the active card has less energy cards
 * than the retreat cost and if AI can't play an energy
 * card from the hand to fulfill the cost */
bool AIDecide_Switch(u8 *a_out)
{
	TRACE_FN();
	u8 a, b;
	bool cy;

	/* check if AI can already play an energy card from hand to retreat */
	if (wAIPlayEnergyCardForRetreat == 0)
		goto check_cost_amount;

	/* can't play energy card from hand to retreat
	 * compare number of energy cards attached to retreat cost */
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	a = GetPlayAreaCardRetreatCost();
	b = CountNumberOfEnergyCardsAttached(PLAY_AREA_ARENA);
	cy = a < b;
	a = (u8)(a - b);
	/* jump if cards attached > retreat cost */
	if (cy)
		goto check_cost_amount;
	/* jump if retreat cost is 2 more energy cards
	 * than the number of cards attached */
	if (a >= 2)
		goto do_switch;

check_cost_amount:
	/* use Switch if retreat cost is 3 or more */
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	a = GetPlayAreaCardRetreatCost();
	if (a >= 3)
		goto do_switch;

	/* use Switch if not enough energies for retreat cost */
	b = a; /* push af / pop bc */
	a = CountNumberOfEnergyCardsAttached(PLAY_AREA_ARENA);
	if (a < b)
		goto do_switch;
	*a_out = a;
	return false;

do_switch: /* .switch */
	cy = AIDecideBenchPokemonToSwitchTo(&a);
	*a_out = a;
	return !cy; /* ccf */
}

void AIPlay_GustOfWind(void)
{
	TRACE_FN();
	wCurrentAIFlags |= AI_FLAG_USED_GUST_OF_WIND;
	hTempCardIndex_ff9f = wAITrainerCardToPlay;
	hTemp_ffa0 = wAITrainerCardParameter;
	AIMakeDecision(OPPACTION_EXECUTE_TRAINER_EFFECTS);
}

/* returns carry if any of the player's
 * benched cards is weak to color in b
 * and has a way to damage it
 * (*a = c, the bench location, on carry; $ff otherwise) */
static bool AIDecide_GustOfWind_FindBenchCardWithWeakness(u8 b, u8 *a)
{
	/* ld a, DUELVARS_BENCH / call GetNonTurnDuelistVariable: hl -> player's bench */
	const u8 *hl = NonTurnDuelistVars() + DUELVARS_BENCH;
	u8 c = PLAY_AREA_ARENA;
	u8 card;

loop_3:
	c++;
	card = *hl++;
	if (card == 0xff) {
		*a = card;
		return false; /* .no_carry */
	}
	SwapTurn();
	LoadCardDataToBuffer1_FromDeckIndex(card);
	SwapTurn();
	if (wLoadedCard1Weakness & b)
		goto check_can_damage;
	goto loop_3;

check_can_damage:
	if (!CheckIfCanDamageDefendingPokemon(PLAY_AREA_ARENA))
		goto loop_3;
	*a = c;
	return true;
}

/* returns carry if attack is Pokemon Power
 * or otherwise doesn't deal any damage */
/* bug (original): an attack with 0 base damage returns no carry (`or a / ret z`),
 * i.e. it is treated as dealing damage. */
static bool AIDecide_GustOfWind_CheckIfAttackDealsNoDamage(void)
{
	u8 e = wSelectedAttack;
	u8 d = GetTurnDuelistVariable(DUELVARS_ARENA_CARD);
	CopyAttackDataAndDamage_FromDeckIndex(d, e);

	/* skip if attack is a Power or has 0 damage */
	if (wLoadedAttackCategory == POKEMON_POWER)
		return true; /* .no_damage */
	if (wDamage[0] == 0)
		return false;

	/* check damage against defending card */
	EstimateDamage_VersusDefendingCard(wSelectedAttack);
	if (wAIMaxDamage != 0)
		return false;

	/* .no_damage */
	return true;
}

/* returns carry if neither attack can deal damage */
static bool AIDecide_GustOfWind_CheckIfNoAttackDealsDamage(void)
{
	wSelectedAttack = FIRST_ATTACK_OR_PKMN_POWER;
	if (!AIDecide_GustOfWind_CheckIfAttackDealsNoDamage())
		return false;
	/* .second_attack */
	wSelectedAttack = SECOND_ATTACK;
	if (!AIDecide_GustOfWind_CheckIfAttackDealsNoDamage())
		return false;
	/* .true */
	return true;
}

/* returns carry if attack KOs player card
 * in location stored in e */
static bool AIDecide_GustOfWind_CheckIfAttackKnocksOut(u8 attack, u8 e)
{
	u8 a;
	bool cy;

	EstimateDamage_VersusDefendingCard(attack);
	a = GetNonTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD_HP + e));
	cy = a < wDamage[0];
	a = (u8)(a - wDamage[0]);
	if (cy)
		return true;
	if (a != 0)
		return false;
	return true;
}

/* returns carry if any of arena card's attacks
 * KOs player card in location stored in e */
static bool AIDecide_GustOfWind_CheckIfAnyAttackKnocksOut(u8 e)
{
	if (AIDecide_GustOfWind_CheckIfAttackKnocksOut(FIRST_ATTACK_OR_PKMN_POWER, e))
		return true;
	/* fallthrough into .CheckIfAttackKnocksOut */
	return AIDecide_GustOfWind_CheckIfAttackKnocksOut(SECOND_ATTACK, e);
}

/* returns carry if there is a player's bench card that
 * the opponent's current active card can KO
 * (*a = its location on carry; $ff otherwise) */
static bool AIDecide_GustOfWind_FindBenchCardToKnockOut(u8 *a)
{
	/* ld a, DUELVARS_BENCH / call GetNonTurnDuelistVariable: hl -> player's bench */
	const u8 *hl = NonTurnDuelistVars() + DUELVARS_BENCH;
	u8 e = PLAY_AREA_BENCH_1;
	u8 b, card, saved_card, saved_hp;

loop_4:
	card = *hl++;
	if (card == 0xff) {
		*a = card;
		return false;
	}

	/* overwrite the player's active card and its HP
	 * with the current bench card that is being checked */
	b = card;
	saved_card = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD);
	NonTurnDuelistVars()[DUELVARS_ARENA_CARD] = b;
	b = GetNonTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD_HP + e));
	saved_hp = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);
	NonTurnDuelistVars()[DUELVARS_ARENA_CARD_HP] = b;

	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	if (!AIDecide_GustOfWind_CheckIfAnyAttackKnocksOut(e))
		goto next;
	if (!CheckIfSelectedAttackIsUnusable())
		goto found;
	if (LookForEnergyNeededForAttackInHand())
		goto found;

	/* the following two local routines can be condensed into one
	 * since they both revert the player's arena card */
next:
	NonTurnDuelistVars()[DUELVARS_ARENA_CARD_HP] = saved_hp;
	NonTurnDuelistVars()[DUELVARS_ARENA_CARD] = saved_card;
	e++;
	goto loop_4;

	/* revert player's arena card and set carry */
found:
	NonTurnDuelistVars()[DUELVARS_ARENA_CARD_HP] = saved_hp;
	NonTurnDuelistVars()[DUELVARS_ARENA_CARD] = saved_card;
	*a = e;
	return true;
}

/* returns carry if opponent's arena card can damage
 * this benched card if it were switched with
 * the player's arena card */
static bool AIDecide_GustOfWind_CheckIfCanDamageBenchedCard(u8 e)
{
	u8 b, saved_card, saved_hp;

	/* overwrite arena card data */
	b = GetNonTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + e));
	saved_card = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD);
	NonTurnDuelistVars()[DUELVARS_ARENA_CARD] = b;

	/* overwrite arena card HP */
	b = GetNonTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD_HP + e));
	saved_hp = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);
	NonTurnDuelistVars()[DUELVARS_ARENA_CARD_HP] = b;

	if (CheckIfCanDamageDefendingPokemon(PLAY_AREA_ARENA))
		goto can_damage;

	/* the following two local routines can be condensed into one
	 * since they both revert the player's arena card */

	/* can't damage */
	NonTurnDuelistVars()[DUELVARS_ARENA_CARD_HP] = saved_hp;
	NonTurnDuelistVars()[DUELVARS_ARENA_CARD] = saved_card;
	return false;

can_damage:
	NonTurnDuelistVars()[DUELVARS_ARENA_CARD_HP] = saved_hp;
	NonTurnDuelistVars()[DUELVARS_ARENA_CARD] = saved_card;
	return true;
}

bool AIDecide_GustOfWind(u8 *a_out)
{
	TRACE_FN();
	u8 a, b, d, e;

	a = (u8)(GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) - 1);
	if (a == 0) {
		*a_out = a;
		return false; /* no bench cards */
	}

	/* if used Gust Of Wind already,
	 * do not use it again. */
	a = wPreviousAIFlags & AI_FLAG_USED_GUST_OF_WIND;
	if (a) {
		*a_out = a;
		return false;
	}

	if (!CanArenaCardUseNonResidualAttack()) {
		*a_out = 0;
		return false; /* no non-residual attack can be used */
	}

	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	if (!CheckIfAnyAttackKnocksOutDefendingCard())
		goto check_id; /* if can't KO */
	a = 0;
	if (!CheckIfSelectedAttackIsUnusable())
		goto no_carry; /* if KO attack is useable */
	if (LookForEnergyNeededForAttackInHand())
		goto no_carry; /* if energy card is in hand */

check_id:
	/* skip if current active card is MEW_LV23 or MEWTWO_LV53 */
	a = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	if (a == MEW_LV23)
		goto no_carry;
	if (a == MEWTWO_LV53)
		goto no_carry;

	if (AIDecide_GustOfWind_FindBenchCardToKnockOut(&a)) {
		*a_out = a;
		return true;
	}

	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	if (AIDecide_GustOfWind_CheckIfNoAttackDealsDamage())
		goto check_bench_energy;

	/* skip if current arena card's color is
	 * the defending card's weakness */
	b = TranslateColorToWR(GetArenaCardColor());
	SwapTurn();
	a = GetArenaCardWeakness();
	SwapTurn();
	a &= b;
	if (a)
		goto no_carry;

	/* check weakness */
	if (!AIDecide_GustOfWind_FindBenchCardWithWeakness(b, &a)) {
		*a_out = a;
		return false; /* no bench card weak to arena card */
	}
	*a_out = a;
	return true; /* found bench card weak to arena card */

no_carry:
	*a_out = a;
	return false;

	/* being here means AI's arena card cannot damage player's arena card */

	/* first check if there is a card in player's bench that
	 * has no attached energy cards and that the AI can damage */
check_bench_energy:
	/* return carry if there's a bench card with weakness */
	/* bug (original): b is not loaded with the arena card's color here: it is what
	 * the last EstimateDamage_VersusDefendingCard (of .CheckIfAttackDealsNoDamage) left
	 * in it. When the Defending Pokemon is not protected by a No Damage or Effect
	 * substatus that is CARD_LOCATION_ARENA (or 0 after Invisible Wall /
	 * PREVENT_LESS_THAN_40 handling), which matches no weakness, so the bench is
	 * scanned (loading each card into wLoadedCard1) without finding one. When it is
	 * protected (Fly, Barrier, Agility, Neutralizing Shield), b is register a of the
	 * attack's EFFECTCMDTYPE_AI function (or older), which the port does not model. */
	b = CARD_LOCATION_ARENA;
	if (AIDecide_GustOfWind_FindBenchCardWithWeakness(b, &a)) {
		*a_out = a;
		return true;
	}

	d = GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	e = PLAY_AREA_ARENA;
	/* loop through bench and check attached energy cards */
loop_1:
	e++;
	d--;
	if (d == 0)
		goto check_bench_hp;
	SwapTurn();
	GetPlayAreaCardAttachedEnergies(e);
	SwapTurn();
	if (wTotalAttachedEnergies != 0)
		goto loop_1; /* skip if has energy attached */
	if (!AIDecide_GustOfWind_CheckIfCanDamageBenchedCard(e))
		goto loop_1;
	*a_out = e;
	return true;

check_bench_hp:
	wce06 = 0xff;
	wce08[0] = 0;
	e = 0;
	d = GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);

	/* find bench card with least amount of available HP */
loop_2:
	e++;
	d--;
	if (d == 0)
		goto check_found;
	b = GetNonTurnDuelistVariable((u8)(e + DUELVARS_ARENA_CARD_HP));
	a = wce06;
	b++;
	if (a < b)
		goto loop_2;
	if (!AIDecide_GustOfWind_CheckIfCanDamageBenchedCard(e))
		goto loop_2;
	b--;
	wce06 = b;
	wce08[0] = e;
	goto loop_2;

check_found:
	a = wce08[0];
	if (a == 0)
		goto no_carry;
	/* a card was found */

	/* .set_carry */
	*a_out = a;
	return true;
}

void AIPlay_Bill(void)
{
	TRACE_FN();
	hTempCardIndex_ff9f = wAITrainerCardToPlay;
	AIMakeDecision(OPPACTION_EXECUTE_TRAINER_EFFECTS);
}

/* return carry if cards in deck > 9 */
bool AIDecide_Bill(u8 *a_out)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK);
	*a_out = a;
	return a < DECK_SIZE - 9;
}

void AIPlay_EnergyRemoval(void)
{
	TRACE_FN();
	hTempCardIndex_ff9f = wAITrainerCardToPlay;
	hTemp_ffa0 = wAITrainerCardParameter;
	hTempPlayAreaLocation_ffa1 = wce1a;
	AIMakeDecision(OPPACTION_EXECUTE_TRAINER_EFFECTS);
}

/* returns carry if this card has any energy cards attached */
static bool AIDecide_EnergyRemoval_CheckIfCardHasEnergyAttached(u8 e)
{
	GetPlayAreaCardAttachedEnergies(e);
	if (wTotalAttachedEnergies == 0)
		return false;
	return true;
}

/* returns carry if this card does not
 * have enough energy for either of its attacks */
static bool AIDecide_EnergyRemoval_CheckIfNotEnoughEnergyToAttack(u8 e)
{
	u8 b, c, e2, a;

	wSelectedAttack = FIRST_ATTACK_OR_PKMN_POWER;
	hTempPlayAreaLocation_ff9d = e;
	if (!CheckEnergyNeededForAttack(&b, &c, &e2))
		goto enough_energy;

	wSelectedAttack = SECOND_ATTACK;
	hTempPlayAreaLocation_ff9d = e;
	if (!CheckEnergyNeededForAttack(&b, &c, &e2))
		goto check_surplus;

	/* neither attack has enough energy */
	return true;

enough_energy:
	return false;

	/* first attack doesn't have enough energy (or is just a Pokemon Power)
	 * but second attack has enough energy to be used
	 * check if there's surplus energy for attack and, if so, return carry */
check_surplus:
	return !CheckIfNoSurplusEnergyForAttack(&a); /* ccf */
}

/* stores in wce06 the highest damaging attack
 * for the card in play area location in e
 * and stores this card's location in wce08 */
static void AIDecide_EnergyRemoval_FindHighestDamagingAttack(u8 e)
{
	u8 a;

	hTempPlayAreaLocation_ff9d = e;
	EstimateDamage_VersusDefendingCard(FIRST_ATTACK_OR_PKMN_POWER);
	a = wDamage[0];
	if (a != 0 && wce06 < a) {
		wce06 = a; /* store this damage value */
		wce08[0] = e; /* store this location */
	}

	/* .second_attack */
	hTempPlayAreaLocation_ff9d = e;
	EstimateDamage_VersusDefendingCard(SECOND_ATTACK);
	a = wDamage[0];
	if (a != 0 && wce06 < a) {
		wce06 = a; /* store this damage value */
		wce08[0] = e; /* store this location */
	}
}

/* picks an energy card in the player's Play Area to remove */
bool AIDecide_EnergyRemoval(u8 *a_out)
{
	TRACE_FN();
	u8 a, e;

	/* check if the current active card can KO player's card
	 * if it's possible to KO, then do not consider the player's
	 * active card to remove its attached energy */
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	if (!CheckIfAnyAttackKnocksOutDefendingCard())
		goto cannot_ko;
	if (!CheckIfSelectedAttackIsUnusable())
		goto can_ko;
	if (!LookForEnergyNeededForAttackInHand())
		goto cannot_ko;

can_ko:
	/* start checking from the bench */
	wce0f[0] = PLAY_AREA_BENCH_1;
	goto check_bench_energy;
cannot_ko:
	/* start checking from the arena card */
	wce0f[0] = PLAY_AREA_ARENA;

	/* loop each card and check if it has enough energy to use any attack
	 * if it does, then proceed to pick an energy card to remove */
check_bench_energy:
	SwapTurn();
	e = wce0f[0];
loop_1:
	a = GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + e));
	if (a == 0xff)
		goto default_;

	if (!AIDecide_EnergyRemoval_CheckIfCardHasEnergyAttached(e))
		goto next_1;
	if (!AIDecide_EnergyRemoval_CheckIfNotEnoughEnergyToAttack(e))
		goto pick_energy; /* jump if enough energy to attack */
next_1:
	e++;
	goto loop_1;

pick_energy:
	/* a play area card was picked to remove energy
	 * store the picked energy card to remove in wce1a
	 * and set carry */
	a = e;
	wce1a = PickAttachedEnergyCardToRemove(a);
	SwapTurn();
	*a_out = a;
	return true;

	/* if no card in player's Play Area was found with enough energy
	 * to attack, just pick an energy card from player's active card
	 * (in case the AI cannot KO it this turn) */
default_: /* .default */
	if (wce0f[0] != 0)
		goto check_bench_damage; /* not active card */
	/* bug (original): e is the first empty Play Area slot here (where .loop_1
	 * stopped), not the active card, so this never finds energy attached. */
	if (AIDecide_EnergyRemoval_CheckIfCardHasEnergyAttached(e))
		goto pick_energy;

	/* lastly, check what attack on player's Play Area is highest damaging
	 * and pick an energy card attached to that Pokemon to remove */
check_bench_damage:
	wce06 = 0;
	wce08[0] = 0;

	e = PLAY_AREA_BENCH_1;
loop_2:
	a = GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + e));
	if (a == 0xff)
		goto found_damage;

	if (!AIDecide_EnergyRemoval_CheckIfCardHasEnergyAttached(e))
		goto next_2;
	AIDecide_EnergyRemoval_FindHighestDamagingAttack(e);
next_2:
	e++;
	goto loop_2;

found_damage:
	a = wce08[0];
	if (a == 0)
		goto no_carry; /* skip if none found */
	e = a;
	goto pick_energy;
no_carry:
	SwapTurn();
	*a_out = a;
	return false;
}

void AIPlay_SuperEnergyRemoval(void)
{
	TRACE_FN();
	hTempCardIndex_ff9f = wAITrainerCardToPlay;
	hTemp_ffa0 = wAITrainerCardParameter;
	hTempPlayAreaLocation_ffa1 = wce1a;
	hTempRetreatCostCards[0] = wce1b;
	hTempRetreatCostCards[1] = wce1c;
	hTempRetreatCostCards[2] = wce1d;
	hTempRetreatCostCards[3] = 0xff;
	AIMakeDecision(OPPACTION_EXECUTE_TRAINER_EFFECTS);
}

/* returns carry if an energy card other than Double Colorless
 * is found attached to the card in play area location e */
static bool AIDecide_SuperEnergyRemoval_LookForNonDoubleColorless(u8 e)
{
	const u8 *hl;
	u8 a;

	CreateArenaOrBenchEnergyCardList(e);
	hl = wDuelTempList;
	for (;;) { /* .loop_2 */
		a = *hl++;
		if (a == 0xff)
			return false;
		a = LoadCardDataToBuffer1_FromDeckIndex(a);
		/* any basic energy card
		 * will set carry flag here */
		if (a < DOUBLE_COLORLESS_ENERGY)
			return true;
	}
}

/* returns carry if the number of energy cards attached
 * is fewer than 2, or if all energy combined yields
 * fewer than 2 energy */
static bool AIDecide_SuperEnergyRemoval_CheckIfFewerThanTwoEnergyCards(u8 e)
{
	u8 a, b;
	const u8 *hl;

	GetPlayAreaCardAttachedEnergies(e);
	if (wTotalAttachedEnergies < 2)
		return true; /* return if fewer than 2 attached cards */

	/* count all energy attached
	 * i.e. colored energy card = 1
	 * and double colorless energy card = 2 */
	a = 0;
	hl = wAttachedEnergies;
	for (b = NUM_COLORED_TYPES; b; b--) /* .loop_5 */
		a = (u8)(a + *hl++);
	b = *hl; /* colorless */
	b >>= 1;
	a = (u8)(a + b);
	return a < 2;
}

/* returns carry if this card does not
 * have enough energy for either of its attacks */
static bool AIDecide_SuperEnergyRemoval_CheckIfNotEnoughEnergyToAttack(u8 e)
{
	u8 b, c, e2, a;

	wSelectedAttack = FIRST_ATTACK_OR_PKMN_POWER;
	hTempPlayAreaLocation_ff9d = e;
	if (!CheckEnergyNeededForAttack(&b, &c, &e2))
		goto enough_energy;

	wSelectedAttack = SECOND_ATTACK;
	hTempPlayAreaLocation_ff9d = e;
	if (!CheckEnergyNeededForAttack(&b, &c, &e2))
		goto check_surplus;

	/* neither attack has enough energy */
	return true;

enough_energy:
	return false;

	/* first attack doesn't have enough energy (or is just a Pokemon Power)
	 * but second attack has enough energy to be used
	 * check if there's surplus energy for attack and, if so,
	 * return carry if this surplus energy is at least 2 */
check_surplus:
	CheckIfNoSurplusEnergyForAttack(&a);
	if (a < 2)
		goto enough_energy;
	return true;
}

/* stores in wce06 the highest damaging attack
 * for the card in play area location in e
 * and stores this card's location in wce08 */
static void AIDecide_SuperEnergyRemoval_FindHighestDamagingAttack(u8 e)
{
	u8 a;

	hTempPlayAreaLocation_ff9d = e;
	EstimateDamage_VersusDefendingCard(FIRST_ATTACK_OR_PKMN_POWER);
	a = wDamage[0];
	if (a != 0 && wce06 < a) {
		wce06 = a; /* store this damage value */
		wce08[0] = e; /* store this location */
	}

	/* .second_attack */
	hTempPlayAreaLocation_ff9d = e;
	EstimateDamage_VersusDefendingCard(SECOND_ATTACK);
	a = wDamage[0];
	if (a != 0 && wce06 < a) {
		wce06 = a; /* store this damage value */
		wce08[0] = e; /* store this location */
	}
}

/* picks two energy cards in the player's Play Area to remove */
bool AIDecide_SuperEnergyRemoval(u8 *a_out)
{
	TRACE_FN();
	u8 a, b, e;

	/* bug (original): the search for an own basic energy starts at the first
	 * Bench Pokemon, the Arena card is never considered. */
	e = PLAY_AREA_BENCH_1;
loop_1:
	/* first find a Pokémon in Play Area card with a basic energy card
	 * to discard for card effect
	 * return immediately if none found */
	a = GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + e));
	if (a == 0xff) {
		/* .exit */
		*a_out = a;
		return false;
	}
	if (AIDecide_SuperEnergyRemoval_LookForNonDoubleColorless(e))
		goto not_double_colorless;
	e++;
	goto loop_1;

	/* card in Play Area location e was found with
	 * a basic energy card */
not_double_colorless:
	wce0f[0] = e;

	/* check if the current active card can KO player's card
	 * if it's possible to KO, then do not consider the player's
	 * active card to remove its attached energy */
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	if (!CheckIfAnyAttackKnocksOutDefendingCard())
		goto cannot_ko;
	if (!CheckIfSelectedAttackIsUnusable())
		goto can_ko;
	if (!LookForEnergyNeededForAttackInHand())
		goto cannot_ko;

can_ko:
	/* start checking from the bench */
	SwapTurn();
	e = PLAY_AREA_BENCH_1;
	goto loop_3;
cannot_ko:
	/* start checking from the arena card */
	SwapTurn();
	e = PLAY_AREA_ARENA;

	/* loop each card and check if it has enough energy to use any attack
	 * if it does, then proceed to pick energy cards to remove */
loop_3:
	a = GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + e));
	if (a == 0xff)
		goto no_carry;

	if (AIDecide_SuperEnergyRemoval_CheckIfFewerThanTwoEnergyCards(e))
		goto next_1;
	if (!AIDecide_SuperEnergyRemoval_CheckIfNotEnoughEnergyToAttack(e))
		goto found_card; /* jump if enough energy to attack */
next_1:
	e++;
	goto loop_3;

found_card:
	/* a play area card was picked to remove energy
	 * if this is not the Arena Card, then check
	 * entire bench to pick the highest damage */
	a = e;
	if (a != 0)
		goto check_bench_damage;

	/* store the picked energy card to remove in wce1a
	 * and set carry */
pick_energy:
	wce1b = a;
	/* b is only left unchanged when fewer than 2 energy cards are attached,
	 * which .CheckIfFewerThanTwoEnergyCards already excluded */
	b = 0;
	a = PickTwoAttachedEnergyCards(a, &b);
	wce1c = a;
	wce1d = b;
	SwapTurn();
	a = wce0f[0];
	wce1a = AIPickEnergyCardToDiscard(a);
	*a_out = a;
	return true;

	/* check what attack on player's Play Area is highest damaging
	 * and pick an energy card attached to that Pokemon to remove */
check_bench_damage:
	wce06 = 0;
	wce08[0] = 0;

	e = PLAY_AREA_BENCH_1;
loop_4:
	a = GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + e));
	if (a == 0xff)
		goto found_damage;

	if (AIDecide_SuperEnergyRemoval_CheckIfFewerThanTwoEnergyCards(e))
		goto next_2;
	if (AIDecide_SuperEnergyRemoval_CheckIfNotEnoughEnergyToAttack(e))
		goto next_2;
	AIDecide_SuperEnergyRemoval_FindHighestDamagingAttack(e);
next_2:
	e++;
	goto loop_4;

found_damage:
	a = wce08[0];
	if (a == 0)
		goto no_carry;
	goto pick_energy;
no_carry:
	SwapTurn();
	*a_out = a;
	return false;
}

void AIPlay_PokemonBreeder(void)
{
	TRACE_FN();
	hTempCardIndex_ff9f = wAITrainerCardToPlay;
	hTempPlayAreaLocation_ffa1 = wAITrainerCardParameter;
	hTemp_ffa0 = wce1a;
	AIMakeDecision(OPPACTION_EXECUTE_TRAINER_EFFECTS);
}

/* d = deck index of the stage 2 card, e = Play Area location */
static void AIDecide_PokemonBreeder_CalculateFitness(u8 d, u8 e)
{
	u8 a, b;

	a = GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD_HP + e));
	a = ConvertHPToDamageCounters_Bank8(a);
	a = (u8)((a << 4) | (a >> 4)); /* swap a */
	b = a;

	/* count number of energy cards attached and keep
	 * the lowest 4 bits (capped at 15) */
	GetPlayAreaCardAttachedEnergies(e);
	a = wTotalAttachedEnergies;
	if (a >= 16)
		a = 15;
	/* .not_maxed_out */
	a |= b;

	/* 4 high bits of a = HP counters Pokemon still has
	 * 4 low  bits of a = number of energy cards attached */

	/* store this score in wce08 + PLAY_AREA* */
	wce08[e] = a;

	/* store the deck index of stage 2 Pokemon in wce0f + PLAY_AREA* */
	wce0f[e] = d;

	/* increase wce06 by one */
	wce06++;
}

/* return carry if card is evolving to DragoniteLv41 and if
 * - the card that is evolving is not Arena card and
 *   number of damage counters in Play Area is under 8;
 * - the card that is evolving is Arena card and has under 5
 *   damage counters or has less than 3 energy cards attached.
 * (preserves every register) */
static bool AIDecide_PokemonBreeder_HandleDragoniteLv41Evolution(u8 d, u8 e)
{
	u8 a, b, c;

	/* check card ID */
	a = GetCardIDFromDeckIndex(d);
	if (a != DRAGONITE_LV41)
		return false;

	/* check card Play Area location */
	if (e == PLAY_AREA_ARENA)
		goto active_card_dragonite;

	/* the card that is evolving is not active card */
	b = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	c = 0;

	/* count damage counters in Play Area */
	do { /* .loop_play_area_damage */
		b--;
		a = GetCardDamageAndMaxHP(b, NULL);
		a = ConvertHPToDamageCounters_Bank8(a);
		c = (u8)(a + c);
	} while (b != 0);

	/* compare number of total damage counters
	 * with 7, if less or equal to that, set carry */
	if (7 < c)
		return false;
	return true;

active_card_dragonite:
	/* the card that is evolving is active card
	 * compare number of this card's damage counters
	 * with 5, if less than that, set carry */
	/* bug (original): GetCardDamageAndMaxHP returns the damage in HP, not in counters */
	a = GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL);
	if (a < 5)
		return true;

	/* compare number of this card's attached energy cards
	 * with 3, if less than that, set carry */
	a = CountNumberOfEnergyCardsAttached(PLAY_AREA_ARENA);
	if (a < 3)
		return true;
	return false;
}

bool AIDecide_PokemonBreeder(u8 *a_out)
{
	TRACE_FN();
	const u8 *hl;
	u8 a, b, c, d, e;

	if (IsPrehistoricPowerActive()) {
		a = 0;
		goto dont_use;
	}

	ClearMemory_Bank8(7, wce08);

	wce06 = 0;
	CreateHandCardList(NULL);
	hl = wDuelTempList;

loop_hand_1:
	a = *hl++;
	if (a == 0xff)
		goto not_found_in_hand;

	/* check if card in hand is any of the following
	 * stage 2 Pokemon cards */
	d = a;
	a = LoadCardDataToBuffer1_FromDeckIndex(a);
	if (a == VENUSAUR_LV64)
		goto found;
	if (a == VENUSAUR_LV67)
		goto found;
	if (a == BLASTOISE)
		goto found;
	if (a == VILEPLUME)
		goto found;
	if (a == ALAKAZAM)
		goto found;
	if (a != GENGAR)
		goto loop_hand_1;

found:
	c = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	e = PLAY_AREA_ARENA;

	/* check Play Area for card that can evolve into
	 * the picked stage 2 Pokemon */
	do { /* .loop_play_area_1 */
		if (!CheckIfCanEvolveInto_BasicToStage2(d, e))
			AIDecide_PokemonBreeder_CalculateFitness(d, e);
		e++;
	} while (--c);
	goto loop_hand_1;

not_found_in_hand:
	if (wce06 == 0)
		goto check_evolution_and_dragonite;

	/* an evolution has been found before */
	wce06 = 0;
	c = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	e = PLAY_AREA_ARENA;
	d = 0;

	/* find highest score in wce08 */
	do { /* .loop_score_1 */
		if (wce06 < wce08[e]) {
			/* store this score to wce06 */
			wce06 = wce08[e];
			/* store this Play Area location to wce07 */
			wce07 = e;
		}
		/* .not_higher */
		e++;
	} while (--c);

	/* store the deck index of the stage 2 card
	 * that has been decided in wce1a,
	 * return the Play Area location of card
	 * to evolve in a and return carry */
	e = wce07;
	wce1a = wce0f[e]; /* ld hl, wce0f / add hl, de (d = $00) */
	*a_out = wce07;
	return true;

check_evolution_and_dragonite:
	ClearMemory_Bank8(7, wce08);

	wce06 = 0;
	CreateHandCardList(NULL);
	hl = wDuelTempList;

loop_hand_2:
	a = *hl++;
	if (a == 0xff)
		goto check_evolution_found;

	d = a;
	c = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	e = PLAY_AREA_ARENA;

	do { /* .loop_play_area_2 */
		/* check if evolution is possible */
		if (!CheckIfCanEvolveInto_BasicToStage2(d, e)) {
			if (!AIDecide_PokemonBreeder_HandleDragoniteLv41Evolution(d, e))
				AIDecide_PokemonBreeder_CalculateFitness(d, e);
		}

		/* not possible to evolve or returned carry
		 * when handling DragoniteLv41 evolution */
		e++;
	} while (--c);
	goto loop_hand_2;

check_evolution_found:
	a = wce06;
	if (a != 0)
		goto evolution_was_found;
	/* no evolution was found before */
	*a_out = a;
	return false;

evolution_was_found:
	wce06 = 0;
	wce07 = 0xff;

	c = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	e = PLAY_AREA_ARENA;
	d = 0;

	/* find highest score in wce08 with at least
	 * 2 energy cards attached */
	do { /* .loop_score_2 */
		if (wce06 < wce08[e]) {
			/* take the lower 4 bits (total energy cards)
			 * and skip if less than 2 */
			b = wce08[e];
			if ((b & 0x0f) >= 2) {
				/* has at least 2 energy cards
				 * store the score in wce06 */
				wce06 = b;
				/* store this Play Area location to wce07 */
				wce07 = e;
			}
		}
		/* .next_score */
		e++;
	} while (--c);

	a = wce07;
	if (a == 0xff)
		goto dont_use;

	/* a card to evolve was found
	 * store the deck index of the stage 2 card
	 * that has been decided in wce1a,
	 * return the Play Area location of card
	 * to evolve in a and return carry */
	e = a;
	wce1a = wce0f[e]; /* ld hl, wce0f / add hl, de (d = $00) */
	*a_out = wce07;
	return true;

dont_use:
	*a_out = a;
	return false;
}

void AIPlay_ProfessorOak(void)
{
	TRACE_FN();
	wCurrentAIFlags |= AI_FLAG_USED_PROFESSOR_OAK | AI_FLAG_MODIFIED_HAND;
	hTempCardIndex_ff9f = wAITrainerCardToPlay;
	AIMakeDecision(OPPACTION_EXECUTE_TRAINER_EFFECTS);
}

/* return carry if there's a card in the hand that
 * can evolve the card in Play Area location in e.
 * sets wce08 to TRUE if any card is found that can
 * evolve regardless of card location. */
static bool AIDecide_ProfessorOak_LookForEvolution(u8 e)
{
	u8 d;

	wce08[0] = 0;
	d = 0; /* deck index */

	/* loop through all cards to check if there's
	 * a card that can evolve this Pokemon. */
loop_find_evolution:
	if (!CheckIfCanEvolveInto(d, e, NULL))
		goto can_evolve;
evolution_not_in_hand:
	d++;
	if (d != DECK_SIZE)
		goto loop_find_evolution;
	/* no evolutions found in hand */
	return false;

	/* a card was found that can evolve, set wce08 to TRUE
	 * and if the card is in the hand, return carry.
	 * otherwise resume looping through all cards */
can_evolve:
	wce08[0] = TRUE;
	if (GetTurnDuelistVariable((u8)(DUELVARS_CARD_LOCATIONS + d)) != CARD_LOCATION_HAND)
		goto evolution_not_in_hand;
	/* is in hand */
	return true;
}

/* sets carry if AI determines a score of playing
 * Professor Oak is over a certain threshold. */
bool AIDecide_ProfessorOak(u8 *a_out)
{
	TRACE_FN();
	const u8 *hl;
	u8 a, d, e;

	/* return if cards in deck <= 6 */
	hl = TurnDuelistVars() + DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK; /* GetTurnDuelistVariable leaves hl */
	a = *hl;
	if (a >= DECK_SIZE - 6)
		goto no_carry;

	a = wOpponentDeckID;
	if (a == LEGENDARY_ARTICUNO_DECK_ID)
		goto HandleLegendaryArticunoDeck;
	if (a == EXCAVATION_DECK_ID)
		goto HandleExcavationDeck;
	if (a == WONDERS_OF_SCIENCE_DECK_ID)
		goto HandleWondersOfScienceDeck;

	/* general deck AI logic for Professor Oak */
general_logic:
	/* do not play if cards in deck <= 14 */
	a = *hl;
	if (a >= DECK_SIZE - 14)
		goto no_carry;

	/* initialize score */
	wce06 = 30;

general_logic_got_initial_score:
	a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_IN_HAND);
	if (a >= 4)
		goto at_least_4_hand_cards;
	/* < 4 hand cards, encourage */
	wce06 = (u8)(wce06 + 50);
	goto check_energy_cards;
at_least_4_hand_cards:
	if (a < 9)
		goto check_energy_cards;
	/* >= 9 hand cards, discourage */
	wce06 = (u8)(wce06 - 30);

check_energy_cards:
	if (!CreateEnergyCardListFromHand())
		goto handle_blastoise;
	/* no energy cards, encourage */
	wce06 = (u8)(wce06 + 40);

handle_blastoise:
	/* is there Muk in play? */
	if (CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK))
		goto check_hand;
	/* if not, does AI have Blastoise lv52 in play? */
	if (!CountTurnDuelistPokemonWithActivePkmnPower(BLASTOISE))
		goto check_hand;
	/* if yes, does AI have any Water Energy cards in hand? */
	if (!LookForCardIDInHand(WATER_ENERGY, &a))
		goto check_hand;
	/* nope, then encourage */
	wce06 = (u8)(wce06 + 10);

check_hand:
	/* loops through hand cards and encourages
	 * if it finds any Basic Pokémon cards */
	CreateHandCardList(NULL);
	hl = wDuelTempList;
loop_hand:
	a = *hl++;
	if (a == 0xff)
		goto check_evolutions;

	/* skip card if it's an Energy card */
	LoadCardDataToBuffer1_FromDeckIndex(a);
	if (wLoadedCard1Type < TYPE_ENERGY)
		goto loop_hand; /* bug, should be jr nc */

	/* skip card if it's not Basic */
	if (wLoadedCard1Stage != BASIC)
		goto loop_hand;

	/* has a Basic card, encourage */
	wce06 = (u8)(wce06 + 10);

check_evolutions:
	wce0f[0] = 0;
	wce0f[1] = 0;

	d = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	e = PLAY_AREA_ARENA;
	do { /* .loop_play_area */
		if (AIDecide_ProfessorOak_LookForEvolution(e)) {
			/* there's a card in hand that can evolve it */
			wce0f[0] = TRUE;
		}
		/* .not_in_hand */
		/* check if a card that can evolve was found at all
		 * if yes, then set wce0f + 1 to TRUE */
		if (wce08[0] == TRUE)
			wce0f[1] = TRUE;
		/* .next_play_area */
		e++;
	} while (--d);

	/* if an evolution card was found... */
	if (wce0f[1] == 0)
		goto check_score;
	/* ...but that card is not in the hand... */
	if (wce0f[0] != 0)
		goto check_score;
	/* ...add to the score */
	wce06 = (u8)(wce06 + 10);

check_score:
	/* only use Professor Oak if score is >= 60 */
	a = wce06;
	*a_out = a;
	return a >= 60;

no_carry:
	*a_out = a;
	return false;

	/* handles Legendary Articuno Deck AI logic. */
HandleLegendaryArticunoDeck:
	a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	if (a >= 3)
		goto check_playable_cards;

	/* has less than 3 Pokemon in Play Area. */
	CreateHandCardList(NULL);
	d = a;
	e = PLAY_AREA_ARENA;

	/* if no cards in hand evolve cards in Play Area,
	 * returns carry. */
	do { /* .loop_play_area_articuno */
		u8 evolution;
		a = GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + e));
		if (CheckForEvolutionInList(a, &evolution))
			goto check_playable_cards;
		e++;
		a = d;
	} while (a != e);

set_carry_articuno:
	*a_out = a;
	return true;

	/* if there are more than 3 energy cards in hand,
	 * return no carry, otherwise check for playable cards. */
check_playable_cards:
	a = CountOppEnergyCardsInHand();
	if (a >= 4)
		goto no_carry_articuno;

	/* remove both Professor Oak cards from list
	 * before checking for playable cards */
	CreateHandCardList(NULL);
	{
		u8 removed;
		RemoveCardIDInList(PROFESSOR_OAK, wDuelTempList, &removed);
		RemoveCardIDInList(PROFESSOR_OAK, wDuelTempList, &removed);
	}
	hl = wDuelTempList;

	/* look in hand for cards that can be played.
	 * if a card that cannot be played is found, return no carry.
	 * otherwise return carry. */
loop_hand_articuno:
	a = *hl++;
	if (a == 0xff)
		goto set_carry_articuno;
	if (CheckIfCardCanBePlayed(a))
		goto loop_hand_articuno;

no_carry_articuno:
	*a_out = a;
	return false;

	/* handles Excavation deck AI logic.
	 * sets score depending on whether there's no
	 * Mysterious Fossil in play and in hand. */
HandleExcavationDeck:
	/* return no carry if cards in deck < 15 */
	a = *hl;
	if (a >= 46)
		goto no_carry;

	/* look for Mysterious Fossil */
	if (LookForCardIDInHandAndPlayArea(MYSTERIOUS_FOSSIL))
		goto found_mysterious_fossil;
	wce06 = 0x50;
	goto general_logic_got_initial_score;
found_mysterious_fossil:
	wce06 = 0x1e;
	goto general_logic_got_initial_score;

	/* handles Wonders of Science AI logic.
	 * if there's either Grimer or Muk in hand,
	 * do not play Professor Oak. */
HandleWondersOfScienceDeck:
	if (LookForCardIDInHandList_Bank8(GRIMER, &a))
		goto found_grimer_or_muk;
	if (LookForCardIDInHandList_Bank8(MUK, &a))
		goto found_grimer_or_muk;

	hl = TurnDuelistVars() + DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK;
	goto general_logic;

found_grimer_or_muk:
	*a_out = a;
	return false;
}

void AIPlay_EnergyRetrieval(void)
{
	TRACE_FN();
	wCurrentAIFlags |= AI_FLAG_MODIFIED_HAND;
	hTempCardIndex_ff9f = wAITrainerCardToPlay;
	hTemp_ffa0 = wAITrainerCardParameter;
	hTempPlayAreaLocation_ffa1 = wce1a;
	u8 a = wce1b;
	hTempRetreatCostCards[0] = a;
	if (a != 0xff)
		hTempRetreatCostCards[1] = 0xff;
	/* .asm_20e68 */
	AIMakeDecision(OPPACTION_EXECUTE_TRAINER_EFFECTS);
}

/* checks whether AI can play Energy Retrieval and
 * picks the energy cards from the discard pile,
 * and duplicate cards in hand to discard. */
bool AIDecide_EnergyRetrieval(u8 *a_out)
{
	TRACE_FN();
	u8 *hl;
	u8 a, b, d, e, count;

	/* return no carry if no cards in hand */
	if (!CreateEnergyCardListFromHand()) {
		a = wDuelTempList[0];
		goto no_carry;
	}

	/* handle Go Go Rain Dance deck
	 * return no carry if there's no Muk card in play and
	 * if there's no Blastoise card in Play Area
	 * if there's a Muk in play, continue as normal */
	if (wOpponentDeckID != GO_GO_RAIN_DANCE_DECK_ID)
		goto start;
	if (CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK))
		goto start;
	a = CountTurnDuelistPokemonWithActivePkmnPower(BLASTOISE);
	if (!a)
		goto no_carry;

start:
	/* find duplicate cards in hand */
	CreateHandCardList(NULL);
	if (FindDuplicateCards(wDuelTempList, &a))
		goto no_carry;

	wce06 = a;
	if (FindBasicEnergyCardsInLocation(CARD_LOCATION_DISCARD_PILE, &count)) {
		a = count;
		goto no_carry;
	}

	/* some basic energy cards were found in Discard Pile */
	wce1a = 0xff;
	wce1b = 0xff;
	wce1c = 0xff;

	d = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	e = PLAY_AREA_ARENA;

	/* first check if there are useful energy cards in the list
	 * and choose them for retrieval first */
	do { /* .loop_play_area */
		/* load this card's ID in wTempCardID
		 * and this card's Type in wTempCardType */
		a = GetCardIDFromDeckIndex(GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + e)));
		wTempCardID = a;
		LoadCardDataToBuffer1_FromCardID(a);
		wTempCardType = wLoadedCard1Type | TYPE_ENERGY;

		/* loop the energy cards in the Discard Pile
		 * and check if they are useful for this Pokemon */
		hl = wDuelTempList;
		for (;;) { /* .loop_energy_cards_1 */
			a = *hl++;
			if (a == 0xff)
				break; /* .next_play_area */

			b = a;
			if (!CheckIfEnergyIsUseful(a))
				continue;

			if (wce1a != 0xff)
				goto second_energy_1;

			/* check if there were already chosen cards,
			 * if this is the second chosen card, return carry */

			/* first energy card found */
			wce1a = b;
			RemoveCardFromList(hl);
			break; /* .next_play_area */
second_energy_1:
			wce1b = b;
			goto set_carry;
		}
		/* .next_play_area */
		e++;
	} while (--d);

	/* next, if there are still energy cards left to choose,
	 * loop through the energy cards again and select
	 * them in order. */
	hl = wDuelTempList;
	for (;;) { /* .loop_energy_cards_2 */
		a = *hl++;
		if (a == 0xff)
			break; /* .check_chosen */
		b = a;
		if (wce1a != 0xff)
			goto second_energy_2;
		wce1a = b;
		hl = RemoveCardFromList(hl);
	}

	/* will set carry if at least one has been chosen */
	/* .check_chosen */
	a = wce1a;
	if (a != 0xff)
		goto set_carry;
no_carry:
	*a_out = a;
	return false;

second_energy_2:
	wce1b = b;
	/* fallthrough to .set_carry */
set_carry:
	*a_out = wce06;
	return true;
}

/* remove an element from the list
 * and shortens it accordingly
 * input:
 *   hl = pointer to element after the one to remove */
u8 *RemoveCardFromList(u8 *hl)
{
	u8 *de = hl;
	hl--;
	u8 *start = hl; /* push hl */
	for (;;) { /* .loop_remove */
		u8 a = *de;
		*hl++ = a;
		if (a == 0xff)
			break;
		de++;
	}
	/* .done_remove */
	return start; /* pop hl */
}

/* finds duplicates in card list in hl.
 * if a duplicate of Pokemon cards are found, return in
 * a the deck index of the second one.
 * otherwise, if a duplicate of non-Pokemon cards are found
 * return in a the deck index of the second one.
 * if no duplicates found, return carry.
 * input:
 *   hl = list to look in
 * output:
 *   a = deck index of duplicate card */
bool FindDuplicateCards(u8 *hl, u8 *a_out)
{
	TRACE_FN();
	const u8 *outer = hl; /* push hl */
	const u8 *inner;
	u8 a, b, c, e;

	wce0f[0] = 0xff;
	wce0f[1] = 0xff;

loop_outer:
	/* get ID of current card */
	a = *outer++; /* pop hl / ld a, [hli] */
	if (a == 0xff)
		goto check_found;
	b = GetCardIDFromDeckIndex(a);
	inner = outer; /* push hl */

	/* loop the rest of the list to find
	 * another card with the same ID */
loop_inner:
	a = *inner++;
	if (a == 0xff)
		goto loop_outer;
	c = a;
	e = GetCardIDFromDeckIndex(a);
	if (e != b)
		goto loop_inner;

	/* found two cards with same ID */
	a = GetCardType(e);
	if (a < TYPE_ENERGY)
		goto not_energy;

	/* they are energy or trainer cards
	 * loads wce0f+1 with this card deck index */
	wce0f[1] = c;
	goto loop_outer;

not_energy:
	/* they are Pokemon cards
	 * loads wce0f with this card deck index */
	wce0f[0] = c;
	goto loop_outer;

check_found:
	a = wce0f[0];
	if (a != 0xff)
		goto no_carry;
	a = wce0f[1];
	if (a != 0xff)
		goto no_carry;

	/* only set carry if duplicate cards were not found */
	*a_out = a;
	return true;

no_carry:
	/* two cards with the same ID were found
	 * of either Pokemon or Non-Pokemon cards */
	*a_out = a;
	return false;
}

void AIPlay_SuperEnergyRetrieval(void)
{
	TRACE_FN();
	u8 a;

	wCurrentAIFlags |= AI_FLAG_MODIFIED_HAND;
	hTempCardIndex_ff9f = wAITrainerCardToPlay;
	hTemp_ffa0 = wAITrainerCardParameter;
	hTempPlayAreaLocation_ffa1 = wce1a;
	hTempRetreatCostCards[0] = wce1b;
	a = wce1c;
	hTempRetreatCostCards[1] = a;
	if (a == 0xff)
		goto asm_20fbb;
	a = wce1d;
	hTempRetreatCostCards[2] = a;
	if (a == 0xff)
		goto asm_20fbb;
	a = wce1e;
	hTempRetreatCostCards[3] = a;
	if (a == 0xff)
		goto asm_20fbb;
	hTempRetreatCostCards[4] = 0xff;
asm_20fbb:
	AIMakeDecision(OPPACTION_EXECUTE_TRAINER_EFFECTS);
}

bool AIDecide_SuperEnergyRetrieval(u8 *a_out)
{
	TRACE_FN();
	u8 *hl;
	u8 a, b, d, e, count;

	/* return no carry if no cards in hand */
	if (!CreateEnergyCardListFromHand()) {
		a = wDuelTempList[0];
		goto no_carry;
	}

	/* handle Go Go Rain Dance deck
	 * return no carry if there's no Muk card in play and
	 * if there's no Blastoise card in Play Area
	 * if there's a Muk in play, continue as normal */
	if (wOpponentDeckID != GO_GO_RAIN_DANCE_DECK_ID)
		goto start;
	if (CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK))
		goto start;
	a = CountTurnDuelistPokemonWithActivePkmnPower(BLASTOISE);
	if (!a)
		goto no_carry;

start:
	/* find duplicate cards in hand */
	CreateHandCardList(NULL);
	if (FindDuplicateCards(wDuelTempList, &a))
		goto no_carry;

	/* remove the duplicate card in hand
	 * and run the hand check again */
	wce06 = a;
	FindAndRemoveCardFromList(a, wDuelTempList);
	if (FindDuplicateCards(wDuelTempList, &a))
		goto no_carry;
	wce08[0] = a;

	if (FindBasicEnergyCardsInLocation(CARD_LOCATION_DISCARD_PILE, &count)) {
		a = count;
		goto no_carry;
	}

	/* some basic energy cards were found in Discard Pile */
	wce1b = 0xff;
	wce1c = 0xff;
	wce1d = 0xff;
	wce1e = 0xff;
	wce1f = 0xff;

	d = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	e = PLAY_AREA_ARENA;

	/* first check if there are useful energy cards in the list
	 * and choose them for retrieval first */
	do { /* .loop_play_area */
		/* load this card's ID in wTempCardID
		 * and this card's Type in wTempCardType */
		a = GetCardIDFromDeckIndex(GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + e)));
		wTempCardID = a;
		LoadCardDataToBuffer1_FromCardID(a);
		wTempCardType = wLoadedCard1Type | TYPE_ENERGY;

		/* loop the energy cards in the Discard Pile
		 * and check if they are useful for this Pokemon */
		hl = wDuelTempList;
		for (;;) { /* .loop_energy_cards_1 */
			a = *hl++;
			if (a == 0xff)
				break; /* .next_play_area */

			b = a;
			if (!CheckIfEnergyIsUseful(a))
				continue;

			/* first energy */
			if (wce1b == 0xff) {
				wce1b = b;
				RemoveCardFromList(hl);
				break; /* .next_play_area */
			}
			/* .second_energy_1 */
			if (wce1c == 0xff) {
				wce1c = b;
				RemoveCardFromList(hl);
				break; /* .next_play_area */
			}
			/* .third_energy_1 */
			if (wce1d == 0xff) {
				wce1d = b;
				RemoveCardFromList(hl);
				break; /* .next_play_area */
			}
			/* .fourth_energy_1 */
			wce1e = b;
			goto set_carry;
		}
		/* .next_play_area */
		e++;
	} while (--d);

	/* next, if there are still energy cards left to choose,
	 * loop through the energy cards again and select
	 * them in order. */
	hl = wDuelTempList;
	for (;;) { /* .loop_energy_cards_2 */
		a = *hl++;
		if (a == 0xff)
			break; /* .check_chosen */
		b = a;
		if (wce1b == 0xff) {
			/* first energy */
			wce1b = b;
			hl = RemoveCardFromList(hl);
			continue;
		}
		/* .second_energy_2 */
		if (wce1c == 0xff) {
			wce1c = b;
			hl = RemoveCardFromList(hl);
			continue;
		}
		/* .third_energy_2 */
		if (wce1d == 0xff) {
			wce1d = b;
			hl = RemoveCardFromList(hl);
			continue;
		}
		/* .fourth_energy_2 */
		wce1e = b;
		goto set_carry;
	}

	/* will set carry if at least one has been chosen */
	/* .check_chosen */
	a = wce1b;
	if (a != 0xff)
		goto set_carry;

no_carry:
	*a_out = a;
	return false;
set_carry:
	wce1a = wce08[0];
	*a_out = wce06;
	return true;
}

/* finds the card with deck index a in list hl,
 * and removes it from the list.
 * the card HAS to exist in the list, since this
 * routine does not check for the terminating byte $ff!
 * input:
 *   a  = card deck index to look
 *   hl = pointer to list of cards */
void FindAndRemoveCardFromList(u8 a, u8 *hl)
{
	u8 b = a;
	u8 *p = hl; /* push hl */
	while (*p++ != b) /* .loop */
		;
	RemoveCardFromList(p);
	/* pop hl */
}

void AIPlay_PokemonCenter(void)
{
	TRACE_FN();
	hTempCardIndex_ff9f = wAITrainerCardToPlay;
	AIMakeDecision(OPPACTION_EXECUTE_TRAINER_EFFECTS);
}

bool AIDecide_PokemonCenter(u8 *a_out)
{
	TRACE_FN();
	u8 a, b, d, e;
	u16 sum;

	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;

	/* return if active Pokemon can KO player's card. */
	a = 0;
	if (!CheckIfAnyAttackKnocksOutDefendingCard())
		goto start;
	if (!CheckIfSelectedAttackIsUnusable())
		goto no_carry;
	if (LookForEnergyNeededForAttackInHand())
		goto no_carry;

start:
	wce06 = 0;
	wce08[0] = 0;
	wce0f[0] = 0;

	d = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	e = PLAY_AREA_ARENA;

	do { /* .loop_play_area */
		LoadCardDataToBuffer1_FromDeckIndex(GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + e)));
		/* ld a, e ; useless instruction */

		/* get this Pokemon's current HP in number of counters
		 * and add it to the total. */
		b = ConvertHPToDamageCounters_Bank8(wLoadedCard1HP);
		wce06 = (u8)(wce06 + b);

		/* get this Pokemon's current damage counters
		 * and add it to the total. */
		b = ConvertHPToDamageCounters_Bank8(GetCardDamageAndMaxHP(e, NULL));
		wce08[0] = (u8)(wce08[0] + b);

		/* get this Pokemon's number of attached energy cards
		 * and add it to the total.
		 * if there's overflow, return no carry. */
		GetPlayAreaCardAttachedEnergies(e);
		b = wTotalAttachedEnergies;
		sum = (u16)(wce0f[0] + b);
		a = (u8)sum;
		if (sum > 0xff)
			goto no_carry;
		wce0f[0] = a;

		e++;
	} while (--d);

	/* if (number of damage counters / 2) < (total energy cards attached)
	 * return no carry. */
	a = (u8)(wce08[0] >> 1);
	if (a < wce0f[0])
		goto no_carry;

	/* if (number of HP counters * 6 / 10) >= (number of damage counters)
	 * return no carry. */
	a = (u8)CalculateWordTensDigit(HtimesL(6, wce06));
	if (a >= wce08[0])
		goto no_carry;

	*a_out = a;
	return true;

no_carry:
	*a_out = a;
	return false;
}

void AIPlay_ImposterProfessorOak(void)
{
	TRACE_FN();
	hTempCardIndex_ff9f = wAITrainerCardToPlay;
	AIMakeDecision(OPPACTION_EXECUTE_TRAINER_EFFECTS);
}

/* sets carry depending on player's number of cards
 * in deck in in hand. */
bool AIDecide_ImposterProfessorOak(u8 *a_out)
{
	TRACE_FN();
	u8 a;

	a = GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK);
	if (a < DECK_SIZE - 14)
		goto more_than_14_cards;

	/* if player has less than 14 cards in deck, only
	 * set carry if number of cards in their hands < 6 */
	a = GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_IN_HAND);
	if (a < 6)
		goto set_carry;
no_carry:
	*a_out = a;
	return false;

	/* if player has more than 14 cards in deck, only
	 * set carry if number of cards in their hands >= 9 */
more_than_14_cards:
	a = GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_IN_HAND);
	if (a < 9)
		goto no_carry;
set_carry:
	*a_out = a;
	return true;
}

void AIPlay_EnergySearch(void)
{
	TRACE_FN();
	hTempCardIndex_ff9f = wAITrainerCardToPlay;
	hTemp_ffa0 = wAITrainerCardParameter;
	AIMakeDecision(OPPACTION_EXECUTE_TRAINER_EFFECTS);
}

/* return carry if cards in wDuelTempList are not
 * useful to any of the Play Area Pokemon
 * (*a = the useful card without carry, the Play Area count with carry) */
static bool AIDecide_EnergySearch_CheckForUsefulEnergyCards(u8 *a_out)
{
	const u8 *hl;
	u8 a, b, d, e;

	d = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	e = PLAY_AREA_ARENA;

loop_play_area_1:
	/* store ID and type of card */
	a = GetCardIDFromDeckIndex(GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + e)));
	wTempCardID = a;
	LoadCardDataToBuffer1_FromCardID(a);
	wTempCardType = wLoadedCard1Type | TYPE_ENERGY;

	/* look in list for a useful energy,
	 * is any is found return no carry. */
	hl = wDuelTempList;
loop_energy_1:
	a = *hl++;
	if (a == 0xff)
		goto none_found;
	b = a;
	if (!CheckIfEnergyIsUseful(a))
		goto loop_energy_1;

	*a_out = b;
	return false;

none_found:
	e++;
	a = e;
	if (a != d)
		goto loop_play_area_1;

	*a_out = a;
	return true;
}

/* checks whether there are useful energies
 * only for Fire and Lightning type Pokemon cards
 * in Play Area. If none found, return carry. */
static bool AIDecide_EnergySearch_CheckUsefulFireOrLightningEnergy(u8 *a_out)
{
	const u8 *hl;
	u8 a, b, d, e;

	d = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	e = PLAY_AREA_ARENA;

loop_play_area_2:
	/* get card's ID and Type */
	a = GetCardIDFromDeckIndex(GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + e)));
	wTempCardID = a;
	LoadCardDataToBuffer1_FromCardID(a);
	a = wLoadedCard1Type | TYPE_ENERGY;

	/* only do check if the Pokemon's type
	 * is either Fire or Lightning */
	if (a == TYPE_ENERGY_FIRE)
		goto fire_or_lightning;
	if (a != TYPE_ENERGY_LIGHTNING)
		goto next_play_area;

	/* loop each energy card in list */
fire_or_lightning:
	wTempCardType = a;
	hl = wDuelTempList;
loop_energy_2:
	a = *hl++;
	if (a == 0xff)
		goto next_play_area;

	/* if this energy card is useful,
	 * return no carry. */
	b = a;
	if (!CheckIfEnergyIsUseful(a))
		goto loop_energy_2;

	*a_out = b;
	return false;

next_play_area:
	e++;
	a = e;
	if (a != d)
		goto loop_play_area_2;

	/* no card was found to be useful
	 * for Fire/Lightning type Pokemon card. */
	*a_out = a;
	return true;
}

/* checks whether there are useful energies
 * only for Grass type Pokemon cards
 * in Play Area. If none found, return carry. */
__attribute__((unused))
static bool AIDecide_EnergySearch_CheckUsefulGrassEnergy(u8 *a_out)
{
	/* unreferenced */
	const u8 *hl;
	u8 a, b, d, e;

	d = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	e = PLAY_AREA_ARENA;

loop_play_area_3:
	/* get card's ID and Type */
	a = GetCardIDFromDeckIndex(GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + e)));
	wTempCardID = a;
	LoadCardDataToBuffer1_FromCardID(a);
	a = wLoadedCard1Type | TYPE_ENERGY;

	/* only do check if the Pokemon's type is Grass */
	if (a != TYPE_ENERGY_GRASS)
		goto next_play_area_3;

	/* loop each energy card in list */
	wTempCardType = a;
	hl = wDuelTempList;
loop_energy_3:
	a = *hl++;
	if (a == 0xff)
		goto next_play_area_3;

	/* if this energy card is useful,
	 * return no carry. */
	b = a;
	if (!CheckIfEnergyIsUseful(a))
		goto loop_energy_3;

	*a_out = b;
	return false;

next_play_area_3:
	e++;
	a = e;
	if (a != d)
		goto loop_play_area_3;

	/* no card was found to be useful
	 * for Grass type Pokemon card. */
	*a_out = a;
	return true;
}

/* AI checks for playing Energy Search */
bool AIDecide_EnergySearch(u8 *a_out)
{
	TRACE_FN();
	u8 a, count;

	a = 0;
	if (CreateEnergyCardListFromHand())
		goto start;
	if (AIDecide_EnergySearch_CheckForUsefulEnergyCards(&a))
		goto start;

	/* there are energy cards in hand and at least
	 * one of them is useful to a card in Play Area */
no_carry:
	*a_out = a;
	return false;

start:
	a = wOpponentDeckID;
	if (a == HEATED_BATTLE_DECK_ID)
		goto heated_battle;
	if (a == WONDERS_OF_SCIENCE_DECK_ID)
		goto wonders_of_science;

	/* if no energy cards in deck, return no carry */
	if (FindBasicEnergyCardsInLocation(CARD_LOCATION_DECK, &count)) {
		a = count;
		goto no_carry;
	}

	/* if any of the energy cards in deck is useful
	 * return carry right away... */
	if (AIDecide_EnergySearch_CheckForUsefulEnergyCards(&a))
		goto no_useful;
	*a_out = a;
	return true;

	/* ...otherwise save the list in a before return carry. */
no_useful:
	*a_out = wDuelTempList[0];
	return true;

	/* Heated Battle deck only searches for Fire and Lightning
	 * if they are found to be useful to some card in Play Area */
heated_battle:
	if (FindBasicEnergyCardsInLocation(CARD_LOCATION_DECK, &count)) {
		a = count;
		goto no_carry;
	}
	if (AIDecide_EnergySearch_CheckUsefulFireOrLightningEnergy(&a))
		goto no_carry;
	*a_out = a;
	return true;

	/* this subroutine has a bug.
	 * it was supposed to use the .CheckUsefulGrassEnergy subroutine
	 * but uses .CheckUsefulFireOrLightningEnergy instead. */
wonders_of_science:
	if (FindBasicEnergyCardsInLocation(CARD_LOCATION_DECK, &count)) {
		a = count;
		goto no_carry;
	}
	if (AIDecide_EnergySearch_CheckUsefulFireOrLightningEnergy(&a))
		goto no_carry;
	*a_out = a;
	return true;
}

void AIPlay_Pokedex(void)
{
	TRACE_FN();
	hTempCardIndex_ff9f = wAITrainerCardToPlay;
	hTemp_ffa0 = wce1a;
	hTempPlayAreaLocation_ffa1 = wce1b;
	hTempRetreatCostCards[0] = wce1c;
	hTempRetreatCostCards[1] = wce1d;
	hTempRetreatCostCards[2] = wce1e;
	hTempRetreatCostCards[3] = 0xff;
	AIMakeDecision(OPPACTION_EXECUTE_TRAINER_EFFECTS);
}

bool AIDecide_Pokedex(u8 *a_out)
{
	TRACE_FN();
	u8 a;

	/* return if counter hasn't reached 6 yet */
	a = wAIPokedexCounter;
	if (a < 5 + 1)
		goto no_carry;

	/* return no carry if number of cards in deck <= 4 */
	a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK);
	if (a >= DECK_SIZE - 4)
		goto no_carry;

	/* has a 3 in 10 chance of actually playing card */
	a = Random(10);
	if (a < 3)
		goto pick_cards;

no_carry:
	*a_out = a;
	return false;

pick_cards:
	/* the following comparison is disregarded
	 * the Wonders of Science deck was probably intended
	 * to use PickPokedexCards_Unreferenced instead */
	(void)(wOpponentDeckID == WONDERS_OF_SCIENCE_DECK_ID);
	return PickPokedexCards(a_out); /* bug, should be jp nz */
}

static u8 PickPokedexCards_Unreferenced_GetCardType(u8 a)
{
	return GetCardType(GetCardIDFromDeckIndex(a));
}

/* picks order of the cards in deck from the effects of Pokedex.
 * prioritizes Pokemon cards, then Trainer cards, then energy cards.
 * stores the resulting order in wce1a. */
bool PickPokedexCards_Unreferenced(u8 *a_out)
{
	TRACE_FN();
	/* unreferenced */
	const u8 *hl;
	u8 *de;
	u8 a, b, c, e;

	wAIPokedexCounter = 0; /* reset counter */

	/* ld l, a (h = hWhoseTurn from GetTurnDuelistVariable) */
	hl = TurnDuelistVars()
		+ (u8)(GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK) + DUELVARS_DECK_CARDS);
	e = 0;

	/* run through 5 of the remaining cards in deck */
	for (b = 5; b; b--) { /* .next_card */
		a = *hl++;
		c = a;
		a = PickPokedexCards_Unreferenced_GetCardType(a);

		/* load this card's deck index and type in memory
		 * wce08 = card types
		 * wce0f = card deck indices */
		wce08[e] = a;
		wce0f[e] = c;

		e++;
	}

	/* terminate the wce08 list */
	wce08[5] = 0xff;

	de = &wce1a;

	/* find Pokemon */
	hl = wce08;
	c = 0xff; /* -1 */

	/* run through the stored cards
	 * and look for any Pokemon cards. */
	for (;;) { /* .loop_pokemon */
		c++;
		a = *hl++;
		if (a == 0xff)
			break;
		if (a >= TYPE_ENERGY)
			continue;
		/* found a Pokemon card
		 * store it in wce1a list */
		*de++ = wce0f[c];
	}

	/* run through the stored cards
	 * and look for any Trainer cards. */
	/* .find_trainers */
	hl = wce08;
	c = 0xff;

	for (;;) { /* .loop_trainers */
		c++;
		a = *hl++;
		if (a == 0xff)
			break;
		if (a != TYPE_TRAINER)
			continue;
		/* found a Trainer card
		 * store it in wce1a list */
		*de++ = wce0f[c];
	}

	/* .find_energy */
	hl = wce08;
	c = 0xff;

	/* run through the stored cards
	 * and look for any energy cards. */
	for (;;) { /* .loop_energy */
		c++;
		a = *hl++;
		if (a == 0xff)
			break;
		a &= TYPE_ENERGY;
		if (a == 0)
			continue;
		/* found an energy card
		 * store it in wce1a list */
		*de++ = wce0f[c];
	}

	/* .done */
	*a_out = a;
	return true;
}

static u8 PickPokedexCards_GetCardType(u8 a)
{
	return GetCardType(GetCardIDFromDeckIndex(a));
}

/* picks order of the cards in deck from the effects of Pokedex.
 * prioritizes energy cards, then Pokemon cards, then Trainer cards.
 * stores the resulting order in wce1a. */
bool PickPokedexCards(u8 *a_out)
{
	TRACE_FN();
	const u8 *hl;
	u8 *de;
	u8 a, b, c, e;

	/* reset counter */
	wAIPokedexCounter = 0;

	/* ld l, a (h = hWhoseTurn from GetTurnDuelistVariable) */
	hl = TurnDuelistVars()
		+ (u8)(GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK) + DUELVARS_DECK_CARDS);
	e = 0;

	/* run through 5 of the remaining cards in deck */
	for (b = 5; b; b--) { /* .next_card */
		a = *hl++;
		c = a;
		a = PickPokedexCards_GetCardType(a);

		/* load this card's deck index and type in memory
		 * wce08 = card types
		 * wce0f = card deck indices */
		wce08[e] = a;
		wce0f[e] = c;

		e++;
	}

	/* terminate the wce08 list */
	wce08[5] = 0xff;

	/* find energy */
	de = &wce1a;
	hl = wce08;
	c = 0xff; /* -1 */

	/* run through the stored cards
	 * and look for any energy cards. */
	for (;;) { /* .loop_energy */
		c++;
		a = *hl++;
		if (a == 0xff)
			break;
		a &= TYPE_ENERGY;
		if (a == 0)
			continue;
		/* found an energy card
		 * store it in wce1a list */
		*de++ = wce0f[c];
	}

	/* .find_pokemon */
	hl = wce08;
	c = 0xff;

	/* run through the stored cards
	 * and look for any Pokemon cards. */
	for (;;) { /* .loop_pokemon */
		c++;
		a = *hl++;
		if (a == 0xff)
			break;
		if (a >= TYPE_ENERGY)
			continue;
		/* found a Pokemon card
		 * store it in wce1a list */
		*de++ = wce0f[c];
	}

	/* .find_trainers */
	hl = wce08;
	c = 0xff;

	/* run through the stored cards
	 * and look for any Trainer cards. */
	for (;;) { /* .loop_trainers */
		c++;
		a = *hl++;
		if (a == 0xff)
			break;
		if (a != TYPE_TRAINER)
			continue;
		/* found a Trainer card
		 * store it in wce1a list */
		*de++ = wce0f[c];
	}

	/* .done */
	*a_out = a;
	return true;
}

void AIPlay_FullHeal(void)
{
	TRACE_FN();
	hTempCardIndex_ff9f = wAITrainerCardToPlay;
	AIMakeDecision(OPPACTION_EXECUTE_TRAINER_EFFECTS);
}

/* register a that AIDecideWhetherToRetreat (retreat.asm, part C) leaves when it
 * returns no carry, which AIDecide_FullHeal returns with carry (so it reaches
 * wAITrainerCardParameter): [wConfusionRetreatCheckWasUnsuccessful] if nonzero
 * (`jp nz, .no_carry`), $ff if the Arena card is Mysterious Fossil or Clefairy Doll
 * (.loop_ko_3 reached the end of the bench), else [wAIScore] (`cp 131`, < 131).
 * Only reads state; must be called right after it returned no carry. */
static u8 AIDecide_FullHeal_RetreatNoCarryA(void)
{
	if (wConfusionRetreatCheckWasUnsuccessful)
		return wConfusionRetreatCheckWasUnsuccessful;
	u8 id = _GetCardIDFromDeckIndex(TurnDuelistVars()[DUELVARS_ARENA_CARD]);
	if (id == MYSTERIOUS_FOSSIL || id == CLEFAIRY_DOLL)
		return 0xff;
	return wAIScore;
}

bool AIDecide_FullHeal(u8 *a_out)
{
	TRACE_FN();
	u8 a, scoop_up;

	a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS);

	/* skip if no status on arena card */
	if (a == NO_STATUS)
		goto no_carry;

	a &= CNF_SLP_PRZ;
	if (a == PARALYZED)
		goto paralyzed;
	if (a == ASLEEP)
		goto asleep;
	if (a == CONFUSED)
		goto confused;
	/* if either PSN or DBLPSN, fallthrough */

set_carry:
	*a_out = a;
	return true;

asleep:
	/* set carry if any of the following
	 * cards are in own Play Area.
	 * bug, should be checking player's Play Area instead */
	if (LookForCardIDInPlayArea_Bank8(GASTLY_LV8, PLAY_AREA_ARENA, &a))
		goto set_carry;
	if (LookForCardIDInPlayArea_Bank8(GASTLY_LV17, PLAY_AREA_ARENA, &a))
		goto set_carry;
	if (LookForCardIDInPlayArea_Bank8(HAUNTER_LV22, PLAY_AREA_ARENA, &a))
		goto set_carry;

	/* otherwise fallthrough */

paralyzed:
	/* if Scoop Up is in hand and decided to be played, skip. */
	if (!LookForCardIDInHandList_Bank8(SCOOP_UP, &a))
		goto no_scoop_up_prz;
	if (AIDecide_ScoopUp(&scoop_up)) {
		a = scoop_up;
		goto no_carry;
	}

no_scoop_up_prz:
	/* return no carry if Arena card
	 * cannot damage the defending Pokémon */

	/* this is a bug, since CheckIfCanDamageDefendingPokemon
	 * also takes into account whether card is paralyzed */
	a = 0;
	if (!CheckIfCanDamageDefendingPokemon(PLAY_AREA_ARENA))
		goto no_carry;

	/* if it can play an energy card to retreat, set carry. */
	a = wAIPlayEnergyCardForRetreat;
	if (a)
		goto set_carry;

	/* if not, check whether it's a card it would rather retreat,
	 * and if it isn't, set carry. */
	if (!AIDecideWhetherToRetreat()) {
		a = AIDecide_FullHeal_RetreatNoCarryA();
		goto set_carry;
	}
	a = 0;

no_carry:
	*a_out = a;
	return false;

confused:
	/* if Scoop Up is in hand and decided to be played, skip. */
	if (!LookForCardIDInHandList_Bank8(SCOOP_UP, &a))
		goto no_scoop_up_cnf;
	if (AIDecide_ScoopUp(&scoop_up)) {
		a = scoop_up;
		goto no_carry;
	}

no_scoop_up_cnf:
	/* if card can damage defending Pokemon... */
	a = 0;
	if (!CheckIfCanDamageDefendingPokemon(PLAY_AREA_ARENA))
		goto no_carry;
	/* ...and can play an energy card to retreat, set carry. */
	a = wAIPlayEnergyCardForRetreat;
	if (a)
		goto set_carry;
	/* if not, return no carry. */
	goto no_carry;
}

void AIPlay_MrFuji(void)
{
	TRACE_FN();
	hTempCardIndex_ff9f = wAITrainerCardToPlay;
	hTemp_ffa0 = wAITrainerCardParameter;
	AIMakeDecision(OPPACTION_EXECUTE_TRAINER_EFFECTS);
}

/* AI logic for playing Mr Fuji */
bool AIDecide_MrFuji(u8 *a_out)
{
	TRACE_FN();
	u8 a, b, d, e;

	wce06 = 0xff;
	wce08[0] = 0xff;

	/* if just one Pokemon in Play Area, skip. */
	a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	if (a == 1) {
		*a_out = a;
		return false;
	}

	a--;
	d = a;
	e = PLAY_AREA_BENCH_1;

	/* find a Pokemon in the bench that has damage counters. */
loop_bench:
	LoadCardDataToBuffer1_FromDeckIndex(GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + e)));

	b = wLoadedCard1HP;

	/* skip if zero damage counters */
	a = ConvertHPToDamageCounters_Bank8(GetCardDamageAndMaxHP(e, NULL));
	if (a == 0)
		goto next;

	/* a = damage counters
	 * b = max HP */
	a = CalculateBDividedByA_Bank8(a, b);
	if (a >= 20)
		goto next;

	/* here, HP left in counters is less than twice
	 * the number of damage counters, that is:
	 * remaining HP < 1/3 max HP */

	/* if value is less than the one found before, store this one. */
	if (a >= wce08[0])
		goto next;
	wce08[0] = a;
	wce06 = e;
next:
	e++;
	d--;
	if (d != 0)
		goto loop_bench;

	a = wce06;
	*a_out = a;
	if (a == 0xff)
		return false;
	return true;
}
