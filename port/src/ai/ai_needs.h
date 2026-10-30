/*
 * Non-AI routines/state the AI uses that are not declared in engine.h,
 * duel_ui.h or effects/effects.h. Implemented by the lead (not by the AI parts).
 *
 * Everything else the AI calls outside its own files is already declared:
 *   engine.h      home/duel.asm, home/substatus.asm, home misc (Random, HtimesL,
 *                 TryExecuteEffectCommandFunction, CheckMatchingCommand, LoadDeck ...),
 *                 core.asm (AIMakeDecision, CheckIfActiveCardParalyzedOrAsleep,
 *                 GetCardOneStageBelow)
 *   duel_ui.h     DoFrame (pkmn_powers.asm delay loops), TossCoin (AIPlay_Recycle,
 *                 AIPlay_Pokeball: register a = 1 heads / 0 tails = the bool result)
 *   effects.h     CreateEnergyCardListFromDiscardPile_AllEnergy
 *
 * Not needed in C (control flow / banking, no observable state in the port):
 *   BankswitchROM, farcall/bank1call (hBankROM is not modeled; all registers and
 *   flags pass through unchanged, so they are plain calls), JumpToFunctionInTable
 *   (AIDoAction, AIPerformScriptedTurn: index a C function-pointer table),
 *   CallIndirect (_AIProcessHandTrainerCards: call the AITrainerCardLogic entry's
 *   decide/play pointer).
 */
#ifndef TCG_AI_NEEDS_H
#define TCG_AI_NEEDS_H

#include "common.h"

/* ---------------------------------------------------------------- SRAM (src/sram.asm, home/sram.asm)
 * Original (engine/duel/ai/core.asm, CheckIfNotABossDeckID):
 *     call EnableSRAM
 *     ld a, [sReceivedLegendaryCards]   ; SRAM $a00a
 *     call DisableSRAM
 * Returns the byte the original reads there: nonzero once the player has received
 * the four legendary cards (written by engine/starter_deck.asm and home/map.asm).
 * When nonzero, every deck is treated like a boss deck by the AI (no random skipping
 * of Trainers/Pkmn Powers, boss retreat/energy logic), so it changes AI decisions:
 * it must come from the duel setup (e.g. a TcgDuelSetup field), not be hard-coded.
 * No side effects (EnableSRAM/DisableSRAM only write the MBC register rRAMG, which
 * the port does not model). */
u8 GetSRAMReceivedLegendaryCards(void);

#endif
