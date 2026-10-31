# Ronald hair surface repair

Ronald's cap, eight spikes and three fringe pieces now join the head and face in one closed owning exterior. Hidden scalp and overlapping hair triangles are removed. The upper scalp above the existing 1.88-unit head ring uses the hair pigment, so true small gaps between the faceted cap and skull cannot show skin islands. The painted face retains its exact image and UVs, including its original 1.89-unit upper edge.

A numerical union sliver is collapsed at its shortest edge instead of being deleted into a pinhole. The resulting character uses 910 triangles and six batches, within the existing 1,000-triangle validation limit. Animated limbs, lower torso and all other character model files remain unchanged.

[Validation](validation.json) checks the actual emitted native triangles, one closed head component, winding outside/inside, absence of upper-scalp skin triangles, unchanged limbs/lower torso, and unchanged sly face paint. It records 402 native head triangles, no non-exterior faces and no boundary edges. Source validation does not replace the subsequent ares screenshots.

Rebuild with `python3 port/characters/tools/build_all.py --only ronald --force`. Run the surface check in Blender with `--python port/characters/tools/check_ronald_hair.py`. An optional `-- BEFORE_L64` argument also compares the previous animated limbs and lower body. The parent task owns native camera-angle screenshots and production ROM builds.
