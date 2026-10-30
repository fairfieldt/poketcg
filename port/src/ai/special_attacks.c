/* Port of src/engine/duel/ai/special_attacks.asm */
#include "ai/ai.h"

static u8 HandleSpecialAIAttacks_CallForFamily(u8 e);
static u8 HandleSpecialAIAttacks_NidoranFCallForFamily(void);
static u8 HandleSpecialAIAttacks_CallForFriend(void);
static u8 HandleSpecialAIAttacks_FriendshipSong(void);
static u8 HandleSpecialAIAttacks_Teleport(void);
static u8 HandleSpecialAIAttacks_SwordsDanceAndFocusEnergy(void);
static u8 HandleSpecialAIAttacks_ChainLightning(void);
static u8 HandleSpecialAIAttacks_DevolutionBeam(void);
static u8 HandleSpecialAIAttacks_Conversion(void);
static u8 HandleSpecialAIAttacks_EnergyAbsorption(void);
static u8 HandleSpecialAIAttacks_MixUp(void);
static u8 HandleSpecialAIAttacks_BigThunder(void);
static u8 HandleSpecialAIAttacks_Fetch(void);
static u8 HandleSpecialAIAttacks_Earthquake(void);
static u8 HandleSpecialAIAttacks_EnergySpike(void);
static u8 HandleSpecialAIAttacks_HyperBeam(void);

/* this function handles attacks with the SPECIAL_AI_HANDLING set,
 * and makes specific checks in each of these attacks
 * to either return a positive score (value above $80)
 * or a negative score (value below $80).
 * input:
 *	hTempPlayAreaLocation_ff9d = location of card with attack. */
u8 HandleSpecialAIAttacks(void)
{
	TRACE_FN();
	u8 a, e;

	e = GetCardIDFromDeckIndex(GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)));
	a = e;

	if (a == NIDORANF)
		return HandleSpecialAIAttacks_NidoranFCallForFamily();
	if (a == ODDISH)
		return HandleSpecialAIAttacks_CallForFamily(e);
	if (a == BELLSPROUT)
		return HandleSpecialAIAttacks_CallForFamily(e);
	if (a == EXEGGUTOR)
		return HandleSpecialAIAttacks_Teleport();
	if (a == SCYTHER)
		return HandleSpecialAIAttacks_SwordsDanceAndFocusEnergy();
	if (a == KRABBY)
		return HandleSpecialAIAttacks_CallForFamily(e);
	if (a == VAPOREON_LV29)
		return HandleSpecialAIAttacks_SwordsDanceAndFocusEnergy();
	if (a == ELECTRODE_LV42)
		return HandleSpecialAIAttacks_ChainLightning();
	if (a == MAROWAK_LV26)
		return HandleSpecialAIAttacks_CallForFriend();
	if (a == MEW_LV23)
		return HandleSpecialAIAttacks_DevolutionBeam();
	if (a == JIGGLYPUFF_LV13)
		return HandleSpecialAIAttacks_FriendshipSong();
	if (a == PORYGON)
		return HandleSpecialAIAttacks_Conversion();
	if (a == MEWTWO_ALT_LV60)
		return HandleSpecialAIAttacks_EnergyAbsorption();
	if (a == MEWTWO_LV60)
		return HandleSpecialAIAttacks_EnergyAbsorption();
	if (a == NINETALES_LV35)
		return HandleSpecialAIAttacks_MixUp();
	if (a == ZAPDOS_LV68)
		return HandleSpecialAIAttacks_BigThunder();
	if (a == KANGASKHAN)
		return HandleSpecialAIAttacks_Fetch();
	if (a == DUGTRIO)
		return HandleSpecialAIAttacks_Earthquake();
	if (a == ELECTRODE_LV35)
		return HandleSpecialAIAttacks_EnergySpike();
	if (a == GOLDUCK)
		return HandleSpecialAIAttacks_HyperBeam();
	if (a == DRAGONAIR)
		return HandleSpecialAIAttacks_HyperBeam();

	/* return zero score. */
	/* .zero_score */
	return 0;
}

/* if any of card ID in a is found in deck,
 * return a score of $80 + slots available in bench. */
static u8 HandleSpecialAIAttacks_CallForFamily(u8 e)
{
	u8 a, b, idx;

	if (!LookForCardIDInLocation_Bank5(CARD_LOCATION_DECK, e, &idx))
		return 0; /* .zero_score */
	a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	if (a >= MAX_BENCH_POKEMON)
		return 0; /* .zero_score */
	b = a;
	a = MAX_BENCH_POKEMON;
	a = (u8)(a - b);
	return (u8)(a + 0x80);
}

/* if any of NidoranM or NidoranF is found in deck,
 * return a score of $80 + slots available in bench. */
static u8 HandleSpecialAIAttacks_NidoranFCallForFamily(void)
{
	u8 a, b, idx;

	if (LookForCardIDInLocation_Bank5(CARD_LOCATION_DECK, NIDORANM, &idx))
		goto found_nidoran;
	if (!LookForCardIDInLocation_Bank5(CARD_LOCATION_DECK, NIDORANF, &idx))
		return 0; /* .zero_score */
found_nidoran:
	a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	if (a >= MAX_PLAY_AREA_POKEMON)
		return 0; /* .zero_score */
	b = a;
	a = MAX_PLAY_AREA_POKEMON;
	a = (u8)(a - b);
	return (u8)(a + 0x80);
}

/* checks for certain card IDs of Fighting color in deck.
 * if any of them are found, return a score of
 * $80 + slots available in bench. */
static u8 HandleSpecialAIAttacks_CallForFriend(void)
{
	u8 a, b, idx;

	if (LookForCardIDInLocation_Bank5(CARD_LOCATION_DECK, GEODUDE, &idx))
		goto found_fighting_card;
	if (LookForCardIDInLocation_Bank5(CARD_LOCATION_DECK, ONIX, &idx))
		goto found_fighting_card;
	if (LookForCardIDInLocation_Bank5(CARD_LOCATION_DECK, CUBONE, &idx))
		goto found_fighting_card;
	if (LookForCardIDInLocation_Bank5(CARD_LOCATION_DECK, RHYHORN, &idx))
		goto found_fighting_card;
	return 0; /* .zero_score */
found_fighting_card:
	a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	if (a >= MAX_BENCH_POKEMON)
		return 0; /* .zero_score */
	b = a;
	a = MAX_BENCH_POKEMON;
	a = (u8)(a - b);
	return (u8)(a + 0x80);
}

/* if any basic cards are found in deck,
 * return a score of $80 + slots available in bench. */
static u8 HandleSpecialAIAttacks_FriendshipSong(void)
{
	u8 a, b;

	if (!CheckIfAnyBasicPokemonInDeck())
		return 0; /* .zero_score */
	a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	if (a >= MAX_PLAY_AREA_POKEMON)
		return 0; /* .zero_score */
	b = a;
	a = MAX_PLAY_AREA_POKEMON;
	a = (u8)(a - b);
	return (u8)(a + 0x80);
}

/* if AI decides to retreat, return a score of $80 + 10. */
static u8 HandleSpecialAIAttacks_Teleport(void)
{
	if (!AIDecideWhetherToRetreat())
		return 0; /* .zero_score */
	return 0x8a;
}

/* tests for the following conditions:
 * - player is under No Damage substatus;
 * - second attack is unusable;
 * - second attack deals no damage;
 * if any are true, returns score of $80 + 5. */
static u8 HandleSpecialAIAttacks_SwordsDanceAndFocusEnergy(void)
{
	if (wAICannotDamage)
		goto swords_dance_focus_energy_success;
	wSelectedAttack = SECOND_ATTACK;
	if (CheckIfSelectedAttackIsUnusable())
		goto swords_dance_focus_energy_success;
	EstimateDamage_VersusDefendingCard(SECOND_ATTACK);
	if (wDamage[0])
		return 0; /* .zero_score */
swords_dance_focus_energy_success:
	return 0x85;
}

/* checks player's active card color, then
 * loops through own bench looking for a Pokémon
 * with that same color.
 * if none are found, returns score of $80 + 2. */
static u8 HandleSpecialAIAttacks_ChainLightning(void)
{
	u8 a, b;
	const u8 *hl;

	SwapTurn();
	a = GetArenaCardColor();
	SwapTurn();
	b = a;
	hl = TurnDuelistVars() + DUELVARS_BENCH;
	for (;;) { /* .loop_chain_lightning_bench */
		a = *hl++;
		if (a == 0xff)
			return 0x82; /* .chain_lightning_success */
		a = GetCardType(GetCardIDFromDeckIndex(a));
		if (a == b)
			return 0; /* .zero_score */
	}
}

static u8 HandleSpecialAIAttacks_DevolutionBeam(void)
{
	u8 loc;

	if (!LookForCardThatIsKnockedOutOnDevolution(&loc))
		return 0; /* .zero_score */
	return 0x85;
}

/* first checks if card is confused, and if so return 0.
 * then checks number of Pokémon in bench that are viable to use:
 * - if that number is < 2  and this attack is Conversion 1 OR
 * - if that number is >= 2 and this attack is Conversion 2
 * then return score of $80 + 2.
 * otherwise return score of $80 + 1. */
static u8 HandleSpecialAIAttacks_Conversion(void)
{
	if ((GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS) & CNF_SLP_PRZ) == CONFUSED)
		return 0; /* .zero_score */

	if (wSelectedAttack)
		goto conversion_2;

	/* conversion 1 */
	if (CountNumberOfSetUpBenchPokemon() < 2)
		goto low_conversion_score;
	return 0x82;

conversion_2:
	if (CountNumberOfSetUpBenchPokemon() >= 2)
		goto low_conversion_score;
	return 0x82;

low_conversion_score:
	return 0x81;
}

/* if any Psychic Energy is found in the Discard Pile,
 * return a score of $80 + 2. */
static u8 HandleSpecialAIAttacks_EnergyAbsorption(void)
{
	u8 idx;

	if (!LookForCardIDInLocation_Bank5(CARD_LOCATION_DISCARD_PILE, PSYCHIC_ENERGY, &idx))
		return 0; /* .zero_score */
	return 0x82;
}

/* if player has cards in hand, AI calls Random:
 * - 1/3 chance to encourage attack regardless;
 * - 1/3 chance to dismiss attack regardless;
 * - 1/3 change to make some checks to player's hand.
 * AI tallies number of basic cards in hand, and if this
 * number is >= 2, encourage attack.
 * otherwise, if it finds an evolution card in hand that
 * can evolve a card in player's deck, encourage.
 * if encouraged, returns a score of $80 + 3. */
static u8 HandleSpecialAIAttacks_MixUp(void)
{
	u8 a, b, n, evo;
	bool carry;
	const u8 *hl;

	a = GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_IN_HAND);
	if (!a)
		return a; /* ret z */

	a = Random(3);
	if (!a)
		goto encourage_mix_up;
	a--;
	if (!a)
		return a; /* ret z */
	SwapTurn();
	CreateHandCardList(&n);
	SwapTurn();
	if (!n)
		return n; /* return if no hand cards (again) */
	if (GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) >= 3)
		goto mix_up_check_play_area;

	hl = wDuelTempList;
	b = 0;
	for (;;) { /* .loop_mix_up_hand */
		a = *hl++;
		if (a == 0xff)
			break; /* .tally_basic_cards */
		SwapTurn();
		LoadCardDataToBuffer2_FromDeckIndex(a);
		SwapTurn();
		if (wLoadedCard2.Type >= TYPE_ENERGY)
			continue;
		if (wLoadedCard2.Stage)
			continue;
		/* is a basic Pokémon card */
		b++;
	}
	/* .tally_basic_cards */
	if (b >= 2)
		goto encourage_mix_up;

	/* less than 2 basic cards in hand */
mix_up_check_play_area:
	hl = NonTurnDuelistVars() + DUELVARS_ARENA_CARD;
	for (;;) { /* .loop_mix_up_play_area */
		a = *hl++;
		if (a == 0xff)
			return 0; /* .zero_score */
		SwapTurn();
		carry = CheckForEvolutionInList(a, &evo);
		SwapTurn();
		if (carry)
			break;
	}

encourage_mix_up:
	return 0x83;
}

/* return score of $80 + 3. */
static u8 HandleSpecialAIAttacks_BigThunder(void)
{
	return 0x83;
}

/* dismiss attack if cards in deck <= 20.
 * otherwise return a score of $80 + 0. */
static u8 HandleSpecialAIAttacks_Fetch(void)
{
	if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK) >= 41)
		return 0; /* .zero_score */
	return 0x80;
}

/* dismiss attack if number of own benched cards which would
 * be KOd is greater than or equal to the number
 * of prize cards left for player. */
static u8 HandleSpecialAIAttacks_Earthquake(void)
{
	u8 a, d, e;
	const u8 *hl;

	hl = TurnDuelistVars() + DUELVARS_BENCH;

	d = 0;
	e = PLAY_AREA_BENCH_1 - 1;
	for (;;) { /* .loop_earthquake */
		e++;
		a = *hl++;
		if (a == 0xff)
			break; /* .count_prizes */
		/* bug, GetTurnDuelistVariable clobbers hl
		 * uncomment the following lines to preserve hl
		 * push hl */
		a = GetTurnDuelistVariable((u8)(e + DUELVARS_ARENA_CARD_HP));
		hl = TurnDuelistVars() + (u8)(e + DUELVARS_ARENA_CARD_HP); /* hl as left by GetTurnDuelistVariable */
		/* pop hl */
		if (a >= 20)
			continue;
		d++;
	}

	/* .count_prizes */
	/* bug, this is supposed to count the player's prize cards
	 * not the opponent's, missing calls to SwapTurn */
	/* call SwapTurn */
	a = CountPrizes();
	/* call SwapTurn */
	if (a <= d) /* cp d / jp c, .zero_score / jp z, .zero_score */
		return 0;
	return 0x80;
}

/* if there's any lightning energy cards in deck,
 * return a score of $80 + 3. */
static u8 HandleSpecialAIAttacks_EnergySpike(void)
{
	u8 idx;

	if (!LookForCardIDInLocation_Bank5(CARD_LOCATION_DECK, LIGHTNING_ENERGY, &idx))
		return 0; /* .zero_score */
	if (!AIProcessButDontPlayEnergy_SkipEvolution())
		return 0; /* .zero_score */
	return 0x83;
}

/* only incentivize attack if player's active card,
 * has any energy cards attached, and if so,
 * return a score of $80 + 3. */
static u8 HandleSpecialAIAttacks_HyperBeam(void)
{
	u8 a;

	SwapTurn();
	a = CountNumberOfEnergyCardsAttached(PLAY_AREA_ARENA);
	SwapTurn();
	if (!a)
		return 0x80; /* .hyper_beam_neutral */
	return 0x83;
}

/* called when second attack is determined by AI to have
 * more AI score than the first attack, so that it checks
 * whether the first attack is a better alternative. */
void CheckWhetherToSwitchToFirstAttack(void)
{
	TRACE_FN();
	u8 a;

	/* this checks whether the first attack is also viable
	 * (has more than minimum score to be used) */
	if (wFirstAttackAIScore < 0x50)
		goto keep_second_attack;

	/* first attack has more than minimum score to be used,
	 * check if it can KO, in case it can't
	 * then the AI keeps second attack as selection. */
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	/* a = FIRST_ATTACK_OR_PKMN_POWER */
	EstimateDamage_VersusDefendingCard(FIRST_ATTACK_OR_PKMN_POWER);
	a = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);
	if (a > wDamage[0]) /* HP - damage: jr z, .check_flag / jr nc, .keep_second_attack */
		goto keep_second_attack; /* cannot KO */

	/* first attack can ko, check flags from second attack
	 * in case its effect is to heal user or nullify/weaken damage
	 * next turn, keep second attack as the option. */
	/* .check_flag */
	CopyAttackDataAndDamage_FromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD), SECOND_ATTACK);
	if (CheckLoadedAttackFlag(ATTACK_FLAG2_ADDRESS | HEAL_USER_F))
		goto keep_second_attack;
	if (CheckLoadedAttackFlag(ATTACK_FLAG2_ADDRESS | NULLIFY_OR_WEAKEN_ATTACK_F))
		goto keep_second_attack;
	/* switch to first attack */
	wSelectedAttack = FIRST_ATTACK_OR_PKMN_POWER;
	return;
keep_second_attack:
	wSelectedAttack = SECOND_ATTACK;
}

/* returns carry if there are
 * any basic Pokémon cards in deck. */
bool CheckIfAnyBasicPokemonInDeck(void)
{
	TRACE_FN();
	u8 e = 0;

	do { /* .loop */
		if (GetTurnDuelistVariable((u8)(DUELVARS_CARD_LOCATIONS + e)) == CARD_LOCATION_DECK) {
			LoadCardDataToBuffer2_FromDeckIndex(e);
			if (wLoadedCard2.Type < TYPE_ENERGY) {
				if (!wLoadedCard2.Stage)
					return true; /* .set_carry */
			}
		}
		/* .next */
		e++;
	} while (e != DECK_SIZE);
	return false;
}
