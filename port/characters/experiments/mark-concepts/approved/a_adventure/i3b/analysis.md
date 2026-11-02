# Final repair 3b review

Raised the crown by 0.05 units, tucked rear hair depth, and restored skin to the
ear/temple sector. Kept the solid red shirt and contiguous scalp/fringe. Native
16-frame capture passed; raw/packed audit passes at 560 triangles.

The native side view exposed too much tan skin: the material boundary follows
a large quad extending from ear-height to the cap. This is an overcorrection,
retained as evidence. Repair 3c must split those actual side polygons at the
ear-top boundary, giving the upper part back to the scalp and lower part to
the skin, rather than toggling the entire side quad.
