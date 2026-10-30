# Brief: porting engine/duel/effect_functions.asm

You are one of four agents porting `src/engine/duel/effect_functions.asm` (pret
poketcg disassembly) to C, as part of a faithful C port of the duel engine in
`/home/user/poketcg/port`. Faithful means: same control flow, same RAM writes,
same RNG calls in the same order, same bugs. Do not fix or "clean up" behaviour.

Read first (they are short): `port/PORTING.md`, `port/src/common.h`,
`port/src/engine.h`, `port/src/duel_ui.h`, `port/src/tcg.h`,
`port/src/effects/effects.h`, and skim `port/src/gen/effect_commands.h`,
`port/src/home_duel.c` and `port/src/substatus.c` (already-ported code that
shows the style and the exact C signatures of the home routines you will call).

## Your output

* `port/src/effects/partN.c` — every routine defined in your line range of
  effect_functions.asm, in the same order, with the asm comments kept.
* Effect functions (those referenced from gen/effect_commands.h) keep exactly
  the signature `bool Name(void)` (return value = carry flag on return). Put
  `TRACE_FN();` as their first statement.
* Helpers listed in `effects/effects.h` for your part: implement with exactly
  that signature (non-static). Other helpers only used inside your part:
  `static`. If a non-effect helper of your part is used by another part but is
  not in effects.h, make it non-static and list it in your report.
* If you need a routine that is not declared anywhere (an engine/core.asm or UI
  routine the lead has not shimmed yet, a serial stub, ...), declare it in
  `port/src/effects/partN_needs.h` with a precise comment: original name and
  file, inputs, outputs (incl. carry and registers callers use), and its side
  effects on WRAM/HRAM. Include that header from partN.c. Do not implement such
  shared routines yourself and **do not edit any shared header** (common.h,
  engine.h, duel_ui.h, tcg.h, effects.h, gen/*) or other agents' files.
* Do not run git commands that modify the repo (no add/commit/stash/checkout).

## Rules of thumb

* `ld a, X / call GetTurnDuelistVariable` -> `GetTurnDuelistVariable(X)`; a
  following `ld l, Y` / `ld [hl], ...` on the same block -> `TurnDuelistVars()[Y]`
  (or `NonTurnDuelistVars()`); beware the block may change after SwapTurn.
* 8-bit math wraps: keep `u8` locals for values held in 8-bit registers. Keep
  16-bit damage in `u16` with the exact operations the asm uses.
* `ldtx hl, Text` right before returning carry -> `gTextID = Text; return true;`.
  `ldtx hl, Text / call DrawWideTextBox_WaitForInput` -> `DrawWideTextBox_WaitForInput(Text)`.
  Text IDs are the asm label names (enum in gen/text.h).
* Check every call for *implicit outputs* the asm uses afterwards (values left
  in a/b/c/d/e/hl by the callee, flags). If a home routine's C signature does
  not expose something the caller needs, compute it the same way locally and
  add a comment (and mention it in the report).
* Player selection code (the `REQUIRE_SELECTION` and other player branches):
  keep the logic (loops, validity checks, PlaySFX_InvalidChoice, which RAM gets
  written) and call the input shims in duel_ui.h (`DisplayCardList`,
  `OpenPlayAreaScreenForSelection`, `HandleMenuInput`, `YesOrNo*`,
  `HandleEnergyDiscardMenuInput`, ...). They block and return exactly what the
  original returns after an A/B press. Drop pure drawing (tilemap writes,
  FillRectangle, LoadDuelCardSymbolTiles, EnableLCD, palette code...). For
  custom cursor menus (`InitializeMenuParameters` with your own parameter
  table), port the parameter table as a `static const struct MenuParams` and
  set its `.menu` field (TCG_MENU_PLAY_AREA for play-area cursors, etc.).
* Link-opponent branches (DUELIST_TYPE_LINK_OPP, Serial*): keep the branch
  structure and call the serial stubs (ExchangeRNG, SetOppAction_SerialSendDuelData,
  SerialSend8Bytes, SerialRecv8Bytes; declare others in your needs header).
* Data tables in the asm (db/dw lists) become `static const` arrays.
* `bank1call`/`farcall`/`call` are all plain calls. `jp Foo` at the end = `return Foo();`.
  Fallthrough into the next label = call it (or `return` its result).
* Never call Random/UpdateRNGSources more or fewer times than the original.

Example (from the top of the file):

```c
bool Poison50PercentEffect(void)
{
	TRACE_FN();
	if (!TossCoin_BankB(PoisonCheckText))
		return false;
	return PoisonEffect();
}

bool PoisonEffect(void)
{
	TRACE_FN();
	return QueueStatusCondition(CNF_SLP_PRZ, POISONED);
}
```
(the `ret nc` after TossCoin returns with carry clear, hence `false`).

## Checking your work

Compile your file alone from `port/`:
`cc -std=c11 -Wall -Wextra -Wno-unused-parameter -Isrc -c src/effects/partN.c -o /dev/null`
It must compile without errors or warnings. Linking will only work once all
parts exist, so don't try to link. The lead will run differential tests
against the original ROM afterwards; correctness of every branch matters more
than speed — re-read each function against the asm before moving on.

## Report (keep it short)

1. Functions ported (count) and any you could not finish.
2. Contents of your needs header (one line each).
3. Non-static helpers not in effects.h that other parts may need.
4. Anything ambiguous, suspected original bugs you preserved, or places where
   a shared signature was awkward.
