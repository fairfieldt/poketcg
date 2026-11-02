# Approved adventure image, iteration 1

Reference: `concepts/a-adventure.png`, approved by the user. Earlier rejected
direct-3D drafts are excluded from this series.

Native DIAGNOSTIC=18 baseline+A capture passed with 16 frames. Mark uses 584
triangles, five rigid parts, and 32×32 face/cap/costume textures. Independent
raw and actual packed-coordinate audit passes with no degenerate or duplicate
triangles. No triangles were filtered to achieve this. `mark.blend` and
`mark/authoring.json` retain editable geometry and UVs; `source.py` snapshots
the exporter used for this round.

Observed in native portrait, turnaround, conversation and room frames:
large vertical eyes now remain readable; three joined fringe locks are visible,
and the fitted cap has no crossed scalp/crown strip. The backward bill is short.
Compared to the approved sheet, the front jaw looks too square, the torso is
narrow, and the hair/ear profile transition is too straight. The white collar
is strong at this scale. The body looks a little small relative to the reference.
Independent review is pending before the second modeling round.
