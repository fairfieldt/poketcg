# Full web game

The default page runs the portable C adventure and duel engines, with all 34 maps
rendered by WebGL from the same L643 meshes, textures and character models used
by the N64 build. No Game Boy CPU or ROM runs in the browser.

```sh
./port/frontends/web/build.sh
python3 -m http.server 8080 --directory port/frontends/web/dist
# Open http://localhost:8080/
```

Build requirements: Emscripten (`emcc`), Binaryen `wasm-opt` (the Emscripten
installation's copy is discovered automatically), Python3. Checked-in audio
and model assets make normal builds independent of Blender and the Game Boy
ROM. `EMCC`, `WASM_OPT` and `PYTHON` override tool paths.

For the existing online relay and standalone duel modes:

```sh
npm ci --prefix port/frontends/web/server
node port/frontends/web/server/server.js
# Full game: http://localhost:8080/
# Duel modes: http://localhost:8080/duel.html
```

The build includes every duel AI/effect module; a failed source build stops
rather than shipping a reduced engine.

## Live preview

[Open the current game](http://ms.tdf.sh:8091/) while connected to the tailnet.
The preview serves the current `dist/` directory without caching; reload after
an update. Keyboard and touch work over this HTTP link. Browsers may require a
secure origin for gamepads; the game falls back to keyboard/touch when access
is unavailable.

## Play

The original opening/title sequence precedes two local diaries. Each diary has
its own name, story, collection, decks, recipes, mail, records, settings, and
salted Card Pop identity. Player naming shows the detailed Mark model. The
story begins with the original island route to Mason Laboratory, scripted
movement, Mason/Sam dialogue and practice duel.

| Input | Action |
|---|---|
| Arrow keys / gamepad stick or D-pad | Walk, select island destination, menus |
| Z / Enter / gamepad A | Talk, inspect, enter, choose, advance text |
| X / Escape / gamepad B | Back, complete text |
| C / gamepad Start | Adventure menu; card details during a duel |
| Hold Shift / gamepad B while walking | Run |

On-screen controls support mouse/touch. The Battle Center uses both diaries'
selected physical decks. Two standard gamepads route each duelist to its own
controller; the keyboard can also be passed between players. Card Pop and Gift
Center exchange cards/recipes between the diaries. Printer features create
actual downloadable text reports.

The browser shares `overworld.c`, `gameplay.c`, and the N64 adventure's
`menus.c`, `profiles.c`, `extra_ui.c`, and `presentation.c`. Collection limits,
recipes, all boosters, mail rewards, Card Pop, transfers, Challenge Machine,
story branches and after-duel scripts therefore use the same C implementations.
Asyncify suspends C callbacks while browser menus or the existing duel UI await
input. WebGL draws the shared assets at device resolution, using native UVs,
vertex colors, joint pivots, source cameras, event patches and character poses.

## Saving

Use Menu → Diary to save. Single-player duels also checkpoint the story and
save their turns, as in the N64 adaptation. Reloading and choosing Continue
resumes a pending saved duel. Battle Center duels are not resumable.

The browser stores one versioned cartridge record in localStorage. Both
canonical diaries and pending duel data are named blobs inside it. A Card Pop
or Gift Center exchange replaces the whole record in one atomic write; a failed
write leaves both old diaries and rolls back the in-memory operation. The C
profile decoder checks version/length and the canonical story/gameplay CRCs.

Export cartridge backs up both diaries and saved duels. Import validates both
canonical diaries before replacing the cartridge. These adventure saves use a
separate namespace from the existing standalone duel frontend's saves. Browser
storage remains local to the site/profile; export before clearing site data.

## Audio and assets

`world/` in the bundle contains native models and shared textures. Build-time
character synchronization picks up completed new character exports automatically.
The source Blender authoring files are linked from [world3d](../../world3d/README.md).
`intro/` contains the original CGB opening art and all 26 motion tables.

`assets/audio/` contains browser Vorbis versions of all 31 music and96 sound-effect
IDs rendered offline from the original driver, using the same PCM as the N64
assets. `tools/pack_adventure.py` copies them into the bundle with loop metadata.
Regenerating missing Vorbis files uses the cached source PCM and `ffmpeg`;
normal builds use the checked-in Vorbis assets. Audio starts after a user gesture
because browsers restrict autoplay.

## Verification

```sh
node port/frontends/web/test/node_duel_test.mjs --seeds 1-60
node port/frontends/web/test/adventure_engine_test.mjs
node port/frontends/web/test/world3d_test.mjs
node port/frontends/web/test/relay_test.mjs
node port/frontends/web/test/world3d_browser.mjs
node port/frontends/web/test/adventure_browser.mjs
```

Browser tests use Playwright and Chrome. Install the test dependency without
changing the production bundle:

```sh
npm install --prefix port/frontends/web/build/world-test playwright
```

- Actual WASM tests run the original intro and source choreography, both starter
  choices, canonical diaries, import/export, pair transaction rollback, selected
  deck Battle Center setup, mail, glossary, print output and island navigation.
  Their story fixture supplies tutorial duel outcomes; separate duel tests run
  the actual tutorial through all eight player turns and test save/continue.
- WebGL checks render all 34 maps, all completed character families, conditional
  doors/Challenge Machine, eight independent deck panels and the naming portrait.
- Browser checks exercise opening → naming → source Mason introduction → actual
  Sam duel UI, the pre-duel cartridge checkpoint and the complete card album.
- Screenshots and JSON validation records are in `build/world-test/` and
  `build/adventure-browser/`.

A full human browser playthrough remains a further check. Native scene fidelity,
sampled audio overlap and platform save/print adaptations have the same limits
as the documented N64 port. Physical Game Boy link/IR/printer transport is
replaced by the local two-diary and download features.

The preview serves `dist/` directly without caching. Run `build.sh` after source
changes, then refresh the same URL. The persistent server is the user LaunchAgent
`com.fairfieldt.poketcg.web-preview`, bound to `100.101.45.117:8091`.
