# Evolution target focus

Playing an evolution now highlights the first legal target: Active first, then Bench slots in order. The existing engine legality scan supplies the preference, including evolution timing and the actual pre-evolution name. Knocked-out targets are skipped. Stage 1 to Stage 2 and Mysterious Fossil evolution remain supported. Prehistoric Power still blocks evolution before a selector opens.

The preference changes only the frontend request cursor. It does not write menu WRAM to move the highlight, change RNG, auto-confirm, or restrict the player's manual choice. Other play-area selectors retain their existing defaults. Both native and browser renderers already honor the request cursor.

Validation: 26 real-engine cases via `make -C port evolutiontest`; P1/P2 native frontend initial focus, START inspection and fresh-A confirmation at both resolutions via `make -C port/frontends/n64 checktest -j6`; the original eight-turn practice trace; and actual WASM duels, save/continue and practice checks. Default/preferred wrappers leave identical WRAM and HRAM before the host answer and after the same answer. [Results](validation.json) and logs are included. No renderer, layout, asset or save-format changes were made.

Both production ROMs are published for the next reload. The browser build is refreshed at <http://ms.tdf.sh:8091/>. [Deployment hashes](deployment.json) identify the production builds. Existing game sessions and saves are preserved.
