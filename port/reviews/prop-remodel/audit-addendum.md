# Implementation status after the source audit

This updates the initial audit's replacement list. “Implemented” means the
shared authoring recipe has changed; final native captures and runtime checks
are recorded separately by the parent task. The initial report is the before
assessment, not a claim that the old models remain in the new build.

## Implemented in this pass

| Family | Concrete replacement | Shared coverage |
|---|---|---|
| Lobby service counter | Continuous L-shaped worktop, navy edge and panelled apron; modeled register/card-tray objects | All nine lobbies use one mesh |
| Duel playing surfaces | Owning wood rim and apron, inset 64×32 TMEM-sized felt material with broad card zones, inset legs | Nine lobbies, Mason tutorial table, Dome long table |
| Table seating | Red inward-facing lobby chairs; low magenta round stools around Mason's table | Shared lobby recipe and four lab stools |
| Shelves and cabinets | Open recessed shelf fronts, thick uprights/cornice, grouped books/jars and lower cupboard doors | Lab, all lobbies, Ishihara's house, Dome entrance |
| Computer/workbench family | CRT housing, readable inset display and key deck, modeled bent instrument arm | Mason, Science and shared small terminals |
| Deck printers | Cartridge crowns, CRTs, sloped controls and dark bodies with independent replacement screen states | Ten printers; eight separate medal states preserved |
| Science instruments | Four colour-bar CRTs, instrument workbench and distinct southern blue-screen test units | Science Club; the central low counters remain generic |
| Challenge Hall leaderboard | Framed green leaderboard with broad score rows and modeled stepped plinth | Challenge Hall |
| Water furniture | Red/cream parasol tables; white slatted navy/orange chaise lounges with raised east-end backs; southeast arrangement has two lounges | Three parasols and four lounges; Amy seat remains at .71 |
| Rock statues | Broad gold creature form with feet, torso, paired arms, head, ears/horn and muzzle | Six instances of one recipe |
| Psychic columns | Navy/cream fluted pedestal and a square framed purple head; cones removed | Eight instances of one recipe |

All additions use existing palette-based vertex colours except the dedicated
felt texture. The decoration-only group adds no texture or material batch.
The original tile permissions, NPC interaction anchors and map events remain
the source of gameplay placement. Authoring changes are in `furniture_props.py`,
`lab_props.py` and `decorative_props.py`; the new persistent prop surface checker
covers raw and native 1/32-unit coordinates.

Additional source corrections: Mason's four magenta circles are stools, not
balloons. The central green field is a duel table. Water's supposed horses are
parasol/table arrangements; its supposed park benches are chaise lounges.
Psychic's supposed flames are square-topped columns. Original pixels take
precedence over the old inferred room briefs.
Mason's southeast "counter" was only the source south-wall band; that phantom
prop was removed. The actual small green north-wall readout was restored.

## Remaining lower-priority work

* **Science central low counters:** all three `counter` entries (source crops
  `(7,12,5,2)`, `(12,12,5,2)` and `(16,4,2,6)`) still use the generic brown
  box with a whole projected source front crop. They are the broad low work
  surfaces visible in the native Science cases, distinct from the remodeled
  CRT banks, test units and instrument bench. Replace them later with a clean
  cyan worktop and owning green/white facade/kerb regions; avoid implying that
  all Science furniture was rebuilt in this pass.
* **Ishihara's small corner desk:** source `table` crop `(2,1,3,4)` still goes
  through the generic table recipe, including the projected source crop on
  its top. The shelves were rebuilt; this desk/lamp/chair arrangement was not.
  A writing surface, dark inset, brass articulated lamp and tucked chair would
  restore the original corner composition.
* **Fire standards:** retain the correct green woven/coiled stem and broad red
  hood/flame mass when replacing the current brown poles/red cones. This is a
  visible identity mismatch, but separate from the lab/lobby work completed now.
* **Fighting loose kit:** separate the barbell, loop weight, dumbbell and red
  speed bag silhouettes. The current single barbell/balloon reuse is coarse;
  the existing dumbbell racks already read adequately at room distance.
* **Grass planting and shared palms:** flower baskets need cream/pink clusters
  and green leaves; palms would benefit from bent two-tone fronds; topiaries
  could have a clearer foliage crown. Keep leaf/triangle counts restrained and
  preserve flush traversable stepping stones.
* **Dome small dragons/flowered hedges:** the little statues could gain a
  readable bent neck/head and the hedges their broad flower clusters. Their
  screen area is small; do this after nearby furniture passes native review.
* **Challenge Hall side flights:** actual shallow stair treads and rails remain
  preferable to a textured solid step slab. Keep the two source approaches and
  existing blockers; the new leaderboard does not imply these are remodeled.

Retain the newly approved Hall of Honor machine/shrine, Lightning towers/stage,
Water pool and island landmarks in this pass. They already have coherent large
forms. No evidence from this audit calls for replacing them again.

## Review images

`source-shape-comparison.png` is a clean two-column, three-row comparison of
original Water parasol, Water chaise and Rock statue source pixels against
isolated Blender author previews. It uses existing images only. It is not a
native screenshot comparison. The separate Psychic preview remains
`model-psychic_club-torch.png`.
