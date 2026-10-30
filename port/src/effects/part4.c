/*
 * Port of src/engine/duel/effect_functions.asm, part 4:
 * from ScrunchEffect (line 8421) to the end of the file (most Trainer card effects).
 *
 * Notes that apply to the whole file:
 * - Effect functions are entered with carry clear (TryExecuteEffectCommandFunction
 *   does `or a` before CallHL), so "ret" after instructions that do not touch the
 *   carry returns false.
 * - After OpenPlayAreaScreenForSelection, register a is hTempPlayAreaLocation_ff9d
 *   (also copied to hCurMenuItem), also after B; the C below reads it from there.
 * - PlayAttackAnimation takes the damage to show in de. Where the asm does not set de
 *   (PlayTrainerEffectAnimation, HealingWind_PlayAreaHealEffect's first animation) it
 *   is a leftover of the caller that only ends up in wDamageAnimAmount (read by the
 *   animation code only); 0 is passed.
 * - CopyCardNameAndLevel returns hl at the end of the name in wDefaultText, and the
 *   callers here terminate it with `ld [hl], $00`; the duel_ui.h shim takes care of the
 *   default text (as for PrintPokemonsAttackText in home_duel.c).
 */
#include "effects.h"
#include "part4_needs.h"

static bool PickRandomBasicCardFromDeck(void);
static bool CheckIfThereAreAnyEnergyCardsAttached(void);
static bool HandlePokemonAndEnergySelectionScreen(void);
static bool CheckIfCardIsBasicEnergy(u8 card);
static bool CreatePlayableStage2PokemonCardListFromHand(void);
static bool CreatePokemonCardListFromHand(void);
static bool CreateBasicPokemonCardListFromDiscardPile(void);
static u8 *GetNextPositionInTempList_TrainerEffects(void);
static bool HandlePlayerSelection2HandCardsToDiscard(void);
static bool HandlePlayerSelection2HandCards(u16 hl, u16 de);
static void PlayTrainerEffectAnimation(u8 anim);
static bool HealPlayAreaCardHP(u8 a);

/* DisplayCardList, also giving register a as the original leaves it: the selected
 * card (hTempCardIndex_ff98) if a card was selected, or hCurMenuItem ($ff) if B was
 * pressed (DisplayCardList.b_pressed: ldh a, [hCurMenuItem] / scf). Used where the asm
 * stores or uses a after DisplayCardList. */
static bool DisplayCardList_RegA(u8 *a)
{
	u8 card = 0;
	bool carry = DisplayCardList(&card);
	*a = carry ? hCurMenuItem : hTempCardIndex_ff98;
	return carry;
}

/* LookForCardsInDeck takes two texts: hl = text printed if the deck has a matching
 * card, bc = card name text that goes to TX_RAM2 ("There is no <bc> in the deck")
 * if not. effects.h only has one text parameter; part1.c recovers the other one from
 * a table of the callers' (hl, bc) pairs, so hl is passed. Pass both here once the
 * shared signature takes them. */
static bool LookForCardsInDeck_HL_BC(u8 d, u8 e, u16 hl, u16 bc)
{
	(void)bc;
	return LookForCardsInDeck(d, e, hl);
}

bool ScrunchEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(IfHeadsNoDamageNextTurnText)) {
		SetWasUnsuccessful();
		return false;
	}
	wLoadedAttackAnimation = ATK_ANIM_SCRUNCH;
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_NO_DAMAGE_SCRUNCH);
	return true; /* carry from the coin toss (ApplySubstatus1ToAttackingCard keeps af) */
}

bool ChanseyDoubleEdgeEffect(void)
{
	TRACE_FN();
	DealRecoilDamageToSelf(80);
	return false; /* DealConfusionDamageToSelf ends with pop af (flags from entry) */
}

bool SuperFang_AIEffect(void)
{
	TRACE_FN();
	SuperFang_HalfHPEffect();
	SetDefiniteAIDamage();
	return false;
}

bool SuperFang_HalfHPEffect(void)
{
	TRACE_FN();
	u8 a = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);
	a >>= 1;
	if (a & 1) {
		/* round up */
		a += 5;
	}
	SetDefiniteDamage(a);
	return false;
}

/* return carry if no Pokemon in Bench */
bool TrainerCardAsPokemon_BenchCheck(void)
{
	TRACE_FN();
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	gTextID = EffectNoPokemonOnTheBenchText;
	return a < 2;
}

bool TrainerCardAsPokemon_PlayerSelectSwitch(void)
{
	TRACE_FN();
	if (hTemp_ffa0 != 0)
		return false; /* no need to switch if it's not Arena card */

	DrawWideTextBox_WaitForInput(SelectPokemonToPlaceInTheArenaText);
	HasAlivePokemonInBench(NULL);
	bool carry = OpenPlayAreaScreenForSelection();
	hTempPlayAreaLocation_ffa1 = hTempPlayAreaLocation_ff9d;
	return carry;
}

bool TrainerCardAsPokemon_DiscardEffect(void)
{
	TRACE_FN();
	MovePlayAreaCardToDiscardPile(hTemp_ffa0);
	if (hTemp_ffa0 == 0)
		SwapArenaWithBenchPokemon(hTempPlayAreaLocation_ffa1);
	/* shift_cards */
	ShiftAllPokemonToFirstPlayAreaSlots();
	return false;
}

bool HealingWind_InitialEffect(void)
{
	TRACE_FN();
	return true;
}

bool HealingWind_PlayAreaHealEffect(void)
{
	TRACE_FN();
	/* play initial animation (de: leftover, see top of file) */
	PlayAttackAnimation(0, hTempPlayAreaLocation_ff9d, 0x00, hWhoseTurn);
	WaitAttackAnimation();
	wLoadedAttackAnimation = ATK_ANIM_HEALING_WIND_PLAY_AREA;

	u8 d = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	u8 e = PLAY_AREA_ARENA;
	do {
		hTempPlayAreaLocation_ff9d = e;
		u8 a = GetCardDamageAndMaxHP(e, NULL);
		if (a != 0) { /* skip if no damage */
			/* if less than 20 damage, cap recovery at 10 damage */
			u16 heal = 20;
			if (a < (u8)heal)
				heal = a;

			/* heal: add HP to this card */
			u8 *hp = &TurnDuelistVars()[(u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD_HP)];
			*hp = (u8)(*hp + (u8)heal);

			/* play heal animation */
			PlayAttackAnimation(heal, hTempPlayAreaLocation_ff9d, 0x01, hWhoseTurn);
			WaitAttackAnimation();
		}
		/* next_pkmn */
		e++;
	} while (--d);
	return false; /* (PKMN_POWER_TRIGGER: carry not read by the caller) */
}

bool DragoniteLv41Slam_AIEffect(void)
{
	TRACE_FN();
	SetExpectedAIDamage((30 * 2) / 2, 0, 60);
	return false;
}

bool DragoniteLv41Slam_MultiplierEffect(void)
{
	TRACE_FN();
	LoadTxRam3(30);
	u8 a = TossCoinATimes_BankB(2, DamageCheckIfHeadsXDamageText);
	u8 c = a;
	a = (u8)(a + a);
	a = (u8)(a + c);
	a = ATimes10(a);
	SetDefiniteDamage(a);
	return false;
}

/* unreferenced */
void CopyPlayAreaHPToBackup_Unreferenced(void)
{
	u8 c = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	u8 *hl = &TurnDuelistVars()[DUELVARS_ARENA_CARD_HP];
	u8 *de = wBackupPlayerAreaHP;
	do {
		*de++ = *hl++;
	} while (--c);
}

/* unreferenced */
void CopyPlayAreaHPFromBackup_Unreferenced(void)
{
	u8 c = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	u8 *hl = &TurnDuelistVars()[DUELVARS_ARENA_CARD_HP];
	u8 *de = wBackupPlayerAreaHP;
	do {
		*hl++ = *de++;
	} while (--c);
}

bool CatPunchEffect(void)
{
	TRACE_FN();
	SwapTurn();
	u8 b = PickRandomPlayAreaCard();
	wLoadedAttackAnimation = ATK_ANIM_CAT_PUNCH_PLAY_AREA;
	DealDamageToPlayAreaPokemon(b, 20);
	SwapTurn();
	return false; /* (AFTER_DAMAGE: carry not read by the caller) */
}

/* picks a random Pokemon in the Deck to morph.
 * needs to be a Basic Pokemon that isn't Ditto
 * returns carry if no Pokemon were found. */
static bool MorphEffect_PickRandomBasicPokemonFromDeck(void)
{
	u8 count = 0;
	if (CreateDeckCardList(&count))
		return true; /* empty deck */
	ShuffleCards(wDuelTempList, count);
	u8 *hl = wDuelTempList;
	for (;;) {
		u8 a = *hl++;
		hTempCardIndex_ff98 = a;
		if (a == 0xff)
			return true;
		LoadCardDataToBuffer2_FromDeckIndex(a);
		if (wLoadedCard2.Type >= TYPE_ENERGY)
			continue; /* skip non-Pokemon cards */
		if (wLoadedCard2.Stage != 0)
			continue; /* skip non-Basic cards */
		if (wLoadedCard2.ID == DITTO)
			continue; /* skip other Ditto cards */
		return false;
	}
}

bool MorphEffect(void)
{
	TRACE_FN();
	ExchangeRNG();
	if (MorphEffect_PickRandomBasicPokemonFromDeck()) {
		DrawWideTextBox_WaitForInput(AttackUnsuccessfulText);
		return false;
	}

	/* successful */
	if (GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STAGE) != 0) {
		/* if this is a stage 1 Pokemon (in case it's used
		 * by Clefable's Metronome attack) then first discard
		 * the lower stage card. */
		hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
		u8 d = 0, e = 0;
		GetCardOneStageBelow(&d, &e);
		PutCardInDiscardPile(d);
		TurnDuelistVars()[DUELVARS_ARENA_CARD_STAGE] = BASIC;
	}

	/* skip_discard_stage_below: overwrite card ID */
	u8 id = GetCardIDFromDeckIndex(hTempCardIndex_ff98);
	u8 a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD);
	hTempCardIndex_ff98 = a;
	/* _GetCardIDFromDeckIndex leaves hl at the card's entry in wPlayerDeck/wOpponentDeck */
	_GetCardIDFromDeckIndex(a);
	{
		const u8 *deck = (hWhoseTurn == PLAYER_TURN) ? wPlayerDeck : wOpponentDeck;
		gWRAM[(u16)(gbaddr(deck) + a) - WRAM_START] = id;
	}

	/* overwrite HP to new card's maximum HP */
	u8 c = 0;
	GetCardDamageAndMaxHP(PLAY_AREA_ARENA, &c);
	u8 *vars = TurnDuelistVars();
	vars[DUELVARS_ARENA_CARD_HP] = c;

	/* clear changed color and status */
	vars[DUELVARS_ARENA_CARD_CHANGED_TYPE] = 0x00;
	ClearAllStatusConditions();

	/* load both card's names for printing text */
	LoadCardDataToBuffer2_FromCardID(wTempTurnDuelistCardID);
	wTxRam2[0] = wLoadedCard2.Name[0];
	wTxRam2[1] = wLoadedCard2.Name[1];
	LoadCardDataToBuffer2_FromDeckIndex(hTempCardIndex_ff98);
	wTxRam2_b[0] = wLoadedCard2.Name[0];
	wTxRam2_b[1] = wLoadedCard2.Name[1];
	DrawWideTextBox_WaitForInput(MetamorphsToText);

	wDuelDisplayedScreen = 0;
	return false;
}

/* returns in a and [hTempCardIndex_ff98] the deck index
 * of random Basic Pokemon card in deck.
 * if none are found, return carry. */
static bool PickRandomBasicCardFromDeck(void)
{
	u8 count = 0;
	if (CreateDeckCardList(&count))
		return true; /* return if empty deck */
	ShuffleCards(wDuelTempList, count);
	u8 *hl = wDuelTempList;
	for (;;) {
		u8 a = *hl++;
		hTempCardIndex_ff98 = a;
		if (a == 0xff)
			return true;
		LoadCardDataToBuffer2_FromDeckIndex(a);
		if (wLoadedCard2.Type >= TYPE_ENERGY)
			continue; /* skip if not Pokemon card */
		if (wLoadedCard2.Stage != 0)
			continue; /* skip if not Basic stage */
		return false;
	}
}

bool SlicingWindEffect(void)
{
	TRACE_FN();
	SwapTurn();
	u8 b = PickRandomPlayAreaCard();
	DealDamageToPlayAreaPokemon_RegularAnim(b, 30);
	SwapTurn();
	return false; /* (AFTER_DAMAGE: carry not read by the caller) */
}

bool Gale_LoadAnimation(void)
{
	TRACE_FN();
	wLoadedAttackAnimation = ATK_ANIM_GALE;
	return false;
}

static bool Gale_SwitchEffect_SwitchWithRandomBenchPokemon(void)
{
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	if (a < 2)
		return true; /* return if no Bench Pokemon */

	/* get random Bench location and swap */
	a = Random((u8)(a - 1));
	a++;
	SwapArenaWithBenchPokemon(a);

	wDuelDisplayedScreen = 0;
	return false;
}

bool Gale_SwitchEffect(void)
{
	TRACE_FN();
	/* if Defending card is unaffected by attack
	 * jump directly to switching this card only. */
	if (!HandleNoDamageOrEffect()) {
		/* handle switching Defending card */
		if (GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_HP) == 0)
			HandleDestinyBondSubstatus();
		/* skip_destiny_bond */
		SwapTurn();
		if (!Gale_SwitchEffect_SwitchWithRandomBenchPokemon()) {
			/* clear dealt damage because Pokemon was switched */
			wDealtDamage[0] = 0;
			wDealtDamage[1] = 0;
		}
		/* skip_clear_damage */
		SwapTurn();
	}
	/* fallthrough for attacking card switch */
	return Gale_SwitchEffect_SwitchWithRandomBenchPokemon();
}

/* return carry if Bench is full */
bool FriendshipSong_BenchCheck(void)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	gTextID = NoSpaceOnTheBenchText;
	return a >= MAX_PLAY_AREA_POKEMON;
}

bool FriendshipSong_AddToBench50PercentEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(SuccessCheckIfHeadsAttackIsSuccessfulText)) {
		/* none_came_text */
		DrawWideTextBox_WaitForInput(NoneCameText);
		return false;
	}

	/* successful */
	if (PickRandomBasicCardFromDeck()) {
		PlayAttackAnimationOverAttackingPokemon(ATK_ANIM_FRIENDSHIP_SONG);
		DrawWideTextBox_WaitForInput(NoneCameText); /* call .none_came_text */
		ShuffleCardsInDeck();
		return false;
	}

	/* put_in_bench (a = deck index from PickRandomBasicCardFromDeck, kept by these calls) */
	u8 a = hTempCardIndex_ff98;
	SearchCardInDeckAndAddToHand(a);
	AddCardToHand(a);
	PutHandPokemonCardInPlayArea(a, NULL);
	PlayAttackAnimationOverAttackingPokemon(ATK_ANIM_FRIENDSHIP_SONG);
	DisplayCardDetailScreen(hTempCardIndex_ff98, CameToTheBenchText);
	ShuffleCardsInDeck();
	return false;
}

bool ExpandEffect(void)
{
	TRACE_FN();
	ApplySubstatus1ToAttackingCard(SUBSTATUS1_REDUCE_BY_10);
	return false;
}

/* returns carry if either there are no damage counters
 * or no Energy cards attached in the Play Area. */
bool SuperPotion_DamageEnergyCheck(void)
{
	TRACE_FN();
	bool carry = CheckIfPlayAreaHasAnyDamage();
	gTextID = NoPokemonWithDamageCountersText;
	if (carry)
		return true; /* no damage counters */
	carry = CheckIfThereAreAnyEnergyCardsAttached();
	gTextID = ThereIsNoEnergyCardAttachedText;
	return carry;
}

bool SuperPotion_PlayerSelectEffect(void)
{
	TRACE_FN();
	DrawWideTextBox_WaitForInput(ChoosePokemonToRemoveDamageCounterFromText);
start:
	HasAlivePokemonInPlayArea(NULL);
read_input:
	if (OpenPlayAreaScreenForSelection())
		return true; /* exit if B is pressed */
	if (GetCardDamageAndMaxHP(hTempPlayAreaLocation_ff9d, NULL) == 0)
		goto read_input; /* Pokemon has no damage? */
	GetPlayAreaCardAttachedEnergies(hCurMenuItem);
	if (wTotalAttachedEnergies == 0) {
		/* no energy cards attached */
		DrawWideTextBox_WaitForInput(NoEnergyCardsText);
		goto start;
	}

	/* got_pkmn:
	 * Pokemon has damage and Energy cards attached,
	 * prompt the Player for Energy selection to discard. */
	CreateArenaOrBenchEnergyCardList(hCurMenuItem);
	DisplayEnergyDiscardScreen(hCurMenuItem);
	if (HandleEnergyDiscardMenuInput())
		return true; /* exit if B was pressed */

	hTemp_ffa0 = hTempCardIndex_ff98;
	u8 e = hTempPlayAreaLocation_ff9d;
	hTempPlayAreaLocation_ffa1 = e;

	/* cap the healing damage if
	 * it would make it exceed max HP. */
	u8 a = GetCardDamageAndMaxHP(e, NULL);
	u8 c = 40;
	if (a < 40)
		c = a;
	/* heal */
	hPlayAreaEffectTarget = c;
	return false;
}

bool SuperPotion_HealEffect(void)
{
	TRACE_FN();
	PutCardInDiscardPile(hTemp_ffa0);
	hTempPlayAreaLocation_ff9d = hTempPlayAreaLocation_ffa1;
	return HealPlayAreaCardHP(hPlayAreaEffectTarget);
}

/* checks if there is at least one Energy card
 * attached to some card in the Turn Duelist's Play Area.
 * return no carry if one is found,
 * and returns carry set if none is found. */
static bool CheckIfThereAreAnyEnergyCardsAttached(void)
{
	u8 *vars = TurnDuelistVars();
	for (u8 l = DUELVARS_CARD_LOCATIONS; l < DECK_SIZE; l++) {
		if (!(vars[l] & CARD_LOCATION_PLAY_AREA))
			continue; /* skip if not in Play Area */
		LoadCardDataToBuffer2_FromDeckIndex(l);
		u8 a = wLoadedCard2.Type;
		if (a == TYPE_TRAINER)
			continue; /* skip if it's a Trainer card */
		if (a >= TYPE_ENERGY)
			return false; /* found */
	}
	return true;
}

/* handles Player selection for Pokemon in Play Area,
 * then opens screen to choose one of the energy cards
 * attached to that selected Pokemon.
 * outputs the selection in:
 *	[hTemp_ffa0] = play area location
 *	[hTempPlayAreaLocation_ffa1] = index of energy card */
static bool HandlePokemonAndEnergySelectionScreen(void)
{
	for (;;) { /* start */
		HasAlivePokemonInPlayArea(NULL);
		if (OpenPlayAreaScreenForSelection())
			return true; /* exit if B is pressed */
		GetPlayAreaCardAttachedEnergies(hTempPlayAreaLocation_ff9d);
		if (wTotalAttachedEnergies != 0)
			break;
		DrawWideTextBox_WaitForInput(NoEnergyCardsText);
		/* loop back to start */
	}

	/* has_energy */
	CreateArenaOrBenchEnergyCardList(hCurMenuItem);
	DisplayEnergyDiscardScreen(hCurMenuItem);
	bool carry = HandleEnergyDiscardMenuInput();
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	hTempPlayAreaLocation_ffa1 = hTempCardIndex_ff98;
	return carry;
}

bool ImakuniEffect(void)
{
	TRACE_FN();
	LoadCardDataToBuffer1_FromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	u8 a = wLoadedCard1.ID;

	/* cannot confuse Clefairy Doll and Mysterious Fossil */
	if (a == CLEFAIRY_DOLL)
		goto failed;
	if (a == MYSTERIOUS_FOSSIL)
		goto failed;

	/* cannot confuse Snorlax if its Pkmn Power is active */
	if (a != SNORLAX)
		goto success;
	if (CheckIsIncapableOfUsingPkmnPower(PLAY_AREA_ARENA))
		goto success;
	/* fallthrough if Thick Skinned is active */

failed:
	/* play confusion animation and print failure text */
	PlayTrainerEffectAnimation(ATK_ANIM_OWN_CONFUSION);
	DrawWideTextBox_WaitForInput(ThereWasNoEffectText);
	return false;

success:
	/* play confusion animation and confuse card */
	PlayTrainerEffectAnimation(ATK_ANIM_OWN_CONFUSION);
	{
		u8 *status = &TurnDuelistVars()[DUELVARS_ARENA_CARD_STATUS];
		*status = (u8)((*status & PSN_DBLPSN) | CONFUSED);
	}
	DrawDuelHUDs();
	return false;
}

/* returns carry if opponent has no energy cards attached */
bool EnergyRemoval_EnergyCheck(void)
{
	TRACE_FN();
	SwapTurn();
	bool carry = CheckIfThereAreAnyEnergyCardsAttached();
	gTextID = NoEnergyAttachedToOpponentsActiveText;
	SwapTurn();
	return carry;
}

bool EnergyRemoval_PlayerSelection(void)
{
	TRACE_FN();
	DrawWideTextBox_WaitForInput(ChoosePokemonToRemoveEnergyFromText);
	SwapTurn();
	bool carry = HandlePokemonAndEnergySelectionScreen();
	SwapTurn();
	return carry;
}

bool EnergyRemoval_AISelection(void)
{
	TRACE_FN();
	/* the original returns the deck index in a (unused: AI_SELECTION results are not
	 * read by its caller) and carry set only in the $ff (no energy) case */
	return AIPickEnergyCardToDiscardFromDefendingPokemon() == 0xff;
}

bool EnergyRemoval_DiscardEffect(void)
{
	TRACE_FN();
	SwapTurn();
	PutCardInDiscardPile(hTempPlayAreaLocation_ffa1);
	SwapTurn();
	if (IsPlayerTurn())
		return true;

	/* show Player which Pokemon was affected */
	SwapTurn();
	DrawPlayAreaScreenToShowChanges(hTemp_ffa0);
	SwapTurn();
	/* (the original returns the carry of DrawPlayAreaScreenToShowChanges, B pressed in
	 * its WaitForButtonAorB; not exposed by its signature, and BEFORE_DAMAGE carry is
	 * not read by the caller) */
	return false;
}

/* return carry if no other card in hand to discard
 * or if there are no Basic Energy cards in Discard Pile. */
bool EnergyRetrieval_HandEnergyCheck(void)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_IN_HAND);
	gTextID = NotEnoughCardsInHandText;
	if (a < 2)
		return true; /* return if doesn't have another card to discard */
	bool carry = CreateEnergyCardListFromDiscardPile_OnlyBasic();
	gTextID = ThereAreNoBasicEnergyCardsInDiscardPileText;
	return carry;
}

bool EnergyRetrieval_PlayerHandSelection(void)
{
	TRACE_FN();
	u8 card;
	DrawWideTextBox_WaitForInput(ChooseCardToDiscardFromHandText);
	CreateHandCardList(NULL);
	RemoveCardFromDuelTempList(hTempCardIndex_ff9f);
	InitAndDrawCardListScreenLayout_WithSelectCheckMenu();
	bool carry = DisplayCardList(&card);
	hTempList[0] = hTempCardIndex_ff98;
	return carry;
}

bool EnergyRetrieval_PlayerDiscardPileSelection(void)
{
	TRACE_FN();
	u8 card;
	hCurSelectionItem = 1; /* start at 1 due to card selected from hand */
	DrawWideTextBox_WaitForInput(Choose2BasicEnergyCardsFromDiscardPileText);
	CreateEnergyCardListFromDiscardPile_OnlyBasic();

select_card:
	InitAndDrawCardListScreenLayout();
	SetCardListHeaderText(PleaseSelectCardText, PlayerDiscardPileText);
	if (DisplayCardList(&card)) {
		/* B was pressed */
		if (AskWhetherToQuitSelectingCards(2 + 1)) /* includes the card selected from hand */
			goto select_card; /* player selected No */
		goto done;
	}

	/* selected */
	*GetNextPositionInTempList_TrainerEffects() = hTempCardIndex_ff98;
	if (RemoveCardFromDuelTempList(hTempCardIndex_ff98))
		goto done;
	if (hCurSelectionItem < 2 + 1) /* includes the card selected from hand */
		goto select_card;

done:
	*GetNextPositionInTempList_TrainerEffects() = 0xff; /* terminating byte */
	return false;
}

bool EnergyRetrieval_DiscardAndAddToHandEffect(void)
{
	TRACE_FN();
	u8 *hl = hTempList;
	u8 a = *hl++;
	RemoveCardFromHand(a);
	PutCardInDiscardPile(a);
	u8 *de = wDuelTempList;
	for (;;) {
		a = *hl++;
		*de++ = a;
		if (a == 0xff)
			break;
		AddCardToHand(MoveDiscardPileCardToHand(a));
	}
	/* done */
	if (IsPlayerTurn())
		return true;
	DisplayCardListDetails();
	return false;
}

/* return carry if no cards left in Deck. */
bool EnergySearch_DeckCheck(void)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK);
	gTextID = NoCardsLeftInTheDeckText;
	return a >= DECK_SIZE;
}

bool EnergySearch_PlayerSelection(void)
{
	TRACE_FN();
	u8 card;
	hTemp_ffa0 = 0xff;
	CreateDeckCardList(NULL);
	if (LookForCardsInDeck_HL_BC(SEARCHEFFECT_BASIC_ENERGY, 0,
	                             Choose1BasicEnergyCardFromDeckText, BasicEnergyText))
		return true; /* skip showing deck */

	InitAndDrawCardListScreenLayout_WithSelectCheckMenu();
	SetCardListHeaderText(ChooseBasicEnergyCardText, DuelistDeckText);
read_input:
	if (DisplayCardList(&card))
		goto try_exit; /* B pressed? */
	hTemp_ffa0 = hTempCardIndex_ff98;
	if (CheckIfCardIsBasicEnergy(hTempCardIndex_ff98))
		goto play_sfx;
	return false;
play_sfx:
	PlaySFX_InvalidChoice();
	goto read_input;

try_exit:
	/* check if Player can exit without selecting anything */
	{
		u8 *hl = wDuelTempList;
		for (;;) { /* next_card */
			u8 a = *hl++;
			if (a == 0xff)
				break;
			if (CheckIfCardIsBasicEnergy(a))
				continue;
			goto read_input; /* no, has to select Energy card */
		}
	}
	/* exit */
	hTemp_ffa0 = 0xff;
	return false;
}

bool EnergySearch_AddToHandEffect(void)
{
	TRACE_FN();
	if (hTemp_ffa0 == 0xff)
		goto done;
	/* add to hand */
	SearchCardInDeckAndAddToHand(hTemp_ffa0);
	AddCardToHand(hTemp_ffa0);
	if (IsPlayerTurn())
		goto done; /* done if Player played card */
	/* display card in screen */
	DisplayCardDetailScreen(hTemp_ffa0, WasPlacedInTheHandText);
done:
	ShuffleCardsInDeck();
	return false;
}

/* check if card index in a is a Basic Energy card.
 * returns carry in case it's not. */
static bool CheckIfCardIsBasicEnergy(u8 card)
{
	LoadCardDataToBuffer2_FromDeckIndex(card);
	u8 a = wLoadedCard2.Type;
	if (a < TYPE_ENERGY)
		return true; /* not_basic_energy */
	if (a >= TYPE_ENERGY_DOUBLE_COLORLESS)
		return true; /* not_basic_energy */
	/* is basic energy */
	return false;
}

bool ProfessorOakEffect(void)
{
	TRACE_FN();
	/* discard hand */
	CreateHandCardList(NULL);
	SortCardsInDuelTempListByID();
	u8 *hl = wDuelTempList;
	for (;;) { /* discard_loop */
		u8 a = *hl++;
		if (a == 0xff)
			break;
		RemoveCardFromHand(a);
		PutCardInDiscardPile(a);
	}

	/* draw_cards */
	DisplayDrawNCardsScreen(7);
	u8 c = 7;
	do {
		u8 card;
		if (DrawCardFromDeck(&card))
			return true; /* done */
		AddCardToHand(card);
	} while (--c);
	return false;
}

bool Potion_DamageCheck(void)
{
	TRACE_FN();
	bool carry = CheckIfPlayAreaHasAnyDamage();
	gTextID = NoPokemonWithDamageCountersText;
	return carry;
}

bool Potion_PlayerSelection(void)
{
	TRACE_FN();
	u8 a;
	HasAlivePokemonInPlayArea(NULL);
	do { /* read_input */
		if (OpenPlayAreaScreenForSelection())
			return true; /* exit is B was pressed */
		hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
		a = GetCardDamageAndMaxHP(hTempPlayAreaLocation_ff9d, NULL);
	} while (a == 0); /* no damage, loop back to start */
	/* cap damage */
	u8 c = 20;
	if (a < 20)
		c = a;
	/* skip_cap */
	hTempPlayAreaLocation_ffa1 = c;
	return false;
}

bool Potion_HealEffect(void)
{
	TRACE_FN();
	hTempPlayAreaLocation_ff9d = hTemp_ffa0;
	return HealPlayAreaCardHP(hTempPlayAreaLocation_ffa1);
}

bool GamblerEffect(void)
{
	TRACE_FN();
	/* TossCoin returns the number of heads (0 or 1) in a */
	hTemp_ffa0 = TossCoin_BankB(CardCheckIfHeads8CardsIfTails1CardText) ? 1 : 0;
	/* discard Gambler card from hand */
	RemoveCardFromHand(hTempCardIndex_ff9f);
	PutCardInDiscardPile(hTempCardIndex_ff9f);

	/* shuffle cards into deck */
	CreateHandCardList(NULL);
	SortCardsInDuelTempListByID();
	u8 *hl = wDuelTempList;
	for (;;) { /* loop_return_deck */
		u8 a = *hl++;
		if (a == 0xff)
			break;
		RemoveCardFromHand(a);
		ReturnCardToDeck(a);
	}

	/* check_coin_toss */
	ShuffleCardsInDeck();
	u8 c = 8;
	if (hTemp_ffa0 == 0) {
		/* if tails, number of cards to draw is 1 */
		c = 1;
	}

	/* correct number of cards to draw is in c */
	DisplayDrawNCardsScreen(c);
	do {
		u8 card;
		if (DrawCardFromDeck(&card))
			return true; /* done */
		AddCardToHand(card);
	} while (--c);
	return false;
}

/* return carry if not enough cards in hand to discard
 * or if there are no cards in the Discard Pile */
bool ItemFinder_HandDiscardPileCheck(void)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_IN_HAND);
	gTextID = NotEnoughCardsInHandText;
	if (a < 3)
		return true;
	return CreateTrainerCardListFromDiscardPile();
}

bool ItemFinder_PlayerSelection(void)
{
	TRACE_FN();
	if (HandlePlayerSelection2HandCardsToDiscard())
		return true; /* was operation cancelled? */

	/* cards were selected to discard from hand.
	 * now to choose a Trainer card from Discard Pile. */
	CreateTrainerCardListFromDiscardPile();
	InitAndDrawCardListScreenLayout_WithSelectCheckMenu();
	SetCardListHeaderText(ChooseCardToPlaceInHandText, PlayerDiscardPileText);
	u8 a;
	bool carry = DisplayCardList_RegA(&a);
	hTempList[2] = a; /* placed after the 2 cards selected to discard */
	return carry;
}

bool ItemFinder_DiscardAddToHandEffect(void)
{
	TRACE_FN();
	/* discard cards from hand */
	u8 *hl = hTempList;
	u8 a = *hl++;
	RemoveCardFromHand(a);
	PutCardInDiscardPile(a);
	a = *hl++;
	RemoveCardFromHand(a);
	PutCardInDiscardPile(a);

	/* place card from Discard Pile to hand */
	a = *hl;
	AddCardToHand(MoveDiscardPileCardToHand(a));
	if (IsPlayerTurn())
		return true;
	/* display card in screen */
	DisplayCardDetailScreen(hTempList[2], WasPlacedInTheHandText);
	return false;
}

bool Defender_PlayerSelection(void)
{
	TRACE_FN();
	DrawWideTextBox_WaitForInput(ChoosePokemonToAttachDefenderToText);
	HasAlivePokemonInPlayArea(NULL);
	bool carry = OpenPlayAreaScreenForSelection();
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	return carry;
}

bool Defender_AttachDefenderEffect(void)
{
	TRACE_FN();
	/* attach Trainer card to Play Area Pokemon */
	PutHandCardInPlayArea(hTempCardIndex_ff9f, hTemp_ffa0);

	/* increase number of Defender cards of this location by 1 */
	TurnDuelistVars()[(u8)(hTemp_ffa0 + DUELVARS_ARENA_CARD_ATTACHED_DEFENDER)]++;
	if (IsPlayerTurn())
		return true;

	DrawPlayAreaScreenToShowChanges(hTemp_ffa0);
	return false; /* (carry of DrawPlayAreaScreenToShowChanges: see EnergyRemoval_DiscardEffect) */
}

/* return carry if Bench is full. */
bool MysteriousFossil_BenchCheck(void)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	bool carry = a >= MAX_PLAY_AREA_POKEMON;
	gTextID = NoSpaceOnTheBenchText;
	return carry;
}

bool MysteriousFossil_PlaceInPlayAreaEffect(void)
{
	TRACE_FN();
	return PutHandPokemonCardInPlayArea(hTempCardIndex_ff9f, NULL);
}

/* return carry if Arena card has no status to heal. */
bool FullHeal_StatusCheck(void)
{
	TRACE_FN();
	if (GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS) != 0)
		return false;
	gTextID = NotAffectedByPoisonSleepParalysisOrConfusionText;
	return true;
}

bool FullHeal_ClearStatusEffect(void)
{
	TRACE_FN();
	PlayTrainerEffectAnimation(ATK_ANIM_FULL_HEAL);
	TurnDuelistVars()[DUELVARS_ARENA_CARD_STATUS] = NO_STATUS;
	DrawDuelHUDs();
	return false;
}

bool ImposterProfessorOakEffect(void)
{
	TRACE_FN();
	bool carry = false;
	SwapTurn();
	CreateHandCardList(NULL);
	SortCardsInDuelTempListByID();

	/* first return all cards in hand to the deck. */
	u8 *hl = wDuelTempList;
	for (;;) { /* loop_return_deck */
		u8 a = *hl++;
		if (a == 0xff)
			break;
		RemoveCardFromHand(a);
		ReturnCardToDeck(a);
	}

	/* then draw 7 cards from the deck. */
	ShuffleCardsInDeck();
	DisplayDrawNCardsScreen(7);
	u8 c = 7;
	do {
		u8 card;
		if (DrawCardFromDeck(&card)) {
			carry = true;
			break; /* done */
		}
		AddCardToHand(card);
	} while (--c);
	/* done */
	SwapTurn();
	return carry;
}

/* return carry if not enough cards in hand to discard
 * or if there are no cards left in the deck. */
bool ComputerSearch_HandDeckCheck(void)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_IN_HAND);
	gTextID = NotEnoughCardsInHandText;
	if (a < 3)
		return true;
	a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK);
	gTextID = NoCardsLeftInTheDeckText;
	return a >= DECK_SIZE;
}

bool ComputerSearch_PlayerDiscardHandSelection(void)
{
	TRACE_FN();
	return HandlePlayerSelection2HandCardsToDiscard();
}

bool ComputerSearch_PlayerDeckSelection(void)
{
	TRACE_FN();
	u8 a;
	CreateDeckCardList(NULL);
	InitAndDrawCardListScreenLayout_WithSelectCheckMenu();
	SetCardListHeaderText(ChooseCardToPlaceInHandText, DuelistDeckText);
	while (DisplayCardList_RegA(&a))
		; /* can't exit with B button */
	hTempList[2] = a;
	return false;
}

bool ComputerSearch_DiscardAddToHandEffect(void)
{
	TRACE_FN();
	/* discard cards from hand */
	u8 *hl = hTempList;
	u8 a = *hl++;
	RemoveCardFromHand(a);
	PutCardInDiscardPile(a);
	a = *hl++;
	RemoveCardFromHand(a);
	PutCardInDiscardPile(a);

	/* add card from deck to hand */
	a = *hl;
	SearchCardInDeckAndAddToHand(a);
	AddCardToHand(a);
	ShuffleCardsInDeck();
	return false;
}

/* return carry if Bench is full. */
bool ClefairyDoll_BenchCheck(void)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	gTextID = NoSpaceOnTheBenchText;
	return a >= MAX_PLAY_AREA_POKEMON;
}

bool ClefairyDoll_PlaceInPlayAreaEffect(void)
{
	TRACE_FN();
	return PutHandPokemonCardInPlayArea(hTempCardIndex_ff9f, NULL);
}

/* return carry if no Pokemon in the Bench. */
bool MrFuji_BenchCheck(void)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	gTextID = EffectNoPokemonOnTheBenchText;
	return a < 2;
}

bool MrFuji_PlayerSelection(void)
{
	TRACE_FN();
	DrawWideTextBox_WaitForInput(ChoosePokemonToReturnToTheDeckText);
	HasAlivePokemonInBench(NULL);
	bool carry = OpenPlayAreaScreenForSelection();
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	return carry;
}

bool MrFuji_ReturnToDeckEffect(void)
{
	TRACE_FN();
	/* get Play Area location's card index */
	hTempCardIndex_ff98 = GetTurnDuelistVariable((u8)(hTemp_ffa0 + DUELVARS_ARENA_CARD));

	/* find all cards that are in the same location
	 * (previous evolutions and energy cards attached)
	 * and return them all to the deck. */
	u8 e = hTemp_ffa0 | CARD_LOCATION_PLAY_AREA;
	u8 *vars = TurnDuelistVars();
	for (u8 l = DUELVARS_CARD_LOCATIONS; l < DECK_SIZE; l++) {
		if (vars[l] != e)
			continue;
		ReturnCardToDeck(l);
	}

	/* clear Play Area location of card */
	EmptyPlayAreaSlot(hTemp_ffa0);
	TurnDuelistVars()[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]--;
	ShiftAllPokemonToFirstPlayAreaSlots();

	/* if Trainer card wasn't played by the Player,
	 * print the selected Pokemon's name and show card on screen. */
	if (!IsPlayerTurn()) {
		LoadCardDataToBuffer1_FromDeckIndex(hTempCardIndex_ff98);
		LoadTxRam2(rd16(wLoadedCard1.Name));
		DrawLargePictureOfCard();
		DrawWideTextBox_WaitForInput(PokemonAndAllAttachedCardsWereReturnedToDeckText);
	}
	/* done */
	ShuffleCardsInDeck();
	return false;
}

bool PlusPowerEffect(void)
{
	TRACE_FN();
	/* attach Trainer card to Arena Pokemon */
	PutHandCardInPlayArea(hTempCardIndex_ff9f, PLAY_AREA_ARENA);

	/* increase number of Defender cards of this location by 1 */
	TurnDuelistVars()[DUELVARS_ARENA_CARD_ATTACHED_PLUSPOWER]++;
	return false;
}

/* return carry if no Pokemon in the Bench. */
bool Switch_BenchCheck(void)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	gTextID = EffectNoPokemonOnTheBenchText;
	return a < 2;
}

bool Switch_PlayerSelection(void)
{
	TRACE_FN();
	DrawWideTextBox_WaitForInput(SelectPkmnOnBenchToSwitchWithActiveText);
	HasAlivePokemonInBench(NULL);
	bool carry = OpenPlayAreaScreenForSelection();
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	return carry;
}

bool Switch_SwitchEffect(void)
{
	TRACE_FN();
	SwapArenaWithBenchPokemon(hTemp_ffa0);
	return false;
}

bool PokemonCenter_DamageCheck(void)
{
	TRACE_FN();
	bool carry = CheckIfPlayAreaHasAnyDamage();
	gTextID = NoPokemonWithDamageCountersText;
	return carry;
}

bool PokemonCenter_HealDiscardEnergyEffect(void)
{
	TRACE_FN();
	u8 d = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	u8 e = PLAY_AREA_ARENA;

	/* go through every Pokemon in the Play Area
	 * and heal all damage & discard their Energy cards. */
	do { /* loop_play_area */
		/* check its damage */
		hTempPlayAreaLocation_ff9d = e;
		u8 a = GetCardDamageAndMaxHP(e, NULL);
		if (a == 0)
			goto next_pkmn; /* if no damage, skip Pokemon */

		/* heal all its damage */
		HealPlayAreaCardHP(a);

		/* loop all cards in deck and for all the Energy cards
		 * that are attached to this Play Area location Pokemon,
		 * place them in the Discard Pile. */
		{
			u8 loc = hTempPlayAreaLocation_ff9d | CARD_LOCATION_PLAY_AREA;
			u8 *vars = TurnDuelistVars();
			for (u8 l = DUELVARS_CARD_LOCATIONS; l < DECK_SIZE; l++) {
				if (vars[l] != loc)
					continue; /* not attached to card, skip */
				LoadCardDataToBuffer2_FromDeckIndex(l);
				if (!(wLoadedCard2.Type & TYPE_ENERGY))
					continue; /* not Energy, skip */
				PutCardInDiscardPile(l);
			}
		}
next_pkmn:
		e++;
	} while (--d);
	return false;
}

/* return carry if non-Turn Duelist has full Bench
 * or if they have no Basic Pokemon cards in Discard Pile. */
bool PokemonFlute_BenchCheck(void)
{
	TRACE_FN();
	u8 a = GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	gTextID = NoSpaceOnTheBenchText;
	if (a >= MAX_PLAY_AREA_POKEMON)
		return true; /* not enough space in Bench */
	/* check Discard Pile */
	SwapTurn();
	bool carry = CreateBasicPokemonCardListFromDiscardPile();
	gTextID = ThereAreNoPokemonInDiscardPileText;
	SwapTurn();
	return carry;
}

bool PokemonFlute_PlayerSelection(void)
{
	TRACE_FN();
	u8 card;
	/* create Discard Pile list */
	SwapTurn();
	CreateBasicPokemonCardListFromDiscardPile();

	/* display selection screen and store Player's selection */
	InitAndDrawCardListScreenLayout_WithSelectCheckMenu();
	SetCardListHeaderText(ChoosePokemonToPlaceInPlayText, PlayerDiscardPileText);
	bool carry = DisplayCardList(&card);
	SwapTurn();
	hTemp_ffa0 = hTempCardIndex_ff98;
	return carry;
}

bool PokemonFlute_PlaceInPlayAreaText(void)
{
	TRACE_FN();
	/* place selected card in non-Turn Duelist's Bench */
	SwapTurn();
	u8 a = MoveDiscardPileCardToHand(hTemp_ffa0);
	AddCardToHand(a);
	PutHandPokemonCardInPlayArea(a, NULL);
	SwapTurn();

	/* unless it was the Player who played the card,
	 * display the Pokemon card on screen. */
	if (IsPlayerTurn())
		return true;
	SwapTurn();
	DisplayCardDetailScreen(hTemp_ffa0, CardWasChosenText);
	SwapTurn();
	return false;
}

bool PokemonBreeder_HandPlayAreaCheck(void)
{
	TRACE_FN();
	if (CreatePlayableStage2PokemonCardListFromHand()) {
		/* cannot_evolve */
		gTextID = ConditionsForEvolvingToStage2NotFulfilledText;
		return true;
	}
	return IsPrehistoricPowerActive();
}

bool PokemonBreeder_PlayerSelection(void)
{
	TRACE_FN();
	u8 card;
	/* create hand list of playable Stage2 cards */
	CreatePlayableStage2PokemonCardListFromHand();
	InitAndDrawCardListScreenLayout_WithSelectCheckMenu();

	/* handle Player selection of Stage2 card */
	SetCardListHeaderText(PleaseSelectCardText, DuelistHandText);
	if (DisplayCardList(&card))
		return true; /* exit if B was pressed */

	hTemp_ffa0 = hTempCardIndex_ff98;
	DrawWideTextBox_WaitForInput(ChooseBasicPokemonToEvolveText);

	/* handle Player selection of Basic card to evolve */
	HasAlivePokemonInPlayArea(NULL);
	do { /* read_input */
		if (OpenPlayAreaScreenForSelection())
			return true; /* exit if B was pressed */
		hTempPlayAreaLocation_ffa1 = hTempPlayAreaLocation_ff9d;
	} while (CheckIfCanEvolveInto_BasicToStage2(hTemp_ffa0, hTempPlayAreaLocation_ff9d));
	/* (loop back if cannot evolve this card) */
	return false;
}

bool PokemonBreeder_EvolveEffect(void)
{
	TRACE_FN();
	u8 saved_ff9f = hTempCardIndex_ff9f;
	hTempCardIndex_ff98 = hTemp_ffa0;
	hTempPlayAreaLocation_ff9d = hTempPlayAreaLocation_ffa1;

	/* load the Basic Pokemon card name to RAM */
	LoadCardDataToBuffer1_FromDeckIndex(
		GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)));
	LoadTxRam2(rd16(wLoadedCard1.Name));

	/* evolve card and overwrite its stage as STAGE2_WITHOUT_STAGE1
	 * (EvolvePokemonCard returns hl at the stage duelvar of hTempPlayAreaLocation_ff9d) */
	EvolvePokemonCard();
	TurnDuelistVars()[(u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD_STAGE)] = STAGE2_WITHOUT_STAGE1;

	/* load Stage2 Pokemon card name to RAM */
	LoadCardDataToBuffer1_FromDeckIndex(hTempCardIndex_ff98);
	CopyCardNameAndLevel(18);
	/* ($0 character at the end of the name: see top of file) */
	wTxRam2_b[0] = 0;
	wTxRam2_b[1] = 0;

	/* display Pokemon picture and play sfx,
	 * print the corresponding card names. */
	DrawLargePictureOfCard();
	PlaySFX(SFX_POKEMON_EVOLUTION);
	DrawWideTextBox_WaitForInput(PokemonEvolvedIntoPokemonText);
	ProcessPlayedPokemonCard();
	hTempCardIndex_ff9f = saved_ff9f;
	return false; /* pop af restores the flags of the entry */
}

/* return carry if Stage2 card in a cannot evolve any
 * of the Basic Pokemon in Play Area through Pokemon Breeder. */
static bool CreatePlayableStage2PokemonCardListFromHand_CheckIfCanEvolveAnyPlayAreaBasicCard(u8 d)
{
	LoadCardDataToBuffer2_FromDeckIndex(d);
	if (wLoadedCard2.Type >= TYPE_ENERGY)
		return true; /* skip if not Pokemon card */
	if (wLoadedCard2.Stage != STAGE2)
		return true; /* skip if not Stage2 */

	/* check if can evolve any Play Area cards */
	u8 c = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	u8 e = PLAY_AREA_ARENA;
	do {
		if (!CheckIfCanEvolveInto_BasicToStage2(d, e))
			return false; /* done_play_area */
		e++;
	} while (--c);
	return true;
}

/* creates list in wDuelTempList of all Stage2 Pokemon cards
 * in the hand that can evolve a Basic Pokemon card in Play Area
 * through use of Pokemon Breeder.
 * returns carry if that list is empty. */
static bool CreatePlayableStage2PokemonCardListFromHand(void)
{
	if (CreateHandCardList(NULL))
		return true; /* return if no hand cards */

	/* check if hand Stage2 Pokemon cards can be made
	 * to evolve a Basic Pokemon in the Play Area and, if so,
	 * add it to the wDuelTempList. */
	u8 *hl = wDuelTempList;
	u8 *de = wDuelTempList;
	while (*hl != 0xff) { /* loop_hand */
		if (!CreatePlayableStage2PokemonCardListFromHand_CheckIfCanEvolveAnyPlayAreaBasicCard(*hl))
			*de++ = *hl;
		/* next_hand_card */
		hl++;
	}

	/* done */
	*de = 0xff; /* terminating byte */
	if (wDuelTempList[0] == 0xff)
		return true; /* return carry if empty */
	/* not empty */
	return false;
}

/* return carry if no cards in the Bench. */
bool ScoopUp_BenchCheck(void)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	gTextID = EffectNoPokemonOnTheBenchText;
	return a < 2;
}

bool ScoopUp_PlayerSelection(void)
{
	TRACE_FN();
	/* print text box */
	DrawWideTextBox_WaitForInput(ChoosePokemonToScoopUpText);

	/* handle Player selection */
	HasAlivePokemonInPlayArea(NULL);
	if (OpenPlayAreaScreenForSelection())
		return true; /* exit if B was pressed */

	u8 a = hTempPlayAreaLocation_ff9d;
	hTemp_ffa0 = a;
	if (a != 0)
		return false; /* if it wasn't the Active Pokemon, we are done */

	/* handle switching to a Pokemon in Bench and store location selected. */
	EmptyScreen();
	DrawWideTextBox_WaitForInput(SelectPokemonToPlaceInTheArenaText);
	HasAlivePokemonInBench(NULL);
	bool carry = OpenPlayAreaScreenForSelection();
	hTempPlayAreaLocation_ffa1 = hTempPlayAreaLocation_ff9d;
	return carry;
}

bool ScoopUp_ReturnToHandEffect(void)
{
	TRACE_FN();
	/* store chosen card location to Scoop Up */
	u8 e = hTemp_ffa0 | CARD_LOCATION_PLAY_AREA;

	/* find Basic Pokemon card that is in the selected Play Area location
	 * and add it to the hand, discarding all cards attached. */
	u8 *vars = TurnDuelistVars();
	for (u8 l = DUELVARS_CARD_LOCATIONS; l < DECK_SIZE; l++) {
		if (vars[l] != e)
			continue; /* skip if not in selected location */
		LoadCardDataToBuffer2_FromDeckIndex(l);
		if (wLoadedCard2.Type >= TYPE_ENERGY)
			continue; /* skip if not Pokemon card */
		if (wLoadedCard2.Stage != 0)
			continue; /* skip if not Basic stage */
		/* found */
		hTempCardIndex_ff98 = l;
		AddCardToHand(l);
		/* optimization: break loop here, since
		 * no two Basic Pokemon cards may occupy
		 * the same Play Area location. */
	}

	/* since the card has been moved to hand,
	 * MovePlayAreaCardToDiscardPile will take care
	 * of discarding every higher stage cards and other cards attached. */
	MovePlayAreaCardToDiscardPile(hTemp_ffa0);

	/* if the Pokemon was in the Arena, clear status */
	if (hTemp_ffa0 == 0)
		ClearAllStatusConditions();
	/* skip_clear_status */

	/* if card was not played by Player, show detail screen
	 * and print corresponding text. */
	if (!IsPlayerTurn()) {
		u16 hl = PokemonWasReturnedFromArenaToHandText;
		if (hTemp_ffa0 != 0)
			hl = PokemonWasReturnedFromBenchToHandText;
		/* display_detail_screen */
		DisplayCardDetailScreen(hTempCardIndex_ff98, hl);
	}

	/* shift_or_switch: if card was in Bench, simply shift Pokemon slots... */
	if (hTemp_ffa0 != 0) {
		ShiftAllPokemonToFirstPlayAreaSlots();
		return false;
	}

	/* switch: ...if Pokemon was in Arena, then switch it with the selected Bench card. */
	SwapPlayAreaPokemon(hTempPlayAreaLocation_ffa1, PLAY_AREA_ARENA);
	ShiftAllPokemonToFirstPlayAreaSlots();
	return false;
}

/* return carry if no other cards in hand,
 * or if there are no Pokemon cards in hand. */
bool PokemonTrader_HandDeckCheck(void)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_IN_HAND);
	gTextID = ThereAreNoCardsInHandThatYouCanChangeText;
	if (a < 2)
		return true; /* return if no other cards in hand */
	bool carry = CreatePokemonCardListFromHand();
	gTextID = ThereAreNoCardsInHandThatYouCanChangeText;
	return carry;
}

bool PokemonTrader_PlayerHandSelection(void)
{
	TRACE_FN();
	/* print text box */
	DrawWideTextBox_WaitForInput(ChooseCardFromYourHandToSwitchText);

	/* create list with all Pokemon cards in hand */
	CreatePokemonCardListFromHand();
	InitAndDrawCardListScreenLayout_WithSelectCheckMenu();

	/* handle Player selection */
	SetCardListHeaderText(ChooseCardToSwitchText, DuelistHandText);
	u8 a;
	bool carry = DisplayCardList_RegA(&a);
	hTemp_ffa0 = a;
	return carry;
}

bool PokemonTrader_PlayerDeckSelection(void)
{
	TRACE_FN();
	/* temporarily place chosen hand card in deck
	 * so it can be potentially chosen to be traded. */
	RemoveCardFromHand(hTemp_ffa0);
	ReturnCardToDeck(hTemp_ffa0);

	/* display deck card list screen */
	DrawWideTextBox_WaitForInput(ChooseBasicOrEvolutionPokemonCardFromDeckText);
	CreateDeckCardList(NULL);
	InitAndDrawCardListScreenLayout_WithSelectCheckMenu();
	SetCardListHeaderText(ChoosePokemonCardText, DuelistDeckText);

	/* handle Player selection */
	for (;;) { /* read_input */
		u8 a;
		if (DisplayCardList_RegA(&a))
			continue; /* pressing B loops back to selection */
		LoadCardDataToBuffer2_FromDeckIndex(a);
		if (wLoadedCard2.Type >= TYPE_ENERGY)
			continue; /* can't select non-Pokemon cards */
		break;
	}

	/* a valid card was selected, store its card index and
	 * place the selected hand card back to the hand. */
	hTempPlayAreaLocation_ffa1 = hTempCardIndex_ff98;
	SearchCardInDeckAndAddToHand(hTemp_ffa0);
	AddCardToHand(hTemp_ffa0);
	return false;
}

bool PokemonTrader_TradeCardsEffect(void)
{
	TRACE_FN();
	/* place hand card in deck */
	RemoveCardFromHand(hTemp_ffa0);
	ReturnCardToDeck(hTemp_ffa0);

	/* place deck card in hand */
	SearchCardInDeckAndAddToHand(hTempPlayAreaLocation_ffa1);
	AddCardToHand(hTempPlayAreaLocation_ffa1);

	/* display cards if the Pokemon Trader wasn't played by Player */
	if (!IsPlayerTurn()) {
		DisplayCardDetailScreen(hTemp_ffa0, PokemonWasReturnedToDeckText);
		DisplayCardDetailScreen(hTempPlayAreaLocation_ffa1, WasPlacedInTheHandText);
	}
	/* done */
	ShuffleCardsInDeck();
	return false;
}

/* makes list in wDuelTempList with all Pokemon cards
 * that are in Turn Duelist's hand.
 * if list turns out empty, return carry. */
static bool CreatePokemonCardListFromHand(void)
{
	u8 c = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_IN_HAND);
	u8 *vars = TurnDuelistVars();
	u8 l = DUELVARS_HAND;
	u8 *de = wDuelTempList;
	do { /* loop */
		LoadCardDataToBuffer2_FromDeckIndex(vars[l]);
		if (wLoadedCard2.Type < TYPE_ENERGY)
			*de++ = vars[l];
		/* next_hand_card */
		l++;
	} while (--c);
	*de = 0xff; /* terminating byte */
	if (wDuelTempList[0] == 0xff)
		return true;
	return false;
}

/* return carry if no cards in deck */
bool Pokedex_DeckCheck(void)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK);
	gTextID = NoCardsLeftInTheDeckText;
	return a >= DECK_SIZE;
}

bool Pokedex_PlayerSelection(void)
{
	TRACE_FN();
	u8 card, a, b, c;
	u8 *hl, *de;

	/* print text box */
	DrawWideTextBox_WaitForInput(RearrangeThe5CardsAtTopOfDeckText);

	/* cap the number of cards to reorder up to
	 * number of cards left in the deck (maximum of 5) */
	b = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK);
	a = (u8)(DECK_SIZE - b);
	c = 5;
	if (a < c)
		c = a;
	/* no_cap */

	/* fill wDuelTempList with cards that are going to be sorted */
	wNumberOfCardsToOrder = (u8)(c + 1);
	hl = &TurnDuelistVars()[(u8)(b + DUELVARS_DECK_CARDS)];
	de = wDuelTempList;
	do { /* loop_cards_to_order */
		*de++ = *hl++;
	} while (--c);
	*de = 0xff; /* terminating byte */

clear_list:
	/* wDuelTempList + 10 will be filled with numbers from 1
	 * to 5 (or whatever the maximum order card is).
	 * so that the first item in that list corresponds to the first card
	 * the second item corresponds to the second card, etc.
	 * and the number in the list corresponds to the ordering number. */
	b = CountCardsInDuelTempList();
	/* fill order list with zeroes */
	hCurSelectionItem = 1;
	hl = wDuelTempList + 10;
	do { /* loop_init */
		*hl++ = 0;
	} while (--b);
	*hl = 0xff; /* terminating byte */

	/* display card list to order */
	InitAndDrawCardListScreenLayout();
	SetCardListHeaderText(ChooseTheOrderOfTheCardsText, DuelistDeckText);
	PrintSortNumberInCardList_SetPointer();

read_input:
	if (DisplayCardList(&card))
		goto undo; /* if B is pressed, undo last order selection */

	/* a card was selected, check if it's already been selected */
	hl = wDuelTempList + 10 + hCurMenuItem;
	if (*hl != 0)
		goto read_input; /* already has an ordering number */

	/* hasn't been ordered yet, apply to it current ordering number
	 * and increase it by 1. */
	a = hCurSelectionItem;
	*hl = a;
	a++;
	hCurSelectionItem = a;

	/* refresh screen */
	PrintSortNumberInCardList_CallFromPointer();

	/* check if we're done ordering */
	if (hCurSelectionItem < wNumberOfCardsToOrder)
		goto read_input; /* if still more cards to select, loop back up */

	/* we're done selecting cards */
	EraseCursor();
	if (YesOrNoMenuWithText_LeftAligned(IsThisOKText))
		goto clear_list; /* "No" was selected, start over */
	/* selection was confirmed */

	/* now wDuelTempList + 10 will be overwritten with the
	 * card indices in order of selection.
	 * (hTempCardIndex_ff9f + order number = hTempList + order number - 1) */
	hl = wDuelTempList + 10;
	de = wDuelTempList;
	c = 0;
	for (;;) { /* loop_write_indices */
		a = *hl++;
		if (a == 0xff)
			break;
		*gbptr((u16)(ADDR_hTempCardIndex_ff9f + a)) = *de;
		de++;
		c++;
	}

	/* done_write_indices */
	hTempList[c] = 0xff; /* terminating byte */
	return false;

undo:
	/* undo last selection and get previous order number */
	a = hCurSelectionItem;
	if (a == 1)
		goto read_input; /* already at first input, nothing to undo */
	a--;
	hCurSelectionItem = a;
	c = a;
	hl = wDuelTempList + 10;
	while (*hl++ != c)
		; /* loop_find_last_entry */
	hl--;
	*hl = 0; /* overwrite order number with 0 */
	PrintSortNumberInCardList_CallFromPointer();
	goto read_input;
}

bool Pokedex_OrderDeckCardsEffect(void)
{
	TRACE_FN();
	/* place cards in order to the hand. */
	u8 *hl = hTempList;
	u8 c = 0;
	for (;;) { /* loop_place_hand */
		u8 a = *hl++;
		if (a == 0xff)
			break;
		SearchCardInDeckAndAddToHand(a);
		c++;
	}

	/* place_top_deck:
	 * go to last card in list and iterate in decreasing order
	 * placing each card in top of deck. */
	hl -= 2;
	do { /* loop_place_deck */
		ReturnCardToDeck(*hl--);
	} while (--c);
	return false;
}

bool BillEffect(void)
{
	TRACE_FN();
	DisplayDrawNCardsScreen(2);
	u8 c = 2;
	do { /* loop_draw */
		u8 card;
		if (DrawCardFromDeck(&card))
			return true; /* done */
		hTempCardIndex_ff98 = card;
		AddCardToHand(card);
		if (IsPlayerTurn())
			DisplayPlayerDrawCardScreen();
		/* skip_display_screen */
	} while (--c);
	/* (if the Player drew, the carry is whatever DisplayPlayerDrawCardScreen left;
	 * BEFORE_DAMAGE carry is not read by the caller) */
	return false;
}

/* LassEffect.ShuffleDuelistHandTrainerCardsInDeck */
static void LassEffect_ShuffleDuelistHandTrainerCardsInDeck(void)
{
	CreateHandCardList(NULL);
	SortCardsInDuelTempListByID();
	hCurSelectionItem = 0;

	/* go through all cards in hand
	 * and any Trainer card is returned to deck. */
	u8 *hl = wDuelTempList;
	for (;;) { /* loop_hand */
		u8 a = *hl++;
		hTempCardIndex_ff98 = a;
		if (a == 0xff)
			break;
		if (GetCardType(GetCardIDFromDeckIndex(a)) != TYPE_TRAINER)
			continue;
		RemoveCardFromHand(hTempCardIndex_ff98);
		ReturnCardToDeck(hTempCardIndex_ff98);
		hCurSelectionItem++;
	}
	/* done: show card list */
	if (hCurSelectionItem != 0)
		ShuffleCardsInDeck(); /* only show list if there were any Trainer cards */
}

/* LassEffect.DisplayOppHand */
static void LassEffect_DisplayOppHand(void)
{
	u8 card;
	if (CreateHandCardList(NULL)) {
		/* no_cards */
		DrawWideTextBox_WaitForInput(DuelistHasNoCardsInHandText);
		return;
	}
	InitAndDrawCardListScreenLayout();
	SetCardListHeaderText(ChooseTheCardYouWishToExamineText, DuelistHandText);
	wNoItemSelectionMenuKeys = PAD_A | PAD_START;
	DisplayCardList(&card);
}

/* LassEffect.DisplayLinkOrCPUHand */
static void LassEffect_DisplayLinkOrCPUHand(void)
{
	if (wDuelType != 0) {
		/* link duel */
		u8 saved = hWhoseTurn;
		hWhoseTurn = OPPONENT_TURN;
		LassEffect_DisplayOppHand();
		hWhoseTurn = saved;
		return;
	}
	/* cpu_opp */
	SwapTurn();
	LassEffect_DisplayOppHand();
	SwapTurn();
}

bool LassEffect(void)
{
	TRACE_FN();
	/* first discard Lass card that was used */
	RemoveCardFromHand(hTempCardIndex_ff9f);
	PutCardInDiscardPile(hTempCardIndex_ff9f);

	DrawWideTextBox_WaitForInput(PleaseCheckTheOpponentsHandText);

	LassEffect_DisplayLinkOrCPUHand();
	/* do for non-Turn Duelist */
	SwapTurn();
	LassEffect_ShuffleDuelistHandTrainerCardsInDeck();
	SwapTurn();
	/* do for Turn Duelist (fallthrough) */
	LassEffect_ShuffleDuelistHandTrainerCardsInDeck();
	return false;
}

/* return carry if not enough cards in hand for effect */
bool Maintenance_HandCheck(void)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_IN_HAND);
	gTextID = NotEnoughCardsInHandText;
	return a < 3;
}

bool Maintenance_PlayerSelection(void)
{
	TRACE_FN();
	return HandlePlayerSelection2HandCards(Choose2HandCardsFromHandToReturnToDeckText,
	                                       ChooseTheCardToPutBackText);
}

bool Maintenance_ReturnToDeckAndDrawEffect(void)
{
	TRACE_FN();
	/* return both selected cards to the deck */
	RemoveCardFromHand(hTempList[0]);
	ReturnCardToDeck(hTempList[0]);
	RemoveCardFromHand(hTempList[1]);
	ReturnCardToDeck(hTempList[1]);
	ShuffleCardsInDeck();

	/* draw one card (the carry of DrawCardFromDeck is not checked) */
	DisplayDrawNCardsScreen(1);
	u8 card = 0;
	DrawCardFromDeck(&card);
	hTempCardIndex_ff98 = card;
	AddCardToHand(card);
	if (!IsPlayerTurn())
		return false;
	/* show card on screen if played by Player */
	DisplayPlayerDrawCardScreen();
	return false; /* (flags of DisplayPlayerDrawCardScreen; BEFORE_DAMAGE carry not read) */
}

/* return carry if no cards in deck */
bool PokeBall_DeckCheck(void)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK);
	gTextID = NoCardsLeftInTheDeckText;
	return a >= DECK_SIZE;
}

bool PokeBall_PlayerSelection(void)
{
	TRACE_FN();
	u8 card;
	u8 *hl;
	/* (a = number of heads, 0 or 1) */
	bool heads = Serial_TossCoin(TrainerCardSuccessCheckText);
	hTempList[0] = heads ? 1 : 0; /* store coin result */
	if (!heads)
		return false;

	/* create list of all Pokemon cards in deck to search for */
	CreateDeckCardList(NULL);
	if (LookForCardsInDeck_HL_BC(SEARCHEFFECT_POKEMON, 0,
	                             ChooseBasicOrEvolutionPokemonCardFromDeckText, EvolutionCardText))
		goto no_pkmn; /* return if Player chose not to check deck */

	/* handle input */
	InitAndDrawCardListScreenLayout_WithSelectCheckMenu();
	SetCardListHeaderText(ChoosePokemonCardText, DuelistDeckText);
read_input:
	if (DisplayCardList(&card))
		goto try_exit; /* B was pressed, check if Player can cancel operation */
	LoadCardDataToBuffer2_FromDeckIndex(hTempCardIndex_ff98);
	if (wLoadedCard2.Type >= TYPE_ENERGY)
		goto play_sfx; /* can't select non-Pokemon card */
	hTempList[1] = hTempCardIndex_ff98;
	return false;

no_pkmn:
	hTempList[1] = 0xff;
	return false;

play_sfx:
	PlaySFX_InvalidChoice();
	goto read_input;

try_exit:
	/* Player can only exit screen if there are no cards to choose */
	hl = wDuelTempList;
	for (;;) { /* loop */
		u8 a = *hl++;
		if (a == 0xff)
			goto no_pkmn;
		LoadCardDataToBuffer2_FromDeckIndex(a);
		if (wLoadedCard2.Type >= TYPE_ENERGY)
			continue;
		goto read_input;
	}
}

bool PokeBall_AddToHandEffect(void)
{
	TRACE_FN();
	if (hTempList[0] == 0)
		return false; /* return if coin toss was tails */

	if (hTempList[1] == 0xff)
		goto done; /* skip if no Pokemon was chosen */

	/* add Pokemon card to hand and show in screen if
	 * it wasn't the Player who played the Trainer card. */
	SearchCardInDeckAndAddToHand(hTempList[1]);
	AddCardToHand(hTempList[1]);
	if (IsPlayerTurn())
		goto done;
	DisplayCardDetailScreen(hTempList[1], WasPlacedInTheHandText);
done:
	ShuffleCardsInDeck();
	return false;
}

/* return carry if no cards in the Discard Pile */
bool Recycle_DiscardPileCheck(void)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_IN_DISCARD_PILE);
	gTextID = ThereAreNoCardsInTheDiscardPileText;
	return a < 1;
}

bool Recycle_PlayerSelection(void)
{
	TRACE_FN();
	u8 card;
	if (!Serial_TossCoin(TrainerCardSuccessCheckText)) {
		/* tails */
		hTempList[0] = 0xff;
		return false;
	}

	CreateDiscardPileCardList();
	InitAndDrawCardListScreenLayout_WithSelectCheckMenu();
	SetCardListHeaderText(PleaseSelectCardText, PlayerDiscardPileText);
	while (DisplayCardList(&card))
		; /* can't cancel with B button */

	/* Discard Pile card was chosen */
	hTempList[0] = hTempCardIndex_ff98;
	return false;
}

bool Recycle_AddToHandEffect(void)
{
	TRACE_FN();
	if (hTempList[0] == 0xff)
		return false; /* return if no card was selected */

	/* add card to hand and show in screen if
	 * it wasn't the Player who played the Trainer card.
	 * (the card is actually put on top of the deck) */
	ReturnCardToDeck(MoveDiscardPileCardToHand(hTempList[0]));
	if (IsPlayerTurn())
		return true;
	DisplayCardDetailScreen(hTempList[0], CardWasChosenText);
	return false;
}

/* return carry if Bench is full or
 * if no Basic Pokemon cards in Discard Pile. */
bool Revive_BenchCheck(void)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	gTextID = NoSpaceOnTheBenchText;
	if (a >= MAX_PLAY_AREA_POKEMON)
		return true;
	bool carry = CreateBasicPokemonCardListFromDiscardPile();
	gTextID = ThereAreNoPokemonInDiscardPileText;
	return carry;
}

bool Revive_PlayerSelection(void)
{
	TRACE_FN();
	u8 card;
	/* create Basic Pokemon card list from Discard Pile */
	DrawWideTextBox_WaitForInput(ChooseBasicPokemonToPlaceOnBenchText);
	CreateBasicPokemonCardListFromDiscardPile();
	InitAndDrawCardListScreenLayout_WithSelectCheckMenu();

	/* display screen to select Pokemon */
	SetCardListHeaderText(PleaseSelectCardText, PlayerDiscardPileText);
	bool carry = DisplayCardList(&card);

	/* store selection */
	hTemp_ffa0 = hTempCardIndex_ff98;
	return carry;
}

bool Revive_PlaceInPlayAreaEffect(void)
{
	TRACE_FN();
	/* place selected Pokemon in the Bench */
	u8 a = MoveDiscardPileCardToHand(hTemp_ffa0);
	AddCardToHand(a);
	PutHandPokemonCardInPlayArea(a, &a);

	/* set HP to half, rounded up */
	u8 *hp = &TurnDuelistVars()[(u8)(a + DUELVARS_ARENA_CARD_HP)];
	a = (u8)(*hp >> 1);
	if (a & 1)
		a += 5; /* round up HP to nearest 10 */
	/* rounded */
	*hp = a;
	if (IsPlayerTurn())
		return true; /* done if Player played Revive */

	/* display card */
	DisplayCardDetailScreen(hTemp_ffa0, PlacedOnTheBenchText);
	return false;
}

/* makes list in wDuelTempList with all Basic Pokemon cards
 * that are in Turn Duelist's Discard Pile.
 * if list turns out empty, return carry. */
static bool CreateBasicPokemonCardListFromDiscardPile(void)
{
	/* gets hl to point at end of Discard Pile cards
	 * and iterates the cards in reverse order. */
	u8 b = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_IN_DISCARD_PILE);
	u8 *vars = TurnDuelistVars();
	u8 l = (u8)(b + DUELVARS_DECK_CARDS);
	u8 *de = wDuelTempList;
	b++;
	for (;;) {
		/* next_discard_pile_card */
		l--;
		if (--b == 0)
			break;
		/* check_card */
		LoadCardDataToBuffer2_FromDeckIndex(vars[l]);
		if (wLoadedCard2.Type >= TYPE_ENERGY)
			continue; /* if not Pokemon card, skip */
		if (wLoadedCard2.Stage != 0)
			continue; /* if not Basic stage, skip */
		/* write this card's index to wDuelTempList */
		*de++ = vars[l];
	}

	/* done with the loop. */
	*de = 0xff; /* terminating byte */
	if (wDuelTempList[0] == 0xff)
		return true;
	return false;
}

/* return carry if Turn Duelist has no Evolution cards in Play Area */
bool DevolutionSpray_PlayAreaEvolutionCheck(void)
{
	TRACE_FN();
	u8 c = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	u8 *vars = TurnDuelistVars();
	u8 l = DUELVARS_ARENA_CARD;
	do { /* loop */
		LoadCardDataToBuffer2_FromDeckIndex(vars[l++]);
		if (wLoadedCard2.Stage != 0)
			return false; /* found an Evolution card */
	} while (--c);

	gTextID = ThereAreNoStage1PokemonText;
	return true;
}

bool DevolutionSpray_PlayerSelection(void)
{
	TRACE_FN();
	u8 d = 0, e = 0;
	u8 *hp, *stage, *arena_card;
	u8 saved_hp, saved_stage, saved_card;

	/* display textbox */
	DrawWideTextBox_WaitForInput(ChooseEvolutionCardAndPressAButtonToDevolveText);

	/* have Player select an Evolution card in Play Area */
	hCurSelectionItem = 1;
	HasAlivePokemonInPlayArea(NULL);
	do { /* read_input */
		if (OpenPlayAreaScreenForSelection())
			return true; /* exit if B was pressed */
	} while (GetCardOneStageBelow(&d, &e)); /* can't select Basic cards */

	/* get pre-evolution card data (pushed on the stack in the original) */
	hp = &TurnDuelistVars()[(u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD_HP)];
	saved_hp = *hp;
	stage = &TurnDuelistVars()[(u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD_STAGE)];
	saved_stage = *stage;
	arena_card = &TurnDuelistVars()[(u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)];
	saved_card = *arena_card;
	goto update_data;

repeat_devolution:
	/* show Play Area screen with static cursor
	 * so that the Player either presses A to do one more devolution
	 * or presses B to finish selection.
	 * (InitAndPrintPlayAreaCardInformationAndLocation_WithTextBox, inlined to get its
	 * carry: see part4_needs.h) */
	InitAndPrintPlayAreaCardInformationAndLocation();
	SetCursorParametersForTextBox(0, wCurPlayAreaY, SYM_CURSOR_R, SYM_SPACE);
	if (WaitForButtonAorB())
		goto done_selection; /* if B pressed, end selection instead */
	/* do one more devolution */
	GetCardOneStageBelow(&d, &e);

update_data:
	/* overwrite the card data to new devolved stats */
	UpdateDevolvedCardHPAndStage(d);
	*GetNextPositionInTempList_TrainerEffects() = e;
	LoadCardDataToBuffer2_FromDeckIndex(d);
	if (wLoadedCard2.Stage != 0)
		goto repeat_devolution; /* can do one more devolution */

done_selection:
	*GetNextPositionInTempList_TrainerEffects() = 0xff; /* terminating byte */

	/* store this Play Area location in first item of temp list */
	hTempList[0] = hTempPlayAreaLocation_ff9d;

	/* update Play Area location display of this Pokemon */
	EmptyScreen();
	wHUDEnergyAndHPBarsX = hTempPlayAreaLocation_ff9d;
	wHUDEnergyAndHPBarsY = 0x00;
	PrintPlayAreaCardInformationAndLocation();

	/* rewrite all duelvars from before the selection was done
	 * this is so that if "No" is selected in confirmation menu,
	 * then the Pokemon isn't devolved and remains unchanged. */
	*arena_card = saved_card;
	bool carry = YesOrNoMenuWithText(IsThisOKText);
	*stage = saved_stage;
	*hp = saved_hp;
	return carry;
}

bool DevolutionSpray_DevolutionEffect(void)
{
	TRACE_FN();
	/* (d and e keep their values across iterations, as in the original) */
	u8 d = 0, e = 0;

	/* first byte in list is Play Area location chosen */
	u8 *hl = hTempList;
	hTempPlayAreaLocation_ff9d = *hl++;
	u8 *arena_card = &TurnDuelistVars()[(u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)];
	u8 top_card = *arena_card;

	/* loop through devolutions selected */
	hl = hTempList + 1;
	while (*hl != 0xff) { /* loop_devolutions (list is over at $ff) */
		/* devolve card to its stage below */
		GetCardOneStageBelow(&d, &e);
		UpdateDevolvedCardHPAndStage(d);
		ResetDevolvedCardStatus();
		PutCardInDiscardPile(*hl++);
	}

	/* check_ko */
	e = top_card;
	d = *arena_card;
	PrintDevolvedCardNameAndLevelText(d, e);
	PrintPlayAreaCardKnockedOutIfNoHP(hTempList[0]);
	HandleDestinyBondAndBetweenTurnKnockOuts();
	return false; /* (BEFORE_DAMAGE: carry not read by the caller) */
}

/* returns carry if neither duelist has any energy cards attached */
bool SuperEnergyRemoval_EnergyCheck(void)
{
	TRACE_FN();
	bool carry = CheckIfThereAreAnyEnergyCardsAttached();
	gTextID = NoEnergyCardsAttachedToPokemonInYourPlayAreaText;
	if (carry)
		return true;
	SwapTurn();
	carry = CheckIfThereAreAnyEnergyCardsAttached();
	gTextID = NoEnergyCardsAttachedToPokemonInOppPlayAreaText;
	SwapTurn();
	return carry;
}

bool SuperEnergyRemoval_PlayerSelection(void)
{
	TRACE_FN();
	/* handle selection of Energy to discard in own Play Area */
	DrawWideTextBox_WaitForInput(ChoosePokemonInYourAreaThenPokemonInYourOppText);
	if (HandlePokemonAndEnergySelectionScreen())
		return true; /* return if operation was cancelled */

	DrawWideTextBox_WaitForInput(ChoosePokemonToRemoveEnergyFromText);

	SwapTurn();
	hCurSelectionItem = 3;
	for (;;) { /* select_opp_pkmn */
		HasAlivePokemonInPlayArea(NULL);
		if (OpenPlayAreaScreenForSelection()) {
			/* B was pressed */
			SwapTurn();
			return true; /* return if operation was cancelled */
		}
		/* opp_pkmn_selected */
		GetPlayAreaCardAttachedEnergies(hTempPlayAreaLocation_ff9d);
		if (wTotalAttachedEnergies != 0)
			break; /* has any energy cards attached? */
		/* no energy, loop back */
		DrawWideTextBox_WaitForInput(NoEnergyCardsText);
	}

	/* has_energy: store this Pokemon's Play Area location */
	hPlayAreaEffectTarget = hTempPlayAreaLocation_ff9d;
	/* store which energy card to discard from it */
	CreateArenaOrBenchEnergyCardList(hTempPlayAreaLocation_ff9d);
	DisplayEnergyDiscardScreen(hTempPlayAreaLocation_ff9d);
	wEnergyDiscardMenuDenominator = 2;

	for (;;) { /* loop_discard_energy_selection */
		if (HandleEnergyDiscardMenuInput()) {
			/* B pressed */
			if (!AskWhetherToQuitSelectingCards(5))
				break; /* finish operation */
			/* player selected to continue selection */
			u8 numerator = wEnergyDiscardMenuNumerator;
			DisplayEnergyDiscardScreen(hTempPlayAreaLocation_ff9d);
			wEnergyDiscardMenuDenominator = 2;
			wEnergyDiscardMenuNumerator = numerator;
			continue;
		}

		/* energy_selected: store energy cards to discard from opponent */
		*GetNextPositionInTempList_TrainerEffects() = hTempCardIndex_ff98;
		RemoveCardFromDuelTempList(hTempCardIndex_ff98);
		wEnergyDiscardMenuNumerator++;
		if (hCurSelectionItem >= 5)
			break; /* no more energy cards to select */
		if (wDuelTempList[0] == 0xff)
			break; /* no more energy cards to select */
		DisplayEnergyDiscardMenu();
	}

	/* done */
	*GetNextPositionInTempList_TrainerEffects() = 0xff;
	SwapTurn();
	return false;
}

bool SuperEnergyRemoval_DiscardEffect(void)
{
	TRACE_FN();
	u8 *hl = hTempList + 1;

	/* discard energy card of own Play Area */
	PutCardInDiscardPile(*hl++);

	/* iterate and discard opponent's energy cards */
	hl++;
	SwapTurn();
	for (;;) { /* loop */
		u8 a = *hl++;
		if (a == 0xff)
			break;
		PutCardInDiscardPile(a);
	}

	/* done_discard: if it's Player's turn, return... */
	SwapTurn();
	if (IsPlayerTurn())
		return true;
	/* ...otherwise show Play Area of affected Pokemon
	 * in opponent's Play Area */
	DrawPlayAreaScreenToShowChanges(hTemp_ffa0);
	/* in player's Play Area */
	wDuelDisplayedScreen = 0;
	SwapTurn();
	DrawPlayAreaScreenToShowChanges(hPlayAreaEffectTarget);
	SwapTurn();
	return false; /* (carry of DrawPlayAreaScreenToShowChanges: see EnergyRemoval_DiscardEffect) */
}

/* return carry if not enough cards in hand to
 * discard for Super Energy Retrieval effect
 * or if the Discard Pile has no basic Energy cards */
bool SuperEnergyRetrieval_HandEnergyCheck(void)
{
	TRACE_FN();
	u8 a = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_IN_HAND);
	gTextID = NotEnoughCardsInHandText;
	if (a < 3)
		return true;
	bool carry = CreateEnergyCardListFromDiscardPile_OnlyBasic();
	gTextID = ThereAreNoBasicEnergyCardsInDiscardPileText;
	return carry;
}

bool SuperEnergyRetrieval_PlayerHandSelection(void)
{
	TRACE_FN();
	return HandlePlayerSelection2HandCardsToDiscard();
}

bool SuperEnergyRetrieval_PlayerDiscardPileSelection(void)
{
	TRACE_FN();
	u8 card;
	/* (hCurSelectionItem is not reset: it is 2 from HandlePlayerSelection2HandCards) */
	DrawWideTextBox_WaitForInput(ChooseUpTo4FromDiscardPileText);
	CreateEnergyCardListFromDiscardPile_OnlyBasic();

loop_discard_pile_selection:
	InitAndDrawCardListScreenLayout();
	SetCardListHeaderText(PleaseSelectCardText, PlayerDiscardPileText);
	if (DisplayCardList(&card)) {
		/* B pressed */
		if (AskWhetherToQuitSelectingCards(6))
			goto loop_discard_pile_selection; /* player selected to continue */
		goto done;
	}

	/* store_selected_card */
	(void)GetTurnDuelistVariable(hTempCardIndex_ff98); /* (result unused in the original too) */
	*GetNextPositionInTempList_TrainerEffects() = hTempCardIndex_ff98; /* store selected energy card */
	if (RemoveCardFromDuelTempList(hTempCardIndex_ff98))
		goto done;
	/* this shouldn't happen */
	if (hCurSelectionItem < 6)
		goto loop_discard_pile_selection;

done:
	/* insert terminating byte */
	*GetNextPositionInTempList_TrainerEffects() = 0xff;
	return false;
}

bool SuperEnergyRetrieval_DiscardAndAddToHandEffect(void)
{
	TRACE_FN();
	/* discard 2 cards selected from the hand */
	u8 *hl = &hTemp_ffa0;
	u8 a = *hl++;
	RemoveCardFromHand(a);
	PutCardInDiscardPile(a);
	a = *hl++;
	RemoveCardFromHand(a);
	PutCardInDiscardPile(a);

	/* put selected cards in hand */
	u8 *de = wDuelTempList;
	for (;;) { /* loop */
		a = *hl++;
		*de++ = a;
		if (a == 0xff)
			break;
		AddCardToHand(MoveDiscardPileCardToHand(a));
	}

	/* done: if Player played the card, exit */
	if (IsPlayerTurn())
		return true;
	/* if not, show card list selected by Opponent */
	DisplayCardListDetails();
	return false;
}

/* outputs in hl the next position
 * in hTempList to place a new card,
 * and increments hCurSelectionItem.
 * identical to GetNextPositionInTempList. */
static u8 *GetNextPositionInTempList_TrainerEffects(void)
{
	u8 a = hCurSelectionItem;
	hCurSelectionItem++;
	return &hTempList[a];
}

/* handles screen for Player to select 2 cards from the hand to discard.
 * first prints text informing Player to choose cards to discard
 * then runs HandlePlayerSelection2HandCards routine. */
static bool HandlePlayerSelection2HandCardsToDiscard(void)
{
	return HandlePlayerSelection2HandCards(Choose2CardsFromHandToDiscardText,
	                                       ChooseTheCardToDiscardText);
}

/* handles screen for Player to select 2 cards from the hand
 * to activate some Trainer card effect.
 * assumes Trainer card index being used is in [hTempCardIndex_ff9f].
 * stores selection of cards in hTempList.
 * returns carry if Player cancels operation.
 * input:
 *	hl = text to print in text box;
 *	de = text to print in screen header (used as the card list info box text). */
static bool HandlePlayerSelection2HandCards(u16 hl, u16 de)
{
	u8 card;
	DrawWideTextBox_WaitForInput(hl);

	/* remove the Trainer card being used from list
	 * of cards to select from hand. */
	CreateHandCardList(NULL);
	RemoveCardFromDuelTempList(hTempCardIndex_ff9f);

	hCurSelectionItem = 0;
	do { /* loop */
		InitAndDrawCardListScreenLayout_WithSelectCheckMenu();
		SetCardListInfoBoxText(de);
		if (DisplayCardList(&card))
			return true; /* was B pressed? */
		*GetNextPositionInTempList_TrainerEffects() = hTempCardIndex_ff98;
		RemoveCardFromDuelTempList(hTempCardIndex_ff98);
	} while (hCurSelectionItem < 2); /* is selection over? */
	return false;
}

/* return carry if non-turn duelist has no benched Pokemon */
bool GustOfWind_BenchCheck(void)
{
	TRACE_FN();
	u8 a = GetNonTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	gTextID = EffectNoPokemonOnTheBenchText;
	return a < 2;
}

bool GustOfWind_PlayerSelection(void)
{
	TRACE_FN();
	DrawWideTextBox_WaitForInput(ChooseAPokemonToSwitchWithActivePokemonText);
	SwapTurn();
	HasAlivePokemonInBench(NULL);
	bool carry = OpenPlayAreaScreenForSelection();
	hTemp_ffa0 = hTempPlayAreaLocation_ff9d;
	SwapTurn();
	return carry;
}

bool GustOfWind_SwitchEffect(void)
{
	TRACE_FN();
	/* play whirlwind animation */
	PlayTrainerEffectAnimation(ATK_ANIM_GUST_OF_WIND);

	/* switch Arena card */
	SwapTurn();
	SwapArenaWithBenchPokemon(hTemp_ffa0);
	SwapTurn();
	ClearDamageReductionSubstatus2();
	wDuelDisplayedScreen = 0;
	return false;
}

/* input:
 *	a = attack animation to play */
static void PlayTrainerEffectAnimation(u8 anim)
{
	wLoadedAttackAnimation = anim;
	ResetAttackAnimationIsPlaying();
	/* (de: leftover, see top of file) */
	PlayAttackAnimation(0, PLAY_AREA_ARENA, 0x0, hWhoseTurn);
	WaitAttackAnimation();
}

/* heals amount of damage in register e for card in
 * Play Area location in [hTempPlayAreaLocation_ff9d].
 * plays healing animation and prints text with card's name.
 * input:
 *	a = amount of HP to heal
 *	[hTempPlayAreaLocation_ff9d] = Play Area location of card to heal
 * returns the carry of the final `add e` (HP overflow). */
static bool HealPlayAreaCardHP(u8 a)
{
	u16 de = a;

	/* play heal animation */
	ResetAttackAnimationIsPlaying();
	wLoadedAttackAnimation = ATK_ANIM_HEALING_WIND_PLAY_AREA;
	PlayAttackAnimation(de, hTempPlayAreaLocation_ff9d, 0x01, hWhoseTurn);
	WaitAttackAnimation();

	/* print Pokemon card name and damage healed */
	LoadTxRam3(de);
	LoadTxRam2(0x0000);
	LoadCardDataToBuffer1_FromDeckIndex(
		GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)));
	CopyCardNameAndLevel(18);
	/* (terminating character on end of the name: see top of file) */
	DrawWideTextBox_WaitForInput(PokemonHealedDamageText);

	/* heal the target Pokemon */
	u8 *hp = &TurnDuelistVars()[(u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD_HP)];
	u16 sum = (u16)(*hp + (u8)de);
	*hp = (u8)sum;
	return sum > 0xff;
}
