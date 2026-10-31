# Intro orb edges

The source art was intact: all seven shared orb PNGs are unchanged, 16×16, and have the same symmetric circular alpha mask. Their outlines touch all four texture edges. The N64 used tight sprite rectangles with bilinear filtering and alpha comparison, which clipped filtering coverage at the rectangle boundary and treated partially transparent edge samples as opaque. Adding a gutter alone produced a dark fringe, so that trial was not kept.

The N64 now copies each orb into an 18×18 surface with a one-texel transparent sampling border, draws it one source pixel earlier, and blends alpha during the orb draw. The original outline RGB is bled through transparent pixels to avoid a straight-alpha filtering halo. All seven original outlines use the same solid RGB (8,0,41); the source checker now verifies a uniform rim and symmetric mask. Original sprites are freed immediately after copying, and padded surfaces are freed after RSP completion when the intro closes.

The nominal center is unchanged: `(x−1)+18/2 = x+16/2`, and likewise for y, before both display scale factors. Every original opaque pixel is copied directly, with no resampling in asset creation. Only invisible RGB and the additional transparent border differ. The source motion tables, durations, coordinates, fades and composition scissor are unchanged. Shared PNGs, metadata and web art remain unchanged; hashes are recorded in [validation.json](validation.json).

The padded pixel payload totals 4,536 bytes, replacing 3,584 bytes of source pixels plus their sprite headers. The replacement does not retain the loaded original orb sprites.

## Captures

These are actual completed native N64 RDP framebuffers **before VI filtering**, captured from the production intro/video functions in private Ares. Both modes run the native animation assertions. The opening uses source tick64; the title uses tick90 with the incoming orbs in their settled positions. An in-bounds seven-orb swatch isolates texture-edge behavior from composition clipping. Before and after use the same poses and source artwork.

- [320×240 comparison at native pixel size](native-comparison-320.png)
- [480×360 comparison at native pixel size](native-comparison-480.png)
- [320×240 orb pixels enlarged 4× nearest](orb-zoom-320.png)
- [480×360 orb pixels enlarged 4× nearest](orb-zoom-480.png)

Unscaled individual captures and hashes are in `before/` and `after/`. Twelve captures passed in total. The raw logs and isolated harness remain under `port/frontends/n64/adventure/build/intro-orb-check`. No user emulator settings or installed SDK code were changed.

`python3 port/frontends/n64/adventure/tools/build_intro.py --check` passes all 12 canonical image hashes/dimensions/transparency checks, the seven symmetric orb/rim checks, and all 26 original animation tables. `adventure_intro_check()` passed at both native resolutions. No 3D, duel, audio or source story behavior changed in this fix.
