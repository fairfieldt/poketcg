# Retained duel UI and board inspection

The hand browser, card details, practice reader, choice lists and adventure text/menu loops now keep the completed UI canvas until their controls or visible state change. Cursor/page/typewriter changes, exact WRAM/HRAM changes, names, logs/events, damage/denied expiry, saving/thinking phases and intervening nested screens invalidate reuse. Each live request still polls input and presents a completed frame. The existing RDP canvas ownership, writeback and presentation fences remain unchanged.

The implementation adds no image or heap allocation. The native shared snapshot is 8,432 bytes, with 11 more static bytes for generations and confirmation flags; each local reuse token is 24 bytes. Compiling the frontend with the same production flags before/after gives object-section deltas of +10,518 text/constant bytes, unchanged data and +8,443 BSS bytes. Linked alignment can differ slightly. The existing board backing still releases on duel exit.

Up from the top action row enters the public board. Up/Down traverses occupied Bench/Active rows, Left/Right selects occupied Bench slots, A/START inspects, and B returns to the remembered action. Physical top/bottom layout remains fixed while input stays owned by the requesting controller. Inspecting never answers the pending action request. B+A returning to actions consumes that sample, requiring a fresh A before selecting an action. Forced attack/card/discard/placement selectors retain their existing controls and engine return values.

Drawn-card ACKs now ignore confirm edges queued before the first completed card presentation and require release if a confirm was held. Successful human Basic placement switches the stale Hand presentation to the main board, displays the placement message until fresh confirmation, then protects the following actions from inherited confirmation. The engine already returns to actions after successful placement, so there is no automatic Hand cancellation. Intervening powers/forced choices and a later intentional Hand choice remain intact. These gates add no fixed time delay and do not change held-B animation skipping.

## Native timing

These are Ares v148 CP0 measurements through actual production UI loops, with real compressed music, mixer and RDP presentation, a 30 fps cap, and a test-only logical-key driver. They are emulator measurements, not physical-console FPS. After eight warmup frames, each case records 32 frames without framebuffer dumping during its timed loop.

| UI loop | 320×240 median work | 480×360 median work | Median frame cadence |
| --- | ---: | ---: | ---: |
| Hand list | 5.41 ms | 6.77 ms | about 33.4 ms |
| Duel card detail | 5.40 ms | 6.66 ms | about 33.4 ms |
| Adventure card detail | 5.40 ms | 6.54 ms | about 33.4 ms |
| Generic menu | 5.38 ms | 6.60 ms | about 33.3 ms |
| Practice reader | 5.37 ms | 6.71 ms | about 33.4 ms |
| Drawn card with fresh confirmation | 5.28 ms | 6.63 ms | about 33.3 ms |
| Placement acknowledgement | 5.35 ms | 6.66 ms | about 33.4 ms |
| Following actions | 5.38 ms | 6.74 ms | about 33.4 ms |

The unchanged loops reach the 30 fps cap at both resolutions. A changed card/page still has a cold render; this pass removes repeated rasterization while waiting for input. Full timing distributions and samples are in [native-8m.json](native-8m.json). [native-4m.json](native-4m.json) confirms the same actual UI, confirmation, pixel and controller paths with 4 MiB and 320×240.

## Validation

- All 11 scripted frontend tests pass at both resolutions, including practice, hotseat/port routing, continuation, widgets and the new board-inspection flow.
- The pure navigation helper checks 97,544 states across all 4,096 occupancy pairs. The actual UI script verifies wrong-controller input is ignored, both board sides inspect correctly, and inspection/B+A never answer an action accidentally.
- Retained-canvas host tests compare 276 complete frames, including 108 reused frames, state/log/page changes, nested owners and real `end_frame` effect expiry. Existing board-cache checks still pass 369 complete frames.
- Native whole-canvas comparisons pass 128 frames at 8 MiB across both resolutions, and 64 frames at 4 MiB. Native board-inspection scripts pass for both requesting controller ports at each mode, including nested inspection and fresh input on return.
- The actual-engine ACK regression verifies byte-identical WRAM/HRAM, event/request/answer digest and RNG against equivalent headless answers. A pre-presentation A tap cannot dismiss the drawn card, placement waits on the main board, following actions reject the carried A, and forced plus later intentional Hand requests remain usable.
- AddressSanitizer and UndefinedBehaviorSanitizer pass the retained-canvas and engine ACK-flow tests.

[validation.json](validation.json) summarizes the checks. Native PNGs in this folder are original RDP framebuffer readbacks before VI processing; board-focus captures intentionally pause for dumping and are not FPS measurements. The isolated build/runner is under `port/frontends/n64/adventure/build/retained-ui-native/`, with a separate `retained-ui-native-4m/` run. No private save material is included.

Final heading-only follow-up: top list headings now start at logical y16, below the badge and with their 12-pixel font ending before the first row highlight at y28. Adventure menu/message headings use y18, above their unchanged y27 separator. The map-name plaque moves from y6 to y12 with its text at y18; its RDP source/blit rectangle moves with it. The badge, navigation, rows and engine behavior are unchanged. The retained-canvas host comparisons pass at both resolutions, and physical bounds were checked at 1x and 1.5x. Native PNGs and timing data above were captured before this final heading adjustment.
