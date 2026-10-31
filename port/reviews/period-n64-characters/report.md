# N64 character reference survey

The best next step is to improve **silhouette, pose and face exposure together**. Our characters already have large heads. Uniformly enlarging heads or adding polygons would miss the main problem: the room camera often sees the top of a cap or hair, while the face occupies only a few native pixels. Period games use strong whole-body shapes at navigation distance and reserve readable facial acting for a closer, better-aimed camera.

[Open the illustrated gallery](index.html), [contact sheet](contact-sheet.png), or [complete source records](sources.json). The packet contains **10 original N64 titles and 21 selected screenshots**, with original published image bytes, attribution, dimensions and SHA256 hashes. No production assets changed.

## How to read the evidence

These are original N64 **game presentations**, captured and published at different times. Several sources do not identify their emulator, VI processing, internal resolution or rescaling. A 640-pixel or 920-pixel published image does not establish how many native pixels an eye occupied. The gallery identifies those limits per image. The Pokémon Snap Todd shot is explicitly the original game running in Nintendo’s later Switch N64 emulator.

Harvest Moon 64’s map characters are sprites, with separate illustrated dialogue portraits. Oak’s Pokémon Snap menu image is flat artwork. They are useful presentation contrasts, not evidence of polygon-head construction. The other selected character scenes show polygon characters. Small UI portraits are not substituted for scene models. No ROMs or AI images were used.

I inspected still images, not gameplay video. Observations about silhouette, face visibility and reference poses are visual. Walking recommendations below are **code-inferred prototype hypotheses**, not claims that static screenshots demonstrate a particular gait or timing.

## Ten useful references

| Original N64 title | Selected views | Useful character lesson |
| --- | --- | --- |
| Ocarina of Time — JP 1998 | Market crowd; shopkeeper close view | Body width, skirt length, sleeves and beard distinguish adults. A close camera makes modest face features usable. |
| Majora’s Mask — JP 2000 | Link in traversal; shop conversation | Shared visual language can contain different ages and builds. Dialogue brings a face toward the camera without adding mesh detail. |
| Mystical Ninja Starring Goemon — JP 1997 | Town gameplay; Yae close view | Huge coherent hair outline at distance; eyes and brows dominate a clean exposed face in conversation. |
| Goemon’s Great Adventure — JP 1998 / English 1999 | Shop gameplay; Goemon and Ebisumaru dialogue | Short limbs, large hair/head masses, simple mittens and broad shoes. Facial proportions vary between characters. |
| Quest 64 — NA 1998 | Room with NPC; larger Brian action pose | Cape, collar and boots identify a small figure from behind. A single visible eye can work when the head and pose are clear. |
| Doubutsu no Mori / Animal Forest — JP 2001 | Player outside house; player with Static | Compact body, readable cap/head shape, short thick limbs and a broad face window suit an elevated social camera. Original N64 release, not GameCube Animal Crossing. |
| Mega Man 64 — NA 2001 | Field rear view; larger side gameplay view | Boots, shoulders and forearms establish weight and identity when the face is absent. N64-specific gallery, not PlayStation Legends screenshots. |
| Harvest Moon 64 — JP 1999 | Church group; Rick dialogue | Sprite silhouette and clothing blocks do navigation work; an illustrated portrait provides expression separately. |
| Pokémon Snap — JP 1999 | Polygon Todd intro; flat Oak menu portrait | Todd’s shirt stripes, hair mass and arm gesture read as a whole. Oak’s large portrait demonstrates a separate expression layer. |
| Hybrid Heaven — JP 1999 | Corridor gameplay; close faces; full-body cutscene | A realistic adult body and detailed face texture are period-correct too, but small gameplay figures rely on jacket/trouser masses. Close framing supplies the facial detail. |

Release dates and regions are linked in [sources.json](sources.json). Screenshot language is not proof of release region. The selected galleries are [Zelda Temple: Ocarina](https://www.zelda-temple.net/media-and-downloads/screenshots/the-legend-of-zelda-ocarina-of-time), [Zelda Temple: Majora](https://www.zelda-temple.net/media-and-downloads/screenshots/the-legend-of-zelda-majoras-mask), [Nintendo64EVER: Mystical Ninja](https://www.nintendo64ever.com/Nintendo-64-Game-Screenshots,215,Mystical-Ninja-Starring-Goemon,1.html), [Nintendo Life: Goemon’s Great Adventure](https://www.nintendolife.com/games/n64/goemons_great_adventure#screenshots), [RPGFan: Quest](https://www.rpgfan.com/gallery/quest-64-screenshots/), [Nookipedia: Animal Forest](https://nookipedia.com/wiki/File:DnM_Outside_Player%27s_House.png), [Nintendo64EVER: Mega Man](https://www.nintendo64ever.com/Nintendo-64-Game-Screenshots,196,Mega-Man-64,1.html), [RPGFan: Harvest Moon](https://www.rpgfan.com/gallery/harvest-moon-64-screenshots/), [GameGrin: Todd](https://www.gamegrin.com/articles/playing-pokemon-snap-25-years-later/), [Pokémon France: Oak](https://www.pokemon-france.com/guides/snap/galerie/), and [Nintendo Life: Hybrid Heaven](https://www.nintendolife.com/games/n64/hybrid_heaven/screenshots).

## What this means for our cast

The current [post-VI actors and rooms](index.html#current-port) already have useful signatures: Mark’s red vest, blue trousers and backpack; Mason’s coat and gray hair; the black hair masses and broad outfit palettes. The simple hands and shoes fit this scale. Keep those features and the original GBC family identities.

The independent cast audit read `parts.py`, `designs.py`, `build_model.py`, `scene.c` and `presentation.c`, and inspected the character montage plus emulator captures. Its head measurements put the basic anatomical head at about 29% of figure height; hair or cap can bring the head region to roughly a third. Our issue is not a general shortage of head volume. Approximate earlier projection estimates put a near lab face around 8×4 native pixels and a far one around 5×3, before filtering; these are estimates rather than measured post-VI feature bounds. The high room view and cap brim reduce useful face exposure further.

### 1. Expose the face in conversation; keep the cap recognizable

Prototype a lower conversation viewpoint and slightly more frontal upper-body framing, keeping both participants visible. Compare that with a modestly shorter or raised brim and an authored head/neck angle on Mark. The goal is a larger **visible face plane**, not just more forehead or a larger head. Keep navigation framing independently useful.

The Majora conversation subject and the Ocarina shopkeeper show why this works: the shot gives their face room while preserving posture and body identity. Goemon’s close view is much more expressive than his shop-distance face. This is an inference from the selected compositions, not a measured claim about those engines’ camera logic.

**Tradeoff:** conversation camera motion can disrupt orientation; settle into a stable two-character shot and return predictably. Edit the cap’s owning closed shell. Preserve the recent headwear surface fixes; do not add floating face overlays or hidden scalp layers.

### 2. Differentiate shared silhouettes and internal proportions

Start with Ken and Mitch. The current audit found both using the same seven-spike recipe despite distinct source descriptions. Give Ken a low, wide hair mass with fewer lateral points; give Mitch a narrower, taller cluster. Reallocate existing points rather than adding fine strands.

Prototype three modest body/rest-pose archetypes using existing parts: shorter legs for a child, clearer shoulder/forearm shape for an athlete, lower shoulders and a compact stance for an elder. Existing archetype scaling changes the whole assembled figure while largely preserving internal ratios. Altering those ratios and rest angles can differentiate families more effectively than uniform enlargement.

Goemon, Animal Forest and Mega Man provide different degrees of exaggeration; Hybrid Heaven is the useful adult-proportion counterexample. Copy the readability principle, not their costumes. Keep mitten hands and broad shoes; try a clearer toe and substantial sole. Add depth before widening occupied space. The audit found athletic hand reach already slightly beyond the authored 0.65-unit collision radius, so inspect maximum poses against furniture and doorways.

**Tradeoff:** exaggeration can change age or personality. Apply it to canonical families, with aliases inheriting the result, and compare against their original GBC sprites.

### 3. Consolidate clothing, materials and light into larger readable groups

Retain the source palette and make each figure read as a few regions: hair/headwear, skin, main garment, lower body and shoes. Favor a broad collar, cuff, coat/skirt hem or backpack profile over tiny buttons, pocket stripes and thin straps. Use two or three broad values to explain planes, rather than many small equal-contrast accents. Keep exposed skin and the face background coordinated.

Brian’s cape/collar, Mega Man’s boots/armor and the Ocarina crowd’s dress/sleeve masses survive views where expressions cannot. Our exporter already bakes broad normal-based shading and runtime actors are unlit; extra lighting or material batches are not required to test this grouping.

**Tradeoff:** too much contrast fragments a small figure; too little merges limbs into the torso. Judge the rear and three-quarter room view after VI, not only a Blender close-up. Keep one or two strong costume cues per family rather than adding new details to 115 aliases.

### 4. Use different facial hierarchies for different families

The reference range is wider than “N64 characters have huge eyes.” Goemon and Yae strongly prioritize eyes/brows; the shopkeeper uses beard, brow and nose; the Majora conversation subject uses hair, a clear face window and modest eye marks. Hybrid Heaven’s realistic texture detail depends on close framing.

Use the existing A–D face experiments as texture options, alongside the camera/brim test. A larger-eye treatment is worth trying for young trainers, while Mason and other adults should retain their brows, glasses and age cues. Simplify mouths and noses if they compete with the eye pair at room size. Keep the center of the face clear; do not add fine eyelid, tooth, nostril or highlight noise everywhere.

**Tradeoff:** uniformly oversized whites can make every adult look startled or childlike. Portrait attractiveness is not enough: retain a face only if it improves the native room view and keeps the intended age/expression in the naming or conversation view.

### 5. Give the existing rigid rig a clearer stance and a few pose presets

The current code uses the same sinusoidal 24-degree rotation magnitude for moving limbs, rigid leg/shoe groups, and an always-positive absolute-sine body bob. That is code evidence; no recording was evaluated for timing here.

Prototype smaller arm swing than leg swing, a restrained shoe angle around ground contact, and two or three rest-angle/shoulder presets. Keep phase tied to distance and preserve the recent continuous tile-walking fix. A scientist, child and club athlete can have different posture without separate knees, ankles or fingers. The reference stills support studying limb separation, shoe placement and whole-body attitude; they cannot validate a new walk curve.

**Tradeoff:** excessive planted-foot correction can look like sliding without knees. Record a side and three-quarter walk, stop, turn and doorway crossing before choosing values. Do not change movement speed merely to conceal a pose problem.

## A small, reviewable next experiment

Use **Mark, Mason, Ken, Mitch and one skirted family**. Compare unchanged and prototype versions at identical near/far room anchors, front/side/rear sheets, conversation framing and the naming portrait. Include a short identical walk/idle recording for each pose preset. Preserve the canonical alias mapping, six-batch limit, current 906-triangle ceiling and 4 MiB target while testing. Validate the largest pose against collision space.

The first two experiments should be **conversation face exposure** and **distinct hair/body silhouettes**. They address the current bottlenecks, reuse shared assets and have stronger reference support than spending the budget on more surface detail.
