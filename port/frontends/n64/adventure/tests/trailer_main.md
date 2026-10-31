# Private trailer harness

`trailer_main.c` includes the production `main.c` under a renamed entry point.
It runs the actual production initialization, then intercepts the first call
to the title/diary flow. No production entry point or shared Makefile is edited.
`tools/build_trailer.py` builds four separate480×360/8MiB private ROMs with
`ADVENTURE_DIAGNOSTIC=0`, `SCENE_T3D=1` and `ADVENTURE_SHOW_FPS=0`.

Every ROM first presents eight seconds of **silent black frames** with real
audio polling. This gives the separate capture process time to identify and
route only the new private SDL audio stream. The virtual controller only
replaces joypad reads in these ROMs; it does not alter SDL settings or a user's
controller configuration.

All clocks and scene/text/request markers go to the development log. No
developer labels or counters are drawn over the game.

## Segments and fidelity

- **INTRO:** seeds an ordinary new game named `Mark`, runs actual
  `ow_new_game`, the island run, lab entry, Mason/Sam choreography and all
  source dialogue. The real menus select Exit from Sam's rules list and Yes
  to the practice duel. A custom host traps immediately before the first Sam
  duel, requiring the correct practice deck and player position10,14.
  Individual pages get a fresh A every180VI ticks (three seconds).
- **RONALD:** loads the untouched legal engine-generated checkpoint described
  in `trailer_ronald.md` (SHA25699033431…). A `tcg_set_host` proxy logs requests
  and delegates to the unchanged native UI. Real controller Down/A presses
  choose Slap, acknowledge damage/KO, select a real Prize, show the card and
  Ronald's promoted Zapdos. All nine source request types and answers must
  match, with real KO/prize events. No combat HP, cards, attachments, flags,
  RNG or save bytes are patched. Display names are `Mark` and `Ronald`.
- **CLUBS:** private post-tutorial fixtures warp to Lightning, Water and Fire
  via actual `ow_load_map`. Joshua is already defeated to avoid his source
  Water Club gate. A BFS probes legal moves on temporary state, then the
  actual player walks using normal `ow_move` and presentation interpolation.
  An approach blocked by furniture uses the nearest reachable cell within
  three source steps. No collision or NPC pose is altered.
- **ENDING:** declares a private Amy victory without depicting a fabricated
  duel, then runs actual `ow_finish_duel` and its reward script. The Water
  Medal flag/count must already exist before the real medal presenter runs;
  original fanfare/ACK/resume behavior is intact. The fixture stops before
  the two booster packs, privately warps to the Hall of Honor, legally walks
  toward the original glowing altar/Legendary Cards, then calls the actual new
  title with original music/orbs. This warp does not claim one win finished
  the campaign. The end marker appears after seven seconds of live title.

Fixture warps, post-tutorial event setup and the declared Amy win are private
capture shortcuts, not footage of a continuous campaign. The visible text,
game art, dialogue, card/prize results, rendering and sound come from the real
game. Profiles/save memory belong to the private emulator session; no live
profile, diary, game thread or installed emulator setting is accessed.

## Markers and native gates

The marker format includes segment/shot identifiers, VI count and N64
microseconds. `READY` starts black preroll; `BEGIN` precedes game audio/frames;
`FIRST_FRAME` records the first presented game frame. `INTRO_RUN` identifies
the first source world movement, so the edited trailer can begin with Mark
already running. `END` closes the segment; final frames/audio keep running
for capture postroll. Ending holds its actual live title.

All four private ROMs pass local native Ares flow gates. Compact proofs and
their exact ROM hashes are in `port/reviews/trailer-harness/`.

| Segment | BEGIN | Main markers | END |
| --- | --- | --- | --- |
| Intro |8.518s | run10.018s; lab/Mason16.097s; Sam table43.709s; duel trap72.791s |74.789s |
| Ronald |8.518s | real KO13.656s; Prize20.001s; Ronald turn24.823s |26.8s |
| Clubs |8.517s | Lightning8.925s; Water15.787s; Fire23.155s |28.908s |
| Ending |8.5s | Water medal16.565–22.001s; Hall25.499s; title29.319s |36.331s |

Times are N64 clocks from these gate runs, not promises about host capture
startup or shader latency. The capture agent records the deployed real CRT
Guest Advanced shader output and private audio, then trims using markers and
visual timing. These gates verify playable flows and are not final video QA.

## Build and probe

Build the regular expansion game first, then:

```sh
python3 port/frontends/n64/adventure/tools/build_trailer.py --segment all
# After coordinating the native emulator slot:
python3 port/frontends/n64/adventure/tools/build_trailer.py --probe intro
```

ROM paths are `adventure/build/trailer-SEGMENT/trailer-SEGMENT.z64`.
Each directory owns its ELF, DFS, pinned object copies, private settings and
saves. The builder records source/object/ROM hashes and the production base
commit `2dda70b3`. It deliberately excludes production `main.o` and `video.o`,
compiling the private entry and counter-free video separately. It preserves
external character DIAG18 work and all shared build files.

`--preroll-vi` and `--ack-vi` expose capture pacing without changing source
game logic. Normal defaults are480 and180 respectively.
