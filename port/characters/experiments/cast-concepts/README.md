# Character model study

[Open the full-cast gallery](index.html) to compare generated concepts, native
turnarounds, and the three retained review rounds for each character family.
[View the cast sheet](review/full-cast-portraits.png) or
[open the editable Blender cast](cast.blend).

The study covers all 37 human model families in the character catalog. Mark's
approved simple-shirt model supplies the construction approach: a closed head
with real profile depth, connected scalp hair, joined sleeves, and simple
clothing. Each character retains its own source identity and palette.
The torch and legendary card are props and are unchanged.

Every family after Mark has one generated concept and three sequential model,
native-capture, and review rounds. Extra repair rounds remain alongside the
originals. [Mark's earlier study](../mark-concepts/index.html) retains four
concepts, three primary rounds per concept, and the subsequent shirt, hair,
profile, and topology corrections. The chosen Mark model is A3d.

Each retained model includes an editable Blender file, mesh and UV authoring
JSON, native L643 export, opaque 32×32 textures, a source snapshot, geometry
audit, and written observations from its actual captures. Final models use
five rigid animation parts and stay within the 900-triangle study budget.
The generated concept sheets contain detail that simplifies at native texture
and display resolution; small badges, trim, eyeglass corners, and rear hair
points can merge at room distance.

## Native comparisons

The screenshots are actual 320×240 ares captures using the production N64
renderer, models, lighting, room, and cameras. Each new-family round retains
four before views and four candidate views: portrait, turnaround, conversation,
and room. The diagnostic stages each subject in the Mark slot; it does not
replace production NPC bindings. The lead study also retains its original
16-frame capture sets and a [three-lead ensemble](ensemble/native/).
These captures do not establish physical-console performance.

The gallery links the individual final Blender and native files, along with
all captured review rounds. [Final family selections](review/final-families.json)
and [appearance selections](review/final-appearances.json) record the exact
retained versions. The combined Blender file includes all 37 families, the
three alternate appearances, and Amy's lying pose in separate collections.

## Shared appearances and poses

Mania and Boy's alternate appearances retain their parent geometry and rig.
Amanda retains Sara's body and rig with her own head and original yellow
swimsuit palette. Amy's lying pose is derived from the standing mesh using the
existing native coordinate transform, preserving UVs, colors, and texture
bytes. Original NPC aliases and Murray's heading override remain in the
[review binding manifest](review/final-bindings.json).

## Verification and sources

- [Full-cast capture and geometry verification](review/full-cast-verification.json)
- [Dr. Mason and Ronald verification](review/verification.json)
- [Mark verification](../mark-concepts/review/verification.json)
- [General palette-variant proof](review/general-variant-verification.json)
- [Water-cast and lying-pose proof](review/water-civilians-variants.json)
- [Concept images, exact prompts, and generation records](concepts/)

Checks cover float32 and packed geometry, texture sizes, export/staged-capture
byte equality, editable sources, closed consistently wound final heads, and
connected hair regions. The study verification was recorded before production integration: its 721
original catalog, binding, model, and runtime files matched the saved baseline.
The selected models now populate production assets. See the
[production integration proof](../../review/cast-integration.json). The original
study verifiers intentionally retain their historical baseline assertions.

```sh
python3 port/characters/experiments/cast-concepts/tools/verify_families.py --complete
python3 port/characters/experiments/cast-concepts/tools/verify_study.py
python3 port/characters/experiments/mark-concepts/tools/verify_study.py
python3 port/characters/experiments/cast-concepts/tools/publish_full_cast.py --complete
python3 port/characters/experiments/cast-concepts/tools/verify_bindings.py
/Applications/Blender.app/Contents/MacOS/Blender --background --python-exit-code 1 --python port/characters/experiments/cast-concepts/tools/build_cast_blend.py
```
