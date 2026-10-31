# Geometry and existing emulator timing audit

Read-only analysis of current generated L643 files and the renderer; no new emulator run. Reproducer: `audit_geometry.py`. Machine-readable output: `geometry.json`.

## Method

- Parsed native L643 headers and indexed payloads, then counted precisely the room, optional overlay, per-map switches, each configured-visible actor, player, and contact-shadow fan. Counts are submitted input, before RSP clipping.
- `main.c:145` submits the entire room and overlay; every visible actor is submitted. There is no application-level frustum/distance culling. `scene.c:47` disables backface culling. Rasterized output may be far smaller.
- Mesh batches are already material sorted and compiled into cached indexed GL display lists. Rooms use a nested cached list. Do not propose caching or indexing as new improvements.
- Verified libGL `VERTEX_CACHE_SIZE=32` in `src/GL/gl_constants.h:16`. `primitive.c:206,252-286` resets a 32-slot LRU per glDrawElements. Simulated precisely the index order of every mesh; triangle lists do not lock vertices. Vertex loads are emitted RSP vertex-loader commands, excluding clipping-created vertices and immediate shadow-fan vertices. This is stronger than counting source vertices.
- All configured rows include every in-bounds NPC entry, including conditional story entries. They are synthetic census scenarios, not proof those NPCs coexist in a normal save. Unconditional is a separate configured lower census, not a universal minimum.
- Contact fans add12 triangles each and one immediate draw call each. Amy lying/elevated pose suppresses its shadow; generic all-configured Water table assumes standing, so actual lying pose subtracts12 triangles and uses model160 (same584tri,30more source vertices).
- Raw texture bytes are width × height ×2 for loaded RGBA16 bitmaps, excluding sprite/GL headers, alignment, compiled list state, mip levels (none), allocator overhead. Repeated NPC models share one Scene; separate Scene objects load their textures independently. Catalog content deduplication is not a global runtime texture cache.
- Map2 includes dynamic base and all8off-state panels; on-state counts match. Event room variants are inventoried separately in scene_table. Default other map rows use event-off rooms.

## Submitted census

Cached batch calls exclude immediate shadow-fan calls; total triangles include shadows. Actor count includes player (overworld also includes cursor). All configured scenario below.

| Map | NPCs | Static tris / batches | Total tris / cached batches | Vertex loads / unique source | Raw static + actor textures |
|---|---:|---:|---:|---:|---:|
| 0 OVERWORLD_MAP | 0 | 5,511 / 18 | 6,513 / 25 | 13,564 / 12,531 | 40,960 + 2,048 B |
| 1 MASON_LABORATORY | 7 | 904 / 24 | 6,602 / 72 | 12,509 / 11,220 | 43,520 + 6,144 B |
| 5 FIGHTING_CLUB_LOBBY | 7 | 870 / 23 | 6,341 / 71 | 12,134 / 10,923 | 42,496 + 14,336 B |
| 6 FIGHTING_CLUB | 4 | 1,214 / 12 | 4,676 / 42 | 9,042 / 8,298 | 19,968 + 8,192 B |
| 9 ROCK_CLUB | 3 | 1,488 / 37 | 4,363 / 61 | 8,292 / 7,527 | 70,144 + 8,192 B |
| 12 WATER_CLUB | 4 | 2,516 / 20 | 5,922 / 50 | 10,126 / 9,313 | 36,352 + 10,240 B |
| 15 LIGHTNING_CLUB | 4 | 626 / 35 | 4,069 / 65 | 7,855 / 7,090 | 68,096 + 8,192 B |
| 18 GRASS_CLUB | 3 | 4,304 / 36 | 7,102 / 60 | 13,792 / 13,179 | 67,072 + 8,192 B |
| 20 PSYCHIC_CLUB_LOBBY | 7 | 870 / 23 | 6,886 / 71 | 13,183 / 11,666 | 42,496 + 14,336 B |
| 21 PSYCHIC_CLUB | 4 | 892 / 33 | 4,872 / 63 | 9,266 / 8,077 | 62,976 + 8,192 B |
| 24 SCIENCE_CLUB | 4 | 678 / 39 | 4,302 / 69 | 8,007 / 7,145 | 72,192 + 6,144 B |
| 27 FIRE_CLUB | 4 | 822 / 35 | 4,414 / 65 | 8,294 / 7,335 | 67,072 + 10,240 B |
| 29 CHALLENGE_HALL_LOBBY | 9 | 870 / 23 | 8,164 / 83 | 15,620 / 13,964 | 42,496 + 18,432 B |
| 32 POKEMON_DOME | 5 | 1,518 / 54 | 6,380 / 90 | 12,094 / 10,748 | 107,008 + 12,288 B |
| 33 HALL_OF_HONOR | 6 | 536 / 27 | 1,582 / 39 | 2,891 / 2,589 | 52,736 + 2,048 B |

Naming has Mark906tri/6batches +24tri immediate fan =930tri,1,783 mesh vertex loads (1,481 unique) plus26fan vertices. No room. Duel has no 3D scene geometry; its CPU 2D renderer needs a separate audit.

The 39-family/49-export character catalog and all98 room exports are not per-frame costs. Only the loaded current scene and needed models are resident/submitted.

## Existing timings

Median microseconds,24 samples per stage per resolution. DIAG10 interval begins after attach/context and ends after GL end + rdpq_detach_wait, excluding UI/maplabel and final display queue. DIAG11 includes attach/context but similarly excludes final labels/display. No physical-console FPS claim is made.

| Run / stage |320×240|480×360|
|---|---:|---:|
| game / overworld | 114,179.5 | 114,189.5 |
| game / lab | 107,184.0 | 107,227.0 |
| game / lightning | 70,268.5 | 70,187.5 |
| game / dome | 103,285.0 | 103,224.5 |
| game / dialogue | 30,880.0 | 30,698.0 |
| leads-after / mark-portrait | 16,108.5 | 16,071.5 |
| leads-after / ronald-portrait | 13,503.5 | 13,475.0 |
| leads-after / mason-portrait | 13,304.5 | 13,297.5 |
| leads-after / imakuni-portrait | 10,177.0 | 10,133.5 |
| leads-after / leads-room | 110,116.5 | 110,079.5 |
| leads-after / leads-conversation | 61,804.5 | 61,678.0 |

DIAG10 stage tags matter: lab and dialogue both print map1; combining their48 samples gives a meaningless mixed median around69ms. Actual lab force-loads7NPCs+player (6,602tri). Dialogue resets game to emptylab+playeronly (1,822tri). Overworld0NPC+Mark+cursor; Lightning4NPC+player; Dome5NPC+player.

DIAG11 room draws two copies each of Mark/Ronald/Mason/Imakuni (8actors); conversation draws one each (4actors). Existing logs predate Ronald hair repair: oldRonald728tri, versus current910. Those captured totals are6,740tri/72cachedbatches+8fans for leads-room;3,822tri/48cachedbatches+4fans for leads-conversation. Captured Dome used6,198tri, current census6,380. Portrait timing for Ronald similarly reflects the old mesh.

The near-identity across2.25× pixels is evidence of resolution-independent geometry/command work in this emulator interval. It does not measure physical RDP fill time. Captures use paraLLEl-RDP and hostGPU work may not account for console raster cycles.

## Geometry optimization implications

- Actor LOD/culling is the largest geometry opportunity in interiors: lab actor geometry5,602of6,602tri; static room only904. A50%actor-geometry reduction removes about42%submitted triangles there; Lightning about42%; Grass only19%. This is a workload reduction, not an equal guaranteed FPS gain. Preserve highdetail leads in close conversation/naming, reduce bodies/faces offscreen or at gameplay distances.
- World geometry is mainly static5,511of6,513tri; NPC LOD will not solve it. Spatial chunks/landmark visibility and simpler distant land/tree/roof detail target the actual cost. Whole-room material sorting currently loses spatial locality; chunking adds batch overhead, so use coarse chunks.
- Index reordering has limited headroom: perfect32-entry reuse could remove at most7.6%ofworld vertex loads,10.3%lab,9.7%Lightning,4.4%Grass,11.1%Dome,10.4%Hall. Those percentages are ceilings for vertex-loader reduction alone; triangle-setup cost remains. Static interior meshes already have zeroLRUrepeated loads, so focus on character indices. Mark1,783vs1,481unique gives16.9%load ceiling; currentRonald15.4%; Mason10.2%; Imakuni9.3%.
- Correct winding and selective backface culling can reduce raster/triangle-setup output, but will not remove vertex input. Avoid a blind global toggle: wall/card/fabric surfaces may be intentionally two-sided; author per-material flags and validate captures.
- The configured maximum among all34maps is ChallengeHalllobby29 at8,164tri,10actors,83cachedbatches,15,620meshvertexloads. Conditional entries may be mutually exclusive; a gameplay-save census is needed before calling it the real worstcase.
