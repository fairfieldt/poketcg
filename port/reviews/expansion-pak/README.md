# N64 Expansion Pak and lead-face comparison

The trial renders at 480×360 with 8 MiB, with a 320×240 fallback on 4 MiB. The standard ROM remains available separately. The four lead faces are newly authored paint on unchanged models.

- [Gallery](index.html) · [resolution sheet](resolution-comparison.png) · [lead faces](lead-faces-after.png)
- [480×360 before/after](face-before-after-480.png) · [320×240 before/after](face-before-after-320.png)
- [Combined validation](validation.json)

All six final v3 cases are required by `gallery_build.py`. They contain 62 original ares post-VI PNGs and 14,284,800 exact callback RGB pixels. Each stage is gated by its own native framebuffer header, at least six later valid callbacks, and six stable frames. The original encoder PNGs and native source frames are linked individually. The same diagnostic ROM hash is required across RAM modes.

Both full native suites pass all 34 maps, 42 captured frames, opening, story, SRAM and an actual engine duel. Per-suite ROM hashes, runtime dimensions and worst free heap are recorded in `validation.json`. Separate host/UI/browser regressions were run by the parent task; this gallery does not infer those checks from screenshots.

Raw 640×240 VI scanout is displayed at 4:3. Contact sheets duplicate rows to 640×480 exactly; 640×480 scanout is unchanged. Face crops use display box [75, 95, 200, 225] for every person, resolution and before/after image, then 2× nearest scaling. Source texture and Blender inspection images under `authoring/` are labeled separately.

Render timings are ares emulated `timer_ticks` samples around scene rendering and RDP completion. They are not a physical-console FPS measurement or a 30 FPS promise. The eight-actor room medians and sample counts are recorded separately for both resolutions and both face sets.

Rebuild after all capture and native-suite results exist:

```sh
python3 port/reviews/expansion-pak/gallery_build.py
```

Production ROM paths, sizes and SHA256 hashes are in `validation.json`. The captures use diagnostic builds of the same renderer, with fixed poses and unchanged camera framing for each comparison.

Controlled earlier assets and identical camera/scene executable sections: [baseline-assets.json](baseline-assets.json). All eight before/after portraits differ only inside the face region: [face-pixel-proof.json](face-pixel-proof.json).
