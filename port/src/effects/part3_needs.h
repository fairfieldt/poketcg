/*
 * Routines used by effects/part3.c (effect_functions.asm lines 5540-8420) that
 * are not declared in any shared header yet (or whose shared declaration does
 * not expose the register inputs the original passes). To be provided by the
 * lead / part 1. Nothing here is implemented in part3.c.
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

/* ---- engine/duel/core.asm CheckIfEnoughEnergiesForGivenAttack, with its inputs ----
 * engine.h declares CheckIfEnoughEnergiesForGivenAttack(void), but the original
 * takes d = deck index of the card (0-59), e = attack index
 * (FIRST_ATTACK_OR_PKMN_POWER / SECOND_ATTACK), plus wAttachedEnergies and
 * wTotalAttachedEnergies. output: carry if the attack is unusable (no attack /
 * Pokemon Power) or there are not enough energies.
 * side effects: wLoadedCard1 = card d (LoadCardDataToBuffer1_FromDeckIndex),
 *   wAttachedEnergiesAccum. preserves de.
 * If engine.h gets the (u8 d, u8 e) signature, this can become a plain alias. */
bool CheckIfEnoughEnergiesForGivenAttack_DE(u8 d, u8 e);

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
