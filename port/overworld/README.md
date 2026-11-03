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

## Port-only cat encounter

After the first club medal, Poppy (tuxedo) and Pepper (tabby) stand at (12,8) and (14,8) in
Ishihara's House. Both render at half their authored size, with conversation
framing adjusted for their height. Either NPC starts the same six-prize duel. Their IDs (240/241),
shared deck ID (120), text IDs and names live in `src/cat_duo.h`.
The original ROM-generated NPC, script and deck tables remain unchanged.

The first victory gives Flying Pikachu; later victories give a Lightning
Colosseum pack and a Lightning/Colorless Mystery pack. The shared first-win flag
uses bit 0 of event byte 62, outside the extracted original event masks, and is
encoded in the existing save layout. A pending duel uses the normal `OwDuel`
record and resume path; both NPCs share the reward flag across diary sessions.

`src/cat_duo.c` defines the 60 cards: Meowth/Persian, Electabuzz Lv35, Scyther,
Kangaskhan, Mew Lv8 and Mew Lv23, with Lightning/Psychic/Double Colorless Energy. `src/ai/cat_duo_ai.c` supplies a randomized
opening hand containing Meowth, Electabuzz Lv35 and Lightning Energy, places
Electabuzz Active and Meowth on the Bench, and adds conservative search/recovery
choices for Computer Search and Item Finder. Later turns use the existing general
AI with boss-level Trainer reliability. All custom decisions are gated by deck ID.

`make -C port catduotest` checks this encounter, deck, AI opening, search choices,
rewards and saved-duel resume. The N64 frontend's normal test target includes it.
`frontends/n64/adventure/tools/review_cats.py` captures the real room and both
conversation cameras with the production renderer in ares using 4 MiB RAM.

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
