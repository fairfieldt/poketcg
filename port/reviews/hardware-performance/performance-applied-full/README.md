# Packed renderer full game validation

Both target configurations passed the complete native gate with the packed renderer, character LOD/visibility integration and cached duel felt enabled.

| Target | Maps | Native captures | Minimum free heap |
|---|---:|---:|---:|
| 320×240 / 4 MiB | 34 | 42 | **784,160 bytes** |
| 480×360 / 8 MiB | 34 | 42 | **4,018,464 bytes** |

Both completed native story, actual engine duel, SRAM, source intro-animation and Mason/Sam choreography checks. The shared diagnostic ROM hash, compilation flags, exact capture hashes, video-mode assertions and heap samples are preserved in [validation.json](validation.json) and each case's result file. The minimum free heap occurred in map 29 in both runs.

The validator creates a separate save directory per run and passes it through the private ares settings. Both produced SRAM files there. `Defocus: Allow` keeps one isolated emulator instance from pausing when the other gains focus. No user save directory was used.

Visual review covered both complete contact sheets, then naming, Water Club/Amy, Hall of Honor's elevated props, and Mason/Sam dialogue at full native resolution. No missing surfaces, new prop/actor clipping or new face obstruction was identified. Sam's white-coated body still overlaps foreground Mason in the canonical table composition; his face remains clear, and this composition predates the backend change.

The native camera scope restores the full display scissor before UI composition, keeping the entire naming keyboard/header/footer visible after the portrait pass. Three independent UI panel probes now reject a portrait-only image with a clipped keyboard. The completed-canvas presentation and RDP completion fences remain in place.

These are functional native-frame and memory checks in ares, not measured physical-console FPS. See the separate [fixed-geometry backend timing comparison](../backend/) and [felt measurements](../felt-optimization/README.md). The subsequent island geometry reduction has its own native/post-VI equivalence checks.

- [4 MiB complete contact sheet](cases/4mib/all-maps-native.png)
- [8 MiB complete contact sheet](cases/8mib/all-maps-native.png)
- [4 MiB full result](cases/4mib/result.json)
- [8 MiB full result](cases/8mib/result.json)

Reproduce with `DIAGNOSTIC=1 HIGH_RES=1 SCENE_T3D=1` in an isolated build directory, then `tools/validate.py --ram-mib 4 --width 320 --height 240` and `--ram-mib 8 --width 480 --height 360`, using separate `--output` paths. Native source logs remain under `build/performance-applied-full-{4mib,8mib}-v2/ares.log`.
