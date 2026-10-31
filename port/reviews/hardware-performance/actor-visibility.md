# Conservative actor draw culling

The N64 world renderer constructs one 4:3 camera frustum per frame using its
actual FOV, near plane 1.5 and far plane 90. It skips an actor and its contact
shadow only when their complete conservative bounding sphere lies outside a
frustum plane. Intersections remain drawn. It preserves source visibility,
positions, headings, movement and save state, and leaves scene/model loading
unchanged to avoid camera-entry loading hitches.

Standing bounds are centered 1.3 units above the presented root with radius
2.2, including model scale, floor elevation and bob. Legacy reclining models
use radius 3.6. An asset proof checks all 55 current models and their optional detail exports / 89,155 vertices
with unrestricted limb rotation and six-degree run lean: the greatest required
standing radius is 1.8455, and legacy tilt needs at most 3.0061. Ground shadow
footprints fit inside the same sphere. Invalid/degenerate camera data draws
conservatively.

The independent projection test preserves 4,023,324 in-frame sphere points
across 36 camera directions. It checks all six clipping-plane boundaries,
camera/pose immutability and actual Mason, Sam, Ronald and wall-adjacent
conversation pairs throughout camera entry and after settling.

```sh
sh port/frontends/n64/adventure/tools/test_visibility.sh
```

The N64 bundle compiled successfully with the new render path. The numerical
and asset checks establish conservative bounds; native screenshot/performance
comparison remains part of the combined optimization validation. No physical
hardware speed claim follows from these tests alone.
