# Packed 3D backend experiment

2026-10-31. Tiny3D adapter for the existing L643 scene format. It is now the production default after the [complete 4/8 MiB native gates](../performance-applied-full/). Cached libdragon GL remains selectable with `SCENE_T3D=0`; legacy standalone diagnostics 5/6/7/8/12 retain that default until their camera scopes are adapted.

The controlled same-geometry comparison shows a substantial geometry-path improvement: **eight high-detail lead actors in Mason's lab fell from 115.38 ms to 37.12 ms**, and a four-actor conversation from **64.33 ms to 21.35 ms**. These are ares-emulated CP0 elapsed intervals, not physical console FPS. The experiment does not resolve real RDP fill/memory timing.

| Fixed workload | Cached GL median | Packed median | Speedup |
|---|---:|---:|---:|
| Mark portrait | 16.16 ms | 6.22 ms | 2.60× |
| Ronald portrait, repaired hair | 16.21 ms | 6.13 ms | 2.65× |
| Mason portrait | 13.38 ms | 5.35 ms | 2.50× |
| Imakuni portrait | 10.21 ms | 4.63 ms | 2.21× |
| Lab, eight lead actors | 115.38 ms | 37.12 ms | 3.11× |
| Conversation, four lead actors | 64.33 ms | 21.35 ms | 3.01× |

Each stage has 24 samples, actual 480×360 framebuffer, 8 MiB RAM. Both six-stage native/post-VI capture suites passed, each checking 1,843,200 original encoder/callback RGB pixels. Source meshes, textures and poses are unchanged; no LOD or application visibility reduction occurs in this fixture. See [results.json](results.json) and the per-case capture provenance.

After all fixture assets loaded, heap use fell from **2,400,320 to 2,331,072 bytes**, despite about 21 KiB more resident code/read-only data. Native GL command streams are replaced by compact geometry command blocks and persistent DMA-fed vertices.

## Implementation

`SCENE_T3D=1` selects the alternate renderer. `tools/fetch_tiny3d.py` fetches official Tiny3D revision `f2eccc3998967f1915be58d52f1c5bb6d5598a8c` under ignored `build/`, building only its core, math and RSP objects. The SDK is unchanged; optional streamed animation/model-loader modules are unnecessary because this adapter retains the existing material and six-part rigid animation system.

The loader packs source position/color/UV attributes into 16-byte native vertices, splits indexed input into bounded 70-vertex chunks, and records material/geometry RSP queue blocks. The original source geometry and triangle order remain intact. It preserves RGBA16 sprite textures, repeat/clamp behavior, shade modulation, perspective filtering, opaque depth, disabled backface culling, and the existing GL shadow/UI presentation path. No advertised triangle-rate claim is used as evidence.

The packed backend uses explicit `scene_camera(...)` and `scene_frame_end()` scopes, because the installed GL implementation does not support querying its current projection/model-view matrices. `scene_draw_actor_presented(...)` accepts the complete outer player/NPC transform, while the older actor API remains available. The cached-GL implementation remains selectable at build time.

Camera matrices and actor matrices remain allocated until the existing completion fence. Position quantization matches GL's five fractional source bits. Projection/UV/raster arithmetic differs between the two RSP pipelines, so screenshots are **not pixel-identical**: portraits have small changes of several pixels in edge/feature placement. Independent visual inspection found no missing room surfaces, lost textures, new repaired-hair clipping, obvious depth corruption or obstructed faces in these six views. This comparison does not replace the remaining full game/map checks.

## Adoption validation

Main world/cursor poses and naming now use the explicit camera/actor APIs. The complete 320×240 / 4 MiB and 480×360 / 8 MiB suites passed 34 maps and 42 captures each, including story, SRAM, actual engine duel, intro and cinematic checks. Naming, close dialogue, Water Club/Amy, elevated Hall props and both complete contact sheets were visually reviewed. The native camera scope restores full-display scissor before UI composition. Legacy renderer-only diagnostics default to `SCENE_T3D=0` until they establish explicit camera scopes.

The 37 ms unmodified crowded lab is still above a 33.33 ms 30 fps budget before unmodeled physical fill costs. The separately implemented character LOD, conservative visibility work and island simplification are still useful. This result supports a targeted backend migration; it does not establish universal 30 fps on hardware.

Reproduce builds with isolated directories:

```sh
make BUILD_DIR=build/backend-gl ROM=poketcg-backend-gl DIAGNOSTIC=11 HIGH_RES=1 SCENE_T3D=0 bundle
make BUILD_DIR=build/backend-t3d ROM=poketcg-backend-t3d DIAGNOSTIC=11 HIGH_RES=1 SCENE_T3D=1 bundle
```

Run the existing `port/reviews/expansion-pak/capture.py` with diagnostic 11 and stages `mark-portrait ronald-portrait mason-portrait imakuni-portrait leads-room leads-conversation`, using fresh case directories. `summarize.py` reads the recorded pair and verifies matching non-LOD world asset bytes.
