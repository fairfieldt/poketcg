# Decisions and questions

Decisions made while porting, with the project owner's answers.

1. **Network play model.** The original's 2-player mode is the serial link
   duel (each Game Boy runs half the logic and they exchange OppActions and RNG
   state). The port instead runs the whole engine on both clients with both
   duelists as `DUELIST_TYPE_PLAYER` and only exchanges answers to `request()`
   (lockstep). Rules are identical; the link-specific code paths
   (DoLinkOpponentTurn, serial exchanges) are kept as stubs that abort.
   *Answer:* lockstep is the right choice.

2. **Practice duel with Sam.** *Answer:* wanted. The scripted tutorial
   (DoPracticeDuelAction, Sam's scripted AI turns, Dr. Mason's instructions)
   is ported (`src/practice.c`) and identical to the original in lockstep,
   including the restarts when the player doesn't follow the instructions.
   Frontends start it with the AI deck `TCG_SAMS_PRACTICE_DECK_ID` and 2
   prizes (see `tcg.h`).

3. **AI vs AI.** The original never pits two AIs against each other; its AI
   dispatch reads wOpponentDeckID for whichever side is the turn duelist. For
   AI-vs-AI both the reference harness and the port set wOpponentDeckID to the
   turn duelist's own deck id around AIDoAction. Human-vs-AI duels are
   unaffected. *Answer:* keep it.

4. **Viewing screens.** "Check" screens, card pages and the B+direction / START
   / SELECT shortcuts are pure viewing UI in the port (the frontend shows them
   itself). In the original they load cards into wLoadedCard1/2 as a side
   effect; nothing in the duel logic depends on that, but RAM can differ from
   the original after the player browses cards. *Answer:* fine as long as the
   behaviour is the same.

5. **Saving / continuing a duel.** *Answer:* add save files, through one
   interface with files on desktop, libdragon's save on the N64, and
   localStorage or the File System Access API in the browser.
   Done:
   - The engine keeps the original's SRAM images: `sCurrentDuel` and
     `sBackupCurrentDuel`, with their header and checksum. It hands both to
     `host->save` after every write. That is the duel's save file,
     `TCG_SAVE_SIZE` bytes.
   - `tcg_continue_duel` is the original's TryContinueDuel ("Continue Duel").
     `difftest --continue-at TURN` continues the port's save in both the
     original and the port in lockstep. It is identical, practice duels
     included. The practice duel needs the backup slot to restart a turn,
     which is why the save file holds both slots.
   - The storage interface is `frontends/common/save_store.h`. Desktop:
     `save_store_file.c`, used by the CLI (`--continue`). Browser and N64:
     in their frontends.
   - The original also validates and loads its general save data (player
     name, overworld, collection) when continuing. The port has none and
     behaves like the original without a valid general save.

6. **Card art in the web build.** The web UI uses the original card graphics
   from `src/gfx/cards`. *Answer:* fine; no public build is planned, this is
   an experiment.

7. **Debug option sSkipDelayAllowed.** *Answer:* add a toggle in the browser
   builds. The original only uses it to cut its draw/shuffle/prize animations
   and delays short while B is held, so it is pacing only. The engine takes it
   as `TcgDuelSetup.skip_delay_allowed`, which sets `wSkipDelayAllowed` like
   the original. The browser skips its own pacing while B is held when the
   toggle is on.

## Notes

- **Sam's AI with other decks.** Sam's AI plays scripted turns made for his
  deck; both of his duels load SAMS_PRACTICE_DECK. Fed other cards, it drives
  the original into impossible states and out-of-range memory, and the
  harness doesn't test that.
- **Lockstep coverage.** The lockstep duels reached 775 of the 815 routines
  that are sync points. The 40 others:
  - dead code: `Func_14323`, `Func_1585b`, `Func_15886`, `Func_16488`,
    `PickPokedexCards_Unreferenced`;
  - `TryContinueDuel` and `_ContinueDuel`: run by `--continue-at`, which
    doesn't write coverage;
  - two AI routines needing special states: `PickRandomBenchPokemon` (Sam's
    scripted forced switch) and `CheckIfPlayerHasPokemonOtherThanMewtwoLv53`;
  - effects of attacks the AI practically never chooses, or that need a lot
    of energy:
    - AI selection: Barrier, both Metronomes, Destiny Bond, Devolution Beam,
      Energy Conversion, Energy Spike, Recover (Kadabra, Starmie), Mewtwo's
      Energy Absorption (both), Lure (Ninetales), Mirror Move (Pidgeotto),
      Prophecy, Scavenge, Teleport, Wildfire, Energy Removal (the trainer);
    - player selection: Fire Blast, Flames of Rage, Gigashock, Golduck's Hyper
      Beam, Hypno's Dark Mind, Marowak's Call for Family, Poliwhirl's Amnesia,
      Scavenge, Victreebel's Lure;
    - Earthquake.

  `unitdiff` can test any of these routines on its own if needed.
- **Gambler and Recycle.** Their card records end past the end of their ROM
  bank. The bytes the original reads there come from VRAM and depend on the
  screen; the port reads 0. Nothing in the duel logic uses those bytes.
- **B held on a two-item menu.** The original's TwoItemHorizontalMenu only
  accepts A, but Prophecy restarts if B is *held* while A is pressed. The port
  answers with A alone, so that restart (which only redraws the text) can't
  be reached.
