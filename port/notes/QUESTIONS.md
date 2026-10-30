# Open questions and decisions taken

Decisions made while porting, to review with the project owner. Each one
records the choice that was made so work could continue.

1. **Network play model.** The original's 2-player mode is the serial link
   duel (each Game Boy runs half the logic and they exchange OppActions and RNG
   state). The port instead runs the whole engine on both clients with both
   duelists as `DUELIST_TYPE_PLAYER` and only exchanges answers to `request()`
   (lockstep). Rules are identical; the link-specific code paths
   (DoLinkOpponentTurn, serial exchanges) are kept as stubs that abort.
   *Decision:* lockstep. Porting the serial protocol itself is possible later.

2. **Practice duel with Sam.** The scripted tutorial (DoPracticeDuelAction
   table) is not ported yet; starting it aborts. Sam's *normal* deck and AI are
   ported like any other AI deck. *Question:* is the tutorial wanted?

3. **AI vs AI.** The original never pits two AIs against each other; its AI
   dispatch reads wOpponentDeckID for whichever side is the turn duelist. For
   AI-vs-AI (used heavily by the differential tests) both the reference
   harness and the port set wOpponentDeckID to the turn duelist's own deck id
   around AIDoAction. Human-vs-AI duels are unaffected.

4. **Viewing screens.** "Check" screens, card pages and the B+direction / START
   / SELECT shortcuts are pure viewing UI in the port (the frontend shows them
   itself). In the original they load cards into wLoadedCard1/2 as a side
   effect; nothing in the duel logic depends on that, but it means RAM can
   differ from the original after the player browses cards.

5. **Saving / continuing a duel.** The original saves the duel to SRAM
   between turns (SaveDuelStateToSRAM, SaveDuelData) for "Continue Duel". The
   port keeps the RAM side effects and calls the optional `host->save` hook,
   but does not implement save files.

6. **Card art in the web build.** The web UI uses the original card
   graphics from `src/gfx/cards` (part of this repository). *Question:* fine
   to ship them in a public build?

7. **Debug option sSkipDelayAllowed** is read from SRAM by the original; the
   port always uses 0 (the normal game value).
