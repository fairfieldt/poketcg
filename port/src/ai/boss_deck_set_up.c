/*
 * Port of src/engine/duel/ai/boss_deck_set_up.asm (INCLUDEd in engine/duel/ai/core.asm).
 */
#include "ai/ai.h"

static bool SetUpBossStartingHandAndDeck_CheckIfIDIsInList(u8 a);

/* sets up the initial hand of boss deck.
 * always draws at least 2 Basic Pokemon cards and 2 Energy cards.
 * also sets up so that the next cards to be drawn have
 * some minimum number of Basic Pokemon and Energy cards. */
void SetUpBossStartingHandAndDeck(void)
{
	TRACE_FN();
	u8 a, b;
	const u8 *hl;

	/* shuffle all hand cards in deck
	 * (hl stays at the first hand slot: RemoveCardFromHand shifts the hand down) */
	{
		const u8 *hand = TurnDuelistVars() + DUELVARS_HAND;
		b = STARTING_HAND_SIZE;
		do {
			a = *hand;
			RemoveCardFromHand(a);
			ReturnCardToDeck(a);
		} while (--b);
	}
	goto count_energy_basic;

shuffle_deck:
	ShuffleDeck();

	/* count number of Energy and basic Pokemon cards
	 * in the first STARTING_HAND_SIZE in deck. */
count_energy_basic:
	wAISetupBasicPokemonCount = 0;
	wAISetupEnergyCount[0] = 0;

	hl = TurnDuelistVars() + DUELVARS_DECK_CARDS;
	b = STARTING_HAND_SIZE;
	do {
		LoadCardDataToBuffer1_FromDeckIndex(*hl++);
		a = wLoadedCard1Type;
		if (a < TYPE_ENERGY) {
			/* .pokemon_card_1 */
			if (wLoadedCard1Stage != 0)
				continue; /* not basic */
			wAISetupBasicPokemonCount = (u8)(wAISetupBasicPokemonCount + 1);
			continue;
		}
		if (a == TYPE_TRAINER)
			continue;
		/* energy card */
		wAISetupEnergyCount[0] = (u8)(wAISetupEnergyCount[0] + 1);
		/* .next_card_deck_1 */
	} while (--b);

	/* tally the number of Energy and basic Pokemon cards
	 * and if any of them is smaller than 2, re-shuffle deck. */
	if (wAISetupBasicPokemonCount < 2)
		goto shuffle_deck;
	if (wAISetupEnergyCount[0] < 2)
		goto shuffle_deck;

	/* now check the following 6 cards (prize cards).
	 * re-shuffle deck if any of these cards is listed in wAICardListAvoidPrize. */
	b = 6;
	do {
		if (SetUpBossStartingHandAndDeck_CheckIfIDIsInList(*hl++))
			goto shuffle_deck;
	} while (--b);

	/* finally, check 6 cards after that.
	 * if Energy or Basic Pokemon counter is below 4
	 * (counting with the ones found in the initial hand)
	 * then re-shuffle deck. */
	b = 6;
	do {
		LoadCardDataToBuffer1_FromDeckIndex(*hl++);
		a = wLoadedCard1Type;
		if (a < TYPE_ENERGY) {
			/* .pokemon_card_2 */
			if (wLoadedCard1Stage != 0)
				continue;
			wAISetupBasicPokemonCount = (u8)(wAISetupBasicPokemonCount + 1);
			continue;
		}
		if (a == TYPE_TRAINER)
			continue;
		/* energy card */
		wAISetupEnergyCount[0] = (u8)(wAISetupEnergyCount[0] + 1);
		/* .next_card_deck_2 */
	} while (--b);

	if (wAISetupBasicPokemonCount < 4)
		goto shuffle_deck;
	if (wAISetupEnergyCount[0] < 4)
		goto shuffle_deck;

	/* draw new set of hand cards */
	hl = TurnDuelistVars() + DUELVARS_DECK_CARDS;
	b = STARTING_HAND_SIZE;
	do {
		a = *hl++;
		SearchCardInDeckAndAddToHand(a);
		AddCardToHand(a);
	} while (--b);
}

/* expectation: return carry if card ID corresponding
 * to the input deck index is listed in wAICardListAvoidPrize;
 * reality: always returns no carry
 * input:
 *	- a = deck index of card to check */
static bool SetUpBossStartingHandAndDeck_CheckIfIDIsInList(u8 a)
{
	u8 b = a;
	if (wAICardListAvoidPrize[1] == 0)
		return false; /* null */
	const u8 *hl = AIListPtr(rd16(wAICardListAvoidPrize));

	u8 e = GetCardIDFromDeckIndex(b);
	/* .loop_id_list */
	a = *hl++;
	/* cp a ; bug, should be 'or a'
	 * `cp a` always sets z, so this always jumps to .false at the first byte
	 * (the rest of the loop, `cp e / jr nz, .loop_id_list / scf`, is never reached) */
	(void)a;
	(void)e;
	/* .false */
	return false;
}
