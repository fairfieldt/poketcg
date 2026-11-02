# Remove animated opaque shadows from actor models

This is the original eight-sided-disk repair. The later reviewed cast exposed
its shape-specific limitation. The current
[ownership-based filter and native regression proof](../actor-ground-shadow-regression/README.md)
supersede the recognition policy described below.

The remaining pop/shrink came from a second shadow embedded in the character
models. The shared soft floor decal was already stable, but every humanoid
also had an opaque eight-sided disk in its body mesh. The disk inherited the
whole actor's bob and running lean. Its authored height (.01584–.01836 units)
truncates to Y=0 in the packed actor coordinates (1/32-unit steps). Idle bob therefore
moves it above/below the floor; running lean tilts its sides through the floor.

Earlier floor-only tests hid actor geometry and could not see this second disk.
The new test renders the actual characters and original source opening.

## Fix and exact asset checks

Runtime import now recognizes the complete legacy primitive: exact source
colour, rigid part/material/pivot, eight canonical ring coordinates, six
exclusive uniformly wound triangles, boundary edges and polygon area. This
is not a general dark-plane or low-geometry filter.

It removes only those triangle references from the N64/web shared imports:

- 41 full actor exports, including appearance variants and Amy's lying pose;
- 41 corresponding LOD exports, migrated only when their old source/target
  hashes prove the change is solely this disk removal;
- the four reserved fallback humanoids in `actors.l64`.

That removes 516 triangle references across 86 disk instances. Vertex records
are deliberately retained; the eight disk vertices become unreferenced.
Every other triangle, vertex position, UV, colour, pivot, texture and source
index order remains exact. All 88 authored L643/Blender hashes are unchanged.
The nonhuman card/torch/cursor meshes are unchanged. N64 and web now use their
existing separate floor-contact pass. Its styling, height and alpha are unchanged.

`assets.json` records all 99 checked runtime files. The strict recognizer has 36
positive/negative cases and 4 conservative LOD source/hash-binding cases.
Existing character/LOD checks pass, including no-op sync, aliases, incomplete
export retention, protected seams and exact textured faces. The original
authoring files and the external character experiments were not regenerated.

## Native composited proof

The private fixture runs genuine `ow_new_game` movement to Mason, then holds
the first source speech state with the production conversation camera. It
compares 52 consecutive lab-run updates plus 64 idle/camera updates before and
after. Every player/NPC pose and camera matches exactly across 116 updates;
rendering leaves the source state unchanged and final story/RNG state matches.
The speech callback holds the scene instead of typing the dialogue UI; no
actor position, heading, gait, bob or camera target is invented for the test.

Both 320×240/4 MiB and 480×360/8 MiB pass with real native rendering/audio.
Independent with/without-soft-contact probes show the floor decal remains:
122 run / 296 idle changed pixels at 320; 266 run /657 idle at 480.

At 320, the old disks contribute 109 visible pixels at idle update 24 and zero
at update 56. Mark's foot region alone goes from 68 changed pixels to zero.
This is the reported animated opaque patch appearing/disappearing; the cleaned
frames retain the same feet/body and a separate soft floor contact.

The `sequence/` folders contain completed, fenced native RDP readbacks.
The `native/` and `post-vi/` folders contain selected paired native readbacks
and **actual original Ares VI output**, with callback/encoder pixel provenance.
Both final VI gates pass: six images per mode, 2,764,800 encoded RGB pixels
verified against the original emulator callbacks. At idle 56 the native
before/after images are identical. At 480 the corresponding VI pair differs
only in 26 bottom-margin pixels on the last scanline, rather than actor pixels.
The 320 VI callback is 640×240 and should be displayed at 4:3; 480 is 640×480.
No WebGL screenshot or altered trailer footage is used as native evidence.

Selected 480 actual VI: [before idle24](480/post-vi/before-idle-24.png),
[after idle24](480/post-vi/after-idle-24.png),
[before idle56](480/post-vi/before-idle-56.png),
[after idle56](480/post-vi/after-idle-56.png).

## Reproduce

```sh
python3 port/world3d/tools/check_actor_grounding.py
python3 port/world3d/tools/check_characters.py
python3 port/world3d/tools/check_character_lods.py
python3 port/frontends/n64/adventure/tools/review_actor_grounding.py --build
python3 port/frontends/n64/adventure/tools/review_actor_grounding.py --ram-mib 4
python3 port/frontends/n64/adventure/tools/review_actor_grounding.py --ram-mib 8
```

The optional migration audit uses
`--before port/frontends/n64/adventure/build/actor-grounding-before`; that
private snapshot holds the old runtime meshes and authored hashes. The native
builder uses it for the before pass and current shared meshes for the after
pass. `--build --selected-captures` keeps the same 116-update state checks while
logging only run-40 / idle-24 / idle-56 for the standard post-VI capture harness.
Both variants use the same soft-shadow renderer. Production requires no new
renderer code, animation adjustment or additional allocation.
