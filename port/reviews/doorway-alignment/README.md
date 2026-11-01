# Doorway and floor-art alignment

Doorway mats were one tile off their openings, and each opening in a tall wall
showed the frame's clear colour as a full-height dark "beam".

A source actor at anchor (x,y) is drawn over tiles x..x+2, y..y+2, but the 3D
world puts actors, prop bodies and door gaps at the anchor itself. The floor
art was sampled at its own tile position, so every carpet, runner, emblem and
floor seam sat one tile east/south of the doors, props and actors. The east and
south walls also stood two and three tiles outside the walkable area, while
the west and north walls stood just outside it.

Changes, all in `port/world3d/tools`:

- Floor art is sampled one tile east/south of its world position in every room,
  including the Lightning prototype floor and the foyer emblems.
- Side-wall doorways (foyer west, lobby/lab east, deck room west) take their
  mats from one further tile south, because the source draws them below the
  projected end of the upper wall segment.
- East walls stand at `w-2.9` and south walls at `h-4.9`, matching the west and
  north walls and the source art. The Fighting Club south wall stays behind its
  barbell racks. The low south wall is now a solid ledge whose top carries the
  source's south livery, which was previously painted on the floor.
- Every north, west and east doorway in a tall wall has a lintel at 2.8 units
  (the tallest actor is 2.55) and a short passage behind it: floor art continues
  through the wall between brown jambs to a dark back panel.
- Grass Club stepping stones move with the floor art. The Rock Club sand strips
  stop at the south wall.
- Baked lighting uses the moved walls and the anchors' centre line (`w/2-1`).

Source permissions, NPC starts, cameras, events and prop bodies are unchanged.

## Verification

`comparison-320.png`: native 320×240 frames, before on the left and after on the
right. All 19 views (every distinct room with a doorway, from the same player
positions) are in `native-4mib/` and `native-8mib/` with `result.json`. Both
runs pass; the smallest free heap is 1,539,248 bytes (4 MiB) and 4,773,552
bytes (8 MiB), at most 48,088 bytes below the previous assets.

`check_world.py`, `check_room_surfaces.py`, `check_prop_models.py`,
`check_prop_clearance.py`, `check_solid_terrain.py`, `check_actor_grounding.py`,
`check_characters.py` and `node port/frontends/web/test/world3d_test.mjs` pass.

## Reproduction

```sh
python3 port/world3d/tools/build_world.py
make -C port/frontends/n64/adventure bundle
make -C port/frontends/n64/adventure BUILD_DIR=build/expansion ROM=poketcg-expansion HIGH_RES=1 bundle
# Copy the previous port/world3d/generated/{maps,textures} to
# port/frontends/n64/adventure/build/doorways-before first.
python3 port/frontends/n64/adventure/tools/review_doorways.py --build --ram-mib 4
python3 port/frontends/n64/adventure/tools/review_doorways.py --ram-mib 8
```
