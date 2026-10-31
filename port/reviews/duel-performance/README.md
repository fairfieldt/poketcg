# Battle board caching and completed-frame timings

The battle renderer now retains the unchanged board rows and draws selection borders, the thinking caption, menus and the text box over that backing. It restores the backing with a fenced RDP copy rather than rerasterizing twelve cards and scanning their attachments on every input frame. The offscreen UI canvas is cleared with an RDP fill, then returned to CPU ownership before text and overlays are drawn. The existing final canvas-to-display copy and fence remain in place.

The backing is checked against an exact WRAM/HRAM snapshot, both names, turn ownership, damage visibility/location/amount and raster dimensions. The snapshot includes both 60-card ID arrays, so silent engine changes between requests invalidate it. Selection is a separate layer; portrait frames, the ninth energy square and damage glyphs retain their original overlap order. Unusually wide bench damage numbers use the original drawing path. Nested card screens can overwrite the UI canvas without changing the backing.

The image allocation is 116,480 bytes at 320×240 and 262,080 bytes at 480×360. The complete cache structure, including the 8 KiB engine snapshot and image descriptor, is 8,384 static bytes. The image is released on duel reset and return to the adventure. If allocation fails, the game keeps using the uncached renderer. Native allocation checks confirm that both images are freed; the diagnostic heap values below belong to a small 8 MiB harness, not the full game's minimum-free-memory budget.

The separate [raster changes](../duel-raster/README.md) preserve scaled glyph and portrait pixels while reducing CPU work during a fresh draw.

## Native results

These are Ares v148 CP0 timing measurements through the actual libdragon CPU/RDP renderer and completed-frame presenter. They are emulator proxies, **not measured physical-console FPS**. Both resolutions ran with 8 MiB available. A 30 fps display cap is active. Audio polling was stubbed in this isolated duel harness; the separate full-game survey measures real audio work.

A full board has both Active Pokémon, all ten Bench Pokémon, attached Energy, status effects, prizes and real card images. After warmup, the old path records 18 samples per case. The final path records 32 samples for battle cases and 16 for the additional common UI cases. No per-frame debug printing or framebuffer dumps occur inside the timed loops.

| Case | Resolution | Before median frame | Final median frame | Final median work, excluding pacing |
| --- | --- | ---: | ---: | ---: |
| Battle acknowledgement | 320×240 | 61.32 ms | 33.24 ms | 7.06 ms |
| Battle actions, idle | 320×240 | 66.97 ms | 33.23 ms | 12.67 ms |
| Battle acknowledgement | 480×360 | 158.18 ms | 33.44 ms | 13.31 ms |
| Battle actions, idle | 480×360 | 171.51 ms | 33.40 ms | 25.86 ms |
| Battle action cursor moves every frame | 480×360 | 171.30 ms | 33.39 ms | 25.72 ms |
| Pokémon slot cursor moves every frame | 480×360 | 163.52 ms | 33.43 ms | 16.02 ms |
| AI thinking acknowledgement | 480×360 | — | 33.49 ms | 14.40 ms |

The final battle cases reach the 30 fps cap at both resolutions. This is actual redraw/presentation work: each frame restores the board, draws its current overlays and presents a completed canvas. Controller input remains sampled independently at VI frequency.

The 480×360 canvas clear fell from approximately 21.25 ms of CPU work to 1.03 ms including the RDP fence. A board restore takes approximately 3.1 ms instead of a roughly 127 ms full board rebuild. Fresh engine-state changes still build a new board once.

A separate [4 MiB native gate](native-4m-gate.json) then ran this same frozen harness with `ExpansionPak:false`. Both requested modes correctly used 320×240, all 128 complete-frame comparisons passed, and the 116,480-byte image allocated and released in both passes. This verifies the real 4 MiB emulator configuration for the renderer; it does not substitute for the full adventure residency budget.

The broader sample identifies the next work separately:

| Common UI case | 320×240 median work | 480×360 median work |
| --- | ---: | ---: |
| Hand card list | 55.54 ms | 118.77 ms |
| Card detail page | 44.00 ms | 92.90 ms |
| Generic popup list | 28.12 ms | 61.72 ms |

Those screens redraw their text and card panels every idle frame and are not accelerated by the board backing. A follow-up can retain their completed canvas until selection, page, state or effect changes, with explicit redraw after nested views.

## Correctness checks

- 128 native complete frames (64 per resolution) match a fresh reference renderer pixel for pixel. Cases include every slot on both sides, maximum Energy rows, changed deck IDs/HP/names/turn, absent cards, thinking phases, expiring damage and denied flashes, and nested card pages overwriting the shared canvas.
- The host board test compares 369 complete frames over 320→480→320 mode changes, including damage amounts up to 65,535, cache hits/invalidation and release. Forced allocation failures preserve the selected/thinking frames through the uncached path, and allocation can recover on a later frame.
- Scaled raster tests compare 1,992 glyph cases, 3,276 blits and 936 card backs, including alpha, padding, parity and clipping.
- All ten scripted frontend tests pass at both resolutions, including the full practice duel, hotseat/controller ownership, save continuation and all-card/practice layout checks. Input and coin event/RNG tests pass.
- AddressSanitizer and UndefinedBehaviorSanitizer pass the board/context test.

Adventure card inspection also now has an explicit footer context. `n64_card_detail` draws its navigation hint without inheriting the completed duel's log. Duel inspection keeps its current log. The regression seeds “all the Prizes”, “Decision” and “You won the Duel”, checks that the external card frame equals an empty-log reference, and verifies that inspecting a duel card still shows the log without changing it.

[before.json](before.json) and [final.json](final.json) include every sample and mean/median/p95/min/max stage distributions. [validation.json](validation.json) records pixel and allocation checks. Raw logs are retained alongside them. [actions-320.png](actions-320.png) and [actions-480.png](actions-480.png) are native RDP framebuffers captured after a warmed input loop; they are pre-VI captures, not photographs of a console or simulated high-resolution renders.

The private build used `port/frontends/n64/adventure/build/board-perf-{before,final}/`. [bench.c](bench.c) and [Makefile.private](Makefile.private) preserve its harness and local build recipe; `source.c` in each private directory is the frozen frontend used by that run. The JSON records its SHA-256. The native source was frozen before the optional-allocation fallback branch and later UI work; the fallback leaves its success path unchanged and is covered by forced-failure host and sanitizer checks.
