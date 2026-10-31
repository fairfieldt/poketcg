# Follow camera in every interior

The approved club follow camera now applies to all 33 interiors, including
foyers, lobbies, Mason's laboratory, the deck-machine room, Ishihara's house
and the Hall of Honor. The room-size bounds and entrance dead zone remain.
The Dome foyer uses its actual 3.5-unit walls when constraining the view.

The user chose to keep the full-island overhead overview. Its camera and
fixed-camera occlusion bake are unchanged. Source intro cinematics still
override exploration, and the approved 20-degree conversation view keeps
final priority. Closing dialogue returns smoothly to the tracked position.
No canonical actor pose, story state, collision, save or RNG changes.

Validation:

- All 33 authored entry compositions and 2,972 source-walkable positions pass
  independent projection/size/TV-safe-bound checks.
- Four-direction walking sweeps cover camera lag and return toward the door.
- Every room verifies conversation priority and smooth return. Invalid
  speakers fall back to the tracked exploration view.
- All 144 source world routes, tutorial movement timing, actual Mason/Sam
  introductory sequence and canonical-state snapshots remain unchanged.
- Host checks pass with warnings as errors and ASan/UBSan.

Native comparisons use the production world renderer/Tiny3D with identical
fixed source NPC/player poses. The baseline returns the previous authored
camera; the candidate uses shared presentation follow. These static map
fixtures compare camera composition rather than an interactive playthrough.

- [320×240 lab/lobby/house comparison](interiors-320.png)
- [480×360 lab/lobby/house comparison](interiors-480.png)
- [Actual ares post-VI laboratory comparison](lab-post-vi-480.png): original
  encoder PNGs and capture provenance are under `480-vi/`. Both source-native
  frames exactly match the native-only run; all 614,400 callback RGB pixels
  match the emulator's original PNG output.
- [Validation and ROM hashes](validation.json)

The VI fixture holds each completed production frame to obtain stable scanout.
Its FPS badge therefore measures that presentation loop; it is not gameplay
timing evidence. No installed emulator settings or user saves changed.

The private harness reuses the previous club camera capture path under
`adventure/build/area-camera-review/`. Production edits are limited to
`presentation.c` and `tests/presentation.c`; no assets, main or Makefile changes.
