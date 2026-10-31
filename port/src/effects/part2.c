/*
 * Port of src/engine/duel/effect_functions.asm, part 2 (from OmastarWaterGunEffect
 * up to, not including, StrangeBehavior_SelectAndSwapEffect).
 *
 * Effect functions return the carry flag the original returns with. The effect
 * command dispatcher (TryExecuteEffectCommandFunction) calls them with carry
 * clear (`or a` before CallHL), so a routine that does not touch the flags
 * returns false. Where the original returns with the carry left over by a callee
 * whose C signature is void (ApplyExtraWaterEnergyDamageBonus, UI routines, ...),
 * and the command type's caller ignores it (BEFORE_DAMAGE, AFTER_DAMAGE, AI,
 * AI_SELECTION, PKMN_POWER_TRIGGER), false is returned.
 */
#include "effects.h"
#include "../tcg.h"
#include "part2_needs.h"

/* constants/hardware.inc and the TX_SYMBOL charmap (constants/charmaps.asm) */
#ifndef PAD_B
#define PAD_B 0x02
#endif
#ifndef SYM_SPACE
#define SYM_SPACE 0x00
#endif
#ifndef SYM_CURSOR_R
#define SYM_CURSOR_R 0x0f
#endif
#ifndef SYM_HP_NOK
#define SYM_HP_NOK 0x17
#endif

/* non-static helpers of this part that are not (yet) in effects.h */
bool PlayerPickAttackForAmnesia_Carry(void);
bool EnergyBurnCheck_Unreferenced(void);

/* PlayAreaSelectionMenuParameters (defined in part 1 of effect_functions.asm, not exported) */
static const struct MenuParams PlayAreaSelectionMenuParameters = {
	0, 0,                   /* cursor x, cursor y */
	3,                      /* y displacement between items */
	MAX_PLAY_AREA_POKEMON,  /* number of items */
	SYM_CURSOR_R,           /* cursor tile number */
	SYM_SPACE,              /* tile behind cursor */
	0,                      /* function pointer if non-0 */
	TCG_MENU_PLAY_AREA,
};

bool OmastarWaterGunEffect(void)
{
	TRACE_FN();
	ApplyExtraWaterEnergyDamageBonus(1, 1);
	/* (the carry ApplyExtraWaterEnergyDamageBonus returns with is not exposed by its C signature) */
	return false;
}

bool OmastarSpikeCannon_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(60 / 2, 0, 60);
	return false;
}

bool OmastarSpikeCannon_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(30);
	u8 e = TossCoinATimes_BankB(2, DamageCheckIfHeadsXDamageText);
	u8 a = (u8)(e + e + e);
	SetDefiniteDamage(ATimes10(a)); /* 3 * 10 * heads */
	return false;
}

bool ClairvoyanceEffect(void)
{
	TRACE_FN();
	return true;
}

bool OmanyteWaterGunEffect(void)
{
	TRACE_FN();
	ApplyExtraWaterEnergyDamageBonus(1, 0);
	return false;
}

bool WartortleWithdrawEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(IfHeadsNoDamageNextTurnText)) {
		SetWasUnsuccessful();
		return false;
	}
	wLoadedAttackAnimation = ATK_ANIM_PROTECT;
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_NO_DAMAGE_WITHDRAW);
	return true; /* (ApplySubstatus1ToAttackingCard preserves the carry of the heads coin toss) */
}

bool RainDanceEffect(void)
{
	TRACE_FN();
	return true;
}

bool HydroPumpEffect(void)
{
	TRACE_FN();
	ApplyExtraWaterEnergyDamageBonus(3, 0);
	return false;
}

bool KinglerFlail_AIEffect(void)
{
	TRACE_FN();
	bool c = KinglerFlail_HPCheck();
	SetDefiniteAIDamage(); /* (does not touch the flags) */
	return c;
}

bool KinglerFlail_HPCheck(void)
{
	TRACE_FN();
	SetDefiniteDamage(GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL));
	return false;
}

/* returns carry if no cards in Deck
 * or if Play Area is full already. */
bool KrabbyCallForFamily_CheckDeckAndPlayArea(void)
{
	TRACE_FN();
	if (CheckIfDeckIsEmpty())
		return true; /* return if no cards in deck */
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	gTextID = NoSpaceOnTheBenchText;
	return a >= MAX_PLAY_AREA_POKEMON; /* cp MAX_PLAY_AREA_POKEMON ; ccf */
}

bool KrabbyCallForFamily_PlayerSelectEffect(void)
{
	TRACE_FN();
	hTemp_ffa0 = 0xff;

	CreateDeckCardList(NULL);
	/* (bc = KrabbyText, the name printed if there is none in the deck: LookForCardsInDeck's
	 * C signature has no parameter for it, it is derived from d/e) */
	if (LookForCardsInDeck(SEARCHEFFECT_CARD_ID, KRABBY, ChooseAKrabbyFromDeckText))
		return true;

	/* draw Deck list interface and print text */
	InitAndDrawCardListScreenLayout_WithSelectCheckMenu();
	SetCardListHeaderText(ChooseAKrabbyText, DuelistDeckText);

	for (;;) {
		/* .loop */
		u8 card;
		if (DisplayCardList(&card)) {
			/* .pressed_b
			 * figure if Player can exit the screen without selecting,
			 * that is, if the Deck has no Krabby card. */
			u8 *vars = TurnDuelistVars();
			bool found = false;
			for (u8 l = DUELVARS_CARD_LOCATIONS; l < DECK_SIZE; l++) {
				if (vars[l] != CARD_LOCATION_DECK)
					continue;
				if (GetCardIDFromDeckIndex(l) == KRABBY) {
					found = true; /* found Krabby, go back to top loop */
					break;
				}
			}
			if (!found) {
				/* no Krabby in Deck, can safely exit screen */
				hTemp_ffa0 = 0xff;
				return false;
			}
		} else if (GetCardIDFromDeckIndex(card) == KRABBY) {
			/* Krabby was selected */
			hTemp_ffa0 = hTempCardIndex_ff98;
			return false;
		}
		/* .play_sfx: play SFX and loop back */
		PlaySFX_InvalidChoice();
	}
}

bool KrabbyCallForFamily_AISelectEffect(void)
{
	TRACE_FN();
	CreateDeckCardList(NULL);
	u8 *hl = wDuelTempList;
	for (;;) {
		u8 a = *hl++;
		hTemp_ffa0 = a;
		if (a == 0xff)
			return false; /* no Krabby */
		if (GetCardIDFromDeckIndex(a) == KRABBY)
			return false; /* Krabby found */
	}
}

bool KrabbyCallForFamily_PutInPlayAreaEffect(void)
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

bool MagikarpFlail_AIEffect(void)
{
	TRACE_FN();
	bool c = MagikarpFlail_HPCheck();
	SetDefiniteAIDamage();
	return c;
}

bool MagikarpFlail_HPCheck(void)
{
	TRACE_FN();
	SetDefiniteDamage(GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL));
	return false;
}

bool HeadacheEffect(void)
{
	TRACE_FN();
	NonTurnDuelistVars()[DUELVARS_ARENA_CARD_SUBSTATUS3] |= 1 << SUBSTATUS3_HEADACHE_F;
	return false;
}

bool PsyduckFurySwipes_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(30 / 2, 0, 30);
	return false;
}

bool PsyduckFurySwipes_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(10);
	u8 a = TossCoinATimes_BankB(3, DamageCheckIfHeadsXDamageText);
	SetDefiniteDamage(ATimes10(a));
	return false;
}

bool GolduckHyperBeam_PlayerSelectEffect(void)
{
	TRACE_FN();
	SwapTurn();
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	if (wTotalAttachedEnergies == 0) {
		/* .no_energy */
		SwapTurn();
		hTemp_ffa0 = (u8)-1;
		return false;
	}

	/* draw Energy Card list screen */
	DrawWideTextBox_WaitForInput(ChooseDiscardEnergyCardFromOpponentText);
	CreateArenaOrBenchEnergyCardList(PLAY_AREA_ARENA);
	DisplayEnergyDiscardScreen(PLAY_AREA_ARENA);

	while (HandleEnergyDiscardMenuInput()) {
		/* .loop_input */
	}

	SwapTurn();
	hTemp_ffa0 = hTempCardIndex_ff98; /* store selected card to discard */
	return false;
}

bool GolduckHyperBeam_AISelectEffect(void)
{
	TRACE_FN();
	hTemp_ffa0 = AIPickEnergyCardToDiscardFromDefendingPokemon();
	return false;
}

bool GolduckHyperBeam_DiscardEffect(void)
{
	TRACE_FN();
	if (HandleNoDamageOrEffect())
		return true; /* return if attack had no effect */

	/* check if energy card was chosen to discard */
	u8 a = hTemp_ffa0;
	if (a == (u8)-1)
		return false; /* return if none selected */

	/* discard Defending card's energy */
	SwapTurn();
	PutCardInDiscardPile(a);
	TurnDuelistVars()[DUELVARS_ARENA_CARD_LAST_TURN_EFFECT] = LAST_TURN_EFFECT_DISCARD_ENERGY;
	SwapTurn();
	return true; /* (carry of `cp -1` with a < $ff, preserved by the calls above) */
}

bool SeadraWaterGunEffect(void)
{
	TRACE_FN();
	ApplyExtraWaterEnergyDamageBonus(1, 1);
	return false;
}

bool SeadraAgilityEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(IfHeadsDoNotReceiveDamageOrEffectText))
		return false; /* return if tails */
	wLoadedAttackAnimation = ATK_ANIM_AGILITY_PROTECT;
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_AGILITY);
	return true;
}

bool ShellderSupersonicEffect(void)
{
	TRACE_FN();
	bool c = Confusion50PercentEffect();
	if (!c)
		SetNoEffectFromStatus();
	return c;
}

bool HideInShellEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(IfHeadsNoDamageNextTurnText)) {
		SetWasUnsuccessful();
		return false;
	}
	wLoadedAttackAnimation = ATK_ANIM_PROTECT;
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_NO_DAMAGE_HIDE_IN_SHELL);
	return true;
}

bool VaporeonQuickAttack_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage((10 + 30) / 2, 10, 30);
	return false;
}

bool VaporeonQuickAttack_DamageBoostEffect(void)
{
	TRACE_FN();
	LoadTxRam3(20);
	if (!TossCoin_BankB(DamageCheckIfHeadsPlusDamageText))
		return false; /* return if tails */
	AddToDamage(20);
	return false;
}

bool VaporeonWaterGunEffect(void)
{
	TRACE_FN();
	ApplyExtraWaterEnergyDamageBonus(2, 1);
	return false;
}

/* returns carry if Arena card has no Water Energy attached
 * or if it doesn't have any damage counters. */
bool StarmieRecover_CheckEnergyHP(void)
{
	TRACE_FN();
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	u8 a = wAttachedEnergies[WATER];
	gTextID = NotEnoughWaterEnergyText;
	if (a < 1)
		return true; /* return if not enough energy */
	a = GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL);
	gTextID = NoDamageCountersText;
	return a < 10; /* return carry if no damage */
}

bool StarmieRecover_PlayerSelectEffect(void)
{
	TRACE_FN();
	CreateListOfEnergyAttachedToArena(TYPE_ENERGY_WATER);
	DisplayEnergyDiscardScreen(PLAY_AREA_ARENA);
	while (HandleEnergyDiscardMenuInput()) {
		/* .loop_input */
	}
	hTemp_ffa0 = hTempCardIndex_ff98; /* store card chosen */
	return false;
}

bool StarmieRecover_AISelectEffect(void)
{
	TRACE_FN();
	CreateListOfEnergyAttachedToArena(TYPE_ENERGY_WATER);
	hTemp_ffa0 = wDuelTempList[0]; /* pick first card */
	return false;
}

bool StarmieRecover_DiscardEffect(void)
{
	TRACE_FN();
	PutCardInDiscardPile(hTemp_ffa0);
	return false;
}

bool StarmieRecover_HealEffect(void)
{
	TRACE_FN();
	u8 e = GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL); /* all damage for recovery */
	ApplyAndAnimateHPRecovery(e);
	return false;
}

bool SquirtleWithdrawEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(IfHeadsNoDamageNextTurnText)) {
		SetWasUnsuccessful();
		return false;
	}
	wLoadedAttackAnimation = ATK_ANIM_PROTECT;
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_NO_DAMAGE_WITHDRAW);
	return true;
}

bool HorseaSmokescreenEffect(void)
{
	TRACE_FN();
	ApplySubstatus2ToDefendingCard(SUBSTATUS2_SMOKESCREEN);
	return false;
}

bool TentacruelSupersonicEffect(void)
{
	TRACE_FN();
	bool c = Confusion50PercentEffect();
	if (!c)
		SetNoEffectFromStatus();
	return c;
}

bool JellyfishSting_AIEffect(void)
{
	TRACE_FN();
	UpdateExpectedAIDamage_AccountForPoison(10, 10, 10);
	return false;
}

/* returns carry if Defending Pokemon has no attacks */
bool PoliwhirlAmnesia_CheckAttacks(void)
{
	TRACE_FN();
	SwapTurn();
	LoadCardDataToBuffer2_FromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	if (wLoadedCard2Atk1Category == POKEMON_POWER &&
	    (wLoadedCard2Atk2Name[0] | wLoadedCard2Atk2Name[1]) == 0) {
		/* has no attack */
		SwapTurn();
		gTextID = NoAttackMayBeChoosenText;
		return true;
	}
	/* .has_attack */
	SwapTurn();
	return false;
}

bool PoliwhirlAmnesia_PlayerSelectEffect(void)
{
	TRACE_FN();
	return PlayerPickAttackForAmnesia_Carry();
}

bool PoliwhirlAmnesia_AISelectEffect(void)
{
	TRACE_FN();
	hTemp_ffa0 = AIPickAttackForAmnesia();
	return false;
}

bool PoliwhirlAmnesia_DisableEffect(void)
{
	TRACE_FN();
	ApplyAmnesiaToAttack();
	return false;
}

/* PlayerPickAttackForAmnesia, returning its carry: set if the Player pressed B
 * (HandleDefendingPokemonAttackSelection; `ld a, e / ldh [hTemp_ffa0], a` keep the flags).
 * Callers that return it as an INITIAL_EFFECT_2 result need it (the attack is cancelled). */
bool PlayerPickAttackForAmnesia_Carry(void)
{
	DrawWideTextBox_WaitForInput(ChooseAttackOpponentWillNotBeAbleToUseText);
	/* (on carry the original stores whatever register e holds after the menu loop;
	 * it cannot be reproduced, 0 is used if the callee leaves e untouched) */
	u8 d = 0, e = 0;
	bool c = HandleDefendingPokemonAttackSelection(&d, &e);
	hTemp_ffa0 = e;
	return c;
}

void PlayerPickAttackForAmnesia(void)
{
	PlayerPickAttackForAmnesia_Carry();
}

/* applies the Amnesia effect on the defending Pokemon,
 * for the attack index in hTemp_ffa0. */
void ApplyAmnesiaToAttack(void)
{
	ApplySubstatus2ToDefendingCard(SUBSTATUS2_AMNESIA);
	if (wNoDamageOrEffect)
		return; /* no effect */

	/* set selected attack as disabled */
	u8 *vars = NonTurnDuelistVars();
	vars[DUELVARS_ARENA_CARD_DISABLED_ATTACK_INDEX] = hTemp_ffa0;
	vars[DUELVARS_ARENA_CARD_LAST_TURN_EFFECT] = LAST_TURN_EFFECT_AMNESIA;

	if (IsPlayerTurn())
		return; /* return if Player */

	/* the rest of the routine if for Opponent
	 * to announce which attack was used for Amnesia. */
	SwapTurn();
	u8 d = GetTurnDuelistVariable(DUELVARS_ARENA_CARD);
	LoadTxRam2(GetAttackName(d, hTemp_ffa0));
	DrawWideTextBox_WaitForInput(WasChosenForTheEffectOfAmnesiaText);
	SwapTurn();
}

bool PoliwhirlDoubleslap_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(60 / 2, 0, 60);
	return false;
}

bool PoliwhirlDoubleslap_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(30);
	u8 e = TossCoinATimes_BankB(2, DamageCheckIfHeadsXDamageText);
	u8 a = (u8)(e + e + e);
	SetDefiniteDamage(ATimes10(a));
	return false;
}

bool PoliwrathWaterGunEffect(void)
{
	TRACE_FN();
	ApplyExtraWaterEnergyDamageBonus(2, 1);
	return false;
}

bool Whirlpool_PlayerSelectEffect(void)
{
	TRACE_FN();
	SwapTurn();
	if (CreateArenaOrBenchEnergyCardList(PLAY_AREA_ARENA)) {
		/* .no_energy */
		SwapTurn();
		hTemp_ffa0 = 0xff;
		return true; /* (carry of CreateArenaOrBenchEnergyCardList is kept) */
	}

	DrawWideTextBox_WaitForInput(ChooseDiscardEnergyCardFromOpponentText);
	DisplayEnergyDiscardScreen(PLAY_AREA_ARENA);
	while (HandleEnergyDiscardMenuInput()) {
		/* .loop_input */
	}

	SwapTurn();
	hTemp_ffa0 = hTempCardIndex_ff98; /* store selected card to discard */
	return false;
}

bool Whirlpool_AISelectEffect(void)
{
	TRACE_FN();
	hTemp_ffa0 = AIPickEnergyCardToDiscardFromDefendingPokemon();
	return false;
}

bool Whirlpool_DiscardEffect(void)
{
	TRACE_FN();
	if (HandleNoDamageOrEffect())
		return true; /* return if attack had no effect */
	u8 a = hTemp_ffa0;
	if (a == 0xff)
		return false; /* return if none selected */

	/* discard Defending card's energy
	 * this doesn't update DUELVARS_ARENA_CARD_LAST_TURN_EFFECT */
	SwapTurn();
	PutCardInDiscardPile(a);
	/* ld a, DUELVARS_ARENA_CARD_LAST_TURN_EFFECT
	 * call GetTurnDuelistVariable
	 * ld [hl], LAST_TURN_EFFECT_DISCARD_ENERGY */
	SwapTurn();
	return true; /* (carry of `cp $ff` with a < $ff) */
}

bool PoliwagWaterGunEffect(void)
{
	TRACE_FN();
	ApplyExtraWaterEnergyDamageBonus(1, 0);
	return false;
}

bool ClampEffect(void)
{
	TRACE_FN();
	wLoadedAttackAnimation = ATK_ANIM_HIT_EFFECT;
	if (TossCoin_BankB(SuccessCheckIfHeadsAttackIsSuccessfulText))
		return ParalysisEffect();
	/* unsuccessful */
	wLoadedAttackAnimation = ATK_ANIM_NONE;
	SetDefiniteDamage(0);
	SetWasUnsuccessful();
	return false;
}

bool CloysterSpikeCannon_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(60 / 2, 0, 60);
	return false;
}

bool CloysterSpikeCannon_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(30);
	u8 e = TossCoinATimes_BankB(2, DamageCheckIfHeadsXDamageText);
	u8 a = (u8)(e + e + e);
	SetDefiniteDamage(ATimes10(a));
	return false;
}

bool Blizzard_BenchDamage50PercentEffect(void)
{
	TRACE_FN();
	bool c = TossCoin_BankB(DamageToOppBenchIfHeadsDamageToYoursIfTailsText);
	hTemp_ffa0 = c ? 1 : 0; /* store coin result (a = number of heads) */
	return c;
}

bool Blizzard_BenchDamageEffect(void)
{
	TRACE_FN();
	if (hTemp_ffa0 == 0) {
		/* own bench */
		wIsDamageToSelf = TRUE;
		DealDamageToAllBenchedPokemon(10);
		return false;
	}
	/* .opp_bench */
	SwapTurn();
	DealDamageToAllBenchedPokemon(10);
	SwapTurn();
	return false;
}

/* return carry if can't use Cowardice */
bool Cowardice_CheckUseAndBench(void)
{
	TRACE_FN();
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	if (CheckIsIncapableOfUsingPkmnPower(hTempPlayAreaLocation_ff9d))
		return true; /* return if cannot use */

	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	gTextID = EffectNoPokemonOnTheBenchText;
	if (a < 2)
		return true; /* return if no bench */

	a = GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD_FLAGS));
	gTextID = CannotBeUsedInTurnWhichWasPlayedText;
	if (!(a & CAN_EVOLVE_THIS_TURN))
		return true; /* return if was played this turn */

	return false;
}

bool Cowardice_PlayerSelectEffect(void)
{
	TRACE_FN();
	if (hTemp_ffa0 != 0)
		return false; /* return if not Arena card */
	DrawWideTextBox_WaitForInput(SelectPokemonToPlaceInTheArenaText);
	u8 count;
	HasAlivePokemonInBench(&count);
	bool c = OpenPlayAreaScreenForSelection();
	hAIPkmnPowerEffectParam = hTempPlayAreaLocation_ff9d;
	return c;
}

bool Cowardice_ReturnToHandEffect(void)
{
	TRACE_FN();
	u8 card = GetTurnDuelistVariable((u8)(hTemp_ffa0 + DUELVARS_ARENA_CARD));

	/* put card in Discard Pile temporarily, so that
	 * all cards attached are discarded as well. */
	MovePlayAreaCardToDiscardPile(hTemp_ffa0);

	/* if card was in Arena, swap selected Bench
	 * Pokemon with Arena, otherwise skip. */
	if (hTemp_ffa0 == 0)
		SwapArenaWithBenchPokemon(hAIPkmnPowerEffectParam);

	/* .skip_switch
	 * move card back to Hand from Discard Pile
	 * and adjust Play Area */
	AddCardToHand(MoveDiscardPileCardToHand(card));
	ShiftAllPokemonToFirstPlayAreaSlots();

	wDuelDisplayedScreen = 0;
	return false;
}

bool LaprasWaterGunEffect(void)
{
	TRACE_FN();
	ApplyExtraWaterEnergyDamageBonus(1, 0);
	return false;
}

bool Quickfreeze_InitialEffect(void)
{
	TRACE_FN();
	return true;
}

bool Quickfreeze_Paralysis50PercentEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(ParalysisCheckText)) {
		/* tails */
		SetWasUnsuccessful();
		DrawDuelMainScene();
		PrintFailedEffectText();
		WaitForWideTextBoxInput();
		return false;
	}

	/* .heads */
	/* register de for PlayAttackAnimation is what ParalysisEffect leaves in it: when the
	 * status is queued, QueueStatusCondition loads de = [wStatusConditionQueueIndex] (before
	 * advancing it). (when it isn't queued, de is left over from the coin toss/power checks
	 * and cannot be reproduced; the same value is used.) */
	u16 de = wStatusConditionQueueIndex;
	ParalysisEffect();
	PlayAttackAnimation(de, hTempPlayAreaLocation_ff9d, 0x00, hWhoseTurn);
	PlayStatusConditionQueueAnimations();
	WaitAttackAnimation();
	ApplyStatusConditionQueue();
	DrawDuelHUDs();
	if (PrintFailedEffectText())
		WaitForWideTextBoxInput();
	return false;
}

bool IceBreath_ZeroDamage(void)
{
	TRACE_FN();
	SetDefiniteDamage(0);
	return false;
}

bool IceBreath_RandomPokemonDamageEffect(void)
{
	TRACE_FN();
	SwapTurn();
	u8 b = PickRandomPlayAreaCard();
	DealDamageToPlayAreaPokemon_RegularAnim(b, 40);
	SwapTurn();
	return false;
}

bool FocusEnergyEffect(void)
{
	TRACE_FN();
	u8 a = wTempTurnDuelistCardID;
	if (a != VAPOREON_LV29)
		return a < VAPOREON_LV29; /* return if no VaporeonLv29 (carry of the cp) */
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_NEXT_TURN_DOUBLE_DAMAGE);
	return false;
}

/* returns carry if B was pressed (no loop: the selection is cancelled) */
static bool PlayerPickFireEnergyCardToDiscard(void)
{
	CreateListOfFireEnergyAttachedToArena();
	DisplayEnergyDiscardScreen(PLAY_AREA_ARENA);
	bool c = HandleEnergyDiscardMenuInput();
	hTemp_ffa0 = hTempCardIndex_ff98;
	return c;
}

static bool AIPickFireEnergyCardToDiscard(void)
{
	CreateListOfFireEnergyAttachedToArena();
	hTemp_ffa0 = wDuelTempList[0]; /* pick first in list */
	return false;
}

/* returns carry if Arena card has no Fire Energy cards */
bool ArcanineFlamethrower_CheckEnergy(void)
{
	TRACE_FN();
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	u8 a = wAttachedEnergies[FIRE];
	gTextID = NotEnoughFireEnergyText;
	return a < 1;
}

bool ArcanineFlamethrower_PlayerSelectEffect(void)
{
	TRACE_FN();
	return PlayerPickFireEnergyCardToDiscard();
}

bool ArcanineFlamethrower_AISelectEffect(void)
{
	TRACE_FN();
	return AIPickFireEnergyCardToDiscard();
}

bool ArcanineFlamethrower_DiscardEffect(void)
{
	TRACE_FN();
	PutCardInDiscardPile(hTemp_ffa0);
	return false;
}

bool TakeDownEffect(void)
{
	TRACE_FN();
	DealRecoilDamageToSelf(30);
	return false;
}

bool ArcanineQuickAttack_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage((10 + 30) / 2, 10, 30);
	return false;
}

bool ArcanineQuickAttack_DamageBoostEffect(void)
{
	TRACE_FN();
	LoadTxRam3(20);
	if (!TossCoin_BankB(DamageCheckIfHeadsPlusDamageText))
		return false; /* return if tails */
	AddToDamage(20);
	return false;
}

/* return carry if has less than 2 Fire Energy cards */
bool FlamesOfRage_CheckEnergy(void)
{
	TRACE_FN();
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	u8 a = wAttachedEnergies[FIRE];
	gTextID = NotEnoughFireEnergyText;
	return a < 2;
}

bool FlamesOfRage_PlayerSelectEffect(void)
{
	TRACE_FN();
	DrawWideTextBox_WaitForInput(ChooseAndDiscard2FireEnergyCardsText);

	hCurSelectionItem = 0;
	CreateListOfFireEnergyAttachedToArena();
	DisplayEnergyDiscardScreen(PLAY_AREA_ARENA);
	for (;;) {
		/* .loop_input */
		if (HandleEnergyDiscardMenuInput())
			return true;
		u8 *hl = GetNextPositionInTempList();
		*hl = hTempCardIndex_ff98;
		RemoveCardFromDuelTempList(hTempCardIndex_ff98);
		if (hCurSelectionItem >= 2)
			return false; /* return when 2 have been chosen */
		DisplayEnergyDiscardMenu();
	}
}

bool FlamesOfRage_AISelectEffect(void)
{
	TRACE_FN();
	AIPickFireEnergyCardToDiscard();
	hTempList[1] = wDuelTempList[1];
	return false;
}

bool FlamesOfRage_DiscardEffect(void)
{
	TRACE_FN();
	PutCardInDiscardPile(hTempList[0]);
	PutCardInDiscardPile(hTempList[1]);
	return false;
}

bool FlamesOfRage_AIEffect(void)
{
	TRACE_FN();
	bool c = FlamesOfRage_DamageBoostEffect();
	SetDefiniteAIDamage();
	return c;
}

bool FlamesOfRage_DamageBoostEffect(void)
{
	TRACE_FN();
	AddToDamage(GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL));
	return false;
}

bool RapidashStomp_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage((20 + 30) / 2, 20, 30);
	return false;
}

bool RapidashStomp_DamageBoostEffect(void)
{
	TRACE_FN();
	LoadTxRam3(10);
	if (!TossCoin_BankB(DamageCheckIfHeadsPlusDamageText))
		return false; /* return if tails */
	AddToDamage(10);
	return false;
}

bool RapidashAgilityEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(IfHeadsDoNotReceiveDamageOrEffectText))
		return false; /* return if tails */
	wLoadedAttackAnimation = ATK_ANIM_AGILITY_PROTECT;
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_AGILITY);
	return true;
}

/* returns carry if Opponent has no Pokemon in bench */
bool NinetalesLure_CheckBench(void)
{
	TRACE_FN();
	u8 a = GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	gTextID = EffectNoPokemonOnTheBenchText;
	return a < 2;
}

bool NinetalesLure_PlayerSelectEffect(void)
{
	TRACE_FN();
	DrawWideTextBox_WaitForInput(SelectPkmnOnBenchToSwitchWithActiveText);
	SwapTurn();
	u8 count;
	HasAlivePokemonInBench(&count);
	while (OpenPlayAreaScreenForSelection()) {
		/* .loop_input */
	}
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	SwapTurn();
	return false;
}

bool NinetalesLure_AISelectEffect(void)
{
	TRACE_FN();
	hTemp_ffa0 = AIFindTargetForBenchAttack();
	return false;
}

bool NinetalesLure_SwitchEffect(void)
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

/* return carry if no Fire energy cards */
bool FireBlast_CheckEnergy(void)
{
	TRACE_FN();
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	gTextID = NotEnoughFireEnergyText;
	return wAttachedEnergies[FIRE] < 1;
}

bool FireBlast_PlayerSelectEffect(void)
{
	TRACE_FN();
	return PlayerPickFireEnergyCardToDiscard();
}

bool FireBlast_AISelectEffect(void)
{
	TRACE_FN();
	return AIPickFireEnergyCardToDiscard();
}

bool FireBlast_DiscardEffect(void)
{
	TRACE_FN();
	PutCardInDiscardPile(hTemp_ffa0);
	return false;
}

/* return carry if no Fire energy cards */
bool Ember_CheckEnergy(void)
{
	TRACE_FN();
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	gTextID = NotEnoughFireEnergyText;
	return wAttachedEnergies[FIRE] < 1;
}

bool Ember_PlayerSelectEffect(void)
{
	TRACE_FN();
	return PlayerPickFireEnergyCardToDiscard();
}

bool Ember_AISelectEffect(void)
{
	TRACE_FN();
	return AIPickFireEnergyCardToDiscard();
}

bool Ember_DiscardEffect(void)
{
	TRACE_FN();
	PutCardInDiscardPile(hTemp_ffa0);
	return false;
}

/* return carry if no Fire energy cards */
bool Wildfire_CheckEnergy(void)
{
	TRACE_FN();
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	gTextID = NotEnoughFireEnergyText;
	return wAttachedEnergies[FIRE] < 1;
}

bool Wildfire_PlayerSelectEffect(void)
{
	TRACE_FN();
	DrawWideTextBox_WaitForInput(DiscardOppDeckAsManyFireEnergyCardsText);

	hCurSelectionItem = 0;
	CreateListOfFireEnergyAttachedToArena();
	DisplayEnergyDiscardScreen(PLAY_AREA_ARENA);

	/* show list to Player and for each card selected to discard,
	 * just increase a counter and store it.
	 * this will be the output used by Wildfire_DiscardEnergyEffect. */
	wEnergyDiscardMenuDenominator = 0;
	for (;;) {
		/* .loop */
		wEnergyDiscardMenuNumerator = hCurSelectionItem;
		if (HandleEnergyDiscardMenuInput())
			break;
		hCurSelectionItem++;
		/* (a = hTempCardIndex_ff98, left by HandleEnergyDiscardMenuInput) */
		if (RemoveCardFromDuelTempList(hTempCardIndex_ff98))
			break;
		DisplayEnergyDiscardMenu();
	}

	/* .done
	 * return carry if no cards were discarded
	 * output the result in hTemp_ffa0 */
	hTemp_ffa0 = hCurSelectionItem;
	return hCurSelectionItem == 0;
}

bool Wildfire_AISelectEffect(void)
{
	TRACE_FN();
	/* AI always chooses 0 cards to discard */
	hTempList[0] = 0;
	return false;
}

bool Wildfire_DiscardEnergyEffect(void)
{
	TRACE_FN();
	CreateListOfFireEnergyAttachedToArena();
	u8 c = hTemp_ffa0;
	if (c == 0)
		return false; /* no cards to discard */

	/* discard cards from wDuelTempList equal to the number
	 * of cards that were input in hTemp_ffa0.
	 * these are all the Fire Energy cards attached to Arena card
	 * so it will discard the cards in order, regardless
	 * of the actual order that was selected by Player. */
	u8 *hl = wDuelTempList;
	do {
		PutCardInDiscardPile(*hl++);
	} while (--c);
	return false;
}

bool Wildfire_DiscardDeckEffect(void)
{
	TRACE_FN();
	u8 c = hTemp_ffa0;
	u8 b = 0x00;
	SwapTurn();
	u8 a = (u8)(DECK_SIZE - TurnDuelistVars()[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK]);
	if (a < c) {
		/* only discard number of cards that are left in deck */
		c = a;
	}

	/* .start_discard */
	u16 bc = (u16)(b << 8 | c); /* push bc */
	c++;
	while (--c) {
		/* discard top card from deck */
		u8 card;
		if (!DrawCardFromDeck(&card))
			PutCardInDiscardPile(card);
	}

	LoadTxRam3(bc); /* pop hl */
	DrawWideTextBox_PrintText(DiscardedCardsFromDeckText);
	SwapTurn();
	return false;
}

bool MoltresLv35DiveBomb_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(80 / 2, 0, 80);
	return false;
}

bool MoltresLv35DiveBomb_Success50PercentEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(SuccessCheckIfHeadsAttackIsSuccessfulText)) {
		/* tails */
		SetDefiniteDamage(0);
		SetWasUnsuccessful();
		return false;
	}
	/* .heads */
	wLoadedAttackAnimation = ATK_ANIM_DIVE_BOMB;
	return true;
}

bool FlareonQuickAttack_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage((10 + 30) / 2, 10, 30);
	return false;
}

bool FlareonQuickAttack_DamageBoostEffect(void)
{
	TRACE_FN();
	LoadTxRam3(20);
	if (!TossCoin_BankB(DamageCheckIfHeadsPlusDamageText))
		return false; /* return if tails */
	AddToDamage(20);
	return false;
}

/* return carry if no Fire Energy attached */
bool FlareonFlamethrower_CheckEnergy(void)
{
	TRACE_FN();
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	gTextID = NotEnoughFireEnergyText;
	return wAttachedEnergies[FIRE] < 1;
}

bool FlareonFlamethrower_PlayerSelectEffect(void)
{
	TRACE_FN();
	return PlayerPickFireEnergyCardToDiscard();
}

bool FlareonFlamethrower_AISelectEffect(void)
{
	TRACE_FN();
	return AIPickFireEnergyCardToDiscard();
}

bool FlareonFlamethrower_DiscardEffect(void)
{
	TRACE_FN();
	PutCardInDiscardPile(hTemp_ffa0);
	return false;
}

/* return carry if no Fire Energy attached */
bool MagmarFlamethrower_CheckEnergy(void)
{
	TRACE_FN();
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	gTextID = NotEnoughFireEnergyText;
	return wAttachedEnergies[FIRE] < 1;
}

bool MagmarFlamethrower_PlayerSelectEffect(void)
{
	TRACE_FN();
	return PlayerPickFireEnergyCardToDiscard();
}

bool MagmarFlamethrower_AISelectEffect(void)
{
	TRACE_FN();
	return AIPickFireEnergyCardToDiscard();
}

bool MagmarFlamethrower_DiscardEffect(void)
{
	TRACE_FN();
	PutCardInDiscardPile(hTemp_ffa0);
	return false;
}

bool MagmarSmokescreenEffect(void)
{
	TRACE_FN();
	ApplySubstatus2ToDefendingCard(SUBSTATUS2_SMOKESCREEN);
	return false;
}

bool MagmarSmog_AIEffect(void)
{
	TRACE_FN();
	UpdateExpectedAIDamage_AccountForPoison(10 / 2, 0, 10);
	return false;
}

/* return carry if no Fire Energy attached */
bool CharmeleonFlamethrower_CheckEnergy(void)
{
	TRACE_FN();
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	gTextID = NotEnoughFireEnergyText;
	return wAttachedEnergies[FIRE] < 1;
}

bool CharmeleonFlamethrower_PlayerSelectEffect(void)
{
	TRACE_FN();
	return PlayerPickFireEnergyCardToDiscard();
}

bool CharmeleonFlamethrower_AISelectEffect(void)
{
	TRACE_FN();
	return AIPickFireEnergyCardToDiscard();
}

bool CharmeleonFlamethrower_DiscardEffect(void)
{
	TRACE_FN();
	PutCardInDiscardPile(hTemp_ffa0);
	return false;
}

bool EnergyBurnEffect(void)
{
	TRACE_FN();
	return true;
}

/* return carry if has less than 2 Fire Energy cards */
bool FireSpin_CheckEnergy(void)
{
	TRACE_FN();
	CreateArenaOrBenchEnergyCardList(PLAY_AREA_ARENA);
	u8 a = CountCardsInDuelTempList();
	gTextID = NotEnoughEnergyCardsText;
	return a < 2;
}

bool FireSpin_PlayerSelectEffect(void)
{
	TRACE_FN();
	DrawWideTextBox_WaitForInput(ChooseAndDiscard2EnergyCardsText);

	hCurSelectionItem = 0;
	CreateArenaOrBenchEnergyCardList(PLAY_AREA_ARENA);
	SortCardsInDuelTempListByID();
	DisplayEnergyDiscardScreen(PLAY_AREA_ARENA);

	wEnergyDiscardMenuDenominator = 2;
	for (;;) {
		/* .loop_input */
		if (HandleEnergyDiscardMenuInput())
			return true;
		u8 *hl = GetNextPositionInTempList();
		*hl = hTempCardIndex_ff98;
		wEnergyDiscardMenuNumerator++;
		if (hCurSelectionItem >= 2)
			break;
		RemoveCardFromDuelTempList(hTempCardIndex_ff98);
		DisplayEnergyDiscardMenu();
	}
	/* .done: return when 2 have been chosen */
	return false;
}

bool FireSpin_AISelectEffect(void)
{
	TRACE_FN();
	bool c = CreateArenaOrBenchEnergyCardList(PLAY_AREA_ARENA);
	hTempList[0] = wDuelTempList[0];
	hTempList[1] = wDuelTempList[1];
	return c;
}

bool FireSpin_DiscardEffect(void)
{
	TRACE_FN();
	PutCardInDiscardPile(hTempList[0]);
	PutCardInDiscardPile(hTempList[1]);
	return false;
}

/* returns carry if Pkmn Power cannot be used
 * or if Arena card is not Charizard.
 * unreferenced */
bool EnergyBurnCheck_Unreferenced(void)
{
	if (CheckIsIncapableOfUsingPkmnPower(PLAY_AREA_ARENA))
		return true;
	u8 a = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	if (a != CHARIZARD)
		return true; /* .not_charizard */
	return false;
}

bool FlareonRage_AIEffect(void)
{
	TRACE_FN();
	bool c = FlareonRage_DamageBoostEffect();
	SetDefiniteAIDamage();
	return c;
}

bool FlareonRage_DamageBoostEffect(void)
{
	TRACE_FN();
	AddToDamage(GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL));
	return false;
}

/* returns carry if card index in a is Pkmn card */
static bool MixUpEffect_CheckIfCardIsPkmnCard(u8 card)
{
	LoadCardDataToBuffer2_FromDeckIndex(card);
	return wLoadedCard2Type < TYPE_ENERGY;
}

bool MixUpEffect(void)
{
	TRACE_FN();
	SwapTurn();
	CreateHandCardList(NULL);
	SortCardsInDuelTempListByID();

	/* first go through Hand to place
	 * all Pkmn cards in it in the Deck. */
	u8 *hl = wDuelTempList;
	u8 c = 0;
	for (; *hl != 0xff; hl++) {
		/* .loop_hand */
		if (!MixUpEffect_CheckIfCardIsPkmnCard(*hl))
			continue;
		/* found Pkmn card, place in deck */
		c++;
		RemoveCardFromHand(*hl);
		ReturnCardToDeck(*hl);
	}

	/* .done_hand */
	hCurSelectionItem = c;
	DrawWideTextBox_WaitForInput(ThePkmnCardsInHandAndDeckWereShuffledText);

	ShuffleCardsInDeck();
	CreateDeckCardList(NULL);
	if (hCurSelectionItem != 0) {
		/* c holds the number of cards that were placed in the Deck.
		 * now pick Pkmn cards from the Deck to place in Hand. */
		hl = wDuelTempList;
		do {
			/* .loop_deck */
			if (MixUpEffect_CheckIfCardIsPkmnCard(*hl)) {
				c--;
				SearchCardInDeckAndAddToHand(*hl);
				AddCardToHand(*hl);
			}
			/* .next_deck_card */
			hl++;
		} while (c != 0);
	}
	/* .done */
	SwapTurn();
	return false;
}

bool DancingEmbers_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(80 / 2, 0, 80);
	return false;
}

bool DancingEmbers_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(10);
	u8 a = TossCoinATimes_BankB(8, DamageCheckIfHeadsXDamageText);
	SetDefiniteDamage(ATimes10(a));
	return false;
}

bool Firegiver_InitialEffect(void)
{
	TRACE_FN();
	return true;
}

bool Firegiver_AddToHandEffect(void)
{
	TRACE_FN();
	/* fill wDuelTempList with all Fire Energy card
	 * deck indices that are in the Deck. */
	u8 *vars = TurnDuelistVars();
	u8 *de = wDuelTempList;
	u8 c = 0;
	for (u8 l = DUELVARS_CARD_LOCATIONS; l < DECK_SIZE; l++) {
		/* .loop_cards */
		if (vars[l] != CARD_LOCATION_DECK)
			continue;
		if (GetCardType(GetCardIDFromDeckIndex(l)) != TYPE_ENERGY_FIRE)
			continue;
		*de++ = l;
		c++;
	}
	*de = 0xff;

	/* check how many were found */
	if (c == 0) {
		/* return if none found */
		DrawWideTextBox_WaitForInput(ThereWasNoFireEnergyText);
		ShuffleCardsInDeck();
		return false;
	}

	/* .found
	 * pick a random number between 1 and 4,
	 * up to the maximum number of Fire Energy
	 * cards that were found. */
	u8 a = (u8)(Random(4) + 1);
	if (!(a < c))
		a = c;

	/* .ok */
	hCurSelectionItem = a;
	/* load correct attack animation depending
	 * on what side the effect is from. */
	u8 d = ATK_ANIM_FIREGIVER_PLAYER;
	if (wDuelistType != DUELIST_TYPE_PLAYER) {
		/* opponent */
		d = ATK_ANIM_FIREGIVER_OPP;
	}
	/* .player_1 */
	wLoadedAttackAnimation = d;

	/* start loop for adding Energy cards to hand */
	c = hCurSelectionItem;
	/* register e still holds the low byte of the address of the list terminator */
	u8 e = (u8)gbaddr(de);
	u8 *hl = wDuelTempList;
	do {
		/* .loop_energy */
		PlayAttackAnimation((u16)(d << 8 | e), PLAY_AREA_ARENA, 0x0, hWhoseTurn);
		WaitAttackAnimation();

		/* load correct coordinates to update the number of cards
		 * in hand and deck during animation. */
		u8 x = 18, y = 7; /* x, y for hand number */
		e = 3; /* y for deck number */
		if (wLoadedAttackAnimation != ATK_ANIM_FIREGIVER_PLAYER) {
			x = 4;
			y = 5; /* x, y for hand number */
			e = 10; /* y for deck number */
		}

		/* .player_2
		 * update and print number of cards in hand */
		a = (u8)(GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_IN_HAND) + 1);
		WriteTwoDigitNumberInTxSymbol_PadSpace(a, x, y);
		/* update and print number of cards in deck */
		a = (u8)(DECK_SIZE - 1 - TurnDuelistVars()[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK]);
		WriteTwoDigitNumberInTxSymbol_PadSpace(a, x, e);

		/* load Fire Energy card index and add to hand */
		a = *hl++;
		SearchCardInDeckAndAddToHand(a);
		AddCardToHand(a);
	} while (--c);

	/* load the number of cards added to hand and print text */
	LoadTxRam3(hCurSelectionItem);
	DrawWideTextBox_WaitForInput(DrewFireEnergyFromTheHandText);
	ShuffleCardsInDeck();
	return false;
}

bool MoltresLv37DiveBomb_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(70 / 2, 0, 70);
	return false;
}

bool MoltresLv37DiveBomb_Success50PercentEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(SuccessCheckIfHeadsAttackIsSuccessfulText)) {
		/* tails */
		SetDefiniteDamage(0);
		SetWasUnsuccessful();
		return false;
	}
	/* .heads */
	wLoadedAttackAnimation = ATK_ANIM_DIVE_BOMB;
	return true;
}

/* output in de the number of energy cards
 * attached to the Defending Pokemon times 10.
 * used for attacks that deal 10x number of energy
 * cards attached to the Defending card. */
static u16 GetEnergyAttachedMultiplierDamage(void)
{
	SwapTurn();
	u8 *vars = TurnDuelistVars();
	u8 c = 0;
	for (u8 l = DUELVARS_CARD_LOCATIONS; l < DECK_SIZE; l++) {
		/* .loop */
		if (vars[l] != CARD_LOCATION_ARENA)
			continue;
		/* is in Arena */
		if (!(GetCardType(GetCardIDFromDeckIndex(l)) & TYPE_ENERGY))
			continue;
		/* is Energy attached to Arena card */
		c++;
	}
	SwapTurn();
	u16 hl = c;
	hl = (u16)(hl + hl); /* hl =  2 * c */
	hl = (u16)(hl + hl); /* hl =  4 * c */
	hl = (u16)(hl + c);  /* hl =  5 * c */
	hl = (u16)(hl + hl); /* hl = 10 * c */
	return hl;
}

/* draws list of Energy Cards in Discard Pile
 * for Player to select from.
 * the Player can select up to 2 cards from the list.
 * these cards are given in $ff-terminated list
 * in hTempList. (text = register hl, printed first) */
static bool HandleEnergyCardsInDiscardPileSelection(u16 text)
{
	hCurSelectionItem = 0;
	if (!CreateEnergyCardListFromDiscardPile_AllEnergy()) {
		DrawWideTextBox_WaitForInput(text);
		for (;;) {
			/* .loop
			 * draws Discard Pile screen and textbox,
			 * and handles Player input */
			InitAndDrawCardListScreenLayout();
			SetCardListHeaderText(ChooseAnEnergyCardText, PlayerDiscardPileText);
			u8 card;
			if (DisplayCardList(&card)) {
				/* Player is trying to exit screen,
				 * but can select up to 2 cards total.
				 * prompt Player to confirm exiting screen. */
				if (AskWhetherToQuitSelectingCards(2))
					continue;
				break;
			}

			/* .selected
			 * a card was selected, so add it to list */
			u8 *hl = GetNextPositionInTempList();
			*hl = hTempCardIndex_ff98;
			if (RemoveCardFromDuelTempList(hTempCardIndex_ff98))
				break; /* no more cards? */
			if (hCurSelectionItem >= 2)
				break; /* already selected 2 cards? */
		}
	}

	/* .finish
	 * place terminating byte on list */
	u8 *hl = GetNextPositionInTempList();
	*hl = 0xff;
	return false;
}

/* returns carry if Pkmn Power cannot be used, and
 * sets the correct text in hl for failure. */
bool Curse_CheckDamageAndBench(void)
{
	TRACE_FN();
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;

	/* fail if Pkmn Power has already been used */
	u8 a = GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD_FLAGS));
	gTextID = OnlyOncePerTurnText;
	if (a & USED_PKMN_POWER_THIS_TURN)
		return true;

	/* fail if Opponent only has 1 Pokemon in Play Area */
	SwapTurn();
	a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	SwapTurn();
	gTextID = CannotUseSinceTheresOnly1PkmnText;
	if (a < 2)
		return true;

	/* fail if Opponent has no damage counters */
	SwapTurn();
	bool c = CheckIfPlayAreaHasAnyDamage();
	SwapTurn();
	gTextID = NoPokemonWithDamageCountersText;
	if (c)
		return true;

	/* return carry if Pkmn Power cannot be used due
	 * to Toxic Gas or status. */
	return CheckIsIncapableOfUsingPkmnPower(hTempPlayAreaLocation_ff9d);
}

bool Curse_PlayerSelectEffect(void)
{
	TRACE_FN();
	DrawWholeScreenTextBox(ProcedureForCurseText);
	SwapTurn();
	hCurSelectionItem = 0;
	SetupPlayAreaScreen();
	u8 a;

start:;
	u8 n = PrintPlayAreaCardList_EnableLCD();
	InitializeMenuParameters(hCurSelectionItem, &PlayAreaSelectionMenuParameters);
	wNumMenuItems = n;
	tcg_ui_screen(TCG_SCREEN_PLAY_AREA,0,TCG_PLAY_AREA_CURSE_FROM);

	/* first pick a target to take 1 damage counter from. */
loop_input_first:
	do {
		DoFrame();
	} while (!HandleMenuInput());
	a = hCurMenuItem;
	if (a == MENU_CANCEL) {
		/* .cancel: return carry if operation was cancelled. */
		SwapTurn();
		return true;
	}
	hCurSelectionItem = a;
	hTempPlayAreaLocation_ffa1 = a;
	if (GetCardDamageAndMaxHP(wCurMenuItem /* e */, NULL) == 0) {
		/* test if has damage: play sfx */
		PlaySFX_InvalidChoice();
		goto loop_input_first;
	}

	/* .picked_first
	 * give 10 HP to card selected, draw the scene,
	 * then immediately revert this. */
	{
		u8 *hp = &TurnDuelistVars()[(u8)(hTempPlayAreaLocation_ffa1 + DUELVARS_ARENA_CARD_HP)];
		u8 saved = *hp;
		*hp = (u8)(saved + 10);
		PrintPlayAreaCardList_EnableLCD();
		*hp = saved;
	}

	/* draw damage counter on cursor */
	DrawSymbolOnPlayAreaCursor(hTempPlayAreaLocation_ffa1, SYM_HP_NOK);
	tcg_ui_screen(TCG_SCREEN_PLAY_AREA,0,TCG_PLAY_AREA_CURSE_TO);

	/* handle input to pick the target to receive the damage counter. */
loop_input_second:
	do {
		DoFrame();
	} while (!HandleMenuInput());
	a = hCurMenuItem;
	hPlayAreaEffectTarget = a;
	if (a == MENU_CANCEL) {
		/* b press
		 * erase the damage counter symbol
		 * and loop back up again. */
		DrawSymbolOnPlayAreaCursor(hTempPlayAreaLocation_ffa1, SYM_SPACE);
		EraseCursor();
		goto start;
	}

	/* .a_press */
	if (a == hTempPlayAreaLocation_ffa1)
		goto loop_input_second; /* same as first? */
	/* a different Pokemon was picked,
	 * so store this Play Area location
	 * and erase the damage counter in the cursor. */
	DrawSymbolOnPlayAreaCursor(hTempPlayAreaLocation_ffa1, SYM_SPACE);
	EraseCursor();
	SwapTurn();
	return false;
}

bool Curse_TransferDamageEffect(void)
{
	TRACE_FN();
	/* set Pkmn Power as used */
	TurnDuelistVars()[(u8)(hTempList[0] + DUELVARS_ARENA_CARD_FLAGS)] |= USED_PKMN_POWER_THIS_TURN;

	/* figure out the type of duelist that used Curse.
	 * if it was the player, no need to draw the Play Area screen. */
	SwapTurn();
	if (GetNonTurnDuelistVariable(DUELVARS_DUELIST_TYPE) != DUELIST_TYPE_PLAYER) {
		/* vs. opponent */
		SetupPlayAreaScreen();
	}

	/* .vs_player
	 * transfer the damage counter to the targets that were selected. */
	u8 *vars = TurnDuelistVars();
	u8 *hl = &vars[(u8)(hPlayAreaEffectTarget + DUELVARS_ARENA_CARD_HP)];
	*hl = (u8)(*hl - 10);
	hl = &vars[(u8)(hTempPlayAreaLocation_ffa1 + DUELVARS_ARENA_CARD_HP)];
	*hl = (u8)(10 + *hl);

	PrintPlayAreaCardList_EnableLCD();
	if (GetNonTurnDuelistVariable(DUELVARS_DUELIST_TYPE) != DUELIST_TYPE_PLAYER) {
		/* vs. opponent */
		hTempPlayAreaLocation_ff9d = hPlayAreaEffectTarget;
		InitAndPrintPlayAreaCardInformationAndLocation_WithTextBox();
	}

	/* .done */
	SwapTurn();
	ExchangeRNG();
	HandleDestinyBondAndBetweenTurnKnockOuts();
	return false;
}

bool GengarDarkMind_PlayerSelectEffect(void)
{
	TRACE_FN();
	if (GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) < 2) {
		/* no bench Pokemon to damage. */
		hTemp_ffa0 = 0xff;
		return true; /* (carry of `cp 2`) */
	}

	/* .has_bench
	 * opens Play Area screen to select Bench Pokemon
	 * to damage, and store it before returning. */
	DrawWideTextBox_WaitForInput(ChoosePkmnInTheBenchToGiveDamageText);
	SwapTurn();
	u8 count;
	HasAlivePokemonInBench(&count);
	while (OpenPlayAreaScreenForSelection()) {
		/* .loop_input */
	}
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	SwapTurn();
	return false;
}

bool GengarDarkMind_AISelectEffect(void)
{
	TRACE_FN();
	hTemp_ffa0 = 0xff;
	if (GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) < 2)
		return true; /* return if no Bench Pokemon */
	/* just pick Pokemon with lowest remaining HP. */
	hTemp_ffa0 = AIFindTargetForBenchAttack();
	return false;
}

bool GengarDarkMind_DamageBenchEffect(void)
{
	TRACE_FN();
	u8 a = hTemp_ffa0;
	if (a == 0xff)
		return false; /* no target chosen */
	SwapTurn();
	DealDamageToPlayAreaPokemon_RegularAnim(a, 10);
	SwapTurn();
	return false;
}

bool SleepingGasEffect(void)
{
	TRACE_FN();
	bool c = Sleep50PercentEffect();
	if (!c)
		SetNoEffectFromStatus();
	return c;
}

bool DestinyBond_CheckEnergy(void)
{
	TRACE_FN();
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	u8 a = wAttachedEnergies[PSYCHIC];
	gTextID = NotEnoughPsychicEnergyText;
	return a < 1;
}

bool DestinyBond_PlayerSelectEffect(void)
{
	TRACE_FN();
	/* handle input and display of Energy card list */
	CreateListOfEnergyAttachedToArena(TYPE_ENERGY_PSYCHIC);
	DisplayEnergyDiscardScreen(PLAY_AREA_ARENA);
	if (HandleEnergyDiscardMenuInput())
		return true;
	hTempList[0] = hTempCardIndex_ff98;
	return false;
}

bool DestinyBond_AISelectEffect(void)
{
	TRACE_FN();
	/* pick first card in list */
	CreateListOfEnergyAttachedToArena(TYPE_ENERGY_PSYCHIC);
	hTempList[0] = wDuelTempList[0];
	return false;
}

bool DestinyBond_DiscardEffect(void)
{
	TRACE_FN();
	PutCardInDiscardPile(hTempList[0]);
	return false;
}

bool DestinyBond_DestinyBondEffect(void)
{
	TRACE_FN();
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_DESTINY_BOND);
	return false;
}

/* returns carry if no Energy cards in Discard Pile. */
bool EnergyConversion_CheckEnergy(void)
{
	TRACE_FN();
	bool c = CreateEnergyCardListFromDiscardPile_AllEnergy();
	gTextID = ThereAreNoEnergyCardsInDiscardPileText;
	return c;
}

bool EnergyConversion_PlayerSelectEffect(void)
{
	TRACE_FN();
	return HandleEnergyCardsInDiscardPileSelection(Choose2EnergyCardsFromDiscardPileForHandText);
}

/* select the first two energy cards found in Discard Pile
 * (shared by EnergyConversion and both Energy Absorption AI selections) */
static bool SelectFirstTwoEnergyCardsInDuelTempList(void)
{
	u8 *hl = wDuelTempList;
	u8 *de = hTempList;
	u8 c = 2;
	bool carry;
	for (;;) {
		/* .loop */
		u8 a = *hl++;
		if (a == 0xff) {
			carry = false;
			break;
		}
		carry = true; /* (cp $ff with a < $ff; dec c keeps it) */
		*de++ = a;
		if (--c == 0)
			break;
	}
	/* .done */
	*de = 0xff;
	return carry;
}

bool EnergyConversion_AISelectEffect(void)
{
	TRACE_FN();
	CreateEnergyCardListFromDiscardPile_AllEnergy();
	return SelectFirstTwoEnergyCardsInDuelTempList();
}

bool EnergyConversion_AddToHandEffect(void)
{
	TRACE_FN();
	/* damage itself */
	DealRecoilDamageToSelf(10);

	/* loop cards that were chosen
	 * until $ff is reached,
	 * and move them to the hand. */
	u8 *hl = hTempList;
	u8 *de = wDuelTempList;
	for (;;) {
		/* .loop_cards */
		u8 a = *hl++;
		*de++ = a;
		if (a == 0xff)
			break;
		AddCardToHand(MoveDiscardPileCardToHand(a));
	}

	/* .done */
	if (IsPlayerTurn())
		return true;
	DisplayCardListDetails();
	return false;
}

/* return carry if Defending Pokemon is not asleep */
bool DreamEaterEffect(void)
{
	TRACE_FN();
	u8 a = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS) & CNF_SLP_PRZ;
	if (a == ASLEEP)
		return false; /* return if asleep */
	/* not asleep, set carry and load text */
	gTextID = OpponentIsNotAsleepText;
	return true;
}

bool TransparencyEffect(void)
{
	TRACE_FN();
	return true;
}

/* returns carry if neither the Turn Duelist or
 * the non-Turn Duelist have any deck cards. */
bool Prophecy_CheckDeck(void)
{
	TRACE_FN();
	if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK) < DECK_SIZE)
		return false;
	if (GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK) < DECK_SIZE)
		return false;
	gTextID = NoCardsLeftInTheDeckText;
	return true;
}

static bool HandleProphecyScreen(void);

bool Prophecy_PlayerSelectEffect(void)
{
start: /* (.start is the routine's entry in the original) */
	TRACE_FN();
	DrawWholeScreenTextBox(ProcedureForProphecyText);
select_deck:
	DrawDuelMainScene();
	TwoItemHorizontalMenu(PleaseSelectTheDeckText);
	if (hKeysHeld & PAD_B)
		goto start; /* loop back to start */

	hTempList[0] = hCurMenuItem; /* store selection in first position in list */
	if (hCurMenuItem != 0) {
		/* non-turn duelist */
		if (GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK) >= DECK_SIZE)
			goto select_deck; /* no cards, go back to deck selection */
		SwapTurn();
		bool c = HandleProphecyScreen();
		SwapTurn();
		return c;
	}

	/* .turn_duelist */
	if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK) >= DECK_SIZE)
		goto select_deck; /* no cards, go back to deck selection */
	return HandleProphecyScreen();
}

bool Prophecy_AISelectEffect(void)
{
	TRACE_FN();
	/* AI doesn't ever choose this attack
	 * so this it does no sorting. */
	hTemp_ffa0 = 0xff;
	return false;
}

/* hl = GB address of the first card of the list (hTempList + 1) */
static bool Prophecy_ReorderDeckEffect_ReorderCards(u16 hl)
{
	u8 c = 0;
	/* add selected cards to hand in the specified order */
	for (;;) {
		/* .loop_add_hand */
		u8 a = *gbptr(hl++);
		if (a == 0xff)
			break;
		SearchCardInDeckAndAddToHand(a);
		c++;
	}

	/* .dec_hl: go to last card that was in the list */
	hl -= 2;

	do {
		/* .loop_return_deck: return the cards to the top of the deck */
		ReturnCardToDeck(*gbptr(hl--));
	} while (--c);
	if (IsPlayerTurn())
		return true;
	/* print text in case it was the opponent */
	DrawWideTextBox_WaitForInput(ExchangedCardsInDuelistsHandText);
	return false;
}

bool Prophecy_ReorderDeckEffect(void)
{
	TRACE_FN();
	u16 hl = ADDR_hTempList;
	u8 a = *gbptr(hl++);
	if (a == 0)
		return Prophecy_ReorderDeckEffect_ReorderCards(hl); /* turn duelist's deck */
	if (a == 0xff)
		return false;

	/* non-turn duelist's deck */
	SwapTurn();
	bool c = Prophecy_ReorderDeckEffect_ReorderCards(hl);
	SwapTurn();
	return c;
}

/* draw and handle Player selection for reordering
 * the top 3 cards of Deck.
 * the resulting list is output in order in hTempList. */
static bool HandleProphecyScreen(void)
{
	u8 *vars = TurnDuelistVars();
	u8 b = vars[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK];
	u8 a = (u8)(DECK_SIZE - b); /* a = number of cards in deck */

	/* store in c the number of cards that will be reordered.
	 * this number is 3, unless the deck as fewer cards than
	 * that in which case it will be the number of cards remaining. */
	u8 c = 3;
	if (a < c)
		c = a; /* store number of remaining cards in c */
	/* .got_number_cards */
	wNumberOfCardsToOrder = (u8)(c + 1);

	/* store in wDuelTempList the cards
	 * at top of Deck to be reordered. */
	u8 l = (u8)(b + DUELVARS_DECK_CARDS);
	u8 *de = wDuelTempList;
	do {
		/* .loop_top_cards */
		*de++ = vars[l++];
	} while (--c);
	*de = 0xff; /* terminating byte */

	u8 *hl;
start:
	b = CountCardsInDuelTempList();
	hCurSelectionItem = 1; /* start at 1 */

	/* initialize buffer ahead in wDuelTempList. */
	hl = wDuelTempList + 10;
	do {
		/* .loop_init_buffer */
		*hl++ = 0;
	} while (--b);
	*hl = 0xff;

	InitAndDrawCardListScreenLayout();
	SetCardListHeaderText(ChooseTheOrderOfTheCardsText, DuelistDeckText);
	PrintSortNumberInCardList_SetPointer();

loop_selection:;
	u8 card;
	if (DisplayCardList(&card))
		goto clear;

	/* first check if this card was already selected */
	hl = wDuelTempList + 10 + hCurMenuItem;
	if (*hl != 0)
		goto loop_selection; /* already chosen */

	/* being here means card hasn't been selected yet,
	 * so add its order number to buffer and increment
	 * the sort number for the next card. */
	*hl = hCurSelectionItem;
	hCurSelectionItem++;
	PrintSortNumberInCardList_CallFromPointer();
	if (hCurSelectionItem < wNumberOfCardsToOrder)
		goto loop_selection; /* still more cards */

	/* confirm that the ordering has been completed */
	EraseCursor();
	if (YesOrNoMenuWithText_LeftAligned(IsThisOKText))
		goto start; /* if not, return back to beginning of selection */

	/* write in hTempList the card list
	 * in order that was selected. */
	hl = wDuelTempList + 10;
	de = wDuelTempList;
	c = 0;
	for (;;) {
		/* .loop_order */
		a = *hl++;
		if (a == 0xff)
			break;
		hTempList[a] = *de;
		de++;
		c++;
	}
	/* now hTempList has the list of card deck indices
	 * in the order selected to be place on top of the deck. */

	/* .done */
	hTempList[1 + c] = 0xff; /* terminating byte */
	return false;

clear:
	/* check if any reordering was done. */
	a = hCurSelectionItem;
	if (a == 1)
		goto loop_selection; /* none done, go back */
	/* clear the order that was selected thus far. */
	a--;
	hCurSelectionItem = a;
	c = a;
	hl = wDuelTempList + 10;
	while (*hl++ != c) {
		/* .loop_clear */
	}
	/* clear this byte */
	hl--;
	*hl = 0x00;
	PrintSortNumberInCardList_CallFromPointer();
	goto loop_selection;
}

bool HypnoDarkMind_PlayerSelectEffect(void)
{
	TRACE_FN();
	if (GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) < 2) {
		/* no bench Pokemon to damage. */
		hTemp_ffa0 = 0xff;
		return true; /* (carry of `cp 2`) */
	}

	/* .has_bench
	 * opens Play Area screen to select Bench Pokemon
	 * to damage, and store it before returning. */
	DrawWideTextBox_WaitForInput(ChoosePkmnInTheBenchToGiveDamageText);
	SwapTurn();
	u8 count;
	HasAlivePokemonInBench(&count);
	while (OpenPlayAreaScreenForSelection()) {
		/* .loop_input */
	}
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	SwapTurn();
	return false;
}

bool HypnoDarkMind_AISelectEffect(void)
{
	TRACE_FN();
	hTemp_ffa0 = 0xff;
	if (GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) < 2)
		return true; /* return if no Bench Pokemon */
	/* just pick Pokemon with lowest remaining HP. */
	hTemp_ffa0 = AIFindTargetForBenchAttack();
	return false;
}

bool HypnoDarkMind_DamageBenchEffect(void)
{
	TRACE_FN();
	u8 a = hTemp_ffa0;
	if (a == 0xff)
		return false; /* no target chosen */
	SwapTurn();
	DealDamageToPlayAreaPokemon_RegularAnim(a, 10);
	SwapTurn();
	return false;
}

bool InvisibleWallEffect(void)
{
	TRACE_FN();
	return true;
}

bool MrMimeMeditate_AIEffect(void)
{
	TRACE_FN();
	bool c = MrMimeMeditate_DamageBoostEffect();
	SetDefiniteAIDamage();
	return c;
}

bool MrMimeMeditate_DamageBoostEffect(void)
{
	TRACE_FN();
	/* add damage counters of Defending card to damage */
	SwapTurn();
	u8 a = GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL);
	SwapTurn();
	AddToDamage(a);
	return false;
}

/* returns carry if Damage Swap cannot be used. */
bool DamageSwap_CheckDamage(void)
{
	TRACE_FN();
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	if (CheckIfPlayAreaHasAnyDamage()) {
		/* .no_damage */
		gTextID = NoPokemonWithDamageCountersText;
		return true;
	}
	return CheckIsIncapableOfUsingPkmnPower(hTempPlayAreaLocation_ff9d);
}

static bool TryGiveDamageCounter_DamageSwap(void);

bool DamageSwap_SelectAndSwapEffect(void)
{
	TRACE_FN();
	u8 a;
	if (GetTurnDuelistVariable(DUELVARS_DUELIST_TYPE) != DUELIST_TYPE_PLAYER) {
		/* non-player */
		SetupPlayAreaScreen();
		PrintPlayAreaCardList_EnableLCD();
		return false;
	}

	/* .player */
	DrawWholeScreenTextBox(ProcedureForDamageSwapText);
	hCurSelectionItem = 0;
	SetupPlayAreaScreen();

start:;
	u8 n = PrintPlayAreaCardList_EnableLCD();
	InitializeMenuParameters(hCurSelectionItem, &PlayAreaSelectionMenuParameters);
	wNumMenuItems = n;
	tcg_ui_screen(TCG_SCREEN_PLAY_AREA,0,TCG_PLAY_AREA_DAMAGE_FROM);

	/* handle selection of Pokemon to take damage from */
loop_input_first:
	do {
		DoFrame();
	} while (!HandleMenuInput());
	a = hCurMenuItem;
	if (a == MENU_CANCEL)
		return false; /* quit when B button is pressed */

	hTempPlayAreaLocation_ffa1 = a;
	hCurSelectionItem = a;

	/* if card has no damage, play sfx and return to start */
	if (GetCardDamageAndMaxHP(wCurMenuItem /* e */, NULL) == 0) {
		/* .no_damage */
		PlaySFX_InvalidChoice();
		goto loop_input_first;
	}

	/* take damage away temporarily to draw UI. */
	{
		u8 *hp = &TurnDuelistVars()[(u8)(hTempPlayAreaLocation_ffa1 + DUELVARS_ARENA_CARD_HP)];
		u8 saved = *hp;
		*hp = (u8)(saved + 10);
		PrintPlayAreaCardList_EnableLCD();
		*hp = saved;
	}

	/* draw damage counter in cursor */
	DrawSymbolOnPlayAreaCursor(hTempPlayAreaLocation_ffa1, SYM_HP_NOK);
	tcg_ui_screen(TCG_SCREEN_PLAY_AREA,0,TCG_PLAY_AREA_DAMAGE_TO);

	/* handle selection of Pokemon to give damage to */
loop_input_second:
	do {
		DoFrame();
	} while (!HandleMenuInput());
	a = hCurMenuItem;
	/* if B is pressed, return damage counter
	 * to card that it was taken from */
	if (a != MENU_CANCEL) {
		/* try to give the card selected the damage counter
		 * if it would KO, ignore it. */
		hPlayAreaEffectTarget = a;
		hCurSelectionItem = a;
		if (TryGiveDamageCounter_DamageSwap())
			goto loop_input_second;

		SetOppAction_SerialSendDuelData(OPPACTION_6B15);
	}

	/* .update_ui */
	DrawSymbolOnPlayAreaCursor(hTempPlayAreaLocation_ffa1, SYM_SPACE);
	EraseCursor();
	goto start;
}

/* tries to give damage counter to hPlayAreaEffectTarget,
 * and if successful updates UI screen. */
bool DamageSwap_SwapEffect(void)
{
	TRACE_FN();
	if (TryGiveDamageCounter_DamageSwap())
		return true;
	PrintPlayAreaCardList_EnableLCD();
	return false;
}

/* tries to give the damage counter to the target
 * chosen by the Player (hPlayAreaEffectTarget).
 * if the damage counter would KO card, then do
 * not give the damage counter and return carry. */
static bool TryGiveDamageCounter_DamageSwap(void)
{
	u8 *vars = TurnDuelistVars();
	u8 a = (u8)(vars[(u8)(hPlayAreaEffectTarget + DUELVARS_ARENA_CARD_HP)] - 10);
	if (a == 0)
		return true; /* would bring HP to zero? */
	/* has enough HP to receive a damage counter */
	vars[(u8)(hPlayAreaEffectTarget + DUELVARS_ARENA_CARD_HP)] = a;
	u8 *hl = &vars[(u8)(hTempPlayAreaLocation_ffa1 + DUELVARS_ARENA_CARD_HP)];
	*hl = (u8)(10 + *hl);
	return false;
}

bool PsywaveEffect(void)
{
	TRACE_FN();
	wr16(wDamage, GetEnergyAttachedMultiplierDamage());
	return false;
}

static bool CheckIfTurnDuelistHasEvolvedCards(void);
static bool HandleEvolvedCardSelection(void);
static bool FindFirstNonBasicCardInPlayArea(u8 *loc);

/* returns carry if neither Duelist has evolved Pokemon. */
bool DevolutionBeam_CheckPlayArea(void)
{
	TRACE_FN();
	if (!CheckIfTurnDuelistHasEvolvedCards())
		return false;
	SwapTurn();
	bool c = CheckIfTurnDuelistHasEvolvedCards();
	SwapTurn();
	gTextID = ThereAreNoStage1PokemonText;
	return c;
}

/* returns carry of Player cancelled selection.
 * otherwise, output in hTemp_ffa0 which Play Area
 * was selected ($0 = own Play Area, $1 = opp. Play Area)
 * and in hTempPlayAreaLocation_ffa1 selected card. */
bool DevolutionBeam_PlayerSelectEffect(void)
{
	TRACE_FN();
	DrawWholeScreenTextBox(ProcedureForDevolutionBeamText);

	for (;;) {
		/* .start */
		DrawDuelMainScene();
		TwoItemHorizontalMenu(PleaseSelectThePlayAreaText);
		if (hKeysHeld & PAD_B)
			return true; /* .set_carry */

		/* a Play Area was selected */
		u8 a;
		if (hCurMenuItem == 0) {
			/* player chosen */
			if (HandleEvolvedCardSelection())
				continue;
			a = 0;
		} else {
			/* .opp_chosen */
			SwapTurn();
			bool c = HandleEvolvedCardSelection();
			SwapTurn();
			if (c)
				continue;
			a = 0x01;
		}

		/* .store_selection */
		hTemp_ffa0 = a; /* store which Duelist Play Area selected */
		hTempPlayAreaLocation_ffa1 = hTempPlayAreaLocation_ff9d; /* store which card selected */
		return false;
	}
}

bool DevolutionBeam_AISelectEffect(void)
{
	TRACE_FN();
	u8 a;
	hTemp_ffa0 = 0x01; /* always choose player's side */
	SwapTurn();
	bool c = FindFirstNonBasicCardInPlayArea(&a);
	SwapTurn();
	if (!c) {
		hTemp_ffa0 = 0;
		c = FindFirstNonBasicCardInPlayArea(&a);
	}
	/* .found */
	hTempPlayAreaLocation_ffa1 = a;
	return c;
}

bool DevolutionBeam_LoadAnimation(void)
{
	TRACE_FN();
	wLoadedAttackAnimation = ATK_ANIM_NONE;
	return false;
}

static bool DevolutionBeam_DevolveEffect_DevolvePokemon(void)
{
	wLoadedAttackAnimation = ATK_ANIM_DEVOLUTION_BEAM;
	/* (register de is left over from the effect command's caller and cannot be
	 * reproduced; it only feeds wDamageAnimAmount, which this animation doesn't show) */
	PlayAttackAnimation(0, hTempPlayAreaLocation_ffa1, 0x00, hWhoseTurn);
	WaitAttackAnimation();

	/* load selected card's data */
	hTempPlayAreaLocation_ff9d = hTempPlayAreaLocation_ffa1;
	wTempPlayAreaLocation_cceb = hTempPlayAreaLocation_ffa1;
	LoadCardDataToBuffer1_FromDeckIndex(
		GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ffa1 + DUELVARS_ARENA_CARD)));

	/* check if card is affected */
	wTempNonTurnDuelistCardID = wLoadedCard1ID;
	u16 de = 0x0;
	if (hTempPlayAreaLocation_ff9d != 0 || !HandleNoDamageOrEffectSubstatus()) {
		/* .skip_substatus_check */
		de = HandleDamageReductionOrNoDamageFromPkmnPowerEffects(de);
	}
	(void)de;
	/* .check_no_damage_effect */
	if (CheckNoDamageOrEffect()) {
		DrawWideTextBox_WaitForInput(gTextID);
		return false;
	}

	/* .devolve */
	hTempPlayAreaLocation_ff9d = hTempPlayAreaLocation_ffa1;
	/* (the original also loads the card into a here; GetCardOneStageBelow
	 * reads hTempPlayAreaLocation_ff9d itself) */
	u8 d = 0, e = 0;
	GetCardOneStageBelow(&d, &e);
	PrintDevolvedCardNameAndLevelText(d, e);

	UpdateDevolvedCardHPAndStage(d);
	ResetDevolvedCardStatus();

	/* add the evolved card to the hand */
	AddCardToHand(e);

	/* check if this devolution KO's card */
	PrintPlayAreaCardKnockedOutIfNoHP(hTempPlayAreaLocation_ffa1);

	wDuelDisplayedScreen = 0;
	return false;
}

bool DevolutionBeam_DevolveEffect(void)
{
	TRACE_FN();
	u8 a = hTemp_ffa0;
	if (a == 0)
		return DevolutionBeam_DevolveEffect_DevolvePokemon();
	if (a == 0xff)
		return false;

	/* opponent's Play Area */
	SwapTurn();
	/* bug (original): `ldh a, [hTempPlayAreaLocation_ffa1] / jr nz, .skip_handle_no_damage_effect`
	 * tests the flags of `cp $ff` above (always nz here), so HandleNoDamageOrEffect is never
	 * called and the Pokemon is devolved even when the opponent's arena card is the target. */
	bool c = DevolutionBeam_DevolveEffect_DevolvePokemon();
	/* .unaffected */
	SwapTurn();
	return c;
}

/* returns carry if Turn Duelist
 * has no Stage1 or Stage2 cards in Play Area. */
static bool CheckIfTurnDuelistHasEvolvedCards(void)
{
	u8 *vars = TurnDuelistVars();
	u8 l = DUELVARS_ARENA_CARD;
	u8 e = DUELVARS_ARENA_CARD_STAGE;
	for (;;) {
		/* .loop */
		u8 a = vars[l++];
		if (a == 0xff)
			return true; /* .set_carry */
		a = vars[e++];
		if (a != 0)
			return false;
		/* is Basic Stage */
	}
}

/* handles Player selection of an evolved card in Play Area.
 * returns carry if Player cancelled operation. */
static bool HandleEvolvedCardSelection(void)
{
	u8 count;
	HasAlivePokemonInPlayArea(&count);
	for (;;) {
		/* .loop */
		if (OpenPlayAreaScreenForSelection())
			return true;
		if (GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD_STAGE)) != 0)
			return false;
		/* if Basic, loop */
	}
}

/* finds first occurrence in Play Area
 * of Stage 1 or 2 card, and outputs its
 * Play Area location in a, with carry set.
 * if none found, don't return carry set.
 * (*loc = register a: the location if found, else the last stage read) */
static bool FindFirstNonBasicCardInPlayArea(u8 *loc)
{
	u8 *vars = TurnDuelistVars();
	u8 c = vars[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA];

	u8 b = PLAY_AREA_ARENA;
	u8 l = DUELVARS_ARENA_CARD_STAGE;
	u8 a;
	do {
		/* .loop */
		a = vars[l++];
		if (a != 0) {
			/* .not_basic */
			*loc = b;
			return true;
		}
		b++;
	} while (--c);
	*loc = a;
	return false;
}

bool NeutralizingShieldEffect(void)
{
	TRACE_FN();
	return true;
}

bool Psychic_AIEffect(void)
{
	TRACE_FN();
	bool c = Psychic_DamageBoostEffect();
	SetDefiniteAIDamage();
	return c;
}

bool Psychic_DamageBoostEffect(void)
{
	TRACE_FN();
	u16 de = GetEnergyAttachedMultiplierDamage();
	u32 sum = (u32)rd16(wDamage) + de;
	wr16(wDamage, (u16)sum);
	return sum > 0xffff; /* (carry of the adc) */
}

/* return carry if no Psychic Energy attached */
bool Barrier_CheckEnergy(void)
{
	TRACE_FN();
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	u8 a = wAttachedEnergies[PSYCHIC];
	gTextID = NotEnoughPsychicEnergyText;
	return a < 1;
}

bool Barrier_PlayerSelectEffect(void)
{
	TRACE_FN();
	CreateListOfEnergyAttachedToArena(TYPE_ENERGY_PSYCHIC);
	DisplayEnergyDiscardScreen(PLAY_AREA_ARENA);
	if (HandleEnergyDiscardMenuInput())
		return true;
	hTemp_ffa0 = hTempCardIndex_ff98;
	return false;
}

bool Barrier_AISelectEffect(void)
{
	TRACE_FN();
	/* AI picks the first energy in list */
	CreateListOfEnergyAttachedToArena(TYPE_ENERGY_PSYCHIC);
	hTemp_ffa0 = wDuelTempList[0];
	return false;
}

bool Barrier_DiscardEffect(void)
{
	TRACE_FN();
	PutCardInDiscardPile(hTemp_ffa0);
	return false;
}

bool Barrier_BarrierEffect(void)
{
	TRACE_FN();
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_BARRIER);
	return false;
}

bool MewtwoAltEnergyAbsorption_CheckDiscardPile(void)
{
	TRACE_FN();
	bool c = CreateEnergyCardListFromDiscardPile_AllEnergy();
	gTextID = ThereAreNoEnergyCardsInDiscardPileText;
	return c;
}

bool MewtwoAltEnergyAbsorption_PlayerSelectEffect(void)
{
	TRACE_FN();
	return HandleEnergyCardsInDiscardPileSelection(Choose2EnergyCardsFromDiscardPileToAttachText);
}

bool MewtwoAltEnergyAbsorption_AISelectEffect(void)
{
	TRACE_FN();
	/* AI picks first 2 energy cards */
	CreateEnergyCardListFromDiscardPile_AllEnergy();
	return SelectFirstTwoEnergyCardsInDuelTempList();
}

bool MewtwoAltEnergyAbsorption_AddToHandEffect(void)
{
	TRACE_FN();
	u8 *hl = hTempList;
	for (;;) {
		/* .loop */
		u8 a = *hl++;
		if (a == 0xff)
			return false;
		a = MoveDiscardPileCardToHand(a);
		TurnDuelistVars()[a] = CARD_LOCATION_ARENA;
	}
}

bool MewtwoEnergyAbsorption_CheckDiscardPile(void)
{
	TRACE_FN();
	bool c = CreateEnergyCardListFromDiscardPile_AllEnergy();
	gTextID = ThereAreNoEnergyCardsInDiscardPileText;
	return c;
}

bool MewtwoEnergyAbsorption_PlayerSelectEffect(void)
{
	TRACE_FN();
	return HandleEnergyCardsInDiscardPileSelection(Choose2EnergyCardsFromDiscardPileToAttachText);
}

bool MewtwoEnergyAbsorption_AISelectEffect(void)
{
	TRACE_FN();
	/* AI picks first 2 energy cards */
	CreateEnergyCardListFromDiscardPile_AllEnergy();
	return SelectFirstTwoEnergyCardsInDuelTempList();
}

bool MewtwoEnergyAbsorption_AddToHandEffect(void)
{
	TRACE_FN();
	u8 *hl = hTempList;
	for (;;) {
		/* .loop */
		u8 a = *hl++;
		if (a == 0xff)
			return false;
		a = MoveDiscardPileCardToHand(a);
		TurnDuelistVars()[a] = CARD_LOCATION_ARENA;
	}
}

/* returns carry if Strange Behavior cannot be used. */
bool StrangeBehavior_CheckDamage(void)
{
	TRACE_FN();
	/* does Play Area have any damage counters? */
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	bool c = CheckIfPlayAreaHasAnyDamage();
	gTextID = NoPokemonWithDamageCountersText;
	if (c)
		return true;
	/* can Slowbro receive any damage counters without KO-ing? */
	u8 a = GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD_HP));
	gTextID = CannotUseBecauseItWillBeKnockedOutText;
	if (a < 10 + 10)
		return true;
	/* can Pkmn Power be used? */
	return CheckIsIncapableOfUsingPkmnPower(hTempPlayAreaLocation_ff9d);
}
