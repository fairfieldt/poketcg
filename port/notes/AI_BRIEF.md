# Brief: porting the duel AI (engine/duel/ai/)

You are one of four agents porting the AI of the pret poketcg disassembly to C,
as part of a faithful C port of the duel engine in `/home/user/poketcg/port`.
Faithful means: same control flow, same RAM writes (WRAM and HRAM must end up
byte-identical to the original's), same RNG calls in the same order, same bugs.
Do not fix or "clean up" behaviour.

Read first: `port/PORTING.md`, `port/src/common.h`, `port/src/engine.h`,
`port/src/duel_ui.h`, `port/src/effects/effects.h`, and **`port/src/ai/ai.h`**
and `port/src/ai/ai_needs.h` (the interface you implement against). Skim
`port/src/home_duel.c`, `port/src/substatus.c`, `port/src/home_misc.c`: they show
the style and the exact C signatures of the home routines you call.

## Your output

One C file per asm file, in `port/src/ai/` (the Makefile builds `src/ai/*.c`;
no subdirectories), routines in the same order as the asm, asm comments kept.

| part | writes | from |
|------|--------|------|
| A | `ai/home_ai.c` | `src/home/ai.asm` (LoadOpponentDeck, AIDoAction*, engine.h signatures; they replace the weak stubs in `src/ai_stubs.c`) |
| A | `ai/core.c` | `engine/duel/ai/core.asm` itself (not its INCLUDEs) |
| A | `ai/init.c`, `ai/damage_calculation.c`, `ai/boss_deck_set_up.c` | the files of the same name |
| A | `ai/deck_ai.c` | `deck_ai.asm` + every `decks/*.asm` in INCLUDE order, plus `decks/unreferenced.asm` (INCLUDEd at the top of core.asm); one `/* ---- decks/xxx.asm */` section each |
| A | `ai/deck_ai_pointers.c`, `ai/ai_trainer_card_logic.c` | `src/data/deck_ai_pointers.asm`, `src/data/duel/ai_trainer_card_logic.asm` |
| B | `ai/trainer_cards.c` | `trainer_cards.asm` (6000 lines: budget accordingly) |
| C | `ai/retreat.c`, `ai/hand_pokemon.c`, `ai/energy.c`, `ai/attacks.c`, `ai/special_attacks.c` | same names |
| D | `ai/pkmn_powers.c`, `ai/common.c` | same names |

* Every global label of your files is declared in `ai/ai.h`: implement it with
  **exactly** that signature (non-static). Locals (`.loop`) become loops/gotos or
  `static` helpers named `<Function>_<label>` (e.g. `AIPickPrizeCards_PickPrizeCard`,
  `AIProcessAndTryToPlayEnergy_has_logic_flags` for `Func_16488`'s jump into it).
* Each ai.h comment documents the contract callers rely on, including implicit
  register outputs (several are odd: read them). If you find a comment or
  signature that is wrong for a routine you implement, **do not edit ai.h**:
  implement the behaviour the asm has, as close to the declaration as possible,
  and report it with the asm evidence.
* Calls into other parts go through ai.h; the other parts are written in
  parallel, so you only need the declarations to compile.
* If you need something declared nowhere (a core.asm or UI routine not in
  engine.h / duel_ui.h / effects.h / ai_needs.h), declare it in
  `port/src/ai/partX_needs.h` (X = your part letter) with a precise comment:
  original name and file, inputs, outputs (incl. carry and registers callers
  use), side effects on WRAM/HRAM. Include it from your .c files. Do not
  implement such shared routines yourself.
* **Do not edit any shared header** (common.h, engine.h, duel_ui.h, tcg.h,
  effects/*.h, gen/*, ai/ai.h, ai/ai_needs.h) or other agents' files, and do not
  touch tools/rom_blobs.txt (ask in your report instead).
* Do not run git commands that modify the repo (no add/commit/stash/checkout).
* `#include "ai/ai.h"` (it pulls in engine.h, effects.h, duel_ui.h, gen/rom_blobs.h,
  ai_needs.h).

## Calling conventions (PORTING.md, applied to the AI)

* carry = `true`. Register inputs are parameters in register order (a, b, c, d,
  e, hl). `ret c` after a bool call -> `if (X()) return true;`. `jp Foo` at the
  end -> `return Foo(...);`. Fallthrough into the next label = call it.
* **Out-parameters are never NULL** and must be written on every return path
  with the exact value the register holds at `ret` in the original, unless the
  ai.h comment says "only on carry" / "unchanged". Callers store several of them
  to RAM even on failure (`call LookForCardIDInHand / ldh [hTemp_ffa0], a / jr nc`).
  Pass a dummy local when you only need the carry.
* `AIDecide_*` (AITrainerDecideFunc): `*a` = register a at return. It ends up in
  `wAITrainerCardParameter` whenever you return true, so track a precisely
  (also when the asm comment does not say what a holds). Routines tail-jumped
  from a decide routine (AIDecide_ComputerSearch_*, AIDecide_PokemonTrader_*,
  PickPokedexCards) have the same contract.
* `farcall` / `bank1call` / `call` are all plain calls: farcall preserves every
  register and flag in both directions. Bank switching (BankswitchROM, hBankROM)
  is not modelled; drop it.
* 8-bit math wraps: `u8` locals for 8-bit registers (`sub` underflow, `add`
  overflow, `inc` of counters). Keep 16-bit damage in `u16` with the asm's operations.
* Scratch WRAM (wTempAI, wce06, wce08[], wce0f[], wce1a.., wCurCardCanAttack,
  wTempCardIDToLook, ...) must be written exactly where the asm writes it,
  even when the value is only used inside the routine: it is compared against
  the original. Register-only values are C locals.
* The asm often temporarily patches duel vars (e.g. `CheckForEvolutionInList`
  stores a card in DUELVARS_ARENA_CARD and restores it): do the same through
  `TurnDuelistVars()`. `ld a, X / call GetTurnDuelistVariable` followed by
  `[hli]` loops -> a `u8 *hl = TurnDuelistVars() + X` pointer; mind that
  GetTurnDuelistVariable *replaces* hl (special_attacks.asm Earthquake bug).
* Implicit outputs of home routines you will meet:
  `GetCardIDFromDeckIndex` preserves a, returns the ID (register e) and sets d = 0;
  `LoadCardDataToBuffer1/2_FromDeckIndex` return the ID in a only (de/bc/hl preserved);
  `CreateHandCardList(&n)` count in a; `CountCardsInDuelTempList` always counts
  wDuelTempList whatever hl is; `PutHandPokemonCardInPlayArea(card, &loc)`
  leaves the new location in a; `TossCoin` leaves a = 1 (heads) / 0 (tails);
  `GetCardDamageAndMaxHP(e, &maxhp)` damage in a, max HP in c;
  `ApplyDamageModifiers_DamageToSelf()` returns de;
  `CheckIfCanEvolveInto(d, e, &a)`; `GetCardOneStageBelow(&d, &e)` (input
  hTempPlayAreaLocation_ff9d). Check every call for what the asm reads afterwards.
* Text IDs (`ldtx de, TrainerCardSuccessCheckText` before TossCoin) are the asm
  label names from gen/text.h.

## AI-specific conventions

### Entering the AI, deck AI tables (part A)

The duel core enters the AI only through `AIDoAction_*` (engine.h). In C:

```c
u8 AIDoAction(u8 action)          /* ai/home_ai.c */
{
	TRACE_FN();
	if (action == 0)              /* would CopyDeckData from the table's first word: never used */
		tcg_fatal("AIDoAction(0)");
	if (wOpponentDeckID > NUM_DECK_IDS)
		tcg_fatal("AIDoAction: wOpponentDeckID out of range");
	return DeckAIPointerTable[wOpponentDeckID][action]();
}
```

Each `AIActionTable_X` (ai.h) is `const AIActionFunc[6]` indexed by AIACTION_*;
entry 0 (the "unused" `dw .do_turn`) also holds the do_turn function. The
`.do_turn` / `.start_duel` / ... locals are `static u8 AIActionTable_X_do_turn(void)`
etc. in deck_ai.c and return register a: the forced_switch / ko_switch value
(AIDecideBenchPokemonToSwitchTo's `*a`, PickRandomBenchPokemon,
GetPlayAreaLocationOfRaticateOrRattata) matters; the others may return 0.
`DeckAIPointerTable` has NUM_DECK_IDS + 1 entries: the last one is
`&AIActionTable_Unreferenced[1]`, which is what the original reads one entry past
its table (see ai.h). AIPerformScriptedTurn's `.scripted_actions_list` is a
`static void (*const[7])(void)` table indexed by wDuelTurns / 2.

### wOpponentDeckID-dependent logic

Besides the table dispatch, many routines compare `wOpponentDeckID` against
`*_DECK_ID` constants (gen/constants.h). Keep them as if/else chains in the asm's
order, reading `wOpponentDeckID` where the asm does.

### Trainer card logic table (parts A and B)

`AITrainerCardLogic` (ai/ai_trainer_card_logic.c, part A) is
`const struct AITrainerCardLogicEntry[38 + 1]`, the asm rows in order, then
`{ 0xff }` as terminator. `_AIProcessHandTrainerCards` (part B) walks it with
an index instead of hl, but keeps the asm's exact flow: the Switch /
AI_FLAG_USED_SWITCH check, CheckCantUseTrainerDueToEffect, the
INITIAL_EFFECT_1 command, AIChooseRandomlyNotToDoAction, `decide(&a)` ->
`wAITrainerCardParameter = a`, OPPACTION_PLAY_TRAINER (its carry skips the play
function and moves on to the next row, like `jr c, .inc_hl_by_2`), `play()`, the
flag merge and the hand re-listing on AI_FLAG_MODIFIED_HAND.

### ROM lists behind WRAM pointers (wAICardList*)

The six `wAICardList*` WRAM words hold original Game Boy addresses (bank $05)
of the deck lists, so RAM stays byte-identical. The lists are raw ROM blobs
generated from `port/tools/rom_blobs.txt` into `gen/rom_blobs.c` (one line per
label, local labels as in poketcg.sym: `AIActionTable_LegendaryMoltres.list_prize`;
the generator is `python3 port/tools/gen_data.py`, run from the repo root). All
lists that any deck stores are already listed. Use the ai.h helpers:

```c
/* store_list_pointer wAICardListAvoidPrize, .list_prize */
StoreListPointer(wAICardListAvoidPrize, "AIActionTable_LegendaryMoltres.list_prize");

/* ld a, [wAICardListRetreatBonus + 1] / or a / jr z, .store_score / ld h, a / ... */
if (wAICardListRetreatBonus[1] == 0)
	goto store_score;
const u8 *hl = AIListPtr(rd16(wAICardListRetreatBonus));
```

Test for NULL exactly where (and how) the asm does: TrySetUpBossStartingPlayArea
tests the high byte of the *address* of the variable (always nonzero, bug), and
SetUpBossStartingHandAndDeck's `.CheckIfIDIsInList` stops at the first byte
(`cp a` bug). Both must stay. Tables the AI reads directly (not through a RAM
pointer: `.prize_flags`, ConvertColorToEnergyCardID's `.card_id`, Sam's script
table, ...) are plain `static const` arrays in your file. If you find another
pointer-valued RAM variable pointing into ROM, report it (the lead adds the label).

### AIMakeDecision

`ld a, OPPACTION_X / bank1call AIMakeDecision` -> `AIMakeDecision(OPPACTION_X)`
(engine.h; implemented by the lead in the duel core). It returns carry when the
turn or the duel ended. Set the HRAM/WRAM parameters the OppAction handler reads
(hTemp_ffa0, hTempPlayAreaLocation_ffa1, hTempCardIndex_ff9f, hTempRetreatCostCards,
hAIPkmnPowerEffectParam, hAIEnergyTransPlayAreaLocation, hAIEnergyTransEnergyCard,
hPlayAreaEffectTarget, ...) exactly as the asm does before each call, and test
or ignore its carry exactly as the asm does (`ret c`, `jr c`, `or a / ret`).

### RNG

Only through `Random(n)` (and the home routines that use it: ShuffleCards,
ShuffleDeck, TossCoin...). Same number of calls, same order, same arguments.
The RNG state lives in WRAM (`wRNG1`, `wRNG2`, `wRNGCounter`), so the RNG cheats
are plain WRAM writes and must be kept exactly:
* `AIPlay_Gambler` (part B): back up wRNG1/wRNG2/wRNGCounter into wce06 /
  wce08[0] / wce0f[0], write $50 to all three (order wRNGCounter, wRNG2, wRNG1),
  AIMakeDecision(OPPACTION_EXECUTE_TRAINER_EFFECTS), restore; not for IMAKUNI_DECK_ID.
* `LoadOpponentDeck` (part A): Sam's duels seed all three with $57.

### Other

* SRAM: `CheckIfNotABossDeckID` reads `sReceivedLegendaryCards` through
  `GetSRAMReceivedLegendaryCards()` (ai_needs.h); drop EnableSRAM/DisableSRAM.
* Delay loops (`ld d, 30 / .loop / call DoFrame / dec d / jr nz`): keep the same
  number of `DoFrame()` calls (a `for` loop); frames are observable state
  (wVBlankCounter drives AIMakeDecision's "thinking" delay) and pacing for the frontend.
* The AI does no drawing of its own; AIMakeDecision and the OppAction handlers do.
* Keep `; bug` comments and add `/* bug (original): ... */` for undocumented ones.
* `TRACE_FN();` as the first statement of every routine declared in ai.h, and of
  the deck action functions, except these leaves: AIEncourage, AIDiscourage,
  ConvertHPToDamageCounters_Bank5/8, CalculateBDividedByA_Bank5/8,
  CalculateWordTensDigit, ClearMemory_Bank5/8, CopyListWithFFTerminatorFromHLToDE_Bank5/8,
  CheckIfEnoughParticularAttachedEnergy, CalculateParticularAttachedEnergyNeeded,
  ConvertColorToEnergyCardID, RemoveCardFromList, FindAndRemoveCardFromList,
  RetrievePlayAreaAIScoreFromBackup1/2.

Example (decks/general.asm):

```c
/* handles AI retreating logic */
void AIProcessRetreat(void)
{
	TRACE_FN();
	u8 a;
	if (wAIRetreatedThisTurn)
		return; /* already retreated this turn */
	if (!AIDecideWhetherToRetreat())
		return; /* not retreating */
	if (AIDecideBenchPokemonToSwitchTo(&a))
		return; /* no Bench Pokemon */
	/* store Play Area to retreat to and set wAIRetreatedThisTurn to true */
	wAIPlayAreaCardToSwitch = a;
	wAIRetreatedThisTurn = TRUE;
	/* if AI can use Switch from hand, use it instead... */
	AIProcessHandTrainerCards(AI_TRAINER_CARD_PHASE_09);
	if (!(wPreviousAIFlags & AI_FLAG_USED_SWITCH)) {
		/* ... else try retreating normally. */
		AITryToRetreat(wAIPlayAreaCardToSwitch);
		return;
	}
	/* if AI used switch, unset its AI flag */
	wPreviousAIFlags &= ~AI_FLAG_USED_SWITCH;
	HandleAIEnergyTrans(AI_ENERGY_TRANS_RETREAT);
}
```

## Checking your work

Compile each file alone from `port/`:
`cc -std=c11 -Wall -Wextra -Wno-unused-parameter -Isrc -c src/ai/<file>.c -o /dev/null`
It must compile without errors or warnings. Linking only works once all parts
exist, so don't try to link. The lead will run differential tests against the
original ROM afterwards; correctness of every branch (and every register value
that reaches RAM) matters more than speed: re-read each routine against the asm
before moving on.

## Report (keep it short)

1. Routines ported (count) and any you could not finish.
2. Contents of your `partX_needs.h` (one line each).
3. Non-static helpers not in ai.h (there should be none) and any ROM label you
   need added to rom_blobs.txt.
4. ai.h signatures/comments you found wrong or awkward (with the asm lines),
   anything ambiguous, and suspected original bugs you preserved.
