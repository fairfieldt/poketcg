# Water Club character pass

The source Water Club cast comprises Amy, Joshua, Sara, Amanda and the player,
Mark. The four canonical families now use owning volumetric meshes, with a
source-specific head variant for Amanda. The same files feed N64 and WebGL.

## Design

- **Mark:** rounded cheeks and jaw, a real nose/profile, rear cap visor and a
  rounded backpack; the approved Mark face artwork remains the basis.
- **Amy:** swept blue bob/nape, white sleeveless dress and pool shoes; one
  canonical mesh supplies both the standing and lying pose.
- **Joshua:** brimless swimming cap, forehead goggles, dark nape and the source
  portrait's cheerful closed-eye grin.
- **Sara:** white swim cap, raised goggles and a cheerful expression.
- **Amanda:** uncovered golden side-part bob, one raised brow and a small smirk.
  Her body and rigid animation rig match Sara exactly; the head has its own
  source-led silhouette and facial texture.

Original portraits are `src/gfx/duelists/{player,amy,joshua,sara,amanda}.png`.
The overworld sprite atlas and original palettes remain the basis of costumes
and colours. [Source/current baseline](source-current-reference.png) is an
inspection board, not an emulator capture.

The initial larger eyes spread too far toward the temples. Following the
user's feedback, the facial features now occupy 120 degrees across the front
and 80 percent of the earlier vertical extent. The owning curved face surface
still extends around the cheeks; its temple texels are plain matching skin.
Noses, cheeks and jaws remain actual geometry. All skin, including hands,
receives the same character pigment and the same scoped lighting floor.

The new heads are manually authored closed shells with disjoint skin, face and
hair/cap material regions. They do not need a Boolean union of thin face and
hair patches. Rigid part-0 head pivots keep the full head silhouette and facial
paint exact in gameplay LODs. Lower-detail bodies retain their original batch
whenever simplification creates duplicate or degenerate triangles at the
N64's actual float32 → trunc(position × 32) precision.

## Authoring and checks

`water_cast_common.py` supplies closed head/mesh helpers; the four `water_*.py`
recipes supply character-specific shape and colour. `build_model.py` also
supports isolated assets through `POKETCG_CHARACTER_ASSETS`. Sara/Amanda's one
saved Blender file retains head presets; `apply_variant` updates geometry,
face texture metadata and palette together. Existing non-Water palette helpers
keep their previous behaviour.

`check_water_cast.py` independently verifies actual native coordinates, raw and
packed surfaces, closed/outward head topology, profile/nose depth, skin/hand
pigments, texture opacity/TMEM, five rigid joints, Amy's derived pose and exact
shared-body preservation for Sara/Amanda. Earlier nearest-grid prototype checks
were superseded by exact float32/TRUNC checks. Small goggle and hand details
were reshaped rather than exempted from those checks.

## Final verification

- [Native source checks](character-validation.json): six exports, actual float32
  and N64 packed surfaces, opaque 32×32 RGBA16 faces (2 KiB each), five joints,
  exact shared Sara/Amanda body, distinct heads and Amy's derived lying pose.
- [Runtime integration](runtime-integration.json): every accepted full mesh,
  gameplay LOD and face PNG matches the pinned native capture inputs byte for
  byte. All 139 previously existing unaffected runtime mesh/texture files and
  all room geometry, cameras, permissions and NPC anchors remain unchanged.
- [Canonical isolation](canonical-isolation.json): all 137 unaffected existing
  canonical model/image files remain unchanged; only the four selected families
  and their derived exports changed.
- Native framebuffer captures: [4 MiB / 320×240](native-4mib/result.json) and
  [8 MiB / 480×360](native-8mib/result.json), 30 cases per mode. Each character
  has six directions/gait views at full detail and actual gameplay LOD, plus
  before/after conversation shots and Amy's lying room state. These use the
  production N64 renderer, lighting, rooms, actor selection and camera code.
- The corrected awake Amy fixture follows `Script_MeetAmy`: Amy at (20,4),
  facing south; Mark at (20,6), facing north. Joshua remains at his returned
  source anchor (22,8). His foreground head crowds this particular conversation
  angle; this pass does not hide actors or change production cameras.
- [Independent art and geometry review](independent-review.md) accepted the
  refined eyes, distinct personalities and clean profiles at native 320×240.
- The [resident-memory gate](resident-footprint.json) retains the normal
  production entry graph and all 2,514 production function names. A 27,392-byte
  BSS reserve covers conditional/pinned-object differences and adds a 16 KiB
  conservative allowance beyond the production LOAD span. The
  [independent resident review](independent-resident-review.json) verifies
  the linked sections, symbols and actual startup references to the reserve.
  The corrected Water Club sweeps leave **906,064 bytes free at 4 MiB**
  and **4,140,368 bytes free at 8 MiB**. These are scene-test margins, not
  worst-case duel/save allocations. Recorded CP0 timings are emulated cadence,
  not physical-console measurements.
- Linked Blender cast validation, all 39-family skin/exterior checks, character
  sync/grounding/LOD/world checks, and [all 34 rooms in Chromium WebGL](webgl-validation.json)
  passed. No GL or browser errors; Mark's naming portrait was also rendered.

The 12 original Ares VI frames are preserved in [standard](vi-4mib/comparison-4x3.png)
and [Expansion Pak](vi-8mib/comparison-4x3.png) display comparisons, with raw
source PNGs and callback/RGB provenance. These use the earlier, smaller **art-only**
review ROM; its memory values are not full-game headroom. Their native pixels
match the accepted frames, and the later full-resident memory sweep must match
all accepted frames exactly. Use [the capture summary](capture-summary.json)
and resident gate for memory claims.

[Eye refinement comparison](eye-refinement-native-head-crops.png) compares the
initial Water-pass eyes to the accepted smaller/front-set eyes using actual
N64 framebuffer crops. The full direction galleries and source hashes are in
[the comparison record](eye-refinement-comparison.json).

## Builds

[Build/publication record](deployment.json) lists the exact local standard and
Expansion Pak ROM hashes. Both are normal game builds with the development Game
FPS counter enabled. The user requested **leaving the ROMs ready locally**;
no Steam Machine publication or restart is part of this pass. Existing saves
were not modified. The shared web assets are also published to
<https://poketcg64.exe.xyz/> and <http://ms.tdf.sh:8091/>.

The private capture harness is `adventure/tools/review_water_cast.py`; all
baseline copies, pinned models, private ROMs and emulator save directories live
under `adventure/build/water-cast-*`, outside the production paths. The committed
screenshots and reports contain no user save data.
