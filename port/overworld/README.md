# Portable out-of-duel engine

`overworld.c` runs the original story and maps through synchronous host callbacks,
independent of the N64 renderer and the portable duel engine. `overworld.h` is the
public interface. The host owns its `GpState` card/deck collection and connects
inventory, boosters, starter decks, menus, dialogue, duels, audio and saves using
`OwHost`.

The extraction tool retains all 104 original story opcodes and their data from
ROM bank 3. It also translates 940 native operations from `src/scripts/*.asm` and
the map-load wrapper into a source control-flow table. Every source label is
checked against the linked disassembly's symbol addresses; extraction fails on
an address mismatch. This covers native NPC preloads, encounter conditions,
movement triggers, and after-duel script dispatch, rather than bypassing them.
The runtime is a game-script interpreter, not a Game Boy machine emulator.

All map permissions, NPC headers and placements, interactable objects, map
handlers, warp connections, event bit masks, text IDs, and source NPC names are
extracted as generated C data. Normal ROM builds consume the checked-in data
and do not need the original ROM. To regenerate, build the original ROM and
symbols first, then run `make gen` here.

## Host integration

- `ow_new_game` runs the opening story, practice duel and starter selection.
- `ow_continue` starts a normal saved-game session, including original temporary
  event reset, Imakuni relocation and Challenge Cup scheduling.
- `ow_move` uses original two-tile steps, permission maps, NPC collisions,
  movement scripts, exact map-event permission patches, and warps (including
  scripted door movement). `ow_interact` runs NPC or object scripts.
- `ow_enter_location` selects an overworld destination; the original cursor graph and all 144 waypoint routes are also available.
  Ishihara's House is accessible before its name is revealed, as in the source.
- `ow_finish_duel` dispatches original win/loss scripts. Before the duel callback,
  `duel_pending`, `pending_duel`, `duel_npc`, and `script_pc` are available to save.
  A saved duel resumes through the existing duel engine, then this function.
- `ow_save_encode`/`ow_save_decode` use explicit byte encoding for integers and
  fixed byte-only actor layouts, so saves are portable across host endianness.
  Frontends should checksum the complete save containing both OwState and GpState.

A feature callback returns true when it supplied the capability. An already
unlocked PC mail item is a successful idempotent operation. A missing capability
returns false and stops the story with an error; it is not silently skipped.
The save callback must use the supplied state: the ending deliberately supplies
an override that resumes the player in Mason's laboratory.

`txram[0..1]` contain the original two text-substitution slots (`wTxRam2` and
`wTxRam2_b`). `txram[2..3]` contain the two number-substitution slots. Text slots
contain text IDs, including card names, rather than card IDs.

## Validation

Run `make test`:

- The original opening sequence reaches practice, receives the starter, and
  resumes a saved pending duel through the correct after-duel story.
- 714 NPC encounters cover all maps, three progression states, and wins/losses.
- Native adapter tests cover every RAM alias byte, postincrement/decrement,
  preload memory changes, Challenge Cup history, medal-mail order, and 65,536
  RNG draws checked against the separately verified gameplay implementation.
- A campaign starts from a new `OwState` and real `GpState`, interacts with NPCs
  without injecting story flags, receives all eight medals, wins the four Grand
  Master duels and Ronald duel, obtains the four legendary cards and credits,
  and wins the postgame third Challenge Cup. It exercises 73 duel callbacks,
  exact booster/card rewards, starter delivery, session resets and prize rewards.
- Navigation tests cover every overworld waypoint route, the source cursor graph,
  unnamed Ishihara access, the locked/open Dome portal, and original map music.

The campaign test supplies duel outcomes; duel rules are independently exercised
by `port/`'s differential harness. External peer protocols, hardware peripherals,
menu rendering and audio playback are frontend capabilities and require their
own end-to-end validation.
