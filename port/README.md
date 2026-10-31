# Portable C port of the Pokémon TCG duel engine

This directory is a faithful C port of the duel engine of Pokémon Trading Card
Game (Game Boy Color, 1998), translated from the pret disassembly in this
repository. Everything the original duel engine does is ported, bugs included:
the rules, all 228 cards and their effects, every AI deck with its special
rules (boss deck setups, Sam's scripted turns, the AI's RNG cheats...), the
practice duel with Sam, and saving / continuing a duel. The goal is 100%
behavioural compatibility, which is checked mechanically against the original
ROM (see *Testing*).

The portable archive now also includes [story/map logic](overworld/README.md)
and [collection/deck/gameplay logic](gameplay/gameplay.h). The complete
[N64 adventure frontend](frontends/n64/adventure/README.md) connects these to
34 3D maps and the existing duel screen, with two local diaries. Its native
verification and N64 presentation/save/transport adaptations are documented
separately from the duel compatibility tests below.

```
port/
  src/            the engine (C11, no OS dependencies)
    gen/          data generated from the built ROM (cards, decks, texts, effect tables, RAM layout)
    effects/      engine/duel/effect_functions.asm
    ai/           engine/duel/ai/*.asm
    tcg.h         the frontend interface
  frontends/
    cli/          terminal frontend (desktop)
    web/          WebAssembly build, browser UI, deck builder, relay server for online duels
    n64/          Nintendo 64 frontend (libdragon)
    common/       save file interface shared by the frontends
  harness/        differential test harness (Game Boy emulator + lockstep driver)
  tools/          generators (gen_*.py), map renderer, helpers
  maps/           the game's maps rendered from the original graphics (see maps/README.md)
  notes/          porting briefs, decisions and questions
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
original's comments (and `; bug` notes) are kept. Where the original reads a
register that a callee happened to leave (an implicit output), the port
models that value explicitly. See `PORTING.md`.

**UI.** The engine does not draw. The original routines that draw, print,
animate or read the joypad are *shims* (`duel_ui.c`). They keep the
original's side effects on engine state that later logic can observe; the HUD
and card list routines, for example, load cards into `wLoadedCard1/2`. They
report to the frontend through two callbacks:

* `event()`: text boxes (already formatted, with `<RAMNAME>`/`<RAMTEXT>`/
  `<RAMNUM>` substituted), screens, animations, coin tosses, sounds.
* `request()`: a blocking question for a human, one type per original input
  routine. Examples: duel menu, card list, play area cursor, yes/no, energy
  discard, prize selection, color, Peek, A/B confirm.

The board is read straight from engine memory (`tcg_duelvars()`, ...).

**Determinism and network play.** A duel is a pure function of the decks, the
RNG seed and the answers to `request()`. Online play runs the same engine on
both clients and only exchanges answers (`frontends/web/server`), with
`tcg_state_hash()` to detect desyncs. Text-box acknowledgements and
view-only lists never change the duel and stay local.

**Saves.** The engine keeps the original's two SRAM duel slots (`sCurrentDuel`,
`sBackupCurrentDuel`) byte for byte, header and checksum included. It gives
them to the frontend (`host->save`) whenever the original would write SRAM.
`tcg_continue_duel` is the original's "Continue Duel". Frontends store the
file through `frontends/common/save_store.h`:

* desktop: files;
* N64: the cartridge EEPROM;
* browser: localStorage, with export/import through the File System Access API.

## The interface (`src/tcg.h`)

```c
tcg_set_host(&host);                    /* event / request / save callbacks */
tcg_run_duel(&setup);                   /* a whole duel; returns the result */
tcg_continue_duel(&setup, data, size);  /* resume a saved duel */
tcg_saved_duel_info(data, size, &info); /* what a save holds, for menus */
tcg_state_hash();                       /* for online desync checks */
tcg_wram(), tcg_duelvars(), tcg_card_data(), tcg_text(), tcg_format_text() ...
```

`TcgDuelSetup` gives each side a human or an AI (any of the game's AI decks),
a deck (an AI deck's list or 60 card ids), and the prizes, RNG seed and
music. It also takes the options `received_legendary_cards` (every AI plays
like a boss deck) and `skip_delay_allowed` (the original's debug option: hold
B to skip delays). The practice duel with Sam is started with the AI deck
`TCG_SAMS_PRACTICE_DECK_ID` and 2 prizes.

## Building

```bash
make            # build/libtcg.a
```

The generated sources in `src/gen` are committed. To regenerate them, build
the ROM at the repository root (`make`, needs RGBDS) and run `make gen` here.

The terminal frontend:

```bash
cc -std=c11 -O2 -Isrc -o build/tcgcli frontends/cli/tcgcli.c frontends/common/save_store_file.c src/*.c src/gen/*.c src/effects/*.c src/ai/*.c
```

```bash
./build/tcgcli --p1 human --p2 ai:12      # you against an AI deck
```

```bash
./build/tcgcli --p1 human --practice      # the practice duel with Sam
```

```bash
./build/tcgcli --p1 human --continue      # resume the saved duel
```

The browser and N64 frontends have their own READMEs:

* `frontends/web/README.md`: wasm build, UI, deck builder, online duels.
* `frontends/n64/README.md`: libdragon ROM, host tests, endianness check.

## Testing

Compatibility is checked against the original game, running the ROM built
from this repository in a small Game Boy emulator (`harness/gb`).

* `harness/unitdiff` calls individual routines of the original ROM and of the
  port on identical random duel states, and compares registers and all of
  WRAM/HRAM.
* `harness/difftest` runs whole duels in the original and in the port in
  lockstep.
  * **Sync points:** every routine of the port that calls `TRACE_FN()` is a
    breakpoint in the original, 815 of them. At each one, both must have
    reached the same routine with identical engine state, and every random
    number drawn must be the same.
  * **Human answers:** a seeded random policy answers human requests. Its
    answers go to both the port (through `request()`) and the original (as
    joypad presses, steered by the original's own cursor).
  * **Modes:**
    * AI vs AI: every AI deck.
    * Human vs AI.
    * `--random-decks`: random and themed decks (whole evolution lines with
      their energies) to reach cards no AI deck has.
    * `--focus`: decks built around cards that were still unreached.
    * The practice duel.
    * `--continue-at TURN`: duels saved by the port and continued by both
      the original (from SRAM) and the port.
  * `--coverage FILE` reports which sync points were reached.

```bash
make test       # a few hundred duels; needs ../poketcg.gbc and ../poketcg.sym (`make` at the repository root)
```

```bash
make test-long  # ten times as many
```

Results so far are identical in every mode, after fixing the divergences these
runs found:

* about 1300 AI-vs-AI and 1000 human-vs-AI duels over all decks, random decks
  and themed decks;
* 30 practice duels;
* about 30 continued duels;
* 77 core routines in `unitdiff`.

Every card effect and AI routine that the original can reach has been reached
in lockstep, except those listed in `notes/QUESTIONS.md`.

The known differences, all outside the duel logic, are listed in
`notes/QUESTIONS.md`:

* RAM written by viewing-only screens;
* the VRAM bytes that Gambler's and Recycle's card records overrun into;
* the general (overworld) save data, which the port doesn't have.
