# Native booster opening

The native frontend now has the original pack receipt sequence, using the four original CGB pack scenes and their separate Pokémon logo sprites. It shows the sealed pack and receipt while the booster fanfare plays, waits on that same screen, restores the preceding music, shows the checked-cards acknowledgement, then opens the inspectable ten-card list. A short white transition can be skipped. A/START inspects a card and B closes the list.

The source does not tear a pack or force a separate reveal acknowledgement for every card. `src/engine/menus/give_booster_pack.asm` displays the pack/receipt/fanfare and checked-cards message. `src/engine/menus/booster_pack.asm` then opens the card list. The list retains the generated order: rare, uncommon, common, then Energy. No sorting, rerolls, inventory grants or saves occur inside the new presenter.

- [320×240 sequence](flow-320.png)
- [480×360 sequence](flow-480.png)
- [Individual native captures](native/)

`pack.c` owns a separate completed CPU/RDP canvas. It recomposes only when the screen or selection changes, and presents the retained image while waiting for input or music. Every RDP operation is fenced before the CPU reuses storage. A VI-rate edge capture prevents held buttons from acknowledging multiple stages and retains short taps during drawing. The list can scroll with held directions. Opening a card temporarily hands input to the existing card detail screen, then restores the pack list.

`tools/build_packs.py` decodes the original linked ROM's CGB tilemaps, 2bpp art, palettes and 32 literal `BoosterLogoOAM` entries. The generated four 64×96 PNGs and hashes are checked in under `adventure/assets/packs/`; ordinary builds do not require the GBC ROM. The card portraits use the existing ROM filesystem sprites and the small card stack uses the same generated card-back color master as the duel.

The isolated native gate rendered seven phases on actual 4 MiB and 8 MiB emulated machines, with 480×360 also checked on 8 MiB. Each phase generated its cards once through `gp_open_booster`, then asserted unchanged card order, complete `GpState` including inventory/RNG, WRAM and HRAM after rendering. All 320×240 frames match between the two memory configurations. A conservative heap-pressure fixture retained at least 632,952 bytes free while pack assets were live. This fixture reserves memory to reproduce the prior full-game minimum-free-memory budget; it does not load every world model itself. The small first-frame heap increase is the shared FPS badge's one-time allocation.

Native captures use production fonts, sprites, paper drawing and RDP copy/presentation, through `adventure_pack_preview`. These images are static UI fixtures; their audio calls are stubbed. They are separate from the subsequent real-audio/input integration gate below.

The host flow test checks the fanfare gate, fresh acknowledgement presses, skippable opening, exact ten-card order, list wrapping, inspection/exit and all 29 source pack-to-set mappings:

```sh
cc -std=c11 -Wall -Wextra -Werror port/frontends/n64/adventure/tests/pack_flow.c -o /tmp/poketcg-pack-flow
/tmp/poketcg-pack-flow
python3 port/frontends/n64/adventure/tools/build_packs.py --check
```

Integration is applied in `menus.c`, `extra_ui.c` and the native Makefile. Existing `game_show_booster` and PC-mail arrays route into the presenter. The existing `gp_open_booster`, `gp_open_mail`, RNG transfer and save calls remain intact. The browser retains its existing card list under the platform guard. Private static harness files, ROM and complete pixel-transfer logs are in `adventure/build/pack-review/`.

Diagnostic 24 additionally exercises the integrated production wrapper, UI and real compressed audio at 320×240/4 MiB and 480×360/8 MiB. The four actual `game_show_booster` calls grant exactly forty cards, one pack from each set. A copied seeded `GpState` supplies the expected generated cards without changing the live game. The gate compares exact card order, inventory and RNG/story state before and after presentation.

Synthetic SDK controller reads pass through the unchanged production VI input capture. Each pack enters with A already held, releases and presses A freshly on receipt, then keeps A held across the fanfare and checked-cards page. That page must remain until another release/fresh press. The test observes the opening transition, navigates the list down and up, inspects a card through the real card-detail screen once, and closes each pack with a fresh B. Every rendered phase is observed; the gate adds only diagnostic observer hooks to `pack.c`.

The world song remains paused under PC music, which remains paused under each booster fanfare. The actual mixer positions prove the world/PC decoders do not advance while paused; PC resumes after every pack, then world music resumes after PC closes. The gate finishes in about fourteen seconds at 4 MiB and fifteen at 8 MiB. Minimum free heap while pack assets are live is 1,331,840 bytes at 4 MiB and 4,371,904 bytes at 8 MiB. This gate retains the full resident game code; it does not load all world models simultaneously.

- [4 MiB integration result](native-integration/4mib.json) and [log](native-integration/4mib.log)
- [8 MiB integration result](native-integration/8mib.json) and [log](native-integration/8mib.log)
- Separate twelve-cycle retained-mixer regression: [4 MiB result](nested-audio/4mib.json) and [8 MiB result](nested-audio/8mib.json). Both audio-only cases run at 320×240 and cover exact fractional positions, decoder identity and nested stereo ownership.

The bounded integration source is `adventure/tests/pack_native.c`; main/Makefile select it only for diagnostic 24, with controller/card-detail linker wrappers. The private runner and build log are under `adventure/build/pack-native/`. No installed emulator settings or user saves changed, and no native process remains active.
