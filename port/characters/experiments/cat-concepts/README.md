# Cat model study

[Open the review gallery](index.html), [final native capture sheet](rounds/i3/native/sheet.png), or [editable Blender models](rounds/i3/cats.blend).

The two new NPCs follow the prior cast study: one real image-generated concept sheet using both reference photos, followed by three sequential model/native-capture/review rounds. Each round retains its source snapshot, mesh/UV authoring JSON, editable Blender file with packed textures, L643 exports, opaque 32×32 textures, geometry audit, native captures, and written review. The pre-study models are preserved in `baseline/`.

The final models are `rounds/i3/cat_tuxedo` and `rounds/i3/cat_tabby`. Each has 728 triangles, five rigid animation parts, a closed consistently wound skull, and two textures. The production files are copied to `port/characters/assets/models/`. Head and ear meshes retain explicit ownership for the gameplay LOD generator. No authored ground-shadow mesh is included.

The concept sheet is generated artwork. `blender-preview.png` files are Blender renders. Only files in each round's `native/` directory are actual 320×240 ares framebuffer captures. Each set contains a portrait and four-angle turnaround per cat, both real conversations, and the encounter room. These retained study frames precede the user's later request for half-height runtime presentation; authored geometry remains unchanged so the runtime scale is applied once. The N64 review uses the production renderer with 4 MiB. It does not measure physical-console performance.

Round 1 replaced protruding eye geometry and fragile whiskers with painted faces and a shaped skull. Round 2 shortened the ears, deepened the muzzle, reduced the tuxedo's upper white muzzle, and simplified tabby markings. Round 3 enlarged the irises and differentiated the cheek shapes. Per-round `analysis.md` files ground these changes in the actual preceding captures. A separate `native-revalidation/` capture preserves round 1's canonical staged bytes without replacing its original review frames.

[Verification](verification.json) independently checks float32 and packed geometry, texture opacity and size, head topology, capture hashes, and staged model equality after the production importer's content-addressed texture renaming. Generated concept provenance and its exact prompt are in `concepts/`.

```sh
python3 port/characters/experiments/cat-concepts/tools/publish_review.py
# Author a NEW retained round (existing rounds are intentionally immutable):
python3 port/characters/tools/build_cats.py --round 4 --publish
python3 port/world3d/tools/sync_characters.py --strict
/Applications/Blender.app/Contents/MacOS/Blender --factory-startup --background --python-exit-code 1 --python port/characters/experiments/cat-concepts/tools/editable_blend.py -- port/characters/experiments/cat-concepts/rounds/i4
python3 port/characters/experiments/cat-concepts/tools/capture_round.py 4
```
