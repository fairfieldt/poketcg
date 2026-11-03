#include "../cat_duo.h"
/*
 * Port of src/engine/duel/ai/core.asm itself (bank $05, "AI Logic 1").
 * Its INCLUDEs are ported separately:
 *   decks/unreferenced.asm, deck_ai.asm + decks/<deck>.asm -> ai/deck_ai.c
 *   damage_calculation.asm -> ai/damage_calculation.c, init.asm -> ai/init.c,
 *   boss_deck_set_up.asm -> ai/boss_deck_set_up.c,
 *   retreat.asm, hand_pokemon.asm, energy.asm, attacks.asm, special_attacks.asm -> part C.
 */
#include "ai/ai.h"

/* `swap a` */
static inline u8 SwapNibbles(u8 a)
{
	return (u8)((a >> 4) | (a << 4));
}

/* `rrca` (used to halve HP values, which are always even) */
static inline u8 Rrca(u8 a)
{
	return (u8)((a >> 1) | (a << 7));
}

/* ---- INCLUDE "engine/duel/ai/decks/unreferenced.asm" (ai/deck_ai.c) */

/* .CheckAttack: carry if the attack in a KOs the defending card */
static bool CheckIfAnyAttackKnocksOutDefendingCard_CheckAttack(u8 a)
{
	EstimateDamage_VersusDefendingCard(a);
	u8 hp = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);
	/* sub [hl] / ret c / ret nz / scf */
	if (hp < wDamage[0])
		return true;
	if (hp != wDamage[0])
		return false;
	return true;
}

/* returns carry if damage dealt from any of
 * a card's attacks KOs defending Pokémon
 * outputs index of the attack that KOs
 * input:
 *	[hTempPlayAreaLocation_ff9d] = location of attacking card to consider
 * output:
 *	[wSelectedAttack] = attack index that KOs */
bool CheckIfAnyAttackKnocksOutDefendingCard(void)
{
	TRACE_FN();
	if (CheckIfAnyAttackKnocksOutDefendingCard_CheckAttack(FIRST_ATTACK_OR_PKMN_POWER))
		return true;
	return CheckIfAnyAttackKnocksOutDefendingCard_CheckAttack(SECOND_ATTACK);
}

/* .check_damage */
static bool CheckIfAnyDefendingPokemonAttackDealsSameDamageAsHP_check_damage(u8 a)
{
	EstimateDamage_FromDefendingPokemon(a);
	u8 hp = GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD_HP));
	/* sub [hl] / jr z, .true */
	if (hp == wDamage[0])
		return true;
	/* bug (original): `ret` keeps the carry of `sub [hl]`,
	 * so this also returns carry if the damage is larger than the HP */
	return hp < wDamage[0];
}

/* returns carry if any of the defending Pokémon's attacks
 * brings card at hTempPlayAreaLocation_ff9d down
 * to exactly 0 HP.
 * outputs that attack index in wSelectedAttack. */
bool CheckIfAnyDefendingPokemonAttackDealsSameDamageAsHP(void)
{
	TRACE_FN();
	if (CheckIfAnyDefendingPokemonAttackDealsSameDamageAsHP_check_damage(FIRST_ATTACK_OR_PKMN_POWER))
		return true;
	return CheckIfAnyDefendingPokemonAttackDealsSameDamageAsHP_check_damage(SECOND_ATTACK);
}

/* checks AI scores for all benched Pokémon
 * returns the location of the card with highest score
 * in a and [hTempPlayAreaLocation_ff9d] */
u8 FindHighestBenchScore(void)
{
	TRACE_FN();
	u8 b = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	u8 c = 0, e = 0, d = 0;
	const u8 *hl = wPlayAreaAIScore + 1;
	/* jp .next ; can be jr */
	for (;;) {
		/* .next */
		c++;
		if (--b == 0)
			break;
		/* .loop */
		u8 a = *hl++;
		if (a < e)
			continue;
		e = a;
		d = c;
	}
	hTempPlayAreaLocation_ff9d = d;
	return d;
}

/* adds a to wAIScore
 * if there's overflow, it's capped at 255
 * output:
 *	a = a + wAIScore (capped at 255) */
u8 AIEncourage(u8 a)
{
	u16 sum = (u16)(a + wAIScore);
	a = (u8)sum;
	if (sum > 0xff)
		a = 255;
	/* .no_cap */
	wAIScore = a;
	return a;
}

/* subs a from wAIScore
 * if there's underflow, it's capped at 0 */
u8 AIDiscourage(u8 a)
{
	u8 e = a;
	a = wAIScore;
	if (a == 0)
		return a; /* .done */
	u8 borrow = a < e;
	a = (u8)(a - e);
	wAIScore = a;
	if (borrow)
		wAIScore = 0;
	/* .done */
	return a;
}

/* loads defending Pokémon's weakness/resistance
 * and the number of prize cards in both sides */
void LoadDefendingPokemonColorWRAndPrizeCards(void)
{
	TRACE_FN();
	SwapTurn();
	wAIPlayerColor = TranslateColorToWR(GetArenaCardColor());
	wAIPlayerWeakness = GetArenaCardWeakness();
	wAIPlayerResistance = GetArenaCardResistance();
	wAIPlayerPrizeCount = CountPrizes();
	SwapTurn();
	wAIOpponentPrizeCount = CountPrizes();
}

/* called when AI has chosen its attack.
 * executes all effects and damage.
 * handles AI choosing parameters for certain attacks as well. */
bool AITryUseAttack(void)
{
	TRACE_FN();
	u8 d, e;
	e = wSelectedAttack;
	hTemp_ffa0 = e;
	d = GetTurnDuelistVariable(DUELVARS_ARENA_CARD);
	hTempCardIndex_ff9f = d;
	CopyAttackDataAndDamage_FromDeckIndex(d, e);
	if (AIMakeDecision(OPPACTION_BEGIN_ATTACK))
		return true;

	if (!AISelectSpecialAttackParameters())
		TryExecuteEffectCommandFunction(EFFECTCMDTYPE_AI_SELECTION);

	/* .use_attack */
	e = wSelectedAttack;
	d = GetTurnDuelistVariable(DUELVARS_ARENA_CARD);
	CopyAttackDataAndDamage_FromDeckIndex(d, e);
	if (AIMakeDecision(OPPACTION_USE_ATTACK))
		return true;

	TryExecuteEffectCommandFunction(EFFECTCMDTYPE_AI_SWITCH_DEFENDING_PKMN);
	return AIMakeDecision(OPPACTION_ATTACK_ANIM_AND_DAMAGE);
}

/* return carry if any of the following is satisfied:
 *	- deck index in a corresponds to a double colorless energy card;
 *	- card type in wTempCardType is colorless;
 *	- card ID in wTempCardID is a Pokémon card that has
 *	  attacks that require energy other than its color and
 *	  the deck index in a corresponds to that energy type;
 *	- card ID is Eevee and a corresponds to an energy type
 *	  of water, fire or lightning;
 *	- type of card in register a is the same as wTempCardType.
 * used for knowing if a given energy card can be discarded
 * from a given Pokémon card
 * input:
 *	a = energy card attached to Pokémon to check
 *	[wTempCardType] = TYPE_ENERGY_* of given Pokémon
 *	[wTempCardID] = card index of Pokémon card to check */
bool CheckIfEnergyIsUseful(u8 card)
{
	TRACE_FN();
	u8 a, d;
	u8 e = GetCardIDFromDeckIndex(card);
	if (e == DOUBLE_COLORLESS_ENERGY)
		return true; /* .set_carry */
	if (wTempCardType == TYPE_ENERGY_DOUBLE_COLORLESS)
		return true;
	a = wTempCardID;

	d = PSYCHIC_ENERGY;
	if (a == EXEGGCUTE)
		goto check_energy;
	if (a == EXEGGUTOR)
		goto check_energy;
	if (a == PSYDUCK)
		goto check_energy;
	if (a == GOLDUCK)
		goto check_energy;

	d = WATER_ENERGY;
	if (a == SURFING_PIKACHU_LV13)
		goto check_energy;
	if (a == SURFING_PIKACHU_ALT_LV13)
		goto check_energy;

	if (a != EEVEE)
		goto check_type;
	if (e == WATER_ENERGY)
		return true;
	if (e == FIRE_ENERGY)
		return true;
	if (e == LIGHTNING_ENERGY)
		return true;

check_type:
	/* ld d, $00 ; unnecessary? */
	d = GetCardType(e);
	if (wTempCardType == d)
		return true;
	return false;

check_energy:
	if (d != e)
		goto check_type;
	return true; /* .set_carry */
}

/* pick a random Pokemon in the bench.
 * output:
 *	- a = PLAY_AREA_* of Bench Pokemon picked. */
u8 PickRandomBenchPokemon(void)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	a = (u8)(a - 1);
	a = Random(a);
	return (u8)(a + 1);
}

static const u8 AIPickPrizeCards_prize_flags[8] = {
	1 << 0,
	1 << 1,
	1 << 2,
	1 << 3,
	1 << 4,
	1 << 5,
	1 << 6,
	1 << 7,
};

/* picks a prize card at random
 * and adds it to the hand. */
static void AIPickPrizeCards_PickPrizeCard(void)
{
	u8 *hl = TurnDuelistVars() + DUELVARS_PRIZES;
	u8 c = *hl;
	u8 e;

	/* choose a random prize card until
	 * one is found that isn't taken already. */
	do {
		e = Random(6);
	} while (!(AIPickPrizeCards_prize_flags[e] & c)); /* no prize */

	/* prize card was found
	 * remove this prize from wOpponentPrizes */
	*hl = (u8)(~AIPickPrizeCards_prize_flags[e] & *hl);

	/* add this prize card to the hand */
	AddCardToHand(GetTurnDuelistVariable((u8)(e + DUELVARS_PRIZE_CARDS)));
}

void AIPickPrizeCards(void)
{
	TRACE_FN();
	u8 b = wNumberPrizeCardsToTake;
	do {
		AIPickPrizeCards_PickPrizeCard();
		if (GetTurnDuelistVariable(DUELVARS_PRIZES) == 0)
			break; /* .done */
	} while (--b);
}

/* routine for AI to play all Basic cards from its hand
 * in the beginning of the Duel. */
void AIPlayInitialBasicCards(void)
{
	TRACE_FN();
	CreateHandCardList(NULL);
	const u8 *hl = wDuelTempList;
	for (;;) {
		/* .check_for_next_card */
		u8 a = *hl++;
		hTempCardIndex_ff98 = a;
		if (a == 0xff)
			return; /* return when done */

		LoadCardDataToBuffer1_FromDeckIndex(a);
		if (wLoadedCard1Type >= TYPE_ENERGY)
			continue; /* skip if not Pokemon card */
		if (wLoadedCard1Stage != 0)
			continue; /* skip if not Basic Stage */

		/* play Basic card from hand */
		PutHandPokemonCardInPlayArea(hTempCardIndex_ff98, NULL);
	}
}

/* returns carry if Pokémon at hTempPlayAreaLocation_ff9d
 * can't use an attack or if that selected attack doesn't have enough energy
 * input:
 *	[hTempPlayAreaLocation_ff9d] = location of Pokémon card
 *	[wSelectedAttack]         = selected attack to examine */
bool CheckIfSelectedAttackIsUnusable(void)
{
	TRACE_FN();
	u8 b, c, e;
	if (hTempPlayAreaLocation_ff9d != 0)
		goto bench;

	if (HandleCantAttackSubstatus())
		return true;
	if (CheckIfActiveCardParalyzedOrAsleep())
		return true;

	CopyAttackDataAndDamage_FromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD), wSelectedAttack);
	if (HandleAmnesiaSubstatus())
		return true;
	if (TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_1))
		return true;

bench:
	if (CheckEnergyNeededForAttack(&b, &c, &e))
		return true; /* can't be used */
	return CheckLoadedAttackFlag(ATTACK_FLAG2_ADDRESS | IGNORE_THIS_ATTACK_F);
}

/* load selected attack from Pokémon in hTempPlayAreaLocation_ff9d
 * and checks if there is enough energy to execute the selected attack
 * input:
 *	[hTempPlayAreaLocation_ff9d] = location of Pokémon card
 *	[wSelectedAttack]            = selected attack to examine
 * output:
 *	b = basic energy still needed
 *	c = colorless energy still needed
 *	e = output of ConvertColorToEnergyCardID, or $0 if not an attack
 *	carry set if no attack
 *	       OR if it's a Pokémon Power
 *	       OR if not enough energy for attack */
bool CheckEnergyNeededForAttack(u8 *b, u8 *c, u8 *e)
{
	TRACE_FN();
	u8 a, rb, rc;
	CopyAttackDataAndDamage_FromDeckIndex(
		GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)), wSelectedAttack);
	if ((wLoadedAttackName[0] | wLoadedAttackName[1]) == 0)
		goto no_attack;
	if (wLoadedAttackCategory != POKEMON_POWER)
		goto is_attack;
no_attack:
	*b = 0;
	*c = 0;
	*e = 0;
	return true;

is_attack:
	GetPlayAreaCardAttachedEnergies(hTempPlayAreaLocation_ff9d);
	HandleEnergyBurn();

	wTempLoadedAttackEnergyCost = 0;
	wTempLoadedAttackEnergyNeededAmount = 0;
	wTempLoadedAttackEnergyNeededType = 0;

	{
		u8 *hl = wAttachedEnergies;
		const u8 *de = wLoadedAttackEnergyCost;
		rb = 0;
		rc = (NUM_TYPES / 2) - 1;
		do {
			/* check all basic energy cards except colorless */
			CheckIfEnoughParticularAttachedEnergy(SwapNibbles(*de), &hl, &rb);
			CheckIfEnoughParticularAttachedEnergy(*de, &hl, &rb);
			de++;
		} while (--rc);

		/* running CheckIfEnoughParticularAttachedEnergy back to back like this
		 * overwrites the results of a previous call of this function,
		 * however, no attack in the game has energy requirements for two
		 * different energy types (excluding colorless), so this routine
		 * will always just return the result for one type of basic energy,
		 * while all others will necessarily have an energy cost of 0
		 * if attacks are added to the game with energy requirements of
		 * two different basic energy types, then this routine only accounts
		 * for the type with the highest index */

		/* colorless */
		rb = (u8)(SwapNibbles(*de) & 0x0f); /* colorless energy still needed */
		rc = (u8)(wTempLoadedAttackEnergyCost - wTempLoadedAttackEnergyNeededAmount); /* basic energy still needed */
		a = (u8)(wTotalAttachedEnergies - rc);
		bool carry = a < rb;
		a = (u8)(a - rb);
		if (carry)
			goto not_enough;

		if (wTempLoadedAttackEnergyNeededAmount == 0) {
			*b = rb;
			*c = rc;
			*e = (u8)gbaddr(de); /* e = LOW(wLoadedAttackEnergyCost + 3) */
			return false;
		}
	}

	/* being here means the energy cost isn't satisfied,
	 * including with colorless energy */
	a = 0;
not_enough:
	a = (u8)(~a + 1);
	*c = a; /* colorless energy still needed */
	*b = wTempLoadedAttackEnergyNeededAmount; /* basic energy still needed */
	*e = ConvertColorToEnergyCardID(wTempLoadedAttackEnergyNeededType);
	return true;
}

/* takes as input the energy cost of an attack for a
 * particular energy, stored in the lower nibble of a
 * if the attack costs some amount of this energy, the lower nibble of a != 0,
 * and this amount is stored in wTempLoadedAttackEnergyCost
 * sets carry flag if not enough energy of this type attached
 * input:
 *	a    = this energy cost of attack (lower nibble)
 *	[hl] = attached energy
 * output:
 *	carry set if not enough of this energy type attached */
bool CheckIfEnoughParticularAttachedEnergy(u8 a, u8 **hl, u8 *b)
{
	a &= 0x0f;
	if (a == 0)
		goto has_enough;

	/* .check */
	wTempLoadedAttackEnergyCost = a;
	{
		u8 attached = **hl;
		if (a == attached || a < attached)
			goto has_enough;
		a = (u8)(a - attached);
	}

	/* not enough energy */
	wTempLoadedAttackEnergyNeededAmount = a;
	wTempLoadedAttackEnergyNeededType = *b;
	(*hl)++;
	(*b)++;
	return true;

has_enough:
	(*hl)++;
	(*b)++;
	return false;
}

static const u8 ConvertColorToEnergyCardID_card_id[7] = {
	FIRE_ENERGY,
	GRASS_ENERGY,
	LIGHTNING_ENERGY,
	WATER_ENERGY,
	FIGHTING_ENERGY,
	PSYCHIC_ENERGY,
	DOUBLE_COLORLESS_ENERGY,
};

/* input:
 *	a = energy type
 * output:
 *	a = energy card ID */
u8 ConvertColorToEnergyCardID(u8 type)
{
	if (type >= 7) {
		tcg_fatal("ConvertColorToEnergyCardID: type out of range (the original reads the following ROM code)");
		return 0;
	}
	return ConvertColorToEnergyCardID_card_id[type];
}

/* returns carry if loaded attack effect has
 * an "initial effect 2" or "require selection" command type
 * unreferenced */
bool Func_14323(void)
{
	TRACE_FN();
	u16 hl = rd16(wLoadedAttackEffectCommands);
	if (CheckMatchingCommand(hl, EFFECTCMDTYPE_INITIAL_EFFECT_2))
		return true; /* .set_carry */
	if (CheckMatchingCommand(hl, EFFECTCMDTYPE_REQUIRE_SELECTION))
		return true; /* .set_carry */
	return false;
}

/* return carry depending on card index in a:
 *	- if energy card, return carry if an energy card has been played already
 *	- if basic Pokémon card, return carry if there's no space in bench
 *	- if evolution card, return carry if there's no Pokémon in Play to evolve
 *	- if trainer card, return carry if it cannot be used
 * input:
 *	a = card index to check */
bool CheckIfCardCanBePlayed(u8 card)
{
	TRACE_FN();
	u8 a, b, c;
	hTempCardIndex_ff9f = card;
	LoadCardDataToBuffer1_FromDeckIndex(card);
	a = wLoadedCard1Type;
	if (a < TYPE_ENERGY)
		goto pokemon_card;
	if (a == TYPE_TRAINER)
		goto trainer_card;

	/* energy card */
	return wAlreadyPlayedEnergy != 0;

pokemon_card:
	if (wLoadedCard1Stage != 0)
		goto evolution_card;
	/* cp MAX_PLAY_AREA_POKEMON / ccf */
	return GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) >= MAX_PLAY_AREA_POKEMON;

evolution_card:
	if (IsPrehistoricPowerActive())
		return true;
	c = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	b = 0;
	do {
		if (!CheckIfCanEvolveInto(hTempCardIndex_ff9f, b, NULL))
			return false;
		b++;
	} while (--c);
	return true;

trainer_card:
	if (CheckCantUseTrainerDueToEffect())
		return true;
	LoadNonPokemonCardEffectCommands();
	return TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_1);
}

/* loads all the energy cards
 * in hand in wDuelTempList
 * return carry if no energy cards found */
bool CreateEnergyCardListFromHand(void)
{
	TRACE_FN();
	u8 *de = wDuelTempList;
	const u8 *vars = TurnDuelistVars();
	u8 c = vars[DUELVARS_NUMBER_OF_CARDS_IN_HAND];
	c++;
	const u8 *hl = vars + DUELVARS_HAND;
	/* jr .decrease */
	while (--c) {
		/* .loop */
		u8 a = *hl++;
		if (!(GetCardType(GetCardIDFromDeckIndex(a)) & TYPE_ENERGY))
			continue; /* .decrease */
		*de++ = hl[-1];
	}
	*de = 0xff;
	/* cp $ff / ccf */
	return wDuelTempList[0] == 0xff;
}

/* looks for card ID in hand and
 * sets carry if a card wasn't found
 * as opposed to LookForCardIDInHandList_Bank5
 * this function doesn't create a list
 * and preserves hl, de and bc
 * input:
 *	a = card ID
 * output:
 *	a = card deck index, if found
 *	carry set if NOT found */
bool LookForCardIDInHand(u8 card_id, u8 *a)
{
	TRACE_FN();
	u8 b = card_id;
	const u8 *vars = TurnDuelistVars();
	u8 r = vars[DUELVARS_NUMBER_OF_CARDS_IN_HAND];
	u8 c = (u8)(r + 1);
	const u8 *hl = vars + DUELVARS_HAND;
	/* jr .next */
	while (--c) {
		/* .loop */
		r = GetCardIDFromDeckIndex(*hl++);
		if (r == b) {
			/* .no_carry */
			*a = hl[-1];
			return false;
		}
	}
	*a = r;
	return true;
}

/* ---- INCLUDE "engine/duel/ai/damage_calculation.asm" (ai/damage_calculation.c) */

void AIProcessHandTrainerCards(u8 phase)
{
	TRACE_FN();
	_AIProcessHandTrainerCards(phase);
}

/* ---- INCLUDE "engine/duel/ai/deck_ai.asm" (ai/deck_ai.c) */

/* return carry if card ID loaded in a is found in hand
 * and outputs in a the deck index of that card
 * as opposed to LookForCardIDInHand, this function
 * creates a list in wDuelTempList
 * input:
 *	a = card ID
 * output:
 *	a = card deck index, if found
 *	carry set if found */
bool LookForCardIDInHandList_Bank5(u8 card_id, u8 *a)
{
	TRACE_FN();
	wTempCardIDToLook = card_id;
	CreateHandCardList(NULL);
	const u8 *hl = wDuelTempList;
	for (;;) {
		/* .loop */
		u8 x = *hl++;
		if (x == 0xff) {
			*a = x;
			return false;
		}
		hTempCardIndex_ff98 = x;
		u8 b = LoadCardDataToBuffer1_FromDeckIndex(x);
		if (wTempCardIDToLook == b)
			break;
	}
	*a = hTempCardIndex_ff98;
	return true;
}

/* returns carry if card ID in a
 * is found in Play Area, starting with
 * location in b
 * input:
 *	a = card ID
 *	b = PLAY_AREA_* to start with
 * output:
 *	a = PLAY_AREA_* of found card
 *	carry set if found */
bool LookForCardIDInPlayArea_Bank5(u8 card_id, u8 b, u8 *a)
{
	TRACE_FN();
	wTempCardIDToLook = card_id;
	do {
		/* .loop */
		u8 x = GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + b));
		if (x == 0xff) {
			*a = x;
			return false;
		}
		u8 c = LoadCardDataToBuffer1_FromDeckIndex(x);
		if (wTempCardIDToLook == c) {
			/* .found */
			*a = b;
			return true;
		}
		b++;
	} while (b != MAX_PLAY_AREA_POKEMON);

	/* not found (ld b, $ff / or a: a still holds MAX_PLAY_AREA_POKEMON) */
	*a = MAX_PLAY_AREA_POKEMON;
	return false;
}

/* .attach: a = deck index of the energy card, b = first PLAY_AREA_* to look, d = Pokemon card ID */
static bool AIAttachEnergyInHandToCardInPlayArea_attach(u8 a, u8 b, u8 d)
{
	u8 e = a;
	LookForCardIDInPlayArea_Bank5(d, b, &a);
	hTempPlayAreaLocation_ffa1 = a;
	hTemp_ffa0 = e;
	return AIMakeDecision(OPPACTION_PLAY_ENERGY);
}

/* check if energy card ID in e is in AI hand and,
 * if so, attaches it to card ID in d in Play Area.
 * input:
 *	e = Energy card ID
 *	d = Pokemon card ID */
bool AIAttachEnergyInHandToCardInPlayArea(u8 d, u8 e)
{
	TRACE_FN();
	u8 a;
	if (!LookForCardIDInHandList_Bank5(e, &a))
		return false; /* not in hand */
	return AIAttachEnergyInHandToCardInPlayArea_attach(a, PLAY_AREA_ARENA, d);
}

/* same as AIAttachEnergyInHandToCardInPlayArea but
 * only look for card ID in the Bench. */
bool AIAttachEnergyInHandToCardInBench(u8 d, u8 e)
{
	TRACE_FN();
	u8 a;
	if (!LookForCardIDInHandList_Bank5(e, &a))
		return false;
	return AIAttachEnergyInHandToCardInPlayArea_attach(a, PLAY_AREA_BENCH_1, d);
}

/* ---- INCLUDE "engine/duel/ai/init.asm" (ai/init.c) */

/* load selected attack from Pokémon in hTempPlayAreaLocation_ff9d,
 * gets an energy card to discard and subsequently
 * check if there is enough energy to execute the selected attack
 * after removing that attached energy card.
 * input:
 *	[hTempPlayAreaLocation_ff9d] = location of Pokémon card
 *	[wSelectedAttack]         = selected attack to examine
 * output:
 *	b = basic energy still needed
 *	c = colorless energy still needed
 *	e = output of ConvertColorToEnergyCardID, or $0 if not an attack
 *	carry set if no attack
 *	       OR if it's a Pokémon Power
 *	       OR if not enough energy for attack */
bool CheckEnergyNeededForAttackAfterDiscard(u8 *b, u8 *c, u8 *e)
{
	TRACE_FN();
	u8 a, rb, rc;
	CopyAttackDataAndDamage_FromDeckIndex(
		GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)), wSelectedAttack);
	if ((wLoadedAttackName[0] | wLoadedAttackName[1]) == 0)
		goto no_attack;
	if (wLoadedAttackCategory != POKEMON_POWER)
		goto is_attack;
no_attack:
	*b = 0;
	*c = 0;
	*e = 0;
	return true;

is_attack:
	a = AIPickEnergyCardToDiscard(hTempPlayAreaLocation_ff9d);
	a = LoadCardDataToBuffer1_FromDeckIndex(a);
	if (a == DOUBLE_COLORLESS_ENERGY) {
		/* .double_colorless
		 * decrease attached colorless by 2. */
		wAttachedEnergies[COLORLESS] = (u8)(wAttachedEnergies[COLORLESS] - 2);
		wTotalAttachedEnergies = (u8)(wTotalAttachedEnergies - 2);
	} else {
		/* color energy
		 * decrease respective attached energy by 1.
		 * bug (original): the card ID - 1 is used as the wAttachedEnergies index, but
		 * the card IDs are ordered GRASS, FIRE, WATER, LIGHTNING, FIGHTING, PSYCHIC while
		 * wAttachedEnergies is FIRE, GRASS, LIGHTNING, WATER, ...: Fire/Grass and
		 * Water/Lightning are swapped */
		u8 *hl = wAttachedEnergies + (u8)(a - 1);
		*hl = (u8)(*hl - 1);
		wTotalAttachedEnergies = (u8)(wTotalAttachedEnergies - 1);
	}

	/* .decremented_energy */
	HandleEnergyBurn();
	wTempLoadedAttackEnergyCost = 0;
	wTempLoadedAttackEnergyNeededAmount = 0;
	wTempLoadedAttackEnergyNeededType = 0;
	{
		u8 *hl = wAttachedEnergies;
		const u8 *de = wLoadedAttackEnergyCost;
		rb = 0;
		rc = (NUM_TYPES / 2) - 1;
		do {
			/* check all basic energy cards except colorless */
			CheckIfEnoughParticularAttachedEnergy(SwapNibbles(*de), &hl, &rb);
			CheckIfEnoughParticularAttachedEnergy(*de, &hl, &rb);
			de++;
		} while (--rc);

		rb = (u8)(SwapNibbles(*de) & 0x0f); /* colorless energy still needed */
		rc = (u8)(wTempLoadedAttackEnergyCost - wTempLoadedAttackEnergyNeededAmount); /* basic energy still needed */
		a = (u8)(wTotalAttachedEnergies - rc);
		bool carry = a < rb;
		a = (u8)(a - rb);
		if (carry)
			goto not_enough_energy;

		if (wTempLoadedAttackEnergyNeededAmount == 0) {
			*b = rb;
			*c = rc;
			*e = (u8)gbaddr(de); /* e = LOW(wLoadedAttackEnergyCost + 3) */
			return false;
		}
	}

	/* being here means the energy cost isn't satisfied,
	 * including with colorless energy */
	a = 0;
not_enough_energy:
	a = (u8)(~a + 1);
	*c = a; /* colorless energy still needed */
	*b = wTempLoadedAttackEnergyNeededAmount; /* basic energy still needed */
	*e = ConvertColorToEnergyCardID(wTempLoadedAttackEnergyNeededType);
	return true;
}

/* zeroes a bytes starting from hl.
 * this function is identical to 'ClearMemory_Bank2',
 * 'ClearMemory_Bank6' and 'ClearMemory_Bank8'.
 * preserves all registers
 * input:
 *	a = number of bytes to clear
 *	hl = where to begin erasing */
void ClearMemory_Bank5(u8 a, u8 *hl)
{
	u8 b = a;
	do {
		*hl++ = 0;
	} while (--b);
}

/* converts an HP value or amount of damage to the number of equivalent damage counters
 * preserves all registers except af
 * input:
 *	a = HP value to convert
 * output:
 *	a = number of damage counters */
u8 ConvertHPToDamageCounters_Bank5(u8 hp)
{
	u8 c = 0;
	for (;;) {
		bool carry = hp < 10;
		hp = (u8)(hp - 10);
		if (carry)
			break;
		c++;
	}
	return c;
}

/* returns in a the result of
 * dividing b by a, rounded down
 * input:
 *	a = divisor
 *	b = dividend */
u8 CalculateBDividedByA_Bank5(u8 a, u8 b)
{
	u8 divisor = a;
	u8 c = 0;
	a = b;
	for (;;) {
		bool carry = a < divisor;
		a = (u8)(a - divisor);
		if (carry)
			break;
		c++;
	}
	return c;
}

/* returns in a the number of energy cards attached
 * to Pokémon in location held by e
 * this assumes that colorless are paired so
 * that one colorless energy card provides 2 colorless energy
 * input:
 *	e = location to check, i.e. PLAY_AREA_*
 * output:
 *	a = number of energy cards attached */
u8 CountNumberOfEnergyCardsAttached(u8 e)
{
	TRACE_FN();
	GetPlayAreaCardAttachedEnergies(e);
	if (wTotalAttachedEnergies == 0)
		return 0;

	u8 a = 0;
	const u8 *hl = wAttachedEnergies;
	/* sum all the attached energies */
	for (u8 b = NUM_COLORED_TYPES; b; b--)
		a = (u8)(a + *hl++);
	/* counts colorless ad halves it */
	return (u8)(a + (*hl >> 1));
}

/* returns carry if any card with ID in e is found
 * in card location in a
 * input:
 *	a = CARD_LOCATION_* constant
 *	e = card ID to look for
 * output:
 *	a & e = deck index of a matching card, if any
 *	carry set if found */
bool LookForCardIDInLocation_Bank5(u8 a, u8 e, u8 *idx)
{
	TRACE_FN();
	u8 b = a;
	u8 c = e;
	e = 0; /* lb de, 0, 0 ; d is never used */
	do {
		/* .loop */
		if (GetTurnDuelistVariable((u8)(DUELVARS_CARD_LOCATIONS + e)) == b &&
		    GetCardIDFromDeckIndex(e) == c) {
			/* .found */
			*idx = e;
			return true;
		}
		/* .next */
		e++;
	} while (e != DECK_SIZE);

	/* not found */
	*idx = DECK_SIZE;
	return false;
}

/* counts total number of energy cards in opponent's hand
 * plus all the cards attached in Turn Duelist's Play Area.
 * output:
 *	a and wTempAI = total number of energy cards. */
u8 CountOppEnergyCardsInHandAndAttached(void)
{
	TRACE_FN();
	wTempAI = 0;
	if (!CreateEnergyCardListFromHand()) {
		/* counts number of energy cards in hand */
		u8 b = 0xff;
		const u8 *hl = wDuelTempList;
		do {
			b++;
		} while (*hl++ != 0xff);
		wTempAI = b;
	}

	/* .attached: counts number of energy cards
	 * that are attached in Play Area */
	u8 d = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	u8 e = PLAY_AREA_ARENA;
	do {
		u8 a = CountNumberOfEnergyCardsAttached(e);
		wTempAI = (u8)(a + wTempAI);
		e++;
	} while (--d);
	return wTempAI;
}

/* returns carry if any card with ID in e is found
 * in the list that is pointed by hl.
 * if one is found, it is removed from the list.
 * input:
 *   e  = card ID to look for.
 *   hl = list to look in */
bool RemoveCardIDInList(u8 e, u8 *hl, u8 *a)
{
	TRACE_FN();
	u8 c = e;
	u8 *p = hl;
	for (;;) {
		/* .loop_1 */
		u8 x = *p++;
		if (x == 0xff) {
			/* .no_carry */
			*a = x;
			return false;
		}
		hTempCardIndex_ff98 = x;
		if (GetCardIDFromDeckIndex(x) == c)
			break;
	}

	/* found */
	{
		const u8 *de = p;
		p--;
		/* remove this index from the list
		 * and reposition the rest of the list ahead. */
		u8 x;
		do {
			/* .loop_2 */
			x = *de++;
			*p++ = x;
		} while (x != 0xff);
	}
	*a = hTempCardIndex_ff98;
	return true;
}

/* runs through input card ID list in de.
 * plays to Play Area first card that is found in hand.
 * returns carry if none of the cards in the list are found.
 * returns number of Pokemon in Play Area in a.
 * (hl = hand list, list_ptr = the WRAM variable holding the GB address of the list) */
static bool TrySetUpBossStartingPlayArea_PlayPokemonCardInOrder(u8 *hl, const u8 *list_ptr, u8 *a)
{
	const u8 *de = AIListPtr(rd16(list_ptr));

	/* go in order of the list in de and
	 * add first card that matches ID.
	 * returns carry if hand doesn't have any card in list. */
	for (;;) {
		/* .loop_id_list */
		u8 x = *de++;
		if (x == 0) {
			/* .not_found */
			*a = x;
			return true;
		}
		u8 card;
		if (RemoveCardIDInList(x, hl, &card)) {
			/* play this card to Play Area and return
			 * (a = the new location, or the deck index if the play area is full;
			 * its carry is discarded by `or a`) */
			u8 loc;
			PutHandPokemonCardInPlayArea(card, &loc);
			*a = loc;
			return false;
		}
	}
}

/* play Pokemon cards from the hand to set the starting
 * Play Area of Boss decks.
 * each Boss deck has two ID lists in order of preference.
 * one list is for the Arena card is the other is for the Bench cards.
 * if Arena card could not be set (due to hand not having any card in its list)
 * or if list is null, return carry and do not play any cards. */
bool TrySetUpBossStartingPlayArea(void)
{
	TRACE_FN();
	u8 a;
	/* ld de, wAICardListArenaPriority / ld a, d / or a
	 * bug (original): tests the high byte of the address of wAICardListArenaPriority
	 * (never 0), not of the pointer stored there */
	if ((gbaddr(wAICardListArenaPriority) >> 8) == 0)
		return true; /* .set_carry: return if null */

	/* pick Arena card */
	CreateHandCardList(NULL);
	if (TrySetUpBossStartingPlayArea_PlayPokemonCardInOrder(wDuelTempList, wAICardListArenaPriority, &a))
		return true;

	/* play Pokemon cards to Bench until there are
	 * a maximum of 3 cards in Play Area. */
	do {
		/* .loop */
		if (TrySetUpBossStartingPlayArea_PlayPokemonCardInOrder(wDuelTempList, wAICardListBenchPriority, &a))
			break;
	} while (a < 3);

	/* .done */
	return false;
}

/* expects a $00-terminated list of 3-byte data with the following:
 * - non-zero value (anything but $1 is ignored)
 * - card ID to look for in Play Area
 * - number of energy cards
 * returns carry if a card ID is found in bench with at least the
 * listed number of energy cards
 * unreferenced */
bool Func_1585b(const u8 *hl, u8 *a)
{
	for (;;) {
		TRACE_FN(); /* (each iteration jumps back to Func_1585b in the original) */
		u8 x = *hl++;
		if (x == 0) {
			/* .no_carry */
			*a = x;
			return false;
		}
		x--;
		if (x != 0) {
			/* .next_1 */
			hl += 2;
			continue;
		}
		x = *hl++;
		u8 e;
		if (!LookForCardIDInPlayArea_Bank5(x, PLAY_AREA_BENCH_1, &e)) {
			/* .next_2 */
			hl++;
			continue;
		}
		x = CountNumberOfEnergyCardsAttached(e);
		if (x >= *hl) {
			/* .set_carry */
			*a = e;
			return true;
		}
		hl++;
	}
}

/* expects a $00-terminated list of 3-byte data with the following:
 * - non-zero value
 * - card ID
 * - number of energy cards
 * goes through the given list and if a card with a listed ID is found
 * with less than the number of energy cards corresponding to its entry
 * then have AI try to play an energy card from the hand to it
 * unreferenced */
bool Func_15886(const u8 *hl)
{
	TRACE_FN();
	if (CreateEnergyCardListFromHand())
		return true; /* quit if no energy cards in hand */

	for (;;) {
		/* .loop_energy_cards */
		u8 x = *hl++;
		if (x == 0)
			return false; /* done */
		x = *hl++;
		u8 e;
		if (!LookForCardIDInPlayArea_Bank5(x, PLAY_AREA_ARENA, &e)) {
			/* .next: skip if not found in Play Area */
			hl++;
			continue;
		}
		x = CountNumberOfEnergyCardsAttached(e);
		bool enough = x >= *hl;
		hl++;
		if (enough)
			continue;
		hTempPlayAreaLocation_ff9d = e;
		if (AITryToPlayEnergyCard())
			return true;
	}
}

/* ---- INCLUDE "engine/duel/ai/retreat.asm" (part C) */

/* copies an $ff-terminated list from hl to de.
 * preserves bc
 * input:
 *	hl = address from which to start copying the data
 *	de = where to copy the data */
void CopyListWithFFTerminatorFromHLToDE_Bank5(const u8 *hl, u8 *de)
{
	for (;;) {
		u8 a = *hl++;
		*de = a;
		if (a == 0xff)
			return;
		de++;
	}
}

/* ---- INCLUDE "engine/duel/ai/hand_pokemon.asm" (part C) */

/* check if player's active Pokémon is Mr Mime
 * if it isn't, set carry
 * if it is, check if Pokémon at a
 * can damage it, and if it can, set carry
 * input:
 *	a = location of Pokémon card */
bool CheckDamageToMrMime(u8 loc)
{
	TRACE_FN();
	u8 a = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD);
	SwapTurn();
	u8 e = GetCardIDFromDeckIndex(a);
	SwapTurn();
	if (e != MR_MIME)
		return true; /* .set_carry */
	if (CheckIfCanDamageDefendingPokemon(loc))
		return true; /* .set_carry */
	return false;
}

/* returns carry if arena card
 * can knock out defending Pokémon */
bool CheckIfActiveCardCanKnockOut(void)
{
	TRACE_FN();
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	if (!CheckIfAnyAttackKnocksOutDefendingCard())
		return false; /* .fail */
	if (CheckIfSelectedAttackIsUnusable())
		return false; /* .fail */
	return true;
}

/* return carry if the Arena Pokémon has any attack that is
 * usable and non-residual */
bool CanArenaCardUseNonResidualAttack(void)
{
	TRACE_FN();
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	wSelectedAttack = FIRST_ATTACK_OR_PKMN_POWER;
	if (!CheckIfSelectedAttackIsUnusable()) {
		if (!(wLoadedAttackCategory & RESIDUAL))
			return true; /* not Residual */
	}
	/* .check_second_attack */
	wSelectedAttack = SECOND_ATTACK;
	if (CheckIfSelectedAttackIsUnusable())
		return false;
	if (!(wLoadedAttackCategory & RESIDUAL))
		return true; /* not Residual */
	/* .no_carry: either both are Residual or
	 * all non-Residual attacks are unusable */
	return false;
}

/* looks for energy card(s) in hand depending on
 * what is needed for selected card, for both attacks
 *	- if one basic energy is required, look for that energy;
 *	- if one colorless is required, create a list at wDuelTempList
 *	  of all energy cards;
 *	- if two colorless are required, look for double colorless;
 * return carry if successful in finding card
 * input:
 *	[hTempPlayAreaLocation_ff9d] = location of Pokémon card */
bool LookForEnergyNeededInHand(void)
{
	TRACE_FN();
	u8 a, b, c, e;
	wSelectedAttack = FIRST_ATTACK_OR_PKMN_POWER;
	CheckEnergyNeededForAttack(&b, &c, &e);
	a = (u8)(b + c);
	if (a == 1)
		goto one_energy;
	if (a != 2)
		goto second_attack;
	if (c == 2)
		goto two_colorless;

second_attack:
	wSelectedAttack = SECOND_ATTACK;
	CheckEnergyNeededForAttack(&b, &c, &e);
	a = (u8)(b + c);
	if (a == 1)
		goto one_energy;
	if (a != 2)
		goto no_carry;
	if (c == 2)
		goto two_colorless;
no_carry:
	return false;

one_energy:
	if (b == 0)
		goto one_colorless;
	if (LookForCardIDInHandList_Bank5(e, &a))
		return true;
	goto no_carry;

one_colorless:
	if (CreateEnergyCardListFromHand())
		goto no_carry;
	return true;

two_colorless:
	if (LookForCardIDInHandList_Bank5(DOUBLE_COLORLESS_ENERGY, &a))
		return true;
	goto no_carry;
}

/* looks for energy card(s) in hand depending on
 * what is needed for selected card and attack
 *	- if one basic energy is required, look for that energy;
 *	- if one colorless is required, create a list at wDuelTempList
 *	  of all energy cards;
 *	- if two colorless are required, look for double colorless;
 * return carry if successful in finding card
 * input:
 *	[hTempPlayAreaLocation_ff9d] = location of Pokémon card
 *	[wSelectedAttack]         = selected attack to examine */
bool LookForEnergyNeededForAttackInHand(void)
{
	TRACE_FN();
	u8 a, b, c, e;
	CheckEnergyNeededForAttack(&b, &c, &e);
	a = (u8)(b + c);
	if (a == 1)
		goto one_energy;
	if (a != 2)
		goto done;
	if (c == 2)
		goto two_colorless;
done:
	return false;

one_energy:
	if (b == 0)
		goto one_colorless;
	if (LookForCardIDInHandList_Bank5(e, &a))
		return true;
	goto done;

one_colorless:
	if (CreateEnergyCardListFromHand())
		goto done;
	return true;

two_colorless:
	if (LookForCardIDInHandList_Bank5(DOUBLE_COLORLESS_ENERGY, &a))
		return true;
	goto done;
}

/* goes through $00 terminated list pointed
 * by wAICardListPlayFromHandPriority and compares it to each card in hand.
 * Sorts the hand in wDuelTempList so that the found card IDs
 * are in the same order as the list pointed by de. */
void SortTempHandByIDList(void)
{
	TRACE_FN();
	if (wAICardListPlayFromHandPriority[1] == 0)
		return; /* return if list is empty */

	/* start going down the ID list */
	const u8 *de = AIListPtr(rd16(wAICardListPlayFromHandPriority));
	u8 c = 0;
	for (;;) {
		/* .loop_list_id
		 * get this item's ID
		 * if $00, list has ended */
		u8 a = *de;
		if (a == 0)
			return; /* return when list is over */
		de++;
		u8 *hl = wDuelTempList + c;
		u8 b = a;

		/* search in the hand card list */
		for (;;) {
			/* .next_hand_card */
			a = *hl;
			hTempCardIndex_ff98 = a;
			if (a == 0xff)
				break; /* .loop_list_id */
			if (GetCardIDFromDeckIndex(a) == b) {
				/* found
				 * swap this hand card with the spot
				 * in hand corresponding to c */
				u8 *spot = wDuelTempList + c;
				u8 tmp = *spot;
				*spot = hTempCardIndex_ff98;
				*hl = tmp;
				c++;
			}
			/* .not_same */
			hl++;
		}
	}
}

/* looks for energy card(s) in list at wDuelTempList
 * depending on energy flags set in a
 * return carry if successful in finding card
 * input:
 *	a = energy flags needed */
bool CheckEnergyFlagsNeededInList(u8 flags)
{
	TRACE_FN();
	u8 e = flags;
	const u8 *hl = wDuelTempList;
	for (;;) {
		/* .loop_cards */
		u8 a = *hl++;
		if (a == 0xff)
			return false; /* .no_carry */
		a = GetCardIDFromDeckIndex(a);

		if (a == FIRE_ENERGY)
			a = FIRE_F;
		else if (a == GRASS_ENERGY)
			a = GRASS_F;
		else if (a == LIGHTNING_ENERGY)
			a = LIGHTNING_F;
		else if (a == WATER_ENERGY)
			a = WATER_F;
		else if (a == FIGHTING_ENERGY)
			a = FIGHTING_F;
		else if (a == PSYCHIC_ENERGY)
			a = PSYCHIC_F;
		else if (a == DOUBLE_COLORLESS_ENERGY)
			a = COLORLESS_F;
		else
			continue;

		/* .check_energy: if energy card matches required energy, return carry */
		if (a & e)
			return true;
	}
}

/* returns in a the energy cost of an attack in [hl]
 * represented by energy flags
 * i.e. each bit represents a different energy type cost
 * if any colorless energy is required, all bits are set
 * input:
 *	[hl] = Loaded card attack energy cost
 * output:
 *	a = bits of each energy requirement */
static u8 GetAttacksEnergyCostBits_GetEnergyCostBits(const u8 *hl)
{
	u8 c = 0, b;
	b = *hl++;
	/* fire */
	if (b & 0xf0)
		c = FIRE_F;
	/* .grass */
	if (b & 0x0f)
		c = (u8)(GRASS_F | c);
	/* .lightning */
	b = *hl++;
	if (b & 0xf0)
		c = (u8)(LIGHTNING_F | c);
	/* .water */
	if (b & 0x0f)
		c = (u8)(WATER_F | c);
	/* .fighting */
	b = *hl++;
	if (b & 0xf0)
		c = (u8)(FIGHTING_F | c);
	/* .psychic */
	if (b & 0x0f)
		c = (u8)(PSYCHIC_F | c);
	/* .colorless */
	b = *hl++;
	if (b & 0xf0)
		c = (u8)(0xff | c); /* or c ; unnecessary */
	/* .done */
	return c;
}

/* returns in a the energy cost of both attacks from card index in a
 * represented by energy flags
 * i.e. each bit represents a different energy type cost
 * if any colorless energy is required, all bits are set
 * input:
 *	a = card index
 * output:
 *	a = bits of each energy requirement */
u8 GetAttacksEnergyCostBits(u8 card)
{
	TRACE_FN();
	LoadCardDataToBuffer2_FromDeckIndex(card);
	u8 b = GetAttacksEnergyCostBits_GetEnergyCostBits(wLoadedCard2Atk1EnergyCost);
	u8 a = GetAttacksEnergyCostBits_GetEnergyCostBits(wLoadedCard2Atk2EnergyCost);
	return (u8)(a | b);
}

/* set carry flag if any card in
 * wDuelTempList evolves card index in a
 * if found, the evolution card index is returned in a
 * input:
 *	a = card index to check evolution
 * output:
 *	a = card index of evolution found */
bool CheckForEvolutionInList(u8 card, u8 *evolution)
{
	TRACE_FN();
	u8 *arena = TurnDuelistVars() + DUELVARS_ARENA_CARD;
	u8 saved = *arena;
	*arena = card;
	const u8 *hl = wDuelTempList;
	for (;;) {
		/* .loop */
		u8 d = *hl++;
		if (d == 0xff)
			break; /* .no_carry */
		if (CheckIfCanEvolveInto(d, PLAY_AREA_ARENA, NULL))
			continue;

		TurnDuelistVars()[DUELVARS_ARENA_CARD] = saved;
		*evolution = d;
		return true;
	}

	/* .no_carry */
	TurnDuelistVars()[DUELVARS_ARENA_CARD] = saved;
	return false;
}

/* set carry if it finds an evolution for
 * the card index in a in the deck
 * if found, return that evolution card index in a
 * input:
 *	a = card index to check evolution
 * output:
 *	a = card index of evolution found */
bool CheckForEvolutionInDeck(u8 card, u8 *evolution)
{
	TRACE_FN();
	u8 *arena = TurnDuelistVars() + DUELVARS_ARENA_CARD;
	u8 saved = *arena;
	*arena = card;
	u8 e = 0;
	do {
		/* .loop */
		if (GetTurnDuelistVariable((u8)(DUELVARS_CARD_LOCATIONS + e)) == CARD_LOCATION_DECK) {
			if (!CheckIfCanEvolveInto(e, PLAY_AREA_ARENA, NULL)) {
				/* .set_carry */
				TurnDuelistVars()[DUELVARS_ARENA_CARD] = saved;
				*evolution = e;
				return true;
			}
		}
		/* .not_in_deck: exit when it gets to the prize cards */
		e++;
	} while (e != DUELVARS_PRIZE_CARDS);

	TurnDuelistVars()[DUELVARS_ARENA_CARD] = saved;
	return false;
}

/* ---- INCLUDE "engine/duel/ai/energy.asm" (part C) */
/* ---- INCLUDE "engine/duel/ai/attacks.asm" (part C) */
/* ---- INCLUDE "engine/duel/ai/special_attacks.asm" (part C) */

/* checks in other Play Area for non-basic cards.
 * afterwards, that card is checked for damage,
 * and if the damage counters it has is greater than or equal
 * to the max HP of the card stage below it,
 * return carry and that card's Play Area location in a.
 * output:
 *	a = card location of Pokémon card, if found;
 *	carry set if such a card is found. */
bool LookForCardThatIsKnockedOutOnDevolution(u8 *a)
{
	TRACE_FN();
	u8 saved = hTempPlayAreaLocation_ff9d;
	SwapTurn();
	u8 b = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	u8 c = PLAY_AREA_ARENA;

	do {
		/* .loop */
		hTempPlayAreaLocation_ff9d = c;
		u8 d, e, max_hp;
		if (!GetCardOneStageBelow(&d, &e)) {
			/* is not a basic card
			 * compare its HP with current damage */
			LoadCardDataToBuffer2_FromDeckIndex(d);
			wTempAI = wLoadedCard2HP;
			e = GetCardDamageAndMaxHP(c, &max_hp);
			if (wTempAI <= e) {
				/* .set_carry */
				SwapTurn();
				hTempPlayAreaLocation_ff9d = saved;
				*a = c;
				return true;
			}
		}
		/* .next */
		c++;
	} while (c != b);

	SwapTurn();
	hTempPlayAreaLocation_ff9d = saved;
	*a = saved;
	return false;
}

/* returns carry if the following conditions are met:
 *	- arena card HP >= half max HP
 *	- arena card cannot potentially evolve
 *	- arena card can use second attack */
bool CheckIfArenaCardIsFullyPowered(void)
{
	TRACE_FN();
	u8 card = GetTurnDuelistVariable(DUELVARS_ARENA_CARD);
	LoadCardDataToBuffer1_FromDeckIndex(card);
	u8 d = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);
	if (Rrca(wLoadedCard1HP) >= d)
		return false; /* .no_carry */

	if (wLoadedCard1AIInfo & HAS_EVOLUTION) {
		u8 evolution;
		if (CheckCardEvolutionInHandOrDeck(card, &evolution))
			return false; /* .no_carry */
	}

	/* .check_second_attack */
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	wSelectedAttack = SECOND_ATTACK;
	if (CheckIfSelectedAttackIsUnusable())
		return false; /* .no_carry */
	return true;
}

/* count Pokemon in the Bench that
 * meet the following conditions:
 *	- card HP > half max HP
 *	- card Unknown2's 4 bit is not set or
 *	  is set but there's no evolution of card in hand/deck
 *	- card can use second attack
 * Outputs the number of Pokémon in bench
 * that meet these requirements in a
 * and returns carry if at least one is found */
u8 CountNumberOfSetUpBenchPokemon(void)
{
	TRACE_FN();
	u8 saved_loc = hTempPlayAreaLocation_ff9d;
	u8 saved_attack = wSelectedAttack;
	const u8 *hl = TurnDuelistVars() + DUELVARS_BENCH;
	u8 b = 0, c = 0;

	for (;;) {
		/* .next */
		c++;
		u8 a = *hl++;
		if (a == 0xff)
			break; /* .done */

		u8 d = a;
		LoadCardDataToBuffer1_FromDeckIndex(d);

		/* compares card's current HP with max HP */
		u8 hp = GetTurnDuelistVariable((u8)(c + DUELVARS_ARENA_CARD_HP));
		a = Rrca(wLoadedCard1HP);
		/* a = max HP / 2
		 * d = current HP
		 * jumps if (current HP) <= (max HP / 2) */
		if (a >= hp)
			continue;

		if (wLoadedCard1AIInfo & HAS_EVOLUTION) {
			u8 evolution;
			if (CheckCardEvolutionInHandOrDeck(d, &evolution))
				continue;
		}

		/* .check_second_attack */
		hTempPlayAreaLocation_ff9d = c;
		/* bug, there is an assumption that the card
		 * has a second attack, but it may be the case
		 * that it doesn't, which will return carry */
		wSelectedAttack = SECOND_ATTACK;
		if (CheckIfSelectedAttackIsUnusable())
			continue;
		b++;
	}

	/* .done */
	wSelectedAttack = saved_attack;
	hTempPlayAreaLocation_ff9d = saved_loc;
	return b;
}

static bool AISelectSpecialAttackParameters_SelectAttackParameters(void)
{
	u8 a, b;
	const u8 *hl;
	u8 e = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	if (e == MEW_LV23)
		goto DevolutionBeam;
	if (e == MEWTWO_ALT_LV60)
		goto EnergyAbsorption;
	if (e == MEWTWO_LV60)
		goto EnergyAbsorption;
	if (e == EXEGGUTOR)
		goto Teleport;
	if (e == ELECTRODE_LV35)
		goto EnergySpike;
	/* fallthrough */

no_carry:
	return false;

DevolutionBeam:
	/* in case selected attack is Devolution Beam
	 * store in hTempPlayAreaLocation_ffa1
	 * the location of card to select to devolve */
	if (wSelectedAttack == 0)
		goto no_carry; /* can be jr */

	hTemp_ffa0 = 0x01; /* always target the Player's play area */
	LookForCardThatIsKnockedOutOnDevolution(&a);
	hTempPlayAreaLocation_ffa1 = a;
	/* .set_carry_1 */
	return true;

EnergyAbsorption:
	/* in case selected attack is Energy Absorption
	 * make list from energy cards in Discard Pile */
	if (wSelectedAttack != 0)
		goto no_carry; /* can be jr */

	hTempPlayAreaLocation_ffa1 = 0xff;
	hTempRetreatCostCards[0] = 0xff;

	/* search for Psychic energy cards in Discard Pile */
	LookForCardIDInLocation_Bank5(CARD_LOCATION_DISCARD_PILE, PSYCHIC_ENERGY, &a);
	hTemp_ffa0 = a;
	CreateEnergyCardListFromDiscardPile_AllEnergy();

	/* find any energy card different from
	 * the one found by LookForCardIDInLocation_Bank5.
	 * since using this attack requires a Psychic energy card,
	 * and another one is in hTemp_ffa0,
	 * then any other energy card would account
	 * for the Energy Cost of Psyburn. */
	hl = wDuelTempList;
	for (;;) {
		/* .loop_energy_cards */
		a = *hl++;
		if (a == 0xff)
			break; /* .set_carry_2 */
		b = a;
		if (hTemp_ffa0 == b)
			continue; /* same card, keep looking */

		/* store the deck index of energy card found */
		hTempPlayAreaLocation_ffa1 = b;
		break;
	}
	/* .set_carry_2 */
	return true;

Teleport:
	/* in case selected attack is Teleport
	 * decide Bench card to switch to. */
	if (wSelectedAttack != 0)
		goto no_carry; /* can be jr */
	if (AIDecideBenchPokemonToSwitchTo(&a))
		goto no_carry;
	hTemp_ffa0 = a;
	return true;

EnergySpike:
	/* in case selected attack is Energy Spike
	 * decide basic energy card to fetch from Deck. */
	if (wSelectedAttack == 0)
		goto no_carry; /* can be jr */

	/* if none were found in Deck, return carry... */
	{
		bool found = LookForCardIDInLocation_Bank5(CARD_LOCATION_DECK, LIGHTNING_ENERGY, &a);
		hTemp_ffa0 = a;
		if (!found)
			goto no_carry; /* can be jr */
	}

	/* ...else find a suitable Play Area Pokemon to
	 * attach the energy card to. */
	if (!AIProcessButDontPlayEnergy_SkipEvolution())
		goto no_carry; /* can be jr */
	hTempPlayAreaLocation_ffa1 = hTempPlayAreaLocation_ff9d;
	return true;
}

/* handles AI logic to determine some selections regarding certain attacks,
 * if any of these attacks were chosen to be used.
 * returns carry if selection was successful,
 * and no carry if unable to make one.
 * outputs in hTempPlayAreaLocation_ffa1 the chosen parameter. */
bool AISelectSpecialAttackParameters(void)
{
	TRACE_FN();
	u8 saved = wSelectedAttack;
	bool carry = AISelectSpecialAttackParameters_SelectAttackParameters();
	wSelectedAttack = saved;
	return carry;
}

/* return carry if Pokémon at play area location
 * in hTempPlayAreaLocation_ff9d does not have
 * energy required for the attack index in wSelectedAttack
 * or has exactly the same amount of energy needed
 * input:
 *	[hTempPlayAreaLocation_ff9d] = play area location
 *	[wSelectedAttack]         = attack index to check
 * output:
 *	a = number of extra energy cards attached */
bool CheckIfNoSurplusEnergyForAttack(u8 *a)
{
	TRACE_FN();
	u8 x, b, c;
	CopyAttackDataAndDamage_FromDeckIndex(
		GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)), wSelectedAttack);
	x = (u8)(wLoadedAttackName[0] | wLoadedAttackName[1]);
	if (x == 0)
		goto not_attack;
	x = wLoadedAttackCategory;
	if (x != POKEMON_POWER)
		goto is_attack;
not_attack:
	*a = x;
	return true;

is_attack:
	GetPlayAreaCardAttachedEnergies(hTempPlayAreaLocation_ff9d);
	HandleEnergyBurn();
	wTempLoadedAttackEnergyCost = 0;
	wTempLoadedAttackEnergyNeededAmount = 0;
	wTempLoadedAttackEnergyNeededType = 0;
	{
		u8 *hl = wAttachedEnergies;
		const u8 *de = wLoadedAttackEnergyCost;
		b = 0;
		c = (NUM_TYPES / 2) - 1;
		do {
			/* check all basic energy cards except colorless */
			CalculateParticularAttachedEnergyNeeded(SwapNibbles(*de), &hl, &b);
			CalculateParticularAttachedEnergyNeeded(*de, &hl, &b);
			de++;
		} while (--c);

		/* colorless */
		b = (u8)(SwapNibbles(*de) & 0x0f);
	}
	x = (u8)(wTotalAttachedEnergies - wTempLoadedAttackEnergyCost);
	/* bug (original): only the borrow of the second `sub` is tested, so if
	 * wTotalAttachedEnergies < wTempLoadedAttackEnergyCost the wrapped result is
	 * usually taken as "surplus energy" (no carry) */
	{
		bool carry = x < b;
		x = (u8)(x - b);
		*a = x;
		if (carry)
			return true; /* return if not enough energy */
	}
	if (x != 0)
		return false; /* return if surplus energy */

	/* exactly the amount of energy needed */
	return true;
}

/* takes as input the energy cost of an attack for a
 * particular energy, stored in the lower nibble of a
 * if the attack costs some amount of this energy, the lower nibble of a != 0,
 * and this amount is stored in wTempLoadedAttackEnergyCost
 * also adds the amount of energy still needed
 * to wTempLoadedAttackEnergyNeededAmount
 * input:
 *	a    = this energy cost of attack (lower nibble)
 *	[hl] = attached energy
 * output:
 *	carry set if not enough of this energy type attached */
void CalculateParticularAttachedEnergyNeeded(u8 a, u8 **hl, u8 *b)
{
	a &= 0x0f;
	if (a != 0) {
		/* .check */
		wTempLoadedAttackEnergyCost = a;
		u8 attached = **hl;
		/* sub [hl] / jr z, .done / jr nc, .done
		 * (so this is only reached when more energy than the cost is attached,
		 * and it stores attached - cost) */
		if (!(a == attached || a > attached))
			wTempLoadedAttackEnergyNeededAmount = (u8)(attached - wTempLoadedAttackEnergyCost);
	}
	/* .done */
	(*hl)++;
	(*b)++;
}

/* return carry if there is a card that
 * can evolve a Pokémon in hand or deck.
 * input:
 *	a = deck index of card to check;
 * output:
 *	a = deck index of evolution in hand, if found;
 *	carry set if there's a card in hand that can evolve. */
bool CheckCardEvolutionInHandOrDeck(u8 card, u8 *evolution)
{
	TRACE_FN();
	u8 *arena = TurnDuelistVars() + DUELVARS_ARENA_CARD;
	u8 saved = *arena;
	*arena = card;
	u8 e = 0;

	do {
		/* .loop */
		u8 a = GetTurnDuelistVariable((u8)(DUELVARS_CARD_LOCATIONS + e));
		if (a == CARD_LOCATION_DECK || a == CARD_LOCATION_HAND) {
			/* .deck_or_hand */
			if (!CheckIfCanEvolveInto(e, PLAY_AREA_ARENA, NULL)) {
				/* .set_carry */
				TurnDuelistVars()[DUELVARS_ARENA_CARD] = saved;
				*evolution = e;
				return true;
			}
		}
		/* .next */
		e++;
	} while (e != DECK_SIZE);

	TurnDuelistVars()[DUELVARS_ARENA_CARD] = saved;
	return false;
}

/* ---- INCLUDE "engine/duel/ai/boss_deck_set_up.asm" (ai/boss_deck_set_up.c) */

/* returns carry if Pokemon at PLAY_AREA* in a
 * can damage defending Pokémon with any of its attacks
 * input:
 *	a = location of card to check */
bool CheckIfCanDamageDefendingPokemon(u8 loc)
{
	TRACE_FN();
	hTempPlayAreaLocation_ff9d = loc;
	wSelectedAttack = FIRST_ATTACK_OR_PKMN_POWER;
	if (!CheckIfSelectedAttackIsUnusable()) {
		EstimateDamage_VersusDefendingCard(FIRST_ATTACK_OR_PKMN_POWER);
		if (wDamage[0] != 0)
			return true; /* .set_carry */
	}

	/* .second_attack */
	wSelectedAttack = SECOND_ATTACK;
	if (CheckIfSelectedAttackIsUnusable())
		return false; /* .no_carry */
	EstimateDamage_VersusDefendingCard(SECOND_ATTACK);
	if (wDamage[0] != 0)
		return true; /* .set_carry */

	/* .no_carry */
	return false;
}

/* return carry if defending Pokémon can knock out
 * card at hTempPlayAreaLocation_ff9d
 * input:
 *	a = attack index
 *	[hTempPlayAreaLocation_ff9d] = location of card to check */
static bool CheckIfDefendingPokemonCanKnockOut_CheckAttack(u8 a)
{
	wSelectedAttack = a;
	u8 saved = hTempPlayAreaLocation_ff9d;
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	SwapTurn();
	bool unusable = CheckIfSelectedAttackIsUnusable();
	SwapTurn();
	hTempPlayAreaLocation_ff9d = saved;
	if (unusable)
		return false; /* .done */

	/* player's active Pokémon can use attack */
	EstimateDamage_FromDefendingPokemon(wSelectedAttack);
	u8 hp = GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD_HP));
	if (hp == wDamage[0])
		return true; /* .can_ko */
	/* ret with the carry of `sub [hl]`: carry if the damage is larger than the HP */
	return hp < wDamage[0];
}

/* checks if defending Pokémon can knock out
 * card at hTempPlayAreaLocation_ff9d with any of its attacks
 * and if so, stores the damage to wAIFirstAttackDamage and wAISecondAttackDamage
 * sets carry if any on the attacks knocks out
 * also outputs the largest damage dealt in a
 * input:
 *	[hTempPlayAreaLocation_ff9d] = location of card to check
 * output:
 *	a = largest damage of both attacks
 *	carry set if can knock out */
bool CheckIfDefendingPokemonCanKnockOut(u8 *damage)
{
	TRACE_FN();
	u8 a, b;
	wAIFirstAttackDamage = 0;
	wAISecondAttackDamage = 0;

	/* first attack */
	if (CheckIfDefendingPokemonCanKnockOut_CheckAttack(FIRST_ATTACK_OR_PKMN_POWER))
		wAIFirstAttackDamage = wDamage[0];

	/* .second_attack */
	if (!CheckIfDefendingPokemonCanKnockOut_CheckAttack(SECOND_ATTACK)) {
		/* .return_if_neither_kos */
		a = wAIFirstAttackDamage;
		if (a == 0) {
			*damage = a;
			return false;
		}
	} else {
		wAISecondAttackDamage = wDamage[0];
	}

	/* .compare */
	b = wAIFirstAttackDamage;
	a = wAISecondAttackDamage;
	if (a < b)
		a = b; /* wAIFirstAttackDamage < wAISecondAttackDamage otherwise */
	/* .set_carry */
	*damage = a;
	return true;
}

/* sets carry if Opponent's deck ID
 * is between LEGENDARY_MOLTRES_DECK_ID (inclusive)
 * and MUSCLES_FOR_BRAINS_DECK_ID (exclusive)
 * these are the decks for Grandmaster/Club Master/Ronald */
bool CheckIfOpponentHasBossDeckID(void)
{
	TRACE_FN();
	u8 a = wOpponentDeckID;
	if (a == CAT_DUO_DECK_ID) return true;
	if (a < LEGENDARY_MOLTRES_DECK_ID)
		return false; /* .no_carry */
	if (a >= MUSCLES_FOR_BRAINS_DECK_ID)
		return false; /* .no_carry */
	return true;
}

/* sets carry if not a boss fight
 * and if hasn't received legendary cards yet */
bool CheckIfNotABossDeckID(void)
{
	TRACE_FN();
	/* (EnableSRAM / ld a, [sReceivedLegendaryCards] / DisableSRAM) */
	if (GetSRAMReceivedLegendaryCards() != 0)
		return false; /* .no_carry */
	if (!CheckIfOpponentHasBossDeckID())
		return true; /* .set_carry */
	/* .no_carry */
	return false;
}

/* probability to return carry:
 * - 50% if deck AI is playing is on the list;
 * - 25% for all other decks;
 * - 0% for boss decks.
 * used for certain decks to randomly choose
 * not to play Trainer card or use PKMN Power */
bool AIChooseRandomlyNotToDoAction(void)
{
	TRACE_FN();
	/* boss decks always use Trainer cards. */
	if (!CheckIfNotABossDeckID())
		return false;

	/* .check_deck */
	u8 a = wOpponentDeckID;
	if (a == MUSCLES_FOR_BRAINS_DECK_ID)
		goto carry_50_percent;
	if (a == BLISTERING_POKEMON_DECK_ID)
		goto carry_50_percent;
	if (a == WATERFRONT_POKEMON_DECK_ID)
		goto carry_50_percent;
	if (a == BOOM_BOOM_SELFDESTRUCT_DECK_ID)
		goto carry_50_percent;
	if (a == KALEIDOSCOPE_DECK_ID)
		goto carry_50_percent;
	if (a == RESHUFFLE_DECK_ID)
		goto carry_50_percent;

	/* carry 25 percent */
	return Random(4) < 1;

carry_50_percent:
	return Random(4) < 2;
}

/* checks if any bench Pokémon has same ID
 * as input, and sets carry if it has more than
 * half health and can use its second attack
 * input:
 *	a = card ID to check for
 * output:
 *	carry set if the above requirements are met */
bool CheckForBenchIDAtHalfHPAndCanUseSecondAttack(u8 card_id)
{
	TRACE_FN();
	wSamePokemonCardID = card_id;
	u8 saved_loc = hTempPlayAreaLocation_ff9d;
	u8 saved_attack = wSelectedAttack;
	/* bug (original): the deck indices are read starting from the arena card
	 * while c (used for the HP and as the location to check) starts at
	 * PLAY_AREA_BENCH_1, so each card is checked against the HP and attack
	 * of the Pokemon in the next play area slot */
	const u8 *hl = TurnDuelistVars() + DUELVARS_ARENA_CARD;
	u8 b = 0, c = PLAY_AREA_ARENA;

	for (;;) {
		/* .loop */
		c++;
		u8 a = *hl++;
		if (a == 0xff)
			break; /* .done */
		LoadCardDataToBuffer1_FromDeckIndex(a);
		u8 d = GetTurnDuelistVariable((u8)(c + DUELVARS_ARENA_CARD_HP));
		if (Rrca(wLoadedCard1HP) >= d)
			continue;
		/* half max HP < current HP */
		if (wLoadedCard1ID != wSamePokemonCardID)
			continue;

		hTempPlayAreaLocation_ff9d = c;
		wSelectedAttack = SECOND_ATTACK;
		if (CheckIfSelectedAttackIsUnusable())
			continue;
		b++;
		break; /* .done */
	}

	/* .done */
	wSelectedAttack = saved_attack;
	hTempPlayAreaLocation_ff9d = saved_loc;
	return b != 0;
}

/* add 5 to wPlayAreaEnergyAIScore AI score corresponding to all cards
 * in bench that have same ID as register a
 * input:
 *	a = card ID to look for */
void RaiseAIScoreToAllMatchingIDsInBench(u8 card_id)
{
	TRACE_FN();
	u8 d = card_id;
	const u8 *hl = TurnDuelistVars() + DUELVARS_BENCH;
	u8 e = 0;
	for (;;) {
		/* .loop */
		e++;
		u8 a = *hl++;
		if (a == 0xff)
			return;
		if (GetCardIDFromDeckIndex(a) != d)
			continue;
		wPlayAreaEnergyAIScore[e] = (u8)(5 + wPlayAreaEnergyAIScore[e]);
	}
}

/* loads wSamePokemonEnergyScore + play area location in e
 * with energy  * 2 + $80 - floor(dam / 10)
 * loads wSamePokemonEnergyScoreHandled + play area location in e
 * with $01 */
static void HandleAIEnergyScoringForRepeatedBenchPokemon_CalculateScore(u8 e)
{
	u8 max_hp;
	u8 a = GetCardDamageAndMaxHP(e, &max_hp);
	u8 b = ConvertHPToDamageCounters_Bank5(a);
	a = CountNumberOfEnergyCardsAttached(e);
	a = (u8)(a << 1);
	a = (u8)(a + 0x80);
	a = (u8)(a - b);
	wSamePokemonEnergyScore[e] = a;
	wSamePokemonEnergyScoreHandled[e] = 0x01;
}

/* counts how many play area locations in wSamePokemonEnergyScore
 * are != 0, and outputs result in a
 * also returns carry if result is < 2 */
static bool HandleAIEnergyScoringForRepeatedBenchPokemon_CountNumberOfCardsWithSameID(void)
{
	const u8 *hl = wSamePokemonEnergyScore;
	u8 d = 0;
	u8 e = MAX_PLAY_AREA_POKEMON + 1;
	for (;;) {
		/* .loop */
		if (--e == 0)
			break; /* .done */
		if (*hl++ == 0)
			continue;
		d++;
	}
	return d < 2;
}

/* used by AI to determine which Pokémon it should favor in the bench
 * in order to attach an energy card from the hand, in case there are repeats
 * if there is repeated Pokémon in bench, then increase wPlayAreaEnergyAIScore
 * from the Pokémon with less damage and more energy cards,
 * and decrease from all others */
void HandleAIEnergyScoringForRepeatedBenchPokemon(void)
{
	TRACE_FN();
	/* clears wSamePokemonEnergyScoreHandled */
	ClearMemory_Bank5(MAX_PLAY_AREA_POKEMON, wSamePokemonEnergyScoreHandled);

	const u8 *hl = TurnDuelistVars() + DUELVARS_BENCH;
	u8 e = 0;
	for (;;) {
		/* .loop_bench
		 * clears wSamePokemonEnergyScore */
		ClearMemory_Bank5(MAX_PLAY_AREA_POKEMON, wSamePokemonEnergyScore);

		e++;
		u8 a = *hl++;
		if (a == 0xff)
			return; /* done looping bench */

		wSamePokemonCardID = a; /* deck index */

		/* checks wSamePokemonEnergyScoreHandled of location in e
		 * if != 0, go to next in play area */
		if (wSamePokemonEnergyScoreHandled[e] != 0)
			continue; /* already handled */

		/* store this card's ID */
		wSamePokemonCardID = GetCardIDFromDeckIndex(wSamePokemonCardID);

		/* calculate score of this Pokémon
		 * and all cards with same ID
		 * (push hl / push de: the search below uses copies) */
		{
			const u8 *hl2 = hl;
			u8 e2 = e;
			HandleAIEnergyScoringForRepeatedBenchPokemon_CalculateScore(e2);
			for (;;) {
				/* .loop_search_same_card_id */
				e2++;
				a = *hl2++;
				if (a == 0xff)
					break; /* .tally_repeated_pokemon */
				if (GetCardIDFromDeckIndex(a) != wSamePokemonCardID)
					continue;
				HandleAIEnergyScoringForRepeatedBenchPokemon_CalculateScore(e2);
			}
		}

		/* .tally_repeated_pokemon */
		if (HandleAIEnergyScoringForRepeatedBenchPokemon_CountNumberOfCardsWithSameID())
			continue; /* .next */

		/* has repeated card IDs in bench
		 * find which one has highest score */
		{
			u8 b = 0, c = 0;
			const u8 *p = wSamePokemonEnergyScore + PLAY_AREA_BENCH_5;
			u8 d = PLAY_AREA_BENCH_5 + 1;
			for (;;) {
				/* .loop_2 */
				if (--d == 0)
					break; /* .got_highest_score */
				a = *p--;
				if (a < b)
					continue;
				b = a; /* highest score */
				c = d; /* play area location */
			}

			/* .got_highest_score
			 * c = play area location of highest score
			 * increase wPlayAreaEnergyAIScore score for card with highest ID
			 * decrease wPlayAreaEnergyAIScore score for all cards with same ID */
			u8 *score = wPlayAreaEnergyAIScore;
			const u8 *same = wSamePokemonEnergyScore;
			b = PLAY_AREA_ARENA;
			for (;;) {
				/* .loop_3 */
				if (c == b) {
					/* .card_with_highest: increase score */
					*score = (u8)(*score + 1);
				} else if (*same != 0) {
					/* decrease score */
					*score = (u8)(*score - 1);
				}
				/* .check_next */
				b++;
				if (b == MAX_PLAY_AREA_POKEMON)
					break; /* .next */
				same++;
				score++;
			}
		}
		/* .next (pop de / pop hl / jp .loop_bench) */
	}
}

/* handle how AI scores giving out Energy Cards
 * when using Legendary Articuno deck */
void HandleLegendaryArticunoEnergyScoring(void)
{
	TRACE_FN();
	if (wOpponentDeckID != LEGENDARY_ARTICUNO_DECK_ID)
		return;
	/* .articuno_deck */
	ScoreLegendaryArticunoCards();
}
