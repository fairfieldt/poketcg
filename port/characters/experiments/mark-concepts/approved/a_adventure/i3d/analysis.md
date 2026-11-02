# Final topology repair 3d

The visually accepted 3c version failed a later head-closure audit: its localized
ear split left T-junctions against neighboring head polygons. This repair
splits every crossed owning-head polygon with a shared edge-intersection cache.
Face UVs interpolate along original edges. The global ear plane moves from
1.835 to 1.81 units so tiny slivers near the 1.84 fringe tip survive packed
1/32-unit coordinates. No triangles are filtered or hidden to pass validation.

Validation passes: 596 triangles, five rigid parts, two opaque 32×32 textures,
zero raw or packed degenerate/duplicate triangles. All 228 owning-head edges
have incidence two. Hair is one connected component. Editable Blender and
native authoring data are retained. `topology.json` records the head audit.

Actual native capture completed 16 frames. Viewed portrait, room, conversation
and four-pose turnaround: no new seam appeared; the connected hairline, visible
ears, curved cranial depth and red shoulder overlap remain. The ear boundary
shift is subpixel at the room camera. The ear/profile remains coarse and the
32-pixel face simplifies the generated sheet. Production assets are unchanged.
