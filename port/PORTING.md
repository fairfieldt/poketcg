# Porting conventions

The C port is a *faithful* translation of the pret disassembly's duel engine.
Readers should be able to put an asm file and its C file side by side.

## Names and files

* Every ported routine keeps its asm label as its C name (`DrawCardFromDeck`,
  `HandleSandAttackOrSmokescreenSubstatus`, ...). Local labels (`.loop`) become
  loops/gotos or `static` helpers named `<Function>_<label>`.
* One C file per asm file (or per big section of one), named after it:
  `src/home/duel.asm` -> `port/src/home_duel.c`, `engine/duel/ai/energy.asm`
  -> `port/src/ai/energy.c`, and so on. Declarations go in the matching header.
* Keep the original comments that explain *why*, and keep `; bug` notes.
  Never fix bugs. If you notice one that isn't documented, add a
  `/* bug (original): ... */` comment and keep the behaviour.

## State

* All engine state lives in `gWRAM` / `gHRAM`, with the original label names
  available as macros from `gen/wram.h` (`wDuelTurns`, `hWhoseTurn`,
  `wLoadedCard1.HP`, `wDuelTempList[i]`, ...). Do not introduce new globals
  for state the original keeps in WRAM/HRAM. Scratch values the original only
  keeps in registers are ordinary C locals.
* Size 1 labels are `u8` lvalues; bigger labels are `u8 *`; `wLoadedCard1/2`
  and `wLoadedCard*Atk*`/`wLoadedAttack` are typed structs (see common.h).
  16-bit values are little-endian: use `rd16()` / `wr16()`.
* Duel variables: `GetTurnDuelistVariable(off)` returns the value;
  `TurnDuelistVars()[off]` / `NonTurnDuelistVars()[off]` give the block, which
  is the idiomatic replacement for `ld l, DUELVARS_X` after a GetTurnDuelistVariable.
* Pointers stored in WRAM are kept as original Game Boy addresses
  (`gbptr()`/`gbaddr()` for WRAM/HRAM; effect command lists by their bank 6
  address, see `GetEffectCommandList`).

## Calling conventions

* Register inputs become parameters (comment which register each one was).
* The carry flag result becomes a `bool` return value (`true` == carry set).
* A value returned in a register that callers use becomes the return value, or
  an out-parameter when the function also returns carry.
* **Implicit outputs matter.** Asm callers often use whatever a callee left in
  a register (`call LoadCardDataToBuffer1_FromDeckIndex` leaves the card ID in
  `a` and `e`). When porting a caller, check what it reads after each call and
  make sure the C callee provides it.
* 8-bit arithmetic must wrap like the original: use `u8` for values that are
  `u8` in the original (HP, damage low byte, counters), and do 16-bit damage
  math with explicit `u16`/byte handling exactly as the asm does.
* Random numbers: only through `Random(a)` / `UpdateRNGSources()`. The number
  and order of calls must match the original exactly.

## UI

The engine does not draw. Every original routine that draws, prints, animates,
plays sound or waits for input is implemented in `duel_ui.c` as a *shim* with
the original name. A shim:

1. reproduces the routine's side effects on engine state that later logic can
   observe (e.g. `wDuelDisplayedScreen`, cards it loads into `wLoadedCard1/2`,
   `wTxRam*`, `hTemp*` outputs of menus), and
2. reports to the frontend through the host interface (`tcg.h`): an event for
   output, or a blocking request for player input.

When porting logic that calls a UI routine, call the shim (add one to
`duel_ui.c`/`duel_ui.h` if it does not exist yet — read the original routine
to find its observable side effects first).

## Tracing

Put `TRACE_FN();` as the first statement of routines that are good
synchronisation points for the differential harness (all effect functions,
OppAction_*, AI entry points, public duel helpers). It compiles to nothing in
normal builds.
