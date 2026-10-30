/* Port of src/engine/duel/ai/hand_pokemon.asm */
#include "ai/ai.h"

static bool AIDecideEvolution_next_bench_pokemon(u8 b);

/* determine whether AI plays
 * basic cards from hand */
bool AIDecidePlayPokemonCard(void)
{
	TRACE_FN();
	u8 a, dmg, evo;
	const u8 *hl;

	CreateHandCardList(NULL);
	SortTempHandByIDList();
	CopyListWithFFTerminatorFromHLToDE_Bank5(wDuelTempList, wHandTempList);
	hl = wHandTempList;

	for (;;) { /* .next_hand_card */
		a = *hl++;
		if (a == 0xff)
			return AIDecideEvolution();

		wTempAIPokemonCard = a;
		LoadCardDataToBuffer1_FromDeckIndex(a);
		if (wLoadedCard1.Type >= TYPE_ENERGY)
			continue; /* .skip */
		/* skip non-pokemon cards */

		if (wLoadedCard1.Stage)
			continue; /* .skip */
		/* skip non-basic pokemon */

		wAIScore = 130;
		AIDecidePlayLegendaryBirds();

		/* if Play Area has more than 4 Pokémon, decrease AI score
		 * else, increase AI score */
		if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) >= 4) {
			AIDiscourage(20);
		} else {
			/* .has_4_or_fewer */
			AIEncourage(50);
		}

		/* if defending Pokémon can KO active card, increase AI score */
		/* .check_defending_can_ko */
		hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
		if (CheckIfDefendingPokemonCanKnockOut(&dmg))
			AIEncourage(20);

		/* if energy cards are found in hand
		 * for this card's attacks, raise AI score */
		/* .check_energy_cards */
		a = GetAttacksEnergyCostBits(wTempAIPokemonCard);
		if (CheckEnergyFlagsNeededInList(a))
			AIEncourage(20);

		/* if evolution card is found in hand
		 * for this card, raise AI score */
		/* .check_evolution_hand */
		if (CheckForEvolutionInList(wTempAIPokemonCard, &evo))
			AIEncourage(20);

		/* if evolution card is found in deck
		 * for this card, raise AI score */
		/* .check_evolution_deck */
		if (CheckForEvolutionInDeck(wTempAIPokemonCard, &evo))
			AIEncourage(10);

		/* if AI score is >= 180, play card from hand */
		/* .check_score */
		if (wAIScore < 180)
			continue; /* .skip */
		hTemp_ffa0 = wTempAIPokemonCard;
		if (CheckIfCardCanBePlayed(wTempAIPokemonCard))
			continue; /* .skip */
		if (AIMakeDecision(OPPACTION_PLAY_BASIC_PKMN))
			return true; /* .done */
		/* .skip */
	}
}

/* determine whether AI evolves
 * Pokémon in the Play Area */
bool AIDecideEvolution(void)
{
	TRACE_FN();
	u8 a, b, c;
	const u8 *hl;

	CreateHandCardList(NULL);
	CopyListWithFFTerminatorFromHLToDE_Bank5(wDuelTempList, wHandTempList);
	hl = wHandTempList;

	for (;;) { /* .next_hand_card */
		a = *hl++;
		if (a == 0xff)
			return false; /* .done */
		wTempAIPokemonCard = a;

		/* check if Prehistoric Power is active
		 * and if so, skip to next card in hand */
		if (IsPrehistoricPowerActive())
			continue; /* .done_hand_card */

		/* load evolution data to buffer1
		 * skip if it's not a Pokémon card
		 * and if it's a basic stage card */
		LoadCardDataToBuffer1_FromDeckIndex(wTempAIPokemonCard);
		if (wLoadedCard1.Type >= TYPE_ENERGY)
			continue; /* .done_hand_card */
		if (!wLoadedCard1.Stage)
			continue; /* .done_hand_card */

		/* start looping Pokémon in Play Area
		 * to find a card to evolve */
		c = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
		b = 0;
		do { /* .next_bench_pokemon */
			if (AIDecideEvolution_next_bench_pokemon(b))
				break; /* .done_hand_card */
			/* .done_bench_pokemon */
			b++;
		} while (--c);
		/* .done_hand_card */
	}
}

/* .next_bench_pokemon body for Play Area location b.
 * returns true if the AI evolved the card (pop bc / jr .done_hand_card),
 * false to go on with the next Play Area Pokémon (.done_bench_pokemon) */
static bool AIDecideEvolution_next_bench_pokemon(u8 b)
{
	u8 a, saved, dmg, evo;
	bool unusable;

	if (CheckIfCanEvolveInto(wTempAIPokemonCard, b, NULL))
		return false; /* jp c, .done_bench_pokemon */

	/* store this Play Area location in wTempAI
	 * and initialize the AI score */
	wTempAI = b;
	hTempPlayAreaLocation_ff9d = b;
	wAIScore = 0x80;
	AIDecideSpecialEvolutions();

	/* check if the card can use any attacks
	 * and if any of those attacks can KO */
	wSelectedAttack = FIRST_ATTACK_OR_PKMN_POWER;
	unusable = CheckIfSelectedAttackIsUnusable();
	if (unusable) {
		wSelectedAttack = SECOND_ATTACK;
		unusable = CheckIfSelectedAttackIsUnusable();
	}
	if (!unusable) {
		/* .can_attack */
		wCurCardCanAttack = 0x01;
		if (CheckIfAnyAttackKnocksOutDefendingCard() && !CheckIfSelectedAttackIsUnusable())
			wCurCardCanKO = 0x01;
	} else {
		/* .cant_attack_or_ko */
		wCurCardCanAttack = 0;
		wCurCardCanKO = 0;
	}

	/* check evolution to see if it can use any of its attacks:
	 * if it can, raise AI score;
	 * if it can't, decrease AI score and if an energy card that is needed
	 * can be played from the hand, raise AI score. */
	/* .check_evolution_attacks */
	saved = GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)); /* push af */
	TurnDuelistVars()[(u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)] = wTempAIPokemonCard;
	wSelectedAttack = FIRST_ATTACK_OR_PKMN_POWER;
	unusable = CheckIfSelectedAttackIsUnusable();
	if (unusable) {
		wSelectedAttack = SECOND_ATTACK;
		unusable = CheckIfSelectedAttackIsUnusable();
	}
	if (!unusable) {
		/* .evolution_can_attack */
		AIEncourage(5);
	} else {
		/* .evolution_cant_attack */
		if (wCurCardCanAttack) {
			AIDiscourage(2);
			if (!wAlreadyPlayedEnergy) {
				if (LookForEnergyNeededInHand())
					AIEncourage(7);
			}
		}
	}

	/* if it's an active card:
	 * if evolution can't KO but the current card can, lower AI score;
	 * if evolution can KO as well, raise AI score. */
	/* .check_evolution_ko */
	if (wCurCardCanAttack && !wTempAI) {
		if (CheckIfAnyAttackKnocksOutDefendingCard() && !CheckIfSelectedAttackIsUnusable()) {
			AIEncourage(5);
		} else {
			/* .evolution_cant_ko */
			if (wCurCardCanKO)
				AIDiscourage(20);
		}
	}

	/* if defending Pokémon can KO evolution, lower AI score */
	/* .check_defending_can_ko_evolution */
	if (!wTempAI) {
		hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
		if (CheckIfDefendingPokemonCanKnockOut(&dmg))
			AIDiscourage(5);
	}

	/* if evolution can't damage player's Mr Mime, lower AI score */
	/* .check_mr_mime */
	if (!CheckDamageToMrMime(wTempAI))
		AIDiscourage(20);

	/* if defending Pokémon can KO current card, raise AI score */
	/* .check_defending_can_ko */
	TurnDuelistVars()[(u8)(wTempAI + DUELVARS_ARENA_CARD)] = saved; /* pop af / ld [hl], a */
	if (!wTempAI) {
		hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
		if (CheckIfDefendingPokemonCanKnockOut(&dmg))
			AIEncourage(5);

		/* if current card has a status condition, raise AI score */
		/* .check_status */
		if (GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS))
			AIEncourage(4);
	}

	/* if hand has 2nd stage card to evolve evolution card, raise AI score */
	/* .check_2nd_stage_hand */
	if (CheckForEvolutionInList(wTempAIPokemonCard, &evo)) {
		AIEncourage(2);
	} else {
		/* if deck has 2nd stage card to evolve evolution card, raise AI score */
		/* .check_2nd_stage_deck */
		if (CheckForEvolutionInDeck(wTempAIPokemonCard, &evo))
			AIEncourage(1);
	}

	/* decrease AI score proportional to damage
	 * AI score -= floor(Damage / 40) */
	/* .check_damage */
	a = GetCardDamageAndMaxHP(wTempAI, NULL);
	if (a) {
		a >>= 1;
		a >>= 1;
		AIDiscourage(ConvertHPToDamageCounters_Bank5(a));
	}

	/* if is Mysterious Fossil or
	 * wLoadedCard1AIInfo is AI_INFO_ENCOURAGE_EVO,
	 * raise AI score */
	/* .check_mysterious_fossil */
	LoadCardDataToBuffer1_FromDeckIndex(GetTurnDuelistVariable((u8)(wTempAI + DUELVARS_ARENA_CARD)));
	if (wLoadedCard1.ID == MYSTERIOUS_FOSSIL) {
		/* .mysterious_fossil */
		AIEncourage(5);
	} else {
		/* bug, should mask out HAS_EVOLUTION flag first */
		if (wLoadedCard1.AIInfo == AI_INFO_ENCOURAGE_EVO)
			AIEncourage(2);
	}

	/* in Pikachu Deck, decrease AI score for evolving Pikachu */
	/* .pikachu_deck */
	if (wOpponentDeckID == PIKACHU_DECK_ID) {
		a = wLoadedCard1.ID;
		if (a == PIKACHU_LV12 || a == PIKACHU_LV14 || a == PIKACHU_LV16 || a == PIKACHU_ALT_LV16) {
			/* .pikachu */
			AIDiscourage(3);
		}
	}

	/* if AI score >= 133, go through with the evolution */
	/* .check_score */
	if (wAIScore < 133)
		return false; /* .done_bench_pokemon */
	hTempPlayAreaLocation_ffa1 = wTempAI;
	hTemp_ffa0 = wTempAIPokemonCard;
	AIMakeDecision(OPPACTION_EVOLVE_PKMN);
	return true; /* pop bc / jr .done_hand_card */
}

/* determine AI score for evolving
 * Charmeleon, Magikarp, Dragonair and Grimer
 * in certain decks */
void AIDecideSpecialEvolutions(void)
{
	TRACE_FN();
	u8 a, b, c;

	/* check if deck applies */
	a = wOpponentDeckID;
	if (a == LEGENDARY_DRAGONITE_DECK_ID)
		goto legendary_dragonite;
	if (a == INVINCIBLE_RONALD_DECK_ID)
		goto invincible_ronald;
	if (a == LEGENDARY_RONALD_DECK_ID)
		goto legendary_ronald;
	return;

legendary_dragonite:
	a = wLoadedCard2.ID;
	if (a == CHARMELEON)
		goto charmeleon;
	if (a == MAGIKARP)
		goto magikarp;
	if (a == DRAGONAIR)
		goto dragonair;
	return;

	/* check if number of energy cards attached to Charmeleon are at least 3
	 * and if adding the energy cards in hand makes at least 6 energy cards */
charmeleon:
	a = CountNumberOfEnergyCardsAttached(hTempPlayAreaLocation_ff9d);
	if (a < 3)
		goto not_enough_energy;
	b = a; /* push af / pop bc */
	/* CountOppEnergyCardsInHand returns $ff if there are none (b - 1 after the add) */
	a = (u8)(CountOppEnergyCardsInHand() + b);
	if (a < 6)
		goto not_enough_energy;
	AIEncourage(3);
	return;
not_enough_energy:
	AIDiscourage(10);
	return;

	/* check if Magikarp is not the active card
	 * and has at least 2 energy cards attached */
magikarp:
	a = hTempPlayAreaLocation_ff9d;
	if (!a)
		return; /* active card */
	if (CountNumberOfEnergyCardsAttached(a) < 2)
		return;
	AIEncourage(3);
	return;

invincible_ronald:
	if (wLoadedCard2.ID == GRIMER)
		goto grimer;
	return;

	/* check if Grimer is not active card */
grimer:
	if (!hTempPlayAreaLocation_ff9d)
		return; /* active card */
	AIEncourage(10);
	return;

legendary_ronald:
	if (wLoadedCard2.ID == DRAGONAIR)
		goto dragonair;
	return;

dragonair:
	if (!hTempPlayAreaLocation_ff9d)
		goto is_active; /* active card */

	/* if Dragonair is benched, check all Pokémon in Play Area
	 * and sum all the damage in HP of all cards
	 * if this result is >= 70, check if there's
	 * a Muk in any duelist's Play Area */
	b = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	c = 0;
	do { /* .loop */
		b--;
		a = GetCardDamageAndMaxHP(b, NULL);
		c = (u8)(a + c);
	} while (b);
	/* ld a, 70 / cp c / jr c: the sum must actually be > 70 */
	if (70 < c)
		goto check_muk;
lower_score:
	AIDiscourage(10);
	return;

	/* if there's no Muk, raise score */
check_muk:
	if (CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK))
		goto lower_score;
	AIEncourage(10);
	return;

	/* if Dragonair is active, check its damage in HP
	 * if this result is >= 50,
	 * and if at least 3 energy cards attached,
	 * check if there's a Muk in any duelist's Play Area */
is_active:
	if (GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL) < 50)
		goto lower_score;
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	if (wTotalAttachedEnergies < 3)
		goto lower_score;
	goto check_muk;
}

/* determine AI score for the legendary cards
 * Moltres, Zapdos and Articuno */
void AIDecidePlayLegendaryBirds(void)
{
	TRACE_FN();
	u8 a, d;

	/* check if deck applies */
	a = wOpponentDeckID;
	if (a != LEGENDARY_ZAPDOS_DECK_ID && a != LEGENDARY_ARTICUNO_DECK_ID && a != LEGENDARY_RONALD_DECK_ID)
		return;

	/* check if card applies */
	/* .begin */
	a = wLoadedCard1.ID;
	if (a == ARTICUNO_LV37)
		goto articuno;
	if (a == MOLTRES_LV37)
		goto moltres;
	if (a == ZAPDOS_LV68)
		goto zapdos;
	return;

articuno:
	/* exit if not enough Pokemon in Play Area */
	if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) < 2)
		return;

	if (CheckIfActiveCardCanKnockOut())
		goto subtract;
	if (!CanArenaCardUseNonResidualAttack())
		goto subtract;
	if (AIDecideWhetherToRetreat())
		goto subtract;

	/* checks for player's active card status */
	if (GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS) & CNF_SLP_PRZ)
		goto subtract;

	/* checks for player's Pokemon Power */
	SwapTurn();
	d = GetTurnDuelistVariable(DUELVARS_ARENA_CARD);
	CopyAttackDataAndDamage_FromDeckIndex(d, FIRST_ATTACK_OR_PKMN_POWER);
	SwapTurn();
	if (wLoadedAttackCategory == POKEMON_POWER)
		goto check_muk_and_snorlax;

	/* return if no space on the bench */
	if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) < MAX_BENCH_POKEMON)
		goto check_muk_and_snorlax;
	return;

check_muk_and_snorlax:
	/* checks for Muk in both Play Areas */
	if (CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK))
		goto subtract;
	/* checks if player's active card is Snorlax */
	a = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD);
	SwapTurn();
	a = GetCardIDFromDeckIndex(a);
	SwapTurn();
	if (a == SNORLAX)
		goto subtract;

	/* add */
	AIEncourage(70);
	return;
subtract:
	AIDiscourage(100);
	return;

moltres:
	/* checks if there's enough cards in deck */
	if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK) >= 56) /* max number of cards not in deck to activate */
		goto subtract;
	return;

zapdos:
	/* checks for Muk in both Play Areas */
	if (CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK))
		goto subtract;
	return;
}
