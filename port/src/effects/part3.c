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

/* PlayAreaSelectionMenuParameters and BenchSelectionMenuParameters are data
 * tables of part 1's range (effect_functions.asm line 1419); copies for part 3 */
static const struct MenuParams PlayAreaSelectionMenuParameters = {
	0, 0,                  /* cursor x, cursor y */
	3,                     /* y displacement between items */
	MAX_PLAY_AREA_POKEMON, /* number of items */
	SYM_CURSOR_R,          /* cursor tile number */
	SYM_SPACE,             /* tile behind cursor */
	0,                     /* function pointer if non-0 */
	TCG_MENU_PLAY_AREA,
};

static const struct MenuParams BenchSelectionMenuParameters = {
	0, 3,                  /* cursor x, cursor y */
	3,                     /* y displacement between items */
	MAX_PLAY_AREA_POKEMON, /* number of items */
	SYM_CURSOR_R,          /* cursor tile number */
	SYM_SPACE,             /* tile behind cursor */
	0,                     /* function pointer if non-0 */
	TCG_MENU_PLAY_AREA,
};

/* `call DoFrame / call HandleMenuInput / jr nc, .loop_input` */
static void WaitForMenuInput(void)
{
	do {
		DoFrame();
	} while (!HandleMenuInput());
}

/*
 * PlayerPickAttackForAmnesia (part 2) returns the carry of
 * HandleDefendingPokemonAttackSelection (set if the Player pressed B), but its
 * shared C signature is void. HandleDefendingPokemonAttackSelection only
 * returns carry from its `bit B_PAD_B, [hKeysPressed]` check, and on the A
 * path hKeysPressed holds the A press, so the carry is recomputed from it.
 */
static bool PlayerPickAttackForAmnesia_Carry(void)
{
	PlayerPickAttackForAmnesia();
	return (hKeysPressed & PAD_B) != 0;
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
	/* bc = FightingPokemonDeckText (LoadTxRam2'd by LookForCardsInDeck if none
	 * is found): the shared signature only takes hl, see the part 3 report */
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

/* @@PART3_CONTINUE@@ */
