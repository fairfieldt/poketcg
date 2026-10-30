/*
 * Port of the logic of src/engine/duel/core.asm: duel setup, the main duel
 * loop, the player's duel menu actions, the AI/link opponent action table,
 * between-turns events, knock outs and prizes.
 *
 * The screen-drawing parts of core.asm live in duel_ui.c as shims.
 * The original's control flow often jumps back to DuelMainInterface or
 * PrintDuelMenuAndHandleInput from deep inside menu handlers; here those
 * handlers return a DuelMenuNext value that the turn loop dispatches on.
 */
#include "engine.h"
#include "effects/effects.h"
#include "gen/rom_blobs.h"

enum DuelMenuNext {
	NEXT_DUEL_MAIN_INTERFACE,          /* jp DuelMainInterface */
	NEXT_PRINT_DUEL_MENU,              /* jp PrintDuelMenuAndHandleInput */
	NEXT_RESTART_PRACTICE_DUEL_TURN,   /* jp RestartPracticeDuelTurn */
	NEXT_TURN_ENDED,                   /* ret (out of HandleTurn) */
};

static void MainDuelLoop(void);
static void HandleTurn(void);
static void DuelMainInterface(int entry);
static bool HandleDuelSetup(void);
static void HandleBetweenTurnsEvents(void);
static bool DoPracticeDuelAction(u8 action);

/* ---------------------------------------------------------------- link stubs (home/serial.asm) */

/* the port never runs a serial link duel (wDuelType is never DUELTYPE_LINK) */
void ExchangeRNG(void)
{
	if (wDuelType == DUELTYPE_LINK)
		tcg_fatal("link duels are not supported");
}

/* sets hOppActionTableIndex to an AI action; in a link duel the original would also
 * send the action and its parameters to the other Game Boy. */
void SetOppAction_SerialSendDuelData(u8 action)
{
	hOppActionTableIndex = action;
	if (GetNonTurnDuelistVariable(DUELVARS_DUELIST_TYPE) == DUELIST_TYPE_LINK_OPP)
		tcg_fatal("link duels are not supported");
	ExchangeRNG();
}

void SerialSend8Bytes(void)
{
	if (GetNonTurnDuelistVariable(DUELVARS_DUELIST_TYPE) == DUELIST_TYPE_LINK_OPP)
		tcg_fatal("link duels are not supported");
}

bool SerialRecvByte(u8 *a)
{
	(void)a;
	tcg_fatal("link duels are not supported");
	return true;
}

void SerialRecv8Bytes(void)
{
	tcg_fatal("link duels are not supported");
}

/* ---------------------------------------------------------------- duel start */

/* this function begins the duel after the opponent's graphics, name and deck have been introduced
 * loads both player's decks and sets up the variables and resources required to begin a duel. */
void StartDuel_VSAIOpp(void)
{
	hWhoseTurn = PLAYER_TURN;
	wPlayerDuelistType = DUELIST_TYPE_PLAYER;
	wOpponentDeckID = wNPCDuelDeckID;
	LoadPlayerDeck();
	SwapTurn();
	LoadOpponentDeck();
	SwapTurn();
	StartDuel();
}

void StartDuel(void)
{
	TRACE_FN();
	wDuelReturnAddress[0] = 0;
	wDuelReturnAddress[1] = 0;
	wCurrentDuelMenuItem = 0;
	SetupDuel();
	wDuelInitialPrizes = wNPCDuelPrizes;
	InitVariablesToBeginDuel();
	PlaySong(wDuelTheme);
	if (HandleDuelSetup())
		return;
	MainDuelLoop();
}

/* the loop returns here after every turn switch */
static void MainDuelLoop(void)
{
	for (;;) {
		wCurrentDuelMenuItem = 0;
		UpdateSubstatusConditions_StartOfTurn();
		DisplayDuelistTurnScreen();
		HandleTurn();
		/* between_turns */
		ExchangeRNG();
		if (!wDuelFinished) {
			UpdateSubstatusConditions_EndOfTurn();
			HandleBetweenTurnsEvents();
			FinishQueuedAnimations();
			ExchangeRNG();
		}
		if (wDuelFinished) {
			/* duel_finished */
			if (!DuelFinished())
				return;
			continue; /* sudden death */
		}
		wDuelTurns++;
		if (wDuelType == DUELTYPE_PRACTICE && wIsPracticeDuel) {
			/* the practice duel lasts 15 turns (8 player turns and 7 opponent turns) */
			if (wDuelTurns >= 15) {
				wDuelResult = DUEL_WIN;
				return;
			}
		}
		SwapTurn();
	}
}

/* .duel_finished: show the result. returns true if the duel continues (sudden death) */
bool DuelFinished(void)
{
	ZeroObjectPositionsAndToggleOAMCopy();
	EmptyScreen();
	DrawDuelBoxMessage(BOXMSG_DECISION);
	DrawWideTextBox_WaitForInput(DecisionText);
	EmptyScreen();
	u8 turn = hWhoseTurn;
	hWhoseTurn = PLAYER_TURN;
	DrawDuelistPortraitsAndNames();
	PrintDuelResultStats();
	hWhoseTurn = turn;
	/* animate the duel result screen: load the correct music and animation depending on result */
	ResetAnimationQueue();
	u8 anim, song;
	u16 text;
	bool player_won;
	if (wDuelFinished == TURN_PLAYER_WON)
		player_won = hWhoseTurn == PLAYER_TURN;
	else if (wDuelFinished == TURN_PLAYER_LOST)
		player_won = hWhoseTurn != PLAYER_TURN;
	else
		goto draw;
	if (player_won) {
		wDuelResult = DUEL_WIN;
		anim = DUEL_ANIM_DUEL_WIN;
		song = MUSIC_MATCH_VICTORY;
		text = WonDuelText;
	} else {
		wDuelResult = DUEL_LOSS;
		anim = DUEL_ANIM_DUEL_LOSS;
		song = MUSIC_MATCH_LOSS;
		text = LostDuelText;
	}
	goto handle;
draw:
	anim = DUEL_ANIM_DUEL_DRAW;
	song = MUSIC_MATCH_DRAW;
	text = DuelWasADrawText;
handle:
	PlayDuelAnimation(anim);
	PlaySong(song);
	hWhoseTurn = OPPONENT_TURN;
	DrawWideTextBox_PrintText(text);
	tcg_ui_event(TCG_EV_DUEL_END, wDuelFinished, turn, 0, 0, text);
	if (wDuelFinished != TURN_PLAYER_TIED) {
		PlayDefaultSong();
		WaitForWideTextBoxInput();
		FinishQueuedAnimations();
		hWhoseTurn = PLAYER_TURN;
		return false;
	}
	/* tied_duel */
	WaitForWideTextBoxInput();
	FinishQueuedAnimations();
	PlaySong(wDuelTheme);
	DrawWideTextBox_WaitForInput(StartSuddenDeathMatchText);
	wDuelInitialPrizes = 1;
	InitVariablesToBeginDuel();
	if (wDuelType == DUELTYPE_LINK)
		tcg_fatal("link duels are not supported");
	hWhoseTurn = PLAYER_TURN;
	if (HandleDuelSetup())
		return false;
	return true;
}

/* handle the turn of the duelist identified by hWhoseTurn.
 * if player's turn, display the animation of the player drawing the card at
 * hTempCardIndex_ff98, and save the duel state to SRAM. */
static void HandleTurn(void)
{
	TRACE_FN();
	wDuelistType = GetTurnDuelistVariable(DUELVARS_DUELIST_TYPE);
	if (wDuelTurns >= 2) /* skip if it's the turn holder's first turn */
		SetAllPlayAreaPokemonCanEvolve();
	InitVariablesToBeginTurn();
	tcg_ui_event(TCG_EV_TURN, hWhoseTurn, wDuelTurns, 0, 0, 0);
	DisplayDrawOneCardScreen();
	u8 card;
	if (DrawCardFromDeck(&card)) {
		wDuelFinished = TURN_PLAYER_LOST;
		return;
	}
	hTempCardIndex_ff98 = card;
	AddCardToHand(card);
	if (wDuelistType != DUELIST_TYPE_PLAYER) {
		/* opponent's turn */
		SwapTurn();
		bool c = IsClairvoyanceActive();
		SwapTurn();
		if (c)
			DisplayPlayerDrawCardScreen();
		DuelMainInterface(NEXT_DUEL_MAIN_INTERFACE);
		return;
	}
	/* player's turn */
	DisplayPlayerDrawCardScreen();
	SaveDuelStateToSRAM();
	DuelMainInterface(NEXT_RESTART_PRACTICE_DUEL_TURN);
}

/* ---------------------------------------------------------------- the player's duel menu */

static int DuelMenu_Hand(void);
static int DuelMenu_Attack(void);
static int DuelMenu_Check(void);
static int DuelMenu_PkmnPower(void);
static int DuelMenu_Retreat(void);
static int DuelMenu_Done(void);

/* PrintDuelMenuAndHandleInput: returns the next step */
static int PrintDuelMenuAndHandleInput(void)
{
	DrawWideTextBox();
	PlaceDuelMenuItems();
	/* bug (original): saving here can be problematic since in specific circumstances the duel may be over,
	 * and reloading it from here will lead to an invalid duel state */
	SaveDuelData();
	if (wDuelFinished)
		return NEXT_TURN_ENDED;
	u8 item = HandleDuelMenuInput(wCurrentDuelMenuItem);
	wCurrentDuelMenuItem = item;
	switch (hCurMenuItem) {
	case 0: return DuelMenu_Hand();
	case 1: return DuelMenu_Attack();
	case 2: return DuelMenu_Check();
	case 3: return DuelMenu_PkmnPower();
	case 4: return DuelMenu_Retreat();
	default: return DuelMenu_Done();
	}
}

/* print the main interface during a duel, including background, Pokemon, HUDs and a text box.
 * the bottom text box changes depending on whether the turn belongs to the player (show the duel menu),
 * an AI opponent (print "Waiting..." and a reduced menu) or a link opponent (print "<Duelist> is thinking"). */
static void DuelMainInterface(int next)
{
	for (;;) {
		switch (next) {
		case NEXT_RESTART_PRACTICE_DUEL_TURN:
			/* when a practice duel turn needs to be restarted because the player did not
			 * follow the instructions correctly, the game loops back here */
			DoPracticeDuelAction(PRACTICEDUEL_PRINT_TURN_INSTRUCTIONS);
			/* fallthrough */
		case NEXT_DUEL_MAIN_INTERFACE:
			DrawDuelMainScene();
			if (wDuelistType == DUELIST_TYPE_PLAYER) {
				next = PrintDuelMenuAndHandleInput();
				break;
			}
			if (wDuelistType == DUELIST_TYPE_LINK_OPP)
				tcg_fatal("link duels are not supported");
			/* DUELIST_TYPE_AI_OPP */
			wVBlankCounter = 0;
			wSkipDuelistIsThinkingDelay = 0;
			DrawWideTextBox_PrintTextNoDelay(DuelistIsThinkingText);
			tcg_ui_event(TCG_EV_THINKING, hWhoseTurn, 0, 0, 0, 0);
			AIDoAction_Turn();
			wPlayerAttackingCardIndex = 0xff;
			wPlayerAttackingAttackIndex = 0xff;
			return;
		case NEXT_PRINT_DUEL_MENU:
			next = PrintDuelMenuAndHandleInput();
			break;
		case NEXT_TURN_ENDED:
			return;
		}
	}
}

/* triggered by selecting the "Pkmn Power" item in the duel menu */
static int DuelMenu_PkmnPower(void)
{
	if (DisplayPlayAreaScreenToUsePkmnPower())
		return NEXT_DUEL_MAIN_INTERFACE;
	UseAttackOrPokemonPower();
	return NEXT_DUEL_MAIN_INTERFACE;
}

/* triggered by selecting the "Done" item in the duel menu */
static int DuelMenu_Done(void)
{
	/* always jumps on practice duel (no action requires player to select Done) */
	if (DoPracticeDuelAction(PRACTICEDUEL_REPEAT_INSTRUCTIONS))
		return NEXT_RESTART_PRACTICE_DUEL_TURN;
	SetOppAction_SerialSendDuelData(OPPACTION_FINISH_NO_ATTACK);
	ClearNonTurnTemporaryDuelvars();
	return NEXT_TURN_ENDED;
}

/* triggered by selecting the "Retreat" item in the duel menu */
static int DuelMenu_Retreat(void)
{
	u8 a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS) & CNF_SLP_PRZ;
	hTemp_ffa0 = a;
	if (a == CONFUSED) {
		if (wConfusionRetreatCheckWasUnsuccessful)
			goto unable_due_to_confusion;
		if (CheckAbleToRetreat())
			goto unable_to_retreat;
		if (DisplayRetreatScreen())
			return NEXT_DUEL_MAIN_INTERFACE;
		DrawWideTextBox_WaitForInput(SelectPkmnOnBenchToSwitchWithActiveText);
		if (OpenPlayAreaScreenForSelection())
			return NEXT_DUEL_MAIN_INTERFACE;
		wBenchSelectedPokemon = hCurMenuItem;
		hTempPlayAreaLocation_ffa1 = wBenchSelectedPokemon;
		SetOppAction_SerialSendDuelData(OPPACTION_ATTEMPT_RETREAT);
		if (!AttemptRetreat())
			return NEXT_DUEL_MAIN_INTERFACE;
		DrawDuelMainScene();
	unable_due_to_confusion:
		DrawWideTextBox_WaitForInput(UnableToRetreatText);
		return NEXT_PRINT_DUEL_MENU;
	}
	/* not_confused:
	 * note that the energy cards are discarded (DiscardRetreatCostCards), then returned
	 * (ReturnRetreatCostCardsToArena), then discarded again for good (AttemptRetreat).
	 * It's done this way so that the retreating Pokemon is listed with its energies updated
	 * when the Play Area screen is shown to select the Pokemon to switch to. The reason why
	 * AttemptRetreat is responsible for discarding the energy cards is because, if the
	 * Pokemon is confused, it may not be able to retreat, so they cannot be discarded earlier. */
	if (CheckAbleToRetreat())
		goto unable_to_retreat;
	if (DisplayRetreatScreen())
		return NEXT_DUEL_MAIN_INTERFACE;
	DiscardRetreatCostCards();
	DrawWideTextBox_WaitForInput(SelectPkmnOnBenchToSwitchWithActiveText);
	{
		bool c = OpenPlayAreaScreenForSelection();
		wBenchSelectedPokemon = hCurMenuItem;
		hTempPlayAreaLocation_ffa1 = hCurMenuItem;
		ReturnRetreatCostCardsToArena();
		if (c)
			return NEXT_DUEL_MAIN_INTERFACE;
	}
	SetOppAction_SerialSendDuelData(OPPACTION_ATTEMPT_RETREAT);
	AttemptRetreat();
	return NEXT_DUEL_MAIN_INTERFACE;

unable_to_retreat:
	DrawWideTextBox_WaitForInput(gTextID);
	return NEXT_PRINT_DUEL_MENU;
}

static int OpenPlayerHandScreen(void);
static int OpenPlayerHandScreen_HandleInput(void);
static int PlayEnergyCard(u8 c);
static bool PlayPokemonCard(void);

/* triggered by selecting the "Hand" item in the duel menu */
static int DuelMenu_Hand(void)
{
	if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_IN_HAND) != 0)
		return OpenPlayerHandScreen();
	DrawWideTextBox_WaitForInput(NoCardsInHandText);
	return NEXT_PRINT_DUEL_MENU;
}

/* draw the screen for the player's hand and handle user input to for example check
 * a card or attempt to use a card, playing the card if possible in that case. */
static int OpenPlayerHandScreen(void)
{
	CreateHandCardList(NULL);
	InitAndDrawCardListScreenLayout();
	SetCardListInfoBoxText(PleaseSelectHandText);
	wCardListItemSelectionMenuType = PLAY_CHECK;
	return OpenPlayerHandScreen_HandleInput();
}

/* reload the card list screen after the card trying to play couldn't be played */
static int ReloadCardListScreen(void)
{
	CreateHandCardList(NULL);
	/* skip doing the things that have already been done when initially opened */
	DrawCardListScreenLayout();
	return OpenPlayerHandScreen_HandleInput();
}

static int OpenPlayerHandScreen_HandleInput(void)
{
	bool c = DisplayCardList(NULL);
	if (wSortCardListByID)
		SortHandCardsByID();
	if (c)
		return NEXT_DUEL_MAIN_INTERFACE;
	LoadCardDataToBuffer1_FromDeckIndex(hTempCardIndex_ff98);
	u8 type = wLoadedCard1.Type;
	if (type & (1 << TYPE_TRAINER_F)) {
		if (PlayTrainerCard())
			return ReloadCardListScreen(); /* card not played */
		return NEXT_DUEL_MAIN_INTERFACE;
	}
	if (type & (1 << TYPE_ENERGY_F))
		return PlayEnergyCard(type);
	if (PlayPokemonCard())
		return ReloadCardListScreen(); /* card not played */
	return NEXT_DUEL_MAIN_INTERFACE;
}

/* play the energy card with deck index at hTempCardIndex_ff98
 * c contains the type of energy card being played */
static int PlayEnergyCard(u8 c)
{
	if (c == TYPE_ENERGY_WATER && IsRainDanceActive()) {
		/* rain_dance_active */
		HasAlivePokemonInPlayArea(NULL);
		if (OpenPlayAreaScreenForSelection()) /* choose card to play energy card on */
			return NEXT_DUEL_MAIN_INTERFACE; /* exit if no card was chosen */
		if (CheckRainDanceScenario())
			goto play_energy;
		if (!wAlreadyPlayedEnergy)
			goto play_energy_set_played;
		DrawWideTextBox_WaitForInput(MayOnlyAttachOneEnergyCardText);
		return OpenPlayerHandScreen();
	}
	if (wAlreadyPlayedEnergy) {
		DrawWideTextBox_WaitForInput(MayOnlyAttachOneEnergyCardText);
		return ReloadCardListScreen();
	}
	HasAlivePokemonInPlayArea(NULL);
	if (OpenPlayAreaScreenForSelection()) /* choose card to play energy card on */
		return NEXT_DUEL_MAIN_INTERFACE; /* exit if no card was chosen */
play_energy_set_played:
	wAlreadyPlayedEnergy = TRUE;
play_energy:
	hTempPlayAreaLocation_ffa1 = hTempPlayAreaLocation_ff9d;
	hTemp_ffa0 = hTempCardIndex_ff98;
	PutHandCardInPlayArea(hTempCardIndex_ff98, hTempPlayAreaLocation_ff9d);
	PrintPlayAreaCardList_EnableLCD();
	SetOppAction_SerialSendDuelData(OPPACTION_PLAY_ENERGY);
	PrintAttachedEnergyToPokemon();
	return NEXT_DUEL_MAIN_INTERFACE;
}

/* place a basic Pokemon card on the arena or bench, or place an stage 1 or 2
 * Pokemon card over a Pokemon card already in play to evolve it.
 * the card to use is loaded in wLoadedCard1 and its deck index is at hTempCardIndex_ff98.
 * return nc if the card was played, carry if it wasn't. */
static bool PlayPokemonCard(void)
{
	if (wLoadedCard1.Stage == BASIC) {
		if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) >= MAX_PLAY_AREA_POKEMON) {
			DrawWideTextBox_WaitForInput(NoSpaceOnTheBenchText);
			return true;
		}
		hTemp_ffa0 = hTempCardIndex_ff98;
		u8 loc;
		PutHandPokemonCardInPlayArea(hTempCardIndex_ff98, &loc);
		hTempPlayAreaLocation_ff9d = loc;
		TurnDuelistVars()[(u8)(loc + DUELVARS_ARENA_CARD_STAGE)] = BASIC;
		SetOppAction_SerialSendDuelData(OPPACTION_PLAY_BASIC_PKMN);
		LoadCardDataToBuffer1_FromDeckIndex(hTempCardIndex_ff98);
		CopyCardNameAndLevel(20);
		LoadTxRam2(0);
		DrawWideTextBox_WaitForInput(PlacedOnTheBenchText);
		ProcessPlayedPokemonCard();
		return false;
	}
	/* try_evolve */
	u8 c = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	u8 d = hTempCardIndex_ff98;
	u8 e = PLAY_AREA_ARENA;
	{
		u8 n = c, ee = e;
		bool found = false;
		do {
			if (!CheckIfCanEvolveInto(d, ee, NULL)) {
				found = true;
				break;
			}
			ee++;
		} while (--n);
		if (!found) {
			u16 text = NoPokemonCapableOfEvolvingText;
			n = c;
			ee = e;
			do {
				u8 reason;
				CheckIfCanEvolveInto(d, ee, &reason);
				if (reason) {
					text = CantEvolvePokemonInSameTurnItsPlacedText;
					break;
				}
				ee++;
			} while (--n);
			/* don't bother opening the selection screen if there are no pokemon capable of evolving */
			DrawWideTextBox_WaitForInput(text);
			return true;
		}
	}
	/* can_evolve */
	if (IsPrehistoricPowerActive()) {
		DrawWideTextBox_WaitForInput(gTextID);
		return true;
	}
	HasAlivePokemonInPlayArea(NULL);
	for (;;) {
		if (OpenPlayAreaScreenForSelection())
			return false;
		hTemp_ffa0 = hTempCardIndex_ff98;
		hTempPlayAreaLocation_ffa1 = hTempPlayAreaLocation_ff9d;
		if (!EvolvePokemonCardIfPossible())
			break; /* jump if evolution wasn't successful somehow */
	}
	SetOppAction_SerialSendDuelData(OPPACTION_EVOLVE_PKMN);
	PrintPlayAreaCardList_EnableLCD();
	PrintPokemonEvolvedIntoPokemon();
	ProcessPlayedPokemonCard();
	return false;
}

/* triggered by selecting the "Check" item in the duel menu */
static int DuelMenu_Check(void)
{
	FinishQueuedAnimations();
	OpenDuelCheckMenu();
	return NEXT_DUEL_MAIN_INTERFACE;
}

/* check if the turn holder's arena Pokemon is unable to retreat due to
 * some status condition or due the bench containing no alive Pokemon.
 * return carry if unable, nc if able (text in gTextID). */
bool CheckAbleToRetreat(void)
{
	if (CheckUnableToRetreatDueToEffect())
		return true;
	if (CheckIfActiveCardParalyzedOrAsleep())
		return true;
	if (HasAlivePokemonInBench(NULL))
		goto unable_to_retreat;
	LoadCardDataToBuffer1_FromCardID(GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD)));
	if (wLoadedCard1.Type == TYPE_TRAINER)
		goto unable_to_retreat;
	if (!CheckIfEnoughEnergiesToRetreat())
		return false;
	LoadTxRam3(wEnergyCardsRequiredToRetreat);
	gTextID = EnergyCardsRequiredToRetreatText;
	return true;
unable_to_retreat:
	gTextID = UnableToRetreatText;
	return true;
}

/* check if the turn holder's arena Pokemon has enough energies attached to it
 * in order to retreat. Return carry if it doesn't.
 * load amount of energies required to wEnergyCardsRequiredToRetreat. */
bool CheckIfEnoughEnergiesToRetreat(void)
{
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	hTempPlayAreaLocation_ff9d = PLAY_AREA_ARENA;
	u8 c = GetPlayAreaCardRetreatCost();
	wEnergyCardsRequiredToRetreat = c;
	u8 a = wTotalAttachedEnergies;
	if (a < c)
		return true;
	wNumRetreatEnergiesSelected = a;
	wEnergyCardsRequiredToRetreat = c;
	return false;
}

/* display the screen that prompts the player to select energy cards to discard
 * in order to retreat a Pokemon card. also handle input in order to display
 * the amount of energy cards already selected, and return whenever enough
 * energy cards have been selected or if the player declines to retreat. */
bool DisplayRetreatScreen(void)
{
	hTempRetreatCostCards[0] = 0xff;
	if (wEnergyCardsRequiredToRetreat == 0)
		return false; /* return if no energy cards are required at all */
	wNumRetreatEnergiesSelected = 0;
	CreateArenaOrBenchEnergyCardList(PLAY_AREA_ARENA);
	SortCardsInDuelTempListByID();
	wTempRetreatCostCardsPos = (u8)ADDR_hTempRetreatCostCards;
	DisplayEnergyDiscardScreen(PLAY_AREA_ARENA);
	wEnergyDiscardMenuDenominator = wEnergyCardsRequiredToRetreat;
	for (;;) {
		wEnergyDiscardMenuNumerator = wNumRetreatEnergiesSelected;
		if (HandleEnergyDiscardMenuInput())
			return true;
		LoadCardDataToBuffer2_FromDeckIndex(hTempCardIndex_ff98);
		/* append selected energy card to hTempRetreatCostCards */
		u8 c = wTempRetreatCostCardsPos++;
		*gbptr(0xff00 | c) = hTempCardIndex_ff98;
		/* accumulate selected energy card */
		c = 1;
		if (wLoadedCard2.Type == TYPE_ENERGY_DOUBLE_COLORLESS)
			c++;
		u8 a = (u8)(wNumRetreatEnergiesSelected + c);
		wNumRetreatEnergiesSelected = a;
		if (a >= wEnergyCardsRequiredToRetreat)
			break;
		/* not enough energies selected yet */
		RemoveCardFromDuelTempList(hTempCardIndex_ff98);
		DisplayEnergyDiscardMenu();
	}
	/* terminate hTempRetreatCostCards array with $ff */
	*gbptr(0xff00 | wTempRetreatCostCardsPos) = 0xff;
	return false;
}

/* triggered by selecting the "Attack" item in the duel menu */
static int DuelMenu_Attack(void)
{
	if (HandleCantAttackSubstatus() || CheckIfActiveCardParalyzedOrAsleep()) {
		DrawWideTextBox_WaitForInput(gTextID);
		return NEXT_PRINT_DUEL_MENU;
	}
	/* can_attack */
	wSelectedDuelSubMenuItem = 0;
	for (;;) {
		/* try_open_attack_menu */
		u8 n = PrintAndLoadAttacksToDuelTempList();
		if (n == 0) {
			DrawWideTextBox_WaitForInput(NoSelectableAttackText);
			return NEXT_PRINT_DUEL_MENU;
		}
		/* open_attack_menu */
		static const struct MenuParams AttackMenuParameters = {
			1, 13, 2, 2, SYM_CURSOR_R, SYM_SPACE, 0, TCG_MENU_ATTACKS,
		};
		InitializeMenuParameters(wSelectedDuelSubMenuItem, &AttackMenuParameters);
		wNumMenuItems = n;
		LoadCardDataToBuffer1_FromDeckIndex(TurnDuelistVars()[DUELVARS_ARENA_CARD]);
		/* wait_for_input (START to display the attack page is UI only) */
		HandleMenuInput();
		if (hCurMenuItem == MENU_CANCEL) /* was B pressed? */
			return NEXT_PRINT_DUEL_MENU;
		wSelectedDuelSubMenuItem = hCurMenuItem;
		if (CheckIfEnoughEnergiesToAttack()) {
			DrawWideTextBox_WaitForInput(NotEnoughEnergyCardsText);
			continue;
		}
		/* enough_energy */
		u8 i = (u8)(hCurMenuItem * 2);
		CopyAttackDataAndDamage_FromDeckIndex(wDuelTempList[i], wDuelTempList[(u8)(i + 1)]);
		if (HandleAmnesiaSubstatus()) {
			DrawWideTextBox_WaitForInput(gTextID);
			continue;
		}
		/* if player did something wrong in the practice duel, jump in order to restart turn */
		if (DoPracticeDuelAction(PRACTICEDUEL_VERIFY_PLAYER_TURN_ACTIONS))
			return NEXT_RESTART_PRACTICE_DUEL_TURN;
		if (UseAttackOrPokemonPower())
			return NEXT_DUEL_MAIN_INTERFACE;
		return NEXT_TURN_ENDED;
	}
}

/* .CheckAttackSlotEmptyOrPokemonPower: carry if the attack is a Pkmn Power or the slot is empty */
static bool CheckAttackSlotEmptyOrPokemonPower(const struct AtkData *atk)
{
	if ((atk->Name[0] | atk->Name[1]) == 0)
		return true;
	if ((atk->Category & (0xff ^ RESIDUAL)) == POKEMON_POWER)
		return true;
	return false;
}

/* given the card at hTempCardIndex_ff98, for each non-empty, non-Pokemon Power attack slot,
 * prints its information at lines 13 (first attack, if any), and 15 (second attack, if any)
 * also, copies zero, one, or both of the following to wDuelTempList, $ff terminated:
 *   if pokemon's first attack slot isn't empty or a Pokemon Power: <card_index>, 0
 *   if pokemon's second attack slot isn't empty or a Pokemon Power: <card_index>, 1
 * return the amount of non-empty, non-Pokemon Power attacks. */
u8 PrintAndLoadAttacksToDuelTempList(void)
{
	DrawWideTextBox();
	hTempCardIndex_ff98 = GetTurnDuelistVariable(DUELVARS_ARENA_CARD);
	LoadCardDataToBuffer1_FromDeckIndex(hTempCardIndex_ff98);
	u8 c = 0;
	u8 *hl = wDuelTempList;
	wCardPageNumber = 0;
	if (!CheckAttackSlotEmptyOrPokemonPower(&wLoadedCard1.Atk1)) {
		*hl++ = hTempCardIndex_ff98;
		*hl++ = 0;
		c++;
	}
	if (!CheckAttackSlotEmptyOrPokemonPower(&wLoadedCard1.Atk2)) {
		*hl++ = hTempCardIndex_ff98;
		*hl++ = 1;
		c++;
	}
	/* (the original does not write a terminator; the comment above is inaccurate) */
	tcg_ui_screen(TCG_SCREEN_OTHER, c, 0);
	return c;
}

/* check if the arena pokemon card has enough energy attached to it
 * in order to use the selected attack.
 * returns: carry if not enough energy, nc if enough energy. */
bool CheckIfEnoughEnergiesToAttack(void)
{
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	HandleEnergyBurn();
	u8 i = (u8)(hCurMenuItem * 2);
	return CheckIfEnoughEnergiesForGivenAttack(wDuelTempList[i], wDuelTempList[(u8)(i + 1)]);
}

/* given the amount of energies of a specific type required for an attack in the
 * lower nybble of a, test if the pokemon card has enough energies of that type
 * (*attached) to use the attack. Return carry if not enough energy. */
static bool CheckIfEnoughEnergiesOfType(u8 a, u8 attached)
{
	a &= 0xf;
	wAttachedEnergiesAccum += a; /* accumulate the amount of energies required */
	if (a == 0)
		return false; /* no energies of this type are required */
	if (a <= attached)
		return false;
	return true;
}

/* check if a pokemon card has enough energy attached to it in order to use an attack
 * input:
 *   d = deck index of card (0 to 59)
 *   e = attack index (SECOND_ATTACK or FIRST_ATTACK_OR_PKMN_POWER)
 *   wAttachedEnergies and wTotalAttachedEnergies
 * returns: carry if not enough energy, nc if enough energy. */
bool CheckIfEnoughEnergiesForGivenAttack(u8 d, u8 e)
{
	LoadCardDataToBuffer1_FromDeckIndex(d);
	const struct AtkData *atk = (e == 0) ? &wLoadedCard1.Atk1 : &wLoadedCard1.Atk2;
	if ((atk->Name[0] | atk->Name[1]) == 0)
		return true;
	if (atk->Category == POKEMON_POWER)
		return true;
	wAttachedEnergiesAccum = 0;
	const u8 *de = atk->EnergyCost;
	const u8 *hl = wAttachedEnergies;
	for (u8 c = NUM_COLORED_TYPES / 2; c; c--) {
		if (CheckIfEnoughEnergiesOfType((u8)(*de >> 4), *hl++))
			return true;
		if (CheckIfEnoughEnergiesOfType(*de, *hl++))
			return true;
		de++;
	}
	u8 b = (*de >> 4) & 0xf; /* colorless energy */
	u8 a = (u8)(wTotalAttachedEnergies - wAttachedEnergiesAccum);
	if (a < b)
		return true;
	return false;
}

/* return carry and the corresponding text in gTextID if the turn holder's
 * arena Pokemon card is paralyzed or asleep. */
bool CheckIfActiveCardParalyzedOrAsleep(void)
{
	u8 a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS) & CNF_SLP_PRZ;
	if (a == PARALYZED) {
		gTextID = UnableDueToParalysisText;
		return true;
	}
	if (a == ASLEEP) {
		gTextID = UnableDueToSleepText;
		return true;
	}
	return false;
}

/* ---------------------------------------------------------------- duel setup */

static bool ChooseInitialArenaAndBenchPokemon(void);
static bool ShuffleDeckAndDrawSevenCards(u8 *a);
static void InitTurnDuelistPrizes(void);
static void InitializeDuelVariables(void);

/* handles the initial duel actions:
 * - drawing starting hand and placing the Basic Pokemon cards
 * - placing the appropriate number of prize cards
 * - tossing coin to determine first player to go */
static bool HandleDuelSetup(void)
{
	TRACE_FN();
restart:
	/* init variables and shuffle cards */
	InitializeDuelVariables();
	SwapTurn();
	InitializeDuelVariables();
	SwapTurn();
	PlayShuffleAndDrawCardsAnimation_BothDuelists();
	u8 a;
	ShuffleDeckAndDrawSevenCards(&a);
	hTemp_ffa0 = a;
	SwapTurn();
	ShuffleDeckAndDrawSevenCards(&a);
	SwapTurn();
	u8 c = a;
	/* check if any Basic Pokémon cards were drawn */
	u8 b = hTemp_ffa0;
	if (!(b & c)) {
		if (!(b | c)) {
			/* neither_drew_basic_pkmn */
			DrawWideTextBox_WaitForInput(NeitherPlayerHasBasicPkmnText);
			DisplayNoBasicPokemonInHandScreen();
			InitializeDuelVariables();
			SwapTurn();
			DisplayNoBasicPokemonInHandScreen();
			InitializeDuelVariables();
			SwapTurn();
			PrintReturnCardsToDeckDrawAgain();
			goto restart;
		}
		bool opp = b != 0;
		if (opp)
			SwapTurn();
		do {
			DisplayNoBasicPokemonInHandScreenAndText();
			InitializeDuelVariables();
			PlayShuffleAndDrawCardsAnimation_TurnDuelist();
		} while (ShuffleDeckAndDrawSevenCards(NULL));
		if (opp)
			SwapTurn();
	}
	/* hand_cards_ok */
	u8 turn = hWhoseTurn;
	hWhoseTurn = PLAYER_TURN;
	ChooseInitialArenaAndBenchPokemon();
	SwapTurn();
	bool err = ChooseInitialArenaAndBenchPokemon();
	SwapTurn();
	if (err) {
		hWhoseTurn = turn;
		return true;
	}
	DrawPlayAreaToPlacePrizeCards();
	DrawWideTextBox_WaitForInput(PlacingThePrizesText);
	ExchangeRNG();
	LoadTxRam3(wDuelInitialPrizes);
	DrawWideTextBox_PrintText(PleasePlacePrizesText);
	HandleDuelSetup_PlacePrizes();
	WaitForWideTextBoxInput();
	hWhoseTurn = turn;
	InitTurnDuelistPrizes();
	SwapTurn();
	InitTurnDuelistPrizes();
	SwapTurn();
	EmptyScreen();
	DrawDuelBoxMessage(BOXMSG_COIN_TOSS);
	DrawWideTextBox_WaitForInput(CoinTossToDecideWhoPlaysFirstText);
	u16 text;
	if (hWhoseTurn == PLAYER_TURN) {
		/* player flips coin */
		CopyPlayerNameToDefaultText();
		LoadTxRam2(0);
		text = YouPlayFirstText;
		if (!TossCoin(IfHeadsDuelistPlaysFirstText)) {
			SwapTurn();
			text = YouPlaySecondText;
		}
	} else {
		/* opp flips coin */
		CopyOpponentNameToDefaultText();
		LoadTxRam2(0);
		text = YouPlaySecondText;
		if (!TossCoin(IfHeadsDuelistPlaysFirstText)) {
			SwapTurn();
			text = YouPlayFirstText;
		}
	}
	DrawWideTextBox_WaitForInput(text);
	ExchangeRNG();
	return false;
}

/* have the turn duelist place, at the beginning of the duel, the active Pokemon
 * and 0 more bench Pokemon, all of which must be basic Pokemon cards.
 * also transmits the turn holder's duelvars to the other duelist in a link duel.
 * called twice, once for each duelist. */
static bool ChooseInitialArenaAndBenchPokemon(void)
{
	TRACE_FN();
	u8 type = GetTurnDuelistVariable(DUELVARS_DUELIST_TYPE);
	if (type != DUELIST_TYPE_PLAYER) {
		if (type == DUELIST_TYPE_LINK_OPP)
			tcg_fatal("link duels are not supported");
		/* AI opponent's turn */
		AIDoAction_StartDuel();
		TurnDuelistVars()[DUELVARS_DUELIST_TYPE] = type;
		return false;
	}
	/* player's turn (either AI or link duel)
	 * prompt (force) the player to choose a basic Pokemon card to place in the arena */
	u8 saved_decider = gUIDecider;
	gUIDecider = hWhoseTurn;
	EmptyScreen();
	DrawDuelBoxMessage(BOXMSG_ARENA_POKEMON);
	DrawWideTextBox_WaitForInput(ChooseBasicPkmnToPlaceInArenaText);
	DoPracticeDuelAction(PRACTICEDUEL_DRAW_SEVEN_CARDS);
	for (;;) {
		if (DisplayPlaceInitialPokemonCardsScreen(0, PleaseChooseAnActivePokemonText))
			continue;
		LoadCardDataToBuffer1_FromDeckIndex(hTempCardIndex_ff98);
		if (DoPracticeDuelAction(PRACTICEDUEL_PLAY_GOLDEEN))
			continue;
		break;
	}
	PutHandPokemonCardInPlayArea(hTempCardIndex_ff98, NULL);
	DisplayCardDetailScreen(hTempCardIndex_ff98, PlacedInTheArenaText);
	/* after choosing the active Pokemon, let the player place 0 or more basic Pokemon
	 * cards in the bench. loop until the player decides to stop placing Pokemon cards. */
	EmptyScreen();
	DrawDuelBoxMessage(BOXMSG_BENCH_POKEMON);
	PrintScrollableText_NoTextBoxLabel(ChooseUpTo5BasicPkmnToPlaceOnBenchText);
	DoPracticeDuelAction(PRACTICEDUEL_PUT_STARYU_IN_BENCH);
	for (;;) {
		if (DisplayPlaceInitialPokemonCardsScreen(TRUE, ChooseYourBenchPokemonText)) {
			/* bench_done */
			if (DoPracticeDuelAction(PRACTICEDUEL_VERIFY_INITIAL_PLAY))
				continue;
			break;
		}
		if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) >= MAX_PLAY_AREA_POKEMON) {
			DrawWideTextBox_WaitForInput(NoSpaceOnTheBenchText);
			continue;
		}
		PutHandPokemonCardInPlayArea(hTempCardIndex_ff98, NULL);
		DisplayCardDetailScreen(hTempCardIndex_ff98, PlacedOnTheBenchText);
		DoPracticeDuelAction(PRACTICEDUEL_DONE_PUTTING_ON_BENCH);
	}
	gUIDecider = saved_decider;
	return false;
}

/* the turn duelist shuffles the deck unless it's a practice duel, then draws 7 cards
 * returns $00 in a and carry if no basic Pokemon cards are drawn, and $01 in a otherwise */
static bool ShuffleDeckAndDrawSevenCards(u8 *out)
{
	TRACE_FN();
	InitializeDuelVariables();
	if (wDuelType != DUELTYPE_PRACTICE) {
		ShuffleDeck();
		ShuffleDeck();
	}
	for (u8 b = 7; b; b--) {
		u8 card;
		DrawCardFromDeck(&card);
		AddCardToHand(card);
	}
	u8 b = 0;
	for (u8 i = 0; i < 7; i++) {
		LoadCardDataToBuffer1_FromDeckIndex(TurnDuelistVars()[(u8)(DUELVARS_HAND + i)]);
		b |= IsLoadedCard1BasicPokemon_SkipMysteriousFossilClefairyDoll(NULL);
	}
	if (out)
		*out = b;
	return b == 0;
}

/* return nc if the card at wLoadedCard1 is a basic Pokemon card
 * MYSTERIOUS_FOSSIL and CLEFAIRY_DOLL do count as basic Pokemon cards.
 * a = 1 if basic (z set for regular basic Pokemon, nz for the trainer cards), 0 otherwise */
u8 IsLoadedCard1BasicPokemon(bool *carry)
{
	if (wLoadedCard1.ID == MYSTERIOUS_FOSSIL || wLoadedCard1.ID == CLEFAIRY_DOLL) {
		if (carry)
			*carry = false;
		return 1;
	}
	return IsLoadedCard1BasicPokemon_SkipMysteriousFossilClefairyDoll(carry);
}

/* MYSTERIOUS_FOSSIL and CLEFAIRY_DOLL do NOT count unless already checked */
u8 IsLoadedCard1BasicPokemon_SkipMysteriousFossilClefairyDoll(bool *carry)
{
	if (wLoadedCard1.Type >= TYPE_ENERGY || wLoadedCard1.Stage != 0) {
		if (carry)
			*carry = true;
		return 0;
	}
	if (carry)
		*carry = false;
	return 1;
}

/* display the screen that prompts the player to choose a Pokemon card to
 * place in the arena or in the bench at the beginning of the duel.
 * input: a = 0 -> arena, a = 1 -> bench
 * return carry if no card was placed (only allowed for bench) */
bool DisplayPlaceInitialPokemonCardsScreen(u8 a, u16 text)
{
	wPlacingInitialBenchPokemon = a;
	CreateHandCardList(NULL);
	InitAndDrawCardListScreenLayout();
	SetCardListInfoBoxText(text);
	wCardListItemSelectionMenuType = PLAY_CHECK;
	bool c;
	for (;;) {
		if (DisplayCardList(NULL)) {
			/* attempted to exit screen */
			if (wPlacingInitialBenchPokemon == 0)
				continue; /* player is forced to place a Pokemon card in the arena */
			/* in the bench, however, we can get away without placing anything */
			c = true;
			break;
		}
		LoadCardDataToBuffer1_FromDeckIndex(hTempCardIndex_ff98);
		IsLoadedCard1BasicPokemon(&c);
		if (!c)
			break;
		/* invalid card selected, tell the player and go back */
		DrawWideTextBox_WaitForInput(YouCannotSelectThisCardText);
		DrawCardListScreenLayout();
	}
	/* valid basic Pokemon card selected, or no card selected (bench only) */
	if (wSortCardListByID)
		SortHandCardsByID();
	return c;
}

void DisplayNoBasicPokemonInHandScreenAndText(void)
{
	DrawWideTextBox_WaitForInput(ThereAreNoBasicPokemonInHand);
	DisplayNoBasicPokemonInHandScreen();
	PrintReturnCardsToDeckDrawAgain();
}

/* prints ReturnCardsToDeckAndDrawAgainText in a textbox and calls ExchangeRNG */
void PrintReturnCardsToDeckDrawAgain(void)
{
	DrawWideTextBox_WaitForInput(ReturnCardsToDeckAndDrawAgainText);
	ExchangeRNG();
}

/* ---------------------------------------------------------------- prizes */

/* has turn duelist take amount of prizes that are in wNumberPrizeCardsToTake
 * returns carry if all prize cards were taken */
bool TurnDuelistTakePrizes(void)
{
	TRACE_FN();
	FinishQueuedAnimations();
	LoadTxRam3(wNumberPrizeCardsToTake);
	u8 type = GetTurnDuelistVariable(DUELVARS_DUELIST_TYPE);
	if (type == DUELIST_TYPE_PLAYER) {
		u8 saved = gUIDecider;
		gUIDecider = hWhoseTurn;
		DrawWideTextBox_WaitForInput(WillDrawNPrizesText);
		SelectPrizeCards(wNumberPrizeCardsToTake);
		gUIDecider = saved;
		SerialSend8Bytes();
	} else {
		DrawYourOrOppPlayAreaScreen_Bank0(PLAYER_TURN);
		DrawWideTextBox_PrintText(WillDrawNPrizesText);
		wTempNumRemainingPrizeCards = CountPrizes();
		if (type == DUELIST_TYPE_LINK_OPP)
			tcg_fatal("link duels are not supported");
		AIDoAction_TakePrize();
		WaitFrames(60);
		if (wTempNumRemainingPrizeCards < wNumberPrizeCardsToTake)
			LoadTxRam3(wTempNumRemainingPrizeCards);
		Func_82b6();
		DrawWideTextBox_WaitForInput(DrewNPrizesText);
	}
	/* return_has_prizes */
	ExchangeRNG();
	return GetTurnDuelistVariable(DUELVARS_PRIZES) == 0;
}

/* ---------------------------------------------------------------- Pkmn Power and retreat */

/* the duel menu's Pkmn Power screen; returns carry if cancelled, else the card in hTemp_ffa0 */
bool DisplayPlayAreaScreenToUsePkmnPower(void)
{
	wSelectedDuelSubMenuItem = 0;
	for (;;) {
		DisplayPlayAreaScreenToUsePkmnPower_DrawScreen();
		static const struct MenuParams params = {
			0, 0, 3, 6, SYM_CURSOR_R, SYM_SPACE, 0, TCG_MENU_PLAY_AREA,
		};
		InitializeMenuParameters(wSelectedDuelSubMenuItem, &params);
		wNumMenuItems = wNumPlayAreaItems;
		for (;;) {
			HandleMenuInput();
			hTempPlayAreaLocation_ff9d = hCurMenuItem;
			wHUDEnergyAndHPBarsX = hCurMenuItem;
			if (hCurMenuItem == MENU_CANCEL)
				return true;
			wSelectedDuelSubMenuItem = hCurMenuItem;
			u8 i = (u8)(hCurMenuItem * 2);
			if (wDuelTempList[(u8)(i + 1)] != POKEMON_POWER)
				continue;
			break;
		}
		u8 i = (u8)(hCurMenuItem * 2);
		hTempCardIndex_ff98 = wDuelTempList[i];
		CopyAttackDataAndDamage_FromDeckIndex(hTempCardIndex_ff98, FIRST_ATTACK_OR_PKMN_POWER);
		DisplayUsePokemonPowerScreen();
		if (TryExecuteEffectCommandFunction(EFFECTCMDTYPE_INITIAL_EFFECT_1)) {
			DrawWideTextBox_WaitForInput(PokemonPowerSelectNotRequiredText);
			continue;
		}
		if (YesOrNoMenuWithText(UseThisPokemonPowerText))
			continue;
		hTemp_ffa0 = hTempCardIndex_ff98;
		return false;
	}
}

/* moves the cards loaded by deck index at hTempRetreatCostCards to the discard pile */
void DiscardRetreatCostCards(void)
{
	for (u8 *hl = hTempRetreatCostCards; *hl != 0xff; hl++)
		PutCardInDiscardPile(*hl);
}

/* moves the discard pile cards that were loaded to hTempRetreatCostCards back to the active Pokemon.
 * this exists because they will be discarded again during the call to AttemptRetreat, so
 * it prevents the energy cards from being discarded twice. */
void ReturnRetreatCostCardsToArena(void)
{
	for (u8 *hl = hTempRetreatCostCards; *hl != 0xff; hl++) {
		u8 a = MoveDiscardPileCardToHand(*hl);
		AddCardToHand(a);
		PutHandCardInPlayArea(a, PLAY_AREA_ARENA);
	}
}

/* discard retreat cost energy cards and attempt retreat of the arena card.
 * return carry if unable to retreat this turn due to unsuccessful confusion check
 * if successful, the retreated card is replaced with a bench Pokemon card */
bool AttemptRetreat(void)
{
	TRACE_FN();
	DiscardRetreatCostCards();
	if ((hTemp_ffa0 & CNF_SLP_PRZ) == CONFUSED) {
		if (!TossCoin(ConfusionCheckRetreatText)) {
			wConfusionRetreatCheckWasUnsuccessful = TRUE;
			return true;
		}
	}
	SwapArenaWithBenchPokemon(hTempPlayAreaLocation_ffa1);
	wConfusionRetreatCheckWasUnsuccessful = FALSE;
	return false;
}

/* ---------------------------------------------------------------- AI and link opponent actions */

/* related to AI taking their turn in a duel; called multiple times during one AI turn.
 * each call results in the execution of an OppActionTable function.
 * returns carry if the turn ended */
bool AIMakeDecision(u8 action)
{
	TRACE_FN();
	hOppActionTableIndex = action;
	u8 skip = wSkipDuelistIsThinkingDelay;
	wSkipDuelistIsThinkingDelay = 0;
	if (!skip) {
		/* delay_loop: the original waits until wVBlankCounter reaches 60 */
		WaitFrames((u8)(wVBlankCounter < 60 ? 60 - wVBlankCounter : 0));
		wVBlankCounter = 60;
	}
	wOpponentTurnEnded = 0;
	OppActionTable(hOppActionTableIndex);
	if (wDuelFinished || wOpponentTurnEnded)
		return true;
	if (wSkipDuelistIsThinkingDelay)
		return false;
	wVBlankCounter = 0;
	DrawWideTextBox_PrintTextNoDelay(DuelistIsThinkingText);
	return false;
}

static void OppAction_DrawCard(void)
{
	u8 card;
	if (!DrawCardFromDeck(&card))
		AddCardToHand(card);
}

static void OppAction_FinishTurnWithoutAttacking(void)
{
	DrawDuelMainScene();
	ClearNonTurnTemporaryDuelvars();
	DrawWideTextBox_WaitForInput(FinishedTurnWithoutAttackingText);
	wOpponentTurnEnded = 1;
}

/* attach an energy card from hand to the arena or a benched Pokemon */
static void OppAction_PlayEnergyCard(void)
{
	hTempPlayAreaLocation_ff9d = hTempPlayAreaLocation_ffa1;
	hTempCardIndex_ff98 = hTemp_ffa0;
	PutHandCardInPlayArea(hTemp_ffa0, hTempPlayAreaLocation_ffa1);
	LoadCardDataToBuffer1_FromDeckIndex(hTemp_ffa0);
	DrawLargePictureOfCard();
	PrintAttachedEnergyToPokemon();
	wAlreadyPlayedEnergy = TRUE;
	DrawDuelMainScene();
}

/* evolve a Pokemon card in the arena or in the bench */
static void OppAction_EvolvePokemonCard(void)
{
	hTempPlayAreaLocation_ff9d = hTempPlayAreaLocation_ffa1;
	hTempCardIndex_ff98 = hTemp_ffa0;
	LoadCardDataToBuffer1_FromDeckIndex(hTemp_ffa0);
	DrawLargePictureOfCard();
	EvolvePokemonCardIfPossible();
	PrintPokemonEvolvedIntoPokemon();
	ProcessPlayedPokemonCard();
	DrawDuelMainScene();
}

/* place a basic Pokemon card from hand in the bench */
static void OppAction_PlayBasicPokemonCard(void)
{
	hTempCardIndex_ff98 = hTemp_ffa0;
	u8 loc;
	PutHandPokemonCardInPlayArea(hTemp_ffa0, &loc);
	hTempPlayAreaLocation_ff9d = loc;
	TurnDuelistVars()[(u8)(loc + DUELVARS_ARENA_CARD_STAGE)] = BASIC;
	DisplayCardDetailScreen(hTemp_ffa0, PlacedOnTheBenchText);
	ProcessPlayedPokemonCard();
	DrawDuelMainScene();
}

/* attempt the retreat of the active Pokemon card
 * if successful, discard the required energy cards for retreat and
 * swap the retreated card with a Pokemon card from the bench */
static void OppAction_AttemptRetreat(void)
{
	u8 arena = GetTurnDuelistVariable(DUELVARS_ARENA_CARD);
	u16 text = RetreatWasUnsuccessfulText;
	if (!AttemptRetreat()) {
		wDuelDisplayedScreen = 0;
		text = RetreatedToTheBenchText;
	}
	DrawDuelMainScene();
	LoadCardNameToTxRam2(arena);
	DrawWideTextBox_WaitForInput(text);
}

/* play trainer card from hand */
static void OppAction_PlayTrainerCard(void)
{
	LoadNonPokemonCardEffectCommands();
	DisplayUsedTrainerCardDetailScreen();
	PrintUsedTrainerCardDescription();
	ExchangeRNG();
	wSkipDuelistIsThinkingDelay = 1;
}

/* execute the effect commands of the trainer card that is being played
 * used only for Trainer cards, as a continuation of OppAction_PlayTrainerCard */
static void OppAction_ExecuteTrainerCardEffectCommands(void)
{
	TryExecuteEffectCommandFunction(EFFECTCMDTYPE_DISCARD_ENERGY);
	TryExecuteEffectCommandFunction(EFFECTCMDTYPE_BEFORE_DAMAGE);
	DrawDuelMainScene();
	MoveHandCardToDiscardPile(hTempCardIndex_ff9f);
	ExchangeRNG();
	DrawDuelMainScene();
}

/* begin the execution of an attack and handle the attack being
 * possibly unsuccessful due to Sand Attack or Smokescreen */
static void OppAction_BeginUseAttack(void)
{
	CopyAttackDataAndDamage_FromDeckIndex(hTempCardIndex_ff9f, hTemp_ffa0);
	UpdateArenaCardIDsAndClearTwoTurnDuelVars();
	wSkipDuelistIsThinkingDelay = 1;
	if (!CheckSandAttackOrSmokescreenSubstatus() &&
	    (GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS) & CNF_SLP_PRZ) != CONFUSED) {
		ExchangeRNG();
		return;
	}
	/* we make it here is attacker is affected by Sand Attack, Smokescreen, or confusion */
	DrawDuelMainScene();
	PrintPokemonsAttackText();
	WaitForWideTextBoxInput();
	ExchangeRNG();
	if (!HandleSandAttackOrSmokescreenSubstatus())
		return; /* return if attack is successful (won the coin toss) */
	ClearNonTurnTemporaryDuelvars();
	/* end the turn if the attack fails */
	wOpponentTurnEnded = 1;
}

/* display the attack used by the opponent, and handle
 * EFFECTCMDTYPE_DISCARD_ENERGY and confusion damage to self */
static void OppAction_UseAttack(void)
{
	TryExecuteEffectCommandFunction(EFFECTCMDTYPE_DISCARD_ENERGY);
	if (CheckSelfConfusionDamage()) {
		HandleConfusionDamageToSelf();
		/* end the turn if dealing damage to self due to confusion */
		wOpponentTurnEnded = 1;
		return;
	}
	DisplayOpponentUsedAttackScreen();
	PrintPokemonsAttackText();
	WaitForWideTextBoxInput();
	ExchangeRNG();
	wSkipDuelistIsThinkingDelay = 1;
}

static void OppAction_PlayAttackAnimationDealAttackDamage(void)
{
	PlayAttackAnimation_DealAttackDamage();
	wOpponentTurnEnded = 1;
}

/* force the player to switch the active Pokemon with a benched Pokemon */
static void OppAction_ForceSwitchActive(void)
{
	DrawWideTextBox_WaitForInput(SelectPkmnOnBenchToSwitchWithActiveText);
	SwapTurn();
	HasAlivePokemonInBench(NULL);
	wPlayAreaSelectAction = 1;
	u8 saved = gUIDecider;
	gUIDecider = hWhoseTurn;
	while (OpenPlayAreaScreenForSelection())
		;
	gUIDecider = saved;
	SwapTurn();
	tcg_fatal("link duels are not supported"); /* SerialSendByte */
}

static void OppAction_UsePokemonPower(void)
{
	CopyAttackDataAndDamage_FromDeckIndex(hTempCardIndex_ff9f, FIRST_ATTACK_OR_PKMN_POWER);
	hTempPlayAreaLocation_ff9d = hTemp_ffa0;
	DisplayUsePokemonPowerScreen();
	LoadCardNameToTxRam2(hTempCardIndex_ff9f);
	wTxRam2_b[0] = wLoadedAttack.Name[0];
	wTxRam2_b[1] = wLoadedAttack.Name[1];
	DrawWideTextBox_WaitForInput(WillUseThePokemonPowerText);
	ExchangeRNG();
	wSkipDuelistIsThinkingDelay = 1;
}

/* execute the EFFECTCMDTYPE_BEFORE_DAMAGE command of the used Pokemon Power */
static void OppAction_ExecutePokemonPowerEffect(void)
{
	ResetAttackAnimationIsPlaying();
	TryExecuteEffectCommandFunction(EFFECTCMDTYPE_BEFORE_DAMAGE);
	wSkipDuelistIsThinkingDelay = 1;
}

/* execute the EFFECTCMDTYPE_AFTER_DAMAGE command of the used Pokemon Power */
static void OppAction_6b15(void)
{
	TryExecuteEffectCommandFunction(EFFECTCMDTYPE_AFTER_DAMAGE);
	wSkipDuelistIsThinkingDelay = 1;
}

static void OppAction_TossCoinATimes(void)
{
	SerialRecv8Bytes();
}

static void OppAction_6b30(void)
{
	u8 turn = hWhoseTurn;
	hWhoseTurn = hTemp_ffa0;
	PlayDeckShuffleAnimation();
	hWhoseTurn = turn;
}

static void OppAction_UseMetronomeAttack(void)
{
	SerialRecv8Bytes();
}

/* actions for the opponent's turn
 * on a link duel, this is referenced by DoLinkOpponentTurn in a loop (on each opponent's HandleTurn)
 * on a non-link duel (vs AI opponent), this is referenced by AIMakeDecision */
void OppActionTable(u8 action)
{
	TRACE_FN();
	switch (action) {
	case OPPACTION_PLAY_BASIC_PKMN: OppAction_PlayBasicPokemonCard(); break;
	case OPPACTION_EVOLVE_PKMN: OppAction_EvolvePokemonCard(); break;
	case OPPACTION_PLAY_ENERGY: OppAction_PlayEnergyCard(); break;
	case OPPACTION_ATTEMPT_RETREAT: OppAction_AttemptRetreat(); break;
	case OPPACTION_FINISH_NO_ATTACK: OppAction_FinishTurnWithoutAttacking(); break;
	case OPPACTION_PLAY_TRAINER: OppAction_PlayTrainerCard(); break;
	case OPPACTION_EXECUTE_TRAINER_EFFECTS: OppAction_ExecuteTrainerCardEffectCommands(); break;
	case OPPACTION_BEGIN_ATTACK: OppAction_BeginUseAttack(); break;
	case OPPACTION_USE_ATTACK: OppAction_UseAttack(); break;
	case OPPACTION_ATTACK_ANIM_AND_DAMAGE: OppAction_PlayAttackAnimationDealAttackDamage(); break;
	case OPPACTION_DRAW_CARD: OppAction_DrawCard(); break;
	case OPPACTION_USE_PKMN_POWER: OppAction_UsePokemonPower(); break;
	case OPPACTION_EXECUTE_PKMN_POWER_EFFECT: OppAction_ExecutePokemonPowerEffect(); break;
	case OPPACTION_FORCE_SWITCH_ACTIVE: OppAction_ForceSwitchActive(); break;
	case OPPACTION_NO_ACTION_0F:
	case OPPACTION_NO_ACTION_10:
	case OPPACTION_NO_ACTION_13: break;
	case OPPACTION_TOSS_COIN_A_TIMES: OppAction_TossCoinATimes(); break;
	case OPPACTION_6B30: OppAction_6b30(); break;
	case OPPACTION_USE_METRONOME_ATTACK: OppAction_UseMetronomeAttack(); break;
	case OPPACTION_6B15: OppAction_6b15(); break;
	case OPPACTION_DUEL_MAIN_SCENE: DrawDuelMainScene(); break;
	default: tcg_fatal("DuelTransmissionError: invalid OppAction");
	}
}

/* load the text ID of the card name with deck index given to TxRam2
 * also loads the card to wLoadedCard1 */
void LoadCardNameToTxRam2(u8 card)
{
	LoadCardDataToBuffer1_FromDeckIndex(card);
	wTxRam2[0] = wLoadedCard1.Name[0];
	wTxRam2[1] = wLoadedCard1.Name[1];
}

void LoadCardNameToTxRam2_b(u8 card)
{
	LoadCardDataToBuffer1_FromDeckIndex(card);
	wTxRam2_b[0] = wLoadedCard1.Name[0];
	wTxRam2_b[1] = wLoadedCard1.Name[1];
}

void Func_6ba2(u16 text)
{
	DrawWideTextBox_PrintText(text);
	if (wDuelistType == DUELIST_TYPE_LINK_OPP)
		return;
	WaitForWideTextBoxInput();
}

/* ---------------------------------------------------------------- between turns */

static void DiscardAttachedPlusPowers(void);
static void DiscardAttachedDefenders(void);
static bool IsArenaPokemonAsleepOrPoisoned(u8 *a);
static void HandleSleepCheck(u8 *hl);
static bool HandlePoisonDamage(u8 *hl);

/* apply and/or refresh status conditions and other events that trigger between turns */
static void HandleBetweenTurnsEvents(void)
{
	TRACE_FN();
	u8 a;
	if (!IsArenaPokemonAsleepOrPoisoned(&a) && a != PARALYZED) {
		SwapTurn();
		bool c = IsArenaPokemonAsleepOrPoisoned(&a);
		SwapTurn();
		if (!c) {
			DiscardAttachedPlusPowers();
			SwapTurn();
			DiscardAttachedDefenders();
			SwapTurn();
			return;
		}
	}
	/* something_to_handle: either:
	 * 1. turn holder's arena Pokemon is paralyzed, asleep, poisoned or double poisoned
	 * 2. non-turn holder's arena Pokemon is asleep, poisoned or double poisoned */
	ResetAnimationQueue();
	ZeroObjectPositionsAndToggleOAMCopy();
	EmptyScreen();
	DrawDuelBoxMessage(BOXMSG_BETWEEN_TURNS);
	DrawWideTextBox_WaitForInput(BetweenTurnsText);

	wTempNonTurnDuelistCardID = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	u8 *hl = &TurnDuelistVars()[DUELVARS_ARENA_CARD_STATUS];
	if (*hl) {
		/* has status condition */
		if (!HandlePoisonDamage(hl)) {
			HandleSleepCheck(hl);
			if ((*hl & CNF_SLP_PRZ) == PARALYZED) {
				/* heal paralysis */
				*hl &= DOUBLE_POISONED;
				RedrawTurnDuelistsMainSceneOrDuelHUD();
				PrintCardNameFromCardIDInTextBox(IsCuredOfParalysisText);
				PlayBetweenTurnsAnimation(DUEL_ANIM_HEAL);
				WaitForWideTextBoxInput();
			}
		}
	}
	/* discard_pluspower */
	DiscardAttachedPlusPowers();
	SwapTurn();
	wTempNonTurnDuelistCardID = GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	hl = &TurnDuelistVars()[DUELVARS_ARENA_CARD_STATUS];
	if (*hl) {
		if (!HandlePoisonDamage(hl))
			HandleSleepCheck(hl);
	}
	DiscardAttachedDefenders();
	SwapTurn();
	HandleBetweenTurnKnockOuts();
}

/* discard any PLUSPOWER attached to the turn holder's arena and/or bench Pokemon */
static void DiscardAttachedPlusPowers(void)
{
	u8 *vars = TurnDuelistVars();
	for (u8 i = 0; i < MAX_PLAY_AREA_POKEMON; i++)
		vars[(u8)(DUELVARS_ARENA_CARD_ATTACHED_PLUSPOWER + i)] = 0;
	MoveCardToDiscardPileIfInPlayArea(hWhoseTurn, PLUSPOWER);
}

/* discard any DEFENDER attached to the turn holder's arena and/or bench Pokemon */
static void DiscardAttachedDefenders(void)
{
	u8 *vars = TurnDuelistVars();
	for (u8 i = 0; i < MAX_PLAY_AREA_POKEMON; i++)
		vars[(u8)(DUELVARS_ARENA_CARD_ATTACHED_DEFENDER + i)] = 0;
	MoveCardToDiscardPileIfInPlayArea(hWhoseTurn, DEFENDER);
}

/* return carry if the turn holder's arena Pokemon card is asleep, poisoned, or double poisoned.
 * also, if confused, paralyzed, or asleep, return the status condition in a. */
static bool IsArenaPokemonAsleepOrPoisoned(u8 *a)
{
	u8 s = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS);
	*a = s;
	if (s == 0)
		return false;
	/* note that POISONED | DOUBLE_POISONED is the same as just DOUBLE_POISONED ($c0)
	 * poison status masking is normally done with PSN_DBLPSN ($f0) */
	if (s & (POISONED | DOUBLE_POISONED)) {
		*a = s & (POISONED | DOUBLE_POISONED);
		return true;
	}
	*a = s & CNF_SLP_PRZ;
	return *a == ASLEEP;
}

/* prints the name of the card at wTempNonTurnDuelistCardID in a text box */
void PrintCardNameFromCardIDInTextBox(u16 text)
{
	LoadCardDataToBuffer1_FromCardID(wTempNonTurnDuelistCardID);
	LoadTxRam2(rd16(wLoadedCard1.Name));
	DrawWideTextBox_PrintText(text);
}

/* handles the sleep check for the NonTurn Duelist
 * heals sleep status if coin is heads, else it plays sleeping animation */
static void HandleSleepCheck(u8 *hl)
{
	if ((*hl & CNF_SLP_PRZ) != ASLEEP)
		return; /* quit if not asleep */
	LoadCardDataToBuffer1_FromCardID(wTempNonTurnDuelistCardID);
	CopyCardNameAndLevel(18);
	wTxRam2[0] = 0;
	wTxRam2[1] = 0;
	u8 anim = DUEL_ANIM_SLEEP;
	u16 text = IsStillAsleepText;
	if (TossCoin(PokemonsSleepCheckText)) {
		/* coin toss was heads, cure sleep status */
		*hl &= DOUBLE_POISONED;
		anim = DUEL_ANIM_HEAL;
		text = IsCuredOfSleepText;
	}
	RedrawTurnDuelistsMainSceneOrDuelHUD();
	PrintCardNameFromCardIDInTextBox(text);
	PlayBetweenTurnsAnimation(anim);
	WaitForWideTextBoxInput();
}

/* returns carry if the Pokemon was knocked out by the poison damage */
static bool HandlePoisonDamage(u8 *status)
{
	if (!(*status & (1 << POISONED_F)))
		return false; /* quit if not poisoned */
	/* load damage and text according to normal/double poison */
	u8 a = PSN_DAMAGE;
	u16 text = Received10DamageDueToPoisonText;
	if (*status & (1 << DOUBLE_POISONED_F)) {
		a = DBLPSN_DAMAGE;
		text = Received20DamageDueToPoisonText;
	}
	wDuelAnimDamage[0] = a;
	wDuelAnimDamage[1] = 0;
	RedrawTurnDuelistsMainSceneOrDuelHUD();
	PrintCardNameFromCardIDInTextBox(text);
	/* play animation */
	PlayBetweenTurnsAnimation(DUEL_ANIM_POISON);
	/* deal poison damage */
	u8 *hl = &TurnDuelistVars()[DUELVARS_ARENA_CARD_HP];
	SubtractHP(hl, a);
	tcg_ui_event(TCG_EV_DAMAGE, hWhoseTurn, PLAY_AREA_ARENA, a, 0, 0);
	PlayBetweenTurnsAnimation(DUEL_ANIM_DAMAGE_HUD);
	bool c = PrintKnockedOutIfHLZero(hl);
	WaitForWideTextBoxInput();
	return c;
}

/* given the deck index of a turn holder's card and a pointer to the wLoadedCard* buffer
 * where the card data is loaded, check if the card is Clefairy Doll or Mysterious Fossil,
 * and, if so, convert it to a Pokemon card in the wLoadedCard* buffer, using .trainer_to_pkmn_data. */
void ConvertSpecialTrainerCardToPokemon(u8 card, struct CardData *buf)
{
	if (buf->Type != TYPE_TRAINER)
		return; /* return if the card is not TRAINER type */
	if (!(GetTurnDuelistVariable(card) & CARD_LOCATION_PLAY_AREA))
		return; /* return if the card is not in the arena or bench */
	/* (register e still holds the card ID loaded by the caller) */
	u8 id = buf->ID;
	if (id != MYSTERIOUS_FOSSIL && id != CLEFAIRY_DOLL)
		return;
	buf->Type = TYPE_PKMN_COLORLESS;
	static const u8 *data;
	if (!data)
		data = RomPtr(0x01, RomAddr("ConvertSpecialTrainerCardToPokemon.trainer_to_pkmn_data"));
	memcpy((u8 *)buf + CARD_DATA_HP, data, CARD_DATA_AI_INFO - CARD_DATA_HP);
}

/* this function applies all status conditions in order
 * that have been added to the wStatusConditionQueue
 * return carry if any status conditions were applied
 * and the defending Pokemon didn't have "No Damage or Effect" status */
bool ApplyStatusConditionQueue(void)
{
	TRACE_FN();
	wPlayerArenaCardLastTurnStatus = 0;
	wOpponentArenaCardLastTurnStatus = 0;
	u8 n = wStatusConditionQueueIndex;
	if (n == 0)
		return false;
	wStatusConditionQueue[n] = 0; /* terminator byte */
	if (!CheckNoDamageOrEffect()) {
		/* apply all status conditions unconditionally */
		for (u8 *hl = wStatusConditionQueue; *hl; hl += 3)
			ApplyStatusConditionToArenaPokemon(hl);
		return true;
	}
	/* no_damage_or_effect */
	if (gTextID)
		DrawWideTextBox_PrintText(gTextID);
	/* if no damage or effect to defending Pokemon,
	 * we will just apply the conditions to turn duelist's Pokemon */
	for (u8 *hl = wStatusConditionQueue; *hl; hl += 3) {
		if (wWhoseTurn == hl[0])
			ApplyStatusConditionToArenaPokemon(hl);
	}
	return false;
}

/* apply the status condition at hl+2 to the arena Pokemon of duelist hl[0]
 * discard the arena Pokemon's status conditions contained in the bitmask at hl+1 */
void ApplyStatusConditionToArenaPokemon(const u8 *hl)
{
	u8 *vars = DuelVarsOf(hl[0]);
	vars[DUELVARS_ARENA_CARD_STATUS] = (vars[DUELVARS_ARENA_CARD_STATUS] & hl[1]) | hl[2];
	vars[DUELVARS_ARENA_CARD_LAST_TURN_STATUS] = (vars[DUELVARS_ARENA_CARD_LAST_TURN_STATUS] & hl[1]) | hl[2];
}

void HandleDestinyBondAndBetweenTurnKnockOuts(void)
{
	HandleDestinyBondSubstatus();
	HandleBetweenTurnKnockOuts();
}

/* clears SUBSTATUS2_REDUCE_BY_20, SUBSTATUS2_POUNCE, SUBSTATUS2_GROWL,
 * SUBSTATUS2_TAIL_WAG, and SUBSTATUS2_LEER for each arena Pokemon with 0 HP */
static void HandleBetweenTurnKnockOuts_ClearDamageReductionSubstatus2OfKnockedOutPokemon(void)
{
	SwapTurn();
	if (GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_HP) == 0)
		ClearDamageReductionSubstatus2();
	SwapTurn();
	if (GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_HP) == 0)
		ClearDamageReductionSubstatus2();
}

static bool Func_6fa5(void);

/* `rl [wDuelFinishParam]` with carry */
static void ShiftDuelFinishParam(bool carry)
{
	wDuelFinishParam = (u8)((wDuelFinishParam << 1) | (carry ? 1 : 0));
}

void HandleBetweenTurnKnockOuts(void)
{
	TRACE_FN();
	static const u8 Data_6ed2[16] = {
		DUEL_NOT_FINISHED, TURN_PLAYER_LOST, TURN_PLAYER_WON,  TURN_PLAYER_TIED,
		TURN_PLAYER_LOST,  TURN_PLAYER_LOST, TURN_PLAYER_TIED, TURN_PLAYER_LOST,
		TURN_PLAYER_WON,   TURN_PLAYER_TIED, TURN_PLAYER_WON,  TURN_PLAYER_WON,
		TURN_PLAYER_TIED,  TURN_PLAYER_LOST, TURN_PLAYER_WON,  TURN_PLAYER_TIED,
	};
	HandleBetweenTurnKnockOuts_ClearDamageReductionSubstatus2OfKnockedOutPokemon();
	wDuelFinishParam = 0;
	SwapTurn();
	ShiftDuelFinishParam(Func_6fa5());
	SwapTurn();
	u8 result;
	if (wDuelFinishParam != 0 && !CheckIfTurnDuelistPlayAreaPokemonAreAllKnockedOut()) {
		u8 c = CountKnockedOutPokemon();
		SwapTurn();
		u8 a = CountPrizes();
		SwapTurn();
		a--;
		if (!(a < c)) {
			SwapTurn();
			TakeAPrizes(c);
			SwapTurn();
			result = TURN_PLAYER_WON;
			goto set_duel_finished;
		}
	}
	/* asm_6e86 */
	ShiftDuelFinishParam(Func_6fa5());
	if (wDuelFinishParam == TRUE) {
		SwapTurn();
		bool c = CheckIfTurnDuelistPlayAreaPokemonAreAllKnockedOut();
		SwapTurn();
		if (!c) {
			result = TURN_PLAYER_LOST;
			goto set_duel_finished;
		}
	}
	/* asm_6e9f */
	SwapTurn();
	ShiftDuelFinishParam(ReplaceKnockedOutPokemon_Carry());
	SwapTurn();
	ShiftDuelFinishParam(ReplaceKnockedOutPokemon_Carry());
	if (wDuelFinishParam == 0)
		goto move_ko;
	result = Data_6ed2[wDuelFinishParam & 0x0f];
set_duel_finished:
	wDuelFinished = result;
move_ko:
	MoveAllTurnHolderKnockedOutPokemonToDiscardPile();
	SwapTurn();
	MoveAllTurnHolderKnockedOutPokemonToDiscardPile();
	SwapTurn();
	ShiftAllPokemonToFirstPlayAreaSlots();
}

/* for each Pokemon in the turn holder's play area (arena and bench),
 * move that card to the discard pile if its HP is 0 */
void MoveAllTurnHolderKnockedOutPokemonToDiscardPile(void)
{
	u8 *vars = TurnDuelistVars();
	u8 d = vars[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA];
	u8 l = DUELVARS_ARENA_CARD_HP;
	u8 e = PLAY_AREA_ARENA;
	do {
		if (vars[l] == 0)
			MovePlayAreaCardToDiscardPile(e);
		l++;
		e++;
	} while (--d);
}

/* have the turn holder replace the arena Pokemon card when it's been knocked out.
 * if there are no Pokemon cards in the turn holder's bench, return carry. */
bool ReplaceKnockedOutPokemon_Carry(void)
{
	TRACE_FN();
	if (GetTurnDuelistVariable(DUELVARS_ARENA_CARD_HP) != 0)
		return false;
	ClearAllStatusConditions();
	if (HasAlivePokemonInBench(NULL)) {
		/* if we made it here, the duelist can't replace the knocked out Pokemon */
		DrawDuelMainScene();
		DrawWideTextBox_WaitForInput(ThereAreNoPokemonInPlayAreaText);
		ExchangeRNG();
		return true;
	}
	/* can_replace_pokemon */
	u8 type = GetTurnDuelistVariable(DUELVARS_DUELIST_TYPE);
	if (type == DUELIST_TYPE_PLAYER) {
		/* prompt the player to replace the knocked out Pokemon with one from bench */
		u8 saved = gUIDecider;
		gUIDecider = hWhoseTurn;
		DrawDuelMainScene();
		DrawWideTextBox_WaitForInput(SelectPokemonToPlaceInTheArenaText);
		wPlayAreaSelectAction = 1;
		DoPracticeDuelAction(PRACTICEDUEL_PLAY_STARYU_FROM_BENCH);
		for (;;) {
			while (OpenPlayAreaScreenForSelection())
				;
			SerialSend8Bytes();
			/* replace_pokemon */
			FinishQueuedAnimations();
			if (DoPracticeDuelAction(PRACTICEDUEL_REPLACE_KNOCKED_OUT_POKEMON))
				continue;
			break;
		}
		gUIDecider = saved;
	} else {
		/* the AI opponent replaces the knocked out Pokemon with one from bench */
		if (type == DUELIST_TYPE_LINK_OPP)
			tcg_fatal("link duels are not supported");
		AIDoAction_KOSwitch();
		hTempPlayAreaLocation_ff9d = hTemp_ffa0;
		FinishQueuedAnimations();
		/* (DoPracticeDuelAction cannot request a retry outside a practice duel) */
		DoPracticeDuelAction(PRACTICEDUEL_REPLACE_KNOCKED_OUT_POKEMON);
	}
	/* replace the arena Pokemon with the one at location [hTempPlayAreaLocation_ff9d] */
	SwapPlayAreaPokemon(hTempPlayAreaLocation_ff9d, PLAY_AREA_ARENA);
	DisplayCardDetailScreen(GetTurnDuelistVariable(DUELVARS_ARENA_CARD), DuelistPlacedACardText);
	ExchangeRNG();
	return false;
}

void ReplaceKnockedOutPokemon(void)
{
	ReplaceKnockedOutPokemon_Carry();
}

static bool Func_6fa5(void)
{
	if (!CountKnockedOutPokemon())
		return false;
	/* at least one Pokemon knocked out */
	SwapTurn();
	bool c = TurnDuelistTakePrizes();
	SwapTurn();
	if (!c)
		return false;
	SwapTurn();
	DrawDuelMainScene();
	DrawWideTextBox_WaitForInput(TookAllThePrizesText);
	ExchangeRNG();
	SwapTurn();
	return true;
}

/* return in wNumberPrizeCardsToTake the amount of Pokemon in the turn holder's
 * play area that are still there despite having 0 HP.
 * that is, the number of Pokemon that have just been knocked out.
 * Clefairy Doll and Mysterious Fossil don't count. (nonzero == carry) */
u8 CountKnockedOutPokemon(void)
{
	u8 *vars = TurnDuelistVars();
	u8 b = 0;
	for (u8 i = 0; i < MAX_PLAY_AREA_POKEMON; i++) {
		u8 card = vars[(u8)(DUELVARS_ARENA_CARD + i)];
		if (card == 0xff)
			continue; /* no Pokemon in this location */
		if (vars[(u8)(DUELVARS_ARENA_CARD_HP + i)] != 0)
			continue; /* this Pokemon's HP isn't 0 */
		/* this Pokemon's HP has just become 0 */
		if (GetCardType(GetCardIDFromDeckIndex(card)) == TYPE_TRAINER)
			continue; /* trainer card (Clefairy Doll or Mysterious Fossil) */
		b++;
	}
	wNumberPrizeCardsToTake = b;
	return b;
}

/* returns carry if turn duelist has no Play Area Pokémon
 * with non-zero HP, that is, all Pokémon are knocked out */
bool CheckIfTurnDuelistPlayAreaPokemonAreAllKnockedOut(void)
{
	u8 *vars = TurnDuelistVars();
	u8 c = vars[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA];
	u8 l = DUELVARS_ARENA_CARD_HP;
	do {
		if (vars[l++] != 0)
			return false;
	} while (--c);
	return true;
}

/* return one of the "There was no effect from" texts (register hl) depending
 * on the value at wNoEffectFromWhichStatus (NO_STATUS or a status condition constant) */
u16 PrintThereWasNoEffectFromStatusText(void)
{
	u8 a = wNoEffectFromWhichStatus;
	if (a == 0) {
		LoadTxRam2(rd16(wLoadedAttack.Name));
		return ThereWasNoEffectFromTxRam2Text;
	}
	u8 c = a;
	if (a == (POISONED | CONFUSED))
		return ThereWasNoEffectFromPoisonConfusionText;
	if (a & PSN_DBLPSN) {
		a &= PSN_DBLPSN;
		if (a == POISONED)
			return ThereWasNoEffectFromPoisonText;
		return ThereWasNoEffectFromToxicText;
	}
	a = c & CNF_SLP_PRZ;
	if (a == PARALYZED)
		return ThereWasNoEffectFromParalysisText;
	if (a == ASLEEP)
		return ThereWasNoEffectFromSleepText;
	return ThereWasNoEffectFromConfusionText;
}

/* returns carry if card at hTempPlayAreaLocation_ff9d is a basic card.
 * otherwise, lists the card indices of all stages in that card location,
 * and returns the card one stage below.
 * output: *e = card index in hTempPlayAreaLocation_ff9d; *d = card index of card one stage below */
bool GetCardOneStageBelow(u8 *d, u8 *e)
{
	LoadCardDataToBuffer2_FromDeckIndex(GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)));
	if (wLoadedCard2.Stage == 0)
		return true;
	wAllStagesIndices[0] = 0xff;
	wAllStagesIndices[1] = 0xff;
	wAllStagesIndices[2] = 0xff;
	/* loads deck indices of the stages present in hTempPlayAreaLocation_ff9d.
	 * the three stages are loaded consecutively in wAllStagesIndices. */
	u8 c = hTempPlayAreaLocation_ff9d | CARD_LOCATION_PLAY_AREA;
	u8 *vars = TurnDuelistVars();
	for (u8 l = 0; l < DECK_SIZE; l++) {
		if (vars[l] != c)
			continue;
		LoadCardDataToBuffer2_FromDeckIndex(l);
		if (wLoadedCard2.Type >= TYPE_ENERGY)
			continue;
		/* (an index past the array writes the following WRAM byte, as on the GB) */
		gWRAM[ADDR_wAllStagesIndices - WRAM_START + wLoadedCard2.Stage] = l;
	}
	/* if card at hTempPlayAreaLocation_ff9d is a stage 1, load d with basic card.
	 * otherwise if stage 2, load d with the stage 1 card. */
	u8 stage = vars[(u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD_STAGE)];
	u8 *hl = wAllStagesIndices; /* pointing to basic */
	if (stage != STAGE1 && stage != STAGE2_WITHOUT_STAGE1)
		hl++; /* pointing to stage 1 */
	*d = *hl;
	*e = vars[(u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)];
	return false;
}

/* ---------------------------------------------------------------- variable initialisation */

/* initializes variables when a duel begins, such as zeroing wDuelFinished or wDuelTurns,
 * and setting wDuelType based on wPlayerDuelistType and wOpponentDuelistType */
void InitVariablesToBeginDuel(void)
{
	wDuelFinished = 0;
	wDuelTurns = 0;
	wUnused_cce7 = 0;
	wUnused_cc0f = 0xff;
	wPlayerAttackingCardIndex = 0xff;
	wPlayerAttackingAttackIndex = 0xff;
	wSkipDelayAllowed = 0; /* [sSkipDelayAllowed]: the port has no debug options in SRAM */
	u8 a = wPlayerDuelistType;
	if (a == DUELIST_TYPE_LINK_OPP || (a & 0x80))
		goto set;
	a = wOpponentDuelistType;
	if (a == DUELIST_TYPE_LINK_OPP || (a & 0x80))
		goto set;
	a = 0;
set:
	wDuelType = a;
}

/* init variables that last a single player's turn */
void InitVariablesToBeginTurn(void)
{
	wAlreadyPlayedEnergy = 0;
	wConfusionRetreatCheckWasUnsuccessful = 0;
	wGotHeadsFromSandAttackOrSmokescreenCheck = 0;
	wWhoseTurn = hWhoseTurn;
}

/* make all Pokemon in the turn holder's play area able to evolve. called from the
 * player's second turn on, in order to allow evolution of all Pokemon already played. */
void SetAllPlayAreaPokemonCanEvolve(void)
{
	u8 *vars = TurnDuelistVars();
	u8 c = vars[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA];
	u8 l = DUELVARS_ARENA_CARD_FLAGS;
	do {
		vars[l] &= ~USED_PKMN_POWER_THIS_TURN;
		vars[l] |= CAN_EVOLVE_THIS_TURN;
		l++;
	} while (--c);
}

/* initializes duel variables such as cards in deck and in hand, or Pokemon in play area
 * player turn: [c200, c2ff]
 * opponent turn: [c300, c3ff] */
static void InitializeDuelVariables(void)
{
	u8 *vars = TurnDuelistVars();
	u8 type = vars[DUELVARS_DUELIST_TYPE];
	memset(vars, 0, 0x100);
	vars[DUELVARS_DUELIST_TYPE] = type;
	/* zero card locations and cards in hand, and init order of cards in deck */
	for (u8 b = 0; b < DECK_SIZE; b++) {
		vars[(u8)(DUELVARS_DECK_CARDS + b)] = b;
		vars[b] = 0;
	}
	/* initialize to $ff card in arena as well as cards in bench (plus a terminator) */
	for (u8 i = 0; i < MAX_PLAY_AREA_POKEMON + 1; i++)
		vars[(u8)(DUELVARS_ARENA_CARD + i)] = 0xff;
}

static const u8 PrizeBitmasks[] = { 0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f };

/* draw [wDuelInitialPrizes] cards from the turn holder's deck and place them as prizes:
 * write their deck indexes to DUELVARS_PRIZE_CARDS, set their location to
 * CARD_LOCATION_PRIZE, and set [wDuelInitialPrizes] bits of DUELVARS_PRIZES. */
static void InitTurnDuelistPrizes(void)
{
	u8 *vars = TurnDuelistVars();
	u8 c = wDuelInitialPrizes;
	u8 e = DUELVARS_PRIZE_CARDS;
	u8 b = 0;
	do {
		u8 a;
		DrawCardFromDeck(&a);
		vars[e++] = a;
		vars[a] = CARD_LOCATION_PRIZE;
		b++;
	} while (b != c);
	/* (PrizeBitmasks is indexed by the number of prizes; values above 6 read the following code bytes) */
	vars[DUELVARS_PRIZES] = c < sizeof(PrizeBitmasks) ? PrizeBitmasks[c] : 0;
}

/* update the turn holder's DUELVARS_PRIZES following that duelist
 * drawing a number of prizes equal to register a */
void TakeAPrizes(u8 a)
{
	if (a == 0)
		return;
	u8 n = CountPrizes();
	n = n >= a ? (u8)(n - a) : 0;
	TurnDuelistVars()[DUELVARS_PRIZES] = PrizeBitmasks[n];
}

/* clear the non-turn holder's duelvars starting at DUELVARS_ARENA_CARD_DISABLED_ATTACK_INDEX
 * these duelvars only last a two-player turn at most. */
void ClearNonTurnTemporaryDuelvars(void)
{
	u8 *vars = NonTurnDuelistVars();
	for (u8 i = 0; i < 8; i++)
		vars[(u8)(DUELVARS_ARENA_CARD_DISABLED_ATTACK_INDEX + i)] = 0;
}

/* same as ClearNonTurnTemporaryDuelvars, except the non-turn holder's arena
 * Pokemon status condition is copied to wUnused_DefendingPkmnStatus */
void ClearNonTurnTemporaryDuelvars_CopyStatus(void)
{
	wUnused_DefendingPkmnStatus = GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_STATUS);
	ClearNonTurnTemporaryDuelvars();
}

/* update non-turn holder's DUELVARS_ARENA_CARD_LAST_TURN_DAMAGE
 * if wDefendingWasForcedToSwitch == 0: set to [wDealtDamage]
 * if wDefendingWasForcedToSwitch != 0: set to 0 */
void UpdateArenaCardLastTurnDamage(void)
{
	u8 *vars = NonTurnDuelistVars();
	if (wDefendingWasForcedToSwitch) {
		vars[DUELVARS_ARENA_CARD_LAST_TURN_DAMAGE] = 0;
		vars[DUELVARS_ARENA_CARD_LAST_TURN_DAMAGE + 1] = 0;
		return;
	}
	vars[DUELVARS_ARENA_CARD_LAST_TURN_DAMAGE] = wDealtDamage[0];
	vars[DUELVARS_ARENA_CARD_LAST_TURN_DAMAGE + 1] = wDealtDamage[1];
}

/* ---------------------------------------------------------------- misc */

/* return carry if the turn holder has any Pokemon with non-zero HP in the play area
 * (in the bench if exclude_arena). *count = how many (register b). also resets the
 * play area screen state for the next DisplayPlayAreaScreen.
 * bug-compatible: the original's comment says "return carry if any"; it actually
 * returns carry if there are NONE. */
static bool _HasAlivePokemonInPlayArea(u8 exclude, u8 *count)
{
	wExcludeArenaPokemon = exclude;
	u8 *vars = TurnDuelistVars();
	u8 c = (u8)(vars[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA] - exclude);
	u8 l = (u8)(DUELVARS_ARENA_CARD_HP + exclude);
	u8 b = 0;
	wPlayAreaScreenLoaded = 0;
	wPlayAreaSelectAction = 0;
	c++;
	while (--c) {
		if (vars[l++] != 0)
			b++;
	}
	if (count)
		*count = b;
	return b == 0;
}

bool HasAlivePokemonInBench(u8 *count)
{
	return _HasAlivePokemonInPlayArea(1, count);
}

bool HasAlivePokemonInPlayArea(u8 *count)
{
	return _HasAlivePokemonInPlayArea(0, count);
}

static bool DoPracticeDuelAction(u8 action)
{
	wPracticeDuelAction = action;
	if (!wIsPracticeDuel)
		return false;
	return PracticeDuelAction(action);
}

void LoadPlayerDeck(void)
{
	/* the original copies sDeck<sCurrentlySelectedDeck>Cards from SRAM; the port's frontend
	 * provides the player's deck (tcg_run_duel) */
	memcpy(wPlayerDeck, gPlayerDeckCards, DECK_SIZE);
}

/* DuelDataToSave: what SaveDuelData copies to SRAM (826 bytes) */
static const struct { u16 address, size; } DuelDataToSave[] = {
	{ ADDR_wPlayerDuelVariables, ADDR_wOpponentDuelVariables - ADDR_wPlayerDuelVariables },
	{ ADDR_wOpponentDuelVariables, ADDR_wPlayerDeck - ADDR_wOpponentDuelVariables },
	{ ADDR_wPlayerDeck, ADDR_wDuelTempList - ADDR_wPlayerDeck },
	{ ADDR_wDuelStates, ADDR_wDuelStatesEnd - ADDR_wDuelStates },
	{ ADDR_hWhoseTurn, 1 },
	{ ADDR_wRNGVars, RNGVARS_SIZE },
	{ ADDR_wAIDuelVars, ADDR_wAIDuelVarsEnd - ADDR_wAIDuelVars },
};

/* in-memory stand-ins for the SRAM slots sCurrentDuel and sBackupCurrentDuel (data part) */
u8 gCurrentDuel[SAVE_DUEL_DATA_SIZE];
u8 gBackupCurrentDuel[SAVE_DUEL_DATA_SIZE];
static u8 *gSaveDuelTarget = gCurrentDuel;

static void SaveDuelDataTo(u8 *de)
{
	for (unsigned i = 0; i < sizeof(DuelDataToSave) / sizeof(*DuelDataToSave); i++) {
		memcpy(de, gbptr(DuelDataToSave[i].address), DuelDataToSave[i].size);
		de += DuelDataToSave[i].size;
	}
}

/* save data of the current duel to sCurrentDuel (or to sBackupCurrentDuel when the
 * SRAM bank was switched by SaveDuelStateToSRAM) */
void SaveDuelData(void)
{
	SaveDuelDataTo(gSaveDuelTarget);
}

void SaveDuelDataToBackup(void)
{
	gSaveDuelTarget = gBackupCurrentDuel;
	SaveDuelData();
	gSaveDuelTarget = gCurrentDuel;
}

/* load data saved at de (in SRAM) to WRAM, using DuelDataToSave table */
void LoadSavedDuelData(const u8 *de)
{
	for (unsigned i = 0; i < sizeof(DuelDataToSave) / sizeof(*DuelDataToSave); i++) {
		memcpy(gbptr(DuelDataToSave[i].address), de, DuelDataToSave[i].size);
		de += DuelDataToSave[i].size;
	}
}
