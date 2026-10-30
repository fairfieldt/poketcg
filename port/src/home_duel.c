/* Port of src/home/duel.asm */
#include "engine.h"
#include "gen/rom_blobs.h"

/* save duel state to SRAM
 * called between each two-player turn, just after player draws card */
void SaveDuelStateToSRAM(void)
{
	/* The original writes the duel to SRAM (sBackupCurrentDuel and one of
	 * four sDuelBuffer slots). The port has no SRAM; only the observable
	 * WRAM side effects are kept. */
	SaveDuelDataToBackup(); /* SaveDuelData with the SRAM bank of sBackupCurrentDuel */
	wTempTurnDuelistCardID = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	SwapTurn();
	wTempNonTurnDuelistCardID = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	SwapTurn();
	host_save_duel();
}

/* copies the deck list at deck to wPlayerDeck or wOpponentDeck (depending on whose turn it is).
 * returns carry if the deck has less than DECK_SIZE cards */
bool CopyDeckData(const u8 *deck)
{
	u8 *hl = (hWhoseTurn == PLAYER_TURN) ? wPlayerDeck : wOpponentDeck;
	u8 *start = hl;
	/* start by putting a terminator at the end of the deck */
	start[DECK_SIZE - 1] = 0;
	for (;;) {
		u8 b = *deck++;
		if (b == 0)
			break;
		u8 c = *deck++;
		do {
			*hl++ = c;
		} while (--b);
	}
	/* the two bytes after the list terminator are copied as the deck name */
	wDeckName[0] = deck[0];
	wDeckName[1] = deck[1];
	if (start[DECK_SIZE - 1] != 0)
		return false;
	return true;
}

/* return the amount of prizes that the turn holder has not yet drawn */
u8 CountPrizes(void)
{
	u8 l = GetTurnDuelistVariable(DUELVARS_PRIZES);
	u8 a = 0;
	while (l) {
		a += l & 1;
		l >>= 1;
	}
	return a;
}

/* shuffles the turn holder's deck
 * if less than 60 cards remain in the deck, it makes sure that the rest are ignored */
void ShuffleDeck(void)
{
	u8 *vars = TurnDuelistVars();
	u8 n = vars[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK];
	ShuffleCards(&vars[(u8)(DUELVARS_DECK_CARDS + n)], DECK_SIZE - n);
}

/* draw a card from the turn holder's deck, saving its location as CARD_LOCATION_JUST_DRAWN.
 * returns carry if deck is empty, nc if a card was successfully drawn (deck index in *card).
 * AddCardToHand is meant to be called next (unless this function returned carry). */
bool DrawCardFromDeck(u8 *card)
{
	u8 *vars = TurnDuelistVars();
	u8 a = vars[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK];
	if (a >= DECK_SIZE) {
		if (card)
			*card = a;
		return true;
	}
	a++;
	vars[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK] = a;
	a = vars[(u8)(a + DUELVARS_DECK_CARDS - 1)];
	vars[a] = CARD_LOCATION_JUST_DRAWN;
	if (card)
		*card = a;
	return false;
}

/* add a card (deck index) to the top of the turn holder's deck */
u8 ReturnCardToDeck(u8 card)
{
	u8 *vars = TurnDuelistVars();
	u8 a = --vars[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK];
	vars[(u8)(a + DUELVARS_DECK_CARDS)] = card;
	vars[card] = CARD_LOCATION_DECK;
	return card;
}

/* search a card in the turn holder's deck, extract it, and set its location to
 * CARD_LOCATION_JUST_DRAWN. AddCardToHand is meant to be called next. */
void SearchCardInDeckAndAddToHand(u8 card)
{
	u8 *vars = TurnDuelistVars();
	u8 b = DECK_SIZE - vars[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK];
	vars[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK]++;
	vars[card] |= CARD_LOCATION_JUST_DRAWN;
	u8 hl = DUELVARS_DECK_CARDS + DECK_SIZE - 1;
	u8 de = hl;
	/* walk the remaining deck from the bottom, shifting cards down over the extracted one */
	b++;
	while (--b) {
		u8 a = vars[hl--];
		if (a == card)
			continue;
		vars[de--] = a;
	}
}

/* adds a card to the turn holder's hand and increments the number of cards in the hand */
void AddCardToHand(u8 card)
{
	u8 *vars = TurnDuelistVars();
	vars[card] = CARD_LOCATION_HAND;
	u8 n = ++vars[DUELVARS_NUMBER_OF_CARDS_IN_HAND];
	vars[(u8)(DUELVARS_HAND - 1 + n)] = card;
}

/* removes a card from the turn holder's hand and decrements the number of cards in the hand */
void RemoveCardFromHand(u8 card)
{
	u8 *vars = TurnDuelistVars();
	u8 b = vars[DUELVARS_NUMBER_OF_CARDS_IN_HAND];
	if (b == 0)
		return;
	u8 hl = DUELVARS_HAND, de = DUELVARS_HAND;
	do {
		u8 a = vars[hl++];
		if (a == card)
			vars[DUELVARS_NUMBER_OF_CARDS_IN_HAND]--;
		else
			vars[de++] = a; /* keep any card that doesn't match in the player's hand */
	} while (--b);
}

/* moves a card to the turn holder's discard pile, as long as it is in the hand */
void MoveHandCardToDiscardPile(u8 card)
{
	u8 loc = GetTurnDuelistVariable(card);
	if ((loc & (0xff ^ CARD_LOCATION_JUST_DRAWN)) != CARD_LOCATION_HAND)
		return; /* return if card not in hand */
	RemoveCardFromHand(card);
	PutCardInDiscardPile(card);
}

/* puts the turn holder's card with the deck index given into the discard pile */
void PutCardInDiscardPile(u8 card)
{
	u8 *vars = TurnDuelistVars();
	vars[card] = CARD_LOCATION_DISCARD_PILE;
	u8 n = ++vars[DUELVARS_NUMBER_OF_CARDS_IN_DISCARD_PILE];
	vars[(u8)(DUELVARS_DECK_CARDS - 1 + n)] = card;
}

/* search a card in the turn holder's discard pile, extract it, and set its location to
 * CARD_LOCATION_JUST_DRAWN. AddCardToHand is meant to be called next.
 * returns a: the card, or 0 if the discard pile was empty */
u8 MoveDiscardPileCardToHand(u8 card)
{
	u8 *vars = TurnDuelistVars();
	vars[card] |= CARD_LOCATION_JUST_DRAWN;
	u8 c = vars[DUELVARS_NUMBER_OF_CARDS_IN_DISCARD_PILE];
	if (c == 0)
		return 0;
	vars[DUELVARS_NUMBER_OF_CARDS_IN_DISCARD_PILE]--;
	u8 hl = DUELVARS_DECK_CARDS, de = DUELVARS_DECK_CARDS;
	do {
		u8 a = vars[hl++];
		if (a != card)
			vars[de++] = a;
	} while (--c);
	return card;
}

const u8 PowersOf2[8] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };

/* return whether turn holder's prize a (0-7) has been drawn or not:
 * the original returns the z flag (z: drawn, nz: not drawn); this returns the
 * masked prize bit, so 0 means drawn */
u8 CheckPrizeTaken(u8 prize)
{
	return GetTurnDuelistVariable(DUELVARS_PRIZES) & PowersOf2[prize];
}

/* fill wDuelTempList with the turn holder's discard pile cards (their 0-59 deck indexes)
 * return carry if the turn holder has no cards in the discard pile */
bool CreateDiscardPileCardList(void)
{
	u8 *vars = TurnDuelistVars();
	u8 b = vars[DUELVARS_NUMBER_OF_CARDS_IN_DISCARD_PILE];
	u8 hl = DUELVARS_DECK_CARDS - 1 + b; /* point to last card in discard pile */
	u8 *de = wDuelTempList;
	b++;
	while (--b)
		*de++ = vars[hl--];
	*de = 0xff;
	return vars[DUELVARS_NUMBER_OF_CARDS_IN_DISCARD_PILE] == 0;
}

/* fill wDuelTempList with the turn holder's remaining deck cards (their 0-59 deck indexes)
 * return carry if the turn holder has no cards left in the deck; count in *count */
bool CreateDeckCardList(u8 *count)
{
	u8 *vars = TurnDuelistVars();
	u8 n = vars[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK];
	if (n >= DECK_SIZE) {
		wDuelTempList[0] = 0xff;
		if (count)
			*count = 0xff;
		return true;
	}
	u8 c = DECK_SIZE - n, b = c;
	u8 hl = n + DUELVARS_DECK_CARDS;
	u8 *de = wDuelTempList;
	b++;
	while (--b)
		*de++ = vars[hl++];
	*de = 0xff;
	if (count)
		*count = c;
	return false;
}

/* fill wDuelTempList with the turn holder's energy cards (their 0-59 deck indexes)
 * at PLAY_AREA_* location loc; return carry if no energy cards were found */
bool CreateArenaOrBenchEnergyCardList(u8 loc)
{
	u8 c = loc | CARD_LOCATION_PLAY_AREA;
	u8 *de = wDuelTempList;
	u8 *vars = TurnDuelistVars();
	for (u8 l = 0; l < DECK_SIZE; l++) {
		if (vars[l] != c)
			continue; /* skip if not in specified play area location */
		LoadCardDataToBuffer2_FromDeckIndex(l);
		if (!(wLoadedCard2.Type & (1 << TYPE_ENERGY_F)))
			continue; /* skip if Pokemon or trainer card */
		*de++ = l;
	}
	*de = 0xff;
	return wDuelTempList[0] == 0xff;
}

/* fill wDuelTempList with the turn holder's hand cards (their 0-59 deck indexes),
 * newest first, skipping cards that are CARD_LOCATION_JUST_DRAWN.
 * return carry if the turn holder has no cards in hand; number of cards in hand in *count */
bool CreateHandCardList(u8 *count)
{
	u8 *vars = TurnDuelistVars();
	u8 b = vars[DUELVARS_NUMBER_OF_CARDS_IN_HAND];
	u8 hl = DUELVARS_HAND - 1 + b;
	u8 *de = wDuelTempList;
	b++;
	while (--b) {
		u8 a = vars[hl--];
		if (vars[a] & CARD_LOCATION_JUST_DRAWN)
			continue;
		*de++ = a;
	}
	*de = 0xff;
	u8 n = vars[DUELVARS_NUMBER_OF_CARDS_IN_HAND];
	if (count)
		*count = n;
	return n == 0;
}

/* sort the turn holder's hand cards by ID (highest to lowest ID)
 * makes use of wDuelTempList */
void SortHandCardsByID(void)
{
	u8 *vars = TurnDuelistVars();
	u8 b = vars[DUELVARS_NUMBER_OF_CARDS_IN_HAND];
	u8 hl = DUELVARS_HAND - 1 + b;
	u8 *de = wDuelTempList;
	/* bug (original): with an empty hand, b = 0 makes these loops run 256 times */
	do {
		*de++ = vars[hl--];
	} while (--b);
	*de = 0xff;
	SortCardsInDuelTempListByID();
	b = vars[DUELVARS_NUMBER_OF_CARDS_IN_HAND];
	hl = DUELVARS_HAND - 1 + b;
	de = wDuelTempList;
	do {
		vars[hl--] = *de++;
	} while (--b);
}

/* shuffles a list of n cards by swapping the position of each card with the position
 * of another random card (Random(n)) */
void ShuffleCards(u8 *list, u8 n)
{
	if (n == 0)
		return; /* return if deck is empty */
	for (u8 i = 0; i < n; i++) {
		u8 j = Random(n);
		u8 t = list[j];
		list[j] = list[i];
		list[i] = t;
	}
}

/* sort a $ff-terminated list of deck index cards by ID (lowest to highest ID).
 * the list is wDuelTempList. */
void SortCardsInDuelTempListByID(void)
{
	wr16(hTempListPtr_ff99, ADDR_wDuelTempList);
	SortCardsInListByID_CheckForListTerminator();
}

/* sort a $ff-terminated list of deck index cards by ID (lowest to highest ID).
 * the pointer to the list is given in hTempListPtr_ff99.
 * sorting by ID rather than deck index means that the order of equal (same ID) cards does not matter,
 * even if they have a different deck index. */
void SortCardsInListByID(void)
{
	do {
		u16 cur = rd16(hTempListPtr_ff99);
		u16 de = cur;
		u16 best = GetCardIDFromDeckIndex_bc(*gbptr(de));
		wr16(hTempCardID_ff9b, best);
		u16 hl = cur + 1;
		while (!(*gbptr(hl) & 0x80)) { /* $ff is the list terminator */
			u16 bc = GetCardIDFromDeckIndex_bc(*gbptr(hl));
			if (!(rd16(hTempCardID_ff9b) < bc)) {
				/* this card has the lowest ID of those checked so far */
				de = hl;
				wr16(hTempCardID_ff9b, bc);
			}
			hl++;
		}
		/* swap the lowest ID card found with the card in the current list position */
		u8 *p = gbptr(cur), *q = gbptr(de);
		u8 t = *p;
		*p = *q;
		*q = t;
		wr16(hTempListPtr_ff99, cur + 1);
	} while (!(*gbptr(rd16(hTempListPtr_ff99)) & 0x80));
}

void SortCardsInListByID_CheckForListTerminator(void)
{
	if (!(*gbptr(rd16(hTempListPtr_ff99)) & 0x80))
		SortCardsInListByID();
}

/* returns the id of the card with the deck index (register bc) */
u16 GetCardIDFromDeckIndex_bc(u8 card)
{
	return _GetCardIDFromDeckIndex(card);
}

/* return [wDuelTempList + i] (also stored in hTempCardIndex_ff98) */
u8 GetCardInDuelTempList_OnlyDeckIndex(u8 i)
{
	u8 a = wDuelTempList[i];
	hTempCardIndex_ff98 = a;
	return a;
}

/* given the deck index (0-59) of a card in [wDuelTempList + i], return:
 *  - the id of the card with that deck index in *card_id (register de)
 *  - [wDuelTempList + i] in hTempCardIndex_ff98 and as return value (register a) */
u8 GetCardInDuelTempList(u8 i, u8 *card_id)
{
	u8 a = wDuelTempList[i];
	hTempCardIndex_ff98 = a;
	u8 id = GetCardIDFromDeckIndex(a);
	if (card_id)
		*card_id = id;
	return a;
}

/* returns (register de) the id of the card with the deck index (0-59) */
u8 GetCardIDFromDeckIndex(u8 card)
{
	return _GetCardIDFromDeckIndex(card);
}

/* remove card from wDuelTempList (it contains a $ff-terminated list of deck indexes)
 * returns carry if no cards remain in the list */
bool RemoveCardFromDuelTempList(u8 card)
{
	u8 *hl = wDuelTempList, *de = wDuelTempList;
	u8 b = 0;
	for (;;) {
		u8 a = *hl++;
		if (a == 0xff) {
			*de = a;
			break;
		}
		if (a == card)
			continue;
		*de++ = a;
		b++;
	}
	return b == 0;
}

/* return the number of cards in wDuelTempList */
u8 CountCardsInDuelTempList(void)
{
	u8 b = 0;
	u8 *hl = wDuelTempList;
	while (*hl++ != 0xff)
		b++;
	return b;
}

/* returns the id of the card with the deck index (0-59) */
u8 _GetCardIDFromDeckIndex(u8 card)
{
	const u8 *deck = (hWhoseTurn == PLAYER_TURN) ? wPlayerDeck : wOpponentDeck;
	/* (the original adds the index to a 16-bit pointer, so any index is "valid") */
	return gWRAM[(u16)(gbaddr(deck) + card) - WRAM_START];
}

/* load data of card with deck index (0-59) to wLoadedCard1; returns the card ID */
u8 LoadCardDataToBuffer1_FromDeckIndex(u8 card)
{
	u8 e = GetCardIDFromDeckIndex(card);
	LoadCardDataToBuffer1_FromCardID(e);
	ConvertSpecialTrainerCardToPokemon(card, &wLoadedCard1);
	return e;
}

/* load data of card with deck index (0-59) to wLoadedCard2; returns the card ID */
u8 LoadCardDataToBuffer2_FromDeckIndex(u8 card)
{
	u8 e = GetCardIDFromDeckIndex(card);
	LoadCardDataToBuffer2_FromCardID(e);
	ConvertSpecialTrainerCardToPokemon(card, &wLoadedCard2);
	return e;
}

/* evolve a turn holder's Pokemon card in the play area slot determined by hTempPlayAreaLocation_ff9d
 * into another turn holder's Pokemon card identified by its deck index (0-59) in hTempCardIndex_ff98.
 * return nc if evolution was successful. */
bool EvolvePokemonCardIfPossible(void)
{
	/* first make sure the attempted evolution is viable */
	if (CheckIfCanEvolveInto(hTempCardIndex_ff98, hTempPlayAreaLocation_ff9d, NULL))
		return true; /* return if it's not capable of evolving into the selected Pokemon */
	EvolvePokemonCard();
	return false;
}

/* evolve a turn holder's Pokemon card in the play area slot determined by hTempPlayAreaLocation_ff9d
 * into another turn holder's Pokemon card identified by its deck index (0-59) in hTempCardIndex_ff98. */
void EvolvePokemonCard(void)
{
	/* place the evolved Pokemon card in the play area location of the pre-evolved Pokemon card */
	u8 e = hTempPlayAreaLocation_ff9d;
	u8 *vars = TurnDuelistVars();
	u8 pre = vars[(u8)(e + DUELVARS_ARENA_CARD)];
	wPreEvolutionPokemonCard = pre; /* save pre-evolved Pokemon card into wPreEvolutionPokemonCard */
	LoadCardDataToBuffer2_FromDeckIndex(pre);
	vars[(u8)(e + DUELVARS_ARENA_CARD)] = hTempCardIndex_ff98;
	LoadCardDataToBuffer1_FromDeckIndex(hTempCardIndex_ff98);
	PutHandCardInPlayArea(hTempCardIndex_ff98, e);
	/* update the Pokemon's HP with the difference */
	u8 *hp = &vars[(u8)(e + DUELVARS_ARENA_CARD_HP)];
	*hp = (u8)(wLoadedCard1.HP - wLoadedCard2.HP + *hp);
	/* reset status (if in arena) and set the flag that prevents it from evolving again this turn */
	vars[(u8)(e + DUELVARS_ARENA_CARD_FLAGS)] = 0;
	vars[(u8)(e + DUELVARS_ARENA_CARD_CHANGED_TYPE)] = 0;
	if (e == 0)
		ClearAllStatusConditions();
	/* set the new evolution stage of the card */
	vars[(u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD_STAGE)] = wLoadedCard1.Stage;
}

/* check if the turn holder's Pokemon card at e can evolve into the turn holder's Pokemon card d.
 * e is the play area location offset (PLAY_AREA_*) of the Pokemon trying to evolve.
 * d is the deck index (0-59) of the Pokemon card that was selected to be the evolution target.
 * return carry if can't evolve, plus *a = 1 (nz) if the reason for it is the card was
 * played this turn, or *a = 0 (z) if the cards are incompatible. */
bool CheckIfCanEvolveInto(u8 d, u8 e, u8 *a)
{
	LoadCardDataToBuffer2_FromDeckIndex(GetTurnDuelistVariable((u8)(e + DUELVARS_ARENA_CARD)));
	LoadCardDataToBuffer1_FromDeckIndex(d);
	if (wLoadedCard1.PreEvoName[0] != wLoadedCard2.Name[0] ||
	    wLoadedCard1.PreEvoName[1] != wLoadedCard2.Name[1]) {
		if (a)
			*a = 0;
		return true; /* incompatible to evolve */
	}
	u8 f = GetTurnDuelistVariable((u8)(e + DUELVARS_ARENA_CARD_FLAGS)) & CAN_EVOLVE_THIS_TURN;
	if (f) {
		if (a)
			*a = f;
		return false;
	}
	/* if the card trying to evolve was played this turn, it can't evolve */
	if (a)
		*a = 1;
	return true;
}

/* check if the turn holder's Pokemon card at e can evolve this turn, and is a basic
 * Pokemon card whose second stage evolution is the turn holder's Pokemon card d.
 * return carry if not basic to stage 2 evolution, or if evolution not possible this turn. */
bool CheckIfCanEvolveInto_BasicToStage2(u8 d, u8 e)
{
	if (!(GetTurnDuelistVariable((u8)(e + DUELVARS_ARENA_CARD_FLAGS)) & CAN_EVOLVE_THIS_TURN))
		return true;
	LoadCardDataToBuffer2_FromDeckIndex(TurnDuelistVars()[(u8)(e + DUELVARS_ARENA_CARD)]);
	LoadCardDataToBuffer1_FromDeckIndex(d);
	LoadCardDataToBuffer1_FromName(rd16(wLoadedCard1.PreEvoName));
	if (wLoadedCard1.PreEvoName[0] != wLoadedCard2.Name[0] ||
	    wLoadedCard1.PreEvoName[1] != wLoadedCard2.Name[1])
		return true;
	return false;
}

/* clear the status, all substatuses, and temporary duelvars of the turn holder's
 * arena Pokemon. called when sending a new Pokemon into the arena.
 * does not reset Headache, since it targets a player rather than a Pokemon. */
void ClearAllStatusConditions(void)
{
	u8 *vars = TurnDuelistVars();
	vars[DUELVARS_ARENA_CARD_STATUS] = NO_STATUS;
	vars[DUELVARS_ARENA_CARD_SUBSTATUS1] = 0;
	vars[DUELVARS_ARENA_CARD_SUBSTATUS2] = 0;
	vars[DUELVARS_ARENA_CARD_CHANGED_WEAKNESS] = 0;
	vars[DUELVARS_ARENA_CARD_CHANGED_RESISTANCE] = 0;
	vars[DUELVARS_ARENA_CARD_SUBSTATUS3] &= ~(1 << SUBSTATUS3_THIS_TURN_DOUBLE_DAMAGE_F);
	/* clears 8 bytes starting at DUELVARS_ARENA_CARD_DISABLED_ATTACK_INDEX ($f2-$f9) */
	for (u8 i = 0; i < 8; i++)
		vars[(u8)(DUELVARS_ARENA_CARD_DISABLED_ATTACK_INDEX + i)] = 0;
}

/* Removes a Pokemon card from the hand and places it in the arena or first available bench slot.
 * If the Pokemon is placed in the arena, the status conditions of the player's arena card are zeroed.
 * return carry if there is no room for more Pokemon; *loc = play area offset (register a/e) */
bool PutHandPokemonCardInPlayArea(u8 card, u8 *loc)
{
	u8 *vars = TurnDuelistVars();
	u8 e = vars[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA];
	if (e >= MAX_PLAY_AREA_POKEMON) {
		if (loc)
			*loc = card;
		return true;
	}
	vars[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]++;
	PutHandCardInPlayArea(card, e);
	vars[(u8)(e + DUELVARS_ARENA_CARD)] = card; /* set card in arena or benchx */
	LoadCardDataToBuffer2_FromDeckIndex(card);
	vars[(u8)(DUELVARS_ARENA_CARD_HP + e)] = wLoadedCard2.HP; /* set card's HP */
	vars[(u8)(DUELVARS_ARENA_CARD_FLAGS + e)] = 0;
	vars[(u8)(DUELVARS_ARENA_CARD_CHANGED_TYPE + e)] = 0;
	vars[(u8)(DUELVARS_ARENA_CARD_ATTACHED_PLUSPOWER + e)] = 0;
	vars[(u8)(DUELVARS_ARENA_CARD_ATTACHED_DEFENDER + e)] = 0;
	vars[(u8)(DUELVARS_ARENA_CARD_STAGE + e)] = wLoadedCard2.Stage; /* set card's evolution stage */
	if (e == 0)
		ClearAllStatusConditions(); /* only call if Pokemon is being placed in the arena */
	if (loc)
		*loc = e;
	return false;
}

/* Removes a card from the hand and changes its location to arena or bench. Given that
 * DUELVARS_ARENA_CARD or DUELVARS_BENCH aren't affected, this function is meant for energy and trainer cards.
 * returns CARD_LOCATION_PLAY_AREA + e */
u8 PutHandCardInPlayArea(u8 card, u8 e)
{
	RemoveCardFromHand(card);
	u8 a = e | CARD_LOCATION_PLAY_AREA;
	TurnDuelistVars()[card] = a;
	return a;
}

/* move the Pokemon card of the turn holder in the
 * PLAY_AREA_* location given in e to the discard pile */
void MovePlayAreaCardToDiscardPile(u8 e)
{
	EmptyPlayAreaSlot(e);
	u8 *vars = TurnDuelistVars();
	vars[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]--;
	for (u8 l = DUELVARS_CARD_LOCATIONS; l < DECK_SIZE; l++) {
		if ((e | CARD_LOCATION_PLAY_AREA) == vars[l])
			PutCardInDiscardPile(l);
	}
}

/* init a turn holder's play area slot to empty
 * which slot (arena or benchx) is determined by the play area location offset (PLAY_AREA_*) in e */
void EmptyPlayAreaSlot(u8 e)
{
	u8 *vars = TurnDuelistVars();
	vars[(u8)(DUELVARS_ARENA_CARD + e)] = 0xff;
	vars[(u8)(DUELVARS_ARENA_CARD_HP + e)] = 0;
	vars[(u8)(DUELVARS_ARENA_CARD_STAGE + e)] = 0;
	vars[(u8)(DUELVARS_ARENA_CARD_CHANGED_TYPE + e)] = 0;
	vars[(u8)(DUELVARS_ARENA_CARD_ATTACHED_DEFENDER + e)] = 0;
	vars[(u8)(DUELVARS_ARENA_CARD_ATTACHED_PLUSPOWER + e)] = 0;
}

/* shift play area Pokemon of both players to the first available play area (arena + benchx) slots */
void ShiftAllPokemonToFirstPlayAreaSlots(void)
{
	ShiftTurnPokemonToFirstPlayAreaSlots();
	SwapTurn();
	ShiftTurnPokemonToFirstPlayAreaSlots();
	SwapTurn();
}

/* shift play area Pokemon of the turn holder to the first available play area (arena + benchx) slots */
void ShiftTurnPokemonToFirstPlayAreaSlots(void)
{
	u8 *vars = TurnDuelistVars();
	u8 e = PLAY_AREA_ARENA;
	for (u8 d = PLAY_AREA_ARENA; d != MAX_PLAY_AREA_POKEMON; d++) {
		if (vars[(u8)(DUELVARS_ARENA_CARD + d)] & 0x80)
			continue; /* empty slot */
		SwapPlayAreaPokemon(d, e);
		e++;
	}
}

/* swap the data of the turn holder's arena Pokemon card with the
 * data of the turn holder's Pokemon card in play area e.
 * reset the status and all substatuses of the arena Pokemon before swapping. */
void SwapArenaWithBenchPokemon(u8 e)
{
	ClearAllStatusConditions();
	SwapPlayAreaPokemon(PLAY_AREA_ARENA, e);
}

/* swap the data of the turn holder's Pokemon card in play area d with the
 * data of the turn holder's Pokemon card in play area e. */
void SwapPlayAreaPokemon(u8 d, u8 e)
{
	if (e == d)
		return;
	u8 *vars = TurnDuelistVars();
	static const u8 swapped[] = {
		DUELVARS_ARENA_CARD, DUELVARS_ARENA_CARD_HP, DUELVARS_ARENA_CARD_FLAGS,
		DUELVARS_ARENA_CARD_STAGE, DUELVARS_ARENA_CARD_CHANGED_TYPE,
		DUELVARS_ARENA_CARD_ATTACHED_PLUSPOWER, DUELVARS_ARENA_CARD_ATTACHED_DEFENDER,
	};
	for (unsigned i = 0; i < sizeof(swapped); i++) {
		u8 *p = &vars[(u8)(swapped[i] + e)], *q = &vars[(u8)(swapped[i] + d)];
		u8 t = *q;
		*q = *p;
		*p = t;
	}
	d |= CARD_LOCATION_PLAY_AREA;
	e |= CARD_LOCATION_PLAY_AREA;
	/* update card locations of the two swapped cards */
	for (u8 l = DUELVARS_CARD_LOCATIONS; l < DECK_SIZE; l++) {
		if (vars[l] == e)
			vars[l] = d;
		else if (vars[l] == d)
			vars[l] = e;
	}
}

/* Find which and how many energy cards are attached to the turn holder's Pokemon card in the arena,
 * or a Pokemon card in the bench, depending on the value of register e.
 * Feedback is returned in wAttachedEnergies and wTotalAttachedEnergies. */
void GetPlayAreaCardAttachedEnergies(u8 e)
{
	for (u8 i = 0; i < NUM_TYPES; i++)
		wAttachedEnergies[i] = 0;
	e |= CARD_LOCATION_PLAY_AREA; /* if e is non-0, a bench location is checked instead */
	u8 *vars = TurnDuelistVars();
	for (u8 l = DUELVARS_CARD_LOCATIONS; l < DECK_SIZE; l++) {
		if (vars[l] != e)
			continue;
		LoadCardDataToBuffer2_FromDeckIndex(l);
		u8 a = wLoadedCard2.Type;
		if (!(a & (1 << TYPE_ENERGY_F)))
			continue;
		a &= TYPE_PKMN; /* zero bit 3 to extract the type */
		wAttachedEnergies[a]++; /* increment the number of energy cards of this type */
		if (a == COLORLESS)
			wAttachedEnergies[a]++; /* each colorless energy counts as two */
	}
	u8 sum = 0;
	for (u8 i = 0; i < NUM_TYPES; i++)
		sum += wAttachedEnergies[i];
	wTotalAttachedEnergies = sum;
}

/* returns how many times card id e can be found in location b
 * of the duelist whose duel variables are at turn << 8 (register h) */
u8 CountCardIDInLocation(u8 turn, u8 b, u8 e)
{
	u8 *vars = DuelVarsOf(turn);
	u8 c = 0;
	for (u8 l = DUELVARS_CARD_LOCATIONS; l < DECK_SIZE; l++) {
		if (vars[l] != b)
			continue;
		if (_GetCardIDFromDeckIndex(l) != e)
			continue;
		c++;
	}
	return c;
}

/* returns duelvar off of the player whose turn it is */
u8 GetTurnDuelistVariable(u8 off)
{
	return TurnDuelistVars()[off];
}

/* returns duelvar off of the player whose turn it is not */
u8 GetNonTurnDuelistVariable(u8 off)
{
	return NonTurnDuelistVars()[off];
}

/* when playing a Pokemon card, initializes some variables according to the
 * card played, and checks if the played card has Pokemon Power to show it to
 * the player, and possibly to use it if it triggers when the card is played. */
void ProcessPlayedPokemonCard(void)
{
	TRACE_FN();
	ClearChangedTypesIfMuk(hTempCardIndex_ff98);
	CopyAttackDataAndDamage_FromDeckIndex(hTempCardIndex_ff98, FIRST_ATTACK_OR_PKMN_POWER);
	UpdateArenaCardIDsAndClearTwoTurnDuelVars();
	hTempCardIndex_ff9f = hTempCardIndex_ff98;
	wTempTurnDuelistCardID = GetCardIDFromDeckIndex(hTempCardIndex_ff98);
	if (wLoadedAttack.Category != POKEMON_POWER)
		return;
	DisplayUsePokemonPowerScreen();
	LoadCardDataToBuffer1_FromDeckIndex(hTempCardIndex_ff98);
	LoadTxRam2(rd16(wLoadedCard1.Name));
	DrawWideTextBox_WaitForInput(HavePokemonPowerText);
	ExchangeRNG();
	if (wLoadedCard1.ID != MUK) {
		if (CheckIsIncapableOfUsingPkmnPower(PLAY_AREA_BENCH_1)) { /* bench: don't check status */
			DisplayUsePokemonPowerScreen();
			DrawWideTextBox_WaitForInput(UnableToUsePkmnPowerDueToToxicGasText);
			ExchangeRNG();
			return;
		}
	}
	/* use_pokemon_power */
	if (!GetEffectCommandFunction(rd16(wLoadedAttack.EffectCommands), EFFECTCMDTYPE_PKMN_POWER_TRIGGER))
		return; /* return if command not found */
	DrawDuelMainScene();
	LoadCardDataToBuffer1_FromDeckIndex(hTempCardIndex_ff9f);
	wTxRam2[0] = wLoadedCard1.Name[0];
	wTxRam2[1] = wLoadedCard1.Name[1];
	wTxRam2_b[0] = wLoadedAttack.Name[0];
	wTxRam2_b[1] = wLoadedAttack.Name[1];
	DrawWideTextBox_WaitForInput(WillUseThePokemonPowerText);
	ExchangeRNG();
	ResetAttackAnimationIsPlaying();
	TryExecuteEffectCommandFunction(EFFECTCMDTYPE_PKMN_POWER_TRIGGER);
}

/* copies, given a card identified by its card ID:
 * - e into wSelectedAttack and d into hTempCardIndex_ff9f
 * - Attack1 (if e == 0) or Attack2 (if e == 1) data into wLoadedAttack
 * - Also from that attack, its Damage field into wDamage
 * finally, clears wNoDamageOrEffect and wDealtDamage */
void CopyAttackDataAndDamage_FromCardID(u8 card_id, u8 d, u8 e)
{
	wSelectedAttack = e;
	hTempCardIndex_ff9f = d;
	LoadCardDataToBuffer1_FromCardID(card_id);
	CopyAttackDataAndDamage(e);
}

/* copies, given a card identified by register d (0-59 deck index):
 * - e into wSelectedAttack and d into hTempCardIndex_ff9f
 * - Attack1 (if e == 0) or Attack2 (if e == 1) data into wLoadedAttack
 * - Also from that attack, its Damage field into wDamage
 * finally, clears wNoDamageOrEffect and wDealtDamage */
void CopyAttackDataAndDamage_FromDeckIndex(u8 d, u8 e)
{
	wSelectedAttack = e;
	hTempCardIndex_ff9f = d;
	LoadCardDataToBuffer1_FromDeckIndex(d);
	CopyAttackDataAndDamage(e);
}

void CopyAttackDataAndDamage(u8 e)
{
	wTempCardID_ccc2 = wLoadedCard1.ID;
	/* (dec e; jr nz) -> only e == 1 selects the second attack */
	const struct AtkData *src = (e == 1) ? &wLoadedCard1.Atk2 : &wLoadedCard1.Atk1;
	wLoadedAttack = *src;
	wDamage[0] = wLoadedAttack.Damage;
	wDamage[1] = 0;
	wNoDamageOrEffect = 0;
	wDealtDamage[0] = 0;
	wDealtDamage[1] = 0;
}

/* inits hTempCardIndex_ff9f and wTempTurnDuelistCardID to the turn holder's arena card,
 * wTempNonTurnDuelistCardID to the non-turn holder's arena card, and zeroes other temp
 * variables that only last between each two-player turn.
 * this is called when a Pokemon card is played or when an attack is used */
void UpdateArenaCardIDsAndClearTwoTurnDuelVars(void)
{
	u8 a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD);
	hTempCardIndex_ff9f = a;
	wTempTurnDuelistCardID = GetCardIDFromDeckIndex(a);
	SwapTurn();
	wTempNonTurnDuelistCardID = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	SwapTurn();
	wSentAttackDataToLinkOpponent = 0;
	wStatusConditionQueueIndex = 0;
	wEffectFailed = 0;
	wIsDamageToSelf = 0;
	wDefendingWasForcedToSwitch = 0;
	wMetronomeEnergyCost = 0;
	wNoEffectFromWhichStatus = 0;
	ClearNonTurnTemporaryDuelvars_CopyStatus();
}

/* Use an attack (from DuelMenu_Attack) or a Pokemon Power (from DuelMenu_PkmnPower).
 * returns carry if it could not be used */
bool UseAttackOrPokemonPower(void)
{
	TRACE_FN();
	wPlayerAttackingAttackIndex = wSelectedAttack;
	wPlayerAttackingCardIndex = hTempCardIndex_ff9f;
	wPlayerAttackingCardID = wTempCardID_ccc2;
	if (wLoadedAttack.Category == POKEMON_POWER)
		return UsePokemonPower();
	UpdateArenaCardIDsAndClearTwoTurnDuelVars();
	if (TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_1))
		return DrawWideTextBox_WaitForInput_ReturnCarry(gTextID);
	if (CheckSandAttackOrSmokescreenSubstatus()) {
		SendAttackDataToLinkOpponent();
		if (HandleSandAttackOrSmokescreenSubstatus())
			return ClearNonTurnTemporaryDuelvars_ResetCarry();
		if (TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_2))
			return true;
	} else {
		if (TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_2))
			return true;
		SendAttackDataToLinkOpponent();
	}
	SetOppAction_SerialSendDuelData(OPPACTION_USE_ATTACK);
	TryExecuteEffectCommandFunction(EFFECTCMDTYPE_DISCARD_ENERGY);
	if (CheckSelfConfusionDamage())
		return HandleConfusionDamageToSelf();
	DrawDuelMainScene_PrintPokemonsAttackText();
	WaitForWideTextBoxInput();
	ExchangeRNG();
	TryExecuteEffectCommandFunction(EFFECTCMDTYPE_REQUIRE_SELECTION);
	SetOppAction_SerialSendDuelData(OPPACTION_ATTACK_ANIM_AND_DAMAGE);
	return PlayAttackAnimation_DealAttackDamage();
}

/* returns nc */
bool PlayAttackAnimation_DealAttackDamage(void)
{
	TRACE_FN();
	ResetAttackAnimationIsPlaying();
	if (!(wLoadedAttack.Category & RESIDUAL)) {
		SwapTurn();
		HandleNoDamageOrEffectSubstatus();
		SwapTurn();
	}
	/* deal_damage */
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	TryExecuteEffectCommandFunction(EFFECTCMDTYPE_BEFORE_DAMAGE);
	u16 de = ApplyDamageModifiers_DamageToTarget();
	de = ApplyTransparencyIfApplicable(de);
	wr16(wDealtDamage, de);
	u8 b = PLAY_AREA_ARENA;
	u8 c = wDamageEffectiveness;
	u8 *hl = &NonTurnDuelistVars()[DUELVARS_ARENA_CARD_HP];
	PlayAttackAnimation(de, b, c, (u8)(gbaddr(hl) >> 8));
	PlayStatusConditionQueueAnimations();
	WaitAttackAnimation();
	SubtractHP(hl, de);
	if (wDuelDisplayedScreen == DUEL_MAIN_SCENE)
		DrawDuelHUDs();
	PrintKnockedOutIfHLZero(hl);
	return HandleAfterDamageEffects();
}

/* unreferenced */
bool Func_17ed(u16 text)
{
	DrawWideTextBox_WaitForInput(text);
	wDamage[0] = 0;
	wDamage[1] = 0;
	wNoDamageOrEffect = NO_DAMAGE_OR_EFFECT_AGILITY;
	return HandleAfterDamageEffects();
}

/* returns nc */
bool HandleAfterDamageEffects(void)
{
	TRACE_FN();
	u8 saved = wTempNonTurnDuelistCardID;
	TryExecuteEffectCommandFunction(EFFECTCMDTYPE_AFTER_DAMAGE);
	wTempNonTurnDuelistCardID = saved;
	HandleStrikesBack_AgainstResidualAttack();
	ApplyStatusConditionQueue();
	Func_1bb4();
	UpdateArenaCardLastTurnDamage();
	HandleDestinyBondAndBetweenTurnKnockOuts();
	return false;
}

bool DisplayUsePokemonPowerScreen_WaitForInput(u16 text)
{
	DisplayUsePokemonPowerScreen();
	return DrawWideTextBox_WaitForInput_ReturnCarry(text);
}

bool DrawWideTextBox_WaitForInput_ReturnCarry(u16 text)
{
	DrawWideTextBox_WaitForInput(text);
	return true;
}

bool ClearNonTurnTemporaryDuelvars_ResetCarry(void)
{
	ClearNonTurnTemporaryDuelvars();
	return false;
}

/* called when attacker deals damage to itself due to confusion
 * display the corresponding animation and deal 20 damage to self. returns nc */
bool HandleConfusionDamageToSelf(void)
{
	TRACE_FN();
	DrawDuelMainScene();
	wIsDamageToSelf = 1;
	DrawWideTextBox_PrintText(DamageToSelfDueToConfusionText);
	wLoadedAttack.Animation = ATK_ANIM_CONFUSION_HIT;
	DealConfusionDamageToSelf(20);
	Func_1bb4();
	HandleDestinyBondAndBetweenTurnKnockOuts();
	ClearNonTurnTemporaryDuelvars();
	return false;
}

/* use Pokemon Power; returns carry if it could not be used */
bool UsePokemonPower(void)
{
	TRACE_FN();
	ResetAttackAnimationIsPlaying();
	if (TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_2))
		return DisplayUsePokemonPowerScreen_WaitForInput(gTextID);
	if (TryExecuteEffectCommandFunction(EFFECTCMDTYPE_REQUIRE_SELECTION))
		return true;
	SetOppAction_SerialSendDuelData(OPPACTION_USE_PKMN_POWER);
	ExchangeRNG();
	SetOppAction_SerialSendDuelData(OPPACTION_EXECUTE_PKMN_POWER_EFFECT);
	TryExecuteEffectCommandFunction(EFFECTCMDTYPE_BEFORE_DAMAGE);
	SetOppAction_SerialSendDuelData(OPPACTION_DUEL_MAIN_SCENE);
	return false;
}

/* called by UseAttackOrPokemonPower (on an attack only)
 * in a link duel, it's used to send the other game data about the
 * attack being in use, triggering a call to OppAction_BeginUseAttack in the receiver */
void SendAttackDataToLinkOpponent(void)
{
	if (wSentAttackDataToLinkOpponent)
		return;
	u8 saved_ffa0 = hTemp_ffa0;
	u8 saved_ff9f = hTempCardIndex_ff9f;
	wSentAttackDataToLinkOpponent = TRUE;
	hTempCardIndex_ff9f = wPlayerAttackingCardIndex;
	hTemp_ffa0 = wPlayerAttackingAttackIndex;
	SetOppAction_SerialSendDuelData(OPPACTION_BEGIN_ATTACK);
	ExchangeRNG();
	hTempCardIndex_ff9f = saved_ff9f;
	hTemp_ffa0 = saved_ffa0;
}

/* returns the (possibly zeroed) damage */
u16 ApplyTransparencyIfApplicable(u16 de)
{
	if (wLoadedAttack.Category & (1 << RESIDUAL_F))
		return de;
	if (wNoDamageOrEffect)
		return de;
	if (de == 0) {
		if (GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_SUBSTATUS2) == 0) {
			if (wStatusConditionQueueIndex == 0)
				return de;
		}
	}
	SwapTurn();
	wTempPlayAreaLocation_cceb = PLAY_AREA_ARENA;
	bool c = HandleTransparency();
	SwapTurn();
	if (!c)
		return de;
	DrawDuelMainScene();
	NonTurnDuelistVars()[DUELVARS_ARENA_CARD_SUBSTATUS2] = 0;
	return 0;
}

/* return carry and TRUE in wConfusionAttackCheckWasUnsuccessful if damage will be dealt to oneself due to confusion */
bool CheckSelfConfusionDamage(void)
{
	wConfusionAttackCheckWasUnsuccessful = FALSE;
	if ((GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS) & CNF_SLP_PRZ) != CONFUSED)
		return false;
	if (TossCoin(ConfusionCheckDamageText))
		return false; /* no confusion damage */
	wConfusionAttackCheckWasUnsuccessful = TRUE;
	return true;
}

/* play the trainer card with deck index at hTempCardIndex_ff98.
 * a trainer card is like an attack effect, with its own effect commands.
 * return nc if the card was played, carry if it wasn't. */
bool PlayTrainerCard(void)
{
	TRACE_FN();
	if (CheckCantUseTrainerDueToEffect())
		goto cant_use;
	hTempCardIndex_ff9f = hTempCardIndex_ff98;
	LoadNonPokemonCardEffectCommands();
	if (TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_1))
		goto cant_use;
	if (TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_2))
		return false; /* (.done does `or a`) */
	SetOppAction_SerialSendDuelData(OPPACTION_PLAY_TRAINER);
	DisplayUsedTrainerCardDetailScreen();
	ExchangeRNG();
	TryExecuteEffectCommandFunction(EFFECTCMDTYPE_DISCARD_ENERGY);
	TryExecuteEffectCommandFunction(EFFECTCMDTYPE_REQUIRE_SELECTION);
	SetOppAction_SerialSendDuelData(OPPACTION_EXECUTE_TRAINER_EFFECTS);
	TryExecuteEffectCommandFunction(EFFECTCMDTYPE_BEFORE_DAMAGE);
	MoveHandCardToDiscardPile(hTempCardIndex_ff9f);
	ExchangeRNG();
	return false;
cant_use:
	DrawWideTextBox_WaitForInput(gTextID);
	return true;
}

/* loads the effect commands of a (trainer or energy) card with deck index (0-59) at hTempCardIndex_ff9f
 * into wLoadedAttackEffectCommands. in practice, only used for trainer cards */
void LoadNonPokemonCardEffectCommands(void)
{
	LoadCardDataToBuffer1_FromDeckIndex(hTempCardIndex_ff9f);
	wLoadedAttack.EffectCommands[0] = wLoadedCard1.EffectCommands[0];
	wLoadedAttack.EffectCommands[1] = wLoadedCard1.EffectCommands[1];
}

/* Make turn holder deal a damage to self due to recoil (e.g. Thrash, Selfdestruct)
 * display recoil animation */
void DealRecoilDamageToSelf(u8 a)
{
	wLoadedAttack.Animation = ATK_ANIM_RECOIL_HIT;
	DealConfusionDamageToSelf(a);
}

/* Make turn holder deal a damage to self due to confusion
 * display animation at wLoadedAttackAnimation */
void DealConfusionDamageToSelf(u8 a)
{
	wDamage[0] = a;
	wDamage[1] = 0;
	u8 saved_no_damage = wNoDamageOrEffect;
	wNoDamageOrEffect = 0;
	ResetAttackAnimationIsPlaying();
	u8 saved_nonturn = wTempNonTurnDuelistCardID;
	wTempNonTurnDuelistCardID = wTempTurnDuelistCardID;
	u16 de = ApplyDamageModifiers_DamageToSelf();
	u8 c = wDamageEffectiveness;
	u8 *hl = &TurnDuelistVars()[DUELVARS_ARENA_CARD_HP];
	PlayAttackAnimation_DealAttackDamageSimple(hl, de, PLAY_AREA_ARENA, c);
	PrintKnockedOutIfHLZero(hl);
	wTempNonTurnDuelistCardID = saved_nonturn;
	wNoDamageOrEffect = saved_no_damage;
}

/* given a damage value at wDamage:
 * - if the non-turn holder's arena card is weak to the turn holder's arena card color: double damage
 * - if the non-turn holder's arena card resists the turn holder's arena card color: reduce damage by 30
 * - also apply PlusPower, Defender, and other kinds of damage reduction accordingly
 * return resulting damage (register de) */
u16 ApplyDamageModifiers_DamageToTarget(void)
{
	wDamageEffectiveness = 0;
	if ((wDamage[0] | wDamage[1]) == 0)
		return 0;
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	u8 d = wDamage[1], e = wDamage[0];
	u16 de;
	if (d & (1 << UNAFFECTED_BY_WEAKNESS_RESISTANCE_F)) {
		d &= ~(1 << UNAFFECTED_BY_WEAKNESS_RESISTANCE_F);
		wDamageEffectiveness = 0;
		de = HandleDoubleDamageSubstatus((u16)(d << 8 | e));
	} else {
		de = HandleDoubleDamageSubstatus((u16)(d << 8 | e));
		if (de == 0)
			return de;
		u8 b = TranslateColorToWR(GetPlayAreaCardColor(hTempPlayAreaLocation_ff9d));
		SwapTurn();
		u8 w = GetArenaCardWeakness();
		SwapTurn();
		if (w & b) {
			de = (u16)(de << 1);
			wDamageEffectiveness |= 1 << WEAKNESS;
		}
		SwapTurn();
		u8 r = GetArenaCardResistance();
		SwapTurn();
		if (r & b) {
			de = (u16)(de - 30);
			wDamageEffectiveness |= 1 << RESISTANCE;
		}
	}
	/* check_pluspower_and_defender */
	de = ApplyAttachedPlusPower(CARD_LOCATION_ARENA, de);
	SwapTurn();
	de = ApplyAttachedDefender(CARD_LOCATION_ARENA, de);
	de = HandleDamageReduction(de);
	if (de & 0x8000)
		de = 0;
	SwapTurn();
	return de;
}

const u8 InvertedPowersOf2[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };

/* convert a color to its equivalent WR_* (weakness/resistance) value.
 * the original indexes InvertedPowersOf2 with any color, reading the ROM bytes that
 * follow the 8-entry table for colors >= 8 (e.g. TYPE_TRAINER through HandleAIShift) */
u8 TranslateColorToWR(u8 color)
{
	static const u8 *table;
	if (!table)
		table = RomPtr(0x00, RomAddr("InvertedPowersOf2"));
	return table[color];
}

/* given a damage value at wDamage:
 * - if the turn holder's arena card is weak to its own color: double damage
 * - if the turn holder's arena card resists its own color: reduce damage by 30
 * return resulting damage (register de) */
u16 ApplyDamageModifiers_DamageToSelf(void)
{
	wDamageEffectiveness = 0;
	if ((wDamage[0] | wDamage[1]) == 0)
		return 0;
	u16 de = rd16(wDamage);
	u8 b = TranslateColorToWR(GetArenaCardColor());
	if (GetArenaCardWeakness() & b) {
		de = (u16)(de << 1);
		wDamageEffectiveness |= 1 << WEAKNESS;
	}
	if (GetArenaCardResistance() & b) {
		de = (u16)(de - 30);
		wDamageEffectiveness |= 1 << RESISTANCE;
	}
	de = ApplyAttachedPlusPower(CARD_LOCATION_ARENA, de);
	de = ApplyAttachedDefender(CARD_LOCATION_ARENA, de);
	if (de & 0x8000) /* test for underflow */
		return 0;
	return de;
}

/* increases de by 10 points for each PlusPower found in location b (of the turn holder) */
u16 ApplyAttachedPlusPower(u8 b, u16 de)
{
	u8 n = CountCardIDInLocation(hWhoseTurn, b, PLUSPOWER);
	return (u16)(de + HtimesL(10, n));
}

/* reduces de by 20 points for each Defender found in location b (of the turn holder) */
u16 ApplyAttachedDefender(u8 b, u16 de)
{
	u8 n = CountCardIDInLocation(hWhoseTurn, b, DEFENDER);
	return (u16)(de - HtimesL(20, n));
}

/* hl: address to subtract HP from
 * de: how much HP to subtract (damage to deal)
 * returns carry if the HP does not become 0 as a result */
bool SubtractHP(u8 *hl, u16 de)
{
	u8 e = (u8)de, d = (u8)(de >> 8);
	u8 old = *hl;
	*hl = (u8)(old - e);
	u8 borrow = old < e;
	u8 a = (u8)(0 - d - borrow);
	if (a & 0x80)
		*hl = 0;
	return *hl != 0;
}

/* given a play area location offset, check if the turn holder's Pokemon card in
 * that location has no HP left, and, if so, print that it was knocked out.
 * returns carry if it was knocked out */
bool PrintPlayAreaCardKnockedOutIfNoHP(u8 e)
{
	if (GetTurnDuelistVariable((u8)(e + DUELVARS_ARENA_CARD_HP)) != 0)
		return false; /* return if arena card has non-0 HP */
	u8 saved = wTempNonTurnDuelistCardID;
	LoadCardDataToBuffer1_FromDeckIndex(GetTurnDuelistVariable((u8)(e + DUELVARS_ARENA_CARD)));
	wTempNonTurnDuelistCardID = wLoadedCard1.ID;
	PrintKnockedOut();
	wTempNonTurnDuelistCardID = saved;
	return true;
}

/* returns carry if [hl] (remaining HP) is zero, after printing the knock out */
bool PrintKnockedOutIfHLZero(const u8 *hl)
{
	if (*hl != 0)
		return false;
	return PrintKnockedOut();
}

/* print in a text box that the Pokemon card at wTempNonTurnDuelistCardID
 * was knocked out and wait 40 frames */
bool PrintKnockedOut(void)
{
	LoadCardDataToBuffer1_FromCardID(wTempNonTurnDuelistCardID);
	LoadTxRam2(rd16(wLoadedCard1.Name));
	DrawWideTextBox_PrintText(WasKnockedOutText);
	WaitFrames(40);
	return true;
}

/* deal damage to turn holder's Pokemon card at play area location at b (PLAY_AREA_*).
 * damage to deal is given in de.
 * shows the defending player's play area screen when dealing the damage
 * instead of the main duel interface with regular attack animation. */
void DealDamageToPlayAreaPokemon_RegularAnim(u8 b, u16 de)
{
	wLoadedAttack.Animation = ATK_ANIM_BENCH_HIT;
	DealDamageToPlayAreaPokemon(b, de);
}

/* deal damage to turn holder's Pokemon card at play area location at b (PLAY_AREA_*).
 * damage to deal is given in de.
 * plays animation that is loaded in wLoadedAttackAnimation. */
void DealDamageToPlayAreaPokemon(u8 b, u16 de)
{
	TRACE_FN();
	wTempPlayAreaLocation_cceb = b;
	if (b == PLAY_AREA_ARENA && wNoDamageOrEffect)
		return;
	wNoDamageOrEffect = 0;
	wTempNonTurnDuelistCardID = GetCardIDFromDeckIndex(
		GetTurnDuelistVariable((u8)(wTempPlayAreaLocation_cceb + DUELVARS_ARENA_CARD)));
	if (wTempPlayAreaLocation_cceb == PLAY_AREA_ARENA) {
		if (wIsDamageToSelf) {
			de = ApplyAttachedPlusPower(CARD_LOCATION_ARENA, de);
		} else {
			SwapTurn();
			de = ApplyAttachedPlusPower(CARD_LOCATION_ARENA, de);
			SwapTurn();
		}
	}
	if (wLoadedAttack.Category != POKEMON_POWER)
		de = ApplyAttachedDefender(wTempPlayAreaLocation_cceb | CARD_LOCATION_PLAY_AREA, de);
	if (wTempPlayAreaLocation_cceb == PLAY_AREA_ARENA) {
		HandleNoDamageOrEffectSubstatus();
		de = HandleDamageReduction(de);
	}
	if (de & 0x8000)
		de = 0;
	de = HandleDamageReductionOrNoDamageFromPkmnPowerEffects(de);
	b = wTempPlayAreaLocation_cceb;
	if (b == PLAY_AREA_ARENA) {
		/* if arena Pokemon, add damage at de to [wDealtDamage] */
		wr16(wDealtDamage, (u16)(rd16(wDealtDamage) + de));
	}
	u8 *hl = &TurnDuelistVars()[(u8)(b + DUELVARS_ARENA_CARD_HP)];
	u8 hp_before = *hl;
	PlayAttackAnimation_DealAttackDamageSimple(hl, de, b, 0);
	if (hp_before != 0)
		PrintKnockedOutIfHLZero(hl);
	HandleStrikesBack_AgainstDamagingAttack(de);
}

/* draw duel main scene, then print the "<Pokemon Lvxx>'s <attack>" text */
void DrawDuelMainScene_PrintPokemonsAttackText(void)
{
	DrawDuelMainScene();
	PrintPokemonsAttackText();
}

/* print the "<Pokemon Lvxx>'s <attack>" text
 * The Pokemon's name is the turn holder's arena Pokemon, and the
 * attack's name is taken from wLoadedAttackName. */
void PrintPokemonsAttackText(void)
{
	LoadCardDataToBuffer1_FromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	CopyCardNameAndLevel(18);
	/* zero wTxRam2 so that the name & level text just loaded to wDefaultText is printed */
	wTxRam2[0] = 0;
	wTxRam2[1] = 0;
	wTxRam2_b[0] = wLoadedAttack.Name[0];
	wTxRam2_b[1] = wLoadedAttack.Name[1];
	DrawWideTextBox_PrintText(PokemonsAttackText);
}

void Func_1bb4(void)
{
	FinishQueuedAnimations();
	DrawDuelMainScene();
	DrawDuelHUDs();
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	PrintFailedEffectText();
	WaitForWideTextBoxInput();
	ExchangeRNG();
}

/* prints one of the ThereWasNoEffectFrom*Text if wEffectFailed contains EFFECT_FAILED_NO_EFFECT,
 * and prints WasUnsuccessfulText if wEffectFailed contains EFFECT_FAILED_UNSUCCESSFUL */
bool PrintFailedEffectText(void)
{
	if (wEffectFailed == 0)
		return false;
	if (wEffectFailed == 1) {
		DrawWideTextBox_PrintText(PrintThereWasNoEffectFromStatusText());
		return true;
	}
	LoadCardDataToBuffer1_FromDeckIndex(GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)));
	CopyCardNameAndLevel(18);
	/* zero wTxRam2 so that the name & level text just loaded to wDefaultText is printed */
	LoadTxRam2(0);
	wTxRam2_b[0] = wLoadedAttack.Name[0];
	wTxRam2_b[1] = wLoadedAttack.Name[1];
	DrawWideTextBox_PrintText(WasUnsuccessfulText);
	return true;
}

/* return the retreat cost of the turn holder's arena or bench Pokemon
 * given the PLAY_AREA_* value in hTempPlayAreaLocation_ff9d */
u8 GetPlayAreaCardRetreatCost(void)
{
	LoadCardDataToBuffer1_FromDeckIndex(GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)));
	return GetLoadedCard1RetreatCost();
}

/* move all of the duelist's (turn << 8 = register h) cards with ID card_id to the
 * discard pile that are currently in the Play Area.
 * this is used to discard all attached PlusPowers and Defenders */
void MoveCardToDiscardPileIfInPlayArea(u8 turn, u8 card_id)
{
	u8 *vars = DuelVarsOf(turn);
	for (u8 l = DUELVARS_CARD_LOCATIONS; l < DECK_SIZE; l++) {
		if (!(vars[l] & CARD_LOCATION_PLAY_AREA))
			continue;
		if (GetCardIDFromDeckIndex(l) != card_id)
			continue;
		PutCardInDiscardPile(l);
	}
}

/* calculate damage and max HP of card at PLAY_AREA_* in e.
 * returns damage (register a), *max_hp = max HP (register c) */
u8 GetCardDamageAndMaxHP(u8 e, u8 *max_hp)
{
	LoadCardDataToBuffer2_FromDeckIndex(GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + e)));
	u8 hp = GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD_HP + e));
	u8 c = wLoadedCard2.HP;
	if (max_hp)
		*max_hp = c;
	return (u8)(c - hp);
}

/* check if a flag of wLoadedAttack is set
 * input: a = %fffffbbb, where fffff = flag address counting from wLoadedAttackFlag1, bbb = flag bit
 * return carry if the flag is set */
bool CheckLoadedAttackFlag(u8 a)
{
	u8 b = PowersOf2[a & 7];
	u8 e = (a >> 3) & 0x1f;
	return (gWRAM[ADDR_wLoadedAttackFlag1 - WRAM_START + e] & b) != 0;
}

/* hWhoseTurn <-- hWhoseTurn ^ 1 */
void SwapTurn(void)
{
	hWhoseTurn = (hWhoseTurn == PLAYER_TURN) ? OPPONENT_TURN : PLAYER_TURN;
}
