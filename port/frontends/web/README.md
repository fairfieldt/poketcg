# Web frontend (WebAssembly)

A browser UI for the C duel engine (`port/src`), modeled on the original duel screens, with a
deck builder and a small relay server for 2-player online duels. No emscripten, no framework,
no CDN: everything in `dist/` works offline from any static server.

## Build and run

    ./build.sh                      # -> dist/ (tcg.wasm, js, css, gfx)
    cd server && npm install        # once (installs `ws`)
    node server/server.js           # http://localhost:8080/ (static dist/ + relay on /ws)

Any static server works for local duels (`python3 -m http.server -d dist`); online duels need
`server/server.js`. Add `?fast=1` to the URL to turn off pacing delays.

Requirements: clang 18 with the wasm32 target and wasm-ld, Binaryen's `wasm-opt` (on PATH or
`WASM_OPT=...`, default `/home/user/tools/npm/node_modules/.bin/wasm-opt`), python3, Node ≥ 18.

### Optional engine sources

`port/src/*.c` and `port/src/gen/*.c` are required. `port/src/effects/*.c` and `port/src/ai/*.c`
are optional because they are ported concurrently: `build.sh` skips any of them that does not
compile, and if linking fails it retries without the AI files, then without the effect files,
then without both. Whatever is left out falls back to the engine's weak stubs
(`gen/effect_stubs.c`, `ai_stubs.c`), which call `tcg_fatal`; the UI then stops the duel with a
message naming the unported routine. `dist/build-info.json` records what was linked (shown on the
title screen); failed link attempts are in `build/link.log`. `CORE_ONLY=1 ./build.sh` forces the
stub-only build.

## Files

    build.sh               the build (compile, link, asyncify, assets, copy static files)
    wasm/web_host.c        host glue: TcgHost -> JS imports, setup buffer, extra read-only exports
    wasm/libc.c, include/  the few libc functions the freestanding build needs
    tools/gen_assets.py    card art sheet, symbols, box messages, constants (stdlib only)
    tools/png.py           minimal PNG reader/writer
    src/index.html, css/   page shell and styles
    src/js/engine.js       Engine.load() / duel.run(): wasm + Asyncify glue (browser and Node)
    src/js/duel_ui.js      the duel screen: events -> board/text box, requests -> menus
    src/js/net.js          online protocol client (relay connection, answer exchange, replay)
    src/js/deckbuilder.js  deck builder
    src/js/data.js         deck catalog, custom deck storage, deck rules, deck codes
    src/js/gfx.js          DOM helpers (card art, symbols, card page)
    src/js/main.js         menus and screen flow
    server/server.js       static file server + WebSocket relay
    test/node_duel_test.mjs  random hot-seat (or --vs-ai) duels in Node
    test/ui_test.mjs         Playwright: hot-seat, online (with reload/rejoin), vs AI, deck builder
    test/ui_fuzz.mjs         Playwright: random clicks through hot-seat duels
    test/relay_test.mjs      relay protocol (rooms, sequencing, rejoin) without a browser

Tests (after `./build.sh`; the browser tests start their own server):

    node test/node_duel_test.mjs --seeds 1-60     # random hot-seat duels (add --vs-ai for the AI)
    node test/relay_test.mjs
    node test/ui_test.mjs                         # screenshots in test/screenshots/
    node test/ui_fuzz.mjs --duels 3

## JavaScript API (src/js/engine.js)

    import { Engine, TcgFatal, DuelAborted, REQ, EV, TCG_PLAYER } from './js/engine.js';
    const duel = await Engine.load();          // or Engine.load(bytes | url | WebAssembly.Module)
    const result = await duel.run({
        duelists: [{ kind: 'human', cards: [/* 60 card ids */], name: 'MARK' },
                   { kind: 'ai', deckId: C.LIGHTNING_AND_FIRE_DECK_ID, name: 'Aaron' }],
        prizes: 6, seed: 12345,                 // or rng: [wRNG1, wRNG2, wRNGCounter]
    }, {
        onEvent(ev) {},                         // {type, arg[4], text, str}; may return a Promise
        async onRequest(req) { return 0; },     // {type, duelist, shown, menu, numItems, cursor,
    });                                         //  text, text2, textStr, text2Str, items, arg, seq}
    // result: {result: 0 if the PLAYER_TURN side won, finished, turns, requests}
    // rejects with TcgFatal (engine abort, e.g. an unported stub) or DuelAborted (duel.abort())

Static data: `duel.card(id)` (parsed card record), `duel.cardData(id)` (raw 65 bytes),
`duel.text(id)`, `duel.deckList(i)`. Live state: `duel.duelvars(TCG_PLAYER)`, `duel.wram()`,
`duel.hram()`, `duel.w('wDuelTurns')`, `duel.cardId(duelist, deckIndex)`, `duel.formatText(id)`,
`duel.stateHash()`. Constants: `C` (constants.h), `W` (WRAM addresses), `T` (text ids).

Each `run()` instantiates a fresh wasm instance (an aborted duel leaves nothing behind).
`request()` blocks the C engine; a Promise returned by `onRequest`/`onEvent` suspends the wasm
stack with Asyncify (`asyncify_start_unwind`, await, `asyncify_start_rewind`, call `web_run`
again) and resumes it with the answer.

## Online protocol

Both clients run the same engine with the same setup (decks, names, RNG seed chosen by the
host, prizes); the host is the TCG_PLAYER side, the guest TCG_OPPONENT, both TCG_HUMAN. Only
request answers are exchanged, as JSON over `ws://host/ws`:

* lobby: `create` / `join {room}` / `rejoin {room, token}`; the host sends `setup` when the guest
  has joined (the guest's deck comes with `join`).
* duel: for every request except `TCG_REQ_ACK` and view-only card lists (`TCG_REQ_CARD_LIST`
  with `arg[0] = 1`), the duelist it belongs to answers in its UI and sends `{t:'answer', seq, rt, v, h}`: `seq` = number of non-ACK requests before it,
  `rt` = request type, `v` = answer, `h` = `duel.stateHash()` (FNV-1a of both duel variable
  blocks and the RNG). The peer waits for the answer with its own `seq`; a different `rt` or `h`
  is a desync, reported to both players. ACKs and view-only lists never change the duel: they
  are answered locally and never sent (a remote duelist's ACK advances by itself after a
  moment, its view-only lists are answered with TCG_CANCEL). `TCG_REQ_CONFIRM` is sent.
* the server checks `seq` continuity and keeps the setup and the answer log of each room.
  After a disconnect a client rejoins with its token and gets the log back: a client that kept
  its engine resends any answers the server missed, one that reloaded the page replays the
  whole duel from the log without UI and continues. The peer sees "lost the connection" /
  "is back" / "left the duel" messages.
