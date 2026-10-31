# Real N64 performance projection

2026-10-31. Read-only analysis of the current cartridge renderer, generated assets, installed libdragon preview, shipping ELF, and existing native ares captures. No physical console measurement, new emulator run, production edit, or SDK change was made. The recorded binary hashes identify the audit snapshot; the conversation camera adjustment being integrated afterward does not reduce submitted meshes or actors.

**The game fits in both RAM configurations, but the current renderer is not a credible steady 30 fps design yet.** Two separate costs matter: several thousand transformed triangles every world frame, and an avoidable CPU division loop in the duel background. The Expansion Pak supplies memory; it does not accelerate the CPU, RSP, or RDP. Its 480×360 mode draws 2.25 times as many pixels.

The fastest useful fixes are to cache the duel felt at its destination resolution, reduce distant character geometry and unseen submissions, and benchmark a packed 3D backend on one representative scene. Keep the close-up lead characters and the completed-canvas presentation path.

## What is established

| Evidence | Confidence | Meaning |
|---|---|---|
| Current asset/index census and shipping ELF disassembly | High | Exact submitted input and executed inner-loop instructions, subject to stated NPC scenarios |
| Full-map heap snapshots and naming peak in existing diagnostics | High for those captures | Both configurations have meaningful spare RAM; no evidence of a capacity failure |
| CP0 intervals in the existing ares diagnostic logs | High as emulator observations | Useful geometry-path proxy, not measured console FPS |
| Per-scene physical-console frame-rate bands below | Low to moderate | Engineering planning estimates pending console profiling |
| Exact physical RDP fill, RDRAM contention, cache misses, cold cartridge stalls | Unknown | Existing captures do not measure them accurately |

Audited ROMs are 36,372,480 bytes each (34.69 MiB). Their hashes and the ELF hash are recorded in [budgets.json](budgets.json). The inspected ELF has 678,832 bytes of `.text`, 579,896 bytes of `.rodata`, 29,432 bytes of `.data` + `.sdata`, and 149,772 bytes of `.bss` + `.sbss`; `size` aggregates executable/read-only sections differently. About 31,440,301 bytes of the filesystem payload are streamed music/SFX, versus 3,049,274 bytes of world assets and 302,780 bytes of card sprites. ROM size is principally audio storage, not resident scene RAM.

## The duel has a concrete CPU bottleneck

`tcgn64.c:draw_felt` builds a 32×32 color tile once, but **recomputes scaled tile coordinates for every destination pixel on every board redraw**. The cached tile does not cache the completed felt background.

The executed logical rectangle is x=[10,310), y=[8,190): 300×182 pixels. At 480×360 it becomes x=[15,465), y=[12,285): 450×273 pixels. Shipping `draw_board` assembly contains **two variable integer `DIV` instructions per inner-loop iteration**, including the y division that could have been outside the loop. The divisions are followed by `MFLO`; they are not an overlapped stream of independent results. See [felt-disassembly.txt](felt-disassembly.txt).

The NEC manual specifies that integer multiply/divide stalls the entire VR4300 pipeline, with `DIV` requiring 37 processor cycles. Nintendo documents a 93.75 MHz CPU clock. These permit a static hardware cost calculation independent of ares. [NEC VR4300 manual, Table 3-12, printed p.76](https://datasheets.chipdb.org/NEC/Vr-Series/Vr43xx/U10504EJ7V0UMJ1.pdf), [Nintendo CPU specifications](https://ultra64.ca/files/documentation/online-manuals/man-v5-1/kantan/step1/2/2_2.htm).

| Per complete board redraw | 320×240 | 480×360 |
|---|---:|---:|
| Felt pixels actually visited | 54,600 | 122,850 |
| Executed DIV operations | 109,200 | 245,700 |
| DIV stall cycles alone | 4,040,400 | 9,090,900 |
| DIV stalls at 93.75 MHz | **43.10 ms** | **96.97 ms** |
| FPS ceiling from those stalls alone | **23.2** | **10.3** |

These are lower bounds on work time, not timings of the complete duel. They exclude loop instructions, framebuffer stores, text, card scaling, cache writeback, RDP copy, audio and logic. Board-based acknowledgement, choices, arena and attack loops call `begin_frame` → `draw_board` → `end_frame_poll` on each iteration, even while waiting for input. Some full card/list pages do not draw the board and do not incur this particular cost.

**Recommended fix:** precompute the small set of physical felt rows once for each display mode and copy the selected row into the cached UI canvas. A 450×48 repeating pattern is only 43,200 bytes; caching the entire 450×273 felt rectangle uses 245,700 bytes. Row copies avoid all per-pixel divisions and preserve the exact image. Hoisting only the y division still leaves half the division cost. RDP texture tiling is another option, but it needs deliberate ordering with CPU canvas drawing.

Retain cache writeback, the RDP presentation copy, and its completion fence. Those solve real tearing/reuse hazards. After this fix, profile `draw_felt`, `sprite_logical`, `raster_glyph`, total CPU UI work and presentation separately. The anti-aliased font blends and CPU art scaler remain measurable costs; the compiler already replaces division by constant 15 in `raster_glyph` with arithmetic, so it is not the same two-DIV problem.

## Actual submitted world geometry

The renderer already compiles indexed material batches into RSP queue blocks, sorts room materials, caches a whole-room list, keeps loaded room assets, loads needed character models, and has a 24-entry card-art LRU. Adding those caches again will not fix frame time.

`main.c:game_render_frame` submits the entire room, overlay and switch geometry, then every game-visible NPC and the player. There is no application frustum/distance rejection. `scene.c:scene_begin_opaque` disables backface culling. The RSP does perform triangle clipping/trivial rejection, so submitted input is larger than rasterized output; that rejection still requires input loading/clip transforms. Cached display lists remove CPU rebuilding, not the RSP transformation and triangle-setup work each frame. [Installed-equivalent libdragon list implementation](https://github.com/DragonMinded/libdragon/blob/12bba06615ce9e89185947bbbeba41e290ec0c23/src/GL/list.c), [RSP pipeline](https://github.com/DragonMinded/libdragon/blob/12bba06615ce9e89185947bbbeba41e290ec0c23/src/GL/rsp_gl_pipeline.S).

The table counts submitted indexed triangles including contact-shadow fans. Cached batches exclude immediate shadow-fan draws. “All configured” forces every in-bounds NPC entry visible; conditional entries may be mutually exclusive in real saves. See the full 34-map census and event variants in [geometry.json](geometry.json), with methodology in [geometry.md](geometry.md).

| Scene, all configured NPCs | NPCs | Static triangles | Total triangles | Cached batches | RSP mesh vertex loads |
|---|---:|---:|---:|---:|---:|
| Overworld | 0 | 5,511 | 6,513 | 25 | 13,564 |
| Mason's lab | 7 | 904 | 6,602 | 72 | 12,509 |
| Lightning Club | 4 | 626 | 4,069 | 65 | 7,855 |
| Water Club | 4 | 2,516 | 5,922 | 50 | 10,126 |
| Grass Club | 3 | 4,304 | 7,102 | 60 | 13,792 |
| Science Club | 4 | 678 | 4,302 | 69 | 8,007 |
| Challenge Hall lobby | 9 | 870 | 8,164 | 83 | 15,620 |
| Pokémon Dome | 5 | 1,518 | 6,380 | 90 | 12,094 |
| Hall of Honor | 6, mostly props | 536 | 1,582 | 39 | 2,891 |

The entire 39-family/49-export character catalog and 98 room exports are not per-frame submissions. Current Mark is 906 triangles, Ronald 910, Mason 718 and Imakuni 518; each has six batches. Naming submits Mark plus a 24-triangle floor fan, **930 triangles total**, without a room. The duel submits zero 3D scene triangles.

Index reuse is already real. Simulating the installed GL pipeline's exact 32-entry LRU gives ceilings of only 7.6% fewer vertex loads in the overworld, 10.3% in the lab, 9.7% in Lightning and 11.1% in the Dome even with ideal index ordering. Triangle setup would remain. Static interior meshes already achieve one load per unique vertex. Reordering is useful, but cannot deliver the several-fold gain needed by itself.

## Existing timings and the hardware projection

At 30 fps the budget is 33.33 ms, or 3.125 million CPU cycles and 2.083 million RSP clocks; 20 fps allows 50 ms and 15 fps allows 66.67 ms. These are separate concurrently operating engines, not cycles that can be added into one budget. The RSP also services audio. SGI documents its 62.5 MHz clock. [SGI RSP Programmer's Guide, Clock Speed](https://ultra64.ca/files/documentation/silicon-graphics/SGI_Nintendo_64_RSP_Programmers_Guide.pdf).

Existing DIAG10 intervals begin after display acquisition/GL context setup and end after `rdpq_detach_wait`; they exclude scene loading, final UI labels, display pacing, and audio polling. Each row has 24 samples:

| Captured scenario | 320×240 median | 480×360 median |
|---|---:|---:|
| Overworld | 114.18 ms | 114.19 ms |
| Lab, seven forced NPCs + player | 107.18 ms | 107.23 ms |
| Lightning, four NPCs + player | 70.27 ms | 70.19 ms |
| Dome, five NPCs + player | 103.29 ms | 103.22 ms |
| Empty lab + player, dialogue fixture | 30.88 ms | 30.70 ms |
| DIAG11: eight lead actors in lab | 110.12 ms | 110.08 ms |
| DIAG11: four lead actors in conversation | 61.80 ms | 61.68 ms |
| DIAG11: Mark alone, portrait | 16.11 ms | 16.07 ms |

The dialogue fixture is not a populated conversation scene. Its game state is reset with no NPCs. Combining its samples with the full lab under map ID 1 would produce a meaningless ~69 ms median. Existing portrait/lead/Dome logs also predate Ronald's 728→910 triangle repair, so their current geometry workload is slightly higher.

**Why these are proxies:** inspected ares revision `0aafd85789215e84e1e43415c07d4c88461b7899` models RSP instruction pipeline clocks, while its RDP consumes commands through the host renderer and clears busy state on `SyncFull`; it does not model realistic per-pixel raster/RDRAM time. Identical times across 2.25× pixels identify resolution-independent work in this emulator interval, but do not establish identical hardware performance. [ares RSP execution](https://github.com/ares-emulator/ares/blob/0aafd85789215e84e1e43415c07d4c88461b7899/ares/n64/rsp/rsp.cpp), [RDP command/busy handling](https://github.com/ares-emulator/ares/blob/0aafd85789215e84e1e43415c07d4c88461b7899/ares/n64/rdp/io.cpp), [RDP synchronization](https://github.com/ares-emulator/ares/blob/0aafd85789215e84e1e43415c07d4c88461b7899/ares/n64/rdp/render.cpp).

For planning, I would expect the current code to fall broadly in these bands. They assume the emulator's geometry-path cost remains of the same order on the console, with additional fill/cache/audio cost. They are deliberately wide engineering estimates, **not measured FPS or statistical confidence intervals**. The current data cannot calibrate a precise emulator-to-console multiplier.

| Workload, current implementation | 320×240 planning band | 480×360 planning band | Main limit |
|---|---:|---:|---|
| Overworld, current full island | **6–11 fps** | **5–10 fps** | Static geometry, vertex loads/triangle setup; high-res fill uncertain |
| Typical club, ~3,000–4,500 triangles | **10–18 fps** | **8–16 fps** | Actors and geometry setup |
| Crowded lab/club/Dome, ~6,000–8,000 triangles | **6–12 fps** | **5–11 fps** | Geometry; some synthetic NPC maxima exceed normal saves |
| Sparse room/Hall, ~1,500–2,000 triangles | **20–30 fps** | **15–30 fps** | Geometry near 30 fps budget plus fill/UI |
| Naming, Mark + keyboard | **25–30 fps** | **20–30 fps** | One actor and CPU UI; no complete-frame timer exists |
| Duel board, current felt loop | **12–18 fps** | **5–8 fps** | CPU; hard ceilings 23.2 / 10.3 fps from DIV stalls alone |
| Card/list pages without board | **20–30 fps** | **15–30 fps** | CPU text/art; weakest projection because unprofiled |

A very favorable console result could exceed a geometry band, or memory stalls could push it below; only the duel division bound is a static hardware constraint. Do not ship a promised frame rate based on these estimates. They are sufficient to prioritize work: a blanket 30 fps target needs major geometry-path savings as well as the felt fix. Lowering the frame cap to 20 does not make a 100 ms scene meet its 50 ms budget.

## Resolution, RAM, bandwidth and audio

Both modes have three RGBA16 display buffers, one full-size depth surface, and one persistent cached UI canvas. Naming temporarily adds another depth surface and canvas.

| Surface/RAM evidence | 320×240, 4 MiB | 480×360, 8 MiB |
|---|---:|---:|
| One full-size 16-bit surface | 153,600 B | 345,600 B |
| Five persistent surfaces | 768,000 B | 1,728,000 B |
| Seven surfaces during naming | 1,075,200 B | 2,419,200 B |
| Full 34-map diagnostic minimum free heap | 929,184 B | 4,165,072 B |
| DIAG10 naming peak free heap | 746,152 B | 3,596,456 B |

Heap figures are from prior diagnostic binaries, before the final ~5 KiB Ronald geometry increase, and are not a maximum-contiguous-allocation guarantee. They include the real engine/UI linked into those diagnostics. See `port/reviews/expansion-pak/validation.json`, full sweeps `build/final-standard-native-v2/result.json` and `build/expansion-native-8mib-v2/result.json`. Persistent surface growth is exactly 960,000 bytes; the Expansion Pak still leaves much more room than the base mode. Extra RAM is useful for geometry/text caches and streaming, not extra execution clocks.

For bandwidth scale, one full surface payload at 30 redraws/s is 4.608 MB/s versus 10.368 MB/s. A UI canvas cache writeback plus RDP source read plus destination write already moves roughly three such payloads, before CPU background reads or VI scanout. World frames also clear color and Z, read/write depth/color, and load textures. These compete for shared memory. Do not divide total bytes by the advertised peak RDRAM bandwidth and call it a frame-time estimate.

RDP copy/fill can process four 16-bit pixels per clock under ideal conditions; textured rendering is one pixel per clock in one-cycle mode and one per two clocks in two-cycle mode. Nintendo explicitly warns that memory latency, buffering and short triangle spans reduce these peaks. This is why 480×360's physical fill cost cannot be inferred from the near-identical ares times. [Nintendo RDP pipeline manual](https://ultra64.ca/files/documentation/online-manuals/man/pro-man/pro12/12-01.html).

World textures are RGBA16, mostly 32×32 (2 KiB); the largest current dimensions are 32×64/64×32 (4 KiB), so individual textures fit the RDP's 4 KiB TMEM. The Dome has 53 static textures, Science 38, Lightning 34: repeated TMEM uploads/state changes can matter even when total resident texture bytes are modest. Their static batch counts are 54, 39 and 35 respectively. Separate character scenes can duplicate texture residency; shared model instances already share their scene. Palette conversion/atlasing may help bandwidth later, but it will not remove the primary transform work.

Audio uses 32 kHz stereo, eight approximately 40 ms output buffers (~320 ms total when filled), eight mixer channels, and streamed WAV64 music/SFX. Libdragon already submits mixer work through the RSP high-priority queue. Audio is polled after rendering, so long synchronous rendering or cold scene loads can drain the queue even if average utilization seems acceptable. Measure minimum queued samples and underruns before changing buffer count. Filling available buffers before heavy loads is safer than assuming eight always remain full. [libdragon audio buffer sizing](https://github.com/DragonMinded/libdragon/blob/12bba06615ce9e89185947bbbeba41e290ec0c23/src/audio.c), [mixer high-priority scheduling](https://github.com/DragonMinded/libdragon/blob/12bba06615ce9e89185947bbbeba41e290ec0c23/src/audio/mixer.c).

The user also reports intro music skipping in ares. The intro is a simple 2D workload, so world triangle cost does not establish its cause. Inspect startup prefill, asset/music opening, wait/fade loops that omit audio polling, stream decoding and emulator audio pacing independently. Later audio fixes should be verified with both cold startup and map loads; this audit does not claim to diagnose the reported intro artifact.

`presentation_step`, movement durations and several script/UI waits advance by rendered frames. Falling from 30 to 10 fps can make movement/cinematics about three times slower unless the engine supplies compensating ticks. Keep an eventual fixed 30 Hz presentation clock with bounded catch-up and interpolate renders; measure and preserve story waits rather than globally multiplying movement speeds. This fixes pacing, not rendering throughput. Test PAL 50 Hz separately; all numerical frame budgets here use NTSC/30 fps.

## Ranked changes

| Priority | Concrete change | Expected benefit | Risk / measurement |
|---|---|---|---|
| 1 | `tcgn64.c:draw_felt`: destination-resolution row cache/copies; keep completed-canvas fence | Removes 43.1/97.0 ms of DIV stalls per board redraw, plus most loop arithmetic | Low. Pixel-identical canvas comparison, disassembly and CPU timing at both resolutions |
| 2 | `scene.c`/exporter: A/B a packed N64-native path on one fixed room + actor workload, e.g. Tiny3D alongside libdragon | Largest potential geometry gain; no honest speedup factor until measured | Medium/high. Preserve vertex colors, depth, three-point filtering and six rigid animated parts; compare RSP time, memory and VI output before a migration |
| 3 | Character export + `main.c:game_render_frame`: gameplay-distance LOD, conservative actor sphere rejection; retain full lead heads in close shots | Halving all actor triangles, including player, removes ~42% of lab/Lightning submissions; halving NPCs alone removes ~36% / ~30%. Culling one unseen lead avoids ~500–900 tris and six batch calls | Medium. Save/state sweep, conversation/naming fidelity, no popping or dialogue actor disappearance; measure actual rejected work |
| 4 | World exporter + `scene_draw_room`: coarse island/room chunks with bounds; simplify distant island trees/land details | World is 5,511 static tris; removing half visible-input world geometry cuts total submitted tris ~42%. Grass has a similarly large static share | Medium. Chunking trades fewer triangles for more calls/texture changes; preserve depth and compare moving cameras |
| 5 | Offline index reorder; per-material closed-mesh backface flag | Modest vertex-load savings, potentially significant hidden-surface triangle setup/fill savings | Low/medium. Reuse ceiling 4–13% overall; global culling would break deliberately two-sided walls/clothing. Validate winding before enabling |
| 6 | UI dirty/static-layer caches; optionally RDP atlas text/art once measured | Avoids redrawing unchanged pages and CPU per-pixel art/font work; makes 30 fps UI plausible after felt fix | Medium. Correct cursor/blink/damage/text invalidation; never redraw a canvas while RDP still reads it |
| 7 | Profile buffer placement, texture packing and audio service; then fixed-tick presentation | Reduces hardware-specific contention and protects pacing/audio under load | Measurement dependent. No blind fence removal, texture quality reduction, or buffer-count reduction |

Tiny3D remains a libdragon-compatible backend; its own documentation describes packed vertex layout, DMA-fed vertices/matrices and RDPQ interoperability. This is a targeted experiment, not a claim that an API swap automatically reaches 30 fps. [Tiny3D design/API](https://github.com/HailToDodongo/tiny3d).

## The next useful measurement

Keep one deterministic performance ROM with unchanged gameplay assets and fixed poses: world, populated lab, sparse room, Lightning, Grass, populated Dome, naming, idle duel, cursor movement, card detail and a full bench/energy state. Record cold first frame separately from at least 300 steady frames; report median, p95, maximum and missed 33.33 ms deadlines.

Split CP0 timers into scene load, game/presentation, CPU command submission, RSP completion, RDP completion, UI CPU drawing, UI copy, audio mixing, and display acquisition/pacing. An end-to-end `detach_wait` timer conflates several of those. Enable a separate profiled libdragon build (`RSPQ_PROFILE` is currently 0) and collect `rspq_profile_get_data` overlay time, Wait CPU, Wait RDP and DP busy. The profiler adds overhead; compare its aggregate frame cost against the release SDK. [Profiler API and slots](https://github.com/DragonMinded/libdragon/blob/12bba06615ce9e89185947bbbeba41e290ec0c23/include/rspq_profile.h).

Run the same ROM on a real 4 MiB and Expansion Pak console via a supported development flashcart when available. Buffer measurements and dump them after the sample window: per-frame USB/debug output can itself alter performance. Until then, the static DIV audit and the workload census already justify the first optimization pass without pretending screenshots establish hardware FPS.

Reproduce the local analysis with `python3 port/reviews/hardware-performance/audit_budgets.py` and `python3 port/reviews/hardware-performance/audit_geometry.py`. These read existing artifacts and update only this review directory; they do not rebuild or launch ares.
