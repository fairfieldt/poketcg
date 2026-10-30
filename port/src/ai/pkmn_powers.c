/*
 * Port of src/engine/duel/ai/pkmn_powers.asm (part D).
 *
 * AIMakeDecision's carry is ignored wherever the original ignores it; the
 * DoFrame delay loops are kept with the same number of frames.
 */
#include "ai/ai.h"

/* local labels of the routines below, in the asm order */
static bool HandleAIEnergyTrans_CheckEnoughGrassEnergyCardsForAttack(u8 *a);
static u8 HandleAIEnergyTrans_CountGrassEnergyInBench(void);
static bool HandleAIEnergyTrans_CheckEnoughGrassEnergyCardsForRetreatCost(u8 *a);
static bool HandleAIHeal_CheckHealTarget(u8 *a);
static bool HandleAIShift_CheckWhetherTurnDuelistHasColor(void);
static bool HandleAICowardice_CheckWhetherToUseCowardice(u8 c);
static bool HandleAIDamageSwap_CheckForDamageSwapTargetInBench(u8 *a);

/* handle AI routines for Energy Trans.
 * uses AI_ENERGY_TRANS_* constants as input:
 *	- AI_ENERGY_TRANS_RETREAT: transfers enough Grass Energy cards to
 *	Arena Pokemon for it to be able to pay the Retreat Cost;
 *	- AI_ENERGY_TRANS_ATTACK: transfers enough Grass Energy cards to
 *	Arena Pokemon for it to be able to use its second attack;
 *	- AI_ENERGY_TRANS_TO_BENCH: transfers all Grass Energy cards from
 *	Arena Pokemon to Bench in case Arena card will be KO'd. */
void HandleAIEnergyTrans(u8 mode)
{
	TRACE_FN();
	u8 a, b, d, e, card;

	wce06 = mode;

	/* choose to randomly return */
	if (AIChooseRandomlyNotToDoAction())
		return;

	if ((u8)(GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) - 1) == 0)
		return; /* return if no Bench cards */

	if (!CountTurnDuelistPokemonWithActivePkmnPower(VENUSAUR_LV67))
		return; /* return if no VenusaurLv67 found in own Play Area */

	if (CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK))
		return; /* return if Muk found in any Play Area */

	a = wce06;
	if (a == AI_ENERGY_TRANS_RETREAT)
		goto check_retreat;

	if (a == AI_ENERGY_TRANS_TO_BENCH) {
		AIEnergyTransTransferEnergyToBench();
		return;
	}

	/* AI_ENERGY_TRANS_ATTACK */
	if (!HandleAIEnergyTrans_CheckEnoughGrassEnergyCardsForAttack(&a))
		return;
	goto TransferEnergyToArena;

check_retreat:
	if (!HandleAIEnergyTrans_CheckEnoughGrassEnergyCardsForRetreatCost(&a))
		return;

	/* use Energy Trans to transfer number of Grass energy cards
	 * equal to input a from the Bench to the Arena card. */
TransferEnergyToArena:
	wAINumberOfEnergyTransCards = a;

	/* look for VenusaurLv67 in Play Area
	 * so that its PKMN Power can be used. */
	b = (u8)(GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) - 1);
	for (;;) {
		/* .loop_play_area */
		card = GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + b));
		hTempCardIndex_ff9f = card;
		if (GetCardIDFromDeckIndex(card) == VENUSAUR_LV67)
			break;
		if (b == 0)
			return; /* return when finished Play Area loop */
		b--;
	}

	/* use Energy Trans Pkmn Power */
	hTemp_ffa0 = b;
	AIMakeDecision(OPPACTION_USE_PKMN_POWER);
	AIMakeDecision(OPPACTION_EXECUTE_PKMN_POWER_EFFECT);

	hAIEnergyTransPlayAreaLocation = PLAY_AREA_ARENA;
	d = wAINumberOfEnergyTransCards;

	/* look for Grass energy cards that
	 * are currently attached to a Bench card. */
	e = 0;
	do {
		/* .loop_deck_locations */
		a = GetTurnDuelistVariable((u8)(DUELVARS_CARD_LOCATIONS + e)) & 0x1f;
		if (a < CARD_LOCATION_BENCH_1)
			continue; /* .next_card */

		hTempPlayAreaLocation_ffa1 = a & 0x0f;

		if (GetCardIDFromDeckIndex(e) != GRASS_ENERGY)
			continue; /* .next_card */

		/* store the deck index of energy card */
		hAIEnergyTransEnergyCard = e;

		for (u8 i = 30; i != 0; i--) /* .small_delay_loop */
			DoFrame();

		AIMakeDecision(OPPACTION_6B15);
		if (--d == 0)
			break; /* .done_transfer */
	} while (++e != DECK_SIZE); /* .next_card */

	/* transfer is done, perform delay
	 * and return to main scene. */
	for (u8 i = 60; i != 0; i--) /* .big_delay_loop */
		DoFrame();
	AIMakeDecision(OPPACTION_DUEL_MAIN_SCENE);
}

/* checks if the Arena card needs energy for its second attack,
 * and if it does, return carry if transferring Grass energy from Bench
 * would be enough to use it. Outputs number of energy cards needed in a
 * (*a only written on carry: the caller returns otherwise). */
static bool HandleAIEnergyTrans_CheckEnoughGrassEnergyCardsForAttack(u8 *a)
{
	u8 b, c, e, n;

	if (GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD)) == EXEGGUTOR)
		goto is_exeggutor;

	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	wSelectedAttack = SECOND_ATTACK;
	if (!CheckEnergyNeededForAttack(&b, &c, &e))
		goto attack_false; /* return if no energy needed */

	/* check if colorless energy is needed... */
	if (c != 0)
		goto count_if_enough;

	/* ...otherwise check if basic energy card is needed
	 * and it's grass energy. */
	if (b == 0)
		goto attack_false;
	if (e != GRASS_ENERGY)
		goto attack_false;
	c = b;
	goto count_if_enough;

attack_false:
	return false;

count_if_enough:
	/* if there's enough Grass energy cards in Bench
	 * to satisfy the attack energy cost, return carry. */
	if (HandleAIEnergyTrans_CountGrassEnergyInBench() < c)
		goto attack_false;
	*a = c;
	return true;

is_exeggutor:
	/* in case it's Exeggutor in Arena, return carry
	 * if there are any Grass energy cards in Bench. */
	n = HandleAIEnergyTrans_CountGrassEnergyInBench();
	if (n == 0)
		goto attack_false;
	*a = n;
	return true;
}

/* outputs in a the number of Grass energy cards
 * currently attached to Bench cards. */
static u8 HandleAIEnergyTrans_CountGrassEnergyInBench(void)
{
	u8 d = 0, e = 0;
	do {
		/* .count_loop */
		if ((GetTurnDuelistVariable((u8)(DUELVARS_CARD_LOCATIONS + e)) & 0x1f) < CARD_LOCATION_BENCH_1)
			continue; /* .count_next */

		/* is in bench */
		if (GetCardIDFromDeckIndex(e) != GRASS_ENERGY)
			continue; /* .count_next */
		d++;
	} while (++e != DECK_SIZE); /* .count_next */
	return d;
}

/* returns carry if there are enough Grass energy cards in Bench
 * to satisfy the retreat cost of the Arena card.
 * if so, output the number of energy cards still needed in a
 * (*a only written on carry). */
static bool HandleAIEnergyTrans_CheckEnoughGrassEnergyCardsForRetreatCost(u8 *a)
{
	u8 b, c, n;

	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	b = GetPlayAreaCardRetreatCost();
	n = CountNumberOfEnergyCardsAttached(PLAY_AREA_ARENA);
	if (n >= b)
		return false; /* return if enough to retreat */

	/* see if there's enough Grass energy cards
	 * in the Bench to satisfy retreat cost */
	c = (u8)(b - n);
	if (HandleAIEnergyTrans_CountGrassEnergyInBench() < c)
		return false; /* return if less cards than needed */

	/* output number of cards needed to retreat */
	*a = c;
	return true;
}

/* AI logic to determine whether to use Energy Trans Pkmn Power
 * to transfer energy cards attached from the Arena Pokemon to
 * some card in the Bench. */
void AIEnergyTransTransferEnergyToBench(void)
{
	TRACE_FN();
	u8 b, e, card, damage;

	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	if (!CheckIfDefendingPokemonCanKnockOut(&damage))
		return; /* return if Defending can't KO */

	/* processes attacks and see if any attack would be used by AI.
	 * if so, return. */
	if (AIProcessButDontUseAttack())
		return;

	/* return if Arena card has no Grass energy cards attached. */
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	if (wAttachedEnergies[GRASS] == 0)
		return;

	/* if no energy card attachment is needed, return. */
	if (!AIProcessButDontPlayEnergy_SkipEvolutionAndArena())
		return;

	/* AI decided that an energy card is needed
	 * so look for VenusaurLv67 in Play Area
	 * so that its PKMN Power can be used. */
	b = (u8)(GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) - 1);
	for (;;) {
		/* .loop_play_area */
		card = GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + b));
		hTempCardIndex_ff9f = card;
		wAIVenusaurLv67DeckIndex = card;
		if (GetCardIDFromDeckIndex(card) == VENUSAUR_LV67)
			break;
		if (b == 0)
			return; /* return when Play Area loop is ended */
		b--;
	}

	/* use Energy Trans Pkmn Power */
	hTemp_ffa0 = b;
	wAIVenusaurLv67PlayAreaLocation = b;
	AIMakeDecision(OPPACTION_USE_PKMN_POWER);
	AIMakeDecision(OPPACTION_EXECUTE_PKMN_POWER_EFFECT);

	/* loop for each energy cards that are going to be transferred. */
	for (;;) {
		/* .loop_energy */
		hTempPlayAreaLocation_ffa1 = 0;
		hTemp_ffa0 = wAIVenusaurLv67PlayAreaLocation;

		/* returns when Arena card has no Grass energy cards attached. */
		GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
		if (wAttachedEnergies[GRASS] == 0)
			goto done_transfer;

		/* look for Grass energy cards that
		 * are currently attached to Arena card. */
		e = 0;
		for (;;) {
			/* .loop_deck_locations */
			if (GetTurnDuelistVariable((u8)(DUELVARS_CARD_LOCATIONS + e)) == CARD_LOCATION_ARENA
			    && GetCardIDFromDeckIndex(e) == GRASS_ENERGY) {
				/* store the deck index of energy card */
				hAIEnergyTransEnergyCard = e;
				break; /* .transfer */
			}
			/* .next_card */
			if (++e == DECK_SIZE)
				goto done_transfer;
		}

		/* .transfer */
		/* get the Bench card location to transfer Grass energy card to. */
		if (!AIProcessButDontPlayEnergy_SkipEvolutionAndArena())
			goto done_transfer;
		hAIEnergyTransPlayAreaLocation = hTempPlayAreaLocation_ff9d;

		for (u8 i = 30; i != 0; i--) /* .small_delay_loop */
			DoFrame();

		card = wAIVenusaurLv67DeckIndex;
		hTempCardIndex_ff9f = card;
		CopyAttackDataAndDamage_FromDeckIndex(card, FIRST_ATTACK_OR_PKMN_POWER);
		AIMakeDecision(OPPACTION_6B15);
	}

	/* transfer is done, perform delay
	 * and return to main scene. */
done_transfer:
	for (u8 i = 60; i != 0; i--) /* .big_delay_loop */
		DoFrame();
	AIMakeDecision(OPPACTION_DUEL_MAIN_SCENE);
}

/* handles AI logic for using some Pkmn Powers.
 * Pkmn Powers handled here are:
 *	- Heal;
 *	- Shift;
 *	- Peek;
 *	- Strange Behavior;
 *	- Curse.
 * returns carry if turn ended. */
bool HandleAIPkmnPowers(void)
{
	TRACE_FN();
	u8 b, c, card, id;

	if (CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK))
		return false; /* return no carry if Muk is in play */

	if (AIChooseRandomlyNotToDoAction())
		return false; /* return no carry if AI randomly decides to */

	b = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	c = PLAY_AREA_ARENA;
	if (GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS) & CNF_SLP_PRZ)
		goto next_2;

	for (;;) {
		/* .loop_play_area */
		card = GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + c));
		wce08[0] = card;

		hTempPlayAreaLocation_ff9d = c;
		CopyAttackDataAndDamage_FromDeckIndex(card, FIRST_ATTACK_OR_PKMN_POWER);
		if (wLoadedAttack.Category != POKEMON_POWER)
			goto next_2; /* .next_3 */

		/* .execute_effect */
		if (TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_2))
			goto next_2; /* .next_3 */

		/* TryExecuteEffectCommandFunction was successful,
		 * so check what Pkmn Power this is through card's ID. */
		id = GetCardIDFromDeckIndex(card);

		if (id == VILEPLUME) {
			/* heal */
			HandleAIHeal(c);
		} else if (id == VENOMOTH) {
			/* .shift */
			HandleAIShift(c);
		} else if (id == MANKEY) {
			/* .peek */
			HandleAIPeek(c);
		} else if (id == SLOWBRO) {
			/* .strange_behavior */
			HandleAIStrangeBehavior(c);
		} else if (id == GENGAR) {
			/* .curse */
			if (HandleAICurse(c))
				return true; /* .done */
		}

		/* .next_1 */
	next_2:
		c++;
		if (c == b)
			return false;
	}
}

/* checks whether AI uses Heal on Pokemon in Play Area.
 * input:
 *	c = Play Area location (PLAY_AREA_*) of Vileplume. */
void HandleAIHeal(u8 c)
{
	TRACE_FN();
	u8 a;

	hTemp_ffa0 = c;
	if (!HandleAIHeal_CheckHealTarget(&a))
		return; /* return if no target to heal */
	hTempCardIndex_ff9f = wce08[0];
	AIMakeDecision(OPPACTION_USE_PKMN_POWER);
	hPlayAreaEffectTarget = a;
	AIMakeDecision(OPPACTION_EXECUTE_PKMN_POWER_EFFECT);
	AIMakeDecision(OPPACTION_DUEL_MAIN_SCENE);
}

/* finds a target suitable for AI to use Heal on.
 * only heals Arena card if the Defending Pokemon
 * cannot KO it after Heal is used.
 * returns carry if target was found and outputs
 * in a the Play Area location of that card. */
static bool HandleAIHeal_CheckHealTarget(u8 *a)
{
	u8 b, c, d, e, h, l, dmg, max_hp;

	/* check if Arena card has any damage counters,
	 * if not, check Bench instead. */
	if (GetCardDamageAndMaxHP(PLAY_AREA_ARENA, &max_hp) == 0)
		goto check_bench;

	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	if (!CheckIfDefendingPokemonCanKnockOut(&d))
		goto set_carry; /* return carry if can't KO */
	h = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);
	dmg = GetCardDamageAndMaxHP(PLAY_AREA_ARENA, &max_hp);
	/* this seems useless since it was already
	 * checked that Arena card has damage,
	 * so card damage is at least 10. */
	if (dmg >= 10 + 1)
		dmg = 10;
	/* a = min(10, CardDamage) */

	/* checks if Defending Pokemon can still KO
	 * if Heal is used on this card.
	 * if Heal prevents KO, return carry. */
	/* .check_remaining */
	l = dmg;
	dmg = (u8)(h + l); /* load remaining HP, add 1 counter to account for heal */
	if (dmg < d)       /* subtract damage of strongest opponent attack */
		goto check_bench;
	if (dmg == d)
		goto check_bench;

set_carry:
	*a = PLAY_AREA_ARENA;
	return true;

	/* check Bench for Pokemon with damage counters
	 * and find the one with the most damage. */
check_bench:
	d = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	b = 0;
	c = 0;
	e = PLAY_AREA_BENCH_1;
	for (;;) {
		/* .loop_bench */
		if (e == d)
			break;
		dmg = GetCardDamageAndMaxHP(e, &max_hp);
		if (dmg > b) {
			b = dmg; /* store this damage */
			c = e;   /* store this Play Area location */
		}
		/* .next_bench */
		e++;
	}

	/* check if a Pokemon with damage counters was found
	 * in the Bench and, if so, return carry. */
	/* .done */
	*a = c;
	if (c == 0)
		return false; /* .not_found */
	/* found */
	return true;
}

/* checks whether AI uses Shift.
 * input:
 *	c = Play Area location (PLAY_AREA_*) of Venomoth */
void HandleAIShift(u8 c)
{
	TRACE_FN();
	u8 a, b;
	bool has_color;

	if (c != 0)
		return; /* return if Venomoth is not Arena card */

	hTemp_ffa0 = c;
	b = TranslateColorToWR(GetArenaCardColor());
	SwapTurn();
	a = GetArenaCardWeakness();
	wAIDefendingPokemonWeakness = a;
	SwapTurn();
	if (a == 0)
		return; /* return if Defending Pokemon has no weakness */
	if (a & b)
		return; /* return if Venomoth is already Defending card's weakness type */

	/* check whether there's a card in play with
	 * the same color as the Player's card weakness */
	if (HandleAIShift_CheckWhetherTurnDuelistHasColor())
		goto found;
	SwapTurn();
	has_color = HandleAIShift_CheckWhetherTurnDuelistHasColor();
	SwapTurn();
	if (!has_color)
		return; /* return if no color found */

found:
	hTempCardIndex_ff9f = wce08[0];
	AIMakeDecision(OPPACTION_USE_PKMN_POWER);

	/* converts WR_* to appropriate color */
	a = wAIDefendingPokemonWeakness;
	b = 0;
	while (!(a & 0x80)) { /* .loop_color: bit 7, a */
		b++;
		a = (u8)((a << 1) | (a >> 7)); /* rlca */
	}

	/* use Pkmn Power effect */
	/* .done */
	hAIPkmnPowerEffectParam = b;
	AIMakeDecision(OPPACTION_EXECUTE_PKMN_POWER_EFFECT);
	AIMakeDecision(OPPACTION_DUEL_MAIN_SCENE);
}

/* returns carry if turn Duelist has a Pokemon
 * with same color as wAIDefendingPokemonWeakness. */
static bool HandleAIShift_CheckWhetherTurnDuelistHasColor(void)
{
	u8 b = wAIDefendingPokemonWeakness;
	const u8 *hl = TurnDuelistVars() + DUELVARS_ARENA_CARD;
	for (;;) {
		/* .loop_play_area */
		u8 a = *hl++;
		if (a == 0xff)
			return false; /* .false */
		/* bug, this could be a Trainer card */
		/* (GetCardType gives TYPE_TRAINER for Clefairy Doll / Mysterious Fossil, and
		 * TranslateColorToWR then reads past InvertedPowersOf2) */
		a = TranslateColorToWR(GetCardType(GetCardIDFromDeckIndex(a)));
		if (a & b)
			return true; /* true */
	}
}

/* checks whether AI uses Peek.
 * input:
 *	c = Play Area location (PLAY_AREA_*) of Mankey. */
void HandleAIPeek(u8 c)
{
	TRACE_FN();
	u8 a, b, d, n;

	hTemp_ffa0 = c;
	if (Random(50) >= 3)
		return; /* return 47 out of 50 times */

	/* choose what to use Peek on at random */
	a = Random(3);
	if (a == 0)
		goto check_ai_prizes;
	if (a < 2)
		goto check_player_hand;

	/* check Player's Deck */
	if (GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK) >= DECK_SIZE - 1)
		return; /* return if Player has one or no cards in Deck */
	a = AI_PEEK_TARGET_DECK;
	goto use_peek;

check_ai_prizes:
	a = GetTurnDuelistVariable(DUELVARS_PRIZES) & wAIPeekedPrizes;
	wAIPeekedPrizes = a;
	if (a == 0)
		return; /* return if no prizes */

	c = a;
	b = 0x1;
	d = 0;
	while (!(c & b)) { /* .loop_prizes */
		b <<= 1;
		d++;
	}
	/* .found_prize */
	/* remove this prize's flag from the prize list
	 * and use Peek on first one in list (lowest bit set) */
	wAIPeekedPrizes = (u8)(c - b);
	a = (u8)(AI_PEEK_TARGET_PRIZE + d);
	goto use_peek;

check_player_hand:
	SwapTurn();
	CreateHandCardList(&n);
	SwapTurn();
	if (n == 0)
		return; /* return if no cards in Hand */
	/* shuffle list and pick the first entry to Peek */
	ShuffleCards(wDuelTempList, CountCardsInDuelTempList());
	a = wDuelTempList[0] | AI_PEEK_TARGET_HAND;

use_peek:
	hTempCardIndex_ff9f = wce08[0];
	AIMakeDecision(OPPACTION_USE_PKMN_POWER);
	hAIPkmnPowerEffectParam = a;
	AIMakeDecision(OPPACTION_EXECUTE_PKMN_POWER_EFFECT);
	AIMakeDecision(OPPACTION_DUEL_MAIN_SCENE);
}

/* checks whether AI uses Strange Behavior.
 * input:
 *	c = Play Area location (PLAY_AREA_*) of Slowbro. */
void HandleAIStrangeBehavior(u8 c)
{
	TRACE_FN();
	u8 a, e, max_hp;

	if (c == 0)
		return; /* return if Slowbro is Arena card */

	hTemp_ffa0 = c;
	a = GetCardDamageAndMaxHP(PLAY_AREA_ARENA, &max_hp);
	if (a == 0)
		return; /* return if Arena card has no damage counters */

	wce06 = a;
	a = (u8)(GetTurnDuelistVariable((u8)(hTemp_ffa0 + DUELVARS_ARENA_CARD_HP)) - 10);
	if (a == 0)
		return; /* return if Slowbro has only 10 HP remaining */

	/* if Slowbro can't receive all damage counters,
	 * only transfer remaining HP - 10 damage */
	if (!(a < wce06))
		a = wce06; /* can receive all damage counters */

	/* .use_strange_behavior */
	hTempCardIndex_ff9f = wce08[0];
	AIMakeDecision(OPPACTION_USE_PKMN_POWER);
	hAIPkmnPowerEffectParam = 0;
	AIMakeDecision(OPPACTION_EXECUTE_PKMN_POWER_EFFECT);

	/* loop counters chosen to transfer and use Pkmn Power */
	e = ConvertHPToDamageCounters_Bank8(a);
	do {
		/* .loop_counters */
		for (u8 i = 30; i != 0; i--) /* .small_delay_loop */
			DoFrame();
		AIMakeDecision(OPPACTION_6B15);
	} while (--e != 0);

	/* return to main scene */
	for (u8 i = 60; i != 0; i--) /* .big_delay_loop */
		DoFrame();
	AIMakeDecision(OPPACTION_DUEL_MAIN_SCENE);
}

/* checks whether AI uses Curse.
 * input:
 *	c = Play Area location (PLAY_AREA_*) of Gengar. */
bool HandleAICurse(u8 c)
{
	TRACE_FN();
	u8 a, b, d, e, h, max_hp;

	hTemp_ffa0 = c;

	/* loop Player's Play Area and checks their damage.
	 * finds the card with lowest remaining HP and
	 * stores its HP and its Play Area location */
	d = GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	e = PLAY_AREA_ARENA;
	b = 0;
	c = 0xff;
	h = PLAY_AREA_ARENA;
	SwapTurn();
	do {
		/* .loop_play_area_1 */
		if (GetCardDamageAndMaxHP(e, &max_hp) == 0)
			continue; /* .next_1 */

		b++;
		a = GetTurnDuelistVariable((u8)(e + DUELVARS_ARENA_CARD_HP));
		if (a >= c)
			continue; /* .next_1 */
		/* lower HP than one stored */
		c = a; /* store this HP */
		h = e; /* store this Play Area location */
	} while (++e != d); /* .next_1: reached end of Play Area */

	if (1 >= b)
		goto failed; /* return if less than 2 cards with damage */

	/* card in Play Area with lowest HP remaining was found.
	 * look for another card to take damage counter from. */
	hTempRetreatCostCards[0] = h;
	b = h;
	if (c == 10) {
		/* .hp_10_remaining */
		/* if Curse can KO, then include
		 * Player's Arena card to take
		 * damage counter from. */
		e = PLAY_AREA_ARENA;
	} else {
		/* if has more than 10 HP remaining,
		 * skip Arena card in choosing which
		 * card to take damage counter from. */
		e = PLAY_AREA_BENCH_1;
	}

	/* .second_card */
	d = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	do {
		/* .loop_play_area_2 */
		if (e == b)
			continue; /* .next_2: skip same Pokemon card */
		if (GetCardDamageAndMaxHP(e, &max_hp) != 0)
			goto use_curse; /* has damage counters, choose this card */
	} while (++e != d); /* .next_2 */

failed:
	SwapTurn();
	return false;

use_curse:
	hAIPkmnPowerEffectParam = e;
	SwapTurn();
	hTempCardIndex_ff9f = wce08[0];
	AIMakeDecision(OPPACTION_USE_PKMN_POWER);
	AIMakeDecision(OPPACTION_EXECUTE_PKMN_POWER_EFFECT);
	return AIMakeDecision(OPPACTION_DUEL_MAIN_SCENE);
}

/* handles AI logic for Cowardice */
void HandleAICowardice(void)
{
	TRACE_FN();
	u8 b, c, card, id;
	bool carry;

	if (CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK))
		return; /* return if there's Muk in play */

	if (AIChooseRandomlyNotToDoAction())
		return; /* randomly return */

	b = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	if (b == 1)
		return; /* return if only one Pokemon in Play Area */

	c = PLAY_AREA_ARENA;
	if (GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS) & CNF_SLP_PRZ)
		goto next;

	for (;;) {
		/* .loop */
		card = GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + c));
		wce08[0] = card;
		id = GetCardIDFromDeckIndex(card);
		/* cp TENTACOOL / call z, .CheckWhetherToUseCowardice / pop bc / jr nc, .next */
		if (id == TENTACOOL)
			carry = HandleAICowardice_CheckWhetherToUseCowardice(c);
		else
			/* bug (original): when the card is not Tentacool, the carry tested by
			 * `jr nc, .next` is the one of `cp TENTACOOL`, so any card with an ID lower
			 * than TENTACOOL is handled as if Cowardice had been used on it. */
			carry = id < TENTACOOL;
		if (carry) {
			b--; /* subtract 1 from number of Pokemon in Play Area */
			if (b == 1)
				return; /* return if no longer has Bench Pokemon */
			c = PLAY_AREA_ARENA; /* reset back to Arena */
			continue;
		}

	next:
		c++;
		if (c == b)
			return;
	}
}

/* checks whether AI uses Cowardice.
 * return carry if Pkmn Power was used.
 * input:
 *	c = Play Area location (PLAY_AREA_*) of Tentacool. */
static bool HandleAICowardice_CheckWhetherToUseCowardice(u8 c)
{
	u8 a, max_hp;

	hTemp_ffa0 = c;
	a = GetCardDamageAndMaxHP(c, &max_hp);
asm_22678:
	if (a == 0)
		return false; /* return if has no damage counters */

	if (hTemp_ffa0 != 0)
		goto is_benched;

	/* this part is buggy if AIDecideBenchPokemonToSwitchTo returns carry
	 * but since this was already checked beforehand, this never happens.
	 * so jr c, .asm_22678 can be safely removed. */
	if (AIDecideBenchPokemonToSwitchTo(&a))
		goto asm_22678; /* bug, this jumps in the middle of damage checking */
	goto use_cowardice;

is_benched:
	a = 0xff;

use_cowardice:
	hTempCardIndex_ff9f = wce08[0];
	AIMakeDecision(OPPACTION_USE_PKMN_POWER);
	hAIPkmnPowerEffectParam = a;
	AIMakeDecision(OPPACTION_EXECUTE_PKMN_POWER_EFFECT);
	AIMakeDecision(OPPACTION_DUEL_MAIN_SCENE);
	return true;
}

/* AI logic for Damage Swap to transfer damage from Arena card
 * to a card in Bench with more than 10 HP remaining
 * and with no energy cards attached. */
void HandleAIDamageSwap(void)
{
	TRACE_FN();
	u8 a, e, id, max_hp;

	if ((u8)(GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) - 1) == 0)
		return; /* return if no Bench Pokemon */

	if (AIChooseRandomlyNotToDoAction())
		return;

	if (!CountTurnDuelistPokemonWithActivePkmnPower(ALAKAZAM))
		return; /* return if no Alakazam */
	if (CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK))
		return; /* return if there's Muk in play */

	/* only take damage off certain cards in Arena */
	id = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	if (id != ALAKAZAM && id != KADABRA && id != ABRA && id != MR_MIME)
		return;

	/* .ok */
	a = GetCardDamageAndMaxHP(PLAY_AREA_ARENA, &max_hp);
	if (a == 0)
		return; /* return if no damage */

	wce06 = ConvertHPToDamageCounters_Bank8(a);
	if (!LookForCardIDInPlayArea_Bank5(ALAKAZAM, PLAY_AREA_BENCH_1, &a)) {
		/* Alakazam is Arena card */
		a = PLAY_AREA_ARENA;
	}
	/* .is_in_bench */
	wce08[0] = a;
	if (HandleAIDamageSwap_CheckForDamageSwapTargetInBench(&a))
		return; /* return if not found */

	/* use Damage Swap */
	hTempCardIndex_ff9f = GetTurnDuelistVariable((u8)(wce08[0] + DUELVARS_ARENA_CARD));
	hTemp_ffa0 = wce08[0];
	AIMakeDecision(OPPACTION_USE_PKMN_POWER);
	AIMakeDecision(OPPACTION_EXECUTE_PKMN_POWER_EFFECT);

	e = wce06;
	do {
		/* .loop_damage */
		for (u8 i = 30; i != 0; i--) /* .small_delay_loop */
			DoFrame();

		if (HandleAIDamageSwap_CheckForDamageSwapTargetInBench(&a))
			break; /* .no_more_target */

		hTempRetreatCostCards[0] = a;
		hAIPkmnPowerEffectParam = PLAY_AREA_ARENA;
		AIMakeDecision(OPPACTION_6B15);
	} while (--e != 0);

	/* .done */
	/* return to main scene */
	for (u8 i = 60; i != 0; i--) /* .big_delay_loop */
		DoFrame();
	AIMakeDecision(OPPACTION_DUEL_MAIN_SCENE);
}

/* looks for a target in the bench to receive damage counters.
 * returns carry if one is found, and outputs remaining HP in a.
 * (actually: returns carry if NONE is found; otherwise a = the Play Area location
 * of the last candidate with no energy attached, else of the last candidate) */
static bool HandleAIDamageSwap_CheckForDamageSwapTargetInBench(u8 *a)
{
	u8 b, c, d, e, id;

	b = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	c = PLAY_AREA_BENCH_1;
	d = 0xff;
	e = 0xff;

	/* look for candidates in bench to get the damage counters
	 * only target specific card IDs. */
	do {
		/* .loop_bench */
		id = GetCardIDFromDeckIndex(GetTurnDuelistVariable((u8)(c + DUELVARS_ARENA_CARD)));
		if (id != CHANSEY && id != KANGASKHAN && id != SNORLAX && id != MR_MIME)
			continue; /* .next_play_area */

		/* .found_candidate */
		/* found a potential candidate to receive damage counters */
		if (GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD_HP + c)) < 20)
			continue; /* ignore cards with only 10 HP left */

		d = c; /* store damage */
		if (CountNumberOfEnergyCardsAttached(c) != 0)
			continue; /* ignore cards with attached energy */
		e = c; /* store deck index */
	} while (++c != b); /* .next_play_area */

	/* done */
	if (e != 0xff) {
		*a = e;
		return false; /* .no_carry */
	}
	*a = d;
	if (d != 0xff)
		return false; /* .no_carry */
	return true; /* .set_carry */
}

/* handles AI logic for attaching energy cards
 * in Go Go Rain Dance deck. */
void HandleAIGoGoRainDanceEnergy(void)
{
	TRACE_FN();
	if (wOpponentDeckID != GO_GO_RAIN_DANCE_DECK_ID)
		return; /* return if not Go Go Rain Dance deck */

	if (!CountTurnDuelistPokemonWithActivePkmnPower(BLASTOISE))
		return; /* return if no Blastoise */
	if (CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK))
		return; /* return if there's Muk in play */

	/* play all the energy cards that is needed. */
	while (AIProcessAndTryToPlayEnergy()) /* .loop */
		;
}
