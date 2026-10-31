# Island hidden-surface export

The shared island mesh now submits **5,259 triangles instead of 5,511**:
252 fewer, or 4.57%. Its complete original vertex records, UVs, colors and
ordering remain intact. Only hidden triangle index triples are removed.
Unreferenced source vertices are not emitted by the packed renderer's cache
compiler. The editable Blender scene, every landmark, coastline, conifer,
route and logical destination remain unchanged.

Coverage uses exact convex polygon subtraction and affine reciprocal depth,
with a one-pixel guard at 480×360 and at least 0.04 world units of depth
clearance. It keeps triangles crossing or outside the reference viewport so
the same mesh remains complete in wider browser windows. The camera position
and direction remain the authored fixed island view.

Geometry-only coverage initially altered a few native RDP depth/AA pixels.
The final bake additionally preserves 127 source triangles that can contribute
to native coverage. Its source mesh, camera and resulting output hashes must
all match the validated bake. Changing any of those inputs retains the full
unoptimized export until it is revalidated; it never applies stale visibility
to newly authored geometry.

The final guarded mesh has **zero changed native or post-VI pixels** in all
four comparisons: legacy GL and packed Tiny3D, each at 320×240 / 4 MiB and
480×360 / 8 MiB. Each pair uses one frozen executable and filesystem contents
differing only in the island mesh. The original ares encoder PNGs, native
framebuffer dumps and capture provenance remain in the linked review folders
listed in [island-visibility.json](island-visibility.json). Together the checks
compare 499,200 native and 921,600 post-VI RGB pixels exactly.

```sh
python3 port/world3d/tools/check_island_visibility.py
python3 port/world3d/tools/check_world.py
```

The pure geometry regression covers union coverage across shared diagonals,
microscopic visible gaps, near/coplanar depth exclusions, wider viewport
retention, protected native contributors and stale source/camera/output
fallback. All existing world, prop-shell and collision-clearance checks pass.
Triangle reduction is a workload count; it is not a measured physical-console
frame-rate claim. A strict subpixel coast simplification experiment found
negligible safe savings, so the authored coast detail is retained.
