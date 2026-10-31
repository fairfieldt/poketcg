# Club exploration camera follow

The camera now follows Mark into the eight club duel rooms, Challenge Hall and
the Pokémon Dome. The authored entrance composition remains identical. Once
Mark advances beyond the entrance area, both eye and target move forward, with
a four-unit lateral dead zone and the existing smooth interpolation.

The target remains inside the room. A bounded downward adjustment keeps the
north wall at its authored screen height as the camera approaches it. The
calculation accounts for Lightning's 4.5-unit walls, the other enclosed rooms'
5-unit walls, and the 2-unit open walls in Grass/Challenge Hall.

Conversation cameras retain their approved 20-degree/FOV50 composition and
take priority. Closing a conversation returns smoothly to the current tracked
view. The island, laboratory, houses, foyers, lobbies and source intro remain
unchanged. No story state, actor coordinates/headings, save format, RNG, or
collision logic changes.

For a 2.2-unit standing silhouette at Lightning's leader approach (12,6),
projected height increases from 17.85 to 29.69 logical pixels: about 27 to 45
native pixels at 480×360. This is a projection envelope rather than an art
pixel count. The doorway view is unchanged.

Validation:

- Existing conversation tests, all 144 source world routes and the actual
  Mason/Sam introduction continue to pass.
- 1,206 source-walkable cell compositions across all ten rooms keep broad
  head/feet bounds within the TV-safe scene, preserve useful player size and
  constrain the north wall/sky band.
- Continuous movement sweeps cover all four directions and camera lag while
  returning to the door. Camera movement is bounded to the walking speed.
- Every room tests conversation priority and smooth restoration; every other
  map retains its exact authored camera. Canonical OwState snapshots remain
  byte-identical during presentation operations.
- Host tests pass with `-Wall -Wextra -Werror` and ASan/UBSan.

Native comparisons use a private harness including the production `main.c`
world renderer and current Tiny3D backend. The baseline returns the authored
camera; the candidate returns the shared presentation camera. Source NPC/player
poses and assets are identical. Mark/NPC idle animation is frozen equally in
both variants. The renderer still applies its normal visibility and character
LOD rules to each camera.

The current spotlight textures and room mesh are included in both variants.
Private source/ROM files live under
`port/frontends/n64/adventure/build/club-camera-review/`; canonical source edits
are limited to `presentation.c` and `tests/presentation.c`.

Captured comparisons:

- [Lightning at 320](lightning-320.png) and [480](lightning-480.png): entrance,
  middle and leader approach, before/after at native scale.
- [Fire at 320](fire-320.png) and [480](fire-480.png): the same three stages.
- Actual ares scanout: [320 comparison](lightning-post-vi-320.png) and
  [480 comparison](lightning-post-vi-480.png). The original encoder PNGs,
  per-stage native frames and capture provenance are under `320-vi/` and
  `480-vi/`.
- [Validation data](validation.json) records 24 native frames, both ROM hashes,
  room mesh hashes and four post-VI captures. The four VI-gate native frames
  exactly match their native-only counterparts. Ares's original PNG output
  matches its callback RGB for all 921,600 pixels.

Both rooms' doorway before/after native images are byte-identical at both
resolutions. The final Lightning leader frame's central ceiling band is five
logical pixels, compared with four in the baseline. The initial candidate
used an incorrect five-unit Lightning wall height and was replaced before
the final captures.

The VI harness holds each completed production frame while the emulator's
encoder captures stable scanout. Its FPS badge therefore measures that held
presentation loop; these screenshots are composition evidence, not gameplay
timing measurements. No installed emulator settings or user saves changed.

An initial 480 capture was interrupted when the host disk filled. Its
incomplete log was excluded; `native-480-8mib-v3` is the passing final case.
The final 320 native case is `native-320-4mib-v2`.
