# N64 special-effect frontend audit

The survey maps all 319 named non-Power attacks and all 26 Power variants from the verified parent catalogs to frontend request families. `request-families.json` records every entry; `map_requests.py` regenerates the map. This is a conservative static call survey, including conditional and AI paths. Copied attacks such as Metronome inherit the copied attack’s selectors at runtime. It does not claim that every card was played through every UI branch.

The audit found presentation state missing from drawing-only original shims. The resulting fixes preserve the engine’s RAM, rules, request answers and RNG:

- Damage Swap, Curse, Strange Behavior and Energy Trans now show the current source/recipient instruction. Held Grass Energy and damage counters, Gigashock’s selected targets and its undo state are visible.
- Prophecy and the shared Pokédex ordering routine show each card’s selected order number.
- The Power picker shows the full Power name instead of clipping it after redundant Pokémon/HP text.
- Forced target, attack, Energy, card, color, confirmation and named-choice screens show current/request-local instructions. They retain the underlying log; the main board and ordinary Hand browser keep their history.
- The parent’s fixes reserve Peek’s original mode 2 and format Conversion/Shift text-only events, so Peek cannot become a Grass-Energy prompt and the current color instruction reaches both frontends.

The shared events are append-only: `PLAY_AREA_MARK=13` carries duelist, slot and marker kind; `CARD_ORDER=14` carries duelist, deck index and order, with index FF clearing that owner’s snapshot. Slot kinds are none/Grass Energy/damage/selected. Phase modes 0–4 retain their existing meanings and modes 5–10 identify damage source/recipient, Curse source/recipient, Strange Behavior and multiple Bench targets. These emissions only read canonical game state. Native and browser constants agree.

Markers sit above the cached native board. A normal Play Area refresh and nested card/Check inspection preserve them. Empty screens, full main-screen redraws and a frontend reset clear them; a HUD-only redraw preserves them. Order snapshots follow the original function pointer and survive list refreshes/nested inspection. Native storage adds 12 marker bytes and 120 order bytes, with no heap/image allocation. The retained-canvas revision changes on every semantic event.

| Real engine workflow | Native host checks |
| --- | --- |
| Opposing attack pairs (Amnesia/Metronome family) | Second attack and B; request owner differs from shown owner |
| Energy selection | Nonsequential deck indices 9/27/48; list position 1 selects deck index 27; B |
| Forced card list and Select/Check | List position 1 selects deck index 9; START inspection; B |
| Shift/Conversion color order | Grass/Fire/Water menu order returns Water’s color value; B; formatted Weakness text |
| Named two-item choice | Right choice returns index 1 |
| Peek | Other deck, B back to area choice, own Prize 2, correct owner restoration and dedicated mode |
| Continue/stop | A/B, selected-card START inspection and opposite shown owner |
| Bench-only menu | Current menu offset 1 despite stale exclusion flag 0; selection and B |
| Prophecy | Opponent’s top three cards reordered 5,3,4 with visible numbers |
| Gigashock | Duplicate target retry, two marked targets and undo/reselect |
| Venusaur Energy Trans | Donor/recipient, exact Grass transfer, both cancellation points, invalid donor, no Grass, Sleep and Muk |

`selectortest` passes 36 real engine/frontend cases at each resolution. Every initial request also receives a press from the non-owning controller, which must not answer it. Each interactive run equals the headless run’s complete WRAM, HRAM and event/request/answer digest, including RNG. `powertest` adds 16 full Power cases per resolution; the seven existing forced-replacement/evolution cases remain green. The common prize/coin/main-action families retain their earlier dedicated coverage. The portable engine has no Pokémon-effect producer for the legacy numbered or generic card-list menu fallbacks.

Final command: `make -C port/frontends/n64 selectortest powertest checktest acktest boardtest retainedtest rastertest -j6`. Board caching passes 369 frames (216 hits, 153 invalidations); retained canvases pass 276 frames including 108 reuses; raster paths pass 1,992 glyph cases, 3,276 scaled blits and 936 card backs. Separate native-host pixel tests prove read-only marker/order emissions, cache restore, nested inspection, empty slots and redraw clearing. ASan/UBSan covers the included frontend and test harness at both resolutions; the linked engine objects are covered separately by the parent audit.

Browser test: `node port/frontends/web/test/effect_markers.mjs` passes with real compiled card data. It checks Peek’s prompt, phase prompts, marker ownership, undo, empty slots, order snapshots, nested inspection, HUD preservation and immediate DOM removal on full/empty redraws. Existing parent WASM duel/save/continue/practice and click/hover regressions also pass.

The PNGs with 320/480 suffixes are CPU host-mock renderings at the native display sizes, with placeholder card art. The web PNGs are browser captures. None is a VI or physical-console performance claim. No additional Ares timing run was used for this frontend work. Procedure/error ACK pagination and each card’s detailed rules are covered by the separate engine/Power audit rather than claimed as exhaustive here.
