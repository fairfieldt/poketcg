# Coin result dismissal

The native coin panel now closes when the attack animation starts, with a
direct-damage fallback. A later coin-spin event can reopen it without losing
the source sequence's result history. Opening flips and every ready/result
acknowledgement keep their existing lifetime and controller routing.

The original source establishes this boundary:

- `src/engine/duel/core.asm` `_TossCoin` preserves the coin screen through all
  flips and acknowledgements, and does not redraw it on return.
- `src/engine/duel/animations/commands.asm:3` `PlayAttackAnimationCommands`
  selects `SET_ANIM_SCREEN_MAIN` before the normal attack effects.
- The same file's `SetScreenForDuelAnimation` calls `DrawDuelMainScene`.
- Portable `duel_ui.c` emits the attack-animation event and damage, while the
  native frontend previously retained its earlier `COIN_TOSS` screen.
  Consequently its later damage/KO redraws painted the landed coin again.

The production change is presentation-only in `tcgn64.c`. It changes no engine
events, answers, WRAM, HRAM, RNG, animation duration or input handling. Its
existing event revision and screen fields invalidate retained UI canvases.
It does not dismiss the coin on arbitrary text or acknowledgement events.

## Checks

`make -C port/frontends/n64 cointest` passes at320 and480. The extended actual
engine regression covers opening flips, AI flips, three-coin attacks, both
P1/P2 owners, disabled-animation result/RNG parity, zero-damage attacks,
the source sleep-check redraw sequence and the direct-damage fallback.
Ready/result acknowledgement counts remain exact. Equal answers preserve the
whole WRAM/HRAM and event/request trace compared with a non-rendering host.
A retained coin canvas is invalidated before an ordinary board redraw.

The native component fixture passes at320×240/4MiB and480×360/8MiB:

| Each resolution | Result |
| --- | --- |
| Human owners | P1 and P2 |
| Ready/result coin ACKs | 6 / 6 |
| Real 40-damage paths | 2 |
| Real HP0 / Goldeen KO paths | 2 |
| Completed board-frame comparisons | 4 exact, pixel for pixel |

The fixture calls the real portable engine coin and damage/KO presentation
functions through the production native frontend. It automatically confirms
ACKs only in a private copied frontend and uses audio stubs. The PNGs are
completed, fenced raw RDP frames, **not post-VI screenshots**. Full Amy captures
with real controller edges/audio are made separately by the trailer recorder.
No screenshot or footage was painted over to remove the coin.

Native examples: [320 damage](320/damage-board.png),
[320 KO](320/goldeen-knockout.png), [480 damage](480/damage-board.png),
[480 KO](480/goldeen-knockout.png). Ready/result images are alongside them.
The report runtime logs omit repetitive pixel lines; full logs stay under
`adventure/build/coin-lifecycle-review/{4,8}/runtime.log`.

```sh
python3 port/frontends/n64/adventure/tools/review_coin_lifecycle.py --build
python3 port/frontends/n64/adventure/tools/review_coin_lifecycle.py --ram-mib 4
python3 port/frontends/n64/adventure/tools/review_coin_lifecycle.py --ram-mib 8
```

The first native-reference attempt omitted the ACK's blinking continuation
arrow from its fresh reference. Correcting that diagnostic reference produced
exact comparisons at both resolutions; no production adjustment was needed.
