Review-only face paint options. Production character models, textures, geometry and live game assets are unchanged.

- `baseline`: exact copies of the current finished face textures.
- `moderate` / Larger classic: wider/taller whites, four-texel pupils, stronger brows and mouth.
- `bold` / Simple dark: dark eye shapes with small glints; glasses keep cream lens interiors and clear frames.
- `large` / Big cartoon: oversized white eyes, five-texel pupils and small glints.

All options remain opaque 32x32 textures. Their skin background is the existing RGB16-safe pigment `(231,173,99)`. Expression, glasses, age marks and Imakuni's distinctive face remain separate treatments. Eleven source styles share nine native texture hashes; aliases which use one native hash receive identical proposed pixels.

Run `python3 port/characters/experiments/face-readability/generate.py` from the repository root. `manifest.json` maps each option's original native texture hash to its replacement PNG path, relative to this directory, and lists affected runtime model IDs. A preview ROM can therefore replace textures in an isolated DFS tree without changing any model bytes or production files.

`texture-sheet.png` shows source texels with nearest-neighbor enlargement. `sampling-guide.png` is only a bilinear texture-minification guide at 8, 12 and 16 pixels, enlarged for inspection; it is explicitly not an N64 framebuffer or VI capture. Use actual N64/ares comparisons before selecting an option.
