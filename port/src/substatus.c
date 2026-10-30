/* Port of src/home/substatus.asm */
#include "engine.h"

/* doubles the damage at de if swords dance or focus energy was used
 * in the last turn by the turn holder's arena Pokemon */
u16 HandleDoubleDamageSubstatus(u16 de)
{
	if (GetTurnDuelistVariable(DUELVARS_ARENA_CARD_SUBSTATUS3) & (1 << SUBSTATUS3_THIS_TURN_DOUBLE_DAMAGE_F)) {
		if (de != 0)
			de = (u16)(de << 1);
	}
	/* (the original then checks SUBSTATUS1 and SUBSTATUS2 and calls empty routines) */
	return de;
}

/* check if the attacking card (non-turn holder's arena card) has any substatus that
 * reduces the damage dealt this turn (SUBSTATUS2).
 * check if the defending card (turn holder's arena card) has any substatus that
 * reduces the damage dealt to it this turn (SUBSTATUS1 or Pkmn Powers).
 * damage is given in de as input and the possibly updated damage is also returned. */
u16 HandleDamageReduction(u16 de)
{
	de = HandleDamageReductionExceptSubstatus2(de);
	u8 a = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_SUBSTATUS2);
	if (a == 0)
		return de;
	if (a == SUBSTATUS2_REDUCE_BY_20)
		return (u16)(de - 20);
	if (a == SUBSTATUS2_POUNCE || a == SUBSTATUS2_GROWL)
		return (u16)(de - 10);
	return de;
}

/* `sla d ; rr e` followed by `bit 0, e` / -5, as used by .halve_damage and .kabuto_armor */
static u16 BuggyHalveDamage(u16 de)
{
	u8 d = (u8)(de >> 8), e = (u8)de;
	u8 carry = d >> 7;
	d = (u8)(d << 1); /* bug (original), should be sra d */
	e = (u8)((carry << 7) | (e >> 1));
	de = (u16)(d << 8 | e);
	if (e & 1)
		de = (u16)(de - 5);
	return de;
}

/* HandleDamageReductionExceptSubstatus2.pkmn_power: Invisible Wall and Kabuto Armor */
static u16 HandleDamageReductionExceptSubstatus2_PkmnPower(u16 de)
{
	if (wLoadedAttack.Category == POKEMON_POWER)
		return de;
	u8 a = wTempNonTurnDuelistCardID;
	if (a == MR_MIME) {
		/* invisible_wall */
		if (wLoadedAttack.Category == POKEMON_POWER)
			return de;
		if (de < 30)
			return de;
		return 0;
	}
	if (a == KABUTO)
		return BuggyHalveDamage(de); /* kabuto_armor */
	return de;
}

/* check if the defending card (turn holder's arena card) has any substatus that
 * reduces the damage dealt to it this turn. (SUBSTATUS1 or Pkmn Powers) */
u16 HandleDamageReductionExceptSubstatus2(u16 de)
{
	if (wNoDamageOrEffect)
		return 0;
	u8 a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_SUBSTATUS1);
	if (a != 0) {
		switch (a) {
		case SUBSTATUS1_NO_DAMAGE_STIFFEN:
		case SUBSTATUS1_NO_DAMAGE_WITHDRAW:
		case SUBSTATUS1_NO_DAMAGE_HIDE_IN_SHELL:
		case SUBSTATUS1_NO_DAMAGE_SCRUNCH:
			return 0;
		case SUBSTATUS1_REDUCE_BY_10:
			return (u16)(de - 10);
		case SUBSTATUS1_REDUCE_BY_20:
			return (u16)(de - 20);
		case SUBSTATUS1_PREVENT_LESS_THAN_40:
			return de >= 40 ? de : 0;
		case SUBSTATUS1_HALVE_DAMAGE:
			return BuggyHalveDamage(de);
		}
	}
	/* not_affected_by_substatus1 */
	if (CheckIsIncapableOfUsingPkmnPower_ArenaCard())
		return de;
	return HandleDamageReductionExceptSubstatus2_PkmnPower(de);
}

/* HandleNoDamageOrEffectSubstatus.pkmn_power: Neutralizing Shield */
static bool HandleNoDamageOrEffectSubstatus_PkmnPower(void)
{
	if (wTempNonTurnDuelistCardID != MEW_LV8)
		return false;
	/* neutralizing_shield */
	if (wIsDamageToSelf)
		return false;
	/* prevent damage if attacked by a non-basic Pokemon */
	LoadCardDataToBuffer2_FromCardID(wTempTurnDuelistCardID);
	if (wLoadedCard2.Stage == 0)
		return false;
	wNoDamageOrEffect = NO_DAMAGE_OR_EFFECT_NSHIELD;
	gTextID = NoDamageOrEffectDueToNShieldText;
	return true;
}

/* check for Invisible Wall, Kabuto Armor, NShield, or Transparency, in order to
 * possibly reduce or make zero the damage at de. */
u16 HandleDamageReductionOrNoDamageFromPkmnPowerEffects(u16 de)
{
	if (wLoadedAttack.Category == POKEMON_POWER)
		return de;
	if (CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK))
		return de;
	if (wTempPlayAreaLocation_cceb != 0)
		de = HandleDamageReductionExceptSubstatus2_PkmnPower(de);
	bool c = HandleNoDamageOrEffectSubstatus_PkmnPower();
	if (!c)
		c = HandleTransparency();
	if (!c)
		return de;
	/* if carry was set due to NShield or Transparency, damage is 0 */
	return 0;
}

/* when MACHAMP is damaged, if its Strikes Back is active, the
 * attacking Pokemon (turn holder's arena Pokemon) takes 10 damage.
 * ignore if damage taken at de is 0.
 * used to bounce back a damaging attack. */
void HandleStrikesBack_AgainstDamagingAttack(u16 de)
{
	if (de == 0)
		return;
	if (wIsDamageToSelf)
		return;
	if (wTempNonTurnDuelistCardID != MACHAMP) /* ID of defending Pokemon */
		return;
	if (CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK))
		return;
	if (wLoadedAttack.Category == POKEMON_POWER)
		return;
	if (wTempPlayAreaLocation_cceb == PLAY_AREA_ARENA) {
		if (CheckIsIncapableOfUsingPkmnPower_ArenaCard())
			return;
	}
	/* subtract 10 HP from attacking Pokemon (turn holder's arena Pokemon) */
	SwapTurn();
	LoadCardDataToBuffer2_FromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	u8 *hl = &TurnDuelistVars()[DUELVARS_ARENA_CARD_HP];
	u8 hp_before = *hl;
	SubtractHP(hl, 10);
	wTempNonTurnDuelistCardID = wLoadedCard2.ID;
	LoadTxRam3(10);
	LoadTxRam2(rd16(wLoadedCard2.Name));
	DrawWideTextBox_WaitForInput(ReceivesDamageDueToStrikesBackText);
	if (hp_before != 0)
		PrintPlayAreaCardKnockedOutIfNoHP(PLAY_AREA_ARENA);
	SwapTurn();
}

/* return carry if NShield or Transparency activate (if MEW_LV8 or HAUNTER_LV17 is
 * the turn holder's Pokemon at play area location e), and print their corresponding text if so */
bool HandleNShieldAndTransparency(u8 e)
{
	u8 id = GetCardIDFromDeckIndex(GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + e)));
	if (id == MEW_LV8) {
		if (GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_STAGE) == 0)
			return false;
		wNoDamageOrEffect = NO_DAMAGE_OR_EFFECT_NSHIELD;
		gTextID = NoDamageOrEffectDueToNShieldText;
		DrawWideTextBox_WaitForInput(gTextID);
		return true;
	}
	if (id == HAUNTER_LV17) {
		wDuelDisplayedScreen = 0;
		if (!TossCoin(TransparencyCheckText))
			return false;
		wNoDamageOrEffect = NO_DAMAGE_OR_EFFECT_TRANSPARENCY;
		gTextID = NoDamageOrEffectDueToTransparencyText;
		DrawWideTextBox_WaitForInput(gTextID);
		return true;
	}
	return false;
}

/* return carry if the turn holder's arena Pokemon is under a condition that makes
 * it unable to attack. also return in gTextID (hl) the text id to be displayed */
bool HandleCantAttackSubstatus(void)
{
	u8 a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_SUBSTATUS2);
	if (a == 0)
		return false;
	gTextID = UnableToAttackDueToTailWagText;
	if (a == SUBSTATUS2_TAIL_WAG)
		return true;
	gTextID = UnableToAttackDueToLeerText;
	if (a == SUBSTATUS2_LEER)
		return true;
	gTextID = UnableToAttackDueToBoneAttackText;
	if (a == SUBSTATUS2_BONE_ATTACK)
		return true;
	return false;
}

/* return carry if the turn holder's arena Pokemon cannot use
 * selected attack at wSelectedAttack due to amnesia */
bool HandleAmnesiaSubstatus(void)
{
	u8 a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_SUBSTATUS2);
	if (a == 0 || a != SUBSTATUS2_AMNESIA)
		return false;
	if (wSelectedAttack != GetTurnDuelistVariable(DUELVARS_ARENA_CARD_DISABLED_ATTACK_INDEX))
		return false;
	gTextID = UnableToUseAttackDueToAmnesiaText;
	return true;
}

/* CheckSandAttackOrSmokescreenSubstatus, also returning the coin toss text (register de) */
static bool CheckSandAttackOrSmokescreenSubstatus_Text(u16 *de)
{
	u8 a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_SUBSTATUS2);
	if (a == 0)
		return false;
	if (a == SUBSTATUS2_SAND_ATTACK)
		*de = SandAttackCheckText;
	else if (a == SUBSTATUS2_SMOKESCREEN)
		*de = SmokescreenCheckText;
	else
		return false;
	if (wGotHeadsFromSandAttackOrSmokescreenCheck)
		return false; /* got heads */
	return true; /* got tails */
}

/* return carry if the turn holder's attack was unsuccessful due to sand attack or smokescreen effect */
bool HandleSandAttackOrSmokescreenSubstatus(void)
{
	u16 de = 0;
	if (!CheckSandAttackOrSmokescreenSubstatus_Text(&de))
		return false;
	bool heads = TossCoin(de);
	wGotHeadsFromSandAttackOrSmokescreenCheck = heads ? 1 : 0;
	if (heads)
		return false;
	gTextID = AttackUnsuccessfulText;
	DrawWideTextBox_WaitForInput(AttackUnsuccessfulText);
	return true;
}

/* return carry if the turn holder's arena card is under
 * the effects of sand attack or smokescreen and got tails on the coin toss */
bool CheckSandAttackOrSmokescreenSubstatus(void)
{
	u16 de;
	return CheckSandAttackOrSmokescreenSubstatus_Text(&de);
}

/* return carry if the defending card (turn holder's arena card) is under a substatus
 * that prevents any damage or effect dealt to it for a turn.
 * also return the cause of the substatus in wNoDamageOrEffect (and its text in gTextID) */
bool HandleNoDamageOrEffectSubstatus(void)
{
	wNoDamageOrEffect = 0;
	if (wLoadedAttack.Category == POKEMON_POWER)
		return false;
	u8 a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_SUBSTATUS1);
	u8 e;
	if (a == SUBSTATUS1_FLY) {
		e = NO_DAMAGE_OR_EFFECT_FLY;
		gTextID = NoDamageOrEffectDueToFlyText;
	} else if (a == SUBSTATUS1_BARRIER) {
		e = NO_DAMAGE_OR_EFFECT_BARRIER;
		gTextID = NoDamageOrEffectDueToBarrierText;
	} else if (a == SUBSTATUS1_AGILITY) {
		e = NO_DAMAGE_OR_EFFECT_AGILITY;
		gTextID = NoDamageOrEffectDueToAgilityText;
	} else {
		if (CheckIsIncapableOfUsingPkmnPower_ArenaCard())
			return false;
		return HandleNoDamageOrEffectSubstatus_PkmnPower();
	}
	wNoDamageOrEffect = e;
	return true;
}

/* if the Pokemon being attacked is HAUNTER_LV17, and its Transparency is active,
 * there is a 50% chance that any damage or effect is prevented
 * return carry if damage is prevented */
bool HandleTransparency(void)
{
	if (wTempNonTurnDuelistCardID != HAUNTER_LV17)
		return false;
	if (wLoadedAttack.Category == POKEMON_POWER)
		return false; /* Transparency has no effect against Pkmn Powers */
	if (CheckIsIncapableOfUsingPkmnPower(wTempPlayAreaLocation_cceb))
		return false;
	wDuelDisplayedScreen = 0;
	if (!TossCoin(TransparencyCheckText))
		return false;
	wNoDamageOrEffect = NO_DAMAGE_OR_EFFECT_TRANSPARENCY;
	gTextID = NoDamageOrEffectDueToTransparencyText;
	return true;
}

const u16 NoDamageOrEffectTextIDTable[] = {
	NoDamageOrEffectDueToAgilityText,      /* NO_DAMAGE_OR_EFFECT_AGILITY */
	NoDamageOrEffectDueToBarrierText,      /* NO_DAMAGE_OR_EFFECT_BARRIER */
	NoDamageOrEffectDueToFlyText,          /* NO_DAMAGE_OR_EFFECT_FLY */
	NoDamageOrEffectDueToTransparencyText, /* NO_DAMAGE_OR_EFFECT_TRANSPARENCY */
	NoDamageOrEffectDueToNShieldText,      /* NO_DAMAGE_OR_EFFECT_NSHIELD */
};

/* return carry and return the appropriate text id in gTextID (hl) if the target has an
 * special status or power that prevents any damage or effect done to it this turn */
bool CheckNoDamageOrEffect(void)
{
	u8 a = wNoDamageOrEffect;
	if (a == 0)
		return false;
	if (a & 0x80) {
		gTextID = 0; /* already been here so don't repeat the text */
		return true;
	}
	wNoDamageOrEffect |= 0x80;
	gTextID = NoDamageOrEffectTextIDTable[(u8)(a - 1)];
	return true;
}

/* return carry if turn holder has Omanyte and its Clairvoyance Pkmn Power is active */
bool IsClairvoyanceActive(void)
{
	if (CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK))
		return false;
	return CountTurnDuelistPokemonWithActivePkmnPower(OMANYTE) != 0;
}

/* returns carry if turn holder's arena card is paralyzed, asleep, confused,
 * and/or toxic gas in play (i.e. its pkmn power cannot be used) */
bool CheckIsIncapableOfUsingPkmnPower_ArenaCard(void)
{
	return CheckIsIncapableOfUsingPkmnPower(PLAY_AREA_ARENA);
}

/* returns carry if Pokemon in turn holder's Play Area location loc
 * cannot use its Pkmn Power (text in gTextID) */
bool CheckIsIncapableOfUsingPkmnPower(u8 loc)
{
	if (loc == 0) {
		if (GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS) & CNF_SLP_PRZ) {
			gTextID = CannotUseDueToStatusText;
			return true;
		}
	}
	bool c = CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK) != 0;
	gTextID = UnableDueToToxicGasText;
	return c;
}

/* return the amount of times that the Pokemon card with a given ID is found in the
 * play area of both duelists (carry if at least once).
 * if the arena Pokemon is asleep, confused, or paralyzed (Pkmn Power-incapable), it doesn't count. */
u8 CountPokemonWithActivePkmnPowerInBothPlayAreas(u8 id)
{
	wTempPokemonID_ce7c = id;
	u8 c = CountTurnDuelistPokemonWithActivePkmnPower(id);
	SwapTurn();
	u8 a = CountTurnDuelistPokemonWithActivePkmnPower(wTempPokemonID_ce7c);
	SwapTurn();
	return (u8)(a + c);
}

/* return the amount of times that the Pokemon card with a given ID is found in the
 * turn holder's play area (carry if at least once).
 * if the arena Pokemon is asleep, confused, or paralyzed (Pkmn Power-incapable), it doesn't count. */
u8 CountTurnDuelistPokemonWithActivePkmnPower(u8 id)
{
	wTempPokemonID_ce7c = id;
	u8 c = 0;
	u8 *vars = TurnDuelistVars();
	u8 a = vars[DUELVARS_ARENA_CARD];
	if (a != 0xff && GetCardIDFromDeckIndex(a) == wTempPokemonID_ce7c &&
	    !(vars[DUELVARS_ARENA_CARD_STATUS] & CNF_SLP_PRZ))
		c++;
	for (u8 i = 0;; i++) {
		a = vars[(u8)(DUELVARS_BENCH + i)];
		if (a == 0xff)
			break;
		if (GetCardIDFromDeckIndex(a) == wTempPokemonID_ce7c)
			c++;
	}
	return c;
}

/* return the retreat cost of the card in wLoadedCard1,
 * adjusting for any Dodrio's Retreat Aid Pkmn Power that is active. */
u8 GetLoadedCard1RetreatCost(void)
{
	u8 c = 0;
	u8 *vars = TurnDuelistVars();
	for (u8 i = 0;; i++) {
		u8 a = vars[(u8)(DUELVARS_BENCH + i)];
		if (a == 0xff)
			break;
		if (GetCardIDFromDeckIndex(a) == DODRIO)
			c++;
	}
	if (c == 0)
		return wLoadedCard1.RetreatCost; /* return regular retreat cost */
	if (CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK))
		return wLoadedCard1.RetreatCost;
	u8 a = wLoadedCard1.RetreatCost;
	/* apply Retreat Aid for each Pkmn Power-capable Dodrio */
	return a >= c ? (u8)(a - c) : 0;
}

/* return carry if the turn holder's arena Pokemon is affected by Acid and can't retreat */
bool CheckUnableToRetreatDueToEffect(void)
{
	u8 a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_SUBSTATUS2);
	if (a != SUBSTATUS2_ACID)
		return false;
	gTextID = UnableToRetreatDueToAcidText;
	return true;
}

/* return carry if the turn holder is affected by Headache and trainer cards can't be used */
bool CheckCantUseTrainerDueToEffect(void)
{
	if (!(GetTurnDuelistVariable(DUELVARS_ARENA_CARD_SUBSTATUS3) & (1 << SUBSTATUS3_HEADACHE_F)))
		return false;
	gTextID = UnableToUseTrainerDueToHeadacheText;
	return true;
}

/* return carry if any duelist has Aerodactyl and its Prehistoric Power Pkmn Power is active */
bool IsPrehistoricPowerActive(void)
{
	if (!CountPokemonWithActivePkmnPowerInBothPlayAreas(AERODACTYL))
		return false;
	bool muk = CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK) != 0;
	gTextID = UnableToEvolveDueToPrehistoricPowerText;
	return !muk;
}

/* clears some SUBSTATUS2 conditions from the turn holder's active Pokemon.
 * more specifically, those conditions that reduce the damage from an attack
 * or prevent the opposing Pokemon from attacking the substatus condition inducer. */
void ClearDamageReductionSubstatus2(void)
{
	u8 *p = &TurnDuelistVars()[DUELVARS_ARENA_CARD_SUBSTATUS2];
	switch (*p) {
	case SUBSTATUS2_REDUCE_BY_20:
	case SUBSTATUS2_POUNCE:
	case SUBSTATUS2_GROWL:
	case SUBSTATUS2_TAIL_WAG:
	case SUBSTATUS2_LEER:
		*p = 0;
	}
}

/* clears the SUBSTATUS1 and updates the double damage condition of the player about to start his turn */
void UpdateSubstatusConditions_StartOfTurn(void)
{
	u8 *vars = TurnDuelistVars();
	u8 a = vars[DUELVARS_ARENA_CARD_SUBSTATUS1];
	vars[DUELVARS_ARENA_CARD_SUBSTATUS1] = 0;
	if (a != SUBSTATUS1_NEXT_TURN_DOUBLE_DAMAGE)
		return;
	vars[DUELVARS_ARENA_CARD_SUBSTATUS3] |= 1 << SUBSTATUS3_THIS_TURN_DOUBLE_DAMAGE_F;
}

/* clears the SUBSTATUS2, Headache, and updates the double damage condition of the player ending his turn */
void UpdateSubstatusConditions_EndOfTurn(void)
{
	u8 *vars = TurnDuelistVars();
	vars[DUELVARS_ARENA_CARD_SUBSTATUS3] &= ~(1 << SUBSTATUS3_HEADACHE_F);
	vars[DUELVARS_ARENA_CARD_SUBSTATUS2] = 0;
	if (vars[DUELVARS_ARENA_CARD_SUBSTATUS1] == SUBSTATUS1_NEXT_TURN_DOUBLE_DAMAGE)
		return;
	vars[DUELVARS_ARENA_CARD_SUBSTATUS3] &= ~(1 << SUBSTATUS3_THIS_TURN_DOUBLE_DAMAGE_F);
}

/* return carry if turn holder has Blastoise and its Rain Dance Pkmn Power is active */
bool IsRainDanceActive(void)
{
	if (!CountTurnDuelistPokemonWithActivePkmnPower(BLASTOISE))
		return false; /* no Pkmn Power-capable Blastoise found in turn holder's play area */
	return !CountPokemonWithActivePkmnPowerInBothPlayAreas(MUK);
}

/* return carry if card at [hTempCardIndex_ff98] is a water energy card AND
 * if card at [hTempPlayAreaLocation_ff9d] is a water Pokemon card. */
bool CheckRainDanceScenario(void)
{
	if (GetCardType(GetCardIDFromDeckIndex(hTempCardIndex_ff98)) != TYPE_ENERGY_WATER)
		return false;
	if (GetPlayAreaCardColor(hTempPlayAreaLocation_ff9d) != TYPE_PKMN_WATER)
		return false;
	return true;
}

/* if the defending (non-turn) card's HP is 0 and the attacking (turn) card's HP
 * is not, the attacking card faints if it was affected by destiny bond */
void HandleDestinyBondSubstatus(void)
{
	if (GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_SUBSTATUS1) != SUBSTATUS1_DESTINY_BOND)
		return;
	if (GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD) == 0xff)
		return;
	if (GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_HP) != 0)
		return;
	u8 *vars = TurnDuelistVars();
	if (vars[DUELVARS_ARENA_CARD_HP] == 0)
		return;
	vars[DUELVARS_ARENA_CARD_HP] = 0;
	DrawDuelMainScene();
	DrawDuelHUDs();
	LoadCardDataToBuffer2_FromDeckIndex(vars[DUELVARS_ARENA_CARD]);
	LoadTxRam2(rd16(wLoadedCard2.Name));
	DrawWideTextBox_WaitForInput(KnockedOutDueToDestinyBondText);
}

/* when MACHAMP is damaged, if its Strikes Back is active, the
 * attacking Pokemon (turn holder's arena Pokemon) takes 10 damage.
 * used to bounce back an attack of the RESIDUAL category */
void HandleStrikesBack_AgainstResidualAttack(void)
{
	if (wTempNonTurnDuelistCardID != MACHAMP)
		return;
	if (wLoadedAttack.Category & RESIDUAL)
		return;
	if (wDealtDamage[0] == 0)
		return;
	SwapTurn();
	bool c = CheckIsIncapableOfUsingPkmnPower_ArenaCard();
	SwapTurn();
	if (c)
		return;
	if (!ApplyStrikesBack_AgainstResidualAttack(10)) /* damage to be dealt to attacker */
		WaitForWideTextBoxInput();
}

/* returns carry if the attacker had HP left before the Strikes Back damage */
bool ApplyStrikesBack_AgainstResidualAttack(u16 damage)
{
	LoadTxRam3(damage);
	LoadCardDataToBuffer2_FromCardID(wTempTurnDuelistCardID);
	LoadTxRam2(rd16(wLoadedCard2.Name));
	u8 *hl = &TurnDuelistVars()[DUELVARS_ARENA_CARD_HP];
	u8 hp_before = *hl;
	SubtractHP(hl, damage);
	DrawWideTextBox_PrintText(ReceivesDamageDueToStrikesBackText);
	if (hp_before == 0)
		return false;
	WaitForWideTextBoxInput();
	PrintPlayAreaCardKnockedOutIfNoHP(PLAY_AREA_ARENA);
	DrawDuelHUDs();
	return true;
}

static void ClearChangedTypesIfMuk_ZeroChangedTypes(void)
{
	u8 *vars = TurnDuelistVars();
	for (u8 i = 0; i < MAX_PLAY_AREA_POKEMON; i++)
		vars[(u8)(DUELVARS_ARENA_CARD_CHANGED_TYPE + i)] = 0;
}

/* if the id of the card provided as a deck index is MUK,
 * clear the changed type of all arena and bench Pokemon */
void ClearChangedTypesIfMuk(u8 card)
{
	if (GetCardIDFromDeckIndex(card) != MUK)
		return;
	SwapTurn();
	ClearChangedTypesIfMuk_ZeroChangedTypes();
	SwapTurn();
	ClearChangedTypesIfMuk_ZeroChangedTypes();
}
