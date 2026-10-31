/*
 * UI shims (see PORTING.md, "UI").
 *
 * Each routine here has the name of the original routine that draws, prints,
 * animates, plays sound or reads input. The shim keeps the original's
 * observable side effects on engine state (WRAM/HRAM) and forwards the rest to
 * the frontend through the host interface in tcg.h.
 *
 * Input shims block until the human answers, and then return exactly what the
 * original routine returns after an A (select) or B (cancel) press, including
 * hKeysPressed = PAD_A / PAD_B, hCurMenuItem, wCurMenuItem, etc., so that
 * ported callers inspecting those keep working.
 *
 * Pure drawing routines with no observable side effects (tile/palette/tilemap
 * writes, LoadDuelCardSymbolTiles, FillRectangle, EnableLCD, ...) are not
 * shimmed: just drop such calls when porting, unless a screen change is worth
 * telling the frontend about (then use tcg_ui_screen()).
 */
#ifndef TCG_DUEL_UI_H
#define TCG_DUEL_UI_H

#include "common.h"
#include "tcg.h"

/* ---------------------------------------------------------------- frontend plumbing */
void tcg_ui_event(u8 type, u8 a0, u8 a1, u8 a2, u8 a3, u16 text);
void tcg_ui_screen(u8 screen, u8 a2, u8 a3);
/* the duelist that answers requests; 0 = default (wWhoseTurn). Set it around
 * selections that the non-turn duelist makes (forced switch, KO replacement, ...). */
extern u8 gUIDecider;

/* ---------------------------------------------------------------- text (home/print_text.asm, text_box.asm, menus.asm) */
void print_ui_text(u16 text);                       /* print a text without a text box routine (instructions etc.) */
void tcg_ui_practice_checklist(u16 label,const u16 *steps,unsigned count);
void tcg_ui_practice_speech(u16 text);
void LoadTxRam2(u16 text);
void LoadTxRam3(u16 number);
void DrawWideTextBox(void);
void DrawNarrowTextBox(void);
void DrawWideTextBox_PrintText(u16 text);
void DrawWideTextBox_PrintTextNoDelay(u16 text);
void DrawNarrowTextBox_PrintText(u16 text);
void DrawNarrowTextBox_PrintTextNoDelay(u16 text);
void DrawWideTextBox_WaitForInput(u16 text);       /* print, then TCG_REQ_ACK */
void DrawNarrowTextBox_WaitForInput(u16 text);
void WaitForWideTextBoxInput(void);                /* TCG_REQ_ACK */
bool WaitForButtonAorB(void);                      /* TCG_REQ_CONFIRM; carry if B */
void DrawWholeScreenTextBox(u16 text);             /* print over the whole screen + ACK */
/* writes "<name> Lv<level>" of wLoadedCard1 (at most max_len tiles) to the default text
 * (what TX_RAM2 prints when its text id is 0). In the original it writes wDefaultText. */
void CopyCardNameAndLevel(u8 max_len);
/* copy text (id 0: turn duelist's name) to de; returns the address of the terminator */
u8 *CopyText(u16 text, u8 *de);
u8 HandlePeekSelection(void);                      /* see duel_ui.c */
void DrawAIPeekScreen(u8 b);

/* ---------------------------------------------------------------- generic menus (home/menus.asm) */
struct MenuParams {                 /* the 8 bytes InitializeMenuParameters copies to wMenuParams */
	u8 cursor_x, cursor_y;
	u8 y_separation;
	u8 num_items;
	u8 visible_cursor_tile, invisible_cursor_tile;
	u16 update_func;               /* original GB address of the menu update function (0 if none) */
	u8 menu;                       /* TcgMenu context for the frontend (port only, not copied to WRAM) */
};
void InitializeMenuParameters(u8 a, const struct MenuParams *params);
/* blocks until A or B. returns carry; a = hCurMenuItem (MENU_CANCEL if B), e = wCurMenuItem */
bool HandleMenuInput(void);
bool HandleColorMenuInput(const u8 *item_to_color); /* TCG_REQ_COLOR; same results as HandleMenuInput */
void EraseCursor(void);
void DrawCursor2(void);
void RefreshMenuCursor(void);
void SetCursorParametersForTextBox(u8 d, u8 e, u8 b, u8 c);
/* yes/no: return carry if "no" selected (hCurMenuItem = 0 yes / 1 no). wDefaultYesOrNo = default */
bool YesOrNoMenu(void);
bool YesOrNoMenuWithText(u16 text);
bool YesOrNoMenuWithText_LeftAligned(u16 text);
bool YesOrNoMenuWithText_SetCursorToYes(u16 text);
bool TwoItemHorizontalMenu(u16 text);              /* carry if the second item */

/* ---------------------------------------------------------------- frames, sound */
void DoFrame(void);
void WaitFrames(u8 n);
void PlaySFX(u8 sfx);
void PlaySong(u8 song);
void PlaySFX_InvalidChoice(void);                  /* SFX_DENIED */
void PlayDefaultSong(void);

/* ---------------------------------------------------------------- duel screens (engine/duel/core.asm) */
extern const char *gDuelistNames[2];               /* TX_RAM1 names of the PLAYER_TURN / OPPONENT_TURN duelists */
void SetupDuel(void);
void ClearJoypad(void);
void PrintDuelResultStats(void);
void PlaceDuelMenuItems(void);
u8 HandleDuelMenuInput(u8 initial);                /* blocks; returns the selected item (also hCurMenuItem) */
void DisplayPlayAreaScreenToUsePkmnPower_DrawScreen(void);
void PrintAttachedEnergyToPokemon(void);
void PrintPokemonEvolvedIntoPokemon(void);
void OpenDuelCheckMenu(void);
void DisplayNoBasicPokemonInHandScreen(void);
void HandleDuelSetup_PlacePrizes(void);
void CopyPlayerNameToDefaultText(void);
void CopyOpponentNameToDefaultText(void);
void DrawPlayAreaToPlacePrizeCards(void);
void PrintScrollableText_NoTextBoxLabel(u16 text);
void SelectPrizeCards(u8 n);                       /* home/duel_menus.asm, engine/menus/duel.asm _SelectPrizeCards */
void DrawYourOrOppPlayAreaScreen_Bank0(u8 l);
void Func_82b6(void);
void EmptyScreen(void);
void ZeroObjectPositionsAndToggleOAMCopy(void);
void DrawDuelMainScene(void);
void DrawDuelHUDs(void);
void DrawDuelBoxMessage(u8 boxmsg);
void DrawDuelistPortraitsAndNames(void);
void DisplayDuelistTurnScreen(void);
void DisplayCardDetailScreen(u8 card, u16 text);   /* loads wLoadedCard1 */
void DisplayPlayerDrawCardScreen(void);            /* hTempCardIndex_ff98, "You drew" */
void DisplayCardListDetails(void);
void DrawLargePictureOfCard(void);                 /* wLoadedCard1 */
void DisplayUsePokemonPowerScreen(void);           /* hTempPlayAreaLocation_ff9d */
void DisplayUsedTrainerCardDetailScreen(void);     /* hTempCardIndex_ff9f */
void PrintUsedTrainerCardDescription(void);
void DisplayOpponentUsedAttackScreen(void);
void DisplayDrawOneCardScreen(void);
void DisplayDrawNCardsScreen(u8 n);
void PlayShuffleAndDrawCardsAnimation_TurnDuelist(void);
void PlayShuffleAndDrawCardsAnimation_BothDuelists(void);
void PlayDeckShuffleAnimation(void);
void PrintAttackOrCardDescription(void);
void OpenCardPage_FromHand(void);                  /* view wLoadedCard1, ACK */
void OpenCardPage_FromCheckPlayArea(void);
void OpenAttackPage(void);
void RedrawTurnDuelistsMainSceneOrDuelHUD(void);
void RedrawTurnDuelistsDuelHUD(void);

/* card lists */
void InitAndDrawCardListScreenLayout(void);
void InitAndDrawCardListScreenLayout_WithSelectCheckMenu(void);
bool DrawCardListScreenLayout(void);               /* carry if wDuelTempList is empty */
void SetCardListHeaderText(u16 hl_info, u16 de_header);
void SetCardListInfoBoxText(u16 hl_info);
void SetDiscardPileScreenTexts(void);
void PrintSortNumberInCardList_SetPointer(void);
void PrintSortNumberInCardList_CallFromPointer(void);
/* DisplayCardList: list in wDuelTempList, texts in wCardListHeaderText/wCardListInfoBoxText.
 * returns carry if B; else the selected card in hTempCardIndex_ff98 (returned), its list
 * index in hCurMenuItem. Runs CardListItemSelectionMenu when wCardListItemSelectionMenuType != 0. */
bool DisplayCardList(u8 *card);
bool CardListItemSelectionMenu(void);              /* carry if B or CHECK */
bool OpenDiscardPileScreen(void);

/* play area screens */
bool HasAlivePokemonInBench(u8 *count);
bool HasAlivePokemonInPlayArea(u8 *count);
/* selection of a turn holder's play area Pokemon (not empty/KO'd). returns carry if B;
 * location in hTempPlayAreaLocation_ff9d and hCurMenuItem. honours wExcludeArenaPokemon. */
bool OpenPlayAreaScreenForSelection(void);
/* Prefer this absolute slot in the frontend without changing menu WRAM. */
bool OpenPlayAreaScreenForEvolutionSelection(u8 cursor);
bool OpenPlayAreaScreenForViewing(void);
bool DisplayPlayAreaScreen(u8 keys);
void SetupPlayAreaScreen(void);
u8 PrintPlayAreaCardList(void);                    /* sets wNumPlayAreaItems, returns it */
u8 PrintPlayAreaCardList_EnableLCD(void);
void InitAndPrintPlayAreaCardInformationAndLocation(void);
bool InitAndPrintPlayAreaCardInformationAndLocation_WithTextBox(void); /* TCG_REQ_CONFIRM; carry if B */
void PrintPlayAreaCardInformationAndLocation(void);

/* energy discard menu (wDuelTempList) */
void DisplayEnergyDiscardScreen(u8 loc);
void DisplayEnergyDiscardMenu(void);
bool HandleEnergyDiscardMenuInput(void);           /* carry if B; card in hTempCardIndex_ff98 */

/* numbers written to wStringBuffer (core.asm); b, c = screen coordinates */
void TwoByteNumberToTxSymbol_PadSpace_Bank1(u16 hl);
void WriteOneByteNumberInTxSymbol_PadSpace(u8 a, u8 b, u8 c);
void WriteTwoDigitNumberInTxSymbol_PadSpace(u8 a, u8 b, u8 c);

/* ---------------------------------------------------------------- animations (engine/duel/animations, core.asm) */
void ResetAnimationQueue(void);
void FinishQueuedAnimations(void);
void PlayDuelAnimation(u8 anim);
bool CheckAnyAnimationPlaying(void);
void ResetAttackAnimationIsPlaying(void);
/* de = damage, b = PLAY_AREA_*, c = wDamageEffectiveness value, h = duelist side ($c2/$c3) */
void PlayAttackAnimation(u16 de, u8 b, u8 c, u8 h);
void WaitAttackAnimation(void);
void PlayStatusConditionQueueAnimations(void);
/* animation + SubtractHP(hl, de) + HUD refresh; h is taken from hl */
void PlayAttackAnimation_DealAttackDamageSimple(u8 *hl, u16 de, u8 b, u8 c);
void PlayBetweenTurnsAnimation(u8 anim);

/* ---------------------------------------------------------------- coin toss (home/coin_toss.asm, core.asm _TossCoin) */
/* toss one coin, text = coin toss screen text. returns carry (true) if heads; wCoinTossNumHeads */
bool TossCoin(u16 text);
/* toss n coins; returns number of heads (carry if >= 1, see TossCoinATimes_Carry) */
u8 TossCoinATimes(u8 n, u16 text);

#endif
