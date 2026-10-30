/*
 * Routines used by effects/part3.c (effect_functions.asm lines 5540-8420) that
 * are not declared in any shared header yet (or whose shared declaration does
 * not expose an output the original returns). Provided by part1.c / part2.c
 * (non-static there) or to be provided by the lead. Nothing here is
 * implemented in part3.c.
 */
#ifndef TCG_EFFECTS_PART3_NEEDS_H
#define TCG_EFFECTS_PART3_NEEDS_H

#include "effects.h"

/* ---- engine/duel/effect_functions.asm, part 1 range (lines 10-35) ----
 * These are not referenced from effect command lists, so they are not in
 * gen/effect_commands.h, but part 3 calls them: part1.c must define them
 * non-static. All of them end in QueueStatusCondition (carry = queued). */
bool DoublePoisonEffect(void);   /* lb bc, CNF_SLP_PRZ, DOUBLE_POISONED / jr QueueStatusCondition */
bool ParalysisEffect(void);      /* lb bc, PSN_DBLPSN, PARALYZED / jr QueueStatusCondition */
bool ConfusionEffect(void);      /* lb bc, PSN_DBLPSN, CONFUSED / jr QueueStatusCondition */
/* TossCoin_BankB(SleepCheckText); ret nc (tails: carry clear); then SleepEffect (carry = queued) */
bool Sleep50PercentEffect(void);

/* data tables of part 1's range (effect_functions.asm line 1419), used by
 * StrangeBehavior_SelectAndSwapEffect and Gigashock_PlayerSelectEffect with
 * InitializeMenuParameters. part1.c defines them non-static (.menu = TCG_MENU_PLAY_AREA):
 *   PlayAreaSelectionMenuParameters: cursor 0,0; y sep 3; MAX_PLAY_AREA_POKEMON items; SYM_CURSOR_R / SYM_SPACE; no func
 *   BenchSelectionMenuParameters:    cursor 0,3; y sep 3; MAX_PLAY_AREA_POKEMON items; SYM_CURSOR_R / SYM_SPACE; no func */
extern const struct MenuParams PlayAreaSelectionMenuParameters;
extern const struct MenuParams BenchSelectionMenuParameters;

/* ---- engine/duel/effect_functions.asm, part 2 range: PlayerPickAttackForAmnesia with its carry ----
 * effects.h declares PlayerPickAttackForAmnesia as void, but the original returns the
 * carry of HandleDefendingPokemonAttackSelection (set if the Player pressed B), which
 * SlowpokeAmnesia_PlayerSelectEffect and MirrorMove_InitialEffect2 return as their
 * EFFECTCMDTYPE_INITIAL_EFFECT_2 result (carry = the attack is cancelled).
 * part2.c defines this non-static: same effects as PlayerPickAttackForAmnesia
 * (DrawWideTextBox_WaitForInput(ChooseAttackOpponentWillNotBeAbleToUseText),
 * HandleDefendingPokemonAttackSelection, hTemp_ffa0 = e), returns that carry.
 * (If effects.h's PlayerPickAttackForAmnesia becomes bool, this is the same routine.) */
bool PlayerPickAttackForAmnesia_Carry(void);

/* ---- src/home/print_text.asm CopyText ----
 * input: hl = text id, de = destination. copies the TX_END-terminated text
 * (hl == 0: the turn duelist's name, via CopyOpponentName/CopyPlayerName
 * depending on hWhoseTurn) to de, including the TX_END.
 * output: de = address of the copied TX_END (the original ends with `dec de`).
 * side effects: only the bytes written at de (here: wDefaultText). */
u8 *CopyText(u16 text, u8 *de);

/* ---- src/home/duel_menus.asm HandlePeekSelection (-> engine/menus/duel.asm _HandlePeekSelection) ----
 * Player input for Peek: menu "Your Play Area / Opp. Play Area" (B ignored), then a
 * cursor over that play area: a prize card (only if not taken yet), the opponent's
 * hand or the deck; B goes back to the first menu. The chosen card is shown with
 * OpenCardPage_FromHand (wLoadedCard1 = that card), then it returns.
 * output: a = wce5c = $40 + prize index (prize card), the deck index of a random
 *   hand card (hand), or $7f (top card of the deck); bit 7 set if the viewed side
 *   was the non-turn duelist's (hWhoseTurn was swapped and is swapped back).
 * side effects: wce5c, wIsSwapTurnPending (0 on return), wYourOrOppPlayAreaCurPosition,
 *   wPrizeCardCursorTemporaryPosition, wTransitionTablePtr, wMenuInputSFX,
 *   wVBlankOAMCopyToggle, wDuelTempList (CreateHandCardList / CreateDeckCardList of
 *   the viewed duelist), RNG: the hand list is shuffled with ShuffleCards (one
 *   Random(n) per hand card), wLoadedCard1, wDuelDisplayedScreen (screens drawn).
 *   reads wCheckMenuPlayAreaWhichDuelist. hWhoseTurn is unchanged on return. */
u8 HandlePeekSelection(void);

/* ---- src/home/duel_menus.asm DrawAIPeekScreen (-> engine/menus/duel.asm _DrawAIPeekScreen) ----
 * input: a (copied to b); Peek passes hAIPkmnPowerEffectParam ^ $80:
 *   bit 7 clear -> draw the turn duelist's play area; bit 7 set -> SwapTurn and draw
 *   the other duelist's play area (swapped back at the end). low 7 bits: $7f = deck,
 *   bit 6 (AI_PEEK_TARGET_PRIZE) = prize card (index in bits 0-5), else the hand.
 * Draws that play area screen with the cursor on the peeked item. No input.
 * side effects: wIsSwapTurnPending (0 on return), wMenuInputTablePointer,
 *   wYourOrOppPlayAreaCurPosition, wVBlankOAMCopyToggle, wDuelDisplayedScreen;
 *   hWhoseTurn is swapped and restored. no outputs used by the caller. */
void DrawAIPeekScreen(u8 a);

/* ---- constants not generated into gen/constants.h ---- */
#ifndef PAD_B
#define PAD_B 0x02          /* 1 << B_PAD_B (src/constants/hardware.inc) */
#endif
#ifndef SYM_SPACE
#define SYM_SPACE 0x00      /* src/constants/charmaps.asm, TX_SYMBOL tiles */
#endif
#ifndef SYM_LIGHTNING
#define SYM_LIGHTNING 0x03
#endif
#ifndef SYM_CURSOR_R
#define SYM_CURSOR_R 0x0f
#endif

#endif
