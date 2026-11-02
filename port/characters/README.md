# Pokémon TCG character models

All 115 NPC entries use 39 canonical models: 37 humans and two prop families.
The production human cast uses the [reviewed concepts and native comparisons](experiments/cast-concepts/index.html), including the rounded-head correction to Mark A3d. The torch and legendary card retain their original assets.

The current cast has rounded connected skulls and front-facing eye paint. Mason
and Sam use compact adult eyes. [Native correction checks](experiments/cast-concepts/review/roundness-verification.json) cover every human appearance and pose.
Gameplay-distance LODs preserve each complete head and simplify the body.

Open [assets/cast.blend](assets/cast.blend) to inspect the linked canonical cast.
Individual editable sources are in `assets/models/<family>/<family>.blend`.
Keep the directory structure intact when moving the cast file.

![Current Blender front/back previews](review/character_models.png)

[bindings.json](bindings.json) preserves every original NPC alias, palette,
pose, component and heading. Identical NPCs share one canonical asset. Mania
and Boy retain alternate palettes. Sara and Amanda share their body and rig,
with distinct editable heads and blue/yellow swimsuits. Amy's lying pose derives
from her standing mesh. Murray's west-facing entry retains heading 270.

Each human model has five rigid animation parts and stays below 900 triangles.
The native L643 exports use the original canonical runtime IDs; the world import
assigns its separate runtime IDs and generates independent lazy-loaded scenes.
The web and N64 builds consume those same world exports. Native packing bakes
mesh pieces into each appearance; aliases still reuse their complete asset.

## Edit and verify

The reviewed Blender files use ordinary materials and packed textures. Mesh
objects retain `rigid_part` and `animation_pivot` properties. Coordinates are
X east, Y south, Z up; native coordinates are X east, Y up, Z south.

Select an appearance or pose in Blender's Python console:

```python
import sys
sys.path.insert(0, 'REPO/port/characters/tools')
import apply_variant
apply_variant.apply('swimmer_palette2')  # Amanda head and yellow swimsuit
apply_variant.apply('swimmer')           # Sara head and blue swimsuit
# In Amy's source:
apply_variant.apply(pose='lying')
apply_variant.apply(pose='standing')
```

`authoring.json` beside each native export retains its editable mesh/UV/material
recipe. The reviewed study source scripts and iteration evidence remain in
`experiments/`. `tools/promote_cast.py` reproduces promotion from the selected
review manifests; it replaces canonical sources, so run it only when deliberately
restoring those reviewed selections. Then run `tools/prepare_cast_blends.py` in
Blender to attach production identity and appearance controls.

The older procedural `build_model.py` recipes are historical. `make models`
validates reviewed manual sources without replacing them. Preserve manual edits
before deliberately promoting reviewed exports again.

```sh
make test
make test-blender
make cast
"/Applications/Blender.app/Contents/MacOS/Blender" --background --python tools/render_cast_sources.py
make sheet
python3 ../world3d/tools/sync_characters.py
python3 ../world3d/tools/build_character_lods.py
```

[Integration proof](review/cast-integration.json) checks every promoted native
export against the selected study bytes with only its model ID remapped, all
115 original bindings, unchanged prop files and all 49 runtime exports.
[Editable-source checks](review/cast-blender-verification.json) cover the 37
canonical models, five-part rigs, shared bodies, head selection, palettes and
Amy's pose. Historical study captures use the actual N64 renderer in ares; they
do not establish physical-console performance. Native and browser build results
are recorded in [the integration build report](review/cast-builds.json).
