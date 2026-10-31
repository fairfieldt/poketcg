# Card back texture detail

The Expansion Pak back now expands the same 24×34 and 32×44 color masters used in standard mode. Previously it independently sampled the original artwork at 36×51 and 48×66, creating finer detail than the Pokémon portraits. Portraits preserve their 64×48 source pixels and expand them by 1.5 at 480×360; the backs now follow that texture scale too.

The original artwork, colors, placement, card dimensions and rounded silhouette remain. Standard-mode art is unchanged. Browser art uses the same 32×44 master, retaining the existing 256×357 file dimensions and smooth outer mask. Native arrays still occupy 14,456 bytes. No renderer code changed for this adjustment.

## Native previews

- [480×360 portrait comparison](native-texels-480-comparison.png)
- [480×360 real Prize screen](native-prizes-480-comparison.png)
- [320×240 portrait comparison](native-texels-320-comparison.png)
- [320×240 real Prize screen](native-prizes-320-comparison.png)

These are actual N64 framebuffer captures from isolated ares executions, using the production CPU canvas, card sprites, card-back drawing and RDP completion path. The renderer and video implementation were frozen across both variants; only the included generated card-back array differed. The first screen is a deliberately constructed comparison fixture, and the second uses `draw_prize_choices` over the existing stress-state board. They are static UI fixtures rather than a recorded playthrough or VI output. The development FPS badge belongs to the frozen renderer and is unrelated to the asset comparison.

Both 320×240 before/after captures are pixel-identical. At 480×360, changes are confined to the backs; all Pokémon portraits are pixel-identical. Native validation records and original-size images are in [native/](native/), with comparison counts in [comparison.json](native/comparison.json). Private reproducible harness files remain in `port/frontends/n64/adventure/build/card-back-review/`.

## Browser previews

- [Native-high scale comparison](web-native-high-comparison.png)
- [Desktop scale comparison](web-desktop-comparison.png)

The browser fixture uses the actual source `.art`, `.prize-card` CSS and compiled sprite sheet. It verifies six backs, unchanged image dimensions, expected card size and no overflow or browser errors at 1.5× and 3× logical pixel scales. The browser retains its existing image sampling behavior. No full web build was run by this task, because the parent agent owns the combined release build.

## Checks

`python3 port/frontends/n64/tools/check_card_back.py` checks the pinned source artwork hash, unchanged direct-filtered standard masters, correct enlarged color masters, original target-size alpha masks, exact PNG/C-array correspondence, native storage and manifest hashes. The result is [asset-validation.json](asset-validation.json).

`node port/frontends/web/test/card_back.mjs` reproduces the browser fixture and writes its captures under `port/frontends/web/build/card-back-review/`. Its result is [web-validation.json](web-validation.json).
