# Source-based room prop pass

The lab, shared lobby furniture and related room props now use real shapes and
owning surfaces instead of an entire projected Game Boy sprite stretched across
a brown box. The shared table has a 64×32 RGBA16 felt material (4 KiB TMEM),
with a separate rim, apron and legs. Books sit in recessed shelves; CRTs have
deep housings, broad inset screens and sloped key decks. Flat coloured geometry
shares each room's existing untextured batch.

The source audit also corrected mistaken identities: Mason's centrepiece is
a duel table with magenta stools; the lobby terminal runs PCMenu; Mason's
cabinets contain rule books; Water has parasol tables and chaise lounges;
Psychic has square-headed columns. A nonexistent south-east lab counter was
removed, and the actual north-wall readout was restored. The original source
permissions, NPC starts, script coordinates, cameras, events and destinations
remain unchanged. The L-counter return joins its existing runs; the lab stools
move .60 units outward to remain visible, with .70 clearance to the adjacent
walking columns (.6875 after native packing). All nine lobbies reuse one mesh.

[Full original audit](audit.md) · [Implemented and remaining work](audit-addendum.md)
· [Source/author-preview comparison](source-shape-comparison.png)
· [Asset isolation and detailed budgets](asset-isolation.json)

| Scene | Triangles | Material batches | RGBA16 texture data |
|---|---:|---:|---:|
| club_lobby | 870 → 1434 | 23 → 19 | 41.5 → 35.5 KiB |
| deck_machine_room_dynamic | 526 → 1748 | 13 → 11 | 23.5 → 17.5 KiB |
| mason_laboratory | 904 → 1838 | 24 → 18 | 42.5 → 33.5 KiB |
| psychic_club | 892 → 1572 | 33 → 33 | 61.5 → 61.5 KiB |
| rock_club | 1488 → 2412 | 37 → 36 | 68.5 → 67.5 KiB |
| science_club | 678 → 1664 | 39 → 30 | 70.5 → 53.5 KiB |
| water_club | 2516 → 3044 | 20 → 20 | 35.5 → 35.5 KiB |

Three new textures replace old projected prop crops: the felt plus two source
floor chunks repainted under the wall display. No retained texture's pixels
changed. All 49 full and 49 LOD character meshes, fallback actors, Hall, island
and Lightning geometry remain byte-identical to the baseline. Total referenced
textures fall from 504 to 469. The new furniture adds broad silhouette geometry
while reducing texture/material switches in the busiest affected rooms.

The 113-case prop checker tests actual source placements and independent deck
printer states, both in authored coordinates and after the native float32 →
truncate-to-1/32 conversion. Every screen opening is owned exactly once.
`check_world.py` validates all 3,528 walkable anchors, 5,817 legal edges, 141 human
NPC starts and 292 permission/event states at a .65-unit actor radius.
[Prop surface results](model-surfaces.json) and [floor surface results](room-surfaces.json)
record zero competing planar pairs.

## Reproduction

```sh
python3 port/world3d/tools/build_world.py
python3 port/world3d/tools/check_world.py
python3 port/world3d/tools/check_prop_models.py
python3 port/world3d/tools/check_room_surfaces.py
node port/frontends/web/test/world3d_test.mjs
make -C port/frontends/n64/adventure bundle
make -C port/frontends/n64/adventure BUILD_DIR=build/expansion ROM=poketcg-expansion HIGH_RES=1 bundle
```

The editable Blender scenes are in
[generated/selected_maps.blend](../../world3d/generated/selected_maps.blend).
The matching previews under `world3d/generated/previews` are author inspection
renders; the native and VI screenshots are labeled separately.

The private native A/B harness is `tests/prop_models_native.c` with
`tools/review_props.py`. It uses production renderer objects and isolated saves,
retaining the original mesh/texture pool under
`adventure/build/prop-remodel-before` (baseline commit `e5182c51`). Capture runs
must be serial: simultaneous instrumented ares instances corrupted the debug
capture stream in an initial discarded run. Those invalid logs remain under
`build/prop-remodel-review/*/runtime-parallel-invalid.log` and are excluded from
accepted results. The harness takes an exclusive capture lock.
