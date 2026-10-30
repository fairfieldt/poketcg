# Portable C port of the Pokémon TCG duel engine

This directory is a faithful C port of the duel engine of Pokémon Trading Card
Game (Game Boy Color, 1998), translated from the pret disassembly in this
repository. Everything the original duel engine does is ported, bugs included:
the rules, all 228 cards and their effects, every AI deck with its special
rules (boss deck setups, scripted Sam turns, the AI's RNG cheats...), and the
practice duel. The goal is 100% behavioural compatibility, which is checked
mechanically against the original ROM (see *Testing* below).

```
port/
  src/            the engine (C11, no OS dependencies)
    gen/          data generated from the disassembly (cards, decks, texts, effect tables, RAM layout)
    effects/      engine/duel/effect_functions.asm (674 routines)
    ai/           engine/duel/ai/*.asm
    tcg.h         the frontend interface
  frontends/
    cli/          terminal frontend (desktop)
    web/          WebAssembly build, browser UI, deck builder, relay server for online duels
    n64/          Nintendo 64 frontend (libdragon)
  harness/        differential test harness (Game Boy emulator + lockstep driver)
  tools/          generators (gen_*.py) and helpers
  notes/          porting briefs and open questions
```

## Design

**Memory model.** The original keeps all its state in Game Boy WRAM and HRAM.
The port keeps the same layout in two byte arrays (`gWRAM`, `gHRAM`) and
exposes every original label as a macro with the same name (`wDuelTurns`,
`hWhoseTurn`, `wLoadedCard1.HP`, `wDuelTempList[i]`...), generated from
`src/wram.asm`. This gives byte-for-byte comparable state, identical
behaviour for out-of-bounds quirks, and free snapshots. Data comes from the
built ROM itself (`tools/gen_data.py`), so card records, deck lists and effect
command tables are byte-identical, including the bytes the original reads past
the end of short records.

**Faithful structure.** Every routine keeps its asm label as its C name and is
in a file named after its asm file (`home/duel.asm` → `home_duel.c`,
`engine/duel/ai/energy.asm` → `ai/energy.c`). Registers become parameters and
return values, the carry flag becomes a `bool` return value, and the
original's comments (and `; bug` notes) are kept. See `PORTING.md`.

**UI.** The engine does not draw. The original routines that draw, print,
animate or read the joypad are *shims* (`duel_ui.c`) that keep the original's
observable side effects on engine state (the HUD and card list routines, for
example, load cards into `wLoadedCard1/2`, which later logic can observe) and
report to the frontend through two callbacks:

* `event()` — text boxes (already formatted, with `<RAMNAME>`/`<RAMTEXT>`/
  `<RAMNUM>` substituted), screens, animations, coin tosses, sounds.
* `request()` — a blocking question for a human, one type per original input
  routine: duel menu, card list, play area cursor, yes/no, energy discard,
  prize selection, ...

The board is read straight from engine memory (`tcg_duelvars()`, ...).

**Determinism and network play.** A duel is a pure function of the decks, the
RNG seed and the answers to `request()`. Online play runs the same engine on
both clients and only exchanges answers (`frontends/web/server`). Text-box
acknowledgements never change state and stay local.

## Building

```bash
make            # build/libtcg.a
```

The generated sources in `src/gen` are committed. To regenerate them, build
the ROM at the repository root (`make`, needs RGBDS) and run `make gen` here.

Frontends: see `frontends/*/README.md`. The terminal frontend:

```bash
cc -std=c11 -O2 -Isrc -o build/tcgcli frontends/cli/tcgcli.c src/*.c src/gen/*.c src/effects/*.c src/ai/*.c
./build/tcgcli --p1 human --p2 ai:12
```

## Testing

Compatibility is checked against the original game, running the ROM built
from this repository in a small Game Boy emulator (`harness/gb`).

* `harness/unitdiff` calls individual routines of the original ROM and of the
  port on identical random duel states and compares registers and all of
  WRAM/HRAM.
* `harness/difftest` runs whole duels in the original and in the port in
  lockstep. Every routine of the port that calls `TRACE_FN()` is a
  breakpoint in the original; at each of them both must have reached the same
  routine with identical engine state, and every random number drawn must be
  the same. AI-vs-AI duels cover all AI decks; duels with a human side use a
  seeded random policy whose answers are fed to both the port (through
  `request()`) and the original (as joypad presses).

```bash
make test       # needs ../poketcg.gbc and ../poketcg.sym (run `make` at the repository root)
```
