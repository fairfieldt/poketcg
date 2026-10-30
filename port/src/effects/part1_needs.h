/*
 * Routines used by effects/part1.c that the port does not declare yet (or
 * declares with a signature that lacks the register inputs part 1 needs).
 * Not implemented here: see the comments for what the original does.
 */
#ifndef TCG_EFFECTS_PART1_NEEDS_H
#define TCG_EFFECTS_PART1_NEEDS_H

#include "../engine.h"

/*
 * PrintAndLoadAttacksToDuelTempList (engine/duel/core.asm)
 * input:  none (uses the turn holder's arena card)
 * output: returns a = number of non-empty, non-Pokemon Power attacks (0-2)
 * effects: DrawWideTextBox; hTempCardIndex_ff98 = turn holder's DUELVARS_ARENA_CARD;
 *   LoadCardDataToBuffer1_FromDeckIndex(that card); wCardPageNumber = 0;
 *   for each attack slot whose name is non-zero and whose category
 *   (& ~RESIDUAL) != POKEMON_POWER, appends the pair <deck index>, <0 or 1>
 *   to wDuelTempList (no $ff terminator is written) and prints the attack
 *   (PrintAttackOrPkmnPowerInformation, drawing only).
 * Used by HandleDefendingPokemonAttackSelection with the TCG_MENU_ATTACKS menu.
 */
u8 PrintAndLoadAttacksToDuelTempList(void);

/*
 * SerialRecvByte (home/serial.asm)
 * output: carry (true) if no byte is available; *a = register a
 * effects: if [wSerialRecvCounter] != 0: decrements wSerialRecvCounter,
 *   *a = wSerialRecvBuf[wcba3], wcba3 = (wcba3 + 1) & $1f, returns nc.
 *   otherwise: if [wSerialFlags] != 0 returns nc with *a = [wSerialFlags],
 *   else returns carry with *a = 0.
 * The original caller (DuelistSelectForcedSwitch, link opponent) loops
 * `halt` until it returns nc, so the port's stub must eventually deliver.
 */
bool SerialRecvByte(u8 *a);

/*
 * CheckIfEnoughEnergiesForGivenAttack (engine/duel/core.asm) with its register
 * inputs. engine.h declares CheckIfEnoughEnergiesForGivenAttack(void), but the
 * original takes:
 *   d = deck index of the card, e = attack index (FIRST_ATTACK_OR_PKMN_POWER
 *   or SECOND_ATTACK), plus wAttachedEnergies / wTotalAttachedEnergies
 * output: carry if the attack slot is empty, is a Pokemon Power, or there are
 *   not enough energies; nc otherwise. de is preserved.
 * effects: LoadCardDataToBuffer1_FromDeckIndex(d); wAttachedEnergiesAccum
 *   (sum of the colored energy requirements checked so far).
 * Used by AIPickAttackForAmnesia.
 */
bool CheckIfEnoughEnergiesForGivenAttack_DE(u8 d, u8 e);

#endif
