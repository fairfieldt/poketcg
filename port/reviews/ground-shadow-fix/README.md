# Ground contact shadow fix

Contact shadows now use the same Tiny3D projector as the rooms and characters,
and the RDP's coplanar decal depth mode. The authored floor stays at Y=0.
The shadow does not write depth, so furniture and the later character pass keep
their normal occlusion. Its radius, colour, alpha and the characters' gait/bob
are unchanged. All interior ground contacts use this shared pass; elevated Amy
and Legendary-card actors retain their existing contact exclusions.

## Cause and evidence

The shipping renderer drew rooms and actors with Tiny3D but retained a GL shadow
fan lifted to Y=.045. The GL source position is quantized to .03125, and the fan
used the transparent/LESS depth path. This mixed projector/depth combination
can reject parts of a shadow as the camera moves across a floor triangle.

The native fixture compares each renderer to **its own** depth-disabled reference.
This matters because GL and Tiny3D have different raster silhouettes; comparing
one silhouette against the other would count harmless edge differences as
depth loss. Early cross-projector counts were discarded.

The final fixture samples the player on lab tile, lobby tile, Fire carpet,
Lightning runner and Water deck floors. It also samples stationary Mason and
Sam through the source intro's entry, middle and arrival camera states, while
Mark runs north along the original x14,z26→8 route. The source NPC states come
from `ow_new_game`; the private fixture selects three original two-unit route
steps rather than playing dialogue in real time. Every comparison freezes the
same presentation pose and checks `OwState` byte for byte.

The original path loses pixels on these actual floors, including Mason and Sam
during the lab approach. The new path matches its complete depth-disabled
shadow reference in every sampled state. The fixture additionally proves:

- a contact wholly behind Sam's real tabletop is completely hidden;
- shadows never change the completed depth buffer;
- the actor pass after the shadow has exactly the same depth contents;
- the normal 4 MiB and Expansion Pak render paths both run successfully.

`results.json` and each resolution's `coverage.log` record the final counts.
`floor-heights.json` records horizontal geometry heights in the representative
meshes. Walkable carpets are floor textures at Y=0; other low surfaces belong
to pool coping, planters and props, rather than walkable raised carpet meshes.

| Native mode | Samples | Old rejected pixels | New rejected pixels |
| --- | ---: | ---: | ---: |
| 320×240 / 4 MiB | 112 | 313 | 0 |
| 480×360 / 8 MiB | 112 | 896 | 0 |

The completed depth buffers match across 8,601,600 sampled pixels per path
at 320 and 19,353,600 at 480, plus the later actor-pass comparison.

## Images

The `native/` PNGs are completed, fenced N64 RGBA16 framebuffer readbacks.
The `post-vi/` PNGs are actual Ares `Program::video` output encoded by Ares,
with VI processing enabled. The provenance files verify every encoded RGB
pixel against the callback buffer. Neither set uses a WebGL render.
The 320 callback is 640×240 and should be displayed at 4:3; 480 is 640×480.

Representative actual VI images: [lab arrival at 480](480/post-vi/walk-3-after.png),
[Mason at 480](480/post-vi/lab-after.png), [Sam at 480](480/post-vi/sam-after.png),
[lab arrival at 320](320/post-vi/walk-3-after.png).

`walk-0`, `walk-3` and `walk-7` show three phases of the source lab arrival;
`lab` and `sam` show the actual source conversation poses. Before/after pairs
share the exact same actor/camera presentation state. A diagnostic-only capture
ordering mistake initially allowed the preceding actor-hidden reference to
remain on the VI while slow pixel logging ran. Those earlier captures are
excluded. The final fixture presents the completed image before logging its
stage and pixels, and the final VI pairs were inspected for actor presence.
The final pair checker also limits changes to small, low-contrast contact
regions; it rejects the real stale actor-hidden frame as a negative check.

## Implementation and reproduction

Production changes are confined to contact-shadow rendering in `scene.c`:
one 7,168-byte uncached vertex pool, reset after the previous frame fence,
and one shared identity transform per shadow pass. Its vertices stay immutable
until frame completion. The existing GL fallback uses its own supported
`GL_EQUAL` decal mode at Y=0; both backend variants compile with `-Werror`.

No gameplay, collision, save, RNG, actor coordinates, camera or animation
changes are part of this fix. The nearby loader-idle/audio work is independent.

Build the isolated full-engine renderer fixture:

```sh
python3 port/frontends/n64/adventure/tools/review_ground_shadows.py --build
python3 port/frontends/n64/adventure/tools/review_ground_shadows.py --case 4mib
python3 port/frontends/n64/adventure/tools/review_ground_shadows.py --case 8mib
```

The builder pins existing production objects privately, uses private settings
and saves, and injects depth-disabled reference variants into a private copy of
`scene.c`. Production has no diagnostic branch. Final VI commands and hashes
are recorded in `results.json`; the standard capture tool requires a fresh case.
