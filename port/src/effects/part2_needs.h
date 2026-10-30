/*
 * Routines that effects/part2.c needs and that are not declared in the shared
 * headers yet (see notes/EFFECTS_BRIEF.md). To be implemented by the lead.
 */
#ifndef TCG_EFFECTS_PART2_NEEDS_H
#define TCG_EFFECTS_PART2_NEEDS_H

#include "../common.h"

/* ---- engine/duel/effect_functions.asm, part 1 range (lines 14-40) ----
 * Not referenced from effect command lists (so not in gen/effect_commands.h) and
 * not in effects.h, but part 2 calls them: part1.c must define them non-static.
 * Both end in QueueStatusCondition (carry = status queued). */
bool ParalysisEffect(void);      /* lb bc, PSN_DBLPSN, PARALYZED / jr QueueStatusCondition */
/* TossCoin_BankB(SleepCheckText); ret nc (tails: carry clear); then SleepEffect (carry = queued) */
bool Sleep50PercentEffect(void);

/*
 * WriteTwoDigitNumberInTxSymbol_PadSpace (engine/duel/core.asm, bank 1; called with bank1call)
 * input:  a = number to print (the hand/deck counters, < 100),
 *         b = x tile coordinate, c = y tile coordinate.
 * output: none used by callers (preserves bc, de and hl; a and flags are clobbered).
 * effect: converts a with TwoByteNumberToTxSymbol_PadSpace_Bank1, which writes 5 TX_SYMBOL
 *         digits (SYM_0 + digit, the first 4 leading zeros replaced by SYM_SPACE) and a TX_END
 *         byte to wStringBuffer[0..5], then copies the last two digits (wStringBuffer + 3) to
 *         BG map 0 at (b, c). The only WRAM side effect is wStringBuffer[0..5].
 * Used by Firegiver_AddToHandEffect to update the hand/deck counters of the duel HUD while
 * the cards are added to the hand (pure drawing apart from wStringBuffer).
 */
void WriteTwoDigitNumberInTxSymbol_PadSpace(u8 a, u8 b, u8 c);

#endif
