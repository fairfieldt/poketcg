# N64 UI and presentation release

Published both production ROMs to the Steam Machine on 2026-10-31. [Deployment hashes and archived releases](deployment.json) identify the exact builds. Publishing preserves live saves and leaves a running game alone; the next launch loads the update.

The release requires a fresh confirm after a drawn card appears. A human Basic Pokémon placed on the Bench immediately shows the updated main board with its acknowledgement, then the action menu. D-pad navigation can inspect either player's public Pokémon without answering an engine action request.

Unchanged duel lists, details and prompts retain their existing canvas. Dialogue and naming retain their existing panels while the world and player remain live. Native emulator UI cases reach the 30 FPS cap at both resolutions. These are emulator measurements rather than physical-console results.

The booster receipt uses the original pack artwork, fanfare, checked-cards page and inspectable ten-card list. World/PC/booster music decoders remain intact through nested pauses. Club exploration follows the player deeper into the room, and the Lightning spotlight remains continuous across its carpet.

Validation:

- Full adventure/story/gameplay, SRAM, world, object-clearance, characters and audio source checks pass.
- Dialogue: 480 CPU states and 24 native frame pairs per resolution, byte-identical.
- Naming: 768 CPU states and 24 native frame pairs per resolution, byte-identical.
- Duel input/UI: host source-engine answer/RNG equivalence, both controller roles, exact native canvas comparisons, 4/8 MiB runs and sanitizers pass.
- Four real booster grants per native memory configuration preserve all 40 generated cards, their order, inventory and story RNG; real card inspection and compressed audio pass.

Reports: [duel UI/input](../retained-duel-ui/README.md), [performance survey](../hardware-performance/fps-survey/index.html), [dialogue](../dialogue-performance/index.html), [naming](../naming-performance/index.html), [packs](../booster-opening/README.md), [club camera](../club-camera-follow/README.md), [spotlight](../lightning-spotlight/README.md), [queue wakeup backport](../rspq-wakeup/index.html).

The full browser build is refreshed at <http://ms.tdf.sh:8091/> with the shared camera, textures and portable-C updates.

The follow-up [control audit](../n64-controls/README.md) adds Z inspection during forced replacement choices and practice lessons, updates native/browser instructions, and preserves START card details. The current deployment hashes include that follow-up.

The [evolution-focus follow-up](../evolution-focus/README.md) starts the selector on the first legal Active or Bench target, preserving game state and existing manual choices. Current deployment hashes include this update.

The [Power-selection fix](../pokemon-power/README.md) replaces stale retreat text and stale Bench offsets, with clear Energy Trans source/destination prompts. [Camera follow](../area-camera-follow/README.md) now covers all interiors while the island stays a complete overview. Current deployment hashes include both. The broader Power/special-effect audit began after this release.
