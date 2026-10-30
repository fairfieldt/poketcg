/*
 * Port of src/engine/duel/effect_functions.asm, part 1 of 4: from the top of
 * the file up to (not including) OmastarWaterGunEffect.
 *
 * Effect functions return the carry flag the original returns. When that carry
 * is left over from a routine whose C version does not expose it (UI shims,
 * void helpers such as DealRecoilDamageToSelf or ApplyAndAnimateHPRecovery),
 * the port returns false; this only happens for BEFORE_DAMAGE, AFTER_DAMAGE,
 * AI and AI selection commands, whose carry no caller reads. Effect functions
 * are always entered with the carry clear (TryExecuteEffectCommandFunction
 * does `or a` before calling them), which matters for routines that restore
 * their caller's flags with `pop af`.
 */
#include "effects.h"
#include "../tcg.h"
#include "part1_needs.h"

/* constants from src/constants/hardware.inc and charmaps.asm that gen/ does not export */
#ifndef PAD_B
#define PAD_B 0x02
#endif
#ifndef PAD_START
#define PAD_START 0x08
#endif
#ifndef SYM_SPACE
#define SYM_SPACE 0x00
#endif
#ifndef SYM_GRASS
#define SYM_GRASS 0x02
#endif
#ifndef SYM_CURSOR_R
#define SYM_CURSOR_R 0x0f
#endif

/* non-static helpers of part 1 that other parts call but effects.h does not
 * declare (status condition effects not referenced by any effect command list,
 * and menu parameter tables) */
bool DoublePoisonEffect(void);
bool ParalysisEffect(void);
bool ConfusionEffect(void);
bool Sleep50PercentEffect(void);
extern const struct MenuParams PlayAreaSelectionMenuParameters;
extern const struct MenuParams BenchSelectionMenuParameters;

/* unreferenced in the original (non-static, like home_duel.c's Func_17ed) */
void CommentedOut_2c086(void);
u8 Serial_TossZeroCoins(u16 text);
u8 Func_2c0a8(void);
void Func_2c6d9(void);

/* defined later, in the asm order */
static const u8 ShiftListItemToColor[NUM_COLORED_TYPES];
static const u16 ColorToTextSymbol[NUM_COLORED_TYPES];

bool Poison50PercentEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(PoisonCheckText))
		return false;
	return PoisonEffect();
}

bool PoisonEffect(void)
{
	TRACE_FN();
	return QueueStatusCondition(CNF_SLP_PRZ, POISONED);
}

bool DoublePoisonEffect(void)
{
	return QueueStatusCondition(CNF_SLP_PRZ, DOUBLE_POISONED);
}

bool Paralysis50PercentEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(ParalysisCheckText))
		return false;
	return ParalysisEffect();
}

bool ParalysisEffect(void)
{
	return QueueStatusCondition(PSN_DBLPSN, PARALYZED);
}

bool Confusion50PercentEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(ConfusionCheckText))
		return false;
	return ConfusionEffect();
}

bool ConfusionEffect(void)
{
	return QueueStatusCondition(PSN_DBLPSN, CONFUSED);
}

bool Sleep50PercentEffect(void)
{
	if (!TossCoin_BankB(SleepCheckText))
		return false;
	return SleepEffect();
}

bool SleepEffect(void)
{
	TRACE_FN();
	return QueueStatusCondition(PSN_DBLPSN, ASLEEP);
}

/* b = mask of status conditions not to discard on the target, c = status condition
 * to inflict. returns carry if the status condition was queued */
bool QueueStatusCondition(u8 b, u8 c)
{
	if (hWhoseTurn == wWhoseTurn) {
		u8 a = wTempNonTurnDuelistCardID;
		if (a == CLEFAIRY_DOLL || a == MYSTERIOUS_FOSSIL)
			goto cant_induce_status;
		/* Snorlax's Thick Skinned prevents it from being statused... */
		if (a == SNORLAX) {
			SwapTurn();
			/* ...unless already so, or if affected by Muk's Toxic Gas */
			bool incapable = CheckIsIncapableOfUsingPkmnPower(PLAY_AREA_ARENA);
			SwapTurn();
			if (!incapable)
				goto cant_induce_status;
		}
	}
	/* .can_induce_status */
	{
		u8 *hl = &wStatusConditionQueue[wStatusConditionQueueIndex];
		SwapTurn();
		hl[0] = hWhoseTurn;
		SwapTurn();
		hl[1] = b; /* mask of status conditions not to discard on the target */
		hl[2] = c; /* status condition to inflict to the target */
		/* advance wStatusConditionQueueIndex */
		wStatusConditionQueueIndex += 3;
		return true;
	}

cant_induce_status:
	wNoEffectFromWhichStatus = c;
	SetNoEffectFromStatus();
	return false;
}

bool TossCoin_BankB(u16 text)
{
	return TossCoin(text);
}

u8 TossCoinATimes_BankB(u8 n, u16 text)
{
	return TossCoinATimes(n, text);
}

/* unreferenced */
void CommentedOut_2c086(void)
{
}

/* unreferenced */
u8 Serial_TossZeroCoins(u16 text)
{
	return Serial_TossCoinATimes(0, text);
}

bool Serial_TossCoin(u16 text)
{
	return Serial_TossCoinATimes(1, text) != 0;
}

/* input:
 * - n (a) = number of coin flips
 * - text (de) = text ID
 * output:
 * - number of heads (a), carry if nonzero */
u8 Serial_TossCoinATimes(u8 n, u16 text)
{
	SetOppAction_SerialSendDuelData(OPPACTION_TOSS_COIN_A_TIMES);
	SerialSend8Bytes(); /* sends registers f, a (= n), l, h, e, d (= text), c, b */
	return TossCoinATimes(n, text);
}

void SetNoEffectFromStatus(void)
{
	wEffectFailed = EFFECT_FAILED_NO_EFFECT;
}

void SetWasUnsuccessful(void)
{
	wEffectFailed = EFFECT_FAILED_UNSUCCESSFUL;
}

/* unreferenced */
u8 Func_2c0a8(void)
{
	u8 saved = hTemp_ffa0;
	hTemp_ffa0 = hWhoseTurn;
	SetOppAction_SerialSendDuelData(OPPACTION_6B30);
	PlayDeckShuffleAnimation();
	u8 c = 1; /* PlayDeckShuffleAnimation always returns a = $01 */
	hTemp_ffa0 = saved;
	return c;
}

void ShuffleCardsInDeck(void)
{
	ExchangeRNG();
	PlayDeckShuffleAnimation();
	ShuffleDeck();
}

/* return carry if Player is the Turn Duelist */
bool IsPlayerTurn(void)
{
	return GetTurnDuelistVariable(DUELVARS_DUELIST_TYPE) == DUELIST_TYPE_PLAYER;
}

/* UpdateExpectedAIDamage.skip_push_af, also returning the carry of its final `add [hl]` */
static bool UpdateExpectedAIDamage_SkipPushAF(u8 a, u8 d, u8 e)
{
	u8 *hl = wDamage;
	wAIMinDamage = (u8)(hl[0] + d);
	wAIMaxDamage = (u8)(hl[0] + e);
	bool carry = a + hl[0] > 0xff;
	hl[0] = (u8)(a + hl[0]); /* (only the low byte of wDamage) */
	return carry;
}

/* UpdateExpectedAIDamage_AccountForPoison, also returning its carry */
static bool UpdateExpectedAIDamage_AccountForPoison_Carry(u8 a, u8 d, u8 e)
{
	if (!(GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS) & (POISONED | DOUBLE_POISONED)))
		return UpdateExpectedAIDamage_SkipPushAF(a, d, e);
	wAIMinDamage = wDamage[0];
	wAIMaxDamage = wDamage[0];
	/* pop af restores the flags of the caller: effect functions are entered with nc */
	return false;
}

/* Stores information about the attack damage for AI purposes
 * taking into account poison damage between turns.
 * if target poisoned
 *	[wAIMinDamage] <- [wDamage]
 *	[wAIMaxDamage] <- [wDamage]
 * else
 *	[wAIMinDamage] <- [wDamage] + d
 *	[wAIMaxDamage] <- [wDamage] + e
 *	[wDamage]      <- [wDamage] + a */
void UpdateExpectedAIDamage_AccountForPoison(u8 a, u8 d, u8 e)
{
	UpdateExpectedAIDamage_AccountForPoison_Carry(a, d, e);
}

/* Sets some variables for AI use
 *	[wAIMinDamage] <- [wDamage] + d
 *	[wAIMaxDamage] <- [wDamage] + e
 *	[wDamage]      <- [wDamage] + a */
void UpdateExpectedAIDamage(u8 a, u8 d, u8 e)
{
	UpdateExpectedAIDamage_SkipPushAF(a, d, e);
}

/* Stores information about the attack damage for AI purposes
 * [wDamage]      <- a (average amount of damage)
 * [wAIMinDamage] <- d (minimum)
 * [wAIMaxDamage] <- e (maximum) */
void SetExpectedAIDamage(u8 a, u8 d, u8 e)
{
	wDamage[0] = a;
	wDamage[1] = 0;
	wAIMinDamage = d;
	wAIMaxDamage = e;
}

void DrawPlayAreaScreenToShowChanges(u8 loc)
{
	hTempPlayAreaLocation_ff9d = loc;
	SetupPlayAreaScreen();
	PrintPlayAreaCardList_EnableLCD();
	InitAndPrintPlayAreaCardInformationAndLocation_WithTextBox();
}

/* deal damage to all the turn holder's benched Pokemon
 * input: damage (a) = amount of damage to deal to each Pokemon */
void DealDamageToAllBenchedPokemon(u8 damage)
{
	u16 de = damage;
	u8 c = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	u8 b = PLAY_AREA_ARENA;
	/* jr .skip_to_bench */
	for (;;) {
		b++;
		if (--c == 0)
			break;
		DealDamageToPlayAreaPokemon_RegularAnim(b, de);
	}
}

/* plays animation given in register a (ATK_ANIM_* constant)
 * on top of the Pokémon that is attacking */
void PlayAttackAnimationOverAttackingPokemon(u8 anim)
{
	wLoadedAttackAnimation = anim;
	/* the original passes on whatever its caller left in de (the damage number
	 * that PlayAttackAnimation stores in wDamageAnimAmount); the shared signature
	 * does not carry it, so 0 is used */
	PlayAttackAnimation(0, hTempPlayAreaLocation_ff9d,
		0 /* neither WEAKNESS nor RESISTANCE */, hWhoseTurn);
	WaitAttackAnimation();
}

/* apply a status condition of type 1 identified by register a to the target */
void ApplySubstatus1ToAttackingCard(u8 substatus)
{
	TurnDuelistVars()[DUELVARS_ARENA_CARD_SUBSTATUS1] = substatus;
}

/* apply a status condition of type 2 identified by register a to the target,
 * unless prevented by wNoDamageOrEffect */
void ApplySubstatus2ToDefendingCard(u8 substatus)
{
	if (CheckNoDamageOrEffect()) {
		/* .no_damage_or_effect */
		u16 hl = gTextID;
		DrawDuelMainScene();
		if (hl)
			DrawWideTextBox_PrintText(hl);
		return;
	}
	u8 *vars = NonTurnDuelistVars();
	vars[DUELVARS_ARENA_CARD_SUBSTATUS2] = substatus;
	vars[DUELVARS_ARENA_CARD_LAST_TURN_SUBSTATUS2] = substatus;
}

/* overwrites in wDamage, wAIMinDamage and wAIMaxDamage
 * with the value in a. */
void SetDefiniteDamage(u8 damage)
{
	wDamage[0] = damage;
	wAIMinDamage = damage;
	wAIMaxDamage = damage;
	wDamage[1] = 0;
}

/* overwrites wAIMinDamage and wAIMaxDamage
 * with value in wDamage. */
void SetDefiniteAIDamage(void)
{
	wAIMinDamage = wDamage[0];
	wAIMaxDamage = wDamage[0];
}

/* returns in a some random occupied Play Area location
 * in Turn Duelist's Play Area. */
u8 PickRandomPlayAreaCard(void)
{
	return Random(GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA));
}

/* outputs in hl the next position
 * in hTempList to place a new card,
 * and increments hCurSelectionItem. */
u8 *GetNextPositionInTempList(void)
{
	u8 a = hCurSelectionItem++;
	return &hTempList[a];
}

/* creates in wDuelTempList list of attached Fire Energy cards
 * that are attached to the Turn Duelist's Arena card. */
u8 CreateListOfFireEnergyAttachedToArena(void)
{
	return CreateListOfEnergyAttachedToArena(TYPE_ENERGY_FIRE);
}

/* creates in wDuelTempList a list of cards that
 * are in the Arena of the same type as input a.
 * this is called to list Energy cards of a specific type
 * that are attached to the Arena Pokemon.
 * input:
 *	type (a) = TYPE_ENERGY_* constant
 * output:
 *	a = number of cards in list;
 *	wDuelTempList filled with cards, terminated by $ff */
u8 CreateListOfEnergyAttachedToArena(u8 type)
{
	u8 b = type, c = 0;
	u8 *de = wDuelTempList;
	u8 *vars = TurnDuelistVars();
	u8 l = DUELVARS_CARD_LOCATIONS;
	do {
		if (vars[l] == CARD_LOCATION_ARENA) {
			if (GetCardType(GetCardIDFromDeckIndex(l)) == b) { /* is same as input type? */
				*de++ = l;
				c++;
			}
		}
		l++;
	} while (l < DECK_SIZE);
	*de = 0xff;
	return c;
}

/* prints the text "<X> devolved to <Y>!" with
 * the proper card names and levels.
 * input:
 *	d = deck index of the lower stage card
 *	e = deck index of card that was devolved */
void PrintDevolvedCardNameAndLevelText(u8 d, u8 e)
{
	LoadCardDataToBuffer1_FromDeckIndex(e);
	wTxRam2[0] = wLoadedCard1.Name[0];
	wTxRam2[1] = wLoadedCard1.Name[1];
	/* wTxRam2_b */
	wTxRam2_b[0] = 0;
	wTxRam2_b[1] = 0;
	LoadCardDataToBuffer1_FromDeckIndex(d);
	CopyCardNameAndLevel(18);
	/* (ld [hl], $00 terminates wDefaultText: done by the CopyCardNameAndLevel shim) */
	DrawWideTextBox_WaitForInput(PokemonDevolvedToText);
}

/* input:
 * - loc (a) = PLAY_AREA_* constant of Bench Pokémon to switch to */
void HandleSwitchDefendingPokemonEffect(u8 loc)
{
	u8 e = loc;
	if (e == 0xff)
		return; /* none selected */

	/* check Defending Pokemon's HP */
	if (GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_HP) == 0) {
		/* if 0, handle Destiny Bond first */
		HandleDestinyBondSubstatus();
	}

	/* .switch */
	if (HandleNoDamageOrEffect())
		return; /* no effect */

	/* attack was successful, switch Defending Pokemon */
	SwapTurn();
	SwapArenaWithBenchPokemon(e);
	SwapTurn();

	wUnused_DefendingPkmnStatus = 0;
	wDuelDisplayedScreen = 0;
	wDefendingWasForcedToSwitch = 1;
}

/* returns carry if Defending has No Damage or Effect
 * if so, print its appropriate text. */
bool HandleNoDamageOrEffect(void)
{
	if (!CheckNoDamageOrEffect())
		return false;
	if (gTextID)
		DrawWideTextBox_PrintText(gTextID);
	return true;
}

/* applies HP recovery on Pokemon after an attack
 * with HP recovery effect, and handles its animation.
 * input:
 *	hp (de) = HP amount to recover */
void ApplyAndAnimateHPRecovery(u16 hp)
{
	u16 de = hp;
	wr16(wUnused_HPRecoverAmount, de);

	/* get Arena card's damage */
	if (GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL) == 0)
		return; /* return if no damage */

	/* load correct animation */
	wLoadedAttackAnimation = ATK_ANIM_HEAL;
	/* bug (original), h needs to be set to hWhoseTurn: it still holds the high byte
	 * of wUnused_HPRecoverAmount + 1 */
	PlayAttackAnimation(de, PLAY_AREA_ARENA, 1 /* arrow */,
		(u8)((ADDR_wUnused_HPRecoverAmount + 1) >> 8));

	/* compare HP to be restored with max HP
	 * if HP to be restored would cause HP to
	 * be larger than max HP, cap it accordingly */
	u8 c;
	GetCardDamageAndMaxHP(PLAY_AREA_ARENA, &c);
	u16 bc = c;
	u8 *hl = &TurnDuelistVars()[DUELVARS_ARENA_CARD_HP];
	de = (u16)(de + *hl);
	/* de = damage dealt + current HP
	 * bc = max HP of card */
	if (!CompareDEtoBC(de, bc)) {
		/* cap de to value in bc */
		de = bc;
	}
	/* .skip_cap */
	*hl = (u8)de; /* apply new HP to arena card */
	WaitAttackAnimation();
}

/* returns carry if Play Area has no damage counters. */
bool CheckIfPlayAreaHasAnyDamage(void)
{
	u8 d = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	u8 e = PLAY_AREA_ARENA;
	do {
		if (GetCardDamageAndMaxHP(e, NULL))
			return false; /* found damage */
		e++;
	} while (--d);
	/* no damage found */
	return true;
}

/* makes a list in wDuelTempList with the deck indices
 * of Trainer cards found in Turn Duelist's Discard Pile.
 * returns carry set if no Trainer cards found, and loads
 * corresponding text to notify this. */
bool CreateTrainerCardListFromDiscardPile(void)
{
	/* get number of cards in Discard Pile
	 * and have hl point to the end of the
	 * Discard Pile list in wOpponentDeckCards. */
	u8 *vars = TurnDuelistVars();
	u8 b = vars[DUELVARS_NUMBER_OF_CARDS_IN_DISCARD_PILE];
	u8 l = (u8)(b + DUELVARS_DECK_CARDS);
	u8 *de = wDuelTempList;
	b++;
	/* jr .next_card */
	for (;;) {
		l--;
		if (--b == 0)
			break;
		/* .check_trainer */
		LoadCardDataToBuffer2_FromDeckIndex(vars[l]);
		if (wLoadedCard2.Type != TYPE_TRAINER)
			continue;
		*de++ = vars[l];
	}

	*de = 0xff; /* terminating byte */
	if (wDuelTempList[0] != 0xff)
		return false;
	/* .no_trainers */
	gTextID = ThereAreNoTrainerCardsInDiscardPileText;
	return true;
}

static bool CreateEnergyCardListFromDiscardPile(u8 c);

/* makes a list in wDuelTempList with the deck indices
 * of all basic energy cards found in Turn Duelist's Discard Pile. */
bool CreateEnergyCardListFromDiscardPile_OnlyBasic(void)
{
	return CreateEnergyCardListFromDiscardPile(1);
}

/* makes a list in wDuelTempList with the deck indices
 * of all energy cards (including Double Colorless)
 * found in Turn Duelist's Discard Pile. */
bool CreateEnergyCardListFromDiscardPile_AllEnergy(void)
{
	return CreateEnergyCardListFromDiscardPile(0);
}

/* makes a list in wDuelTempList with the deck indices
 * of energy cards found in Turn Duelist's Discard Pile.
 * if (c == 0), all energy cards are allowed;
 * if (c != 0), double colorless energy cards are not included.
 * returns carry if no energy cards were found. */
static bool CreateEnergyCardListFromDiscardPile(u8 c)
{
	/* get number of cards in Discard Pile
	 * and have hl point to the end of the
	 * Discard Pile list in wOpponentDeckCards. */
	u8 *vars = TurnDuelistVars();
	u8 b = vars[DUELVARS_NUMBER_OF_CARDS_IN_DISCARD_PILE];
	u8 l = (u8)(b + DUELVARS_DECK_CARDS);
	u8 *de = wDuelTempList;
	b++;
	/* jr .next_card */
	for (;;) {
		/* goes through Discard Pile list
		 * in wOpponentDeckCards in descending order. */
		l--;
		if (--b == 0)
			break;
		/* .check_energy */
		LoadCardDataToBuffer2_FromDeckIndex(vars[l]);
		if (!(wLoadedCard2.Type & TYPE_ENERGY))
			continue;
		/* if (c != $00), then we dismiss Double Colorless
		 * energy cards found. */
		if (c != 0 && wLoadedCard2.Type >= TYPE_ENERGY_DOUBLE_COLORLESS)
			continue;
		/* .copy */
		*de++ = vars[l];
	}

	/* terminating byte on wDuelTempList */
	*de = 0xff;

	/* check if any energy card was found
	 * by checking whether the first byte
	 * in wDuelTempList is $ff.
	 * if none were found, return carry. */
	return wDuelTempList[0] == 0xff;
}

/* returns carry if Deck is empty */
bool CheckIfDeckIsEmpty(void)
{
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK);
	gTextID = NoCardsLeftInTheDeckText;
	return a >= DECK_SIZE;
}

/* returns carry if no card with
 * same card ID as e is found in Deck */
static bool LookForCardsInDeck_SearchDeckForCardID(u8 e)
{
	const u8 *hl = wDuelTempList;
	for (;;) {
		u8 a = *hl++;
		if (a == 0xff)
			return true;
		if (GetCardIDFromDeckIndex(a) == e)
			return false;
	}
}

/* returns carry if no NidoranM or NidoranF card is found in Deck */
static bool LookForCardsInDeck_SearchDeckForNidoran(void)
{
	const u8 *hl = wDuelTempList;
	for (;;) {
		u8 a = *hl++;
		if (a == 0xff)
			return true;
		a = GetCardIDFromDeckIndex(a);
		if (a == NIDORANF || a == NIDORANM)
			return false; /* .found_nidoran */
	}
}

/* returns carry if no Basic Fighting Pokemon is found in Deck */
static bool LookForCardsInDeck_SearchDeckForBasicFighting(void)
{
	const u8 *hl = wDuelTempList;
	for (;;) {
		u8 a = *hl++;
		if (a == 0xff)
			return true;
		LoadCardDataToBuffer2_FromDeckIndex(a);
		if (wLoadedCard2.Type != TYPE_PKMN_FIGHTING)
			continue;
		if (wLoadedCard2.Stage != BASIC)
			continue;
		return false;
	}
}

/* returns carry if no Basic Energy cards are found in Deck */
static bool LookForCardsInDeck_SearchDeckForBasicEnergy(void)
{
	const u8 *hl = wDuelTempList;
	for (;;) {
		u8 a = *hl++;
		if (a == 0xff)
			return true;
		a = GetCardType(GetCardIDFromDeckIndex(a));
		if (a == TYPE_ENERGY_DOUBLE_COLORLESS)
			continue;
		if (!(a & TYPE_ENERGY))
			continue;
		return false;
	}
}

/* returns carry if no Pokemon cards are found in Deck */
static bool LookForCardsInDeck_SearchDeckForPokemon(void)
{
	const u8 *hl = wDuelTempList;
	for (;;) {
		u8 a = *hl++;
		if (a == 0xff)
			return true;
		if (GetCardType(GetCardIDFromDeckIndex(a)) >= TYPE_ENERGY)
			continue;
		return false;
	}
}

/* LookForCardsInDeck with both of its text inputs:
 *	hl = text to print if Deck has card(s)
 *	bc = name of the card(s) searched for, for ThereIsNoInTheDeckText */
static bool LookForCardsInDeck_HLBC(u8 d, u8 e, u16 hl, u16 bc)
{
	if (wDuelTempList[0] != 0xff) {
		bool c;
		/* .search_table (JumpToFunctionInTable) */
		switch (d) {
		case SEARCHEFFECT_CARD_ID:        c = LookForCardsInDeck_SearchDeckForCardID(e); break;
		case SEARCHEFFECT_NIDORAN:        c = LookForCardsInDeck_SearchDeckForNidoran(); break;
		case SEARCHEFFECT_BASIC_FIGHTING: c = LookForCardsInDeck_SearchDeckForBasicFighting(); break;
		case SEARCHEFFECT_BASIC_ENERGY:   c = LookForCardsInDeck_SearchDeckForBasicEnergy(); break;
		case SEARCHEFFECT_POKEMON:        c = LookForCardsInDeck_SearchDeckForPokemon(); break;
		default:
			tcg_fatal("LookForCardsInDeck: invalid SEARCHEFFECT_* value");
			return true;
		}
		if (!c) {
			DrawWideTextBox_WaitForInput(hl);
			return false;
		}
	}

	/* .none_in_deck */
	LoadTxRam2(bc);
	DrawWideTextBox_WaitForInput(ThereIsNoInTheDeckText);
	return YesOrNoMenuWithText_SetCursorToYes(WouldYouLikeToCheckTheDeckText);
}

/* the (hl, bc) text pairs of every LookForCardsInDeck caller in effect_functions.asm */
static const u16 LookForCardsInDeck_CallerTexts[][2] = {
	{ ChooseAnOddishFromDeckText,                    OddishText },
	{ ChooseNidoranFromDeckText,                     NidoranMNidoranFText },
	{ ChooseABellsproutFromDeckText,                 BellsproutText },
	{ ChooseAKrabbyFromDeckText,                     KrabbyText },
	{ ChooseBasicFightingPokemonFromDeckText,        FightingPokemonDeckText },
	{ Choose1BasicEnergyCardFromDeckText,            BasicEnergyText },
	{ ChooseBasicOrEvolutionPokemonCardFromDeckText, EvolutionCardText },
};

/* searches through Deck in wDuelTempList looking for
 * a certain card or cards, and prints text depending
 * on whether at least one was found.
 * if none were found, asks the Player whether to look
 * in the Deck anyway, and returns carry if No is selected.
 * uses SEARCHEFFECT_* as input which determines what to search for:
 *	SEARCHEFFECT_CARD_ID = search for card ID in e
 *	SEARCHEFFECT_NIDORAN = search for either NidoranM or NidoranF
 *	SEARCHEFFECT_BASIC_FIGHTING = search for any Basic Fighting Pokemon
 *	SEARCHEFFECT_BASIC_ENERGY = search for any Basic Energy
 *	SEARCHEFFECT_POKEMON = search for any Pokemon card
 * input:
 *	d = SEARCHEFFECT_* constant
 *	e = (optional) card ID to search for in deck
 *	hl = text to print if Deck has card(s)
 *	bc = name of what is searched for (printed if none is in the Deck)
 * output:
 *	carry set if refused to look at deck
 * The shared signature passes a single text: the original's other text input
 * is recovered from the caller table above (either hl or bc may be given). */
bool LookForCardsInDeck(u8 d, u8 e, u16 text)
{
	for (size_t i = 0; i < sizeof(LookForCardsInDeck_CallerTexts) / sizeof(LookForCardsInDeck_CallerTexts[0]); i++) {
		const u16 *t = LookForCardsInDeck_CallerTexts[i];
		if (t[0] == text || t[1] == text)
			return LookForCardsInDeck_HLBC(d, e, t[0], t[1]);
	}
	tcg_fatal("LookForCardsInDeck: unknown caller text");
	return true;
}

static const struct MenuParams HandleDefendingPokemonAttackSelection_MenuParameters = {
	1, 13,        /* cursor x, cursor y */
	2,            /* y displacement between items */
	2,            /* number of items */
	SYM_CURSOR_R, /* cursor tile number */
	SYM_SPACE,    /* tile behind cursor */
	0,            /* function pointer if non-0 */
	TCG_MENU_ATTACKS,
};

/* handles the Player selection of attack
 * to use, i.e. Amnesia or Metronome on.
 * returns carry if none selected.
 * outputs:
 *	d = card index of defending card
 *	e = attack index selected */
bool HandleDefendingPokemonAttackSelection(u8 *d, u8 *e)
{
	DrawDuelMainScene();
	SwapTurn();
	hCurSelectionItem = 0;

	for (;;) {
		/* .start */
		u8 n = PrintAndLoadAttacksToDuelTempList();
		InitializeMenuParameters(hCurSelectionItem, &HandleDefendingPokemonAttackSelection_MenuParameters);
		wNumMenuItems = n;
		/* (EnableLCD) */
		for (;;) {
			/* .loop_input */
			DoFrame();
			if (hKeysPressed & PAD_B)
				goto set_carry;
			if (hKeysPressed & PAD_START)
				goto open_atk_page;
			if (!HandleMenuInput())
				continue;
			if (hCurMenuItem == (u8)MENU_CANCEL) {
				/* the input shim reports B through HandleMenuInput; the original
				 * catches the B press in hKeysPressed above (.set_carry) */
				goto set_carry;
			}

			/* an attack was selected */
			u8 i = (u8)(hCurMenuItem + hCurMenuItem);
			if (d)
				*d = wDuelTempList[i];
			if (e)
				*e = wDuelTempList[i + 1];
			SwapTurn();
			return false;
		}

	open_atk_page:
		hCurSelectionItem = hCurMenuItem;
		LoadCardDataToBuffer1_FromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
		OpenAttackPage();
		SwapTurn();
		DrawDuelMainScene();
		SwapTurn();
		/* jr .start */
	}

set_carry:
	SwapTurn();
	return true;
}

/* loads in hl the pointer to attack's name.
 * input:
 *	d = deck index of card
 *	e = attack index (0 = first attack, 1 = second attack) */
u16 GetAttackName(u8 d, u8 e)
{
	LoadCardDataToBuffer1_FromDeckIndex(d);
	const u8 *hl = wLoadedCard1.Atk1.Name;
	if (e != 0)
		hl = wLoadedCard1.Atk2.Name;
	/* .load_name */
	return rd16(hl);
}

/* returns carry if Defending Pokemon
 * doesn't have an attack. */
bool CheckIfDefendingPokemonHasAnyAttack(void)
{
	SwapTurn();
	LoadCardDataToBuffer2_FromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	if (wLoadedCard2.Atk1.Category == POKEMON_POWER &&
	    (wLoadedCard2.Atk2.Name[0] | wLoadedCard2.Atk2.Name[1]) == 0) {
		SwapTurn();
		return true;
	}
	/* .has_attack */
	SwapTurn();
	return false;
}

/* overwrites HP and Stage data of the card that was
 * devolved in the Play Area to the values of new card.
 * if the damage exceeds HP of pre-evolution,
 * then HP is set to zero.
 * input:
 *	card (a) = card index of pre-evolved card */
void UpdateDevolvedCardHPAndStage(u8 card)
{
	u8 e = hTempPlayAreaLocation_ff9d;
	u8 b = GetCardDamageAndMaxHP(e, NULL); /* store damage */

	/* set this card index to duelist's Play Area */
	u8 *vars = TurnDuelistVars();
	vars[(u8)(e + DUELVARS_ARENA_CARD)] = card;

	LoadCardDataToBuffer2_FromDeckIndex(card);
	u8 a = wLoadedCard2.HP;
	if (a < b) {
		/* damage exceeds HP */
		a = 0; /* 0 HP */
	} else {
		a = (u8)(a - b); /* subtract damage from new HP */
	}
	/* .got_hp */
	vars[(u8)(e + DUELVARS_ARENA_CARD_HP)] = a;
	/* overwrite card stage */
	vars[(u8)(e + DUELVARS_ARENA_CARD_STAGE)] = wLoadedCard2.Stage;
}

/* reset various status after devolving card. */
void ResetDevolvedCardStatus(void)
{
	/* if it's Arena card, clear status conditions */
	if (hTempPlayAreaLocation_ff9d == PLAY_AREA_ARENA)
		ClearAllStatusConditions();
	/* .skip_clear_status */
	u8 *vars = TurnDuelistVars();
	/* reset changed color status */
	vars[(u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD_CHANGED_TYPE)] = 0;
	/* reset flags */
	vars[(u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD_FLAGS)] = 0;
}

/* prompts the Player with a Yes/No question
 * whether to quit the screen, even though
 * they can select more cards from list.
 * [hCurSelectionItem] holds number of cards
 * that were already selected by the Player.
 * input:
 *	- total (a) = total number of cards that can be selected
 * output:
 *	- carry set if "No" was selected */
bool AskWhetherToQuitSelectingCards(u8 total)
{
	LoadTxRam3((u8)(total - hCurSelectionItem));
	return YesOrNoMenuWithText(YouCanSelectMoreCardsQuitText);
}

/* handles the selection of a forced switch by link/AI opponent or by the player.
 * outputs the Play Area location of the chosen bench card in hTempPlayAreaLocation_ff9d
 * (also returned). */
u8 DuelistSelectForcedSwitch(void)
{
	u8 a = GetNonTurnDuelistVariable(DUELVARS_DUELIST_TYPE);
	if (a == DUELIST_TYPE_LINK_OPP) {
		/* .link_opp: get selection from link opponent */
		SetOppAction_SerialSendDuelData(OPPACTION_FORCE_SWITCH_ACTIVE);
		while (SerialRecvByte(&a)) {
			/* halt; nop (wait for the next interrupt) */
		}
		/* .received */
		hTempPlayAreaLocation_ff9d = a;
		return hTempPlayAreaLocation_ff9d;
	}

	if (a == DUELIST_TYPE_PLAYER) {
		/* .player: the non-turn duelist makes this selection */
		u8 saved_decider = gUIDecider;
		gUIDecider = (hWhoseTurn == PLAYER_TURN) ? OPPONENT_TURN : PLAYER_TURN;
		DrawWideTextBox_WaitForInput(SelectPkmnOnBenchToSwitchWithActiveText);
		SwapTurn();
		HasAlivePokemonInBench(NULL);
		wPlayAreaSelectAction = 1;
		while (OpenPlayAreaScreenForSelection())
			;
		SwapTurn();
		gUIDecider = saved_decider;
		return hTempPlayAreaLocation_ff9d;
	}

	/* AI opponent */
	SwapTurn();
	AIDoAction_ForcedSwitch();
	SwapTurn();

	CopyAttackDataAndDamage_FromCardID(wPlayerAttackingCardID, wPlayerAttackingCardIndex,
		wPlayerAttackingAttackIndex);
	UpdateArenaCardIDsAndClearTwoTurnDuelVars();
	return hTempPlayAreaLocation_ff9d;
}

/* returns in a the card index of energy card
 * attached to Defending Pokemon
 * that is to be discarded by the AI for an effect.
 * outputs -1 is none was found.
 * output:
 *	a = deck index of attached energy card chosen */
u8 AIPickEnergyCardToDiscardFromDefendingPokemon(void)
{
	u8 a, e;
	u8 *hl;

	SwapTurn();
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);

	if (CreateArenaOrBenchEnergyCardList(PLAY_AREA_ARENA)) {
		/* no energy, return */
		a = 0xff;
		goto done;
	}

	/* .has_energy */
	LoadCardDataToBuffer1_FromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	e = COLORLESS;
	if (wAttachedEnergies[COLORLESS] == 0) {
		/* no colorless energy attached.
		 * if it's colorless Pokemon, just
		 * pick any energy card at random... */
		a = wLoadedCard1.Type;
		if (a >= COLORLESS)
			goto choose_random;

		/* ...if not, check if it has its
		 * own color energy attached.
		 * if it doesn't, pick at random. */
		e = a;
		if (wAttachedEnergies[e] == 0)
			goto choose_random;
	}

	/* .pick_color: pick attached card with same color as e */
	hl = wDuelTempList;
	for (;;) {
		a = *hl++;
		if (a == 0xff)
			goto choose_random;
		LoadCardDataToBuffer2_FromDeckIndex(a);
		if ((wLoadedCard2.Type & TYPE_PKMN) == e)
			break;
	}
	hl--;

done_chosen:
	a = *hl;
done:
	SwapTurn();
	return a;

choose_random:
	ShuffleCards(wDuelTempList, CountCardsInDuelTempList());
	hl = wDuelTempList;
	goto done_chosen;
}

/* handles AI logic to pick attack for Amnesia */
u8 AIPickAttackForAmnesia(void)
{
	/* load Defending Pokemon attacks */
	SwapTurn();
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	HandleEnergyBurn();
	/* HandleEnergyBurn leaves in e the ID of the arena card (GetCardIDFromDeckIndex) */
	u8 e = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	u8 d = GetTurnDuelistVariable(DUELVARS_ARENA_CARD);
	LoadCardDataToBuffer2_FromDeckIndex(d);
	/* if has no attack 1 name, return */
	if ((wLoadedCard2.Atk1.Name[0] | wLoadedCard2.Atk1.Name[1]) == 0)
		goto chosen; /* bug (original): e holds the card ID here, not an attack index */

	/* if Defending Pokemon has enough energy for second attack, choose it */
	e = SECOND_ATTACK;
	if (!CheckIfEnoughEnergiesForGivenAttack(d, e))
		goto chosen;
	/* otherwise if first attack isn't a Pkmn Power, choose it instead. */
	e = FIRST_ATTACK_OR_PKMN_POWER;
	if (wLoadedCard2.Atk1.Category != POKEMON_POWER)
		goto chosen;
	/* if it is a Pkmn Power, choose second attack. */
	e = SECOND_ATTACK;

chosen:
	SwapTurn();
	return e;
}

/* Return in a the PLAY_AREA_* of the non-turn holder's Pokemon card in bench with the lowest (remaining) HP.
 * if multiple cards are tied for the lowest HP, the one with the highest PLAY_AREA_* is returned. */
u8 AIFindTargetForBenchAttack(void)
{
	SwapTurn();
	u8 *vars = TurnDuelistVars();
	u8 c = vars[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA];
	u8 d = PLAY_AREA_ARENA, e = 255;
	u8 b = d;
	u8 hl = DUELVARS_BENCH1_CARD_HP;
	/* jr .start */
	for (;;) {
		/* .start */
		b++;
		if (--c == 0)
			break;
		/* .loop_bench: find Play Area location with least amount of HP */
		if (!(e < vars[hl])) { /* skip if HP is higher */
			e = vars[hl];
			d = b;
		}
		/* .next */
		hl++;
	}
	SwapTurn();
	return d;
}

static const struct MenuParams HandleColorChangeScreen_MenuParams = {
	1, 1,                  /* cursor x, cursor y */
	2,                     /* y displacement between items */
	MAX_PLAY_AREA_POKEMON, /* number of items */
	SYM_CURSOR_R,          /* cursor tile number */
	SYM_SPACE,             /* tile behind cursor */
	0,                     /* function pointer if non-0 */
	TCG_MENU_GENERIC,      /* items: the colors in ShiftListItemToColor order */
};

static void HandleColorChangeScreen_DrawScreen(u8 a, u16 hl)
{
	EmptyScreen();
	/* (ZeroObjectPositions, LoadDuelCardSymbolTiles: drawing only) */

	/* load card data */
	a &= 0x7f;
	wTempPlayAreaLocation_cceb = a;
	LoadCardDataToBuffer1_FromDeckIndex(GetTurnDuelistVariable((u8)(a + DUELVARS_ARENA_CARD)));

	/* (draw card gfx, palettes: drawing only) */

	/* print card name and level at the top */
	CopyCardNameAndLevel(16);

	/* (list all the colors: ShiftMenuData, drawing only) */

	/* print card's color, resistance and weakness
	 * (only the lookups are kept, for their side effects) */
	(void)GetPlayAreaCardColor(wTempPlayAreaLocation_cceb);
	(void)GetPlayAreaCardWeakness(wTempPlayAreaLocation_cceb);
	(void)GetPlayAreaCardResistance(wTempPlayAreaLocation_cceb);

	DrawWideTextBox();

	/* (print list of color names on all list items: ColorListText) */

	/* print input hl to text box */
	tcg_ui_screen(TCG_SCREEN_OTHER, wTempPlayAreaLocation_cceb, 0);
	tcg_ui_event(TCG_EV_TEXT, 0, 0, 0, 0, hl);

	/* (draw and apply palette to color icons: ColorTileAndBGP, drawing only) */
}

/* handles drawing and selection of screen for
 * choosing a color (excluding colorless), for use
 * of Shift Pkmn Power and Conversion attacks.
 * outputs in a the color that was selected or,
 * if B was pressed, returns carry.
 * input:
 *	loc (a)  = Play Area location (PLAY_AREA_*), with:
 *	     bit 7 not set if it's applying to opponent's card
 *	     bit 7 set if it's applying to player's card
 *	text (hl) = text to be printed in the bottom box
 * output:
 *	*color (a) = color that was selected (MENU_CANCEL if carry) */
bool HandleColorChangeScreen(u8 loc, u16 text, u8 *color)
{
	bool z = loc == 0; /* or a */
	if (z)
		SwapTurn();
	HandleColorChangeScreen_DrawScreen(loc, text);
	if (z)
		SwapTurn();

	InitializeMenuParameters(0, &HandleColorChangeScreen_MenuParams);
	/* (EnableLCD) */

	/* .loop_input (the menu's items map to colors through ShiftListItemToColor) */
	HandleColorMenuInput(ShiftListItemToColor);
	u8 a = hCurMenuItem;
	if (a == (u8)MENU_CANCEL) { /* b pressed? */
		/* .set_carry */
		if (color)
			*color = a;
		return true;
	}
	a = ShiftListItemToColor[a];
	if (color)
		*color = a;
	return false;
}

/* loads wTxRam2 and wTxRam2_b:
 * [wTxRam2]   <- wLoadedCard1Name
 * [wTxRam2_b] <- input color as text symbol
 * input:
 *	color (a) = type (color) constant */
void LoadCardNameAndInputColor(u8 color)
{
	/* add a: 8-bit, so bit 7 (HAS_CHANGED_COLOR, see Shift_ChangeColorEffect) is dropped */
	u8 i = (u8)(color + color) >> 1;
	if (i >= NUM_COLORED_TYPES)
		tcg_fatal("LoadCardNameAndInputColor: color past ColorToTextSymbol");

	/* load wTxRam2 with card's name */
	wTxRam2[0] = wLoadedCard1.Name[0];
	wTxRam2[1] = wLoadedCard1.Name[1];

	/* load wTxRam2_b with ColorToTextSymbol */
	wTxRam2_b[0] = (u8)ColorToTextSymbol[i];
	wTxRam2_b[1] = (u8)(ColorToTextSymbol[i] >> 8);
}

/* drawing data of HandleColorChangeScreen.DrawScreen (not needed by the port) */
__attribute__((unused)) static const struct { u8 x, y; u16 text; } ShiftMenuData[] = {
	/* x, y, text id */
	{ 10,  9, TypeText },
	{ 10, 10, WeaknessText },
	{ 10, 11, ResistanceText },
	/* db $ff */
};

__attribute__((unused)) static const u8 ColorTileAndBGP[NUM_COLORED_TYPES][2] = {
	/* tile, BG */
	{ 0xe4, 0x02 },
	{ 0xe0, 0x01 },
	{ 0xec, 0x02 },
	{ 0xe8, 0x01 },
	{ 0xf0, 0x03 },
	{ 0xf4, 0x03 },
};

static const u8 ShiftListItemToColor[NUM_COLORED_TYPES] = {
	GRASS,
	FIRE,
	WATER,
	LIGHTNING,
	FIGHTING,
	PSYCHIC,
};

static const u16 ColorToTextSymbol[NUM_COLORED_TYPES] = {
	FireSymbolText,
	GrassSymbolText,
	LightningSymbolText,
	WaterSymbolText,
	FightingSymbolText,
	PsychicSymbolText,
};

/* a = play area slot, b = symbol tile.
 * writes tile b at x = 0, y = 3 * a + 2 of the play area screen: drawing only */
void DrawSymbolOnPlayAreaCursor(u8 a, u8 b)
{
}

/* possibly unreferenced */
void Func_2c6d9(void)
{
	DrawWideTextBox_WaitForInput(IncompleteText);
}

const struct MenuParams PlayAreaSelectionMenuParameters = {
	0, 0,                  /* cursor x, cursor y */
	3,                     /* y displacement between items */
	MAX_PLAY_AREA_POKEMON, /* number of items */
	SYM_CURSOR_R,          /* cursor tile number */
	SYM_SPACE,             /* tile behind cursor */
	0,                     /* function pointer if non-0 */
	TCG_MENU_PLAY_AREA,
};

const struct MenuParams BenchSelectionMenuParameters = {
	0, 3,                  /* cursor x, cursor y */
	3,                     /* y displacement between items */
	MAX_PLAY_AREA_POKEMON, /* number of items */
	SYM_CURSOR_R,          /* cursor tile number */
	SYM_SPACE,             /* tile behind cursor */
	0,                     /* function pointer if non-0 */
	TCG_MENU_PLAY_AREA,
};

bool SpitPoison_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(10 / 2, 0, 10);
	return false;
}

/* If heads, defending Pokemon becomes poisoned */
bool SpitPoison_Poison50PercentEffect(void)
{
	TRACE_FN();
	if (TossCoin_BankB(PoisonCheckText))
		return PoisonEffect();
	wLoadedAttackAnimation = ATK_ANIM_SPIT_POISON_SUCCESS;
	SetNoEffectFromStatus();
	return false;
}

/* outputs in hTemp_ffa0 the result of the coin toss (0 = tails, 1 = heads).
 * in case it was heads, stores in hTempPlayAreaLocation_ffa1
 * the PLAY_AREA_* location of the Bench Pokemon that was selected for switch. */
bool TerrorStrike_50PercentSelectSwitchPokemon(void)
{
	TRACE_FN();
	hTemp_ffa0 = PLAY_AREA_ARENA;

	/* return failure if no Pokemon to switch to */
	if (GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) < 2)
		return true;

	/* toss coin and store whether it was tails (0) or heads (1) in hTemp_ffa0.
	 * return if it was tails. */
	bool heads = Serial_TossCoin(IfHeadsChangeOpponentsActivePokemonText);
	hTemp_ffa0 = heads;
	if (!heads)
		return false;

	DuelistSelectForcedSwitch();
	hTempPlayAreaLocation_ffa1 = hTempPlayAreaLocation_ff9d;
	return false;
}

/* if coin toss at hTemp_ffa0 was heads and it's possible,
 * switch the Defending Pokemon */
bool TerrorStrike_SwitchDefendingPokemon(void)
{
	TRACE_FN();
	if (hTemp_ffa0 == 0)
		return false;
	HandleSwitchDefendingPokemonEffect(hTempPlayAreaLocation_ffa1);
	return false;
}

bool PoisonFang_AIEffect(void)
{
	TRACE_FN();
	return UpdateExpectedAIDamage_AccountForPoison_Carry(10, 10, 10);
}

bool WeepinbellPoisonPowder_AIEffect(void)
{
	TRACE_FN();
	return UpdateExpectedAIDamage_AccountForPoison_Carry(10 / 2, 0, 10);
}

/* return carry if there are no Pokemon cards in the non-turn holder's bench */
bool VictreebelLure_AssertPokemonInBench(void)
{
	TRACE_FN();
	u8 a = GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	gTextID = EffectNoPokemonOnTheBenchText;
	return a < 2;
}

/* return in hTempPlayAreaLocation_ffa1 the PLAY_AREA_* location
 * of the Bench Pokemon that was selected for switch */
bool VictreebelLure_SelectSwitchPokemon(void)
{
	TRACE_FN();
	DrawWideTextBox_WaitForInput(SelectPkmnOnBenchToSwitchWithActiveText);
	SwapTurn();
	HasAlivePokemonInBench(NULL);
	/* .select_pokemon */
	while (OpenPlayAreaScreenForSelection())
		;
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	SwapTurn();
	return false;
}

/* Return in hTemp_ffa0 the PLAY_AREA_* of the non-turn holder's Pokemon card in bench with the lowest (remaining) HP.
 * if multiple cards are tied for the lowest HP, the one with the highest PLAY_AREA_* is returned. */
bool VictreebelLure_GetBenchPokemonWithLowestHP(void)
{
	TRACE_FN();
	hTemp_ffa0 = AIFindTargetForBenchAttack();
	return false;
}

/* Defending Pokemon is swapped out for the one with the PLAY_AREA_* at hTemp_ffa0
 * unless Mew's Neutralizing Shield or Haunter's Transparency prevents it. */
bool VictreebelLure_SwitchDefendingPokemon(void)
{
	TRACE_FN();
	SwapTurn();
	u8 e = hTemp_ffa0;
	if (!HandleNShieldAndTransparency(e))
		SwapArenaWithBenchPokemon(e);
	SwapTurn();
	wDuelDisplayedScreen = 0;
	return false;
}

/* If heads, defending Pokemon can't retreat next turn */
bool AcidEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(AcidCheckText))
		return false;
	/* ApplySubstatus2ToDefendingCard restores the flags of its caller (carry set by
	 * the coin toss) unless the Defending Pokemon has No Damage or Effect */
	bool carry = wNoDamageOrEffect == 0;
	ApplySubstatus2ToDefendingCard(SUBSTATUS2_ACID);
	return carry;
}

bool GloomPoisonPowder_AIEffect(void)
{
	TRACE_FN();
	return UpdateExpectedAIDamage_AccountForPoison_Carry(10, 10, 10);
}

/* Defending Pokemon and user become confused */
bool FoulOdorEffect(void)
{
	TRACE_FN();
	ConfusionEffect();
	SwapTurn();
	bool carry = ConfusionEffect();
	SwapTurn();
	return carry;
}

/* If heads, prevent all damage done to user next turn */
bool KakunaStiffenEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(IfHeadsNoDamageNextTurnText)) {
		SetWasUnsuccessful();
		return false;
	}
	wLoadedAttackAnimation = ATK_ANIM_PROTECT;
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_NO_DAMAGE_STIFFEN);
	return true; /* (carry from the coin toss, kept by push af / pop af) */
}

bool KakunaPoisonPowder_AIEffect(void)
{
	TRACE_FN();
	return UpdateExpectedAIDamage_AccountForPoison_Carry(10 / 2, 0, 10);
}

bool GolbatLeechLifeEffect(void)
{
	TRACE_FN();
	ApplyAndAnimateHPRecovery(rd16(wDealtDamage));
	return false;
}

bool VenonatLeechLifeEffect(void)
{
	TRACE_FN();
	ApplyAndAnimateHPRecovery(rd16(wDealtDamage));
	return false;
}

/* During your next turn, double damage */
bool SwordsDanceEffect(void)
{
	TRACE_FN();
	u8 a = wTempTurnDuelistCardID;
	if (a != SCYTHER)
		return a < SCYTHER; /* (carry of cp SCYTHER) */
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_NEXT_TURN_DOUBLE_DAMAGE);
	return false;
}

/* If heads, defending Pokemon becomes confused */
bool ZubatSupersonicEffect(void)
{
	TRACE_FN();
	bool carry = Confusion50PercentEffect();
	if (!carry)
		SetNoEffectFromStatus();
	return carry;
}

bool ZubatLeechLifeEffect(void)
{
	TRACE_FN();
	ApplyAndAnimateHPRecovery(rd16(wDealtDamage));
	return false;
}

bool Twineedle_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(60 / 2, 0, 60);
	return false;
}

/* Flip 2 coins; deal 30x number of heads */
bool Twineedle_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(30);
	u8 a = TossCoinATimes_BankB(2, DamageCheckIfHeadsXDamageText);
	a = (u8)(a + a + a);
	SetDefiniteDamage(ATimes10(a));
	return false;
}

bool BeedrillPoisonSting_AIEffect(void)
{
	TRACE_FN();
	return UpdateExpectedAIDamage_AccountForPoison_Carry(10 / 2, 0, 10);
}

bool ExeggcuteLeechSeedEffect(void)
{
	TRACE_FN();
	if (wDealtDamage[0] == 0)
		return false; /* return if no damage dealt (only the low byte is checked) */
	ApplyAndAnimateHPRecovery(10);
	return false;
}

bool FoulGas_AIEffect(void)
{
	TRACE_FN();
	return UpdateExpectedAIDamage_SkipPushAF(10 / 2, 0, 10);
}

/* If heads, defending Pokemon becomes poisoned. If tails, defending Pokemon becomes confused */
bool FoulGas_PoisonOrConfusionEffect(void)
{
	TRACE_FN();
	if (TossCoin_BankB(PoisonedIfHeadsConfusedIfTailsText))
		return PoisonEffect();
	return ConfusionEffect();
}

/* an exact copy of KakunaStiffenEffect
 * If heads, prevent all damage done to user next turn */
bool MetapodStiffenEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(IfHeadsNoDamageNextTurnText)) {
		SetWasUnsuccessful();
		return false;
	}
	wLoadedAttackAnimation = ATK_ANIM_PROTECT;
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_NO_DAMAGE_STIFFEN);
	return true; /* (carry from the coin toss, kept by push af / pop af) */
}

/* returns carry if no cards in Deck or if
 * Play Area is full already. */
bool Sprout_CheckDeckAndPlayArea(void)
{
	TRACE_FN();
	if (CheckIfDeckIsEmpty())
		return true; /* return if no cards in deck */
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	gTextID = NoSpaceOnTheBenchText;
	return a >= MAX_PLAY_AREA_POKEMON;
}

bool Sprout_PlayerSelectEffect(void)
{
	TRACE_FN();
	hTemp_ffa0 = 0xff;

	CreateDeckCardList(NULL);
	if (LookForCardsInDeck_HLBC(SEARCHEFFECT_CARD_ID, ODDISH, ChooseAnOddishFromDeckText, OddishText))
		return true;

	/* draw Deck list interface and print text */
	InitAndDrawCardListScreenLayout_WithSelectCheckMenu();
	SetCardListHeaderText(ChooseAnOddishText, DuelistDeckText);

	for (;;) {
		/* .loop */
		u8 card;
		if (!DisplayCardList(&card)) {
			if (GetCardIDFromDeckIndex(card) == ODDISH) {
				/* Oddish was selected */
				hTemp_ffa0 = hTempCardIndex_ff98;
				return false;
			}
		} else {
			/* .pressed_b: figure if Player can exit the screen without selecting,
			 * that is, if the Deck has no Oddish card. */
			u8 *vars = TurnDuelistVars();
			bool found = false;
			for (u8 l = DUELVARS_CARD_LOCATIONS; l < DECK_SIZE; l++) {
				if (vars[l] != CARD_LOCATION_DECK)
					continue;
				if (GetCardIDFromDeckIndex(l) == ODDISH) {
					found = true; /* found Oddish, go back to top loop */
					break;
				}
			}
			if (!found) {
				/* no Oddish in Deck, can safely exit screen */
				hTemp_ffa0 = 0xff;
				return false;
			}
		}
		/* .play_sfx: play SFX and loop back */
		PlaySFX_InvalidChoice();
	}
}

bool Sprout_AISelectEffect(void)
{
	TRACE_FN();
	CreateDeckCardList(NULL);
	const u8 *hl = wDuelTempList;
	for (;;) {
		/* .loop_deck */
		u8 a = *hl++;
		hTemp_ffa0 = a;
		if (a == 0xff)
			return false; /* no Oddish */
		if (GetCardIDFromDeckIndex(a) == ODDISH)
			return false; /* Oddish found */
	}
}

bool Sprout_PutInPlayAreaEffect(void)
{
	TRACE_FN();
	u8 a = hTemp_ffa0;
	if (a != 0xff) {
		SearchCardInDeckAndAddToHand(a);
		AddCardToHand(a);
		PutHandPokemonCardInPlayArea(a, NULL);
		if (!IsPlayerTurn()) {
			/* display card on screen */
			DisplayCardDetailScreen(hTemp_ffa0, PlacedOnTheBenchText);
		}
	}
	/* .shuffle */
	ShuffleCardsInDeck();
	return false;
}

/* returns carry if no Pokemon on Bench */
bool Teleport_CheckBench(void)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	gTextID = ThereAreNoPokemonOnBenchText;
	return a < 2;
}

bool Teleport_PlayerSelectEffect(void)
{
	TRACE_FN();
	DrawWideTextBox_WaitForInput(SelectPkmnOnBenchToSwitchWithActiveText);
	HasAlivePokemonInBench(NULL);
	wPlayAreaSelectAction = 1;
	/* .loop */
	while (OpenPlayAreaScreenForSelection())
		;
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	return false;
}

/* selects a random card from the bench to switch to */
bool Teleport_AISelectEffect(void)
{
	TRACE_FN();
	hTemp_ffa0 = Random(GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA));
	return false;
}

bool Teleport_SwitchEffect(void)
{
	TRACE_FN();
	SwapArenaWithBenchPokemon(hTemp_ffa0);
	wDuelDisplayedScreen = 0;
	return false;
}

static u16 SetDamageToATimes20(u8 a);

bool BigEggsplosion_AIEffect(void)
{
	TRACE_FN();
	GetPlayAreaCardAttachedEnergies(hTempPlayAreaLocation_ff9d);
	u16 hl = SetDamageToATimes20(wTotalAttachedEnergies);
	u8 h = (u8)(hl >> 8), l = (u8)hl;
	/* bug (original): inc h / jr nz only caps the damage when h is $ff */
	if ((u8)(h + 1) == 0)
		l = 255;
	/* .capped: sets max damage as (num energy cards) * 20 */
	wAIMaxDamage = l;
	/* sets expected damage as half max amount */
	wDamage[0] = l >> 1;
	/* sets min damage as 0 */
	wAIMinDamage = 0;
	return false;
}

/* Flip coins equal to attached energies; deal 20x number of heads */
bool BigEggsplosion_MultiplierEffect(void)
{
	TRACE_FN();
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	LoadTxRam3(20);
	u8 a = TossCoinATimes_BankB(wTotalAttachedEnergies, DamageCheckIfHeadsXDamageText);
	/* fallthrough */
	SetDamageToATimes20(a);
	return false;
}

/* set damage to 20*a. Also return result in hl */
static u16 SetDamageToATimes20(u8 a)
{
	u16 hl = a, de = hl;
	hl = (u16)(hl + hl);
	hl = (u16)(hl + hl);
	hl = (u16)(hl + de);
	hl = (u16)(hl + hl);
	hl = (u16)(hl + hl);
	wr16(wDamage, hl);
	return hl;
}

bool Thrash_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage((30 + 40) / 2, 30, 40);
	return false;
}

/* AddToDamage, also returning the carry of its final `adc [hl]` */
static bool AddToDamage_Carry(u8 a)
{
	bool carry = wDamage[0] + a > 0xff && wDamage[1] == 0xff;
	AddToDamage(a);
	return carry;
}

/* If heads 10 more damage; if tails, 10 damage to itself */
bool Thrash_ModifierEffect(void)
{
	TRACE_FN();
	bool heads = TossCoin_BankB(IfHeadPlus10IfTails10ToYourselfText);
	hTemp_ffa0 = heads;
	if (!heads)
		return false;
	return AddToDamage_Carry(10);
}

bool Thrash_RecoilEffect(void)
{
	TRACE_FN();
	if (hTemp_ffa0 != 0)
		return false;
	DealRecoilDamageToSelf(10);
	return false;
}

bool Toxic_AIEffect(void)
{
	TRACE_FN();
	return UpdateExpectedAIDamage_SkipPushAF(20, 20, 20);
}

/* Defending Pokémon becomes double poisoned (takes 20 damage per turn rather than 10) */
bool Toxic_DoublePoisonEffect(void)
{
	TRACE_FN();
	return DoublePoisonEffect();
}

bool BoyfriendsEffect(void)
{
	TRACE_FN();
	u8 *vars = TurnDuelistVars();
	u8 l = DUELVARS_ARENA_CARD;
	u8 c = PLAY_AREA_ARENA;
	for (;;) {
		/* .loop */
		u8 a = vars[l];
		if (a == 0xff)
			break;
		/* is this card Nidoking? */
		if (GetCardIDFromDeckIndex(a) == NIDOKING)
			c++; /* (the original also checks that d is $00: card IDs are only 1 byte long) */
		/* .next */
		l++;
	}
	/* .done: c holds number of Nidoking found in Play Area */
	return AddToDamage_Carry(ATimes10((u8)(c + c))); /* adds 2 * 10 * c */
}

bool NidoranFFurySwipes_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(30 / 2, 0, 30);
	return false;
}

bool NidoranFFurySwipes_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(10);
	u8 a = TossCoinATimes_BankB(3, DamageCheckIfHeadsXDamageText);
	SetDefiniteDamage(ATimes10(a));
	return false;
}

bool NidoranFCallForFamily_CheckDeckAndPlayArea(void)
{
	TRACE_FN();
	if (CheckIfDeckIsEmpty())
		return true; /* return if no cards in deck */
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	gTextID = NoSpaceOnTheBenchText;
	return a >= MAX_PLAY_AREA_POKEMON;
}

bool NidoranFCallForFamily_PlayerSelectEffect(void)
{
	TRACE_FN();
	hTemp_ffa0 = 0xff;

	CreateDeckCardList(NULL);
	if (LookForCardsInDeck_HLBC(SEARCHEFFECT_NIDORAN, 0x00, ChooseNidoranFromDeckText, NidoranMNidoranFText))
		return true;

	/* draw Deck list interface and print text */
	InitAndDrawCardListScreenLayout_WithSelectCheckMenu();
	SetCardListHeaderText(ChooseNidoranText, DuelistDeckText);

	for (;;) {
		/* .loop */
		u8 card;
		if (!DisplayCardList(&card)) {
			u8 id = GetCardIDFromDeckIndex(card);
			if (id == NIDORANF || id == NIDORANM) {
				/* .selected_nidoran */
				hTemp_ffa0 = hTempCardIndex_ff98;
				return false;
			}
			continue; /* .play_sfx would be more appropriate here */
		}

		/* .pressed_b: figure if Player can exit the screen without selecting,
		 * that is, if the Deck has no NidoranF or NidoranM card. */
		u8 *vars = TurnDuelistVars();
		bool found = false;
		for (u8 l = DUELVARS_CARD_LOCATIONS; l < DECK_SIZE; l++) {
			if (vars[l] != CARD_LOCATION_DECK)
				continue;
			/* bug (original): the NIDORANM check (ld bc, NIDORANM / jr z) reuses the
			 * flags of the NIDORANF comparison, so only NidoranF is looked for */
			if (GetCardIDFromDeckIndex(l) == NIDORANF) {
				found = true; /* found, go back to top loop */
				break;
			}
		}
		if (!found) {
			/* no Nidoran in Deck, can safely exit screen */
			hTemp_ffa0 = 0xff;
			return false;
		}
		/* .play_sfx: play SFX and loop back */
		PlaySFX_InvalidChoice();
	}
}

bool NidoranFCallForFamily_AISelectEffect(void)
{
	TRACE_FN();
	CreateDeckCardList(NULL);
	const u8 *hl = wDuelTempList;
	for (;;) {
		/* .loop_deck */
		u8 a = *hl++;
		hTemp_ffa0 = a;
		if (a == 0xff)
			return false; /* none found */
		a = GetCardIDFromDeckIndex(a);
		if (a == NIDORANF || a == NIDORANM)
			return false; /* .found */
	}
}

bool NidoranFCallForFamily_PutInPlayAreaEffect(void)
{
	TRACE_FN();
	u8 a = hTemp_ffa0;
	if (a != 0xff) {
		SearchCardInDeckAndAddToHand(a);
		AddCardToHand(a);
		PutHandPokemonCardInPlayArea(a, NULL);
		if (!IsPlayerTurn()) {
			/* display card on screen */
			DisplayCardDetailScreen(hTemp_ffa0, PlacedOnTheBenchText);
		}
	}
	/* .shuffle */
	ShuffleCardsInDeck();
	return false;
}

bool HornHazard_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(30 / 2, 0, 30);
	return false;
}

bool HornHazard_NoDamage50PercentEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(DamageCheckIfTailsNoDamageText)) {
		SetDefiniteDamage(0);
		SetWasUnsuccessful();
		return false;
	}
	/* .heads */
	wLoadedAttackAnimation = ATK_ANIM_HIT;
	return true; /* (carry from the coin toss) */
}

bool NidorinaSupersonicEffect(void)
{
	TRACE_FN();
	bool carry = Confusion50PercentEffect();
	if (!carry)
		SetNoEffectFromStatus();
	return carry;
}

bool NidorinaDoubleKick_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(60 / 2, 0, 60);
	return false;
}

bool NidorinaDoubleKick_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(30);
	u8 a = TossCoinATimes_BankB(2, DamageCheckIfHeadsXDamageText);
	a = (u8)(a + a + a);
	SetDefiniteDamage(ATimes10(a));
	return false;
}

bool NidorinoDoubleKick_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(60 / 2, 0, 60);
	return false;
}

bool NidorinoDoubleKick_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(30);
	u8 a = TossCoinATimes_BankB(2, DamageCheckIfHeadsXDamageText);
	a = (u8)(a + a + a);
	SetDefiniteDamage(ATimes10(a));
	return false;
}

bool ButterfreeWhirlwind_CheckBench(void)
{
	TRACE_FN();
	if (GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) < 2) {
		/* no bench, do not do effect */
		hTemp_ffa0 = 0xff;
		return true; /* (carry of cp 2) */
	}
	/* .has_bench */
	DuelistSelectForcedSwitch();
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	return false;
}

bool ButterfreeWhirlwind_SwitchEffect(void)
{
	TRACE_FN();
	HandleSwitchDefendingPokemonEffect(hTemp_ffa0);
	return false;
}

bool ButterfreeMegaDrainEffect(void)
{
	TRACE_FN();
	u16 hl = rd16(wDealtDamage);
	hl >>= 1; /* srl h / rr l */
	if (hl & 1) {
		/* round up to nearest 10 */
		hl = (u16)(hl + 10 / 2);
	}
	/* .rounded */
	ApplyAndAnimateHPRecovery(hl);
	return false;
}

bool WeedlePoisonSting_AIEffect(void)
{
	TRACE_FN();
	return UpdateExpectedAIDamage_AccountForPoison_Carry(10 / 2, 0, 10);
}

bool IvysaurPoisonPowder_AIEffect(void)
{
	TRACE_FN();
	return UpdateExpectedAIDamage_AccountForPoison_Carry(10, 10, 10);
}

bool BulbasaurLeechSeedEffect(void)
{
	TRACE_FN();
	if ((wDealtDamage[0] | wDealtDamage[1]) == 0)
		return false; /* return if no damage dealt */
	ApplyAndAnimateHPRecovery(10);
	return false;
}

/* returns carry if no Grass Energy in Play Area */
bool EnergyTrans_CheckPlayArea(void)
{
	TRACE_FN();
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	if (CheckIsIncapableOfUsingPkmnPower(hTempPlayAreaLocation_ff9d))
		return true; /* cannot use Pkmn Power */

	/* search in Play Area for at least 1 Grass Energy type */
	u8 *vars = TurnDuelistVars();
	u8 l = DUELVARS_CARD_LOCATIONS;
	do {
		/* .loop_deck */
		if (vars[l] & CARD_LOCATION_PLAY_AREA) {
			if (GetCardType(GetCardIDFromDeckIndex(l)) == TYPE_ENERGY_GRASS)
				return false; /* (hl is left pointing to the card's location) */
		}
		/* .next */
		l++;
	} while (l < DECK_SIZE);

	/* none found */
	gTextID = NoGrassEnergyText;
	return true;
}

bool EnergyTrans_PrintProcedure(void)
{
	TRACE_FN();
	DrawWholeScreenTextBox(ProcedureForEnergyTransferText);
	return false;
}

static bool CheckIfCardHasGrassEnergyAttached(u8 loc, u8 *card);

bool EnergyTrans_TransferEffect(void)
{
	TRACE_FN();
	if (GetTurnDuelistVariable(DUELVARS_DUELIST_TYPE) != DUELIST_TYPE_PLAYER) {
		/* not player */
		SetupPlayAreaScreen();
		PrintPlayAreaCardList_EnableLCD();
		return false;
	}

	/* .player */
	hCurSelectionItem = 0;
	SetupPlayAreaScreen();

	for (;;) {
		/* .draw_play_area */
		u8 n = PrintPlayAreaCardList_EnableLCD();
		InitializeMenuParameters(hCurSelectionItem, &PlayAreaSelectionMenuParameters);
		wNumMenuItems = n;

		/* handle the action of taking a Grass Energy card */
		u8 a, card;
		for (;;) {
			/* .loop_input_take */
			do {
				DoFrame();
			} while (!HandleMenuInput());
			a = hCurMenuItem;
			if (a == (u8)MENU_CANCEL) /* b press? */
				return false;

			/* a press */
			hAIPkmnPowerEffectParam = a;
			hCurSelectionItem = a;
			if (!CheckIfCardHasGrassEnergyAttached(a, &card))
				break;
			/* .play_sfx: no Grass attached */
			PlaySFX_InvalidChoice();
		}

		hAIEnergyTransEnergyCard = card;
		/* temporarily take card away to draw Play Area */
		AddCardToHand(hAIEnergyTransEnergyCard);
		PrintPlayAreaCardList_EnableLCD();
		/* give card back */
		PutHandCardInPlayArea(hAIEnergyTransEnergyCard, hAIPkmnPowerEffectParam);

		/* draw Grass symbol near cursor */
		DrawSymbolOnPlayAreaCursor(hAIPkmnPowerEffectParam, SYM_GRASS);

		/* handle the action of placing a Grass Energy card */
		/* .loop_input_put */
		do {
			DoFrame();
		} while (!HandleMenuInput());
		a = hCurMenuItem;
		if (a != (u8)MENU_CANCEL) { /* b press? */
			/* a press */
			hCurSelectionItem = a;
			hAIEnergyTransPlayAreaLocation = a;
			SetOppAction_SerialSendDuelData(OPPACTION_6B15);
			/* give card being held to this Pokemon */
			AddCardToHand(hAIEnergyTransEnergyCard);
			PutHandCardInPlayArea(hAIEnergyTransEnergyCard, hAIEnergyTransPlayAreaLocation);
		}

		/* .remove_symbol */
		DrawSymbolOnPlayAreaCursor(hAIPkmnPowerEffectParam, SYM_SPACE);
		EraseCursor();
		/* jr .draw_play_area */
	}
}

bool EnergyTrans_AIEffect(void)
{
	TRACE_FN();
	AddCardToHand(hAIEnergyTransEnergyCard);
	PutHandCardInPlayArea(hAIEnergyTransEnergyCard, hAIEnergyTransPlayAreaLocation);
	PrintPlayAreaCardList_EnableLCD();
	return false;
}

/* returns carry if no Grass Energy cards
 * attached to card in Play Area location of a.
 * input:
 *	loc (a) = PLAY_AREA_* of location to check
 * output (nc): *card (a) = deck index of the Grass Energy found */
static bool CheckIfCardHasGrassEnergyAttached(u8 loc, u8 *card)
{
	u8 e = loc | CARD_LOCATION_PLAY_AREA;
	u8 *vars = TurnDuelistVars();
	u8 l = DUELVARS_CARD_LOCATIONS;
	do {
		/* .loop */
		if (vars[l] == e) {
			if (GetCardType(GetCardIDFromDeckIndex(l)) == TYPE_ENERGY_GRASS) {
				/* .no_carry */
				*card = l;
				return false;
			}
		}
		/* .next */
		l++;
	} while (l < DECK_SIZE);
	*card = l;
	return true;
}

bool GrimerMinimizeEffect(void)
{
	TRACE_FN();
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_REDUCE_BY_20);
	return false;
}

bool ToxicGasEffect(void)
{
	TRACE_FN();
	return true;
}

bool Sludge_AIEffect(void)
{
	TRACE_FN();
	return UpdateExpectedAIDamage_AccountForPoison_Carry(10 / 2, 0, 10);
}

/* returns carry if no cards in Deck
 * or if Play Area is full already. */
bool BellsproutCallForFamily_CheckDeckAndPlayArea(void)
{
	TRACE_FN();
	if (CheckIfDeckIsEmpty())
		return true; /* return if no cards in deck */
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	gTextID = NoSpaceOnTheBenchText;
	return a >= MAX_PLAY_AREA_POKEMON;
}

bool BellsproutCallForFamily_PlayerSelectEffect(void)
{
	TRACE_FN();
	hTemp_ffa0 = 0xff;

	CreateDeckCardList(NULL);
	if (LookForCardsInDeck_HLBC(SEARCHEFFECT_CARD_ID, BELLSPROUT, ChooseABellsproutFromDeckText, BellsproutText))
		return true;

	/* draw Deck list interface and print text */
	InitAndDrawCardListScreenLayout_WithSelectCheckMenu();
	SetCardListHeaderText(ChooseABellsproutText, DuelistDeckText);

	for (;;) {
		/* .loop */
		u8 card;
		if (!DisplayCardList(&card)) {
			if (GetCardIDFromDeckIndex(card) == BELLSPROUT) {
				/* Bellsprout was selected */
				hTemp_ffa0 = hTempCardIndex_ff98;
				return false;
			}
		} else {
			/* .pressed_b: figure if Player can exit the screen without selecting,
			 * that is, if the Deck has no Bellsprout card. */
			u8 *vars = TurnDuelistVars();
			bool found = false;
			for (u8 l = DUELVARS_CARD_LOCATIONS; l < DECK_SIZE; l++) {
				if (vars[l] != CARD_LOCATION_DECK)
					continue;
				if (GetCardIDFromDeckIndex(l) == BELLSPROUT) {
					found = true; /* found Bellsprout, go back to top loop */
					break;
				}
			}
			if (!found) {
				/* no Bellsprout in Deck, can safely exit screen */
				hTemp_ffa0 = 0xff;
				return false;
			}
		}
		/* .play_sfx: play SFX and loop back */
		PlaySFX_InvalidChoice();
	}
}

bool BellsproutCallForFamily_AISelectEffect(void)
{
	TRACE_FN();
	CreateDeckCardList(NULL);
	const u8 *hl = wDuelTempList;
	for (;;) {
		/* .loop_deck */
		u8 a = *hl++;
		hTemp_ffa0 = a;
		if (a == 0xff)
			return false; /* no Bellsprout */
		if (GetCardIDFromDeckIndex(a) == BELLSPROUT)
			return false; /* Bellsprout found */
	}
}

bool BellsproutCallForFamily_PutInPlayAreaEffect(void)
{
	TRACE_FN();
	u8 a = hTemp_ffa0;
	if (a != 0xff) {
		SearchCardInDeckAndAddToHand(a);
		AddCardToHand(a);
		PutHandPokemonCardInPlayArea(a, NULL);
		if (!IsPlayerTurn())
			DisplayCardDetailScreen(hTemp_ffa0, PlacedOnTheBenchText);
	}
	/* .shuffle */
	ShuffleCardsInDeck();
	return false;
}

bool WeezingSmog_AIEffect(void)
{
	TRACE_FN();
	return UpdateExpectedAIDamage_AccountForPoison_Carry(10 / 2, 0, 10);
}

bool WeezingSelfdestructEffect(void)
{
	TRACE_FN();
	DealRecoilDamageToSelf(60);
	wIsDamageToSelf = TRUE;
	DealDamageToAllBenchedPokemon(10);
	SwapTurn();
	wIsDamageToSelf = FALSE;
	DealDamageToAllBenchedPokemon(10);
	SwapTurn();
	return false;
}

bool Shift_OncePerTurnCheck(void)
{
	TRACE_FN();
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	if (GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD_FLAGS)) & USED_PKMN_POWER_THIS_TURN) {
		/* .already_used */
		gTextID = OnlyOncePerTurnText;
		return true;
	}
	return CheckIsIncapableOfUsingPkmnPower(hTempPlayAreaLocation_ff9d);
}

/* checks in input color in a exists in Turn Duelist's Play Area
 * returns carry if not found. */
static bool Shift_PlayerSelectEffect_CheckColorInPlayArea(void)
{
	u8 c = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	u8 b = PLAY_AREA_ARENA;
	do {
		/* .loop_play_area */
		if (GetPlayAreaCardColor(b) == hAIPkmnPowerEffectParam)
			return false; /* found */
		b++;
	} while (--c);
	/* not found */
	return true;
}

bool Shift_PlayerSelectEffect(void)
{
	for (;;) {
		/* .loop (the routine's entry in the original) */
		TRACE_FN();
		u8 a;
		bool cancelled = HandleColorChangeScreen(hTemp_ffa0 | 0x80, ChoosePokemonWishToColorChangeText, &a);
		hAIPkmnPowerEffectParam = a;
		if (cancelled)
			return true; /* cancelled */

		/* check whether the color selected is valid */
		/* look in Turn Duelist's Play Area */
		if (!Shift_PlayerSelectEffect_CheckColorInPlayArea())
			return false;
		/* look in NonTurn Duelist's Play Area */
		SwapTurn();
		bool not_found = Shift_PlayerSelectEffect_CheckColorInPlayArea();
		SwapTurn();
		if (!not_found)
			return false;
		/* not found in either Duelist's Play Area */
		DrawWideTextBox_WaitForInput(UnableToSelectText);
		/* loop back to start */
	}
}

bool Shift_ChangeColorEffect(void)
{
	TRACE_FN();
	u8 *vars = TurnDuelistVars();
	LoadCardDataToBuffer1_FromDeckIndex(vars[(u8)(hTemp_ffa0 + DUELVARS_ARENA_CARD)]);

	vars[(u8)(hTemp_ffa0 + DUELVARS_ARENA_CARD_FLAGS)] |= 1 << USED_PKMN_POWER_THIS_TURN_F;

	u8 a = hAIPkmnPowerEffectParam | HAS_CHANGED_COLOR;
	vars[(u8)(hTemp_ffa0 + DUELVARS_ARENA_CARD_CHANGED_TYPE)] = a;
	LoadCardNameAndInputColor(a);
	DrawWideTextBox_WaitForInput(ChangedTheColorOfText);
	return false;
}

bool VenomPowder_AIEffect(void)
{
	TRACE_FN();
	return UpdateExpectedAIDamage_SkipPushAF(10 / 2, 0, 10);
}

bool VenomPowder_PoisonConfusion50PercentEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(VenomPowderCheckText))
		return false; /* return if tails */

	/* heads */
	PoisonEffect();
	if (ConfusionEffect())
		return true;
	wNoEffectFromWhichStatus = CONFUSED | POISONED;
	return false;
}

bool TangelaPoisonPowder_AIEffect(void)
{
	TRACE_FN();
	return UpdateExpectedAIDamage_AccountForPoison_Carry(10 / 2, 0, 10);
}

bool Heal_OncePerTurnCheck(void)
{
	TRACE_FN();
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	if (GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD_FLAGS)) & USED_PKMN_POWER_THIS_TURN) {
		/* .already_used */
		gTextID = OnlyOncePerTurnText;
		return true;
	}

	bool none = CheckIfPlayAreaHasAnyDamage();
	gTextID = NoPokemonWithDamageCountersText;
	if (none)
		return true; /* no damage counters to heal */

	return CheckIsIncapableOfUsingPkmnPower(hTempPlayAreaLocation_ff9d);
}

bool Heal_RemoveDamageEffect(void)
{
	TRACE_FN();
	bool heads = TossCoin_BankB(IfHeadsHealIsSuccessfulText);
	hAIPkmnPowerEffectParam = heads;
	if (heads) {
		u8 a = GetTurnDuelistVariable(DUELVARS_DUELIST_TYPE);
		if (a == DUELIST_TYPE_LINK_OPP) {
			/* .link_opp */
			SerialRecv8Bytes();
			/* register a = the second received byte (SerialRecv8Bytes receives into
			 * wTempSerialBuf and pops f, a from its first two bytes) */
			hPlayAreaEffectTarget = wTempSerialBuf[1];
			/* fallthrough */
		} else if (!(a & DUELIST_TYPE_AI_OPP)) {
			/* player */
			DrawWideTextBox_WaitForInput(ChoosePkmnToRemoveDamageCounterText);
			HasAlivePokemonInPlayArea(NULL);
			for (;;) {
				/* .loop_input */
				while (OpenPlayAreaScreenForSelection())
					;
				hPlayAreaEffectTarget = hTempPlayAreaLocation_ff9d;
				if (GetCardDamageAndMaxHP(hTempPlayAreaLocation_ff9d, NULL) != 0)
					break;
				/* has no damage counters */
			}
			SerialSend8Bytes(); /* sends registers f, a (= hTempPlayAreaLocation_ff9d), l, h, e, d, c, b */
		}
	}

	/* .done: flag Pkmn Power as being used regardless of coin outcome */
	u8 *vars = TurnDuelistVars();
	vars[(u8)(hTemp_ffa0 + DUELVARS_ARENA_CARD_FLAGS)] |= 1 << USED_PKMN_POWER_THIS_TURN_F;
	if (hAIPkmnPowerEffectParam == 0)
		return false; /* return if coin was tails */

	u8 *hp = &vars[(u8)(hPlayAreaEffectTarget + DUELVARS_ARENA_CARD_HP)];
	*hp = (u8)(*hp + 10); /* remove 1 damage counter */
	DrawPlayAreaScreenToShowChanges(hPlayAreaEffectTarget);
	ExchangeRNG();
	return false;
}

bool PetalDance_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(120 / 2, 0, 120);
	return false;
}

bool PetalDance_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(40);
	u8 a = TossCoinATimes_BankB(3, DamageCheckIfHeadsXDamageText);
	a = (u8)(a + a);
	a = (u8)(a + a);
	a = ATimes10(a);
	/* a = 4 * 10 * heads */
	SetDefiniteDamage(a);
	SwapTurn();
	bool carry = ConfusionEffect();
	SwapTurn();
	return carry;
}

bool PoisonWhip_AIEffect(void)
{
	TRACE_FN();
	return UpdateExpectedAIDamage_AccountForPoison_Carry(10, 10, 10);
}

bool SolarPower_CheckUse(void)
{
	TRACE_FN();
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	if (GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD_FLAGS)) & USED_PKMN_POWER_THIS_TURN) {
		/* .already_used */
		gTextID = OnlyOncePerTurnText;
		return true;
	}

	if (CheckIsIncapableOfUsingPkmnPower(hTempPlayAreaLocation_ff9d))
		return true; /* can't use PKMN due to status or Toxic Gas */

	/* return carry if none of the Arena cards have status conditions */
	if (GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS) == NO_STATUS &&
	    GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS) == NO_STATUS) {
		/* .no_status */
		gTextID = NotAffectedByPoisonSleepParalysisOrConfusionText;
		return true;
	}
	/* .has_status */
	return false;
}

bool SolarPower_RemoveStatusEffect(void)
{
	TRACE_FN();
	wLoadedAttackAnimation = ATK_ANIM_HEAL_BOTH_SIDES;
	ResetAttackAnimationIsPlaying();
	/* de is whatever the caller of the effect function left in it (it ends up in
	 * wDamageAnimAmount); the port does not model it, so 0 is used */
	PlayAttackAnimation(0, hTempPlayAreaLocation_ff9d, 0, hWhoseTurn);
	WaitAttackAnimation();

	u8 *vars = TurnDuelistVars();
	vars[(u8)(hTemp_ffa0 + DUELVARS_ARENA_CARD_FLAGS)] |= 1 << USED_PKMN_POWER_THIS_TURN_F;
	vars[DUELVARS_ARENA_CARD_STATUS] = NO_STATUS;

	NonTurnDuelistVars()[DUELVARS_ARENA_CARD_STATUS] = NO_STATUS;
	DrawDuelHUDs();
	return false;
}

bool VenusaurMegaDrainEffect(void)
{
	TRACE_FN();
	u16 hl = rd16(wDealtDamage);
	hl >>= 1; /* srl h / rr l */
	if (hl & 1) {
		/* round up to nearest 10 */
		hl = (u16)(hl + 10 / 2);
	}
	/* .rounded */
	ApplyAndAnimateHPRecovery(hl);
	return false;
}

/* applies the damage bonus for attacks that get bonus
 * from extra Water energy cards.
 * this bonus is always 10 more damage for each extra Water energy
 * and is always capped at a maximum of 20 damage.
 * input:
 *	b = number of Water energy cards needed for paying Energy Cost
 *	c = number of colorless energy cards needed for paying Energy Cost */
void ApplyExtraWaterEnergyDamageBonus(u8 b, u8 c)
{
	if (wMetronomeEnergyCost != 0) {
		c = wMetronomeEnergyCost; /* amount of colorless needed for Metronome */
		b = 0;                    /* no Water energy needed for Metronome */
	}

	/* .not_metronome */
	GetPlayAreaCardAttachedEnergies(hTempPlayAreaLocation_ff9d);

	u8 *hl = &wAttachedEnergies[WATER];
	if (c != 0) { /* is Energy cost all water energy? */
		/* it's not, so we need to remove the
		 * Water energy cards from calculations
		 * if they pay for colorless instead. */
		if (wTotalAttachedEnergies == *hl) {
			/* Water is the only energy color attached */
			b = (u8)(c + b); /* b += c */
		}
		/* (skip if at least 1 non-Water energy attached) */
	}

	/* .check_bonus */
	if (*hl > b) { /* (skip if water energy <= b) */
		/* a holds number of water energy not payed for energy cost */
		u8 a = (u8)(*hl - b);
		if (a >= 3)
			a = 2; /* cap this to 2 for bonus effect */
		/* .less_than_3 */
		AddToDamage(ATimes10(a)); /* add 10 * a to damage */
	}

	/* .skip_bonus */
	wAIMinDamage = wDamage[0];
	wAIMaxDamage = wDamage[0];
}
