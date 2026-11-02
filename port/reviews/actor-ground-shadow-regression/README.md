# Preview shadow regression

The earlier fix recognized one exact eight-sided shadow primitive. The new
reviewed cast introduced a twelve-sided fan with a different color, so that
recognizer missed it. Its height still rounded to the floor in native packed
coordinates, and the opaque fan appeared and disappeared with actor animation.

Runtime import now uses `authoring.json` mesh ownership: a mesh named or tagged
`shadow` is excluded regardless of its geometry or color. Mismatched authoring
and exports fail instead of silently publishing. Exact legacy recognition is
retained only to migrate existing runtime and distant-model files.

The regression fixture uses a seventeen-sided shadow with different dimensions
and color. It verifies removal, preservation of an unowned solid prop, and
rejection of stale authoring. All 99 runtime files are checked, including distant
models and fallback actors. Normal N64 and web builds enforce the check;
character tests also run it.

The [asset audit](../../characters/review/shadow-regression.json) proves that 672
triangle references were removed: twelve each from 28 full and 28 distant
models. All retained indices, vertices, UVs, colors, pivots, textures, and
canonical source exports remain exact. The renderer's existing soft contact
shadow is unchanged.

## Native proof

The diagnostic runs the actual opening walk to Dr. Mason, followed by the
conversation camera. All 116 actor/camera updates and the final story/RNG state
match between the previous committed assets and the corrected assets.

Both memory/resolution configurations pass:

| Mode | Contact-shadow pixels during run / idle | Native comparison |
| --- | --- | --- |
| 4 MiB, 320×240 | 119 / 281 | [Before / after](comparison-320.png) |
| 8 MiB, 480×360 | 262 / 644 | [Before / after](comparison-480.png) |

The contact counts compare corrected frames with and without the separate soft
shadow pass. They establish that removing the preview geometry did not remove
the game's contact shadow. Images are completed native RDP readbacks, with no
retouching. Individual frames and native results are in `320/` and `480/`.

Reproduce with `tools/review_actor_grounding.py --build --selected-captures`,
then `--ram-mib 4` and `--ram-mib 8` from the N64 adventure tooling. Its ignored
`build/actor-grounding-before` fixture contains the pre-fix runtime catalog
from commit `88c2c149`, plus hashes of the current canonical authoring exports.
