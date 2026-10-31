# Distinctive faces for the story leads

Four original face-paint recipes replace the previous shared or restrained faces without changing geometry. These are production canonical-family updates; all aliases inherit them. The rest of the cast keeps its previous artwork.

- **Mark:** open eyes, substantial dark pupils and small glints; calm brows and a restrained raised smile give a young, confident expression. His new `mark` face style is separate from the ordinary `smile` family.
- **Ronald:** lower asymmetric lids, a sideways gaze, one raised brow and a one-sided smirk make the rival look knowing rather than uniformly angry.
- **Dr. Mason:** broad gray brows, square spectacles with visible kind eyes and a warm smile preserve the professor’s age and identity.
- **Imakuni?:** unequal raised brows, round staring eyes and an exaggerated open toothy grin suit the bear-eared hood and eccentric costume.

[Source paint comparison](face-paint-comparison.png) · [Blender model comparison](blender-comparison.png) · [Validation proof](validation.json). These review images are texture and Blender inspections, not emulator screenshots. Gameplay-size and resolution comparisons belong to the native capture pass. Mark’s cap still affects how much face the elevated camera exposes.

`tools/lead_faces.py` contains the deterministic 32×32 paint. `build_model.py` uses it for these families only; Mason and Ronald override copied canonical facial images while leaving the original Lightning-demo sources unchanged. Opaque backgrounds pass through the same skin/RGBA16 preparation as the body. Rebuild with `python3 port/characters/tools/build_all.py --only SLUG --force`, then sync the world exports.

All four final native mesh records compare exactly equal to their pre-edit records, including positions, colors, UVs, indices, pivots and batch membership. Only texture names/pixels differ. All other family files and runtime character records remain byte-identical, and aliases are unchanged. The complete cast exterior audit retains the maximum 906 triangles and six batches; the four-family skin check reports a maximum adjacent skin difference of 1/255.
