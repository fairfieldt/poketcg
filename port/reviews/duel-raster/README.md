# Duel raster hot paths

The 480×360 compact font previously called the generic rectangle API for every lit source pixel, including a second shadow pass. It now writes the same one-/two-pixel spans directly into the cached canvas, preserving both horizontal and vertical 1.5× phases and the original whole-source-pixel clipping behavior. The 320×240 font path is unchanged.

Card portraits, bench portraits and card backs now share a nearest-neighbor blitter. It calculates source column indices once per blit rather than dividing for every destination pixel on every row. Source row selection, alpha, clipping and padded stride behavior remain unchanged. The lookup uses at most 960 temporary stack bytes at the supported 480-pixel width; no heap or persistent allocation was added.

The original scalar algorithms and optimized paths produce identical bytes in 1,992 glyph cases, 3,276 scaled blits and 936 card-back cases. These include 320×240, 480×360, a 113×47 partial canvas, both coordinate phases, screen-edge clipping, transparent pixels, padded rows and untouched guard bytes. The original card-back shadow panel requires a full logical canvas, so its cases use the two full display sizes; the shared blitter itself is also checked on the partial canvas.

An isolated native benchmark uses real fonts/card sprites and a crowded board plus four lines of text. After eight warmup draws, 12 timed draws give these CPU-only medians:

| Resolution | Before | After |
| --- | ---: | ---: |
| 320×240 | 50.843 ms | 49.936 ms |
| 480×360 | 138.368 ms | 109.922 ms |

The complete canvas hashes match before/after at both resolutions: `c16242f3` at 320 and `e08501d4` at 480. Timings exclude presentation, audio polling and checksum calculation. These numbers isolate the raster change; they do not include the separately implemented board cache or represent final gameplay FPS.

The benchmark sources and ROMs remain in the ignored `port/frontends/n64/build/raster-review/` directory. [validation.json](validation.json) contains the result summary, with original sample arrays and logs alongside it. The permanent exact-pixel test is `port/frontends/n64/tests/raster_paths.c`.
