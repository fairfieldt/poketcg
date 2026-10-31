# Native duel inspection controls

Z opens the play-area/discard inspector; START views the selected card. The Sam Turn 4 replacement selector lacked Z handling even though its lesson taught the original Select inspection shortcut. Z now works during that selector and the paused practice lesson, returning without answering either pending request. START keeps its existing card-detail action. On the Steam Controller, left trigger is Z and ||| is START. View/Back remains the emulator's reload control.

Three live instructions have platform-specific copy: `SamTurn4Instr2PracticeDuelText` (0x01bf), `SamTurn4DrMason2PracticeDuelText` (0x01d6), and `UsefulButtonsBookText` (0x04ee). The native lesson/checklist teach Z; the book teaches Z, L/R views, A/B, START and board navigation. Browser lessons teach C, and its book teaches the Check menu and browser keys. Clicking a selectable replacement commits that choice in the browser, so the tutorial deliberately teaches C for inspection.

The frontend supplies an optional exact-ID text lookup before ordinary formatting and checklist composition. The generated text table and original GBC source remain intact. All other text IDs, ordinary uses of "select", card rules and the original headless text retain their source strings. Native A/B and START names already match existing controls. The old B+direction duel shortcuts are replaced in the book with the implemented native/browser controls. Remaining SELECT references belong to unused source debug texts, not reachable production prompts; original card-list Select-to-sort behavior is not globally rebound.

Validation:

- `make -C port controltest`: all text IDs across source/N64/web layouts, formatted speech, composed checklist, book, invalid-ID fallback and engine-state equivalence pass.
- `make -C port practicecheck`: all eight tutorial turns retain original memory/RNG/request traces, including the expected wrong-action rewind.
- [Focused selector/input tests](../forced-selector-check/README.md): five real engine/frontend cases at both resolutions, P1/P2 ownership, START inspection, Z+A priority, Z/B cursor preservation, lesson return and physical buffered Z edges pass.
- `node port/frontends/web/test/control_text.mjs`: the actual WASM build resolves its browser override and teaches C/Check.
- `node port/frontends/web/test/practice_reading.mjs`: browser lesson/pagination/input checks pass at 320 and 960 pixels.
- Both production ROMs compile with the pinned SDK guards; their linked `tcg_text_override` is a strong native symbol. [Native dispatch disassembly](native-text-dispatch.txt) retains the call to that override before the source-table fallback.

No Steam controller mapping or live save changes are required. Both ROM variants are published for the next launch; the current game is left running. [Deployment evidence](deployment.json) records the exact production hashes.

Original action paths: `src/engine/duel/core.asm` (`DuelMenuShortcut_BothActivePokemon`, `SelectingBenchPokemonMenu`, `HandleSpecialDuelMainSceneHotkeys`); `src/engine/menus/play_area.asm`; `src/text/text2.asm`; `src/text/text5.asm`.
