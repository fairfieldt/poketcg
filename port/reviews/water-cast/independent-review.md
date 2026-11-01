# Independent final Water cast review

**Pass for native still-image art and geometry acceptance.** The smaller,
front-centered facial features address the user's eye-size/side-wrap feedback
while retaining readable expressions. No blocking face, hair, cap or profile
defect is visible in the reviewed final captures. No model, renderer or camera
source was changed for this review, and no emulator was launched.

Inspected all five final 320×240 full and gameplay-LOD galleries (six actual
views each), the native eye-refinement comparison, awake Amy's conversation,
Sara/Amanda conversations and the lying-Amy room view. These are completed
production N64 RDP frames, not Blender art substituted for native output.

* **Eyes:** Mark, Amy, Sara and Amanda no longer place a large eye on the side
  of the head. The reduced feature width/height leaves a clear temple and
  cheek; profile views retain a small visible eye rather than a wrapped oval.
  The before/refined native crops demonstrate a real change at 320×240.
* **Profile and hair:** noses, chins and rear skulls have visible depth. Amy's
  blue bob/nape, Mark's backward visor/backpack and the swim caps/goggles have
  coherent front, quarter, side and rear silhouettes. No exposed scalp island
  or detached jagged fringe is apparent. LODs preserve the facial silhouette.
* **Personality:** Mark reads as a cute, confident trainer; Joshua's closed-eye
  grin is distinct and source recognizable. Amy remains calm and identifiable
  in her white outfit. Sara's cap/goggles and Amanda's uncovered golden bob
  now separate them beyond a colour swap, while their shared body is coherent.
* **Conversation/room reading:** awake Amy's face remains clear beside the new
  lounge. The nearby foreground Joshua head makes this camera composition
  busy, but does not cover Amy's face. This is a source-awake camera/framing
  limitation rather than a model regression; it is a future camera refinement,
  not a blocker for the character model pass. The lying pose remains supported
  and recognizable on the chaise.

Canonical `check_water_cast.py` passed all four families/six exports, including
actual float32/TRUNC-packed surfaces, closed/outward skulls, skin/hand pigment,
rig pivots, true profile/nose depth, Amy's derived pose and Sara/Amanda's exact
shared-body/distinct-head constraint. `check_caps.py` independently passed the
four ready-shell families and the three untouched capped families using their
original legacy proof. See `independent-native-validation.json` and
`independent-caps.json`.

Proof consistency also passes: the six current canonical mesh hashes match the
pinned capture inputs and the existing validation report; all six production
full meshes and all six gameplay LOD meshes match the captured runtime assets.
Both resolution manifests point to the same staged-input hash, and all 60 PNG
hashes match their frame manifests. See `independent-proof-consistency.json`.
The 480×360 manifest integrity was checked; the art observations above are
from the requested 320×240 set. This review does not claim CRT-shader, temporal
artifact or physical-hardware performance validation.
