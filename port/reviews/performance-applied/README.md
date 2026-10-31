# Applied N64 performance and presentation update

The default renderer is now the pinned Tiny3D core integrated with libdragon. Cached GL remains selectable with SCENE_T3D=0; legacy standalone renderer diagnostics retain that default until explicitly wired. The production world, complete presented actor transforms, cursor depth reset and naming viewport have explicit camera lifetimes. The scissor is restored before full-canvas UI blits.

There are 49 optional character exports: 27,984 high-detail triangles versus 19,858 gameplay-detail triangles across the catalog. Textured face batches, rigid pivots, open seams, opacity and source assets remain intact. Blender generation locks seams and volume anchors, biases color edges and retains any batch that would produce degenerate/duplicate surfaces. Normal builds use checked-in data; changed source meshes invalidate their LOD automatically. Close conversation subjects/player force full detail, while 48/60 logical-pixel hysteresis avoids threshold flicker. These catalog totals are not per-frame submissions.

The shared camera frustum conservatively rejects only complete offscreen actor/shadow spheres. The island drops 252 proven hidden triangles (5511→5259), with native and VI byte equality at both resolutions under both backends. All viewport-only triangles remain for wider browser windows. Geometry/camera hashes protect the export bake.

The new duel felt cache retains19,200/43,200bytes and preserves every pixel, existing completed canvas, cache writeback, RDP copy and completion fence. A controlled actual N64 CPU-only benchmark reports about82% less felt time; it is not a complete-duel frame-rate claim.

The capture cases in this directory use the same DIAG15/HIGH_RES1 ROM in both RAM modes. All12 original VI PNGs pass exact callback-RGB verification, matching native headers, six later valid callbacks and six stable callbacks. The gallery records emulated CP0 medians and links separate full native34-map/42-frame gates, unchanged-geometry backend A/B, intro/orb/audio and coin proofs. Re-run build_gallery.py after replacing both cases.
