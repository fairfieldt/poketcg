/* Port of src/engine/duel/ai/attacks.asm */
#include "ai/ai.h"

static bool GetAIScoreOfAttack_check_if_kos_bench(u8 a, u8 b, u8 *d_out);

/* have AI choose an attack to use, but do not execute it.
 * return carry if an attack is chosen. */
bool AIProcessButDontUseAttack(void)
{
	TRACE_FN();
	u8 *de;
	const u8 *hl;
	u8 b;

	wAIExecuteProcessedAttack = 0x01;

	/* backup wPlayAreaAIScore in wTempPlayAreaAIScore. */
	de = wTempPlayAreaAIScore;
	hl = wPlayAreaAIScore;
	b = MAX_PLAY_AREA_POKEMON;
	do { /* .loop */
		*de++ = *hl++;
	} while (--b);

	/* copies wAIScore to wTempAIScore */
	*de = wAIScore;
	return AIProcessAttacks();
}

/* copies wTempPlayAreaAIScore to wPlayAreaAIScore
 * and loads wAIScore with value in wTempAIScore.
 * identical to RetrievePlayAreaAIScoreFromBackup1. */
void RetrievePlayAreaAIScoreFromBackup2(void)
{
	u8 *de = wPlayAreaAIScore;
	const u8 *hl = wTempPlayAreaAIScore;
	u8 b = MAX_PLAY_AREA_POKEMON;
	do { /* .loop */
		*de++ = *hl++;
	} while (--b);

	wAIScore = *hl; /* wTempAIScore */
}

/* have AI choose and execute an attack.
 * return carry if an attack was chosen and attempted. */
bool AIProcessAndTryToUseAttack(void)
{
	TRACE_FN();
	wAIExecuteProcessedAttack = 0;
	/* fallthrough */
	return AIProcessAttacks();
}

/* checks which of the Active card's attacks for AI to use.
 * If any of the attacks has enough AI score to be used,
 * AI will use it if wAIExecuteProcessedAttack is 0.
 * in either case, return carry if an attack is chosen to be used. */
bool AIProcessAttacks(void)
{
	TRACE_FN();
	u8 a, b, c;

	/* if AI used PlusPower, load its attack index */
	if (wPreviousAIFlags & AI_FLAG_USED_PLUSPOWER) {
		wSelectedAttack = wAIPlusPowerAttack;
		goto attack_chosen;
	}

	/* .no_pluspower */
	/* if Player is running MewtwoLv53 mill deck,
	 * skip attack if Barrier counter is 0. */
	if (wAIBarrierFlagCounter == AI_MEWTWO_MILL + 0)
		goto dont_attack;

	/* determine AI score of both attacks. */
	GetAIScoreOfAttack(FIRST_ATTACK_OR_PKMN_POWER);
	wFirstAttackAIScore = wAIScore;
	GetAIScoreOfAttack(SECOND_ATTACK);

	/* compare both attack scores */
	c = SECOND_ATTACK;
	b = wFirstAttackAIScore;
	a = wAIScore;
	if (a < b) {
		/* first attack has higher score */
		c--;
		a = b;
	}

	/* c holds the attack index chosen by AI,
	 * and a holds its AI score.
	 * first check if chosen attack has at least minimum score.
	 * then check if first attack is better than second attack
	 * in case the second one was chosen. */
	/* .check_score */
	if (a < 0x50) /* minimum score to use attack */
		goto dont_attack;
	/* enough score, proceed */

	wSelectedAttack = c;
	if (c)
		CheckWhetherToSwitchToFirstAttack();

attack_chosen:
	/* check whether to execute the attack chosen */
	if (wAIExecuteProcessedAttack) {
		/* set carry and reset Play Area AI score
		 * to the previous values. */
		RetrievePlayAreaAIScoreFromBackup2();
		return true; /* scf */
	}

	/* .execute */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_14);

	/* load this attack's damage output against
	 * the current Defending Pokemon. */
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	EstimateDamage_VersusDefendingCard(wSelectedAttack);

	if (!wDamage[0]) {
		/* .check_damage_bench */
		/* check if it can otherwise damage player's bench */
		if (!CheckLoadedAttackFlag(ATTACK_FLAG1_ADDRESS | DAMAGE_TO_OPPONENT_BENCH_F)) {
			/* cannot damage either Defending Pokemon or Bench */
			wAIRetreatScore++;
			goto use_attack;
		}
	}
	/* if damage is not 0, fallthrough */

	/* .can_damage */
	wAIRetreatScore = 0;

	/* return carry if attack is chosen
	 * and AI tries to use it. */
use_attack:
	wAITriedAttack = TRUE;
	AITryUseAttack();
	return true; /* scf */

dont_attack:
	if (!wAIExecuteProcessedAttack) {
		/* return no carry if no viable attack. */
		/* .failed_to_use */
		wAIRetreatScore++;
		return false; /* or a */
	}
	/* reset Play Area AI score
	 * to the previous values. */
	RetrievePlayAreaAIScoreFromBackup2();
	return false; /* (carry cleared by the `or a` before) */
}

/* determines the AI score of attack index in a
 * of card in Play Area location hTempPlayAreaLocation_ff9d. */
void GetAIScoreOfAttack(u8 attack)
{
	TRACE_FN();
	u8 a, b, c, d, e, dmg, player_kos;
	bool carry;

	/* initialize AI score. */
	wSelectedAttack = attack;
	wAIScore = 0x50;

	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	if (CheckIfSelectedAttackIsUnusable())
		goto unusable;
	goto usable;

	/* return zero AI score. */
unusable:
	wAIScore = 0;
	return; /* jp .done */

	/* load arena card IDs */
usable:
	wAICannotDamage = 0;
	wTempTurnDuelistCardID = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	SwapTurn();
	wTempNonTurnDuelistCardID = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));

	/* handle the case where the player has No Damage substatus.
	 * in the case the player does, check if this attack
	 * has a residual effect, or if it can damage the opposing bench.
	 * If none of those are true, render the attack unusable.
	 * also if it's a PKMN power, consider it unusable as well. */
	carry = HandleNoDamageOrEffectSubstatus();
	SwapTurn();
	if (carry) {
		/* player is under No Damage substatus */
		wAICannotDamage = 0x01;
		EstimateDamage_VersusDefendingCard(wSelectedAttack);
		a = wLoadedAttackCategory;
		if (a == POKEMON_POWER)
			goto unusable;
		if (!(a & RESIDUAL)) {
			if (!CheckLoadedAttackFlag(ATTACK_FLAG1_ADDRESS | DAMAGE_TO_OPPONENT_BENCH_F))
				goto unusable;
		}
	}

	/* calculate damage to player to check if attack can KO.
	 * encourage attack if it's able to KO. */
	/* .check_if_can_ko */
	EstimateDamage_VersusDefendingCard(wSelectedAttack);
	a = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);
	if (a <= wDamage[0]) {
		/* .can_ko */
		AIEncourage(20);
	}

	/* raise AI score by the number of damage counters that this attack deals.
	 * if no damage is dealt, subtract AI score. in case wDamage is zero
	 * but wMaxDamage is not, then encourage attack afterwards.
	 * otherwise, if wMaxDamage is also zero, check for damage against
	 * player's bench, and encourage attack in case there is. */
	/* .check_damage */
	wAIAttackIsNonDamaging = 0;
	a = wDamage[0];
	wTempAI = a;
	if (a) {
		AIEncourage(ConvertHPToDamageCounters_Bank5(a));
	} else {
		/* .no_damage */
		wAIAttackIsNonDamaging = 0x01;
		AIDiscourage(0x01);
		if (wAIMaxDamage) {
			AIEncourage(2);
			wAIAttackIsNonDamaging = 0;
		}
		/* .no_max_damage */
		if (CheckLoadedAttackFlag(ATTACK_FLAG1_ADDRESS | DAMAGE_TO_OPPONENT_BENCH_F))
			AIEncourage(2);
	}

	/* handle recoil attacks (low and high recoil). */
	/* .check_recoil */
	if (!CheckLoadedAttackFlag(ATTACK_FLAG1_ADDRESS | LOW_RECOIL_F)) {
		if (!CheckLoadedAttackFlag(ATTACK_FLAG1_ADDRESS | HIGH_RECOIL_F))
			goto check_defending_can_ko;
	}
	/* .is_recoil */
	/* sub from AI score number of damage counters
	 * that attack deals to itself. */
	a = wLoadedAttackEffectParam;
	if (!a)
		goto check_defending_can_ko;
	wDamage[0] = a; /* ld [wDamage], a (low byte only) */
	e = (u8)ApplyDamageModifiers_DamageToSelf();
	AIDiscourage(ConvertHPToDamageCounters_Bank5(e));

	if (CheckLoadedAttackFlag(ATTACK_FLAG1_ADDRESS | HIGH_RECOIL_F))
		goto high_recoil;

	/* if LOW_RECOIL KOs self, decrease AI score */
	a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);
	if (a > e) /* cp e / jr c, .kos_self / jp nz, .check_defending_can_ko */
		goto check_defending_can_ko;
	/* .kos_self */
	AIDiscourage(10);
	/* (falls through into .high_recoil) */

high_recoil:
	/* dismiss this attack if no benched Pokémon */
	if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) < 2)
		goto dismiss_high_recoil_atk;
	/* has benched Pokémon */

	/* here the AI handles high recoil attacks differently
	 * depending on what deck it's playing. */
	a = wOpponentDeckID;
	if (a == ROCK_CRUSHER_DECK_ID)
		goto rock_crusher_deck;
	if (a == ZAPPING_SELFDESTRUCT_DECK_ID)
		goto zapping_selfdestruct_deck;
	if (a == BOOM_BOOM_SELFDESTRUCT_DECK_ID)
		goto encourage_high_recoil_atk;
	/* Boom Boom Selfdestruct deck always encourages */
	if (a != POWER_GENERATOR_DECK_ID)
		goto high_recoil_generic_checks;
	/* Power Generator deck always dismisses */

dismiss_high_recoil_atk:
	wAIScore = 0;
	return; /* jp .done */

encourage_high_recoil_atk:
	AIEncourage(20);
	return; /* jp .done */

	/* Zapping Selfdestruct deck only uses this attack
	 * if number of cards in deck >= 30 and
	 * HP of active card is < half max HP. */
zapping_selfdestruct_deck:
	if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK) >= 31)
		goto high_recoil_generic_checks;
	a = GetCardDamageAndMaxHP(PLAY_AREA_ARENA, &c);
	a = (u8)(a << 1);
	if (a < c)
		goto high_recoil_generic_checks;
	b = 0;
	if (GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD)) != MAGNEMITE_LV13)
		b = 10; /* bench damage */
	/* .magnemite1 */
	b = (u8)(10 + b); /* 20 bench damage if not MagnemiteLv13 */

	/* if this attack causes player to win the duel by
	 * knocking out own Pokémon, dismiss attack. */
	if (GetAIScoreOfAttack_check_if_kos_bench(1, b, &d)) /* count active Pokémon as KO'd */
		goto dismiss_high_recoil_atk;
	goto encourage_high_recoil_atk;

	/* Rock Crusher Deck only uses this attack if
	 * prize count is below 4 and attack wins (or potentially draws) the duel,
	 * (i.e. at least gets KOs equal to prize cards left). */
rock_crusher_deck:
	if (CountPrizes() >= 4)
		goto dismiss_high_recoil_atk;
	/* prize count < 4 */
	b = 20; /* damage dealt to bench */
	SwapTurn();
	carry = GetAIScoreOfAttack_check_if_kos_bench(0, b, &d);
	SwapTurn();
	if (carry)
		goto encourage_high_recoil_atk;

	/* generic checks for all other deck IDs.
	 * encourage attack if it wins (or potentially draws) the duel,
	 * (i.e. at least gets KOs equal to prize cards left).
	 * dismiss it if it causes the player to win. */
high_recoil_generic_checks:
	a = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	if (a == CHANSEY) {
		/* .chansey */
		b = 0; /* no bench damage */
	} else if (a == MAGNEMITE_LV13 || a == WEEZING) {
		/* .magnemite1_or_weezing */
		b = 10; /* bench damage */
	} else {
		b = 20; /* bench damage */
	}

	/* .check_bench_kos */
	SwapTurn();
	carry = GetAIScoreOfAttack_check_if_kos_bench(0, b, &d);
	SwapTurn();
	if (carry)
		goto wins_the_duel;
	player_kos = d; /* push de ... pop bc: b = d */
	if (!GetAIScoreOfAttack_check_if_kos_bench(1, b, &d))
		goto count_own_ko_bench;

	/* attack causes player to draw all prize cards */
	wAIScore = 0;
	return; /* jp .done */

	/* attack causes CPU to draw all prize cards */
wins_the_duel:
	AIEncourage(20);
	return; /* jp .done */

	/* subtract from AI score number of own benched Pokémon KO'd */
count_own_ko_bench:
	a = d;
	if (a) {
		a--;
		AIDiscourage(a);
	}

	/* add to AI score number of player benched Pokémon KO'd */
	/* .count_player_ko_bench */
	AIEncourage(player_kos);
	goto check_defending_can_ko;

	/* if defending card can KO, encourage attack
	 * unless attack is non-damaging. */
check_defending_can_ko:
	a = wSelectedAttack; /* push af */
	carry = CheckIfDefendingPokemonCanKnockOut(&dmg);
	wSelectedAttack = a; /* pop bc / ld a, b */
	if (carry) {
		AIEncourage(5);
		if (wAIAttackIsNonDamaging)
			AIDiscourage(5);
	}

	/* subtract from AI score if this attack requires
	 * discarding any energy cards. */
	/* .check_discard */
	e = wSelectedAttack;
	d = GetTurnDuelistVariable(DUELVARS_ARENA_CARD);
	CopyAttackDataAndDamage_FromDeckIndex(d, e);
	if (CheckLoadedAttackFlag(ATTACK_FLAG2_ADDRESS | DISCARD_ENERGY_F)) {
		AIDiscourage(1);
		AIDiscourage(wLoadedAttackEffectParam);
	}

	/* .asm_16ca6 */
	if (CheckLoadedAttackFlag(ATTACK_FLAG2_ADDRESS | ENCOURAGE_THIS_ATTACK_F))
		AIEncourage(wLoadedAttackEffectParam);

	/* encourage attack if it has a nullify or weaken attack effect. */
	/* .check_nullify_flag */
	if (CheckLoadedAttackFlag(ATTACK_FLAG2_ADDRESS | NULLIFY_OR_WEAKEN_ATTACK_F))
		AIEncourage(1);

	/* encourage attack if it has an effect to draw a card. */
	/* .check_draw_flag */
	if (CheckLoadedAttackFlag(ATTACK_FLAG1_ADDRESS | DRAW_CARD_F))
		AIEncourage(1);

	/* .check_heal_flag */
	if (CheckLoadedAttackFlag(ATTACK_FLAG2_ADDRESS | HEAL_USER_F)) {
		a = wLoadedAttackEffectParam;
		if (a != HEALING_EQUALS_10_HP) {
			b = ConvertHPToDamageCounters_Bank5(wTempAI);
			if (wLoadedAttackEffectParam != HEALING_EQUALS_DAMAGE_DEALT) {
				/* must be HEALING_EQUALS_HALF_DAMAGE_DEALT */
				carry = b & 1;
				b >>= 1;
				if (carry)
					b++;
			}
			/* .asm_16cec */
			a = ConvertHPToDamageCounters_Bank5(GetTurnDuelistVariable(DUELVARS_ARENA_CARD_HP));
			if (a >= b)
				a = b;
		}
		/* .tally_heal_score */
		b = a; /* push af / pop bc */
		a = ConvertHPToDamageCounters_Bank5(GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL));
		if (a >= b)
			a = b;
		/* .add_heal_score */
		AIEncourage(a);
	}

	/* .check_status_effect */
	a = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD);
	SwapTurn();
	e = GetCardIDFromDeckIndex(a);
	SwapTurn();
	/* skip if player has Snorlax */
	if (e == SNORLAX)
		goto handle_special_atks;

	wTempAI = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS);

	/* encourage a poison inflicting attack if opposing Pokémon
	 * isn't (doubly) poisoned already.
	 * if opposing Pokémon is only poisoned and not double poisoned,
	 * and this attack has ENCOURAGE_THIS_ATTACK set, discourage it
	 * (possibly to make Nidoking's Toxic attack less likely to be chosen
	 * if the other Pokémon is poisoned.) */
	if (CheckLoadedAttackFlag(ATTACK_FLAG1_ADDRESS | INFLICT_POISON_F)) {
		a = wTempAI & DOUBLE_POISONED;
		if (!a) {
			/* .add_poison_score */
			AIEncourage(2);
		} else if (a & 0x40) { /* only double poisoned? */
			if (CheckLoadedAttackFlag(ATTACK_FLAG2_ADDRESS | ENCOURAGE_THIS_ATTACK_F))
				AIDiscourage(2);
		}
	}

	/* encourage sleep-inducing attack if other Pokémon isn't asleep. */
	/* .check_sleep */
	if (CheckLoadedAttackFlag(ATTACK_FLAG1_ADDRESS | INFLICT_SLEEP_F)) {
		if ((wTempAI & CNF_SLP_PRZ) != ASLEEP)
			AIEncourage(1);
	}

	/* encourage paralysis-inducing attack if other Pokémon isn't asleep.
	 * otherwise, if other Pokémon is asleep, discourage attack. */
	/* .check_paralysis */
	if (CheckLoadedAttackFlag(ATTACK_FLAG1_ADDRESS | INFLICT_PARALYSIS_F)) {
		if ((wTempAI & CNF_SLP_PRZ) == ASLEEP) {
			/* .sub_prz_score */
			AIDiscourage(1);
		} else {
			AIEncourage(1);
		}
	}

	/* encourage confuse-inducing attack if other Pokémon isn't asleep
	 * or confused already.
	 * otherwise, if other Pokémon is asleep or confused,
	 * discourage attack instead. */
	/* .check_confusion */
	if (CheckLoadedAttackFlag(ATTACK_FLAG1_ADDRESS | INFLICT_CONFUSION_F)) {
		if ((wTempAI & CNF_SLP_PRZ) == ASLEEP) {
			/* .sub_cnf_score */
			AIDiscourage(1);
		} else if ((wTempAI & CNF_SLP_PRZ) != CONFUSED) {
			AIEncourage(1);
		}
	}

	/* if this Pokémon is confused, subtract from score. */
	/* .check_if_confused */
	if ((GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS) & CNF_SLP_PRZ) == CONFUSED)
		AIDiscourage(1);

	/* SPECIAL_AI_HANDLING marks attacks that the AI handles individually.
	 * each attack has its own checks and modifies AI score accordingly. */
handle_special_atks:
	if (!CheckLoadedAttackFlag(ATTACK_FLAG3_ADDRESS | SPECIAL_AI_HANDLING_F))
		return; /* .done */
	a = HandleSpecialAIAttacks();
	if (a >= 0x80) {
		AIEncourage((u8)(a - 0x80));
	} else {
		/* .negative_score */
		b = a;
		AIDiscourage((u8)(0x80 - b));
	}
	/* .done */
}

/* local function that gets called to determine damage to
 * benched Pokémon caused by a HIGH_RECOIL attack.
 * return carry if using attack causes number of benched Pokémon KOs
 * equal to or larger than remaining prize cards.
 * this function is independent on duelist turn, so whatever
 * turn it is when this is called, it's that duelist's
 * bench/prize cards that get checked.
 * input:
 *	a = initial number of KO's beside benched Pokémon,
 *		so that if the active Pokémon is KO'd by the attack,
 *		this counts towards the prize cards collected
 *	b = damage dealt to bench Pokémon
 * output:
 *	*d = number of KO's (register d) */
static bool GetAIScoreOfAttack_check_if_kos_bench(u8 a, u8 b, u8 *d_out)
{
	u8 d, e;
	const u8 *hl;

	d = a;
	hl = TurnDuelistVars() + DUELVARS_BENCH;
	e = PLAY_AREA_ARENA;
	for (;;) { /* .loop */
		e++;
		a = *hl++;
		if (a == 0xff)
			break; /* .exit_loop */
		a = GetTurnDuelistVariable((u8)(e + DUELVARS_ARENA_CARD_HP));
		if (a > b) /* cp b / jr z, .increase_count / jr nc, .loop */
			continue;
		/* .increase_count */
		/* increase d if damage dealt KOs */
		d++;
	}
	/* .exit_loop */
	SwapTurn();
	a = CountPrizes();
	SwapTurn();
	*d_out = d;
	return a <= d; /* cp d / jp c, .set_carry / jp z, .set_carry */
}
