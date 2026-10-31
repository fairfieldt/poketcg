# All-map 3D scene assets

Every one of the disassembly's **34 map IDs** has a 3D scene entry. Interiors
use the Lightning Club camera proportions; the overworld uses an almost
vertical perspective camera over raised land, hills, buildings, and conifers.
The island is authored in Blender against the approved
[concept](../maps/prototypes/overworld_n64_concept_v1.png). It has organic grass
shelves, layered rock cliffs, sandy routes and beaches, small foam breakers,
timber bridges, boats, and linked conifer models. Each of the twelve source
locations has its own detailed building group, including doors, blue windows,
roof trim, steps, and its original broad silhouette. Landmark bodies use larger
footprints and broad roof/rune identifiers that survive the native video output.
Nearby trees leave their silhouettes clear. Logical destinations, routes and
the island camera remain the original data. The selected name is drawn by the
frontend.

`generated/overworld.blend` preserves the complete editable models and material
hierarchy. `tools/author_overworld.py` creates that scene with `bpy`, then exports
its actual mesh positions, UVs, and baked vertex colors into the native island.
Small opaque 32/64-pixel textures supply grass, rock, stone, timber, roofs,
water, foliage and roof badges; each fits RGBA16 TMEM. The native fixed-camera export omits
unseen back/bottom faces, retaining about 5,600 triangles in 300 KiB. Repeated
textures retain their original UVs; the renderer selects `GL_REPEAT` for these
batches rather than subdividing geometry at every texture boundary. The complete
Blender models retain their hidden faces.

The Lightning Club retains the approved standalone prototype, with its solid
props and doorway aligned to the original movement grid. Its camera remains `(14,9,34)` looking toward
`(14,0,21.5)`, with a 58-degree vertical field of view.

## Reuse

All nine club lobbies refer to one mesh file. The nine club/challenge foyers
refer to one shell with separate floor-emblem overlays. Palms, cabinets,
workstations, tables, chairs, statues, boulders, shrubs, topiaries, torches,
braziers, and other props use common mesh recipes. Original projected prop
silhouettes are removed from floor textures before real geometry replaces
them. Textures are globally deduplicated by content hash, including shared
laboratory/deck-machine and dome/honor materials.

Fallback characters share four rigidly jointed human archetypes (player, club
member, lab coat, and female character), plus legendary-card and overworld
cursor models. Finished individual exports replace these fallbacks per source sprite family.
All 39 canonical families and three palette variants are currently imported,
covering all 115 source NPC aliases, with separate Amy lying and Legendary Card
component meshes. Native NPC IDs, exact start coordinates,
directions, preload conditions, and off-map scripted starts remain in the
manifest; gameplay selects visibility and movement through the story engine.

## Files and regeneration

```sh
python3 port/world3d/tools/build_world.py
python3 port/world3d/tools/build_world.py --overworld-only
python3 port/world3d/tools/check_world.py
python3 port/world3d/tools/check_solid_terrain.py
python3 port/world3d/tools/check_characters.py
/Applications/Blender.app/Contents/MacOS/Blender --background --python-exit-code 1 \
  --python port/world3d/tools/author_overworld.py
/Applications/Blender.app/Contents/MacOS/Blender --background --python-exit-code 1 \
  --python port/world3d/tools/check_overworld_roofs.py
/Applications/Blender.app/Contents/MacOS/Blender --background \
  --python port/world3d/tools/blender_world.py -- --render
```

The interior asset generator uses Python's standard library and the existing map PNG
reader. A normal full generation preserves the checked-in Blender-authored
island; `--overworld-only` invokes Blender to re-author it (set
`POKETCG_BLENDER` if Blender is installed elsewhere). It extracts metadata from the original built ROM/symbols, with shared
map rules from `port/maps/3D_GUIDE.md` and hand-reviewed prop elevations in
`tools/scene_specs.py`. Building the N64 ROM from checked-in assets needs
neither Blender nor the original Game Boy ROM.

Island roofs use one closed outer shell per tier. Colored slopes, caps and
white fascia share their boundary vertices; there is no complete trim roof
under another roof. Round roofs have real recessed centers. Roof identifiers
are painted into their owning cap faces. The authoring export checks all twelve
roof sites for manifold shells, duplicate faces and competing surfaces from
the native camera before publishing meshes. `check_overworld_roofs.py` repeats
the check against the saved Blender artifact.

- `generated/world3d_maps.h`: native-map-indexed scene paths and cameras.
- `generated/world.json`: all maps, source NPC/warp/permission data, mesh
  counts, typed prop placements, overworld destinations, and event variants.
- `generated/maps/*.l64`: static indexed triangle meshes, including shared
  foyer/lobby shells and emblem overlays.
- `generated/actors.l64`: shared rigid character parts, with the same model
  and joint IDs as the Lightning prototype.
- `generated/textures/*.png`: power-of-two textures, each within 4 KiB TMEM
  when converted to RGBA16 with `mksprite`.
- `generated/overworld.blend`: the complete concept-led island, with twelve
  named building groups, shared trees, authored textures, and the native camera.
- `generated/overworld_authoring.json`: checked-in native mesh/camera provenance,
  retained during full interior asset regeneration.
- `generated/all_maps.blend`: 34 editable Blender scenes sharing the same
  mesh and texture datablocks. Each scene includes a camera, entrance player,
  and the original NPC starts for inspection.
- `generated/previews/`: Blender inspection renders; these are not emulator
  screenshots. All conditional NPCs are shown together in these previews.
- `generated/native/`: actual N64 RDP captures from the 4 MiB diagnostic: all
  34 maps and a completed native AI duel, plus the ROM hash and validation
  evidence. `all-maps-native.png` is the combined gallery.

The final native diagnostic also passes the production story campaign fixture,
714 source NPC branches, 144 navigation paths, an actual AI duel through the
existing duel UI/audio, and a two-diary SRAM save/reload. Per-map measured
heap values and the worst free-heap result are recorded in the native gallery
validation JSON alongside its ROM hash. This is emulator evidence, not
a physical-console measurement; campaign fixture duel outcomes are scripted
separately from the actual native AI duel.

L643 uses big-endian 24-byte vertices (XYZ float, UV float, RGBA8), 16-bit
indices, and material/rigid-part batches. Runtime coordinates are X east,
Y up, Z south; one map tile is one world unit. The Blender importer converts
these to its right-handed Z-up coordinates.

## Map events

The challenge-machine installation, opened dome/honor doors, and all-lit deck
room are supplied as alternate scenes. The all-lit deck-room scene represents
all eight enabled medal printers; it must not be selected on the first medal.

For independent printer states, load `W3_DECK_MACHINE_BASE`, then one off/on
front from each of the eight `w3_switch_scenes` entries according to native
map-event bits 2..9. The alternate shell omits those eight faces, so each
state is a replacement surface with no coplanar overlap.

The original permission grid and story map events are authoritative for
movement. Texture crops retain the source image rectangles. Each solid has a separate
authored floor footprint and an exported body bound, aligned to the logical
actor centres at the original even coordinates. `check_world.py` checks a
0.65-unit actor radius at every walkable anchor, along every legal cardinal
step, and at human NPC starts, including all permission-event combinations.
It records results in `generated/prop-clearance.json`. The Water pool and
imported Lightning triangles have an additional geometry check. Lobby chairs
are tucked under the table because their original surrounding tiles are
walkable. Amy rests on her bench; Legendary Card figures float above the
altar. These elevations affect presentation alone. Original broad floor
motifs, carpeting, pool outline, lawn stone positions, and wall livery are
preserved as texture details. Some geometric shapes remain simplified
prototypes rather than final art; water and arena elevation still use the
source floor composition.

## Individual character discovery

`tools/sync_characters.py` imports finished standalone L643 exports without
regenerating room meshes, room textures, or Blender previews. The adventure
build runs this before parsing its asset wildcards. It scans:

- `port/characters/assets/models/<family>/` (completed central exports)
- `port/characters/models/<slug>/`
- `port/characters/generated/models/<slug>/`
- `port/frontends/n64/lightning-characters/assets/models/<slug>/`

Each directory must contain `scene.l64`, `scene.json`, and every referenced
PNG. Central exports use shared `port/characters/assets/textures/`, place
palette variants in `<family>/variants/<appearance>/`, poses in
`<family>/poses/<pose>/`, and card components in
`<family>/components/<component>/`. The central `<family>/complete.json` must
exist, list the appearance, and be newer than that export; it is checked again
after reading. Legacy standalone exports use local `textures/`. Binary mesh counts must match the JSON metadata and
textures must fit TMEM. Partial new exports are skipped; a partial update to
an existing model retains its last coherent import. Source files are read
without modification. Output meshes, headers, and JSON are published atomically;
an unchanged sync preserves their mtimes, so it does not rebuild the ROM.

The canonical-family and appearance aliases come from
`port/characters/catalog.json`. Imported IDs are `7 + original sprite ID`,
separate from fallback IDs 1..6 and the authoring demo's private IDs. Thus
Ronald's source model1 becomes runtime model8; Dr. Mason's source model4 becomes
runtime model9. `W3_PLAYER_MODEL` now selects the finished Mark model7,
while fallback model1 remains unchanged; Ronald never replaces it. Palette-specific appearance exports get IDs128+ and override
their canonical family only when available. Pose exports use IDs160+ and card
components IDs176+. `w3_npc_pose_models` provides the baked Amy lying model
for the runtime to select when her original laying animation is active; it
should not receive a second tilt transform. Legendary aliases select their
individual card quadrants or sparks directly through `w3_npc_models`.

`w3_actor_scenes[256]` maps these imported IDs to independent
`rom:/world/maps/actor_<slug>.l64` files. The runtime loads models only for the
current visible NPCs, retaining shared fallback actors separately; it does not
load every finished model into the 4 MiB heap. Textures still use the same
content-hashed global texture pool. Geometry, UVs, baked colors, indices, and
rigid pivots are preserved from the source export. The Blender importer resolves
these same individual files when rebuilding `all_maps.blend`.
