# Duel input capture review

Read-only review of the VI input service, installed SDK interrupt/Joypad implementation, controller ownership and host regression coverage. Reviewed fixes are in `tcgn64.c`, `duel_input.h` and the two input test files; this reviewer made no production input changes.

**No remaining target ISR/API blocker was identified after the two concrete race fixes below.** Both native configurations exercised the actual registered VI callback through a complete engine duel and returned to the adventure without a lock or assertion.

| DIAG2 target | Maps | Live duel VI samples | Result |
|---|---:|---:|---|
| 320×240 / 4 MiB | 34 | **68** | PASS |
| 480×360 / 8 MiB | 34 | **162** | PASS |

Both completed the intro, native MIPS story, actual engine duel, SRAM, cinematic and map-tour markers. DIAG2 deliberately emits no framebuffer dumps. The shared tested ROM SHA-256 is `e1df1441e588bedc632a0b1e871797718ed3cfb997630bd84a724151862b58da`, built with `DIAGNOSTIC=2 HIGH_RES=1 SCENE_T3D=1` in `build/input-isr-gate`.

The counter increments only when the duel input service is active. In native builds that active service samples through its VI handler; the render poll does not call the sampler then. The positive counters therefore confirm execution of the real SDK/interrupt path, rather than only the host stub. Counter logging happens after unregistering and leaving the critical section.

Each run used private settings and SRAM paths with `Defocus: Allow`. See [validation.json](validation.json), [4 MiB log](4mib/ares.log) and [8 MiB log](8mib/ares.log). The marker watcher originally over-required a warp-transition marker that belongs only to DIAG3; corrected parsing of the already completed logs passed the actual DIAG2 contract without modifying or rerunning the ROM.

## Concrete findings and fixes reviewed

1. **Handler registration publication race.** The installed SDK's `register_VI_handler` does not protect its linked list. Native ELF disassembly publishes the new head before initializing its next/callback fields. The caller now disables interrupts across the complete begin/end ownership transitions, including registration/unregistration. Nested SDK critical sections correctly preserve the previous interrupt state.

2. **Late off-turn edge carryover.** A deterministic actual-frontend reproducer injected a P2 VI tap between `input_poll()` and `keys(1)`. Previously `keys` cleared only the render latch, leaving the new capture pending until a future P2 prompt. `keys()` now atomically merges eligible pending edges and consumes/discards both controllers' capture/latch state. The same reproducer now reports no leak, and a same-window on-turn tap remains accepted. Updated host tests cover both cases and the held-D-pad duplicate boundary.

`make -C port/frontends/n64 inputtest` passed after those fixes. The tests cover one-VI taps across 1–12-VI render intervals, passive redraw/ACK carryover, release/hold/fresh press, two independent controllers, Start, D-pad and unplug reset.

## SDK and lifetime checks

- `input_sample` uses current SDK button state and its own previous-held mask, avoiding render-rate edge loss. The pending capture and main-thread snapshots are protected by interrupt critical sections.
- The installed `joypad_poll` snapshots asynchronous Joybus results and parses them without a blocking controller read. Its GameCube-origin branch queues a bounded asynchronous operation. No graphics, audio mixing, allocation or debug printing occurs in the input VI sampler.
- The callback function has static lifetime. Duel/card-detail entry primes already-held state; card detail borrows an existing service when nested. End unregisters before clearing ownership and restores normal adventure polling.
- Button aliases map to the intended logical controls. The late D-pad edge seeds repeat state to avoid firing again merely because the next render first observes it held.
- A controller reconnecting while A is already held currently produces a new logical edge. Unplug reset is tested; physical hotplug/reconnect was not injected in the native runs. This is a stated behavior/test boundary, not an identified release blocker.

The native exercise verifies ISR registration/execution, engine traversal and cleanup. Physical short-tap interleavings are covered by host tests; no desktop input simulation was used.
