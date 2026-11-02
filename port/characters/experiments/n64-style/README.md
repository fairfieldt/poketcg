# N64 character style directions (experiment)

Four complete style directions for the three story leads (Mark, Dr. Mason,
Ronald), built as isolated review options. Production models, textures and the
world exports are unchanged; the game only sees these through the
`DIAGNOSTIC=18` preview ROM.

| Option | Reference | What changes |
|---|---|---|
| `a_round` | Goemon's Great Adventure, Pokémon Snap's Todd | Near-spherical smooth heads at ~41% of height, egg torso, stubby limbs, mitten hands, big rounded shoes, large painted eyes. |
| `b_pokemon` | Nintendo's late-90s Pokémon 3D (Snap, Stadium era) | Medium head (~36%), egg head with a nose bump, sculpted hair lobes, anime-style eyes from the GBC duelist portraits, costume detail painted in the torso texture. |
| `c_zelda` | Ocarina of Time / Majora's Mask townsfolk | Taller, slimmer figures (~4.5 heads), visible neck, nose cone, smaller eyes, stronger directional light; Mason is receding and stooped. |
| `d_angular` | Mega Man 64, Mystical Ninja (1997) | Flat-shaded, deliberately faceted: octagonal head, chamfered-box torso, hexagonal limbs, bevelled shoes and hands, sharp hair wedges. |

Every figure is a handful of continuous shells (head, hair or cap, torso, two
arms, two legs, plus Mark's backpack) with no floating buttons, straps, lapels
or buckles: those details are painted into one 32×32 torso texture per
character. Faces are one 32×32 texture across the front three head facets.
Lighting is baked into vertex colours with smooth vertex normals (flat for D),
which is what gives A–C their rounded look on the unlit runtime. All exports keep
the five rigid parts and hip/shoulder pivots of the existing walk animation,
total height ≈2.2 units, and RGBA16-safe palettes so Blender previews match the
console.

## Layout

- `tools/geom.py` — mesh primitives (lathe, loft, box, spike, extrude), smooth
  or flat baked lighting, L643 writer, Blender-import JSON.
- `tools/paint.py`, `tools/colors.py` — Pillow texture painting, RGBA16 quantisation.
- `tools/builder.py` — the four `Style` records, the three `Design` records,
  the shared figure assembly and per-character hair/cap recipes.
- `tools/build_options.py` — writes `options/<style>/<slug>/scene.l64`,
  `scene.json`, `textures/*.png` and `authoring.json`.
- `tools/blender_preview.py` — imports each option into Blender, saves
  `options/<style>/<style>.blend` (editable meshes with the baked vertex colours
  and textures) and renders `review/*-front.png`, `review/*-threequarter.png`.
- `tools/capture_options.py` — runs the DIAGNOSTIC=18 ROM in ares and saves
  native 320×240 RDP frames; `tools/sheet.py` builds the comparison sheets;
  `tools/publish_review.py` assembles `port/reviews/n64-style-characters/`.
- `review/native/` — the current native sheets.

## Rebuild and capture

```sh
cd port/characters/experiments/n64-style
python3 tools/build_options.py                      # all options, or pass style ids
/Applications/Blender.app/Contents/MacOS/Blender --background --python-exit-code 1 --python tools/blender_preview.py
cd ../../../frontends/n64/adventure
make BUILD_DIR=build/n64style ROM=poketcg-n64style DIAGNOSTIC=18 bundle
python3 ../../../characters/experiments/n64-style/tools/capture_options.py poketcg-n64style.z64 --output build/n64style-capture
python3 ../../../characters/experiments/n64-style/tools/sheet.py build/n64style-capture ../../../characters/experiments/n64-style/review/native
```

The preview ROM (`tests/model_options_preview.c`) stages the production
exports as the `baseline` option with their model IDs rewritten to 1, and each
option directory containing `option.json` plus the three characters. It
captures, per option: the laboratory room camera (front row facing the camera,
back row facing away), the conversation camera, the exact naming-screen
portrait viewport for each lead, and a close turnaround (front, side walking,
back, side walking) for each lead.

Note that `blender_preview.py` runs inside Blender's Python, which has no
Pillow; it rebuilds geometry only and reads the PNG textures already written by
`build_options.py`.
