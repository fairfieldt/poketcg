# Duel felt row cache

`tcgn64.c:draw_felt` retains the original 32×32 color tile, then precomputes only the physical rows in its repeating pattern. A warmed board redraw copies 182/273 rows into the existing cached CPU canvas with `memcpy`. Scaling/division occurs only when building the cache. The row period is 32 at 320×240 and 48 at 480×360. Cache replacement frees the old allocation; the retained cache uses 19,200/43,200 bytes plus its small descriptor. Cache initialization also precomputes column indices once.

Canvas ownership, cache writeback, RDP copy and completion fence are unchanged. UI content and duel logic are unchanged.

## Verification

`make -C port/frontends/n64 felttest` compiles the actual production function and compares complete buffers against the original scalar formula, checking both resolutions, resolution switchback, padding and untouched pixels. It also passes AddressSanitizer. All ten existing host UI tests passed at each resolution. The N64 duel object compiles successfully; existing standalone-only unused-function warnings remain.

The isolated N64 microbenchmark includes the actual production function and the previous scalar function, draws into cached 16-bit RAM canvases, checks pixel equality, and records CP0 intervals over 24 warmed redraws. It runs on a private ares process/settings with no debugger or VI breakpoints. Both modes and switchback match exactly. Heap use returns to the same 308,328 bytes after releasing all benchmark canvases and cache, confirming no allocation leak across mode changes.

| Mode | Previous scalar median | Cached median | Reduction | Cache | Cold cache + first draw |
|---|---:|---:|---:|---:|---:|
| 320×240 | 62.551 ms | 10.769 ms | 82.8% | 19,200 B | 14.222 ms |
| 480×360 | 139.847 ms | 25.461 ms | 81.8% | 43,200 B | 30.922 ms |

These are CPU-only emulator proxies, not complete UI frame times or physical-console measurements. They exclude canvas writeback, final RDP presentation, text/cards, audio and game logic. The static 43.10/96.97 ms per-redraw DIV stall bound from the prior audit no longer applies to warmed felt rendering.

In the compiled production object, `draw_felt` is inlined into `draw_board`. Its warmed row loop at `draw_board+0x478` through `+0x4ac` calls `memcpy` and contains no DIV. Remaining divisions belong to cache setup. See `draw-board-after.txt` and `validation.json`.

The standalone benchmark source/Makefile are retained here; `make -C port/reviews/hardware-performance/felt-optimization` builds its own ROM without touching the adventure build. The actual successful private run lived under `port/frontends/n64/adventure/build/felt-bench`, with its launch script/settings. The tested ROM was padded to 4 MiB for the private launch; create its private saves directory before launching ares to avoid the emulator's read-only-save warning dialog. No user emulator settings were changed.
