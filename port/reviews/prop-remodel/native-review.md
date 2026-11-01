# Independent native prop review

The integrated pass improves room readability and source identity. No
release-blocking art defect or new actor/prop clipping is visible in the
72 completed frames across 320×240 and 480×360. Shelves merit a later material-contrast pass;
that does not outweigh the overall silhouette and surface improvements.

## Evidence

Reviewed all 18 before/after pairs at native cell resolution in
`native-4mib/contact-1.png` through `contact-3.png`, plus individual native
frames for the Water, Rock, Psychic, Science, deck-printer and bookshelf
cases. Reviewed the matching 18 pairs in `native-8mib/contact-1.png` through
`contact-3.png`, plus individual 480×360 frames for the library/lobby shelves,
lab Challenge Machine, Water parasol, Rock statue and Science north bank. This
includes the earlier four lab/counter captures. Both serial runs use source
commit `437152d3`; their `result.json` files record the 4/8 MiB memory settings,
320×240/480×360 dimensions, 36 completed images each and unchanged canonical
source state.
The captures use the production renderer with settled identical cameras and
actor poses; room meshes/textures differ. These are fenced RDP framebuffers,
not Blender previews. This review does not claim physical-hardware timing,
VI/CRT output, animation or all story-state combinations.

## All 18 pairs

| Pair | Native reading and source identity |
|---|---|
| lab-table | Broad felt zones, clean wooden rim and low magenta stools form a coherent card table. The former projected apron pixels and tall balloon sticks are gone. Player/Sam remain readable. |
| lab-workstation | CRT and instrument desk have separate large forms; pale monitor, blue display and dark instrument survive at native size. North-wall readout is present. |
| lab-challenge | Installed machine is recognizable as a console with a cartridge/rack crown and magenta display. Mason/player and the neighboring technician stay visible. |
| lobby-counter | Continuous warm L worktop, dark binding and actual service objects; clerk heads/upper bodies remain visible with an open approach. |
| lobby-table | Felt is quieter and clearer than the oversaturated blocky source crop. Wooden frame, apron and red seating still establish the play area. |
| lobby-shelves | Real cabinet depth/cornice and grouped contents are visible. Pale content blocks lose some book-spine rhythm; see the nonblocking note below. |
| deck-off | Printer silhouette now has a recognizable crown, CRT and inclined control deck instead of a flat source image on a white-lidded box. The base active pink terminal remains distinct from blue screens. |
| deck-mixed | A mixed screen selection is visibly distinct without exposing duplicate surfaces. Static crown/body stays coherent across front replacements. |
| deck-on | All-enabled pink/cyan fronts remain visually coherent with the same printer shells; nearby player does not disappear into a solid lid. |
| ishihara-shelves | Library arrangement and warm palette persist; shelf objects have depth. Thick warm frames and large cream book groupings can read as cupboards from this camera, warranting a later contrast pass. |
| science-north | Four separate colour-bar CRTs are immediately countable, with cyan work surfaces and real equipment pipes. Much stronger room identity than the flattened bank images. |
| science-machines | Purple test units have blue display hoods and paired red caps; four units remain a repeated family. Mark is legitimately behind the near machine row at source position (18,18), with head/torso visible rather than clipping into a front surface. |
| challenge-scoreboard | Green leaderboard and broad score rows read from the approach; the large cream lid no longer hides the board. Warm plinth and magenta side accents remain. |
| water-parasol | Red/cream parasol silhouettes clearly establish poolside furniture, replacing the misidentified white block animals. Amy and Joshua remain readable near the upper lounge. |
| water-bench | White chaise slats, navy frames, orange legs and raised backs survive at 320×240. Southeast twin lounges match the source arrangement. Amy remains supported on her upper lounge. |
| rock-statue | Paired gold creatures have readable heads, horns/ears, arms and planted feet. They no longer read as pawns while preserving the source gold-statue corridor. |
| psychic-pedestal | Square purple heads and fluted cream/navy pedestals restore the actual source architecture. No pointed-flame silhouette remains; aisle and people remain readable. |
| dome-table | Long bounded green mat and marked zones retain the ceremonial central duel platform while removing unrelated projected side pixels from its top. Adjacent statues and player remain clear. |

## Nonblocking follow-up

The bookshelf replacement has real shelf depth, but the bright thick wood
frames and broad cream content blocks dominate at this resolution. A small
contrast pass could strengthen the dark horizontal openings and distinguish
narrow light spines from one or two dark/mid-tone groups. Ishihara should read
as a library before the viewer walks close. This can be done with existing
geometry/material colours; it does not need denser textures or more tiny
polygons. Keep it on the lower-priority polish list rather than changing the
frozen assets during this capture run.

No new request is made for the approved Hall shrine/machine, Lightning stage,
Water pool or island. Fire standards, gym kit distinctions, plant/palm shapes,
Dome small dragons/hedges and Challenge side steps remain as listed in
`audit-addendum.md`. That list also explicitly retains Science's three generic
low counters and Ishihara's small projected-crop corner desk;
the remodeled equipment/shelves do not mean every table/counter
recipe was replaced.

## Expansion Pak counterpart review — 480×360

All 18 counterparts pass the same art/readability check. The extra resolution
clarifies CRT hoods, broad screen bars, cartridge crowns, chaise slats and
statue heads without exposing new competing planes or detached parts. The
actor poses and room framing match the 320 cases; the heads/upper bodies that
should be visible remain visible. No additional actor/prop clipping issue was
found. A parasol near the extreme right frame edge is naturally cropped by
the tracking camera in the close Water composition; its canopy and table
remain a coherent object, not a geometry/floor-clipping artifact.

The shelf caveat is consistent at 480: thicker bright framing and large pale
content groups still dominate the small dark shelf openings. Higher output
resolution alone does not change that material hierarchy. Keep the same
nonblocking contrast/spine-grouping follow-up; no new model change requested.

The selected actual VI subsets at both resolutions are reviewed below. These
are core VI output with shader None, not CRT Guest Advanced recordings.

## Actual VI output — standard 320×240 mode

Reviewed the three before/after pairs for lab table, lobby counter and Water
parasol in `vi-4mib/post-vi/`, displayed at 4:3 in
`comparison-320-vi.png`. These are six actual ares VI scanout images; the raw
640×240 buffer uses nonsquare pixels and is not a 640-pixel-detail game render.
The capture provenance identifies ares v148, enabled VI processing, no
supersampling and shader `None`: this checks real VI output, not CRT Guest
Advanced appearance.

The calmer felt/card markings, solid service counter and parasol/chaise
silhouettes remain readable after VI. Amy's supported pose is coherent. No
new visible broken face, source-crop fragment or ground-clipping defect is
exposed by this output stage in these selected stills. There is no release
blocker from the standard VI subset. No motion/CRT-shader or hardware
performance claim is made.

## Actual VI output — Expansion Pak 480×360 mode

Reviewed the matching three before/after pairs in `vi-8mib/post-vi/`, displayed
in `comparison-480-vi.png`. Capture provenance records the 480×360 native
runtime mode, 8 MiB RAM, six original ares VI callback PNGs and enabled VI
processing with shader `None`. The VI output is a 640×480 display buffer;
this is not a claim of rendering the game at 640×480.

All three pairs pass. The improved green felt, wooden counter/service objects,
CRT shape and Water parasol/chaise grouping remain coherent through the real
VI stage. No new missing surface, broken depth boundary or ground-clipping
artifact is visible in this selected still subset. The extra resolution
clarifies the broad card-zone lines and angled lounge back. Shelf contents
retain the same nonblocking contrast/spine-proportion refinement already
recorded above. No model revisions requested.

The review now covers 72 completed native framebuffer images (18 pairs per
resolution) and 12 actual VI images (three selected pairs per mode). This is
sufficient still-image acceptance for this pass; it does not replace motion,
CRT Guest Advanced or physical-hardware validation.
