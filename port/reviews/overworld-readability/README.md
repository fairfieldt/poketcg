# Overworld readability

`comparison.png` shows actual ares v148 SD video output before and after the
changes. Both images were saved with Tools → Capture Screenshot. They include
VI processing, before window shaders/scaling; these are not raw RDP framebuffer
captures. The original 640 × 240 files are retained. The comparison duplicates
rows to show their 4:3 display aspect; it adds no sharpening or smoothing.

The final ROM uses resample-only VI output and larger landmark footprints with
broad identifiers. The source camera, coastline, entrance anchors and route
coordinates are unchanged. The island uses fewer triangles; one 32 × 32 RGBA16
roof-badge texture makes its bolt survive the native RDP depth precision.

`filter-comparison.png` holds the old geometry constant and compares full VI
AA/divot/dedither, AA without dedither, resampling, and the point-filter control.
These four ROMs have identical DFS asset payloads. Cursor animation phase varies
between captures. Resample-only preserves small features without the point
control's harsher stair steps.

The separate native diagnostic validates the full game's 4 MiB memory budget,
map loading, story, duel, saves and presentation. The stationary capture ROM
uses the production renderer and assets; it is not a full-game memory test.
