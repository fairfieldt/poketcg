# Adventure study

References: [original approved image](../../concepts/a-adventure.png) and
[user's shirt/hair/profile correction](../../concepts/a-simple-shirt.png).
Rejected direct-3D drafts are outside this series.

[All retained native iterations](iterations.png).

| Round | Native comparison | Packed audit | Review |
|---|---|---|---|
| 1 | [capture](i1/native/comparison.png) | 584 triangles, pass | [analysis](i1/analysis.md) |
| 2 | [capture](i2/native/comparison.png) | 584 triangles, pass | [analysis](i2/analysis.md) |
| 3 | [capture](i3/native/comparison.png) | 560 triangles, pass | [analysis](i3/analysis.md) |
| 3b ear repair | [capture](i3b/native/comparison.png) | 560 triangles, pass | [analysis](i3b/analysis.md) |
| 3c visual repair | [capture](i3c/native/comparison.png) | 568 triangles, pass; head topology failed | [analysis](i3c/analysis.md) |
| 3d topology repair | [capture](i3d/native/comparison.png) | 596 triangles, pass; closed head | [analysis](i3d/analysis.md) |

The final review candidate is `i3d`. Three primary rounds and three documented
repairs were modeled, captured, analyzed and retained sequentially. The final
model implements the user's plain red shirt and sleeves, contiguous scalp and
fringe, visible ears, and deeper profile. No production assets were replaced.

Each retained version contains actual L643 exports, editable `mark.blend`,
mesh/UV `mark/authoring.json`, 32×32 PNG textures, a source snapshot, packed
geometry audit, emulator log, ROM hash, and 16 native diagnostic frames.
Mason and Ronald are unchanged production controls.

Final validation: 596 triangles, parts 0–4, zero raw/packed degenerate or duplicate
triangles. The owning head has 228 edges, each incident to exactly two faces,
and hair is one connected component. The two 32×32 RGBA16 textures are drawn
in separate material batches. `topology.json` records the closed-head check.

The ear/profile remains coarse and the 32-pixel face simplifies the generated
sheet. Diagnostic captures establish appearance and valid packed geometry;
they do not establish physical-console performance or normal gameplay behavior.
