/*
 * Port of src/engine/duel/effect_functions.asm, part 3 of 4:
 * StrangeBehavior_SelectAndSwapEffect .. AISelectConversionColor
 * (asm lines 5540-8420). See port/notes/EFFECTS_BRIEF.md and PORTING.md.
 *
 * Effect functions return the carry flag the original returns. Where the
 * original returns right after a call to a routine whose carry output is not
 * defined (drawing, damage, ...), false is returned; no caller of those
 * command types reads the carry.
 */
#include "effects.h"
#include "../tcg.h"
#include "part3_needs.h"

/* (PlayAreaSelectionMenuParameters / BenchSelectionMenuParameters are part 1's
 * tables and PlayerPickAttackForAmnesia_Carry is part 2's: see part3_needs.h) */

/* `call DoFrame / call HandleMenuInput / jr nc, .loop_input` */
static void WaitForMenuInput(void)
{
	do {
		DoFrame();
	} while (!HandleMenuInput());
}

bool StrangeBehavior_SelectAndSwapEffect(void)
{
	TRACE_FN();
	if (GetTurnDuelistVariable(DUELVARS_DUELIST_TYPE) != DUELIST_TYPE_PLAYER) {
		/* not player */
		SetupPlayAreaScreen();
		PrintPlayAreaCardList_EnableLCD();
		return false;
	}

	/* player */
	DrawWholeScreenTextBox(ProcedureForStrangeBehaviorText);

	hCurSelectionItem = 0;
	SetupPlayAreaScreen();
start:;
	u8 n = PrintPlayAreaCardList_EnableLCD();
	InitializeMenuParameters(hCurSelectionItem, &PlayAreaSelectionMenuParameters);
	wNumMenuItems = n;
loop_input:
	WaitForMenuInput();
	u8 a = hCurMenuItem;
	if (a == (u8)MENU_CANCEL)
		return false; /* return when B button is pressed */

	hCurSelectionItem = a;
	hTempPlayAreaLocation_ffa1 = a;
	if (a == hTemp_ffa0)
		goto play_sfx; /* can't select Slowbro itself */

	/* e = wCurMenuItem, as left by HandleMenuInput */
	if (GetCardDamageAndMaxHP(wCurMenuItem, NULL) == 0)
		goto play_sfx; /* can't select card without damage */

	if (TryGiveDamageCounter_StrangeBehavior())
		goto play_sfx;
	SetOppAction_SerialSendDuelData(OPPACTION_6B15);
	goto start;

play_sfx:
	PlaySFX_InvalidChoice();
	goto loop_input;
}

bool StrangeBehavior_SwapEffect(void)
{
	TRACE_FN();
	if (TryGiveDamageCounter_StrangeBehavior())
		return true;
	PrintPlayAreaCardList_EnableLCD();
	return false;
}

/* tries to give the damage counter to the target
 * chosen by the Player (hTemp_ffa0).
 * if the damage counter would KO card, then do
 * not give the damage counter and return carry. */
bool TryGiveDamageCounter_StrangeBehavior(void)
{
	u8 *vars = TurnDuelistVars();
	u8 a = (u8)(vars[(u8)(hTemp_ffa0 + DUELVARS_ARENA_CARD_HP)] - 10);
	if (a == 0)
		return true; /* would bring HP to zero? */
	/* has enough HP to receive a damage counter */
	vars[(u8)(hTemp_ffa0 + DUELVARS_ARENA_CARD_HP)] = a;
	u8 *hl = &vars[(u8)(hTempPlayAreaLocation_ffa1 + DUELVARS_ARENA_CARD_HP)];
	*hl = (u8)(10 + *hl);
	return false;
}

/* returns carry if has no damage counters. */
bool SpacingOut_CheckDamage(void)
{
	TRACE_FN();
	u8 a = GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL);
	gTextID = NoDamageCountersText;
	return a < 10;
}

bool SpacingOut_Success50PercentEffect(void)
{
	TRACE_FN();
	bool heads = TossCoin_BankB(SuccessCheckIfHeadsAttackIsSuccessfulText);
	hTemp_ffa0 = heads; /* a = 1 if heads, 0 if tails */
	if (!heads) {
		SetWasUnsuccessful();
		return false;
	}
	wLoadedAttackAnimation = ATK_ANIM_RECOVER;
	return true; /* carry from TossCoin_BankB */
}

bool SpacingOut_HealEffect(void)
{
	TRACE_FN();
	if (hTemp_ffa0 == 0)
		return false; /* coin toss was tails */
	/* unnecessary damage check, this was already
	 * checked in SpacingOut_CheckDamage */
	if (GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL) == 0)
		return false; /* no damage counters */
	u8 *vars = TurnDuelistVars();
	vars[DUELVARS_ARENA_CARD_HP] = (u8)(vars[DUELVARS_ARENA_CARD_HP] + 10);
	return false;
}

/* sets carry if no Trainer cards in the Discard Pile. */
bool Scavenge_CheckDiscardPile(void)
{
	TRACE_FN();
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	gTextID = NotEnoughPsychicEnergyText;
	if (wAttachedEnergies[PSYCHIC] < 1)
		return true; /* return if no Psychic energy attached */
	bool carry = CreateTrainerCardListFromDiscardPile();
	gTextID = ThereAreNoTrainerCardsInDiscardPileText; /* this is redundant */
	return carry;
}

bool Scavenge_PlayerSelectEnergyEffect(void)
{
	TRACE_FN();
	CreateListOfEnergyAttachedToArena(TYPE_ENERGY_PSYCHIC);
	DisplayEnergyDiscardScreen(PLAY_AREA_ARENA);
	if (HandleEnergyDiscardMenuInput())
		return true;
	hTemp_ffa0 = hTempCardIndex_ff98;
	return false;
}

bool Scavenge_AISelectEffect(void)
{
	TRACE_FN();
	/* AI picks first Energy card in list */
	CreateListOfEnergyAttachedToArena(TYPE_ENERGY_PSYCHIC);
	hTemp_ffa0 = wDuelTempList[0];
	/* AI picks first Trainer card in list */
	bool carry = CreateTrainerCardListFromDiscardPile();
	hTempPlayAreaLocation_ffa1 = wDuelTempList[0];
	return carry;
}

bool Scavenge_DiscardEffect(void)
{
	TRACE_FN();
	PutCardInDiscardPile(hTemp_ffa0);
	return false;
}

bool Scavenge_PlayerSelectTrainerEffect(void)
{
	TRACE_FN();
	CreateTrainerCardListFromDiscardPile();
	InitAndDrawCardListScreenLayout_WithSelectCheckMenu();
	SetCardListHeaderText(PleaseSelectCardText, PlayerDiscardPileText);
	u8 card;
	while (DisplayCardList(&card))
		;
	hTempPlayAreaLocation_ffa1 = hTempCardIndex_ff98;
	return false;
}

bool Scavenge_AddToHandEffect(void)
{
	TRACE_FN();
	AddCardToHand(MoveDiscardPileCardToHand(hTempPlayAreaLocation_ffa1));
	if (IsPlayerTurn())
		return true;
	DisplayCardDetailScreen(hTempPlayAreaLocation_ffa1, WasPlacedInTheHandText);
	return false;
}

/* returns carry if Defending Pokemon has no attacks */
bool SlowpokeAmnesia_CheckAttacks(void)
{
	TRACE_FN();
	bool carry = CheckIfDefendingPokemonHasAnyAttack();
	gTextID = NoAttackMayBeChoosenText;
	return carry;
}

bool SlowpokeAmnesia_PlayerSelectEffect(void)
{
	TRACE_FN();
	return PlayerPickAttackForAmnesia_Carry();
}

bool SlowpokeAmnesia_AISelectEffect(void)
{
	TRACE_FN();
	hTemp_ffa0 = AIPickAttackForAmnesia();
	return false;
}

bool SlowpokeAmnesia_DisableEffect(void)
{
	TRACE_FN();
	ApplyAmnesiaToAttack();
	return false;
}

/* returns carry if Arena card has no Psychic Energy attached
 * or if it doesn't have any damage counters. */
bool KadabraRecover_CheckEnergyHP(void)
{
	TRACE_FN();
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	gTextID = NotEnoughPsychicEnergyText;
	if (wAttachedEnergies[PSYCHIC] < 1)
		return true; /* return if not enough energy */
	u8 a = GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL); /* (e is still PLAY_AREA_ARENA) */
	gTextID = NoDamageCountersText;
	return a < 10; /* return carry if no damage */
}

bool KadabraRecover_PlayerSelectEffect(void)
{
	TRACE_FN();
	CreateListOfEnergyAttachedToArena(TYPE_ENERGY_PSYCHIC);
	DisplayEnergyDiscardScreen(PLAY_AREA_ARENA);
	if (HandleEnergyDiscardMenuInput())
		return true;
	hTemp_ffa0 = hTempCardIndex_ff98; /* store card chosen */
	return false;
}

bool KadabraRecover_AISelectEffect(void)
{
	TRACE_FN();
	CreateListOfEnergyAttachedToArena(TYPE_ENERGY_PSYCHIC);
	hTemp_ffa0 = wDuelTempList[0]; /* pick first card */
	return false;
}

bool KadabraRecover_DiscardEffect(void)
{
	TRACE_FN();
	PutCardInDiscardPile(hTemp_ffa0);
	return false;
}

bool KadabraRecover_HealEffect(void)
{
	TRACE_FN();
	u8 a = GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL);
	ApplyAndAnimateHPRecovery(a); /* all damage for recovery */
	return false;
}

bool JynxDoubleslap_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(20 / 2, 0, 20);
	return false;
}

bool JynxDoubleslap_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(10);
	u8 heads = TossCoinATimes_BankB(2, DamageCheckIfHeadsXDamageText);
	SetDefiniteDamage(ATimes10(heads));
	return false;
}

bool JynxMeditate_AIEffect(void)
{
	TRACE_FN();
	JynxMeditate_DamageBoostEffect();
	SetDefiniteAIDamage();
	return false;
}

bool JynxMeditate_DamageBoostEffect(void)
{
	TRACE_FN();
	/* add damage counters of Defending card to damage */
	SwapTurn();
	u8 a = GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL);
	SwapTurn();
	AddToDamage(a);
	return false;
}

bool MysteryAttack_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(10, 0, 20);
	return false;
}

bool MysteryAttack_RandomEffect(void)
{
	TRACE_FN();
	SetDefiniteDamage(10);

	/* chooses a random effect from 8 possible options. */
	u8 a = UpdateRNGSources() & 0x7;
	hTemp_ffa0 = a;
	switch (a) { /* JumpToFunctionInTable .random_effect */
	case 0:
		return ParalysisEffect();
	case 1:
		return PoisonEffect();
	case 2:
		return SleepEffect();
	case 3:
		return ConfusionEffect();
	case 4:
		/* .recover
		 * this will actually activate recovery effect afterwards
		 * for this command do nothing */
		return false;
	case 5:
		/* .no_effect */
		return false;
	case 6:
		/* .more_damage */
		SetDefiniteDamage(20);
		return false;
	default:
		/* .no_damage */
		wLoadedAttackAnimation = ATK_ANIM_GLOW_EFFECT;
		SetDefiniteDamage(0);
		SetNoEffectFromStatus();
		/* fallthrough to .recover / .no_effect */
		return false;
	}
}

bool MysteryAttack_RecoverEffect(void)
{
	TRACE_FN();
	/* in case the 5th option was chosen for random effect,
	 * trigger recovery effect for 10 HP. */
	if (hTemp_ffa0 != 4)
		return hTemp_ffa0 < 4; /* (cp 4 / ret nz) */
	ApplyAndAnimateHPRecovery(10);
	return false;
}

bool StoneBarrage_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(10, 0, 100);
	return false;
}

bool StoneBarrage_MultiplierEffect(void)
{
	TRACE_FN();
	hTemp_ffa0 = 0;
	/* .loop_coin_toss */
	while (TossCoinATimes_BankB(0, FlipUntilFailAppears10DamageForEachHeadsText) != 0)
		hTemp_ffa0++; /* increase heads count */

	/* .tails: store resulting damage */
	wr16(wDamage, HtimesL(10, hTemp_ffa0));
	return false;
}

bool OnixHardenEffect(void)
{
	TRACE_FN();
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_PREVENT_LESS_THAN_40);
	return false;
}

bool PrimeapeFurySwipes_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(60 / 2, 0, 60);
	return false;
}

bool PrimeapeFurySwipes_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(20);
	u8 a = TossCoinATimes_BankB(3, DamageCheckIfHeadsXDamageText);
	a = (u8)(a + a);
	SetDefiniteDamage(ATimes10(a));
	return false;
}

bool TantrumEffect(void)
{
	TRACE_FN();
	if (TossCoin_BankB(IfTailsYourPokemonBecomesConfusedText))
		return true; /* return if heads */
	/* confuse Pokemon */
	wLoadedAttackAnimation = ATK_ANIM_MULTIPLE_SLASH;
	SwapTurn();
	bool carry = ConfusionEffect();
	SwapTurn();
	return carry;
}

bool StrikesBackEffect(void)
{
	TRACE_FN();
	return true;
}

bool KabutoArmorEffect(void)
{
	TRACE_FN();
	return true;
}

bool AbsorbEffect(void)
{
	TRACE_FN();
	u16 hl = rd16(wDealtDamage);
	hl >>= 1; /* srl h / rr l */
	if (hl & 1) {
		/* round up to nearest 10 */
		hl = (u16)(hl + 5);
	}
	/* .rounded */
	ApplyAndAnimateHPRecovery(hl);
	return false;
}

bool SnivelEffect(void)
{
	TRACE_FN();
	ApplySubstatus2ToDefendingCard(SUBSTATUS2_REDUCE_BY_20);
	return false;
}

bool CuboneRage_AIEffect(void)
{
	TRACE_FN();
	CuboneRage_DamageBoostEffect();
	SetDefiniteAIDamage();
	return false;
}

bool CuboneRage_DamageBoostEffect(void)
{
	TRACE_FN();
	AddToDamage(GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL));
	return false;
}

bool Bonemerang_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(60 / 2, 0, 60);
	return false;
}

bool Bonemerang_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(30);
	u8 e = TossCoinATimes_BankB(2, DamageCheckIfHeadsXDamageText);
	u8 a = (u8)(e + e); /* a = 2 * heads */
	a = (u8)(a + e);    /* a = 3 * heads */
	SetDefiniteDamage(ATimes10(a));
	return false;
}

/* returns carry if can't add Pokemon from deck */
bool MarowakCallForFamily_CheckDeckAndPlayArea(void)
{
	TRACE_FN();
	if (CheckIfDeckIsEmpty())
		return true; /* no cards in deck */
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	gTextID = NoSpaceOnTheBenchText;
	return a >= MAX_PLAY_AREA_POKEMON; /* cp MAX_PLAY_AREA_POKEMON / ccf */
}

bool MarowakCallForFamily_PlayerSelectEffect(void)
{
	TRACE_FN();
	hTemp_ffa0 = 0xff;

	CreateDeckCardList(NULL);
	/* (bc = FightingPokemonDeckText: the shared signature takes only hl,
	 * part 1 recovers bc from its table of LookForCardsInDeck callers) */
	if (LookForCardsInDeck(SEARCHEFFECT_BASIC_FIGHTING, 0x00, ChooseBasicFightingPokemonFromDeckText))
		return true;

	/* draw Deck list interface and print text */
	InitAndDrawCardListScreenLayout_WithSelectCheckMenu();
	SetCardListHeaderText(ChooseBasicFightingPokemonText, DuelistDeckText);

	for (;;) {
		/* .loop */
		u8 card;
		if (!DisplayCardList(&card)) {
			LoadCardDataToBuffer2_FromDeckIndex(card);
			if (wLoadedCard2.Type == FIGHTING && /* is Fighting? */
			    wLoadedCard2.Stage == 0) {       /* is Basic? */
				hTemp_ffa0 = hTempCardIndex_ff98;
				return false;
			}
			goto play_sfx;
		}

		/* .pressed_b
		 * figure if Player can exit the screen without selecting,
		 * that is, if the Deck has no Basic Fighting Pokemon. */
		u8 *vars = TurnDuelistVars();
		for (u8 l = DUELVARS_CARD_LOCATIONS; l < DECK_SIZE; l++) {
			if (vars[l] != CARD_LOCATION_DECK)
				continue;
			LoadCardDataToBuffer2_FromDeckIndex(l);
			/* bug (original): checks wLoadedCard1 instead of the card just loaded to wLoadedCard2 */
			if (wLoadedCard1.Type != FIGHTING)
				continue; /* found, go back to top loop */
			if (wLoadedCard1.Stage == 0)
				goto play_sfx; /* found, go back to top loop */
		}

		/* no valid card in Deck, can safely exit screen */
		hTemp_ffa0 = 0xff;
		return false;

	play_sfx:
		/* play SFX and loop back */
		PlaySFX_InvalidChoice();
	}
}

bool MarowakCallForFamily_AISelectEffect(void)
{
	TRACE_FN();
	CreateDeckCardList(NULL);
	u8 *hl = wDuelTempList;
	for (;;) {
		/* .loop_deck */
		u8 a = *hl++;
		hTemp_ffa0 = a;
		if (a == 0xff)
			return false; /* none found */
		LoadCardDataToBuffer2_FromDeckIndex(a);
		if (wLoadedCard2.Type != FIGHTING)
			continue;
		if (wLoadedCard2.Stage != 0)
			continue;
		/* found */
		return false;
	}
}

bool MarowakCallForFamily_PutInPlayAreaEffect(void)
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

bool KarateChop_AIEffect(void)
{
	TRACE_FN();
	KarateChop_DamageSubtractionEffect();
	SetDefiniteAIDamage();
	return false;
}

bool KarateChop_DamageSubtractionEffect(void)
{
	TRACE_FN();
	u8 e = GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL);
	u8 lo = wDamage[0];
	wDamage[0] = (u8)(lo - e);
	wDamage[1] = (u8)(wDamage[1] - (lo < e ? 1 : 0)); /* sbc 0 */
	if (!(wDamage[1] & 0x80)) /* rla / ret nc */
		return false;
	/* cap it to 0 damage */
	SetDefiniteDamage(0);
	return false;
}

bool SubmissionEffect(void)
{
	TRACE_FN();
	DealRecoilDamageToSelf(20);
	return false;
}

bool GolemSelfdestructEffect(void)
{
	TRACE_FN();
	DealRecoilDamageToSelf(100);
	wIsDamageToSelf = TRUE;
	DealDamageToAllBenchedPokemon(20);
	SwapTurn();
	wIsDamageToSelf = FALSE;
	DealDamageToAllBenchedPokemon(20);
	SwapTurn();
	return false;
}

bool GravelerHardenEffect(void)
{
	TRACE_FN();
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_PREVENT_LESS_THAN_40);
	return false;
}

bool Ram_SelectSwitchEffect(void)
{
	TRACE_FN();
	if (GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) < 2) {
		/* .no_bench */
		hTemp_ffa0 = 0xff;
		return true; /* carry from cp 2 */
	}
	DuelistSelectForcedSwitch();
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	return false;
}

bool Ram_RecoilSwitchEffect(void)
{
	TRACE_FN();
	DealRecoilDamageToSelf(20);
	HandleSwitchDefendingPokemonEffect(hTemp_ffa0);
	return false;
}

bool LeerEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(IfHeadsOpponentCannotAttackText)) {
		SetWasUnsuccessful();
		return false;
	}
	wLoadedAttackAnimation = ATK_ANIM_LEER;
	ApplySubstatus2ToDefendingCard(SUBSTATUS2_LEER);
	return false;
}

/* return carry if opponent has no Bench Pokemon. */
bool StretchKick_CheckBench(void)
{
	TRACE_FN();
	u8 a = GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	gTextID = EffectNoPokemonOnTheBenchText;
	return a < 2;
}

bool StretchKick_PlayerSelectEffect(void)
{
	TRACE_FN();
	DrawWideTextBox_WaitForInput(ChoosePkmnInTheBenchToGiveDamageText);
	SwapTurn();
	HasAlivePokemonInBench(NULL);
	while (OpenPlayAreaScreenForSelection())
		; /* .loop_input */
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	SwapTurn();
	return false;
}

bool StretchKick_AISelectEffect(void)
{
	TRACE_FN();
	/* chooses Bench Pokemon with least amount of remaining HP */
	hTemp_ffa0 = AIFindTargetForBenchAttack();
	return false;
}

bool StretchKick_BenchDamageEffect(void)
{
	TRACE_FN();
	SwapTurn();
	DealDamageToPlayAreaPokemon_RegularAnim(hTemp_ffa0, 20);
	SwapTurn();
	return false;
}

bool SandAttackEffect(void)
{
	TRACE_FN();
	ApplySubstatus2ToDefendingCard(SUBSTATUS2_SAND_ATTACK);
	return false;
}

bool SandslashFurySwipes_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(60 / 2, 0, 60);
	return false;
}

bool SandslashFurySwipes_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(20);
	u8 a = TossCoinATimes_BankB(3, DamageCheckIfHeadsXDamageText);
	a = (u8)(a + a);
	SetDefiniteDamage(ATimes10(a));
	return false;
}

bool EarthquakeEffect(void)
{
	TRACE_FN();
	wIsDamageToSelf = TRUE;
	DealDamageToAllBenchedPokemon(10);
	return false;
}

bool PrehistoricPowerEffect(void)
{
	TRACE_FN();
	return true;
}

/* returns carry if Pkmn Power can't be used. */
bool Peek_OncePerTurnCheck(void)
{
	TRACE_FN();
	u8 a = hTempPlayAreaLocation_ff9d;
	hTemp_ffa0 = a;
	if (GetTurnDuelistVariable((u8)(a + DUELVARS_ARENA_CARD_FLAGS)) & USED_PKMN_POWER_THIS_TURN) {
		/* .already_used */
		gTextID = OnlyOncePerTurnText;
		return true;
	}
	return CheckIsIncapableOfUsingPkmnPower(hTempPlayAreaLocation_ff9d);
}

bool Peek_SelectEffect(void)
{
	TRACE_FN();
	/* set Pkmn Power used flag */
	TurnDuelistVars()[(u8)(hTemp_ffa0 + DUELVARS_ARENA_CARD_FLAGS)] |= USED_PKMN_POWER_THIS_TURN;

	u8 a = GetTurnDuelistVariable(DUELVARS_DUELIST_TYPE);
	if (a == DUELIST_TYPE_LINK_OPP)
		goto link_opp;
	if (a & DUELIST_TYPE_AI_OPP)
		goto ai_opp;

	/* player */
	FinishQueuedAnimations();
	hAIPkmnPowerEffectParam = HandlePeekSelection();
	SerialSend8Bytes();
	return false;

link_opp:
	SerialRecv8Bytes();
	/* register a as received: SerialRecv8Bytes pops af from wTempSerialBuf[0..1] */
	hAIPkmnPowerEffectParam = wTempSerialBuf[1];

ai_opp:
	a = hAIPkmnPowerEffectParam;
	if (a & AI_PEEK_TARGET_HAND) {
		a &= (u8)~AI_PEEK_TARGET_HAND; /* unset bit to get deck index */
		/* if masked value is higher than $40, then it means
		 * that AI chose to look at Player's deck.
		 * all deck indices will be smaller than $40. */
		if (a < 0x40) {
			/* .hand
			 * AI chose to look at random card in hand,
			 * so display it to the Player on screen. */
			SwapTurn();
			DisplayCardDetailScreen(a, PeekWasUsedToLookInYourHandText);
			SwapTurn();
			return false;
		}
	}

	/* .prize_or_deck
	 * AI chose either a prize card or Player's top deck card,
	 * so show Play Area and draw cursor appropriately. */
	FinishQueuedAnimations();
	SwapTurn();
	DrawAIPeekScreen(hAIPkmnPowerEffectParam ^ 0x80);
	SwapTurn();
	DrawWideTextBox_WaitForInput(CardPeekWasUsedOnText);
	return false;
}

bool BoneAttackEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(IfHeadsOpponentCannotAttackText))
		return false;
	ApplySubstatus2ToDefendingCard(SUBSTATUS2_BONE_ATTACK);
	return false;
}

/* return carry if neither Play Area
 * has room for more Bench Pokemon. */
bool Wail_BenchCheck(void)
{
	TRACE_FN();
	if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) < MAX_PLAY_AREA_POKEMON)
		return false;
	if (GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) < MAX_PLAY_AREA_POKEMON)
		return false;
	gTextID = NoSpaceOnTheBenchText;
	return true;
}

static void Wail_FillBenchEffect_FillBench(void)
{
	u8 n;
	if (CreateDeckCardList(&n))
		return;
	u8 *hl = wDuelTempList;
	ShuffleCards(hl, n);

	for (;;) {
		/* .check_bench: if no more space in the Bench, then return. */
		if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) >= MAX_PLAY_AREA_POKEMON)
			break;

		/* .loop: there's still space, so look for the next
		 * Basic Pokemon card to put in the Bench. */
		for (;;) {
			u8 a = *hl++;
			hTempCardIndex_ff98 = a;
			if (a == 0xff)
				goto done;
			LoadCardDataToBuffer2_FromDeckIndex(a);
			if (wLoadedCard2.Type >= TYPE_ENERGY)
				continue; /* is Pokemon card? */
			if (wLoadedCard2.Stage != 0)
				continue; /* is Basic? */
			break;
		}
		/* place card in Bench */
		SearchCardInDeckAndAddToHand(hTempCardIndex_ff98);
		AddCardToHand(hTempCardIndex_ff98);
		PutHandPokemonCardInPlayArea(hTempCardIndex_ff98, NULL);
	}

done:
	ShuffleCardsInDeck();
}

bool Wail_FillBenchEffect(void)
{
	TRACE_FN();
	SwapTurn();
	Wail_FillBenchEffect_FillBench();
	SwapTurn();
	Wail_FillBenchEffect_FillBench();

	/* display both Play Areas */
	DrawWideTextBox_WaitForInput(BasicPokemonWasPlacedOnEachBenchText);
	HasAlivePokemonInPlayArea(NULL);
	OpenPlayAreaScreenForSelection();
	SwapTurn();
	HasAlivePokemonInPlayArea(NULL);
	OpenPlayAreaScreenForSelection();
	SwapTurn();
	return false;
}

bool Thunderpunch_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage((30 + 40) / 2, 30, 40);
	return false;
}

bool Thunderpunch_ModifierEffect(void)
{
	TRACE_FN();
	bool heads = TossCoin_BankB(IfHeadPlus10IfTails10ToYourselfText);
	hTemp_ffa0 = heads; /* a = 1 if heads, 0 if tails */
	if (!heads)
		return false; /* return if got tails */
	AddToDamage(10);
	return false;
}

bool Thunderpunch_RecoilEffect(void)
{
	TRACE_FN();
	if (hTemp_ffa0 != 0)
		return false; /* return if got heads */
	DealRecoilDamageToSelf(10);
	return false;
}

bool LightScreenEffect(void)
{
	TRACE_FN();
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_HALVE_DAMAGE);
	return false;
}

bool ElectabuzzQuickAttack_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage((10 + 30) / 2, 10, 30);
	return false;
}

bool ElectabuzzQuickAttack_DamageBoostEffect(void)
{
	TRACE_FN();
	LoadTxRam3(20);
	if (!TossCoin_BankB(DamageCheckIfHeadsPlusDamageText))
		return false; /* return if tails */
	AddToDamage(20);
	return false;
}

bool MagnemiteSelfdestructEffect(void)
{
	TRACE_FN();
	DealRecoilDamageToSelf(40);

	wIsDamageToSelf = TRUE;
	DealDamageToAllBenchedPokemon(10);
	SwapTurn();

	wIsDamageToSelf = FALSE;
	DealDamageToAllBenchedPokemon(10);
	SwapTurn();
	return false;
}

bool ZapdosThunder_Recoil50PercentEffect(void)
{
	TRACE_FN();
	LoadTxRam3(30);
	bool heads = TossCoin_BankB(IfTailsDamageToYourselfTooText);
	hTemp_ffa0 = heads; /* a = 1 if heads, 0 if tails */
	return heads;
}

bool ZapdosThunder_RecoilEffect(void)
{
	TRACE_FN();
	LoadTxRam3(30);
	if (hTemp_ffa0 != 0)
		return false; /* return if got heads */
	DealRecoilDamageToSelf(30);
	return false;
}

bool ThunderboltEffect(void)
{
	TRACE_FN();
	CreateArenaOrBenchEnergyCardList(PLAY_AREA_ARENA);
	/* put all energy cards in Discard Pile */
	for (u8 *hl = wDuelTempList; *hl != 0xff; hl++)
		PutCardInDiscardPile(*hl);
	return false;
}

/* displays text for current Bench Pokemon,
 * printing its Bench number and name. */
static void ThunderstormEffect_DisplayText(u8 e)
{
	u8 b = e;
	u8 *de = CopyText(BenchText, wDefaultText);
	*de++ = (u8)('0' + b);
	*de++ = ' ';

	LoadCardDataToBuffer2_FromDeckIndex(GetTurnDuelistVariable((u8)(DUELVARS_ARENA_CARD + b)));
	CopyText(rd16(wLoadedCard2.Name), de);

	wDuelDisplayedScreen = 0;
}

bool ThunderstormEffect(void)
{
	TRACE_FN();
	hCurSelectionItem = 1;

	SwapTurn();
	u8 c = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	u8 b = 0;
	u8 e = b;
	for (;;) {
		/* .next_pkmn */
		e++;
		if (--c == 0)
			break;
		/* .check_damage */
		ThunderstormEffect_DisplayText(e);
		SwapTurn();
		bool heads = TossCoin_BankB(0); /* (ld de, $0: the text just built in wDefaultText) */
		SwapTurn();
		*GetNextPositionInTempList() = heads; /* store result in list */
		if (!heads)
			b++; /* increase number of tails */
	}

	/* all coins were tossed for each Benched Pokemon */
	*GetNextPositionInTempList() = 0xff;
	hTemp_ffa0 = b;
	ResetAnimationQueue();
	SwapTurn();

	/* tally recoil damage */
	if (hTemp_ffa0 != 0) {
		/* deal number of tails times 10 to self */
		DealRecoilDamageToSelf(ATimes10(hTemp_ffa0));
	}
	/* .skip_recoil */

	/* deal damage for Bench Pokemon that got heads */
	SwapTurn();
	u8 *hl = &hTempPlayAreaLocation_ffa1;
	b = PLAY_AREA_BENCH_1;
	for (;;) {
		/* .loop_bench */
		u8 a = *hl++;
		if (a == 0xff)
			break;
		if (a != 0) /* skip if tails */
			DealDamageToPlayAreaPokemon_RegularAnim(b, 20);
		/* .skip_damage */
		b++;
	}
	/* .done */
	SwapTurn();
	return false;
}

bool JolteonQuickAttack_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage((10 + 30) / 2, 10, 30);
	return false;
}

bool JolteonQuickAttack_DamageBoostEffect(void)
{
	TRACE_FN();
	LoadTxRam3(20);
	if (!TossCoin_BankB(DamageCheckIfHeadsPlusDamageText))
		return false; /* return if tails */
	AddToDamage(20);
	return false;
}

bool PinMissile_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage((20 * 4) / 2, 0, 80);
	return false;
}

bool PinMissile_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(20);
	u8 a = TossCoinATimes_BankB(4, DamageCheckIfHeadsXDamageText);
	a = (u8)(a + a); /* a = 2 * heads */
	SetDefiniteDamage(ATimes10(a));
	return false;
}

bool Fly_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(30 / 2, 0, 30);
	return false;
}

bool Fly_Success50PercentEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(SuccessCheckIfHeadsAttackIsSuccessfulText)) {
		wLoadedAttackAnimation = ATK_ANIM_NONE;
		SetDefiniteDamage(0);
		SetWasUnsuccessful();
		return false;
	}
	/* .heads */
	wLoadedAttackAnimation = ATK_ANIM_AGILITY_PROTECT;
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_FLY);
	return false;
}

bool ThunderJolt_Recoil50PercentEffect(void)
{
	TRACE_FN();
	LoadTxRam3(10);
	bool heads = TossCoin_BankB(IfTailsDamageToYourselfTooText);
	hTemp_ffa0 = heads; /* a = 1 if heads, 0 if tails */
	return heads;
}

bool ThunderJolt_RecoilEffect(void)
{
	TRACE_FN();
	LoadTxRam3(10);
	if (hTemp_ffa0 != 0)
		return false; /* return if was heads */
	DealRecoilDamageToSelf(10);
	return false;
}

bool Spark_PlayerSelectEffect(void)
{
	TRACE_FN();
	hTemp_ffa0 = 0xff;
	if (GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) < 2)
		return true; /* has no Bench Pokemon (ret c) */

	DrawWideTextBox_WaitForInput(ChoosePkmnInTheBenchToGiveDamageText);
	SwapTurn();
	HasAlivePokemonInBench(NULL);

	/* the following two instructions can be removed
	 * since Player selection will overwrite it. */
	hTempPlayAreaLocation_ff9d = PLAY_AREA_BENCH_1;

	while (OpenPlayAreaScreenForSelection())
		; /* .loop_input */
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	SwapTurn();
	return false;
}

bool Spark_AISelectEffect(void)
{
	TRACE_FN();
	hTemp_ffa0 = 0xff;
	if (GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) < 2)
		return true; /* has no Bench Pokemon (ret c) */
	/* AI always picks Pokemon with lowest HP remaining */
	hTemp_ffa0 = AIFindTargetForBenchAttack();
	return false;
}

bool Spark_BenchDamageEffect(void)
{
	TRACE_FN();
	if (hTemp_ffa0 == 0xff)
		return false;
	SwapTurn();
	DealDamageToPlayAreaPokemon_RegularAnim(hTemp_ffa0, 10);
	SwapTurn();
	return false;
}

bool PikachuLv16GrowlEffect(void)
{
	TRACE_FN();
	ApplySubstatus2ToDefendingCard(SUBSTATUS2_GROWL);
	return false;
}

bool PikachuAltLv16GrowlEffect(void)
{
	TRACE_FN();
	ApplySubstatus2ToDefendingCard(SUBSTATUS2_GROWL);
	return false;
}

static void ChainLightningEffect_DamageSameColorBench(void)
{
	u8 e = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	u8 d = PLAY_AREA_ARENA;
	for (;;) {
		/* .next_bench */
		d++;
		if (--e == 0)
			break;
		/* .check_damage */
		u8 c = GetPlayAreaCardColor(d);
		if (hCurSelectionItem != c)
			continue; /* skip if not same color */
		/* apply damage to this Bench card */
		DealDamageToPlayAreaPokemon_RegularAnim(d, 10);
	}
}

bool ChainLightningEffect(void)
{
	TRACE_FN();
	SetDefiniteDamage(10);
	SwapTurn();
	u8 a = GetArenaCardColor();
	SwapTurn();
	hCurSelectionItem = a;
	if (a == COLORLESS)
		return false; /* don't damage if colorless */

	/* opponent's Bench */
	SwapTurn();
	ChainLightningEffect_DamageSameColorBench();
	SwapTurn();

	/* own Bench */
	wIsDamageToSelf = TRUE;
	ChainLightningEffect_DamageSameColorBench();
	return false;
}

bool RaichuAgilityEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(IfHeadsDoNotReceiveDamageOrEffectText))
		return false; /* skip if got tails */
	wLoadedAttackAnimation = ATK_ANIM_AGILITY_PROTECT;
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_AGILITY);
	return false;
}

bool RaichuThunder_Recoil50PercentEffect(void)
{
	TRACE_FN();
	LoadTxRam3(30);
	bool heads = TossCoin_BankB(IfTailsDamageToYourselfTooText);
	hTemp_ffa0 = heads; /* a = 1 if heads, 0 if tails */
	return heads;
}

bool RaichuThunder_RecoilEffect(void)
{
	TRACE_FN();
	LoadTxRam3(30);
	if (hTemp_ffa0 != 0)
		return false; /* return if got heads */
	DealRecoilDamageToSelf(30);
	return false;
}

/* returns carry if Bench Pokemon
 * in register a was already chosen. */
static bool Gigashock_PlayerSelectEffect_CheckIfChosenAlready(u8 a)
{
	u8 c = (u8)(a + 1);
	u8 b = hCurSelectionItem;
	u8 *hl = hTempList;
	b++;
	while (--b) {
		/* .check_chosen */
		if (*hl++ == c)
			return true; /* return if chosen already */
	}
	return false;
}

bool Gigashock_PlayerSelectEffect(void)
{
	TRACE_FN();
	SwapTurn();
	if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) < 2) {
		SwapTurn();
		hTempList[0] = 0xff;
		return true; /* (carry from cp 2) */
	}

	/* .has_bench */
	DrawWideTextBox_WaitForInput(ChooseUpTo3PkmnOnBenchToGiveDamageText);

	/* init number of items in list and cursor position */
	hCurSelectionItem = 0;
	wCurGigashockItem = 0;
	SetupPlayAreaScreen();
start:;
	u8 n = PrintPlayAreaCardList_EnableLCD();
	InitializeMenuParameters(wCurGigashockItem, &BenchSelectionMenuParameters);

	/* exclude Arena Pokemon from number of items */
	wNumMenuItems = (u8)(n - 1);

loop_input:
	WaitForMenuInput();
	u8 a = hCurMenuItem;
	if (a == (u8)MENU_CANCEL)
		goto try_cancel;

	wCurGigashockItem = a;
	if (Gigashock_PlayerSelectEffect_CheckIfChosenAlready(a)) {
		/* play SFX */
		PlaySFX_InvalidChoice();
		goto loop_input;
	}

	/* .not_chosen: mark this Play Area location */
	DrawSymbolOnPlayAreaCursor((u8)(hCurMenuItem + 1), SYM_LIGHTNING);
	/* store it in the list of chosen Bench Pokemon */
	*GetNextPositionInTempList() = (u8)(hCurMenuItem + 1);

	/* check if 3 were chosen already */
	u8 c = hCurSelectionItem;
	if (c < 3) {
		if ((u8)(GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) - 1) != c)
			goto start; /* if sill more options available, loop back */
		/* fallthrough if no other options available to choose */
	}

	/* .chosen */
	DrawPlayAreaScreenToShowChanges((u8)(hCurMenuItem + 1));
	if (hKeysPressed & PAD_B)
		goto try_cancel;
	SwapTurn();
	*GetNextPositionInTempList() = 0xff; /* terminating byte */
	return false;

try_cancel:
	a = hCurSelectionItem;
	if (a == 0)
		goto start; /* none selected, can safely loop back to start */

	/* undo last selection made */
	a--;
	hCurSelectionItem = a;
	a = hTempList[a];

	DrawSymbolOnPlayAreaCursor(a, SYM_SPACE);
	EraseCursor();

	wCurGigashockItem = (u8)(a - 1);
	goto start;
}

bool Gigashock_AISelectEffect(void)
{
	TRACE_FN();
	/* if Bench has 3 Pokemon or less, no need for selection,
	 * since AI will choose them all. */
	u8 a = GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	if (a < MAX_PLAY_AREA_POKEMON - 1) {
		/* select them all */
		u8 *hl = hTempList;
		u8 b = PLAY_AREA_ARENA;
		for (;;) {
			/* .next_bench */
			b++;
			if (--a == 0)
				break;
			/* .select_bench */
			*hl++ = b;
		}
		*hl = 0xff; /* terminating byte */
		return true; /* (carry from cp MAX_PLAY_AREA_POKEMON - 1) */
	}

	/* .start_selection
	 * has more than 3 Bench cards, proceed to sort them
	 * by lowest remaining HP to highest, and pick first 3. */
	SwapTurn();
	a--;
	u8 c = a;
	u8 b = PLAY_AREA_BENCH_1;

	/* first select all of the Bench Pokemon and write to list */
	u8 *hl = hTempList;
	do {
		/* .loop_all */
		*hl++ = b;
		b++;
	} while (--c);
	*hl = 0x00; /* end list with $00 */

	/* then check each of the Bench Pokemon HP
	 * sort them from lowest remaining HP to highest.
	 * (note: `cp c / jr c, .loop_inner` skips the Pokemon with less HP, so the
	 * one with the most HP is moved first) */
	u8 *de = hTempList;
	do {
		/* .loop_outer */
		c = GetTurnDuelistVariable((u8)(*de + DUELVARS_ARENA_CARD_HP));
		hl = de + 1;
		for (;;) {
			/* .loop_inner */
			a = *hl++;
			if (a == 0)
				break; /* reaching $00 means it's end of list */
			a = GetTurnDuelistVariable((u8)(a + DUELVARS_ARENA_CARD_HP));
			if (a < c)
				continue;
			/* a Bench Pokemon was found with less HP */
			c = a; /* store its HP */

			/* switch the two */
			hl--;
			b = *hl;
			*hl++ = *de;
			*de = b;
		}
		/* .next */
		de++;
	} while (*de != 0);

	/* done */
	hTempList[3] = 0xff; /* terminating byte */
	SwapTurn();
	return false;
}

bool Gigashock_BenchDamageEffect(void)
{
	TRACE_FN();
	SwapTurn();
	u8 *hl = hTempList;
	for (;;) {
		/* .loop_selection */
		u8 a = *hl++;
		if (a == 0xff)
			break;
		DealDamageToPlayAreaPokemon_RegularAnim(a, 10);
	}
	/* .done */
	SwapTurn();
	return false;
}

bool MagnetonLv28SelfdestructEffect(void)
{
	TRACE_FN();
	DealRecoilDamageToSelf(80);

	/* own bench */
	wIsDamageToSelf = TRUE;
	DealDamageToAllBenchedPokemon(20);

	/* opponent's bench */
	SwapTurn();
	wIsDamageToSelf = FALSE;
	DealDamageToAllBenchedPokemon(20);
	SwapTurn();
	return false;
}

bool MagnetonSonicboom_UnaffectedByColorEffect(void)
{
	TRACE_FN();
	wDamage[1] |= 1 << UNAFFECTED_BY_WEAKNESS_RESISTANCE_F;
	return false;
}

bool MagnetonSonicboom_NullEffect(void)
{
	TRACE_FN();
	return false;
}

bool MagnetonLv35SelfdestructEffect(void)
{
	TRACE_FN();
	DealRecoilDamageToSelf(100);

	/* own bench */
	wIsDamageToSelf = TRUE;
	DealDamageToAllBenchedPokemon(20);

	/* opponent's bench */
	SwapTurn();
	wIsDamageToSelf = FALSE;
	DealDamageToAllBenchedPokemon(20);
	SwapTurn();
	return false;
}

bool PealOfThunder_InitialEffect(void)
{
	TRACE_FN();
	return true;
}

static void RandomlyDamagePlayAreaPokemon(u16 de);

bool PealOfThunder_RandomlyDamageEffect(void)
{
	TRACE_FN();
	ExchangeRNG();
	RandomlyDamagePlayAreaPokemon(30); /* damage to inflict */
	HandleDestinyBondAndBetweenTurnKnockOuts();
	return false;
}

/* RandomlyDamagePlayAreaPokemon.DoDamage: input b = PLAY_AREA_*, de = damage */
static void RandomlyDamagePlayAreaPokemon_DoDamage(u8 b, u16 de)
{
	wLoadedAttackAnimation = ATK_ANIM_THUNDER_PLAY_AREA;
	DealDamageToPlayAreaPokemon(b, de);
}

/* randomly damages a Pokemon in play, except
 * card that is in [hTempPlayAreaLocation_ff9d].
 * plays thunder animation when Play Area is shown.
 * input: de = amount of damage to deal */
static void RandomlyDamagePlayAreaPokemon(u16 de)
{
	u8 b;
	for (;;) {
		/* .sample */
		wNoDamageOrEffect = 0;

		/* choose randomly which Play Area to attack */
		if (UpdateRNGSources() & 1)
			break; /* .opp_play_area */

		/* own Play Area */
		wIsDamageToSelf = TRUE;
		b = Random(GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA));
		/* can't select Zapdos */
		if (hTempPlayAreaLocation_ff9d == b)
			continue; /* re-roll Pokemon to attack */
		RandomlyDamagePlayAreaPokemon_DoDamage(b, de);
		return;
	}

	/* .opp_play_area */
	wIsDamageToSelf = FALSE;
	SwapTurn();
	b = Random(GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA));
	RandomlyDamagePlayAreaPokemon_DoDamage(b, de);
	SwapTurn();
}

bool BigThunderEffect(void)
{
	TRACE_FN();
	ExchangeRNG();
	RandomlyDamagePlayAreaPokemon(70); /* damage to inflict */
	return false;
}

bool MagneticStormEffect(void)
{
	TRACE_FN();
	u8 *vars = TurnDuelistVars();

	/* writes in wDuelTempList all deck indices
	 * of Energy cards attached to Pokemon
	 * in the Turn Duelist's Play Area. */
	u8 *de = wDuelTempList;
	u8 c = 0;
	for (u8 l = DUELVARS_CARD_LOCATIONS; l < DECK_SIZE; l++) {
		/* .loop_card_locations */
		if (!(vars[l] & CARD_LOCATION_PLAY_AREA))
			continue;
		/* is a card that is in the Play Area */
		if (!(GetCardType(GetCardIDFromDeckIndex(l)) & TYPE_ENERGY))
			continue;
		/* is an Energy card attached to Pokemon in Play Area */
		*de++ = l;
		c++;
	}
	*de = 0xff; /* terminating byte */

	/* divide number of energy cards
	 * by number of Pokemon in Play Area */
	u8 b = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	u8 a = c;
	c = 0xff; /* -1 */
	bool borrow;
	do {
		/* .loop_division */
		c++;
		borrow = a < b;
		a = (u8)(a - b);
	} while (!borrow);
	/* c = floor(a / b) */

	/* evenly divides the Energy cards randomly
	 * to every Pokemon in the Play Area. */
	u8 saved_b = b, saved_c = c; /* push bc */
	u8 *hl = wDuelTempList;
	ShuffleCards(hl, CountCardsInDuelTempList());
	u8 d = c;
	u8 e = PLAY_AREA_ARENA;
	do {
		/* .start_attach */
		c = d;
		c++;
		while (--c) {
			/* .attach_energy */
			a = *hl++;
			AddCardToHand(a);
			PutHandCardInPlayArea(a, e);
		}
		/* .check_done: go to next Pokemon in Play Area */
		e++; /* next in Play Area */
	} while (--b);
	b = saved_b; /* pop bc */
	c = saved_c;

	u8 *rest = hl; /* push hl */

	/* fill hTempList with PLAY_AREA_* locations
	 * that have Pokemon in them. */
	hl = hTempList;
	a = 0;
	do {
		/* .loop_init */
		*hl++ = a;
		a++;
	} while (a != b);

	/* shuffle them and distribute
	 * the remaining cards in random order. */
	ShuffleCards(hTempList, b);
	hl = rest; /* pop hl */
	de = hTempList;
	for (;;) {
		/* .next_random_pokemon */
		if (*hl == 0xff)
			break;
		e = *de;
		a = *hl;
		AddCardToHand(a);
		PutHandCardInPlayArea(a, e);
		hl++;
		de++;
	}

	/* .done */
	DrawDuelMainScene();
	DrawDuelHUDs();
	DrawWideTextBox_WaitForInput(TheEnergyCardFromPlayAreaWasMovedText);
	DrawPlayAreaScreenToShowChanges(PLAY_AREA_ARENA);
	return false;
}

bool ElectrodeSonicboom_UnaffectedByColorEffect(void)
{
	TRACE_FN();
	wDamage[1] |= 1 << UNAFFECTED_BY_WEAKNESS_RESISTANCE_F;
	return false;
}

bool ElectrodeSonicboom_NullEffect(void)
{
	TRACE_FN();
	return false;
}

/* return carry if no cards in Deck */
bool EnergySpike_DeckCheck(void)
{
	TRACE_FN();
	return CheckIfDeckIsEmpty();
}

bool EnergySpike_PlayerSelectEffect(void)
{
	TRACE_FN();
	hTemp_ffa0 = 0xff;

	/* search cards in Deck */
	CreateDeckCardList(NULL);
	/* (bc = BasicEnergyText: the shared signature takes only hl,
	 * part 1 recovers bc from its table of LookForCardsInDeck callers) */
	if (LookForCardsInDeck(SEARCHEFFECT_BASIC_ENERGY, 0, Choose1BasicEnergyCardFromDeckText))
		return true;

	InitAndDrawCardListScreenLayout_WithSelectCheckMenu();
	SetCardListHeaderText(ChooseBasicEnergyCardText, DuelistDeckText);
	for (;;) {
		/* .select_card */
		u8 card;
		if (DisplayCardList(&card))
			goto try_cancel;
		u8 type = GetCardType(GetCardIDFromDeckIndex(card));
		if (type >= TYPE_ENERGY_DOUBLE_COLORLESS)
			continue; /* not a Basic Energy card */
		if (!(type & TYPE_ENERGY))
			continue; /* not a Basic Energy card */
		/* Energy card selected */

		hTemp_ffa0 = hTempCardIndex_ff98;
		EmptyScreen();
		DrawWideTextBox_WaitForInput(ChoosePokemonToAttachEnergyCardText);

		/* choose a Pokemon in Play Area to attach card */
		HasAlivePokemonInPlayArea(NULL);
		while (OpenPlayAreaScreenForSelection())
			; /* .loop_input */
		hTempPlayAreaLocation_ffa1 = hTempPlayAreaLocation_ff9d;
		return false;

	play_sfx:
		PlaySFX_InvalidChoice();
		continue;

	try_cancel:;
		/* Player tried exiting screen, if there are
		 * any Basic Energy cards, Player is forced to select them.
		 * otherwise, they can safely exit. */
		u8 *vars = TurnDuelistVars();
		for (u8 l = DUELVARS_CARD_LOCATIONS; l < DECK_SIZE; l++) {
			/* .loop_deck */
			if (vars[l] != CARD_LOCATION_DECK)
				continue;
			u8 a = GetCardType(GetCardIDFromDeckIndex(l)) & TYPE_ENERGY;
			if (a == 0)
				continue;
			/* bug (original): a is TYPE_ENERGY after the `and`, so this is always
			 * true and any Energy card in the Deck (Double Colorless Energy
			 * included) prevents exiting */
			if (a < TYPE_ENERGY_DOUBLE_COLORLESS)
				goto play_sfx;
		}
		/* can exit */
		hTemp_ffa0 = 0xff;
		return false;
	}
}

bool EnergySpike_AISelectEffect(void)
{
	TRACE_FN();
	/* AI doesn't choose card here,
	 * instead this is handled in AISelectSpecialAttackParameters */
	hTemp_ffa0 = 0xff;
	return false;
}

bool EnergySpike_AttachEnergyEffect(void)
{
	TRACE_FN();
	u8 a = hTemp_ffa0;
	if (a == 0xff)
		goto done;

	/* add card to hand and attach it to the selected Pokemon */
	SearchCardInDeckAndAddToHand(a);
	AddCardToHand(a);
	PutHandCardInPlayArea(hTemp_ffa0, hTempPlayAreaLocation_ffa1);
	if (IsPlayerTurn())
		goto done;

	/* not Player, so show detail screen
	 * and which Pokemon was chosen to attach Energy. */
	LoadCardDataToBuffer1_FromDeckIndex(
		GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ffa1 + DUELVARS_ARENA_CARD)));
	wTxRam2_b[0] = wLoadedCard1.Name[0];
	wTxRam2_b[1] = wLoadedCard1.Name[1];
	DisplayCardDetailScreen(hTemp_ffa0, AttachedEnergyToPokemonText);

done:
	ShuffleCardsInDeck();
	return false;
}

bool JolteonDoubleKick_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(40 / 2, 0, 40);
	return false;
}

bool JolteonDoubleKick_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(20);
	u8 a = TossCoinATimes_BankB(2, DamageCheckIfHeadsXDamageText);
	a = (u8)(a + a); /* a = 2 * heads */
	SetDefiniteDamage(ATimes10(a));
	return false;
}

bool TailWagEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(IfHeadsOpponentCannotAttackText)) {
		SetWasUnsuccessful();
		return false;
	}
	wLoadedAttackAnimation = ATK_ANIM_LURE;
	ApplySubstatus2ToDefendingCard(SUBSTATUS2_TAIL_WAG);
	return false;
}

bool EeveeQuickAttack_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage((10 + 30) / 2, 10, 30);
	return false;
}

bool EeveeQuickAttack_DamageBoostEffect(void)
{
	TRACE_FN();
	LoadTxRam3(20);
	if (!TossCoin_BankB(DamageCheckIfHeadsPlusDamageText))
		return false; /* return if tails */
	AddToDamage(20);
	return false;
}

/* these are effect commands that Mirror Move uses
 * in order to mimic last turn's attack.
 * it covers all possible effect steps to perform its commands
 * (i.e. selection for Amnesia and Energy discarding attacks, etc) */
static bool MirrorMove_AIEffect(void);
static bool MirrorMove_InitialEffect1(void);
static bool MirrorMove_InitialEffect2(void);
static bool MirrorMove_PlayerSelection(void);
static bool MirrorMove_AISelection(void);
static bool MirrorMove_BeforeDamage(void);
static bool MirrorMove_AfterDamage(void);
static bool MirrorMove_ExecuteStatusEffect(u8 a);
static bool HandleEnergyDiscardEffectSelection(void);

bool SpearowMirrorMove_AIEffect(void)
{
	TRACE_FN();
	return MirrorMove_AIEffect();
}

bool SpearowMirrorMove_InitialEffect1(void)
{
	TRACE_FN();
	return MirrorMove_InitialEffect1();
}

bool SpearowMirrorMove_InitialEffect2(void)
{
	TRACE_FN();
	return MirrorMove_InitialEffect2();
}

bool SpearowMirrorMove_PlayerSelection(void)
{
	TRACE_FN();
	return MirrorMove_PlayerSelection();
}

bool SpearowMirrorMove_AISelection(void)
{
	TRACE_FN();
	return MirrorMove_AISelection();
}

bool SpearowMirrorMove_BeforeDamage(void)
{
	TRACE_FN();
	return MirrorMove_BeforeDamage();
}

bool SpearowMirrorMove_AfterDamage(void)
{
	TRACE_FN();
	return MirrorMove_AfterDamage();
}

static bool MirrorMove_AIEffect(void)
{
	u8 a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_LAST_TURN_DAMAGE);
	wAIMinDamage = a;
	wAIMaxDamage = a;
	return false;
}

static bool MirrorMove_InitialEffect1(void)
{
	u8 *vars = TurnDuelistVars();
	u8 a = vars[DUELVARS_ARENA_CARD_LAST_TURN_DAMAGE];
	a |= vars[DUELVARS_ARENA_CARD_LAST_TURN_DAMAGE + 1];
	a |= vars[DUELVARS_ARENA_CARD_LAST_TURN_STATUS];
	if (a)
		return false; /* return if has last turn damage or status */
	if (vars[DUELVARS_ARENA_CARD_LAST_TURN_SUBSTATUS2])
		return false; /* return if has last turn substatus */
	/* no attack received last turn */
	gTextID = YouDidNotReceiveAnAttackToMirrorMoveText;
	return true;
}

static bool MirrorMove_InitialEffect2(void)
{
	hTemp_ffa0 = 0xff;
	u8 a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_LAST_TURN_EFFECT);
	if (a == 0)
		return false; /* no effect */
	if (a == LAST_TURN_EFFECT_AMNESIA)
		return PlayerPickAttackForAmnesia_Carry();
	return false;
}

static bool MirrorMove_PlayerSelection(void)
{
	u8 a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_LAST_TURN_EFFECT);
	if (a == 0)
		return false; /* no effect */
	/* handle Energy card discard effect */
	if (a == LAST_TURN_EFFECT_DISCARD_ENERGY)
		return HandleEnergyDiscardEffectSelection();
	return a < LAST_TURN_EFFECT_DISCARD_ENERGY; /* (carry from cp) */
}

static bool MirrorMove_AISelection(void)
{
	hTemp_ffa0 = 0xff;
	u8 a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_LAST_TURN_EFFECT);
	if (a == 0)
		return false; /* no effect */
	if (a == LAST_TURN_EFFECT_DISCARD_ENERGY) {
		/* .discard_energy */
		hTemp_ffa0 = AIPickEnergyCardToDiscardFromDefendingPokemon();
		return false;
	}
	if (a == LAST_TURN_EFFECT_AMNESIA) {
		/* .pick_amnesia_attack */
		hTemp_ffa0 = AIPickAttackForAmnesia();
		return false;
	}
	return a < LAST_TURN_EFFECT_AMNESIA; /* (carry from cp) */
}

static bool MirrorMove_BeforeDamage(void)
{
	/* if was attacked with Amnesia, apply it to the selected attack */
	if (GetTurnDuelistVariable(DUELVARS_ARENA_CARD_LAST_TURN_EFFECT) == LAST_TURN_EFFECT_AMNESIA) {
		/* .apply_amnesia */
		ApplyAmnesiaToAttack();
		return false;
	}

	/* otherwise, check if there was last turn damage,
	 * and write it to wDamage. */
	u8 *vars = TurnDuelistVars();
	wDamage[0] = vars[DUELVARS_ARENA_CARD_LAST_TURN_DAMAGE];
	wDamage[1] = vars[DUELVARS_ARENA_CARD_LAST_TURN_DAMAGE + 1];
	if (vars[DUELVARS_ARENA_CARD_LAST_TURN_DAMAGE + 1] | vars[DUELVARS_ARENA_CARD_LAST_TURN_DAMAGE])
		wLoadedAttackAnimation = ATK_ANIM_HIT;
	/* .no_damage
	 * check if there was a status applied to Defending Pokemon
	 * from the attack it used. */
	u8 *de = NonTurnDuelistVars(); /* (de = DUELVARS_ARENA_CARD_STATUS of this block) */
	bool carry = false;
	u8 a = vars[DUELVARS_ARENA_CARD_LAST_TURN_STATUS];
	if (a)
		carry = MirrorMove_ExecuteStatusEffect(a);
	/* .no_status
	 * hl is at DUELVARS_ARENA_CARD_LAST_TURN_SUBSTATUS2
	 * apply substatus2 to defending card */
	de[DUELVARS_ARENA_CARD_SUBSTATUS2] = vars[DUELVARS_ARENA_CARD_LAST_TURN_SUBSTATUS2];
	return carry;
}

static bool MirrorMove_AfterDamage(void)
{
	if (wNoDamageOrEffect)
		return false; /* is unaffected */
	if (GetTurnDuelistVariable(DUELVARS_ARENA_CARD_LAST_TURN_EFFECT) == LAST_TURN_EFFECT_DISCARD_ENERGY) {
		/* execute Energy discard effect for card chosen */
		SwapTurn();
		PutCardInDiscardPile(hTemp_ffa0);
		TurnDuelistVars()[DUELVARS_ARENA_CARD_LAST_TURN_EFFECT] = LAST_TURN_EFFECT_DISCARD_ENERGY;
		SwapTurn();
	}

	/* .change_weakness */
	u8 *hl = &TurnDuelistVars()[DUELVARS_ARENA_CARD_LAST_TURN_CHANGE_WEAK];
	if (*hl == 0)
		return false; /* weakness wasn't changed last turn */

	SwapTurn();
	LoadCardDataToBuffer2_FromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	SwapTurn();

	if (wLoadedCard2.Weakness == 0)
		return false; /* defending Pokemon has no weakness to change */

	/* apply same color weakness to Defending Pokemon */
	u8 a = *hl;
	NonTurnDuelistVars()[DUELVARS_ARENA_CARD_CHANGED_WEAKNESS] = a;

	/* print message of weakness color change */
	u8 c = 0xff; /* -1 */
	bool carry = false; /* (cleared by the `or a` on wLoadedCard2Weakness) */
	do {
		/* .loop_color */
		c++;
		bool bit7 = (a & 0x80) != 0; /* rla */
		a = (u8)((a << 1) | (carry ? 1 : 0));
		carry = bit7;
	} while (!carry);
	a = c;
	SwapTurn();
	LoadCardDataToBuffer1_FromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	LoadCardNameAndInputColor(a);
	DrawWideTextBox_PrintText(ChangedTheWeaknessOfPokemonToColorText);
	SwapTurn();
	return false;
}

static bool MirrorMove_ExecuteStatusEffect(u8 a)
{
	u8 c = a;
	a &= PSN_DBLPSN;
	if (a) {
		u8 b = a;
		if (a == DOUBLE_POISONED)
			DoublePoisonEffect();
		if (b == POISONED)
			PoisonEffect();
	}
	/* .cnf_slp_prz */
	a = c & CNF_SLP_PRZ;
	if (a == 0)
		return false;
	if (a == CONFUSED)
		return ConfusionEffect();
	if (a == ASLEEP)
		return SleepEffect();
	if (a == PARALYZED)
		return ParalysisEffect();
	return a < PARALYZED; /* (carry from cp) */
}

bool FearowAgilityEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(IfHeadsDoNotReceiveDamageOrEffectText))
		return false;
	wLoadedAttackAnimation = ATK_ANIM_AGILITY_PROTECT;
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_AGILITY);
	return false;
}

/* return carry if cannot use Step In */
bool StepIn_BenchCheck(void)
{
	TRACE_FN();
	u8 a = hTempPlayAreaLocation_ff9d;
	hTemp_ffa0 = a;
	gTextID = CanOnlyBeUsedOnTheBenchText;
	if (a == 0)
		return true;

	a = GetTurnDuelistVariable((u8)(a + DUELVARS_ARENA_CARD_FLAGS));
	gTextID = OnlyOncePerTurnText;
	if (a & USED_PKMN_POWER_THIS_TURN)
		return true;

	return CheckIsIncapableOfUsingPkmnPower(hTempPlayAreaLocation_ff9d);
}

bool StepIn_SwitchEffect(void)
{
	TRACE_FN();
	SwapArenaWithBenchPokemon(hTemp_ffa0);
	TurnDuelistVars()[DUELVARS_ARENA_CARD_FLAGS] |= USED_PKMN_POWER_THIS_TURN;
	return false;
}

bool DragoniteLv45Slam_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage((40 * 2) / 2, 0, 80);
	return false;
}

bool DragoniteLv45Slam_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(40);
	u8 a = TossCoinATimes_BankB(2, DamageCheckIfHeadsXDamageText);
	a = (u8)(a + a);
	a = (u8)(a + a);
	SetDefiniteDamage(ATimes10(a));
	return false;
}

bool ThickSkinnedEffect(void)
{
	TRACE_FN();
	return true;
}

bool LeekSlap_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage(30 / 2, 0, 30);
	return false;
}

/* return carry if already used attack in this duel */
bool LeekSlap_OncePerDuelCheck(void)
{
	TRACE_FN();
	/* can only use attack if it was never used before this duel */
	if (!(GetTurnDuelistVariable(DUELVARS_ARENA_CARD_FLAGS) & USED_LEEK_SLAP_THIS_DUEL))
		return false;
	gTextID = ThisAttackCannotBeUsedTwiceText;
	return true;
}

bool LeekSlap_SetUsedThisDuelFlag(void)
{
	TRACE_FN();
	TurnDuelistVars()[DUELVARS_ARENA_CARD_FLAGS] |= USED_LEEK_SLAP_THIS_DUEL;
	return false;
}

bool LeekSlap_NoDamage50PercentEffect(void)
{
	TRACE_FN();
	if (TossCoin_BankB(DamageCheckIfTailsNoDamageText))
		return true;
	SetDefiniteDamage(0); /* 0 damage */
	return false;
}

bool FetchEffect(void)
{
	TRACE_FN();
	DrawWideTextBox_WaitForInput(Draw1CardFromTheDeckText);
	DisplayDrawOneCardScreen();
	u8 card;
	if (DrawCardFromDeck(&card))
		return true; /* return if deck is empty */
	AddCardToHand(card);
	LoadCardDataToBuffer1_FromDeckIndex(card);
	if (wDuelistType != DUELIST_TYPE_PLAYER)
		return false;
	/* show card on screen if it was Player */
	OpenCardPage_FromHand();
	return false;
}

bool CometPunch_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage((20 * 4) / 2, 0, 80);
	return false;
}

bool CometPunch_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(20);
	u8 a = TossCoinATimes_BankB(4, DamageCheckIfHeadsXDamageText);
	a = (u8)(a + a);
	SetDefiniteDamage(ATimes10(a));
	return false;
}

bool TaurosStomp_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage((20 + 30) / 2, 20, 30);
	return false;
}

bool TaurosStomp_DamageBoostEffect(void)
{
	TRACE_FN();
	LoadTxRam3(10);
	if (!TossCoin_BankB(DamageCheckIfHeadsPlusDamageText))
		return false; /* tails */
	AddToDamage(10);
	return false;
}

bool Rampage_AIEffect(void)
{
	TRACE_FN();
	AddToDamage(GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL));
	SetDefiniteAIDamage();
	return false;
}

bool Rampage_Confusion50PercentEffect(void)
{
	TRACE_FN();
	AddToDamage(GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL));
	if (TossCoin_BankB(IfTailsYourPokemonBecomesConfusedText))
		return true; /* heads */
	SwapTurn();
	bool carry = ConfusionEffect();
	SwapTurn();
	return carry;
}

bool FuryAttack_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage((10 * 2) / 2, 0, 20);
	return false;
}

bool FuryAttack_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(10);
	u8 a = TossCoinATimes_BankB(2, DamageCheckIfHeadsXDamageText);
	SetDefiniteDamage(ATimes10(a));
	return false;
}

bool RetreatAidEffect(void)
{
	TRACE_FN();
	return true;
}

bool DodrioRage_AIEffect(void)
{
	TRACE_FN();
	DodrioRage_DamageBoostEffect();
	SetDefiniteAIDamage();
	return false;
}

bool DodrioRage_DamageBoostEffect(void)
{
	TRACE_FN();
	AddToDamage(GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL));
	return false;
}

bool PayDayEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(IfHeadsDraw1CardFromDeckText))
		return false; /* tails */
	DrawWideTextBox_WaitForInput(Draw1CardFromTheDeckText);
	DisplayDrawOneCardScreen();
	u8 card;
	if (DrawCardFromDeck(&card))
		return true; /* empty deck */
	AddCardToHand(card);
	LoadCardDataToBuffer1_FromDeckIndex(card);
	if (wDuelistType != DUELIST_TYPE_PLAYER)
		return false;
	/* show card on screen if it was Player */
	OpenCardPage_FromHand();
	return false;
}

bool DragonairSlam_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage((30 * 2) / 2, 0, 60);
	return false;
}

bool DragonairSlam_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(30);
	u8 e = TossCoinATimes_BankB(2, DamageCheckIfHeadsXDamageText);
	u8 a = (u8)(e + e);
	a = (u8)(a + e);
	SetDefiniteDamage(ATimes10(a));
	return false;
}

bool DragonairHyperBeam_PlayerSelectEffect(void)
{
	TRACE_FN();
	return HandleEnergyDiscardEffectSelection();
}

bool DragonairHyperBeam_AISelectEffect(void)
{
	TRACE_FN();
	hTemp_ffa0 = AIPickEnergyCardToDiscardFromDefendingPokemon();
	return false;
}

bool DragonairHyperBeam_DiscardEffect(void)
{
	TRACE_FN();
	if (HandleNoDamageOrEffect())
		return true; /* is unaffected */
	if (hTemp_ffa0 == 0xff)
		return false; /* no energy card chosen */
	SwapTurn();
	PutCardInDiscardPile(hTemp_ffa0);
	TurnDuelistVars()[DUELVARS_ARENA_CARD_LAST_TURN_EFFECT] = LAST_TURN_EFFECT_DISCARD_ENERGY;
	SwapTurn();
	return false;
}

/* handles screen for selecting an Energy card to discard
 * that is attached to Defending Pokemon,
 * and store the Player selection in [hTemp_ffa0]. */
static bool HandleEnergyDiscardEffectSelection(void)
{
	SwapTurn();
	if (CreateArenaOrBenchEnergyCardList(PLAY_AREA_ARENA)) {
		/* .no_energy */
		SwapTurn();
		hTemp_ffa0 = 0xff;
		return true; /* (carry from CreateArenaOrBenchEnergyCardList) */
	}
	DrawWideTextBox_WaitForInput(ChooseDiscardEnergyCardFromOpponentText);
	DisplayEnergyDiscardScreen(PLAY_AREA_ARENA);

	while (HandleEnergyDiscardMenuInput())
		; /* .loop_input */

	SwapTurn();
	hTemp_ffa0 = hTempCardIndex_ff98; /* store selected card to discard */
	return false;
}

static bool HandlePlayerMetronomeEffect(u8 a);
static void HandleAIMetronomeEffect(void);

/* return carry if Defending Pokemon has no attacks */
bool ClefableMetronome_CheckAttacks(void)
{
	TRACE_FN();
	bool carry = CheckIfDefendingPokemonHasAnyAttack();
	gTextID = NoAttackMayBeChoosenText;
	return carry;
}

bool ClefableMetronome_AISelectEffect(void)
{
	TRACE_FN();
	HandleAIMetronomeEffect();
	return false;
}

bool ClefableMetronome_UseAttackEffect(void)
{
	TRACE_FN();
	return HandlePlayerMetronomeEffect(1); /* energy cost of this attack */
}

bool ClefableMinimizeEffect(void)
{
	TRACE_FN();
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_REDUCE_BY_20);
	return false;
}

bool HurricaneEffect(void)
{
	TRACE_FN();
	if (HandleNoDamageOrEffect())
		return true; /* is unaffected */

	if (GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_HP) == 0)
		return false; /* return if Pokemon was KO'd */

	/* look at all the card locations and put all cards
	 * that are in the Arena in the hand. */
	SwapTurn();
	u8 *vars = TurnDuelistVars();
	for (u8 l = DUELVARS_CARD_LOCATIONS; l < DECK_SIZE; l++) {
		/* .loop_locations */
		if (vars[l] != CARD_LOCATION_ARENA)
			continue;
		/* card in Arena found, put in hand */
		AddCardToHand(l);
	}

	/* empty the Arena card slot */
	u8 a = vars[DUELVARS_ARENA_CARD];
	vars[DUELVARS_ARENA_CARD] = 0xff;
	vars[DUELVARS_ARENA_CARD_HP] = 0;
	LoadCardDataToBuffer1_FromDeckIndex(a);
	LoadTxRam2(rd16(wLoadedCard1.Name));
	DrawWideTextBox_WaitForInput(PokemonAndAllAttachedCardsReturnedToHandText);
	wDuelDisplayedScreen = 0;
	SwapTurn();
	return false;
}

bool PidgeottoWhirlwind_SelectEffect(void)
{
	TRACE_FN();
	if (GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) < 2) {
		/* no Bench Pokemon */
		hTemp_ffa0 = 0xff;
		return true; /* (carry from cp 2) */
	}
	/* .switch */
	DuelistSelectForcedSwitch();
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	return false;
}

bool PidgeottoWhirlwind_SwitchEffect(void)
{
	TRACE_FN();
	HandleSwitchDefendingPokemonEffect(hTemp_ffa0);
	return false;
}

bool PidgeottoMirrorMove_AIEffect(void)
{
	TRACE_FN();
	return MirrorMove_AIEffect();
}

bool PidgeottoMirrorMove_InitialEffect1(void)
{
	TRACE_FN();
	return MirrorMove_InitialEffect1();
}

bool PidgeottoMirrorMove_InitialEffect2(void)
{
	TRACE_FN();
	return MirrorMove_InitialEffect2();
}

bool PidgeottoMirrorMove_PlayerSelection(void)
{
	TRACE_FN();
	return MirrorMove_PlayerSelection();
}

bool PidgeottoMirrorMove_AISelection(void)
{
	TRACE_FN();
	return MirrorMove_AISelection();
}

bool PidgeottoMirrorMove_BeforeDamage(void)
{
	TRACE_FN();
	return MirrorMove_BeforeDamage();
}

bool PidgeottoMirrorMove_AfterDamage(void)
{
	TRACE_FN();
	return MirrorMove_AfterDamage();
}

bool SingEffect(void)
{
	TRACE_FN();
	bool carry = Sleep50PercentEffect();
	if (!carry)
		SetNoEffectFromStatus();
	return carry;
}

/* return carry if Defending Pokemon has no attacks */
bool ClefairyMetronome_CheckAttacks(void)
{
	TRACE_FN();
	bool carry = CheckIfDefendingPokemonHasAnyAttack();
	gTextID = NoAttackMayBeChoosenText;
	return carry;
}

bool ClefairyMetronome_AISelectEffect(void)
{
	TRACE_FN();
	HandleAIMetronomeEffect();
	return false;
}

bool ClefairyMetronome_UseAttackEffect(void)
{
	TRACE_FN();
	return HandlePlayerMetronomeEffect(3); /* energy cost of this attack (fallthrough) */
}

/* handles Metronome selection, and validates
 * whether it can use the selected attack.
 * if unsuccessful, returns carry.
 * input: a = amount of colorless energy needed for Metronome */
static bool HandlePlayerMetronomeEffect(u8 a)
{
	wMetronomeEnergyCost = a;
	DrawWideTextBox_WaitForInput(ChooseOppAttackToBeUsedWithMetronomeText);

	u8 d, e;
	if (HandleDefendingPokemonAttackSelection(&d, &e))
		return true; /* return if operation cancelled */

	/* store this attack as selected attack to use */
	wMetronomeSelectedAttack[0] = d;
	wMetronomeSelectedAttack[1] = e;

	/* compare selected attack's name with
	 * the attack that is loaded, which is Metronome.
	 * if equal, then cannot select it.
	 * (i.e. cannot use Metronome with Metronome.) */
	u16 name = rd16(wLoadedAttack.Name);
	SwapTurn();
	CopyAttackDataAndDamage_FromDeckIndex(d, e);
	SwapTurn();
	u16 hl;
	if ((u8)name != wLoadedAttack.Name[0] || (u8)(name >> 8) != wLoadedAttack.Name[1])
		goto try_use;
	/* cannot select Metronome */
	hl = UnableToSelectText;
failed:
	DrawWideTextBox_WaitForInput(hl);
	/* .set_carry */
	return true;

try_use:
	/* run the attack checks to determine
	 * whether it can be used. */
	if (TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_1)) {
		hl = gTextID;
		goto failed;
	}

	/* run initial effect 2 where player selection
	 * happens for some attacks */
	if (TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_2))
		return true; /* .set_carry */
	/* successful */

	/* send data to link opponent */
	SendAttackDataToLinkOpponent();
	SetOppAction_SerialSendDuelData(OPPACTION_USE_METRONOME_ATTACK);
	/* (d, e = wMetronomeSelectedAttack, c = wMetronomeEnergyCost are the data sent) */
	SerialSend8Bytes();

	wPlayerAttackingCardIndex = hTempCardIndex_ff9f;
	wPlayerAttackingAttackIndex = wSelectedAttack;
	wPlayerAttackingCardID = wTempCardID_ccc2;
	return false;
}

/* does nothing for AI. */
static void HandleAIMetronomeEffect(void)
{
}

bool DoTheWaveEffect(void)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	a--; /* don't count arena card */
	AddToDamage(ATimes10(a));
	return false;
}

/* return carry if no damage counters */
bool FirstAid_DamageCheck(void)
{
	TRACE_FN();
	u8 a = GetCardDamageAndMaxHP(PLAY_AREA_ARENA, NULL);
	gTextID = NoDamageCountersText;
	return a < 10;
}

bool FirstAid_HealEffect(void)
{
	TRACE_FN();
	ApplyAndAnimateHPRecovery(10);
	return false;
}

bool JigglypuffDoubleEdgeEffect(void)
{
	TRACE_FN();
	DealRecoilDamageToSelf(20);
	return false;
}

bool PounceEffect(void)
{
	TRACE_FN();
	ApplySubstatus2ToDefendingCard(SUBSTATUS2_POUNCE);
	return false;
}

bool LickitungSupersonicEffect(void)
{
	TRACE_FN();
	bool carry = Confusion50PercentEffect();
	if (!carry)
		SetNoEffectFromStatus();
	return carry;
}

bool PidgeyWhirlwind_SelectEffect(void)
{
	TRACE_FN();
	if (GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) < 2) {
		/* no Bench Pokemon */
		hTemp_ffa0 = 0xff;
		return true; /* (carry from cp 2) */
	}
	/* .switch */
	DuelistSelectForcedSwitch();
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	return false;
}

bool PidgeyWhirlwind_SwitchEffect(void)
{
	TRACE_FN();
	HandleSwitchDefendingPokemonEffect(hTemp_ffa0);
	return false;
}

static void PrintArenaCardNameAndColorText(u16 hl);
static bool AISelectConversionColor(void);

/* return carry if Defending card has no weakness */
bool Conversion1_WeaknessCheck(void)
{
	TRACE_FN();
	SwapTurn();
	LoadCardDataToBuffer2_FromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	SwapTurn();
	if (wLoadedCard2.Weakness)
		return false;
	gTextID = NoWeaknessText;
	return true;
}

bool Conversion1_PlayerSelectEffect(void)
{
	TRACE_FN();
	u8 color;
	bool carry = HandleColorChangeScreen(PLAY_AREA_ARENA, ChooseWeaknessYouWishToChangeText, &color);
	/* a = selected color, or MENU_CANCEL (left by HandleMenuInput) if B was pressed */
	hTemp_ffa0 = carry ? (u8)MENU_CANCEL : color;
	return carry;
}

bool Conversion1_AISelectEffect(void)
{
	TRACE_FN();
	return AISelectConversionColor();
}

bool Conversion1_ChangeWeaknessEffect(void)
{
	TRACE_FN();
	if (HandleNoDamageOrEffect())
		return true; /* is unaffected */

	/* apply changed weakness */
	u8 *vars = NonTurnDuelistVars();
	u8 a = TranslateColorToWR(hTemp_ffa0);
	vars[DUELVARS_ARENA_CARD_CHANGED_WEAKNESS] = a;
	vars[DUELVARS_ARENA_CARD_LAST_TURN_CHANGE_WEAK] = a;

	/* print text box */
	SwapTurn();
	PrintArenaCardNameAndColorText(ChangedTheWeaknessOfPokemonToColorText);
	SwapTurn();

	/* apply substatus */
	ApplySubstatus2ToDefendingCard(SUBSTATUS2_CONVERSION2);
	return false;
}

/* returns carry if Active Pokemon has no Resistance. */
bool Conversion2_ResistanceCheck(void)
{
	TRACE_FN();
	LoadCardDataToBuffer2_FromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	if (wLoadedCard2.Resistance)
		return false;
	gTextID = NoResistanceText;
	return true;
}

bool Conversion2_PlayerSelectEffect(void)
{
	TRACE_FN();
	u8 color;
	bool carry = HandleColorChangeScreen(0x80, ChooseResistanceYouWishToChangeText, &color);
	/* a = selected color, or MENU_CANCEL (left by HandleMenuInput) if B was pressed */
	hTemp_ffa0 = carry ? (u8)MENU_CANCEL : color;
	return carry;
}

bool Conversion2_AISelectEffect(void)
{
	TRACE_FN();
	/* AI will choose Defending Pokemon's color
	 * unless it is colorless. */
	SwapTurn();
	LoadCardDataToBuffer1_FromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	SwapTurn();
	u8 a = wLoadedCard1.Type;
	if (a != COLORLESS) {
		hTemp_ffa0 = a;
		return a < COLORLESS; /* (carry from cp) */
	}

	/* .is_colorless */
	SwapTurn();
	bool carry = AISelectConversionColor();
	SwapTurn();
	return carry;
}

bool Conversion2_ChangeResistanceEffect(void)
{
	TRACE_FN();
	/* apply changed resistance */
	TurnDuelistVars()[DUELVARS_ARENA_CARD_CHANGED_RESISTANCE] = TranslateColorToWR(hTemp_ffa0);
	/* fallthrough */
	PrintArenaCardNameAndColorText(ChangedTheResistanceOfPokemonToColorText);
	return false;
}

/* prints text that requires card name and color,
 * with the card name of the Turn Duelist's Arena Pokemon
 * and color in [hTemp_ffa0].
 * input: hl = text to print */
static void PrintArenaCardNameAndColorText(u16 hl)
{
	LoadCardDataToBuffer1_FromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	LoadCardNameAndInputColor(hTemp_ffa0);
	DrawWideTextBox_PrintText(hl);
}

/* handles AI logic for selecting a new color
 * for weakness/resistance.
 * - if within the context of Conversion1, looks
 * in own Bench for a non-colorless card that can attack.
 * - if within the context of Conversion2, looks
 * in Player's Bench for a non-colorless card that can attack. */
static bool AISelectConversionColor(void)
{
	u8 d = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	u8 e = PLAY_AREA_ARENA;

	/* look for a non-colorless Bench Pokemon
	 * that has enough energy to use an attack. */
	for (;;) {
		/* .next_pkmn_atk */
		e++;
		if (--d == 0)
			break;
		/* .loop_atk */
		GetPlayAreaCardAttachedEnergies(e);
		u8 card = GetTurnDuelistVariable((u8)(e + DUELVARS_ARENA_CARD));
		LoadCardDataToBuffer1_FromDeckIndex(card);
		if (wLoadedCard1.Type == COLORLESS)
			continue; /* skip colorless Pokemon */
		if (!CheckIfEnoughEnergiesForGivenAttack(card, FIRST_ATTACK_OR_PKMN_POWER))
			goto found;
		if (!CheckIfEnoughEnergiesForGivenAttack(card, SECOND_ATTACK))
			goto found;
		/* .skip_pkmn_atk */
	}

	/* none found in Bench.
	 * next, look for a non-colorless Bench Pokemon
	 * that has any Energy cards attached. */
	d = e; /* number of Play Area Pokemon */
	e = PLAY_AREA_ARENA;
	for (;;) {
		/* .next_pkmn_energy */
		e++;
		if (--d == 0)
			break;
		/* .loop_energy */
		GetPlayAreaCardAttachedEnergies(e);
		if (wTotalAttachedEnergies == 0)
			continue; /* .skip_pkmn_energy */
		LoadCardDataToBuffer1_FromDeckIndex(GetTurnDuelistVariable((u8)(e + DUELVARS_ARENA_CARD)));
		if (wLoadedCard1.Type != COLORLESS)
			goto found;
		/* .skip_pkmn_energy */
	}

	/* otherwise, just select a random energy. */
	hTemp_ffa0 = Random(NUM_COLORED_TYPES);
	return false;

found:
	hTemp_ffa0 = wLoadedCard1.Type & TYPE_PKMN;
	return false;
}
