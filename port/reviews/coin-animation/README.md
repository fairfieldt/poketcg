# Coin toss animation

The N64 frontend ignored coin animation events and showed only result text. It now displays the original CGB Pikachu coin, tosses it through an arc with edge-on flips, lands with a short bounce, then reveals Heads/Tails and adds the result to the ordered history. The opening toss, attack tosses and between-turn status tosses share the engine's existing event path.

The frontend animates only the result received in `TCG_EV_COIN`. No engine RNG, game result, prize or payout code changed. The original toss sound is played during launch; the engine's existing result sound follows the landing. The animation-disabled setting shows the settled result immediately. A/B can skip pacing for the tossing controller; the allowed held-B shortcut is honored. The animation consumes its skip input and leaves the subsequent engine ACK separate. Long coin sequences retain the last sixteen results in order and show the last twelve.

The web uses the same source faces, flight/landing/reveal and history. Keyboard, allowed held-B skipping and clicking the active coin all route through its current control. The modal stays inside a 320px viewport and no longer compresses the coin into the bottom text panel. The adventure passes its animation setting and original launch sound callback; existing engine result sounds remain unchanged.

## Artwork

`tools/build_coin.py` decodes the original OAM/tile data and CoinPal into 24×24 RGBA images and a 2,304-byte native header. `core.asm .done_toss_anim` maps e=HEADS to animation170 (Pikachu), e=TAILS to 171 (plain reverse); the source DUEL_ANIM_COIN_HEADS/TAILS labels at that branch are inverted, so the mapping follows the actual code. See [source manifest](../../assets/ui/coin/manifest.json). No generated artwork substitutes for the original coin.

## Captures and checks

- [320×240 native animation preview](coin-toss-320.gif)
- [320×240 native sequence](native-sequence-320.png)
- [480×360 native sequence](native-sequence-480.png)
- [Web flight](web/flight.png), [Heads](web/heads.png), [Tails and history](web/tails-history.png)

Native PNGs are actual completed N64 RDP framebuffers **before VI filtering** from the production renderer, canvas and completion fence. The deterministic capture fixture uses the real card database and delivers Heads then Tails; it does not run an alternative randomizer. Thirty coin-panel captures provide the complete 320×240 flight sequence. The GIF composites those exact panels onto the unchanged native board and uses palette quantization for preview. Full-color native PNGs and their hashes remain alongside it. The private capture harness/raw log are under `adventure/build/coin-review`.

`make -C port/frontends/n64 cointest` exercises actual engine TossCoin/TossCoinATimes callbacks at both resolutions. It verifies identical RNG/results with animation enabled/disabled, ordered multiple coins, opening/AI/status contexts, long histories, disabled motion and controller ownership. The nine interactive host scripts and explicit all-card/practice layout checks passed at both resolutions. The latter printed semantic PASS markers using a nonempty idle script, avoiding the mock EOF early-exit issue discovered during the input-buffer change. `node port/frontends/web/test/coin_animation.mjs` passes flight/result ordering, settings/skip/click behavior, narrow layout and browser error checks. The existing duel-presentation browser suite also passes. The N64 frontend compiles successfully.

See [validation.json](validation.json) and [native capture hashes](native-validation.json).
