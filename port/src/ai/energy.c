/* Port of src/engine/duel/ai/energy.asm */
#include "ai/ai.h"

static bool AIProcessAndTryToPlayEnergy_has_logic_flags(void);
static void AIProcessEnergyCards_loop_play_area(u8 b);
static u8 CheckSpecificDecksToAttachDoubleColorless_get_id(void);

/* processes AI energy card playing logic
 * with AI_ENERGY_FLAG_DONT_PLAY flag on
 * unreferenced */
bool Func_16488(void)
{
	TRACE_FN();
	u8 *de;
	const u8 *hl;
	u8 b;

	wAIEnergyAttachLogicFlags = AI_ENERGY_FLAG_DONT_PLAY;
	de = wTempPlayAreaAIScore;
	hl = wPlayAreaAIScore;
	b = MAX_PLAY_AREA_POKEMON;
	do { /* .loop */
		*de++ = *hl++;
	} while (--b);
	*de = wAIScore; /* wTempAIScore */
	return AIProcessAndTryToPlayEnergy_has_logic_flags();
}

/* have AI choose an energy card to play, but do not play it.
 * does not consider whether the cards have evolutions to be played.
 * return carry if an energy card is chosen to use in any Play Area card,
 * and if so, return its Play Area location in hTempPlayAreaLocation_ff9d. */
bool AIProcessButDontPlayEnergy_SkipEvolution(void)
{
	TRACE_FN();
	u8 *de;
	const u8 *hl;
	u8 b;

	wAIEnergyAttachLogicFlags = AI_ENERGY_FLAG_DONT_PLAY | AI_ENERGY_FLAG_SKIP_EVOLUTION;

	/* backup wPlayAreaAIScore in wTempPlayAreaAIScore. */
	de = wTempPlayAreaAIScore;
	hl = wPlayAreaAIScore;
	b = MAX_PLAY_AREA_POKEMON;
	do { /* .loop */
		*de++ = *hl++;
	} while (--b);

	*de = wAIScore; /* wTempAIScore */

	return AIProcessEnergyCards();
}

/* have AI choose an energy card to play, but do not play it.
 * does not consider whether the cards have evolutions to be played.
 * return carry if an energy card is chosen to use in any Bench card,
 * and if so, return its Play Area location in hTempPlayAreaLocation_ff9d. */
bool AIProcessButDontPlayEnergy_SkipEvolutionAndArena(void)
{
	TRACE_FN();
	u8 *de;
	const u8 *hl;
	u8 b;

	wAIEnergyAttachLogicFlags = AI_ENERGY_FLAG_DONT_PLAY | AI_ENERGY_FLAG_SKIP_EVOLUTION | AI_ENERGY_FLAG_SKIP_ARENA_CARD;

	/* backup wPlayAreaAIScore in wTempPlayAreaAIScore. */
	de = wTempPlayAreaAIScore;
	hl = wPlayAreaAIScore;
	b = MAX_PLAY_AREA_POKEMON;
	do { /* .loop */
		*de++ = *hl++;
	} while (--b);

	*de = wAIScore; /* wTempAIScore */

	return AIProcessEnergyCards();
}

/* copies wTempPlayAreaAIScore to wPlayAreaAIScore
 * and loads wAIScore with value in wTempAIScore.
 * identical to RetrievePlayAreaAIScoreFromBackup2. */
void RetrievePlayAreaAIScoreFromBackup1(void)
{
	u8 *de = wPlayAreaAIScore;
	const u8 *hl = wTempPlayAreaAIScore;
	u8 b = MAX_PLAY_AREA_POKEMON;
	do { /* .loop */
		*de++ = *hl++;
	} while (--b);
	wAIScore = *hl; /* wTempAIScore */
}

/* have AI decide whether to play energy card from hand
 * and determine which card is best to attach it. */
bool AIProcessAndTryToPlayEnergy(void)
{
	TRACE_FN();
	wAIEnergyAttachLogicFlags = 0;
	return AIProcessAndTryToPlayEnergy_has_logic_flags();
}

/* AIProcessAndTryToPlayEnergy.has_logic_flags (also entered from Func_16488) */
static bool AIProcessAndTryToPlayEnergy_has_logic_flags(void)
{
	if (!CreateEnergyCardListFromHand())
		return AIProcessEnergyCards();

	/* no energy */
	if (!wAIEnergyAttachLogicFlags)
		return false; /* .exit */
	RetrievePlayAreaAIScoreFromBackup1();
	return false; /* or a: carry clear */
}

/* have AI decide whether to play energy card
 * and determine which card is best to attach it. */
bool AIProcessEnergyCards(void)
{
	TRACE_FN();
	u8 b, c;
	u8 *hl;

	/* initialize Play Area AI score */
	b = MAX_PLAY_AREA_POKEMON;
	hl = wPlayAreaEnergyAIScore;
	do { /* .loop */
		*hl++ = 0x80;
	} while (--b);

	/* Legendary Articuno Deck has its own energy card logic */
	HandleLegendaryArticunoEnergyScoring();

	/* start the main Play Area loop */
	b = PLAY_AREA_ARENA;
	c = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);

	do { /* .loop_play_area */
		AIProcessEnergyCards_loop_play_area(b);
		/* store bench score for this card. */
		/* .store_score */
		wPlayAreaAIScore[hTempPlayAreaLocation_ff9d] = wAIScore;
		b++;
	} while (--c);

	/* the Play Area loop is over and the score
	 * for each card has been calculated.
	 * now to determine the highest score. */
	if (!FindPlayAreaCardWithHighestAIScore())
		goto not_found;

	if (wAIEnergyAttachLogicFlags) {
		RetrievePlayAreaAIScoreFromBackup1();
		return true; /* scf */
	}

	/* .play_card */
	CreateEnergyCardListFromHand();
	return AITryToPlayEnergyCard();

not_found:
	if (!wAIEnergyAttachLogicFlags)
		return false; /* .no_carry */
	RetrievePlayAreaAIScoreFromBackup1();
	return false; /* or a: carry clear */
}

/* .loop_play_area body for Play Area location b, up to the point
 * where the original jumps to (or reaches) .store_score */
static void AIProcessEnergyCards_loop_play_area(u8 b)
{
	u8 a, d, e, dmg, evo;
	const u8 *hl;

	hTempPlayAreaLocation_ff9d = b;
	wAIScore = 0x80;
	wTempAI = 0xff;
	if (!(wAIEnergyAttachLogicFlags & AI_ENERGY_FLAG_SKIP_EVOLUTION)) {
		/* check if energy needed is found in hand
		 * and if there's an evolution in hand or deck
		 * and if so, add to AI score */
		CreateHandCardList(NULL);
		a = GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD));
		wCurCardCanAttack = a;
		a = GetAttacksEnergyCostBits(a);
		if (!CheckEnergyFlagsNeededInList(a))
			return; /* jp nc, .store_score */
		if (CheckForEvolutionInList(wCurCardCanAttack, &evo)) {
			wTempAI = evo; /* store evolution card found */
			AIEncourage(2);
		} else {
			/* .no_evolution_in_hand */
			if (CheckForEvolutionInDeck(wCurCardCanAttack, &evo))
				AIEncourage(1);
		}
	}

	/* if there's no Muk in any Play Area
	 * and there's VenusaurLv67 in own Play Area,
	 * add to AI score */
	/* .check_venusaur */
	if (!CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK)) {
		if (CountTurnDuelistPokemonWithActivePkmnPower(VENUSAUR_LV67))
			AIEncourage(1);
	}

	/* .check_if_active */
	a = hTempPlayAreaLocation_ff9d;
	if (a)
		goto bench;

	/* arena */
	if (wAIBarrierFlagCounter & (1 << AI_MEWTWO_MILL_F)) {
		/* subtract from score instead
		 * if Player is running MewtwoLv53 mill deck. */
		AIDiscourage(5);
		goto check_defending_can_ko;
	}

	/* .add_to_score */
	AIEncourage(4);

	/* lower AI score if poison/double poison
	 * will KO Pokémon between turns
	 * or if the defending Pokémon can KO */
	a = ConvertHPToDamageCounters_Bank5(GetTurnDuelistVariable(DUELVARS_ARENA_CARD_HP));
	if (a >= 3)
		goto check_defending_can_ko;
	/* hp < 30 */
	if (a == 2) {
		/* .has_20_hp */
		if (!(GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS) & DOUBLE_POISONED))
			goto check_defending_can_ko;
	} else {
		/* hp = 10 */
		if (!(GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS) & POISONED))
			goto check_defending_can_ko;
	}
	/* .poison_will_ko */
	AIDiscourage(10);
	goto check_bench;

check_defending_can_ko:
	if (!CheckIfDefendingPokemonCanKnockOut(&dmg))
		goto ai_score_bonus;
	AIDiscourage(10);

	/* if either poison will KO or defending Pokémon can KO,
	 * check if there are bench Pokémon,
	 * if there are not, add AI score */
check_bench:
	if ((u8)(GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) - 1))
		goto ai_score_bonus;
	AIEncourage(6);
	goto ai_score_bonus;

	/* lower AI score by 3 - (bench HP)/10
	 * if bench HP < 30 */
bench:
	a = ConvertHPToDamageCounters_Bank5(GetTurnDuelistVariable((u8)(a + DUELVARS_ARENA_CARD_HP)));
	if (a >= 3)
		goto ai_score_bonus;
	/* hp < 30 */
	AIDiscourage((u8)(3 - a));

	/* check list in wAICardListEnergyBonus */
ai_score_bonus:
	if (wAICardListEnergyBonus[1] == 0)
		goto check_boss_deck; /* is null */
	hl = AIListPtr(rd16(wAICardListEnergyBonus));

	e = GetCardIDFromDeckIndex(GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)));

	for (;;) { /* .loop_id_list */
		a = *hl++;
		if (!a)
			goto check_boss_deck;
		if (a != e) {
			/* .next_id */
			hl += 2;
			continue;
		}

		/* number of attached energy cards */
		d = *hl++;
		GetPlayAreaCardAttachedEnergies(hTempPlayAreaLocation_ff9d);
		if (wTotalAttachedEnergies >= d) {
			/* already reached target number of energy cards */
			AIDiscourage(10);
			return; /* jr .store_score ; bug, should be jr .check_boss_deck */
		}

		/* .check_id_score */
		a = *hl++;
		if (a >= 0x80) {
			AIEncourage((u8)(a - 0x80));
		} else {
			/* .decrease_score_1 */
			d = a;
			AIDiscourage((u8)(0x80 - d));
		}
		goto check_boss_deck;
	}

	/* if it's a boss deck, call HandleAIEnergyScoringForRepeatedBenchPokemon
	 * and apply to the AI score the values
	 * determined for this card */
check_boss_deck:
	if (!CheckIfNotABossDeckID()) {
		HandleAIEnergyScoringForRepeatedBenchPokemon();

		/* applies wPlayAreaEnergyAIScore */
		a = wPlayAreaEnergyAIScore[hTempPlayAreaLocation_ff9d];
		if (a >= 0x80) {
			AIEncourage((u8)(a - 0x80));
		} else {
			/* .decrease_score_2 */
			b = a;
			AIDiscourage((u8)(0x80 - b));
		}
	}

	/* .skip_boss_deck */
	AIEncourage(1);

	/* add AI score for both attacks,
	 * according to their energy requirements. */
	DetermineAIScoreOfAttackEnergyRequirement(FIRST_ATTACK_OR_PKMN_POWER);
	DetermineAIScoreOfAttackEnergyRequirement(SECOND_ATTACK);
}

/* checks score related to selected attack,
 * in order to determine whether to play energy card.
 * the AI score is increased/decreased accordingly.
 * input:
 *	[wSelectedAttack] = attack to check. */
void DetermineAIScoreOfAttackEnergyRequirement(u8 attack)
{
	TRACE_FN();
	u8 a, b, c, e, idx, saved;

	wSelectedAttack = attack;
	if (CheckEnergyNeededForAttack(&b, &c, &e))
		goto not_enough_energy;
	if (CheckLoadedAttackFlag(ATTACK_FLAG2_ADDRESS | ATTACHED_ENERGY_BOOST_F))
		goto attached_energy_boost;
	if (CheckLoadedAttackFlag(ATTACK_FLAG2_ADDRESS | DISCARD_ENERGY_F))
		goto discard_energy;
	goto check_evolution;

attached_energy_boost:
	a = wLoadedAttackEffectParam;
	if (a == MAX_ENERGY_BOOST_IS_LIMITED)
		goto check_surplus_energy;

	/* is MAX_ENERGY_BOOST_IS_NOT_LIMITED,
	 * which is equal to 3, add to score. */
	AIEncourage(a);
	goto check_evolution;

check_surplus_energy:
	if (CheckIfNoSurplusEnergyForAttack(&a))
		goto asm_166cd;
	if (a < 3) /* check how much surplus energy */
		goto asm_166cd;

asm_166c5:
	AIDiscourage(5);
	goto check_evolution;

asm_166cd:
	AIEncourage(2);

	/* check whether attack has ATTACHED_ENERGY_BOOST flag
	 * and add to AI score if attaching another energy
	 * will KO defending Pokémon.
	 * add more to score if this is currently active Pokémon. */
	if (!CheckLoadedAttackFlag(ATTACK_FLAG2_ADDRESS | ATTACHED_ENERGY_BOOST_F))
		goto check_evolution;
	EstimateDamage_VersusDefendingCard(wSelectedAttack);
	a = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);
	if (a <= wDamage[0]) /* sub [hl] / jp c / jp z */
		goto check_evolution;
	b = (u8)(wDamage[0] + 10); /* boost gained by attaching another energy card */
	a = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);
	if (a > b) /* sub b / jr c, .attaching_kos_player / jr nz, .check_evolution */
		goto check_evolution;

	/* .attaching_kos_player */
	AIEncourage(20);
	if (hTempPlayAreaLocation_ff9d)
		goto check_evolution;
	AIEncourage(10);
	goto check_evolution;

	/* checks if there is surplus energy for attack
	 * that discards attached energy card.
	 * if current card is ZapdosLv64, don't add to score.
	 * if there is no surplus energy, encourage playing energy. */
discard_energy:
	if (wLoadedCard1.ID == ZAPDOS_LV64)
		goto check_evolution;
	if (CheckIfNoSurplusEnergyForAttack(&a))
		goto asm_166cd;
	goto asm_166c5;

not_enough_energy:
	if (CheckLoadedAttackFlag(ATTACK_FLAG2_ADDRESS | IGNORE_THIS_ATTACK_F))
		AIDiscourage(5);

	/* if the energy card color needed is in hand, increase AI score.
	 * if a colorless card is needed, increase AI score. */
	/* .check_color_needed */
	if (b) {
		if (!LookForCardIDInHand(e, &idx)) {
			AIEncourage(4);
			goto check_total_needed;
		}
	}
	/* .check_colorless_needed */
	if (!c)
		goto check_evolution;
	AIEncourage(3);

	/* if only one energy card is needed for attack,
	 * encourage playing energy card. */
check_total_needed:
	a = (u8)(b + c);
	a--;
	if (a)
		goto check_evolution;
	AIEncourage(3);

	/* if the attack KOs player and this is the active card, add to AI score. */
	if (hTempPlayAreaLocation_ff9d)
		goto check_evolution;
	EstimateDamage_VersusDefendingCard(wSelectedAttack);
	a = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);
	if (a > wDamage[0]) /* sub [hl] / jr z, .atk_kos_defending / jr nc, .check_evolution */
		goto check_evolution;
	/* .atk_kos_defending */
	AIEncourage(20);

	/* this is possibly a bug.
	 * this is an identical check as above to test whether this card is active.
	 * in case it is active, the score gets added 10 more points, */
	if (hTempPlayAreaLocation_ff9d)
		goto check_evolution;
	AIEncourage(10);

check_evolution:
	a = wTempAI; /* evolution in hand */
	if (a == 0xff)
		return;

	/* temporarily replace this card with evolution in hand. */
	b = a;
	saved = GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)); /* push af */
	TurnDuelistVars()[(u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)] = b;

	/* check for energy still needed for evolution to attack.
	 * if IGNORE_THIS_ATTACK is not set, check what color is needed.
	 * if the energy card color needed is in hand, increase AI score.
	 * if a colorless card is needed, increase AI score. */
	if (!CheckEnergyNeededForAttack(&b, &c, &e))
		goto done;
	if (CheckLoadedAttackFlag(ATTACK_FLAG2_ADDRESS | IGNORE_THIS_ATTACK_F))
		goto done;
	if (b) {
		if (!LookForCardIDInHand(e, &idx)) {
			AIEncourage(2);
			goto done;
		}
	}
	/* .check_colorless_needed_evo */
	if (c)
		AIEncourage(1);

	/* recover the original card in the Play Area location. */
done:
	TurnDuelistVars()[(u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)] = saved; /* pop af */
}

/* returns in hTempPlayAreaLocation_ff9d the Play Area location
 * of the card with the highest Play Area AI score, unless
 * the highest score is below $85.
 * if it succeeds in return a card location, set carry.
 * if AI_ENERGY_FLAG_SKIP_ARENA_CARD is set in wAIEnergyAttachLogicFlags
 * doesn't include the Arena card and there's no minimum score. */
bool FindPlayAreaCardWithHighestAIScore(void)
{
	TRACE_FN();
	u8 a, b, c, d, e;
	const u8 *hl;

	if (wAIEnergyAttachLogicFlags & AI_ENERGY_FLAG_SKIP_ARENA_CARD)
		goto only_bench;

	b = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	c = PLAY_AREA_ARENA;
	e = c;
	d = c;
	hl = wPlayAreaAIScore;
	/* find highest Play Area AI score. */
	do { /* .loop_1 */
		a = *hl++;
		if (a > e) {
			e = a; /* overwrite highest score found */
			d = c; /* overwrite Play Area of highest score */
		}
		/* .next_1 */
		c++;
	} while (--b);

	/* if highest AI score is below $85, return no carry.
	 * else, store Play Area location and return carry. */
	if (e < 0x85)
		return false; /* .not_enough_score */
	hTempPlayAreaLocation_ff9d = d;
	return true;

	/* same as above but only check bench Pokémon scores. */
only_bench:
	a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	a--;
	if (!a)
		return false; /* .no_carry */

	b = a;
	e = 0;
	c = PLAY_AREA_BENCH_1;
	d = c;
	hl = wPlayAreaAIScore + 1;
	do { /* .loop_2 */
		a = *hl++;
		if (a > e) {
			e = a; /* overwrite highest score found */
			d = c; /* overwrite Play Area of highest score */
		}
		/* .next_2 */
		c++;
	} while (--b);

	/* in this case, there is no minimum threshold AI score. */
	hTempPlayAreaLocation_ff9d = d;
	return true;
}

/* returns carry if there's an evolution card
 * that can evolve card in hTempPlayAreaLocation_ff9d,
 * and that card needs energy to use wSelectedAttack. */
bool CheckIfEvolutionNeedsEnergyForAttack(u8 *b, u8 *c, u8 *e)
{
	TRACE_FN();
	u8 a, evo, saved;
	bool carry;

	CreateHandCardList(NULL);
	a = GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD));
	if (!CheckCardEvolutionInHandOrDeck(a, &evo))
		return false; /* *b, *c, *e unchanged (the only caller returns right away) */

	/* .has_evolution */
	saved = GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)); /* push af */
	TurnDuelistVars()[(u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)] = evo; /* ld [hl], b */
	carry = CheckEnergyNeededForAttack(b, c, e);
	/* (.not_enough_energy restores the card the same way, then sets carry) */
	TurnDuelistVars()[(u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)] = saved; /* pop af */
	return carry;
}

/* returns in e the card ID of the energy required for
 * the Discard/Energy Boost attack loaded in wSelectedAttack.
 * if it's ZapdosLv64's Thunderbolt attack, return no carry.
 * if it's Charizard's Fire Spin or Exeggutor's Big Eggsplosion
 * attack, don't return energy card ID, but set carry.
 * output:
 *	b = TRUE if needs color energy;
 *	c = TRUE if only needs colorless energy;
 *	carry set if not ZapdosLv64's Thunderbolt attack. */
bool GetEnergyCardForDiscardOrEnergyBoostAttack(u8 *b, u8 *c, u8 *e)
{
	TRACE_FN();
	u8 a;
	const u8 *hl;

	/* load card ID and check selected attack index. */
	a = LoadCardDataToBuffer2_FromDeckIndex(GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)));
	*b = a;
	if (!wSelectedAttack) {
		/* .first_attack */
		hl = wLoadedCard2.Atk1.EnergyCost;
	} else {
		/* check if second attack is ZapdosLv64's Thunderbolt,
		 * Charizard's Fire Spin or Exeggutor's Big Eggsplosion,
		 * for these to be treated differently.
		 * for both attacks, load its energy cost. */
		a = *b;
		if (a == ZAPDOS_LV64) {
			/* for ZapdosLv64's Thunderbolt attack, return with no carry. */
			/* .zapdos */
			return false;
		}
		if (a == CHARIZARD || a == EXEGGUTOR) {
			/* Charizard's Fire Spin and Exeggutor's Big Eggsplosion,
			 * return carry. */
			/* .charizard_or_exeggutor */
			*b = FALSE;
			*c = TRUE;
			return true;
		}
		hl = wLoadedCard2.Atk2.EnergyCost;
	}

	/* check which energy color the attack requires,
	 * and load in e the card ID of corresponding energy card,
	 * then return carry flag set. */
	/* .fire */
	a = *hl++;
	*b = a;
	if (a & 0xf0) {
		*e = FIRE_ENERGY;
		goto set_carry;
	}
	/* .grass */
	if (*b & 0x0f) {
		*e = GRASS_ENERGY;
		goto set_carry;
	}
	/* .lightning */
	a = *hl++;
	*b = a;
	if (a & 0xf0) {
		*e = LIGHTNING_ENERGY;
		goto set_carry;
	}
	/* .water */
	if (*b & 0x0f) {
		*e = WATER_ENERGY;
		goto set_carry;
	}
	/* .fighting */
	a = *hl++;
	*b = a;
	if (a & 0xf0) {
		*e = FIGHTING_ENERGY;
		goto set_carry;
	}
	/* .psychic */
	*e = PSYCHIC_ENERGY;

set_carry:
	*b = TRUE;
	*c = FALSE;
	return true;
}

/* called after the AI has decided which card to attach
 * energy from hand. AI does checks to determine whether
 * this card needs more energy or not, and chooses the
 * right energy card to play. If the card is played,
 * return with carry flag set. */
bool AITryToPlayEnergyCard(void)
{
	TRACE_FN();
	u8 a, b, c, e;
	bool carry;
	const u8 *hl;

	/* check if energy cards are still needed for attacks.
	 * if first attack doesn't need, test for the second attack. */
	wTempAI = 0;
	wSelectedAttack = FIRST_ATTACK_OR_PKMN_POWER;
	if (CheckEnergyNeededForAttack(&b, &c, &e)) {
		if (b)
			goto check_deck;
		if (c)
			goto check_deck;
	}

second_attack:
	wSelectedAttack = SECOND_ATTACK;
	if (!CheckEnergyNeededForAttack(&b, &c, &e))
		goto check_discard_or_energy_boost;
	if (b)
		goto check_deck;
	if (c)
		goto check_deck;

	/* neither attack needs energy cards to be used.
	 * check whether these attacks can be given
	 * extra energy cards for their effects. */
check_discard_or_energy_boost:
	wTempAI = 0x01;

	/* for both attacks, check if it has the effect of
	 * discarding energy cards or attached energy boost. */
	wSelectedAttack = FIRST_ATTACK_OR_PKMN_POWER;
	CheckEnergyNeededForAttack(&b, &c, &e);
	if (CheckLoadedAttackFlag(ATTACK_FLAG2_ADDRESS | ATTACHED_ENERGY_BOOST_F))
		goto energy_boost_or_discard_energy;
	if (CheckLoadedAttackFlag(ATTACK_FLAG2_ADDRESS | DISCARD_ENERGY_F))
		goto energy_boost_or_discard_energy;

	wSelectedAttack = SECOND_ATTACK;
	CheckEnergyNeededForAttack(&b, &c, &e);
	if (CheckLoadedAttackFlag(ATTACK_FLAG2_ADDRESS | ATTACHED_ENERGY_BOOST_F))
		goto energy_boost_or_discard_energy;
	if (CheckLoadedAttackFlag(ATTACK_FLAG2_ADDRESS | DISCARD_ENERGY_F))
		goto energy_boost_or_discard_energy;

	/* if none of the attacks have those flags, do an additional
	 * check to ascertain whether evolution card needs energy
	 * to use second attack. Return if all these checks fail. */
	if (!CheckIfEvolutionNeedsEnergyForAttack(&b, &c, &e))
		return false;
	CreateEnergyCardListFromHand();
	goto check_deck;

	/* for attacks that discard energy or get boost for
	 * additional energy cards, get the energy card ID required by attack.
	 * if it's ZapdosLv64's Thunderbolt attack, return. */
energy_boost_or_discard_energy:
	if (!GetEnergyCardForDiscardOrEnergyBoostAttack(&b, &c, &e))
		return false;

	/* some decks allow basic Pokémon to be given double colorless
	 * in anticipation for evolution, so play card if that is the case. */
check_deck:
	if (CheckSpecificDecksToAttachDoubleColorless())
		goto play_energy_card;

	if (!b)
		goto colorless_energy;

	/* in this case, Pokémon needs a specific basic energy card.
	 * look for basic energy card needed in hand and play it. */
	carry = LookForCardIDInHand(e, &a);
	hTemp_ffa0 = a;
	if (!carry)
		goto play_energy_card;

	/* in this case Pokémon just needs colorless (any basic energy card).
	 * if active card, check if it needs 2 colorless.
	 * if it does (and also doesn't additionally need a color energy),
	 * look for double colorless card in hand and play it if found. */
colorless_energy:
	if (hTempPlayAreaLocation_ff9d)
		goto look_for_any_energy;
	if (!c)
		goto check_if_done;
	if (c != 2)
		goto look_for_any_energy;

	/* needs two colorless */
	hl = wDuelTempList;
	for (;;) { /* .loop_1 */
		a = *hl++;
		if (a == 0xff)
			goto look_for_any_energy;
		hTemp_ffa0 = a;
		e = GetCardIDFromDeckIndex(a);
		if (e == DOUBLE_COLORLESS_ENERGY)
			goto play_energy_card;
	}

	/* otherwise, look for any card and play it.
	 * if it's a boss deck, don't play double colorless in this situation. */
look_for_any_energy:
	hl = wDuelTempList;
	ShuffleCards(wDuelTempList, CountCardsInDuelTempList());
	for (;;) { /* .loop_2 */
		a = *hl++;
		if (a == 0xff)
			goto check_if_done;
		if (CheckIfOpponentHasBossDeckID()) {
			/* push af / pop bc: b = the card (c = the flags; neither is read again) */
			b = a;
			e = GetCardIDFromDeckIndex(a);
			if (e == DOUBLE_COLORLESS_ENERGY)
				continue;
			a = b;
		}
		/* .load_card */
		hTemp_ffa0 = a;
		break;
	}

	/* plays energy card loaded in hTemp_ffa0 and sets carry flag. */
play_energy_card:
	hTempPlayAreaLocation_ffa1 = hTempPlayAreaLocation_ff9d;
	AIMakeDecision(OPPACTION_PLAY_ENERGY);
	return true;

	/* wTempAI is 1 if the attack had a Discard/Energy Boost effect,
	 * and 0 otherwise. If 1, then return. If not one, check if
	 * there is still a second attack to check. */
check_if_done:
	if (wTempAI)
		return false;
	/* .check_first_attack */
	if (!wSelectedAttack)
		goto second_attack;
	return false;
}

/* check if playing certain decks so that AI can decide whether to play
 * double colorless to some specific cards.
 * these are cards that do not need double colorless to any of their attacks
 * but are required by their evolutions.
 * return carry if there's a double colorless in hand to attach
 * and it's one of the card IDs from these decks.
 * output:
 *	[hTemp_ffa0] = card index of double colorless in hand;
 *	carry set if can play energy card.
 * (preserves bc, de and hl: the C caller's registers are untouched) */
bool CheckSpecificDecksToAttachDoubleColorless(void)
{
	TRACE_FN();
	u8 a, idx;

	/* check if AI is playing any of the applicable decks. */
	a = wOpponentDeckID;
	if (a == LEGENDARY_DRAGONITE_DECK_ID)
		goto legendary_dragonite_deck;
	if (a == FIRE_CHARGE_DECK_ID)
		goto fire_charge_deck;
	if (a == LEGENDARY_RONALD_DECK_ID)
		goto legendary_ronald_deck;

no_carry:
	return false;

	/* if playing Legendary Dragonite deck,
	 * check for Charmander and Dratini. */
legendary_dragonite_deck:
	a = CheckSpecificDecksToAttachDoubleColorless_get_id();
	if (a == CHARMANDER)
		goto check_colorless_attached;
	if (a == DRATINI)
		goto check_colorless_attached;
	goto no_carry;

	/* if playing Fire Charge deck,
	 * check for Growlithe. */
fire_charge_deck:
	a = CheckSpecificDecksToAttachDoubleColorless_get_id();
	if (a == GROWLITHE)
		goto check_colorless_attached;
	goto no_carry;

	/* if playing Legendary Ronald deck,
	 * check for Dratini. */
legendary_ronald_deck:
	a = CheckSpecificDecksToAttachDoubleColorless_get_id();
	if (a == DRATINI)
		goto check_colorless_attached;
	goto no_carry;

	/* check if card has any colorless energy cards attached,
	 * and if there are any, return no carry. */
check_colorless_attached:
	GetPlayAreaCardAttachedEnergies(hTempPlayAreaLocation_ff9d);
	if (wAttachedEnergies[COLORLESS])
		goto no_carry;

	/* card has no colorless energy, so look for double colorless
	 * in hand and if found, return carry and its card index. */
	if (LookForCardIDInHand(DOUBLE_COLORLESS_ENERGY, &idx))
		goto no_carry;
	hTemp_ffa0 = idx;
	return true;
}

/* .get_id: card ID of the card at [hTempPlayAreaLocation_ff9d] (registers a and e) */
static u8 CheckSpecificDecksToAttachDoubleColorless_get_id(void)
{
	return GetCardIDFromDeckIndex(GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)));
}
