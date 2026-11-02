# Mark model study

[Open the visual review](index.html). Four imagegen concepts were developed
through three sequential modeling/capture/review rounds each. Further ear and
head-topology repairs are retained separately. The user's later corrections
replace the vest/black sleeves with a plain red shirt, connect the hair to the
scalp, and add depth to the side profile.

## Final review models

| Concept | Final editable model | Triangles | History |
|---|---|---:|---|
| Adventure | [Blender](approved/a_adventure/i3d/mark.blend) | 596 | [rounds](approved/a_adventure/README.md) |
| Compact | [Blender](approved/b_compact/i3c/mark.blend) | 596 | [models and captures](approved/b_compact/) |
| Lean | [Blender](approved/c_lean/i3c/mark.blend) | 608 | [models and captures](approved/c_lean/) |
| Angular | [Blender](approved/d_angular/i3c/mark.blend) | 548 | [models and captures](approved/d_angular/) |

[Final portraits](review/final-portraits.png) and
[front/profile/back walking views](review/final-turnarounds.png) are crops of
actual native RDP frames enlarged with nearest-neighbor sampling. They are not
imagegen or Blender renders. Individual full 320×240 frames accompany every
round, including the laboratory, conversation, naming portrait and turnaround.

The adventure direction is the base for the subsequent cast study: balanced
proportions, clear facial features and a deeper skull. Compact emphasizes the
head and short legs; lean retains a narrower, longer silhouette; angular uses
fewer, harder planes. All four remain deliberately coarse at native resolution;
the generated sheets have more apparent texture detail than the actual 32×32
faces can reproduce. These are review candidates, not production replacements.

## Generated references

The built-in `image_gen` tool produced all four concepts in [concepts](concepts/).
Each image has its exact successful prompt alongside it. The original source
portrait and earlier unsuccessful model comparison were the references.
[Generation record](concepts/generation.json). The user approved the adventure
sheet, then requested the [simpler shirt and connected hair](concepts/a-simple-shirt.png).
The other concepts retain distinct proportions while applying those same
construction/costume corrections.

Early text-only generation attempts failed; reference-based generation
subsequently worked. The rejected direct-3D drafts in `options/` and
`review/round1/` are not counted as any of the twelve requested concept rounds.

## What changed through review

- Round 1 translated each generated design into a real mesh and painted face.
  The captures exposed small heads, weak shoulder joins and disconnected fringe.
- Subsequent rounds corrected head/body balance and face placement, replaced
  the vest and black sleeves with one red shirt, widened the overlapping sleeve
  roots, and made hair the lower boundary of the owning head/scalp surface.
- Profile revisions increased forehead/cheek/occipital depth and used a nose
  formed in the face surface. Ear-material repairs removed black side blocks.
- The final ear boundary uses a shared full-plane mesh split, with interpolated
  UVs. Earlier partial splits passed triangle checks but introduced T-junctions;
  those versions remain explicitly rejected as final geometry.

Per-round `analysis.md` files record observed captures and subsequent changes.
Every round contains its source snapshot, editable mesh/UV JSON, packed L643
export, texture PNGs and Blender file with packed textures.

## Verification

[Independent verification](review/verification.json) passes for **12 primary
rounds**, **21 retained captured versions**, and **336 decoded native frames**
(including baseline/control frames). It verifies exact equality between exports
and staged native capture inputs, unchanged Mason/Ronald controls, image sizes,
model hashes and ROM hashes. Final heads are closed, with every edge shared by
two faces and all hair faces in one connected component.

All retained image-led exports pass the independent byte-level float32/packed
coordinate audit: no degenerate or duplicate triangles, no coordinate overflow,
five rigid animation parts and opaque 32×32 textures. Each final model uses two
RGBA16 textures, 2,048 bytes each, drawn by material batches. Earlier rejected
direct-3D prototypes are excluded from this claim.

These are isolated `DIAGNOSTIC=18` ares captures using the real N64 renderer,
production room geometry and cameras. They establish appearance and valid
packed assets, not physical-console performance or ordinary gameplay behavior.
Production assets and the prior uncommitted experiment are unchanged.

```sh
python3 port/characters/experiments/mark-concepts/tools/verify_study.py
python3 port/characters/experiments/mark-concepts/tools/publish_approved.py
# Re-capture a retained option (Blender is not required):
python3 port/characters/experiments/mark-concepts/tools/capture_approved.py a_adventure 3d
```

[The next character study](../cast-concepts/) applies this process to the cast.
