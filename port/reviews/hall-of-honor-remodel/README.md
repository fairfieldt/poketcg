# Hall of Honor model pass

The central object is the **Legendary Card shrine**. The separate object against the north wall is the **Legendary Auto Deck Machine**.

The original [object table](../../../src/data/map_objects.asm) sends north-facing interactions at `(10,2)` and `(12,2)` to `Script_fbe1`; [its script](../../../src/scripts/hall_of_honor.asm) opens `DECK_MACHINE_LEGENDARY`. [Text05b5–05b7](../../../src/text/text6.asm) names the machine. The shrine's `(10,10)` and `(12,10)` interactions instead call `Script_fbf1`, award the four Legendary Cards and lead to the credits. Text05b8 explicitly describes four cards floating in the air. The six NPC entries comprise four graphical card/aura quadrants and two sparks. The [actual alternating card frame](reference/original-card-frame-8x.png) and [aura frame](reference/original-aura-frame-8x.png), decoded from `AnimFrameTable18`, show one composite glowing card icon. The original narrative calls these the four Legendary Cards; the visual does not display four independent card sprites. Logical actor IDs, positions, visibility and events are preserved. Four Hall-only derivative meshes now assemble those quadrants into one card and a restrained pink ring; the two source spark models are unchanged. Canonical character exports remain untouched.

The old model stretched the projected blue/white altar artwork across brown boxes, including full pillar front faces. It had no actual front stairs, and a large plain floating sphere dominated the cards. The north-wall machine was only painted into the floor. The map brief's description of that machine as a banner has been corrected from the source.

The replacement uses real stair treads, blue risers, octagonal ivory columns, blue/gold plinth bands and a shallow blue Poké Ball relief on a framed back panel. The separate deck machine has a cream frame, blue display, card silhouette, inclined console, card slot and red activation key. Both use broad geometry and flat face shading. The shrine's complete old projected footprint is erased from the floor, including the side feet that previously remained as blue fragments.

| Hall architecture | Before | After |
|---|---:|---:|
| Triangles | 536 | 920 |
| Vertices | 868 | 1,594 |
| Material batches | 27 | 21 |
| Referenced textures | 26 | 20 |
| L643 bytes | 25,644 | 45,012 |

The architecture needs no new texture. The Hall-only card derivatives share one 16×16 source-derived card-front texture (`t85656ca994b61b8d.png`, 512 bytes in RGBA16); their total 64 triangles replace 96 original quarter triangles. The original corner tile 4 and OAM palette 5 supply the cream border, purple face and pale central mark. The four derivatives use local offsets so unchanged 3D source anchors assemble into one image plane. Shared `map_actor_scenes` metadata selects them only on map 33 in native and web; other maps retain the canonical actor paths.

Every existing solid's bounds remain identical. The new machine occupies source-blocked cells and passes the same 0.65-unit actor-clearance check. Original permissions, doorways, warps, scripts and card/spark positions are unchanged. Other exported meshes and all existing textures remain byte-identical; see [asset-isolation.json](asset-isolation.json).

[Original GBC map](reference/original-gbc.png) · [Old actual CRT trailer frame](reference/before-crt-trailer.png) · [Old Blender preview](reference/before-blender.png) · [Final native shrine, 480](native-8mib/hall-shrine-after.png) · [Final native machine pose, 480](native-8mib/hall-machine-after.png) · [Final native shrine, 320](native-4mib/hall-shrine-after.png)

The Blender views are inspection renders, not emulator captures. The editable [single-room Blender file](../../world3d/generated/hall_of_honor.blend) contains the actual exported geometry and source actor positions and Hall-only card derivatives. The native captures are completed raw N64 RDP framebuffers before VI and CRT processing; the parent's paired trailer capture provides the final shader output. Both 320/4MiB and 480/8MiB runs passed four before/after frames each, including the north-machine interaction pose. The panel was lowered to 3.3 units after a first visual check, leaving Mark's head visible from behind the shrine. Every frame asserts unchanged source `OwState`; native lookup assertions also check Hall-only selection and the canonical path outside Hall. See [4MiB result](native-4mib/result.json) and [8MiB result](native-8mib/result.json).

Validation:

```sh
python3 port/world3d/tools/build_world.py --hall-only
python3 port/world3d/tools/check_world.py
python3 port/world3d/tools/check_room_surfaces.py
python3 port/reviews/hall-of-honor-remodel/check.py
python3 port/world3d/tools/check_characters.py
node port/frontends/web/test/hall_card_models.mjs
node port/frontends/web/test/world3d_test.mjs
/Applications/Blender.app/Contents/MacOS/Blender --background --python-exit-code 1 \
  --python port/world3d/tools/blender_world.py -- --hall-only --render
```

All checks pass: 34 map links, valid indexed meshes, no duplicate triangles, texture TMEM limits, all 3,528 walkable anchors and 5,817 legal edges, and 141 human NPC starts. The Hall-specific check also verifies original metadata and solid bounds, and finds zero competing planar surfaces across 332 axis-aligned prop triangles. [validation.json](validation.json) records hashes and geometry; [room-surfaces.json](room-surfaces.json) records the 37 existing floor/prop surface checks.

The Hall-only generator updates only this room and its manifest record. A normal complete generation uses the same Hall recipe in `tools/honor_props.py`. Canonical character assets and other room materials are unchanged. The native/frontend loading change is a small map-specific actor-path lookup; the existing map-change cleanup and per-NPC visibility remain authoritative. The path/cache regression covers Hall→Dome→Hall and hidden quadrants; canonical character geometry and no-op synchronization also pass.

Web asset copies and map-specific routing were checked separately: [live asset verification](web-assets.json). The 140-assertion host routing test checks every map, Hall→Dome→Hall reload, hidden card quadrants and unchanged sparks. It is not a browser visual capture.
