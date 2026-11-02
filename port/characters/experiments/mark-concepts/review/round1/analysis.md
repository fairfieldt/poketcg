# Round 1 review

Viewed `comparison.png` and native Mark turnaround/portrait frames. The ROM
completed all 40 frames. These are failed prototypes, not recommended assets.

- Compact: broad shoulders and large blank cheeks make the face feel older;
  the pointed chin overshoots the intended taper. Eyes sit too high.
- Portrait: strongest body proportions; eyes and fringe touch and read dark.
- Slender: distinct longer legs, but the face is tiny at the room camera.
- Angular: horizontal brows/eyes resemble glasses. The lower face is empty.
- All four: a red/black slit crosses the rear crown. Separate scalp and cap
  surfaces overlap. Back cranium bulges while the face profile remains flat.

Independent packed-coordinate audit failed with 5, 17, 33 and 21 degenerate
triangles respectively. Raw float geometry passed, demonstrating why raw-only
validation is insufficient. Thin limb/cap/sole rings must be reauthored.

Round 2 decisions: give head/cap one shared surface; reduce rear depth, use a
less pinched jaw, lighten face shading, move eye row down three texels, shorten
fringe, raise and darken the mouth. Rebuild arms/legs and shoes as coarse closed
shells, retain full-size hands, thicken rear bill. Preserve these failed i1
exports unchanged as evidence.
