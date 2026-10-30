/*
 * Port of src/engine/duel/ai/common.asm (part D).
 */
#include "ai/ai.h"

/* register e left by CreateHandCardList: the low byte of de, which points to the
 * $ff terminator it wrote to wDuelTempList (the list is not modified afterwards
 * where this is used). LookForCardIDToTradeWithDifferentHandCard returns it in e. */
static u8 CreateHandCardList_RegE(void)
{
	u8 n = 0;
	while (wDuelTempList[n] != 0xff)
		n++;
	return (u8)(ADDR_wDuelTempList + n);
}

/* register a at the `ret` of LookForCardIDInHandAndPlayArea. ai.h only exposes its
 * carry, but LookForCardIDInDeck_GivenCardIDInHandAndPlayArea / _GivenCardIDInHand
 * return it as their own a on some no-carry paths. A CPU register, not WRAM/HRAM
 * state: it is kept here so that LookForCardIDInHandAndPlayArea keeps its ai.h
 * signature and its TRACE_FN() on every entry. */
static u8 sLookForCardIDInHandAndPlayArea_a;

/* runs through Player's whole deck and
 * sets carry if there's any Pokemon other
 * than MewtwoLv53. */
bool CheckIfPlayerHasPokemonOtherThanMewtwoLv53(void)
{
	TRACE_FN();
	u8 e;

	SwapTurn();
	e = 0;
	do {
		/* .loop_deck */
		LoadCardDataToBuffer2_FromDeckIndex(e);
		if (wLoadedCard2.Type >= TYPE_ENERGY)
			continue; /* .next (can be a jr) */
		if (wLoadedCard2.ID != MEWTWO_LV53)
			goto not_mewtwo1;
	} while (++e != DECK_SIZE); /* .next */

	/* no carry */
	SwapTurn();
	return false;

not_mewtwo1:
	SwapTurn();
	return true;
}

/* returns no carry if, given the Player is using a MewtwoLv53 mill deck,
 * the AI already has a Bench fully set up, in which case it
 * will process some Trainer cards in hand (namely Energy Removals).
 * this is used to check whether to skip some normal AI routines
 * this turn and jump right to the attacking phase. */
bool HandleAIAntiMewtwoDeckStrategy(void)
{
	TRACE_FN();
	u8 a;

	/* return carry if Player is not playing MewtwoLv53 mill deck */
	a = wAIBarrierFlagCounter;
	if (!(a & (1 << AI_MEWTWO_MILL_F)))
		goto set_carry;

	/* else, check if there's been less than 2 turns
	 * without the Player using Barrier. */
	if (a < AI_MEWTWO_MILL + 2)
		goto count_bench;

	/* if there has been, reset wAIBarrierFlagCounter
	 * and return carry. */
	wAIBarrierFlagCounter = 0;
	goto set_carry;

	/* else, check number of Pokemon that are set up in Bench
	 * if less than 4, return carry. */
count_bench:
	if (CountNumberOfSetUpBenchPokemon() < 4)
		goto set_carry;

	/* if there's at least 4 Pokemon in the Bench set up,
	 * process Trainer hand cards of AI_TRAINER_CARD_PHASE_05 */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_05);
	return false;

set_carry:
	return true;
}

/* lists in wDuelTempList all the basic energy cards
 * in card location of a.
 * outputs in a number of cards found.
 * returns carry if none were found.
 * input:
 *   a = CARD_LOCATION_* to look
 * output:
 *   a = number of cards found */
bool FindBasicEnergyCardsInLocation(u8 loc, u8 *count)
{
	TRACE_FN();
	u8 d, e;
	u8 *hl;

	wTempAI = loc;
	d = 0;
	e = 0;
	hl = wDuelTempList;

	/* d = number of basic energy cards found
	 * e = current card in deck
	 * loop entire deck */
	do {
		/* .loop */
		if (GetTurnDuelistVariable((u8)(DUELVARS_CARD_LOCATIONS + e)) != wTempAI)
			continue; /* .next_card */

		/* is in the card location we're looking for */
		if (GetCardIDFromDeckIndex(e) >= DOUBLE_COLORLESS_ENERGY)
			continue; /* only basic energy cards will set carry here */

		/* is a basic energy card
		 * add this card to the TempList */
		*hl++ = e;
		d++;
	} while (++e != DECK_SIZE); /* .next_card */

	/* check if any were found */
	*count = d;
	if (d == 0)
		return true; /* .set_carry */

	/* some were found, add the termination byte on TempList */
	*hl = 0xff;
	return false;
}

/* returns in a the card index of energy card
 * attached to Pokémon in Play Area location a,
 * that is to be discarded by the AI for an effect.
 * outputs $ff is none was found.
 * input:
 *	a = PLAY_AREA_* constant of card
 * output:
 *	a = deck index of attached energy card chosen */
u8 AIPickEnergyCardToDiscard(u8 loc)
{
	TRACE_FN();
	u8 e;
	const u8 *hl;

	/* load Pokémon's attached energy cards. */
	hTempPlayAreaLocation_ff9d = loc;
	CreateArenaOrBenchEnergyCardList(loc);
	GetPlayAreaCardAttachedEnergies(hTempPlayAreaLocation_ff9d);
	if (wTotalAttachedEnergies == 0)
		return 0xff; /* .no_energy */

	/* load card's ID and type. */
	e = GetCardIDFromDeckIndex(GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + hTempPlayAreaLocation_ff9d)));
	wTempCardID = e;
	LoadCardDataToBuffer1_FromCardID(e);
	wTempCardType = wLoadedCard1.Type | TYPE_ENERGY;

	/* find a card that is not useful.
	 * if none is found, just return the first energy card attached. */
	for (hl = wDuelTempList; *hl != 0xff; hl++) {
		/* .loop */
		if (!CheckIfEnergyIsUseful(*hl))
			return *hl; /* .found */
	}

	/* .not_found */
	return wDuelTempList[0];
}

/* returns in a the deck index of an energy card attached to card
 * in player's Play Area location a to remove.
 * prioritizes double colorless energy, then any useful energy,
 * then defaults to the first energy card attached if neither
 * of those are found.
 * returns $ff in a if there are no energy cards attached.
 * input:
 *   a = Play Area location to check
 * output:
 *   a = deck index of attached energy card */
u8 PickAttachedEnergyCardToRemove(u8 loc)
{
	TRACE_FN();
	u8 e;
	const u8 *hl;

	/* construct energy list and check if there are any energy cards attached */
	hTempPlayAreaLocation_ff9d = loc;
	CreateArenaOrBenchEnergyCardList(loc);
	GetPlayAreaCardAttachedEnergies(hTempPlayAreaLocation_ff9d);
	if (wTotalAttachedEnergies == 0)
		return 0xff; /* .no_energy */

	/* load card data and store its type */
	e = GetCardIDFromDeckIndex(GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + hTempPlayAreaLocation_ff9d)));
	wTempCardID = e;
	LoadCardDataToBuffer1_FromCardID(e);
	wTempCardType = wLoadedCard1.Type | TYPE_ENERGY;

	/* first look for any double colorless energy */
	for (hl = wDuelTempList; *hl != 0xff; hl++) {
		/* .loop_1 */
		if (GetCardIDFromDeckIndex(*hl) == DOUBLE_COLORLESS_ENERGY)
			return *hl; /* .found */
	}

	/* then look for any energy cards that are useful */
	/* .check_useful */
	for (hl = wDuelTempList; *hl != 0xff; hl++) {
		/* .loop_2 */
		if (CheckIfEnergyIsUseful(*hl))
			return *hl; /* .found: return the energy card that was found */
	}

	/* if none were found with the above criteria,
	 * just return the first option */
	/* .default */
	return wDuelTempList[0];
}

/* stores in wTempAI and wCurCardCanAttack the deck indices
 * of energy cards attached to card in Play Area location a.
 * prioritizes double colorless energy, then any useful energy,
 * then defaults to the first two energy cards attached if neither
 * of those are found.
 * returns $ff in a if there are no energy cards attached.
 * input:
 *   a = Play Area location to check
 * output:
 *   [wTempAI] = deck index of attached energy card
 *   [wCurCardCanAttack] = deck index of attached energy card */
u8 PickTwoAttachedEnergyCards(u8 loc, u8 *b)
{
	TRACE_FN();
	u8 a, e, first;
	const u8 *hl;

	hTempPlayAreaLocation_ff9d = loc;
	CreateArenaOrBenchEnergyCardList(loc);
	if (CountNumberOfEnergyCardsAttached(hTempPlayAreaLocation_ff9d) < 2)
		return 0xff; /* .not_enough: return $ff if no energy cards attached */

	/* load card data and store its type */
	e = GetCardIDFromDeckIndex(GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + hTempPlayAreaLocation_ff9d)));
	wTempCardID = e;
	LoadCardDataToBuffer1_FromCardID(e);
	wTempCardType = wLoadedCard1.Type | TYPE_ENERGY;
	wTempAI = 0xff;
	wCurCardCanAttack = 0xff;

	/* first look for any double colorless energy */
	hl = wDuelTempList;
	while (*hl != 0xff) {
		/* .loop_1 */
		if (GetCardIDFromDeckIndex(*hl) != DOUBLE_COLORLESS_ENERGY) {
			hl++;
			continue;
		}
		/* .found_double_colorless */
		if (wTempAI != 0xff) {
			/* .already_chosen_1 */
			wCurCardCanAttack = *hl;
			goto done;
		}
		wTempAI = *hl++;
	}

	/* then look for any energy cards that are useful */
	/* .check_useful */
	hl = wDuelTempList;
	while (*hl != 0xff) {
		/* .loop_2 */
		if (!CheckIfEnergyIsUseful(*hl)) {
			hl++;
			continue;
		}
		/* .found_useful */
		/* bug (original): this loop restarts from the top of the list without skipping
		 * the card already chosen in .loop_1, and CheckIfEnergyIsUseful always returns
		 * carry for a Double Colorless Energy: with a single DCE attached, unless a useful
		 * energy precedes it in the list, wCurCardCanAttack ends up equal to wTempAI. */
		if (wTempAI != 0xff) {
			/* .already_chosen_2 */
			wCurCardCanAttack = *hl;
			goto done;
		}
		wTempAI = *hl++;
	}

	/* if none were found with the above criteria,
	 * just return the first 2 options */
	/* .default */
	hl = wDuelTempList;
	if (wTempAI != 0xff)
		goto pick_one_card;

	/* pick 2 cards */
	wTempAI = *hl++;
	wCurCardCanAttack = *hl;
	goto done;

pick_one_card:
	first = wTempAI;
	do {
		/* .loop_3 */
		a = *hl++;
	} while (a == first); /* already picked */
	wCurCardCanAttack = a;

done:
	*b = wCurCardCanAttack;
	return wTempAI;
}

/* copies an $ff-terminated list from hl to de
 * preserves bc
 * input:
 *	hl = address from which to start copying the data
 *	de = where to copy the data */
void CopyListWithFFTerminatorFromHLToDE_Bank8(const u8 *hl, u8 *de)
{
	for (;;) {
		u8 a = *hl++;
		*de = a;
		if (a == 0xff)
			return;
		de++;
	}
}

/* zeroes a bytes starting from hl.
 * this function is identical to 'ClearMemory_Bank2',
 * 'ClearMemory_Bank5' and 'ClearMemory_Bank6'.
 * preserves all registers
 * input:
 *	a = number of bytes to clear
 *	hl = where to begin erasing */
void ClearMemory_Bank8(u8 a, u8 *hl)
{
	u8 b = a;
	do {
		/* .loop */
		*hl++ = 0;
	} while (--b != 0); /* (a = 0 clears 256 bytes) */
}

/* counts number of energy cards found in hand
 * and outputs result in a
 * sets carry if none are found
 * output:
 *	a = number of energy cards found */
u8 CountOppEnergyCardsInHand(void)
{
	TRACE_FN();
	u8 b;
	const u8 *hl;

	if (CreateEnergyCardListFromHand())
		return wDuelTempList[0]; /* register a of CreateEnergyCardListFromHand: $ff */
	b = 0xff;
	hl = wDuelTempList;
	do {
		/* .loop */
		b++;
	} while (*hl++ != 0xff);
	return b;
}

/* converts an HP value or amount of damage to the number of equivalent damage counters
 * preserves all registers except af
 * input:
 *	a = HP value to convert
 * output:
 *	a = number of damage counters */
u8 ConvertHPToDamageCounters_Bank8(u8 hp)
{
	u8 c = 0;
	for (;;) {
		/* .loop: sub 10 / jr c, .carry */
		if (hp < 10)
			break;
		hp -= 10;
		c++;
	}
	/* .carry */
	return c;
}

/* calculates floor(hl / 10) */
u16 CalculateWordTensDigit(u16 hl)
{
	u16 de = 0xffff; /* lb de, $ff, -1 */
	bool carry;
	do {
		/* .asm_229b8 */
		de++;
		carry = hl >= 10; /* add hl, bc (bc = -10) */
		hl -= 10;
	} while (carry);
	return de;
}

/* returns in a division of b by a */
u8 CalculateBDividedByA_Bank8(u8 a, u8 b)
{
	u8 x = b, c = 0;
	for (;;) {
		/* .loop: sub b / jr c, .done (never ends if a = 0, as in the original) */
		if (x < a)
			break;
		x -= a;
		c++;
	}
	/* .done */
	return c;
}

/* returns in a the deck index of the first
 * instance of card with ID equal to the ID in e
 * in card location a.
 * returns carry if found.
 * input:
 *   a = CARD_LOCATION_*
 *   e = card ID to look for */
bool LookForCardIDInLocation_Bank8(u8 a, u8 e, u8 *idx)
{
	TRACE_FN();
	u8 b = a;
	u8 c = e;

	e = 0; /* lb de, 0, 0 ; d is never used */
	do {
		/* .loop */
		if (GetTurnDuelistVariable((u8)(DUELVARS_CARD_LOCATIONS + e)) != b)
			continue; /* .next */
		if (GetCardIDFromDeckIndex(e) == c) {
			/* .found */
			*idx = e;
			return true;
		}
	} while (++e != DECK_SIZE); /* .next */

	/* not found */
	*idx = DECK_SIZE;
	return false;
}

/* return carry if card ID loaded in a is found in hand
 * and outputs in a the deck index of that card
 * input:
 *	a = card ID
 * output:
 *	a = card deck index, if found
 *	carry set if found */
bool LookForCardIDInHandList_Bank8(u8 card_id, u8 *a)
{
	TRACE_FN();
	const u8 *hl;
	u8 x, id;

	wTempCardIDToLook = card_id;
	CreateHandCardList(NULL);
	hl = wDuelTempList;

	for (;;) {
		/* .loop */
		x = *hl++;
		if (x == 0xff) {
			*a = x;
			return false;
		}

		hTempCardIndex_ff98 = x;
		id = LoadCardDataToBuffer1_FromDeckIndex(x);
		if (wTempCardIDToLook == id)
			break;
	}

	*a = hTempCardIndex_ff98;
	return true;
}

/* searches in deck for card ID 1 in a, and
 * if found, searches in Hand/Play Area for card ID 2 in b, and
 * if found, searches for card ID 1 in Hand/Play Area, and
 * if none found, return carry and output deck index
 * of the card ID 1 in deck.
 * input:
 *   a = card ID 1
 *   b = card ID 2
 * output:
 *   a = index of card ID 1 in deck */
bool LookForCardIDInDeck_GivenCardIDInHandAndPlayArea(u8 a, u8 b, u8 *idx)
{
	TRACE_FN();

	/* store a in wCurCardCanAttack
	 * and b in wTempAI */
	wTempAI = b;
	wCurCardCanAttack = a;

	/* look for the card ID 1 in deck */
	if (!LookForCardIDInLocation_Bank8(CARD_LOCATION_DECK, a, idx))
		return false;

	/* was found, store its deck index in memory */
	wTempAIPokemonCard = *idx;

	/* look for the card ID 2
	 * in Hand and Play Area, return if not found. */
	if (!LookForCardIDInHandAndPlayArea(wTempAI)) {
		*idx = sLookForCardIDInHandAndPlayArea_a;
		return false;
	}

	/* look for the card ID 1 in the Hand and Play Area
	 * if any card is found, return no carry. */
	if (LookForCardIDInHandAndPlayArea(wCurCardCanAttack)) {
		/* .no_carry */
		*idx = sLookForCardIDInHandAndPlayArea_a;
		return false;
	}
	/* none found */

	*idx = wTempAIPokemonCard;
	return true;
}

/* returns carry if card ID in a
 * is found in Play Area or in hand
 * input:
 *	a = card ID */
bool LookForCardIDInHandAndPlayArea(u8 card_id)
{
	TRACE_FN();
	u8 a;

	if (LookForCardIDInHandList_Bank8(card_id, &a)) {
		sLookForCardIDInHandAndPlayArea_a = a;
		return true;
	}

	if (LookForCardIDInPlayArea_Bank8(card_id, PLAY_AREA_ARENA, &a)) {
		sLookForCardIDInHandAndPlayArea_a = a;
		return true;
	}
	sLookForCardIDInHandAndPlayArea_a = a;
	return false;
}

/* searches in deck for card ID 1 in a, and
 * if found, searches in Hand Area for card ID 2 in b, and
 * if found, searches for card ID 1 in Hand/Play Area, and
 * if none found, return carry and output deck index
 * of the card ID 1 in deck.
 * input:
 *   a = card ID 1
 *   b = card ID 2
 * output:
 *   a = index of card ID 1 in deck */
bool LookForCardIDInDeck_GivenCardIDInHand(u8 a, u8 b, u8 *idx)
{
	TRACE_FN();

	/* store a in wCurCardCanAttack
	 * and b in wTempAI */
	wTempAI = b;
	wCurCardCanAttack = a;

	/* look for the card ID 1 in deck */
	if (!LookForCardIDInLocation_Bank8(CARD_LOCATION_DECK, a, idx))
		return false;

	/* was found, store its deck index in memory */
	wTempAIPokemonCard = *idx;

	/* look for the card ID 2 in hand, return if not found. */
	if (!LookForCardIDInHandList_Bank8(wTempAI, idx))
		return false;

	/* look for the card ID 1 in the Hand and Play Area
	 * if any card is found, return no carry. */
	if (LookForCardIDInHandAndPlayArea(wCurCardCanAttack)) {
		/* .no_carry */
		*idx = sLookForCardIDInHandAndPlayArea_a;
		return false;
	}
	/* none found */

	*idx = wTempAIPokemonCard;
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
bool LookForCardIDInPlayArea_Bank8(u8 card_id, u8 b, u8 *a)
{
	TRACE_FN();
	u8 x, c;

	wTempCardIDToLook = card_id;
	for (;;) {
		/* .loop */
		x = GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + b));
		if (x == 0xff) {
			*a = x;
			return false;
		}
		c = LoadCardDataToBuffer1_FromDeckIndex(x);
		if (wTempCardIDToLook == c) {
			/* .found */
			*a = b;
			return true;
		}
		b++;
		if (b == MAX_PLAY_AREA_POKEMON)
			break;
	}

	/* not found */
	*a = MAX_PLAY_AREA_POKEMON;
	return false;
}

/* runs through list avoiding card in e.
 * removes first card in list not equal to e
 * and that has a type allowed to be removed, in d.
 * returns carry if successful in finding a card.
 * input:
 *   d = type of card allowed to be removed
 *       ($00 = Trainer, $01 = Pokemon, $02 = Energy)
 *   e = card deck index to avoid removing
 * output:
 *   a = card index of removed card */
bool RemoveFromListDifferentCardOfGivenType(u8 d, u8 e, u8 *hl, u8 *a)
{
	TRACE_FN();
	u8 x, type;
	u8 *de;

	ShuffleCards(hl, CountCardsInDuelTempList());

	/* loop list until a card with
	 * deck index different from e is found. */
	for (;;) {
		/* .loop_list */
		x = *hl++;
		if (x == 0xff) {
			/* .no_carry */
			*a = x;
			return false;
		}
		if (x == e)
			continue;

		/* get this card's type */
		hTempCardIndex_ff98 = x;
		type = GetCardType(GetCardIDFromDeckIndex(x));
		if (type < TYPE_ENERGY) {
			/* .pkmn_card */
			if (d != 0x01)
				continue;
			break; /* fallthrough */
		}
		if (type != TYPE_TRAINER) {
			/* .energy */
			if (d != 0x02)
				continue;
			break;
		}

		/* only remove from list specific type. */

		/* trainer */
		if (d != 0)
			continue;
		break;
	}

	/* .remove_card */
	de = hl;
	hl--;
	do {
		/* .loop_remove */
		x = *de++;
		*hl++ = x;
	} while (x != 0xff);

	/* success */
	*a = hTempCardIndex_ff98;
	return true;
}

/* used in Pokemon Trader checks to look for a specific
 * card in the deck to trade with a card in hand that
 * has a card ID different from e.
 * returns carry if successful.
 * input:
 *   a = card ID 1
 *   e = card ID 2
 * output:
 *   a = deck index of card ID 1 found in deck
 *   e = deck index of Pokemon card in hand different than card ID 2 */
bool LookForCardIDToTradeWithDifferentHandCard(u8 a, u8 e, u8 *deck_card, u8 *hand_card)
{
	TRACE_FN();
	const u8 *hl;
	u8 b, c;

	wCurCardCanAttack = e;
	wTempAI = a;

	/* if card ID 1 is in hand, return no carry. */
	if (LookForCardIDInHandList_Bank8(a, deck_card)) {
		/* .no_carry */
		*hand_card = CreateHandCardList_RegE();
		return false;
	}

	/* if card ID 1 is not in deck, return no carry. */
	if (!LookForCardIDInLocation_Bank8(CARD_LOCATION_DECK, wTempAI, deck_card)) {
		/* .no_carry */
		*hand_card = DECK_SIZE; /* e at the end of LookForCardIDInLocation_Bank8's loop */
		return false;
	}

	/* store its deck index */
	wTempAI = *deck_card;

	/* look in hand for Pokemon card ID that
	 * is different from card ID 2. */
	c = wCurCardCanAttack;
	CreateHandCardList(NULL);
	hl = wDuelTempList;

	for (;;) {
		/* .loop_hand */
		b = *hl++;
		if (b == 0xff) {
			/* .no_carry */
			*deck_card = b;
			*hand_card = CreateHandCardList_RegE();
			return false;
		}
		if (LoadCardDataToBuffer1_FromDeckIndex(b) == c)
			continue;
		if (wLoadedCard1.Type >= TYPE_ENERGY)
			continue;
		break;
	}

	/* found, output deck index of card ID 1 in deck
	 * and deck index of card found in hand, and set carry */
	*hand_card = b;
	*deck_card = wTempAI;
	return true;
}

/* returns carry if at least one card in the hand
 * has the card ID of input. Outputs its index.
 * input:
 *   a = card ID to look for
 * output:
 *   a = deck index of card in hand found
 * (the hand must have two of them: carry and the second one's deck index) */
bool CheckIfHasCardIDInHand(u8 card_id, u8 *a)
{
	TRACE_FN();
	const u8 *hl;
	u8 c, x, id;

	wTempCardIDToLook = card_id;
	CreateHandCardList(NULL);
	hl = wDuelTempList;
	c = 0;

	for (;;) {
		/* .loop_hand */
		x = *hl++;
		if (x == 0xff) {
			*a = x;
			return false;
		}
		hTempCardIndex_ff98 = x;
		id = LoadCardDataToBuffer1_FromDeckIndex(x);
		if (wTempCardIDToLook != id)
			continue;
		if (c != 0)
			break; /* .set_carry */
		if (++c != 0)
			continue;
		break; /* .set_carry */
	}

	/* .set_carry */
	*a = hTempCardIndex_ff98;
	return true;
}

/* outputs in a total number of Pokemon cards in hand
 * plus Pokemon in Turn Duelist's Play Area. */
u8 CountPokemonCardsInHandAndInPlayArea(void)
{
	TRACE_FN();
	const u8 *hl;
	u8 x;

	wTempAI = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	CreateHandCardList(NULL);
	hl = wDuelTempList;
	for (;;) {
		/* .loop_hand */
		x = *hl++;
		if (x == 0xff)
			break; /* .done */
		if (GetCardType(GetCardIDFromDeckIndex(x)) >= TYPE_ENERGY)
			continue;
		wTempAI++;
	}

	/* .done */
	return wTempAI;
}

/* returns carry if a duplicate Pokemon card is found in hand.
 * outputs in a the deck index of one of them. */
bool FindDuplicatePokemonCards(u8 *a)
{
	TRACE_FN();
	const u8 *outer, *hl;
	u8 b, c, x;

	wTempAI = 0xff;
	CreateHandCardList(NULL);
	outer = wDuelTempList;

	for (;;) {
		/* .loop_hand_outer */
		x = *outer++;
		if (x == 0xff)
			break; /* .done */
		b = GetCardIDFromDeckIndex(x);
		hl = outer;

		for (;;) {
			/* .loop_hand_inner */
			c = *hl++;
			if (c == 0xff)
				break; /* .loop_hand_outer */
			if (GetCardIDFromDeckIndex(c) != b)
				continue;

			/* found two cards with same ID,
			 * if they are Pokemon cards, store its deck index. */
			if (GetCardType(b) < TYPE_ENERGY)
				wTempAI = c;
			/* for some reason loop still continues
			 * even though if some other duplicate
			 * cards are found, it overwrites the result. */
			break; /* .loop_hand_outer */
		}
	}

	/* .done */
	*a = wTempAI;
	if (wTempAI == 0xff)
		return false; /* .no_carry */

	/* found */
	return true;
}

/* return carry flag if attack is not high recoil. */
bool AICheckIfAttackIsHighRecoil(void)
{
	TRACE_FN();
	u8 d, e;

	if (!AIProcessButDontUseAttack())
		return false;
	e = wSelectedAttack;
	d = GetTurnDuelistVariable(DUELVARS_ARENA_CARD);
	CopyAttackDataAndDamage_FromDeckIndex(d, e);
	return !CheckLoadedAttackFlag(ATTACK_FLAG1_ADDRESS | HIGH_RECOIL_F);
}
