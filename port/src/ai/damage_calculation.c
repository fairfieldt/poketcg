/*
 * Port of src/engine/duel/ai/damage_calculation.asm (INCLUDEd in engine/duel/ai/core.asm).
 */
#include "ai/ai.h"

/* `bit UNAFFECTED_BY_WEAKNESS_RESISTANCE_F, d` */
#define DE_UNAFFECTED_BY_WR ((u16)(1u << (8 + UNAFFECTED_BY_WEAKNESS_RESISTANCE_F)))

/* stores in wDamage, wAIMinDamage and wAIMaxDamage the calculated damage
 * done to the defending Pokémon by a given card and attack
 * input:
 *	a = attack index to take into account
 *	[hTempPlayAreaLocation_ff9d] = location of attacking card to consider */
void EstimateDamage_VersusDefendingCard(u8 attack)
{
	TRACE_FN();
	wSelectedAttack = attack;
	u8 e = attack;
	u8 d = GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD));
	CopyAttackDataAndDamage_FromDeckIndex(d, e);
	if (wLoadedAttackCategory == POKEMON_POWER) {
		/* is a Pokémon Power
		 * set wDamage, wAIMinDamage and wAIMaxDamage to zero */
		wDamage[0] = 0;
		wDamage[1] = 0;
		wAIMinDamage = 0;
		wAIMaxDamage = 0;
		return;
	}

	/* .is_attack
	 * set wAIMinDamage and wAIMaxDamage to damage of attack
	 * these values take into account the range of damage
	 * that the attack can span (e.g. min and max number of hits) */
	wAIMinDamage = wDamage[0];
	wAIMaxDamage = wDamage[0];
	TryExecuteEffectCommandFunction(EFFECTCMDTYPE_AI);
	if ((wAIMinDamage | wAIMaxDamage) == 0) {
		wAIMinDamage = wDamage[0];
		wAIMaxDamage = wDamage[0];
	}

	/* .calculation
	 * if temp. location is active, damage calculation can be done directly... */
	if (hTempPlayAreaLocation_ff9d == 0) {
		CalculateDamage_VersusDefendingPokemon();
		return;
	}

	/* ...otherwise substatuses need to be temporarily reset to account
	 * for the switching, to obtain the right damage calculation... */
	u8 *vars = TurnDuelistVars();
	/* reset substatus1 */
	u8 *substatus1 = &vars[DUELVARS_ARENA_CARD_SUBSTATUS1];
	u8 saved_substatus1 = *substatus1;
	*substatus1 = 0;
	/* reset substatus2 */
	u8 *substatus2 = &vars[DUELVARS_ARENA_CARD_SUBSTATUS2];
	u8 saved_substatus2 = *substatus2;
	*substatus2 = 0;
	/* reset changed resistance */
	u8 *changed_res = &vars[DUELVARS_ARENA_CARD_CHANGED_RESISTANCE];
	u8 saved_changed_res = *changed_res;
	*changed_res = 0;
	CalculateDamage_VersusDefendingPokemon();
	/* ...and subsequently recovered to continue the duel normally */
	*changed_res = saved_changed_res;
	*substatus2 = saved_substatus2;
	*substatus1 = saved_substatus1;
}

/* .Calculate: hl = wAIMinDamage, wAIMaxDamage or wDamage (low byte) */
static void CalculateDamage_VersusDefendingPokemon_Calculate(u8 *hl)
{
	u16 de = *hl;
	u8 a, b;

	/* load this card's data */
	LoadCardDataToBuffer2_FromDeckIndex(GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)));
	wTempTurnDuelistCardID = wLoadedCard2ID;

	/* load player's arena card data */
	SwapTurn();
	LoadCardDataToBuffer2_FromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	wTempNonTurnDuelistCardID = wLoadedCard2ID;
	SwapTurn();

	if (HandleNoDamageOrEffectSubstatus()) {
		/* invulnerable to damage */
		de = 0;
		goto done;
	}
	/* .vulnerable */
	if (hTempPlayAreaLocation_ff9d == 0)
		de = HandleDoubleDamageSubstatus(de);
	/* skips the weak/res checks if unaffected. */
	{
		bool unaffected = (de & DE_UNAFFECTED_BY_WR) != 0;
		de &= (u16)~DE_UNAFFECTED_BY_WR;
		if (unaffected)
			goto not_resistant;
	}

	/* handle weakness */
	b = TranslateColorToWR(GetPlayAreaCardColor(hTempPlayAreaLocation_ff9d));
	SwapTurn();
	a = GetArenaCardWeakness();
	SwapTurn();
	if (a & b)
		de = (u16)(de << 1); /* double de */

	/* .not_weak: handle resistance */
	SwapTurn();
	a = GetArenaCardResistance();
	SwapTurn();
	if (a & b)
		de = (u16)(de - 30);

not_resistant:
	/* apply pluspower and defender boosts */
	de = ApplyAttachedPlusPower((u8)(hTempPlayAreaLocation_ff9d + CARD_LOCATION_ARENA), de);
	SwapTurn();
	gRegB = CARD_LOCATION_ARENA; /* (ld b, CARD_LOCATION_ARENA: see TryExecuteEffectCommandFunction) */
	de = ApplyAttachedDefender(CARD_LOCATION_ARENA, de);
	de = HandleDamageReduction(de);
	/* test if de underflowed */
	if (de & 0x8000)
		de = 0;

	/* .no_underflow */
	a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS) & DOUBLE_POISONED;
	if (a) {
		u8 c = 20;
		if (!(a & (DOUBLE_POISONED & (POISONED ^ 0xff))))
			c = 10;
		/* .add_poison */
		de = (u16)(de + c);
	}
	/* .not_poisoned */
	SwapTurn();

done:
	*hl = (u8)de;
	if ((de >> 8) == 0)
		return;
	/* cap damage */
	*hl = 0xff;
}

/* calculates the damage that will be dealt to the player's active card
 * using the card that is located in hTempPlayAreaLocation_ff9d
 * taking into account weakness/resistance/pluspowers/defenders/etc
 * and outputs the result capped at a max of $ff
 * input:
 *	[wAIMinDamage] = base damage
 *	[wAIMaxDamage] = base damage
 *	[wDamage]      = base damage
 *	[hTempPlayAreaLocation_ff9d] = turn holder's card location as the attacker */
void CalculateDamage_VersusDefendingPokemon(void)
{
	TRACE_FN();
	CalculateDamage_VersusDefendingPokemon_Calculate(&wAIMinDamage);
	CalculateDamage_VersusDefendingPokemon_Calculate(&wAIMaxDamage);
	CalculateDamage_VersusDefendingPokemon_Calculate(wDamage);
}

/* stores in wDamage, wAIMinDamage and wAIMaxDamage the calculated damage
 * done to the Pokémon at hTempPlayAreaLocation_ff9d
 * by the defending Pokémon, using the attack index at a
 * input:
 *	a = attack index
 *	[hTempPlayAreaLocation_ff9d] = location of card to calculate
 *	                               damage as the receiver */
void EstimateDamage_FromDefendingPokemon(u8 attack)
{
	TRACE_FN();
	SwapTurn();
	wSelectedAttack = attack;
	u8 e = attack;
	u8 d = GetTurnDuelistVariable(DUELVARS_ARENA_CARD);
	CopyAttackDataAndDamage_FromDeckIndex(d, e);
	SwapTurn();
	if (wLoadedAttackCategory == POKEMON_POWER) {
		/* is a Pokémon Power
		 * set wDamage, wAIMinDamage and wAIMaxDamage to zero */
		wDamage[0] = 0;
		wDamage[1] = 0;
		wAIMinDamage = 0;
		wAIMaxDamage = 0;
		return;
	}

	/* .is_attack
	 * set wAIMinDamage and wAIMaxDamage to damage of attack
	 * these values take into account the range of damage
	 * that the attack can span (e.g. min and max number of hits) */
	wAIMinDamage = wDamage[0];
	wAIMaxDamage = wDamage[0];
	SwapTurn();
	{
		u8 saved_loc = hTempPlayAreaLocation_ff9d;
		hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
		TryExecuteEffectCommandFunction(EFFECTCMDTYPE_AI);
		hTempPlayAreaLocation_ff9d = saved_loc;
	}
	SwapTurn();
	if ((wAIMinDamage | wAIMaxDamage) == 0) {
		wAIMinDamage = wDamage[0];
		wAIMaxDamage = wDamage[0];
	}

	/* .calculation
	 * if temp. location is active, damage calculation can be done directly... */
	if (hTempPlayAreaLocation_ff9d == 0) {
		CalculateDamage_FromDefendingPokemon();
		return;
	}

	/* ...otherwise substatuses need to be temporarily reset to account
	 * for the switching, to obtain the right damage calculation... */
	u8 *vars = TurnDuelistVars();
	u8 *substatus1 = &vars[DUELVARS_ARENA_CARD_SUBSTATUS1];
	u8 saved_substatus1 = *substatus1;
	*substatus1 = 0;
	/* reset substatus2 */
	u8 *substatus2 = &vars[DUELVARS_ARENA_CARD_SUBSTATUS2];
	u8 saved_substatus2 = *substatus2;
	*substatus2 = 0;
	/* reset changed resistance */
	u8 *changed_res = &vars[DUELVARS_ARENA_CARD_CHANGED_RESISTANCE];
	u8 saved_changed_res = *changed_res;
	*changed_res = 0;
	CalculateDamage_FromDefendingPokemon();
	/* ...and subsequently recovered to continue the duel normally */
	*changed_res = saved_changed_res;
	*substatus2 = saved_substatus2;
	*substatus1 = saved_substatus1;
}

/* .CalculateDamage: hl = wAIMinDamage, wAIMaxDamage or wDamage (low byte) */
static void CalculateDamage_FromDefendingPokemon_CalculateDamage(u8 *hl)
{
	u16 de = *hl;
	u8 a, b;

	/* load player active card's data */
	SwapTurn();
	LoadCardDataToBuffer2_FromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	wTempTurnDuelistCardID = wLoadedCard2ID;
	SwapTurn();

	/* load opponent's card data */
	LoadCardDataToBuffer2_FromDeckIndex(GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)));
	wTempNonTurnDuelistCardID = wLoadedCard2ID;

	SwapTurn();
	de = HandleDoubleDamageSubstatus(de);
	{
		/* bug (original): this jump skips the SwapTurn of the weakness check, so the
		 * turn is left swapped on return (and PlusPower/Defender are taken from the wrong
		 * sides). Unreachable in practice: d is 0 before HandleDoubleDamageSubstatus,
		 * which at most doubles a value < $100, so this bit is never set. */
		bool unaffected = (de & DE_UNAFFECTED_BY_WR) != 0;
		de &= (u16)~DE_UNAFFECTED_BY_WR;
		if (unaffected)
			goto not_resistant;
	}

	/* handle weakness */
	b = TranslateColorToWR(GetArenaCardColor());
	SwapTurn();
	if (hTempPlayAreaLocation_ff9d != 0)
		goto bench_weak;
	a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_CHANGED_WEAKNESS);
	if (a != 0)
		goto unchanged_weak;
bench_weak:
	LoadCardDataToBuffer2_FromDeckIndex(GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)));
	a = wLoadedCard2Weakness;
unchanged_weak:
	if (a & b)
		de = (u16)(de << 1); /* double de */

	/* .not_weak: handle resistance */
	if (hTempPlayAreaLocation_ff9d != 0)
		goto bench_res;
	a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_CHANGED_RESISTANCE);
	if (a != 0)
		goto unchanged_res;
bench_res:
	LoadCardDataToBuffer2_FromDeckIndex(GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)));
	a = wLoadedCard2Resistance;
unchanged_res:
	if (a & b)
		de = (u16)(de - 30);

not_resistant:
	/* apply pluspower and defender boosts */
	SwapTurn();
	de = ApplyAttachedPlusPower(CARD_LOCATION_ARENA, de);
	SwapTurn();
	de = ApplyAttachedDefender((u8)(hTempPlayAreaLocation_ff9d + CARD_LOCATION_ARENA), de);
	if (hTempPlayAreaLocation_ff9d == 0)
		de = HandleDamageReduction(de);
	if (de & 0x8000)
		de = 0;

	/* .no_underflow */
	if (hTempPlayAreaLocation_ff9d != 0)
		goto done;
	a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS) & DOUBLE_POISONED;
	if (a == 0)
		goto done;
	{
		u8 c = 40;
		if (!(a & (DOUBLE_POISONED & (POISONED ^ 0xff))))
			c = 20;
		/* .add_poison */
		de = (u16)(de + c);
	}

done:
	*hl = (u8)de;
	if ((de >> 8) == 0)
		return;
	*hl = 0xff;
}

/* similar to CalculateDamage_VersusDefendingPokemon but reversed,
 * calculating damage of the defending Pokémon versus
 * the card located in hTempPlayAreaLocation_ff9d
 * taking into account weakness/resistance/pluspowers/defenders/etc
 * and poison damage for two turns
 * and outputs the result capped at a max of $ff
 * input:
 *	[wAIMinDamage] = base damage
 *	[wAIMaxDamage] = base damage
 *	[wDamage]      = base damage
 *	[hTempPlayAreaLocation_ff9d] = location of card to calculate
 *								 damage as the receiver */
void CalculateDamage_FromDefendingPokemon(void)
{
	TRACE_FN();
	CalculateDamage_FromDefendingPokemon_CalculateDamage(&wAIMinDamage);
	CalculateDamage_FromDefendingPokemon_CalculateDamage(&wAIMaxDamage);
	/* fallthrough */
	CalculateDamage_FromDefendingPokemon_CalculateDamage(wDamage);
}
