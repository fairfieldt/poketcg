# Ronald study

Reference: [generated concept](../../concepts/ronald.png), derived from the
approved Mark style and Ronald's original portrait. Only one concept was used.

[Three native iterations](iterations.png).

| Round | Native comparison | Revision | Review |
|---|---|---|---|
| 1 | [capture](i1/native/comparison.png) | New competitive face, six joined hair points, solid blue shirt | [analysis](i1/analysis.md) |
| 2 | [capture](i2/native/comparison.png) | Lower/back-swept peaks, clearer eyes and smirk | [analysis](i2/analysis.md) |
| 3 | [capture](i3/native/comparison.png) | Five staggered broad points and lower interior crown | [analysis](i3/analysis.md) |

The final review candidate is `i3`. Each round was built, captured in the actual
native renderer, reviewed, then revised. All three rounds retain editable
`ronald.blend`, `ronald/authoring.json`, L643 export, texture, source snapshot,
geometry/topology audits and 16 diagnostic frames. Mark uses approved A3d as
an unchanged control; Mason uses the unchanged production export.

All rounds pass independent raw/packed validation at 512 triangles, five rigid
parts and one opaque 32×32 RGBA16 texture. Owning head has 180 edges with
incidence two and hair is a single connected component. No geometry was filtered
to hide defects. The final profile has depth and swept points, with visible
skin ears and joined blue shoulders. The face is Ronald-specific.

Independent review accepted `i3`. Rear hair points merge in the native front
projection; ear/profile geometry and the 32-pixel face simplify the concept.
These diagnostic captures are appearance/geometry evidence, not a physical
console performance claim. Production assets have not been replaced.
