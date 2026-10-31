# N64 duel interface reference survey

The strongest direction is **Pokémon Stadium 2's readable library on a physical card table**: larger proportional text, clearly separated surfaces, and an unmistakable current choice. Our green felt, symmetrical players, HP bars, card art and new paper turn plaques are a good foundation. The board needs stronger hierarchy more than additional 3D geometry.

This review covers ten original Nintendo 64 titles, researched on 2026-10-31. Inventory, library, status, transfer and results screens solve the same problem as our duel: several kinds of information, selection and explanation in a small TV image.

- [Ten-game contact sheet](contact-sheet.png)
- [Gallery with full images, credits and per-title lessons](index.html)
- [Source manifest with direct image URLs, dimensions and SHA-256 hashes](sources.json)

The screenshot files retain the exact published bytes. The contact sheet fits them into 320×240 viewports without cropping or stretching. Some published sources are enlarged, compressed or cropped. Hardware, VI and prior filtering are usually undocumented. These are design references, not proof of exact CRT sharpness, native texel sizes or ares appearance. No generated mockups, remaster interfaces or replacement texture packs were selected. The Ogre Battle image is an original-era publisher press capture, potentially preceding final English retail.

## Ten references

| Game and verified period | Selected screen | What translates to our duel |
| --- | --- | --- |
| **Pokémon Stadium**, European N64 release 2000-04-07. [Nintendo](https://www.nintendo.com/en-gb/Games/Nintendo-64/Pokemon-Stadium-269679.html) | Laboratory PC party/box lists. [Serebii](https://www.serebii.net/stadium/pokemonlab.shtml) · [image](screenshots/01-pokemon-stadium.jpg) | Distinct list titles, aligned name/level columns, a filled row and a large directional symbol. A dense menu still announces its operation clearly. |
| **Pokémon Stadium 2**, European N64 release 2001-10-19. [Nintendo](https://www.nintendo.com/en-gb/Games/Nintendo-64/Pokemon-Stadium-2-269690.html) | Earl's Academy Pokémon/move library. [Serebii](https://www.serebii.net/stadium2/academy.shtml) · [image](screenshots/02-pokemon-stadium-2.jpg) | Closest match: warm pages, colored category tabs, a selectable list and a stable description area. Borrow hierarchy and physical-object language. |
| **Paper Mario / Mario Story**, Japanese release 2000-08-11. [Nintendo historical description](https://www.nintendo.co.jp/wii/vc/vc_ms/vc_ms_01.html) | Japanese status page. [RPGFan](https://www.rpgfan.com/gallery/paper-mario-screenshots/) · [image](screenshots/03-paper-mario.jpg) | Large tabs, chunky resource icons and different label/value scales. Broad, low-contrast paper checks stay quieter than fine noise. |
| **Ogre Battle 64**, Japanese release 1999-07-14. [Nintendo historical description](https://www.nintendo.co.jp/wii/vc/vc_ob64/vc_ob64_01.html) | English equipment/comparison page from the June 2000 Nintendo E3 image group. [RPGamer original-era source](https://archive.rpgamer.com/games/ob/ob64/ob64ss.html) · [image](screenshots/04-ogre-battle-64.jpg) | Overview, selection and effect comparison get separate regions. Solid blue focus, a hand cursor and semantic stat colors keep dense data understandable. |
| **Quest 64**, North American N64 release 1998-06-01. [Regional release record](https://gamefaqs.gamespot.com/n64/198386-quest-64/data) | Item selection and recovery description. [RPGFan](https://www.rpgfan.com/gallery/quest-64-screenshots/) · [image](screenshots/05-quest-64.jpg) | One selected item, an icon strip and a short effect description. Opaque paper and a small cast shadow provide depth over a busy scene. |
| **Hybrid Heaven**, Japanese N64 release 1999-08-05. [Nintendo 1999 catalog](https://www.nintendo.co.jp/n01/n64/software/1999.html) | Statistics and body-part offense. [Kenta's retrospective](https://www.kenchron.com/blog/ode2hybridheaven) · [image](screenshots/06-hybrid-heaven.png) | Large important numbers on dark backing; restrained cyan/amber/white roles. Its original high-resolution mode makes it unsuitable for measuring our 320×240 font budget. |
| **Mario Golf**, Japanese N64 release 1999-06-11. [Original Nintendo page](https://www.nintendo.co.jp/n01/n64/software/nus_p_nmfj/index.html) | Final results board. [VGMuseum](https://www.vgmuseum.com/end/n64/a/mgolf.htm) · [image](screenshots/07-mario-golf.png) | One large score, reward symbol and broad wood surface make results substantial. Outlined numerals dominate the texture. |
| **Mario Tennis**, Japanese N64 release 2000-07-21. [Nintendo historical description](https://www.nintendo.co.jp/wii/vc/vc_mt64/vc_mt64_01.html) | Transfer Pak slots and commands. [File history](https://www.mariowiki.com/File:Mario_Tennis_64_Transfer_Pak_Mode.png) · [image](screenshots/08-mario-tennis.png) | Paired slots and arrows explain relationships; unavailable commands are subdued. Published file explicitly cropped from a 320×237 N64 screenshot. |
| **Pokémon Puzzle League**, original N64 release in 2000. [Nintendo platform/year confirmation](https://www.nintendo.com/us/whatsnew/nintendo-switch-online-expansion-pack-pokemon-puzzle-league-is-now-available/) | Options page. [VGMuseum, contributed by Rey](https://www.vgmuseum.com/images/n64/01/pokemonpuzzle.html) · [image](screenshots/09-pokemon-puzzle-league.png) | One list shares space with one large illustration. A/B shapes and short verbs occupy a predictable corner. |
| **Mario Party**, Japanese N64 release 1998-12-18. [Original Nintendo page](https://www.nintendo.co.jp/n01/n64/software/nus_p_clbj/index.html) | Final ranked results. [VGMuseum](https://www.vgmuseum.com/end/n64/a/mparty.htm) · [image](screenshots/10-mario-party.png) | Aligned portraits, stars, coins and values communicate several counts compactly. Large rank numerals establish importance first. |

The western **Pokémon Stadium** corresponds to Japan's **Pokémon Stadium 2**, distinct from the smaller Japan-only 1998 game. **Pokémon Stadium 2** in English is the later Gold/Silver-era title. They are two separate released N64 games. Japanese release dates above establish original platform/period; they do not imply an English capture came from the Japanese release. Modern Nintendo Virtual Console pages are used only for their explicit historical original-release descriptions, not as sources of original captures.

## What our current duel actually does

I inspected the [current board](current/board.png) at 320×240 and the [hand](current/hand.png), [card detail](current/detail.png) and [action menu](current/actions.png) captures. The latter three are from the existing presentation review set. I also read port/frontends/n64/tcgn64.c. This is a static visual/code review, not a new motion playtest.

- Layout constants at lines 50–63 use an 8×8 fixed-width font, 10-pixel ordinary rows, and four log lines at 9-pixel spacing. The renderer at line 435 adds a one-pixel shadow. Names, HP, resources and log messages receive similar weight.
- The 64-pixel Active illustrations are stronger visual objects than their text. Bench cards have tiny names, HP, art and Energy marks inside 56×30 slots. These already tight areas cannot simply inherit a uniformly enlarged font.
- Panels at line 412 already have light/shade edges. Lists at line 1138 already have a full-row selection fill and a greater-than cursor. Improve contrast and state hierarchy; adding those features again would be redundant.
- The hand screen at line 1261 allocates 196 pixels to thirteen list rows and roughly 96 to the summary. A full name, art, type, level and attacks compete inside that preview. The full card page at line 1006 has ample unused space while important text remains small.
- The proportional turn plaque at line 1315 already moves in the right direction. Coin and hit feedback also exist. Keep that work and validate its pacing in motion.

The cross-game pattern is an inference from the reference images: **period interfaces often spend more pixels on the current choice, important values and substantial panel shapes. They do not make every line equally small just because much data exists.**

## Five prioritized improvements

### 1. Prototype proportional body text with two clear sizes

Start with hand names, card inspection and action labels. Try an original proportional bitmap face around **10–11 pixels high** for body text and **12–14 for headings**, retaining compact numerals where necessary. Use decisive strokes and small shadows. Measure strings in pixels instead of treating it as a replacement for the fixed-width cell constant. These are proposed sizes for our renderer, not measurements taken from enlarged reference images.

Stadium 2 and Paper Mario supply hierarchy; Ogre Battle shows dense data with visibly different heading and value weights. Keep the current plaque font for plaques rather than applying its large styling everywhere.

**Tradeoff:** taller text needs fewer rows, rewrapping and sometimes another rules page. Test 9–10 hand rows plus a stable preview instead of protecting thirteen rows at all costs. A 96-glyph 12×12 atlas at 4-bit coverage is approximately 6.8 KiB before metadata; layout and rendering cost are the first constraints. Full bench slots need a separate compact treatment.

### 2. Give the table three explicit visual levels

Keep felt as the lowest surface, player/Active mats as the middle surface, and the currently actionable card or menu as the top surface. Give Active cards a modest paper edge, distinct name strip and prominent HP value. Quiet the background directly behind text. Reduce the importance of empty bench outlines and resource metadata.

Stadium's paired lists, Tennis's paired slots and Paper Mario's page construction are useful. Small card-back/pile symbols with aligned counts could make deck, discard and prizes feel physical, while short labels remain available. A short cast shadow supplies depth without a new camera or 3D arena.

**Tradeoff:** framing costs pixels. Preserve opposing top/bottom orientation and TV-safe margins. Do not shrink art or bench contents to fund ornament. If a tiny pile symbol does not read at 1×, retain the label.

### 3. Strengthen focus and distinguish it from committed selection

The existing full-row highlight can become a cream/gold strip with dark text and a compact hand/arrow cursor. A card chosen for play should have a persistent frame or corner marker. Legal targets, unavailable commands and navigation focus need distinct states. Web hover must not silently replace the committed card.

Stadium and Ogre Battle are the direct references. This is a palette/state pass over existing selection behavior, not an additional redundant rectangle.

**Tradeoff:** type colors, selection, low-HP red and warning red must have different jobs. Pair shape and contrast with hue. Avoid making every selected element pulse.

### 4. Rebalance the card browser around one useful preview

Give the selected card a stable illustration, full readable name, HP/type/level and the relevant effect. Put Energy costs beside attack names. Full inspection should use labeled sections for exact rules, retreat, weakness and resistance. Its existing blank space can fund better line spacing and larger text.

Stadium 2 supplies list/description separation, Quest 64 supplies item/effect pairing, and Puzzle League demonstrates one substantial illustration beside a list. Keep the list, preview and help areas in the same places when switching between Pokémon, Energy and Trainer cards.

**Tradeoff:** a useful preview means fewer list rows or more scrolling. The cursor must retain its place after inspection. A short contextual summary must not replace exact rules or erase conditions.

### 5. Unify event emphasis with the existing plaques and effects

Keep current turn plaques, coin and hit feedback. Give duel start, prize-taking and results a related emphasis: one headline/number, one relevant card/reward symbol and a brief accent before returning to the board. Ordinary choices should remain immediate. Mario Golf and Mario Party are references for hierarchy, not literal wood-board skins.

**Tradeoff:** new delays can make a duel tedious. Screenshots do not establish good animation duration or prove our existing effects are missing. Review the current sequence in ares before deciding whether it needs a lift, slide, hold or less motion. Keep essential state visible.

## Keep and validate

**Keep:** felt, mirrored players, existing art, HP bars, paper turn plaques, opaque text panels and short instructional input labels. They already connect the duel to the rest of the port.

**Avoid:** a blanket blur/CRT filter, high-frequency noise behind glyphs, unreadable decorative symbols, modern glass panels, or higher resolution as a substitute for a deliberate 320×240 layout. More polygons will not improve predominantly 2D information design.

For the first concrete prototype, combine **1 + 3 + the hand portion of 4**. Compare the same hand, long card page, action menu and board at native resolution through actual ares video output and the web build. Check long names, maximum Energy, full benches, multiple statuses, low HP, two-controller ownership and lengthy Trainer rules. Inspect at 1× as well as enlarged; a contact sheet can conceal the readability failure we need to fix.

Third-party references remain research material credited in the gallery and manifest. Create original production fonts, textures and symbols using these principles; do not copy the referenced games' assets.
