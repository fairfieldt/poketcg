# Compact, faster coin toss

The ordinary coin window is now 104×128 logical pixels at (108,42), down from 196×156: 56% less area. It expands only for long history rows, to 148 pixels wide. The complete 48-pixel coin, 20-pixel arc, landing bounce, result and history fit inside it. The original source artwork, actual engine result and controller ownership are unchanged.

The deliberate animation is 20 frames (16 spin + 4 landing), about 0.67 seconds at 30 Hz. The engine's following result acknowledgement remains separate.

## Measured cause and improvement

The previous 30-frame sequence was slow because it cleared and rebuilt the full static duel board, cards and text on every frame. In a populated six-Pokemon fixture, actual elapsed CP0 intervals were 1.992 seconds at 320×240 and 5.104 seconds at 480×360, despite an intended 1-second sequence. Median static board/text drawing alone was 47.3/129.1 ms per frame, plus 9.4/21.2 ms for acquisition/clear and 8.1/16.8 ms for the old panel.

The adventure now draws the board once and retains its completed CPU canvas. Each following frame replaces only the opaque coin rectangle. It still acquires a display buffer, writes back the canvas, completes the RDP copy/fence, presents and polls input. No additional pixel cache is allocated. Standalone direct-display builds retain their full-redraw path.

| Private Ares CP0 observation |320×240|480×360|
|---|---:|---:|
| Before, 30 frames |1.992s|5.104s|
| After, 20 frames |0.683s|0.769s|
| Hot frame total, including 30 Hz pacing |33.55ms|33.32ms|
| Hot coin painting |4.53ms|9.07ms|
| Presentation/copy completion |1.81ms|3.06ms|

These are emulator measurements with a 30 FPS cap, not physical-console FPS. Timed loops contain no per-frame screenshot dumps. The retained-board setup costs about 20 microseconds; most remaining frame time is waiting for the display cadence. The first frame still builds the board. Generic duel menus/ACK loops remain outside this optimization.

Timed damage/denied indicators, thinking ellipses and save indicators invalidate the retained board when their visible phase changes. A separate native check compared every pixel against a fresh full redraw for 20 frames at both resolutions, exercising damage expiry, denied-border expiry and thinking-phase transitions. All pixels matched; four board rebuilds were needed in that stressed sequence.

The web uses the same compact presentation and a 667 ms wall-clock trajectory. Host scheduling delays skip visual poses instead of lengthening the toss. Redundant cumulative result text was removed; the heading and ordered last 12 results remain. Disabled motion, held-B, keyboard and click skips still deliver the actual result.

## Captures and validation

- [Native coin motion detail](coin-toss-320.gif)
- [320×240 before/after window](panel-comparison-320.png), [480×360](panel-comparison-480.png)
- [320×240 native sequence](native-sequence-320.png), [480×360](native-sequence-480.png)
- [Web result](web/heads.png), [narrow long-history result](web/narrow-tails.png)

These PNGs are actual completed N64 RDP pixels **before VI filtering**. They include the current opponent name/metadata placement and real Game badge. Full poses are held for inspection; diagnostic pixel dumping can perturb badge samples, so use the separate unlogged timing data for performance. The GIF uses 20 native 148×128 coin-panel captures and a result hold; its palette is quantized for preview. Native PNGs preserve full color.

`make -C port/frontends/n64 cointest` passes real-engine result/RNG, multi-coin ordering, motion-option and ownership checks at both resolutions. The focused browser test passes duration, result concealment/reveal, long-history ordering, settings, keyboard/click skips and 320 px bounds. The isolated native timing/pixel harnesses live under `adventure/build/coin-speed-before`, `coin-speed-after` and `coin-review-compact`.

See [validation](validation.json), [before timing](before-timing.json), [after timing and full-canvas comparisons](after-timing.json), and [native hashes](native-validation.json).
