# Room prop audit — 2026-10-31

The strongest remaining problem is the use of a **whole projected Game Boy prop image as one face of a brown box**. Raising texture resolution alone cannot restore the missing CRT, sloped control deck, shelf depth, table apron or parasol silhouette. Author those large forms, then spend texture detail on their owning surfaces. Keep the original permission grid, source anchors and footprints.

## Evidence and limits

Inspected all 34 original `port/maps/*.png` room images beside the 34 Blender previews in `port/world3d/generated/previews/`; inspected `build_world.py`, `scene_specs.py`, the map briefs, source scripts and tileset provenance. Also inspected the native presentation contact sheet, the recent native lab image and the Hall remodel's native machine image. The Blender previews and presentation contact sheet precede some camera/material improvements: they establish prop silhouettes, not the final display pipeline or current timing. The current builder remains the authority for unchanged geometry. No gameplay or performance measurement is claimed by this audit.

Inspection sheets: `source-current-00.png`, `source-current-09.png`, `source-current-18.png`, `source-current-27.png`; enlarged source details: `source-prop-crops.png`. These are inspection artifacts, not new game art.

## Correct the object interpretation first

* **Mason's central green object is a duel table.** Its field, marked card slots and brown apron match the lobby/Dome playing surfaces. `src/scripts/mason_laboratory.asm` names the Sam state `.sam_at_table`; `src/text/text6.asm` tells the player to take a place at the table. The lab brief's “collection booth” description is wrong. Do not turn it into a computer.
* **Water's three `horse` props are parasol/table arrangements.** Original pixels show red/cream radial canopy strips, an orange vertical support, a cream round tabletop outlined in navy, and legs. The “carousel horse” brief produced the wrong model entirely. Preserve the cream, navy and orange with the red canopy.
* **Psychic's eight `torch` props are square-topped ornamental columns.** A dark purple square inset in a lavender/cream frame sits on a fluted navy/cream pedestal. No cone/flame appears in the background or NPC-composited map. Preserve this silhouette even if their precise fictional purpose is ambiguous.
* **The blue lobby terminal's exact purpose is uncertain from art alone.** It is a small blue/white terminal with a screen, controls and grille. Service/Gift Center clerks stand behind the counter. The prose label “Card Pop machine that dispenses daily cards” is not sufficient evidence to invent mechanism or labels. Model the actual cabinet/CRT/controls; retain interaction logic.

## Replacement order

| Priority | Family and maps | Current issue | Replacement direction |
|---|---|---|---|
| 1 | Lobby L counter, 9 rooms | Two plain boxes; tiny projected card objects smeared on front; little readable trim | One continuous L worktop with disjoint corner, navy edge, warm panelled apron and a few real card trays/white registers. Keep two clerks visible. |
| 1 | Green duel table: lobby ×9, Mason, Dome | Whole source table including its side/apron is stretched onto top; bright noisy card marks | Real wood rim/apron, inset green mat containing only the play field, broad card-zone markings. Retain different square/long proportions; improve leg placement and orient chairs toward play. |
| 1 | Mason workstation and Science workbench | Bare table with projected instrument pixels; CRT reduced to face on cube | Distinct CRT housing, inset display, sloping key deck, desk and readable bent instrument/lamp silhouette. |
| 1 | Bookshelves/cabinets: lab ×7, lobby ×3 shared, house ×12, Dome entry ×6 | Cabinet image on a full box; no recess; top/body mismatch | Shallow open shelf recesses, thick top and uprights, grouped book/bottle silhouettes, simple lower doors. One shared family with palette/content variants. |
| 1 | Deck Room ten printers and independent medal states | White-lidded counters instead of tall printer silhouettes; CRT/rack flattened | Gold cartridge crown above discrete blue CRT and sloped keypad, dark pedestal. Keep the eight independent switch fronts and base/all-lit variants coherent. |
| 1 | Science computer banks/test range | Two whole banks smeared across box front; four individual monitors unreadable; white lids dominate | Four separate CRTs with broad colour-bar screens, clean cyan bench, four modest test cabinets with blue displays/red caps and coarse piping. |
| 2 | Still-generic Science low counters, Ishihara corner desk | Whole projected crops remain on a box/table despite the neighboring equipment remodel | Author their proper worktop/front surfaces; add the house desk lamp/chair. See the implementation addendum for exact source entries. |
| 2 | Challenge Hall leaderboard and side steps | Tall generic machine with cream top; stepped route drawn across a solid slab | Green framed leaderboard on a warm plinth; readable row bars, stepped foot, genuine shallow steps with rails. Keep source off-axis placement. |
| 2 | Water parasols and benches | Wrong animal silhouette; benches are white combs with no arms and uniform slab seat | Red/cream low-poly parasol over small round cream table, navy/orange supports; chaise lounges with navy frame, white slats, raised east-end backs and orange legs. Preserve Amy's supported height. |
| 2 | Rock creature statues ×6 | Gold ball on cylinder reads as pawn | Broad creature torso, head/snout and ears/horn, paired arms and feet on gold pedestal. No need to infer an exact species. |
| 2 | Psychic square pedestal lights ×8 | Purple cones on thin black poles erase original architectural forms | Cream/navy plinth, fluted shaft, small capital and framed square purple inset. Shared untextured geometry is enough. |
| 3 | Fire standards ×6 | Brown stick and red cone; loses green coiled stem and red hood/flame mass | Broader red crown above cream collar and green patterned/twisting pedestal. Avoid adding many layered translucent flames. |
| 3 | Fighting loose weights/speed bag | Every loose weight gets same barbell; speed bag reused magenta balloon | Three recognizable kit silhouettes: barbell, short dumbbell, loop weight; red speed bag on dark mount. Rack itself reads adequately. |
| 3 | Grass flower baskets/topiary, Dome hedges/serpents | Flower baskets are plain pink balls; topiary green cylinders lack leaf crown; Dome dragons barely read | Few cream/pink petal clusters with green leaves, broad two-tone crown, coarse dragon neck/head. Preserve Grass stepping stones as flush walkable floor details. |
| 3 | Shared palms | Four/six flat triangle wedges read as spikes near the foreground | Curved or bent frond silhouette with two/two-tone segments per leaf, same modest crown footprint and clear trunk. Existing palm is acceptable at distance. |
| Retain | Hall shrine and Legendary machine | Recent remodel reads clearly and is source grounded | Keep. Use this as the standard for silhouette, owning surfaces and broad contrasting bands. |
| Retain | Lightning stage/towers, Water pool, overworld landmarks | Established silhouettes already support room identity | No replacement needed in this prop pass. Further changes need actual native readability evidence. |

## Coverage — all 34 map IDs

| IDs | Maps inspected | Shared prop coverage |
|---|---|---|
| 0 | overworld | Twelve landmark groups, terrain, trees, coast; retain this pass. |
| 1–3 | mason_laboratory; deck_machine_room; ishiharas_house | Lab/desk/duel-table/cabinet; printer; house desk/bookcase/plant. |
| 4,7,10,13,16,19,22,25,28 | fighting_club_entrance; rock_club_entrance; water_club_entrance; lightning_club_entrance; grass_club_entrance; psychic_club_entrance; science_club_entrance; fire_club_entrance; challenge_hall_entrance | Same foyer mesh and palm, 9 distinct floor emblems; no reason to build nine furniture sets. |
| 5,8,11,14,17,20,23,26,29 | fighting_club_lobby; rock_club_lobby; water_club_lobby; lightning_club_lobby; grass_club_lobby; psychic_club_lobby; science_club_lobby; fire_club_lobby; challenge_hall_lobby | Same complete lobby mesh: counter ×2, terminal, table, chair ×4, bookcase ×3, palm ×2. Original backgrounds are identical. |
| 6 | fighting_club | Loose weights ×6, rack ×2, bag currently `balloon`. |
| 9 | rock_club | Statue ×6, large/medium rocks ×8. |
| 12 | water_club | Parasols currently `horse` ×3, lounge arrangements ×3 (four lounges total), palm ×3, shrub ×2, actual pool/coping. |
| 15 | lightning_club | Imported stage, four pillars and room shell; retain. |
| 18 | grass_club | Beds ×6, topiary ×18, flower basket ×8, flush stones; preserve collision authority. |
| 21 | psychic_club | Square pedestal ornaments currently `torch` ×8. |
| 24 | science_club | Workbench, computer bank ×2, central tester, southern tester ×4, cabinet, lab counters. |
| 27 | fire_club | Fire standards currently `brazier` ×6. |
| 30 | challenge_hall | Leaderboard, two magenta stands, six palms, two side step flights. |
| 31 | pokemon_dome_entrance | Shelves ×6, small terminal, door panels, palm; test both closed/open door variants. |
| 32 | pokemon_dome | Long duel platform, two small dragon figures, flowered hedge rows; open Hall door variant. |
| 33 | hall_of_honor | New shrine, columns, crest, Legendary machine, torches/cards; retain remodeling. |

Four room-event images were also compared: lab Challenge Machine installation, all-lit printer room, open Dome entrance, and open Hall door. Printer state surfaces must remain independently replaceable: a complete new printer face must not sit over an old full backing face.

## Native acceptance

Review each new family at 320×240 and 480×360 from the entrance and a nearby interaction position, then through VI/CRT output. A room-wide enlarged Blender preview is insufficient. Prefer geometry for a large silhouette change and 32×32 / 64×32 reusable textures for actual surface detail. More small polygons or more texels on the same incorrect box will not help.

Check exact permissions/door variants, NPC starting poses and script approaches with `check_world.py` / `check_prop_clearance.py`. Amy must retain her bench support; clerks, Sam and Mason must remain visible. Preserve nonoverlapping owning caps and inset screen faces so the old jagged depth artifact does not return. Recheck room triangle/batch counts, then profile changed worst-case rooms rather than assuming extra geometry is free.
