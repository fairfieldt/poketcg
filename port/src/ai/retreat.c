/* Port of src/engine/duel/ai/retreat.asm */
#include "ai/ai.h"

static void AIDecideBenchPokemonToSwitchTo_loop_play_area(u8 c);
static void AIDecideBenchPokemonToSwitchTo_HandleAttackDamageScore(void);

/* determine AI score for retreating
 * return carry if AI decides to retreat */
bool AIDecideWhetherToRetreat(void)
{
	TRACE_FN();
	u8 a, b, c, e, dmg;
	const u8 *hl;

	if (wConfusionRetreatCheckWasUnsuccessful)
		return false; /* jp nz, .no_carry */
	wAIPlayEnergyCardForRetreat = 0;
	LoadDefendingPokemonColorWRAndPrizeCards();
	wAIScore = 0x80; /* initial retreat score */
	a = wAIRetreatScore;
	if (a) {
		/* add wAIRetreatScore * 8 to score
		 * (srl a / srl a / sla a: what is actually added is (wAIRetreatScore / 4) * 2) */
		a >>= 1;
		a >>= 1;
		a = (u8)(a << 1); /* *8 */
		AIEncourage(a);
	}

	/* .check_status */
	a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS);
	if (a) {
		if (a & DOUBLE_POISONED)
			AIEncourage(2);
		/* .check_cnf (ld a, [hl]: hl still points to the status) */
		if ((TurnDuelistVars()[DUELVARS_ARENA_CARD_STATUS] & CNF_SLP_PRZ) == CONFUSED)
			AIEncourage(1);
	}

	/* .skip_status_check */
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	if (CheckIfAnyAttackKnocksOutDefendingCard()) {
		if (!CheckIfSelectedAttackIsUnusable() /* jp nc, .active_cant_use_atk */
		    || LookForEnergyNeededForAttackInHand()) {
			/* .active_cant_use_atk */
			AIDiscourage(5);
			if (wAIOpponentPrizeCount < 2)
				AIDiscourage(35);
		}
	}

	/* .active_cant_ko_1 */
	if (CheckIfDefendingPokemonCanKnockOut(&dmg)) {
		AIEncourage(2);
		if (CheckIfNotABossDeckID())
			goto check_resistance_1;
		if (wAIPlayerPrizeCount >= 2)
			goto check_prize_count;
		wAIPlayEnergyCardForRetreat = TRUE;
	}

	/* .defending_cant_ko */
	if (CheckIfNotABossDeckID())
		goto check_resistance_1;
	if (wAIPlayerPrizeCount >= 2)
		goto check_prize_count;
	AIEncourage(2);

check_prize_count:
	if (wAIOpponentPrizeCount < 2)
		AIDiscourage(2);

check_resistance_1:
	b = TranslateColorToWR(GetArenaCardColor());
	if (wAIPlayerResistance & b) {
		AIEncourage(1);

		/* check bench for Pokémon that
		 * the defending card is not resistant to
		 * if one is found, skip AIDiscourage */
		b = wAIPlayerResistance;
		hl = TurnDuelistVars() + DUELVARS_BENCH;
		for (;;) { /* .loop_resistance_1 */
			a = *hl++;
			if (a == 0xff) {
				/* .exit_loop_resistance_1 */
				AIDiscourage(2);
				break;
			}
			LoadCardDataToBuffer1_FromDeckIndex(a);
			if (!(TranslateColorToWR(wLoadedCard1.Type) & b))
				break; /* jr .check_weakness_1 */
		}
	}

	/* .check_weakness_1 */
	b = wAIPlayerColor;
	if (GetArenaCardWeakness() & b) {
		AIEncourage(2);

		/* check bench for Pokémon that
		 * is not weak to defending Pokémon
		 * if one is found, skip AIDiscourage */
		b = wAIPlayerColor;
		hl = TurnDuelistVars() + DUELVARS_BENCH;
		for (;;) { /* .loop_weakness_1 */
			a = *hl++;
			if (a == 0xff) {
				/* .exit_loop_weakness_1 */
				AIDiscourage(3);
				break;
			}
			LoadCardDataToBuffer1_FromDeckIndex(a);
			if (!(wLoadedCard1.Weakness & b))
				break; /* jr .check_resistance_2 */
		}
	}

	/* .check_resistance_2 */
	b = wAIPlayerColor;
	if (GetArenaCardResistance() & b)
		AIDiscourage(3);

	/* check bench for Pokémon that
	 * is the defending Pokémon's weakness
	 * if none is found, skip AIEncourage */
	/* .check_weakness_2 */
	b = wAIPlayerWeakness;
	hl = TurnDuelistVars() + DUELVARS_BENCH;
	e = PLAY_AREA_BENCH_1 - 1;
	for (;;) { /* .loop_weakness_2 */
		e++;
		a = *hl++;
		if (a == 0xff)
			goto check_resistance_3;
		LoadCardDataToBuffer1_FromDeckIndex(a);
		if (TranslateColorToWR(wLoadedCard1.Type) & b)
			break;
	}
	AIEncourage(2);

	if (GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD)) == PORYGON) {
		/* handle Porygon */
		if (CheckIfCanDamageDefendingPokemon(e)) {
			AIEncourage(10);
			goto check_resistance_3;
		}
	}

	/* .check_weakness_3 */
	b = TranslateColorToWR(GetArenaCardColor());
	if (wAIPlayerWeakness & b)
		AIDiscourage(3);

	/* check bench for Pokémon that
	 * is resistant to defending Pokémon
	 * if none is found, skip AIEncourage */
check_resistance_3:
	b = wAIPlayerColor;
	hl = TurnDuelistVars() + DUELVARS_BENCH;
	for (;;) { /* .loop_resistance_2 */
		a = *hl++;
		if (a == 0xff)
			break; /* .check_ko_2 */
		LoadCardDataToBuffer1_FromDeckIndex(a);
		if (wLoadedCard1.Resistance & b) {
			AIEncourage(1);
			break;
		}
	}

	/* check bench for Pokémon that
	 * can KO defending Pokémon
	 * if none is found, skip AIEncourage */
	/* .check_ko_2 */
	hl = TurnDuelistVars() + DUELVARS_BENCH;
	c = 0;
	for (;;) { /* .loop_ko_1 */
		c++;
		a = *hl++;
		if (a == 0xff)
			goto check_defending_id;
		hTempPlayAreaLocation_ff9d = c;
		if (CheckIfAnyAttackKnocksOutDefendingCard()) {
			if (!CheckIfSelectedAttackIsUnusable() || LookForEnergyNeededForAttackInHand())
				break; /* .success */
		}
		/* .no_ko */
	}
	/* .success */
	AIEncourage(2);

	/* a bench Pokémon was found that can KO
	 * if this is a boss deck and it's at last prize card
	 * if arena Pokémon cannot KO, add to AI score
	 * and set wAIPlayEnergyCardForRetreat to $01 */
	if (wAIOpponentPrizeCount >= 2)
		goto check_defending_id;
	if (CheckIfNotABossDeckID())
		goto check_defending_id;

	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	if (CheckIfAnyAttackKnocksOutDefendingCard()) {
		if (!CheckIfSelectedAttackIsUnusable())
			goto check_defending_id;
	}
	/* .active_cant_ko_2 */
	AIEncourage(40);
	wAIPlayEnergyCardForRetreat = TRUE;

check_defending_id:
	a = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD);
	SwapTurn();
	e = GetCardIDFromDeckIndex(a);
	SwapTurn();
	if (e == MR_MIME || e == HITMONLEE) {
		/* check bench if there's any Pokémon
		 * that can damage defending Pokémon
		 * this is done because of Mr. Mime's PKMN PWR
		 * and Hitmonlee's ability to attack Bench */
		/* .mr_mime_or_hitmonlee */
		if (!CheckIfCanDamageDefendingPokemon(PLAY_AREA_ARENA)) {
			hl = TurnDuelistVars() + DUELVARS_BENCH;
			c = 0;
			for (;;) { /* .loop_damage */
				c++;
				a = *hl++;
				if (a == 0xff)
					break; /* .check_retreat_cost */
				if (CheckIfCanDamageDefendingPokemon(c)) {
					/* .can_damage */
					AIEncourage(5);
					wAIPlayEnergyCardForRetreat = TRUE;
					break;
				}
			}
		}
	}

	/* subtract from wAIScore if retreat cost is larger than 1
	 * then check if any cards have at least half HP,
	 * are final evolutions and can use second attack in the bench
	 * and adds to wAIScore if the active Pokémon doesn't meet
	 * these conditions */
	/* .check_retreat_cost */
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	a = GetPlayAreaCardRetreatCost();
	if (a >= 2) {
		if (a < 3) {
			/* exactly two */
			AIDiscourage(1);
		} else {
			/* .three_or_more */
			AIDiscourage(2);
		}
	}

	/* .one_or_none */
	if (!CheckIfArenaCardIsFullyPowered()) {
		a = CountNumberOfSetUpBenchPokemon();
		if (a >= 2)
			AIEncourage(a);
	}

	/* check bench for Pokémon that
	 * the defending Pokémon can't knock out
	 * if none is found, skip AIDiscourage */
	/* .check_defending_can_ko */
	hl = TurnDuelistVars() + DUELVARS_BENCH;
	e = 0;
	for (;;) { /* .loop_ko_2 */
		e++;
		a = *hl++;
		if (a == 0xff) {
			/* .exit_loop_ko */
			AIDiscourage(20);
			break;
		}
		LoadCardDataToBuffer2_FromDeckIndex(a);
		a = wLoadedCard2.ID;
		if (a == MYSTERIOUS_FOSSIL)
			continue;
		if (a == CLEFAIRY_DOLL)
			continue;
		hTempPlayAreaLocation_ff9d = e;
		if (!CheckIfDefendingPokemonCanKnockOut(&dmg))
			break; /* jr .check_active_id */
	}

	/* .check_active_id */
	e = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	if (e == MYSTERIOUS_FOSSIL || e == CLEFAIRY_DOLL)
		goto mysterious_fossil_or_clefairy_doll;

	/* if wAIScore is at least 131, set carry */
	if (wAIScore >= 131)
		return true; /* .set_carry */
	/* .no_carry */
	return false;

	/* set carry regardless if active card is
	 * either Mysterious Fossil or Clefairy Doll
	 * and there's a bench Pokémon who is not KO'd
	 * by defending Pokémon and can damage it */
mysterious_fossil_or_clefairy_doll:
	e = 0;
	for (;;) { /* .loop_ko_3 */
		e++;
		if (GetTurnDuelistVariable((u8)(e + DUELVARS_ARENA_CARD)) == 0xff)
			return false; /* .no_carry */
		hTempPlayAreaLocation_ff9d = e;
		if (CheckIfDefendingPokemonCanKnockOut(&dmg))
			continue;
		if (!CheckIfCanDamageDefendingPokemon(e))
			continue;
		return true; /* .set_carry */
	}
}

/* if player's turn and loaded attack is not a Pokémon Power OR
 * if opponent's turn and wAITriedAttack == 0
 * set wAIRetreatFlags's bit 7 flag */
void SetAIRetreatFlags(void)
{
	TRACE_FN();
	wAIRetreatFlags = 0;
	if (wWhoseTurn != OPPONENT_TURN) {
		/* player */
		if (wLoadedAttackCategory == POKEMON_POWER)
			return;
	} else {
		/* .opponent */
		if (wAITriedAttack)
			return;
	}
	/* .set_flag */
	wAIRetreatFlags = 0x80; /* %10000000 */
}

/* calculates AI score for bench Pokémon
 * returns in a and [hTempPlayAreaLocation_ff9d] the
 * Play Area location of best card to switch to.
 * returns carry if no Bench Pokemon. */
bool AIDecideBenchPokemonToSwitchTo(u8 *a)
{
	TRACE_FN();
	u8 b, c, n;

	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	n = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	if (n < 2) {
		*a = n;
		return true;
	}

	/* has at least 2 Pokémon in Play Area */
	SetAIRetreatFlags();
	LoadDefendingPokemonColorWRAndPrizeCards();
	wAIScore = 50;
	b = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	c = PLAY_AREA_ARENA;
	/* jp .store_score: the Arena card keeps the score of 50 */
	for (;;) {
		/* .store_score */
		wPlayAreaAIScore[hTempPlayAreaLocation_ff9d] = wAIScore;
		c++;
		if (--b == 0)
			break;
		/* .loop_play_area */
		AIDecideBenchPokemonToSwitchTo_loop_play_area(c);
	}

	/* done */
	wAIRetreatScore = 0;
	*a = FindHighestBenchScore();
	return false;
}

/* .loop_play_area body for Play Area location c, up to the point
 * where the original jumps to (or reaches) .store_score */
static void AIDecideBenchPokemonToSwitchTo_loop_play_area(u8 c)
{
	u8 a, b, e, dmg;
	const u8 *hl;

	hTempPlayAreaLocation_ff9d = c;
	wAIScore = 50;

	/* check if card can KO defending Pokémon
	 * if it can, raise AI score
	 * if on last prize card, raise AI score again */
	if (CheckIfAnyAttackKnocksOutDefendingCard() && !CheckIfSelectedAttackIsUnusable()) {
		AIEncourage(10);
		wAIRetreatFlags |= 0x01; /* %00000001 */
		if (CountPrizes() >= 2)
			goto check_defending_weak;
		AIEncourage(10);
	}

	/* calculates damage of both attacks
	 * to raise AI score accordingly */
	/* .check_can_use_atks */
	wSelectedAttack = FIRST_ATTACK_OR_PKMN_POWER;
	if (!CheckIfSelectedAttackIsUnusable())
		AIDecideBenchPokemonToSwitchTo_HandleAttackDamageScore();
	wSelectedAttack = SECOND_ATTACK;
	if (!CheckIfSelectedAttackIsUnusable())
		AIDecideBenchPokemonToSwitchTo_HandleAttackDamageScore();

	/* if an energy card that is needed is found in hand
	 * calculate damage of the move and raise AI score
	 * AI score += floor(Damage / 20) */
	/* .check_energy_card */
	if (LookForEnergyNeededInHand()) {
		EstimateDamage_VersusDefendingCard(wSelectedAttack);
		a = ConvertHPToDamageCounters_Bank5(wDamage[0]);
		a >>= 1;
		AIEncourage(a);
	}

	/* if no energies attached to card, lower AI score */
	/* .check_attached_energy */
	GetPlayAreaCardAttachedEnergies(hTempPlayAreaLocation_ff9d);
	if (!wTotalAttachedEnergies)
		AIDiscourage(1);

	/* if can damage Mr Mime, raise AI score */
	/* .check_mr_mime */
	a = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD);
	SwapTurn();
	a = LoadCardDataToBuffer2_FromDeckIndex(a);
	SwapTurn();
	if (a == MR_MIME) {
		EstimateDamage_VersusDefendingCard(FIRST_ATTACK_OR_PKMN_POWER);
		if (!wDamage[0]) {
			EstimateDamage_VersusDefendingCard(SECOND_ATTACK);
			if (!wDamage[0])
				goto check_defending_weak;
		}
		/* .can_damage */
		AIEncourage(5);
	}

	/* if defending card is weak to this card, raise AI score */
check_defending_weak:
	LoadCardDataToBuffer1_FromDeckIndex(GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)));
	c = TranslateColorToWR(wLoadedCard1.Type);
	if (c & wAIPlayerWeakness)
		AIEncourage(3);

	/* if defending card is resistant to this card, lower AI score */
	/* .check_defending_resist */
	if (c & wAIPlayerResistance)
		AIDiscourage(2);

	/* if this card is resistant to defending Pokémon, raise AI score */
	/* .check_resistance */
	if (wAIPlayerColor & wLoadedCard1.Resistance)
		AIEncourage(2);

	/* if this card is weak to defending Pokémon, lower AI score */
	/* .check_weakness */
	if (wAIPlayerColor & wLoadedCard1.Weakness)
		AIDiscourage(3);

	/* if this card's retreat cost < 2, raise AI score
	 * if this card's retreat cost > 2, lower AI score */
	/* .check_retreat_cost */
	a = GetPlayAreaCardRetreatCost();
	if (a < 2) {
		/* .one_or_none */
		AIEncourage(1);
	} else if (a != 2) {
		AIDiscourage(1);
	}

	/* if wAIRetreatFlags != $81
	 * if defending Pokémon can KO this card
	 * if player is not at last prize card, lower 3 from AI score
	 * if player is at last prize card, lower 10 from AI score */
	/* .check_player_prize_count */
	if (wAIRetreatFlags != (0x80 | 0x01)) {
		if (CheckIfDefendingPokemonCanKnockOut(&dmg)) {
			e = 3;
			if (wAIPlayerPrizeCount == 1)
				e = 10;
			/* .lower_score_1 */
			AIDiscourage(e);
		}
	}

	/* if this card's HP is 0, make AI score 0 */
	/* .check_hp */
	a = GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD_HP));
	if (!a) {
		wAIScore = a;
		return; /* jr .store_score */
	}

	/* AI score += floor(HP/40) */
	/* .add_hp_score */
	b = a;
	a = CalculateBDividedByA_Bank5(4, b);
	a = ConvertHPToDamageCounters_Bank5(a);
	AIEncourage(a);

	/* raise AI score if
	 *	- is a Mr Mime OR
	 *	- is a MewLv8 and defending card is not basic stage */
	a = LoadCardDataToBuffer1_FromDeckIndex(GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)));
	if (a != MR_MIME) {
		if (a != MEW_LV8)
			goto check_if_has_bench_utility;
		/* bug (original): no SwapTurn around LoadCardDataToBuffer2_FromDeckIndex, so the
		 * defending card's deck index is looked up in the turn duelist's own deck */
		LoadCardDataToBuffer2_FromDeckIndex(GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD));
		if (!wLoadedCard2.Stage)
			goto check_if_has_bench_utility;
	}
	/* .raise_score */
	AIEncourage(5);

	/* if wLoadedCard1AIInfo == AI_INFO_BENCH_UTILITY,
	 * lower AI score */
check_if_has_bench_utility:
	/* bug, should mask out HAS_EVOLUTION flag first */
	if (wLoadedCard1.AIInfo == AI_INFO_BENCH_UTILITY)
		AIDiscourage(2);

	/* if card is Mysterious Fossil or Clefairy Doll,
	 * lower AI score */
	/* .mysterious_fossil_or_clefairy_doll */
	a = wLoadedCard1.ID;
	if (a == MYSTERIOUS_FOSSIL || a == CLEFAIRY_DOLL) {
		/* .lower_score_2 */
		/* bug (original): .ai_score_bonus then compares the list IDs against the
		 * register a that AIDiscourage leaves (score-derived), not the card ID */
		a = AIDiscourage(10);
	}

	/* .ai_score_bonus */
	b = a;
	if (wAICardListRetreatBonus[1] == 0)
		return; /* jr z, .store_score */
	hl = AIListPtr(rd16(wAICardListRetreatBonus));

	for (;;) { /* .loop_ids */
		a = *hl++;
		if (!a)
			return; /* list is over (.store_score) */
		if (a == b) {
			a = *hl;
			if (a >= 0x80) {
				AIEncourage((u8)(a - 0x80));
			} else {
				/* .subtract_score */
				c = a;
				AIDiscourage((u8)(0x80 - c));
			}
		}
		/* .next_id */
		hl++;
	}
}

/* adds to AI score depending on amount of damage
 * it can inflict to the defending Pokémon
 * AI score += floor(Damage / 10) + 1 */
static void AIDecideBenchPokemonToSwitchTo_HandleAttackDamageScore(void)
{
	EstimateDamage_VersusDefendingCard(wSelectedAttack);
	AIEncourage((u8)(ConvertHPToDamageCounters_Bank5(wDamage[0]) + 1));
}

/* handles AI action of retreating Arena Pokémon
 * and chooses which energy cards to discard.
 * if card can't discard, return carry.
 * in case it's Clefairy Doll or Mysterious Fossil,
 * handle its effect to discard itself instead of retreating.
 * input:
 *	- a = Play Area location (PLAY_AREA_*) of card to retreat to. */
bool AITryToRetreat(u8 loc)
{
	TRACE_FN();
	u8 a, b, c, e;
	u8 *hl, *de;

	/* push af (loc) */
	if (wAIPlayEnergyCardForRetreat) {
		/* AI is allowed to play an energy card
		 * from the hand in order to provide
		 * the necessary energy for retreat cost */

		/* check status */
		a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS) & CNF_SLP_PRZ;
		if (a == ASLEEP)
			goto check_id;
		if (a == PARALYZED)
			goto check_id;

		/* if an energy card hasn't been played yet,
		 * checks if the Pokémon needs just one more energy to retreat
		 * if it does, check if there are any energy cards in hand
		 * and if there are, play that energy card */
		if (wAlreadyPlayedEnergy)
			goto check_id;
		b = CountNumberOfEnergyCardsAttached(PLAY_AREA_ARENA);
		hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
		a = GetPlayAreaCardRetreatCost();
		if (a <= b)
			goto check_id;
		/* energy attached < retreat cost */
		a = (u8)(a - b);
		if (a != 1)
			goto check_id;
		if (CreateEnergyCardListFromHand())
			goto check_id;
		hTemp_ffa0 = wDuelTempList[0];
		hTempPlayAreaLocation_ffa1 = PLAY_AREA_ARENA;
		AIMakeDecision(OPPACTION_PLAY_ENERGY);
	}

check_id:
	e = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	if (e == MYSTERIOUS_FOSSIL)
		goto mysterious_fossil_or_clefairy_doll;
	if (e == CLEFAIRY_DOLL)
		goto mysterious_fossil_or_clefairy_doll;

	/* if card is Asleep or Paralyzed, set carry and exit
	 * else, load the status in hTemp_ffa0 */
	hTempPlayAreaLocation_ffa1 = loc; /* pop af */
	b = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS);
	a = b & CNF_SLP_PRZ;
	if (a == ASLEEP)
		return true; /* .set_carry */
	if (a == PARALYZED)
		return true; /* .set_carry */
	hTemp_ffa0 = b;
	hTempRetreatCostCards[0] = 0xff;

	/* check energy required to retreat
	 * if the cost is 0, retreat right away */
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	wTempCardRetreatCost = GetPlayAreaCardRetreatCost();
	if (!wTempCardRetreatCost)
		goto retreat;

	/* if cost > 0 and number of energy cards attached == cost
	 * discard them all */
	CreateArenaOrBenchEnergyCardList(PLAY_AREA_ARENA);
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	c = wTotalAttachedEnergies;
	if (wTempCardRetreatCost == c) {
		hl = hTempRetreatCostCards;
		de = wDuelTempList;
		do { /* .loop_1 */
			a = *de++;
			*hl++ = a;
		} while (a != 0xff);
		goto retreat;
	}

	/* if cost > 0 and number of energy cards attached > cost
	 * choose energy cards to discard according to color */
	/* .choose_energy_discard */
	e = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	wTempCardID = e;
	LoadCardDataToBuffer1_FromCardID(e);
	wTempCardType = wLoadedCard1.Type | TYPE_ENERGY;
	c = wTempCardRetreatCost;

	/* first, look for and discard double colorless energy
	 * if retreat cost is >= 2 */
	hl = wDuelTempList;
	de = hTempRetreatCostCards;
	for (;;) { /* .loop_2 */
		if (c < 2)
			break; /* .energy_not_same_color */
		a = *hl++;
		if (a == 0xff)
			break; /* .energy_not_same_color */
		*de = a;
		if (GetCardIDFromDeckIndex(a) != DOUBLE_COLORLESS_ENERGY)
			continue;
		RemoveCardFromDuelTempList(*de);
		hl--;
		de++;
		c--;
		c--;
		if (!c)
			goto end_retreat_list;
	}

	/* second, shuffle attached cards and discard energy cards
	 * that are not of the same type as the Pokémon
	 * the exception for this are cards that are needed for
	 * some attacks but are not of the same color as the Pokémon
	 * (i.e. Psyduck's Headache attack)
	 * and energy cards attached to Eevee corresponding to a
	 * color of any of its evolutions (water, fire, lightning) */
	/* .energy_not_same_color */
	hl = wDuelTempList;
	ShuffleCards(hl, CountCardsInDuelTempList());
	for (;;) { /* .loop_3 */
		a = *hl++;
		if (a == 0xff)
			break; /* .any_energy */
		*de = a;
		if (CheckIfEnergyIsUseful(a))
			continue;
		RemoveCardFromDuelTempList(*de);
		hl--;
		de++;
		if (!--c)
			goto end_retreat_list;
	}

	/* third, discard any card until
	 * cost requirement is met */
	/* .any_energy */
	hl = wDuelTempList;
	for (;;) { /* .loop_4 */
		a = *hl++;
		if (a == 0xff)
			return true; /* .set_carry */
		*de++ = a;
		if (GetCardIDFromDeckIndex(a) == DOUBLE_COLORLESS_ENERGY) {
			if (!--c)
				goto end_retreat_list;
		}
		/* .not_double_colorless */
		if (!--c)
			break;
	}

end_retreat_list:
	*de = 0xff;

retreat:
	AIMakeDecision(OPPACTION_ATTEMPT_RETREAT);
	return false; /* or a */

	/* handle Mysterious Fossil and Clefairy Doll
	 * if there are bench Pokémon, use effect to discard card
	 * this is equivalent to using its Pokémon Power */
mysterious_fossil_or_clefairy_doll:
	if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) < 2) {
		/* doesn't have any bench */
		return true; /* pop af / jr .set_carry */
	}

	/* .has_bench */
	hTempCardIndex_ff9f = GetTurnDuelistVariable(DUELVARS_ARENA_CARD);
	hTemp_ffa0 = PLAY_AREA_ARENA;
	AIMakeDecision(OPPACTION_USE_PKMN_POWER);
	hAIPkmnPowerEffectParam = loc; /* pop af */
	AIMakeDecision(OPPACTION_EXECUTE_PKMN_POWER_EFFECT);
	AIMakeDecision(OPPACTION_DUEL_MAIN_SCENE);
	return false; /* or a */
}
