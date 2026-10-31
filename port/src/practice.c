/* Port of the practice duel with Sam: DoPracticeDuelAction's PracticeDuelActionTable
 * and its helpers (engine/duel/core.asm). Only active when wIsPracticeDuel != 0. */
#include "engine.h"
#include "gen/rom_blobs.h"

static bool ReturnWrongAction(void)
{
	return true;
}

/* print a text box with the given text id, labeled as 'Dr. Mason' (flags are preserved) */
static void PrintPracticeDuelDrMasonInstructions(u16 text)
{
	wr16(wTextBoxLabel, DrMasonText);
	tcg_ui_practice_speech(text);
}

/* used only during the practice duel with Sam.
 * displays the list with the player's cards in hand, and the player's name above the list. */
static void DisplayPracticeDuelPlayerHandScreen(void)
{
	CreateHandCardList(NULL);
	EmptyScreen();
	tcg_ui_screen(TCG_SCREEN_CARD_LIST, CountCardsInDuelTempList(), 0);
}

/* print "<Player>'s Turn [wDuelTurns]" (usually) as the textbox label */
static void PrintPracticeDuelInstructionsTextBoxLabel(void)
{
	if (wDuelTurns == 7)
		return; /* replace_due_to_knockout: ReplaceDueToKnockoutPracticeDuelText */
	/* load the player's turn number to TX_RAM3 in order to print it */
	LoadTxRam3((u8)((wDuelTurns >> 1) + 1));
}

/* in a practice duel, draws the text box where the point-by-point
 * instructions for the next player action will be written into */
static void DrawPracticeDuelInstructionsTextBox(void)
{
	tcg_ui_practice_checklist(0,NULL,0);
	EmptyScreen();
	PrintPracticeDuelInstructionsTextBoxLabel();
}

/* the practicetext structs: db y, tx DrMasonText, tx InstructionText ... db 0 */
static const u8 *PracticeText(u16 addr)
{
	return RomPtr(0x01, addr);
}

/* The GBC redraws these lines in its main-area instruction panel, while
 * Mason's scrollable speech remains below. Redraws never request input. */
static void PublishPracticeChecklist(u16 addr,u8 through_y)
{
	u16 steps[8];unsigned count=0;
	const u8 *entry=PracticeText(addr);
	while(entry[0]&&entry[0]<=through_y&&count<sizeof(steps)/sizeof(*steps)) {
		steps[count++]=rd16(&entry[3]);entry+=5;
	}
	tcg_ui_practice_checklist(wDuelTurns==7?ReplaceDueToKnockoutPracticeDuelText:
		PlayersTurnPracticeDuelText,steps,count);
}

/* print a single instruction bullet for the current turn */
static void PrintNextPracticeDuelInstruction(void)
{
	hffb0 = 1;
	PrintPracticeDuelInstructionsTextBoxLabel();
	PublishPracticeChecklist(rd16(wPracticeDuelTextPointer),wPracticeDuelTextY);
	hffb0 = 0;
}

static void PrintPracticeDuelLetsPlayTheGame(void)
{
	PrintPracticeDuelDrMasonInstructions(LetsPlayTheGamePracticeDuelText);
}

/* print practice duel instructions given the GB address of a PracticeDuelText_* struct.
 * each practicetext entry contains a Dr. Mason text along with
 * a numbered instruction text, that is later printed without text delay. */
static void PrintPracticeDuelInstructions(u16 addr)
{
	wPracticeDuelTextY = 0;
	wr16(wPracticeDuelTextPointer, addr);
	const u8 *hl = PracticeText(addr);
	for (;;) {
		PrintNextPracticeDuelInstruction();
		u8 y = *hl++;
		wPracticeDuelTextY = y;
		if (y == 0)
			break;
		PrintPracticeDuelDrMasonInstructions(rd16(hl));
		hl += 2;
		PublishPracticeChecklist(addr,wPracticeDuelTextY);
		hl += 2;
	}
	PrintPracticeDuelLetsPlayTheGame();
}

/* simplified version of PrintPracticeDuelInstructions that skips Dr. Mason's text
 * and instead places the point-by-point instructions all at once. */
static void PrintPracticeDuelInstructions_Fast(u16 addr)
{
	PublishPracticeChecklist(addr,255);
	PrintPracticeDuelLetsPlayTheGame();
}

/* print the instructions of the current practice duel turn, taken from
 * one of the structs in PracticeDuelTextPointerTable.
 * if a != 0, only the point-by-point instructions are printed, otherwise
 * Dr. Mason instructions are also shown in a textbox at the bottom of the screen. */
static void PrintPracticeDuelInstructionsForCurrentTurn(u8 a)
{
	const u8 *table = RomPtr(0x01, RomAddr("PracticeDuelTextPointerTable"));
	u16 addr = rd16(&table[wDuelTurns & 0xfe]);
	if (a)
		PrintPracticeDuelInstructions_Fast(addr);
	else
		PrintPracticeDuelInstructions(addr);
}

static bool PracticeDuel_DrawSevenCards(void)
{
	DisplayPracticeDuelPlayerHandScreen();
	PrintPracticeDuelDrMasonInstructions(DrawSevenCardsPracticeDuelText);
	return false;
}

static bool PracticeDuel_PlayGoldeen(void)
{
	if (wLoadedCard1.ID == GOLDEEN)
		return false;
	PrintPracticeDuelDrMasonInstructions(ChooseGoldeenPracticeDuelText);
	return true;
}

static bool PracticeDuel_PutStaryuInBench(void)
{
	DisplayPracticeDuelPlayerHandScreen();
	PrintPracticeDuelDrMasonInstructions(PutPokemonOnBenchPracticeDuelText);
	return false;
}

static bool PracticeDuel_VerifyInitialPlay(void)
{
	if (GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA) == 2)
		return false;
	PrintPracticeDuelDrMasonInstructions(ChooseStaryuPracticeDuelText);
	return true;
}

static bool PracticeDuel_DonePuttingOnBench(void)
{
	DisplayPracticeDuelPlayerHandScreen();
	wPracticeDuelTurn = 0xff;
	PrintPracticeDuelDrMasonInstructions(PressBToFinishPracticeDuelText);
	return false;
}

static bool PracticeDuel_PrintTurnInstructions(void)
{
	DrawPracticeDuelInstructionsTextBox();
	u8 a = wDuelTurns;
	bool same = a == wPracticeDuelTurn;
	wPracticeDuelTurn = a;
	/* calling PrintPracticeDuelInstructionsForCurrentTurn with a = 0 means that Dr. Mason's
	 * instructions are also printed along with each of the point-by-point instructions */
	if (!same) {
		PrintPracticeDuelInstructionsForCurrentTurn(0);
		return false;
	}
	/* if we're here, the player followed the current turn actions wrong and has to
	 * repeat them. ask the player whether to show detailed instructions again, in
	 * order to call PrintPracticeDuelInstructionsForCurrentTurn with a = 0 or a = 1. */
	wr16(wTextBoxLabel, DrMasonText);
	DrawWideTextBox_PrintText(NeedPracticeAgainPracticeDuelText);
	bool no = YesOrNoMenu();
	PrintPracticeDuelInstructionsForCurrentTurn(no ? 1 : 0);
	return false;
}

static bool PracticeDuelVerify_Turn1(void)
{
	if (wTempCardID_ccc2 != GOLDEEN)
		return ReturnWrongAction();
	return false;
}

static bool PracticeDuelVerify_Turn2(void)
{
	if (wTempCardID_ccc2 != SEAKING)
		return ReturnWrongAction();
	if (wSelectedAttack != SECOND_ATTACK)
		return ReturnWrongAction();
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	if (wAttachedEnergies[PSYCHIC] == 0)
		return ReturnWrongAction();
	return false;
}

static bool PracticeDuelVerify_Turn3(void)
{
	if (wTempCardID_ccc2 != SEAKING)
		return ReturnWrongAction();
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_BENCH_1);
	if (wAttachedEnergies[WATER] == 0)
		return ReturnWrongAction();
	return false;
}

static bool PracticeDuelVerify_Turn4(void)
{
	if (wPlayerNumberOfPokemonInPlayArea != 3)
		return ReturnWrongAction();
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_BENCH_2);
	if (wAttachedEnergies[WATER] == 0)
		return ReturnWrongAction();
	if (wTempCardID_ccc2 != SEAKING)
		return ReturnWrongAction();
	if (wSelectedAttack != SECOND_ATTACK)
		return ReturnWrongAction();
	return false;
}

static bool PracticeDuelVerify_Turn5(void)
{
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	if (wAttachedEnergies[WATER] != 2)
		return ReturnWrongAction();
	if (wTempCardID_ccc2 != STARYU)
		return ReturnWrongAction();
	return false;
}

static bool PracticeDuelVerify_Turn6(void)
{
	GetPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	if (wAttachedEnergies[WATER] != 3)
		return ReturnWrongAction();
	if (wPlayerArenaCardHP != 40)
		return ReturnWrongAction();
	if (wTempCardID_ccc2 != STARYU)
		return ReturnWrongAction();
	return false;
}

static bool PracticeDuelVerify_Turn7Or8(void)
{
	if (wTempCardID_ccc2 != STARMIE)
		return ReturnWrongAction();
	if (wSelectedAttack != SECOND_ATTACK)
		return ReturnWrongAction();
	return false;
}

static bool PracticeDuel_RepeatInstructions(void)
{
	PrintPracticeDuelDrMasonInstructions(FollowMyGuidancePracticeDuelText);
	/* restart the turn from the saved data of the previous turn */
	LoadSavedDuelDataFromDE(gBackupCurrentDuel);
	/* return carry in order to repeat instructions */
	return true;
}

static bool PracticeDuel_VerifyPlayerTurnActions(void)
{
	static bool (*const table[8])(void) = {
		PracticeDuelVerify_Turn1, PracticeDuelVerify_Turn2, PracticeDuelVerify_Turn3,
		PracticeDuelVerify_Turn4, PracticeDuelVerify_Turn5, PracticeDuelVerify_Turn6,
		PracticeDuelVerify_Turn7Or8, PracticeDuelVerify_Turn7Or8,
	};
	u8 i = wDuelTurns >> 1;
	if (i >= 8)
		tcg_fatal("practice duel: turn out of range");
	/* return nc if player followed instructions correctly */
	if (!table[i]())
		return false;
	return PracticeDuel_RepeatInstructions();
}

static bool PracticeDuel_PlayStaryuFromBench(void)
{
	if (wDuelTurns != 7)
		return false;
	/* ask player to choose Staryu from bench to replace knocked out Seaking */
	DrawPracticeDuelInstructionsTextBox();
	PrintPracticeDuelInstructions(RomAddr("PracticeDuelText_SamTurn4"));
	return false;
}

static bool PracticeDuel_ReplaceKnockedOutPokemon(void)
{
	if (hTempPlayAreaLocation_ff9d == PLAY_AREA_BENCH_1)
		return false;
	/* if player selected Drowzee instead (which is at PLAY_AREA_BENCH_2) */
	HasAlivePokemonInBench(NULL);
	PrintPracticeDuelDrMasonInstructions(SelectStaryuPracticeDuelText);
	return true;
}

bool PracticeDuelAction(u8 action)
{
	switch (action) {
	case PRACTICEDUEL_DRAW_SEVEN_CARDS: return PracticeDuel_DrawSevenCards();
	case PRACTICEDUEL_PLAY_GOLDEEN: return PracticeDuel_PlayGoldeen();
	case PRACTICEDUEL_PUT_STARYU_IN_BENCH: return PracticeDuel_PutStaryuInBench();
	case PRACTICEDUEL_VERIFY_INITIAL_PLAY: return PracticeDuel_VerifyInitialPlay();
	case PRACTICEDUEL_DONE_PUTTING_ON_BENCH: return PracticeDuel_DonePuttingOnBench();
	case PRACTICEDUEL_PRINT_TURN_INSTRUCTIONS: return PracticeDuel_PrintTurnInstructions();
	case PRACTICEDUEL_VERIFY_PLAYER_TURN_ACTIONS: return PracticeDuel_VerifyPlayerTurnActions();
	case PRACTICEDUEL_REPEAT_INSTRUCTIONS: return PracticeDuel_RepeatInstructions();
	case PRACTICEDUEL_PLAY_STARYU_FROM_BENCH: return PracticeDuel_PlayStaryuFromBench();
	case PRACTICEDUEL_REPLACE_KNOCKED_OUT_POKEMON: return PracticeDuel_ReplaceKnockedOutPokemon();
	}
	tcg_fatal("invalid practice duel action");
	return false;
}
