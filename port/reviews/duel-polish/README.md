# Duel presentation pass

Source: `00a52fcb`. The accepted period-interface recommendations are implemented for N64 and web.

The N64 keeps its 320×240 layout and compact bench/log font, with an 11px proportional body face, cream focus, distinct committed-card corners, stable previews, exact paginated rules, felt/mat/paper layers and related start/prize/result treatment. Existing coin/hit effects, rules, two-controller input and fenced presentation remain in place. Web uses proportional browser text and the same hierarchy, with scrollable exact inspection and clicked-card lock.

## Verification

See [validation.json](validation.json) and [post-VI provenance](post-vi/capture-provenance.json). Ten host UI scripts pass; all 228 card records and 316 descriptions retain their non-whitespace contents and fit measured line widths. The diagnostic renders every exact rules page. Full native validation passes 34 maps, 42 framebuffers, actual duel/story/intro/cinematic and SRAM at 4 MiB. All 13 review stages have original ares post-VI PNGs checked against the callback RGB bytes.

The DIAG9 stress scenes are constructed display states using real card data; they are not represented as a played duel. Actual duel execution is separately covered by DIAG1 and host tests. The gallery displays raw 640×240 VI PNGs at 4:3. The contact sheet duplicates rows exactly to 640×480 without new filtering. Before-VI native images remain separate.

Web checks include a full compiled-C hotseat/AI/online/save/tutorial/deckbuilder smoke test, pointer selection, all seven banners, long-rule and 320px layout tests. Production viewport sizing was adjusted after an independent review found crowded boards could push actions below the screen; both entry points now pass six size/stress cases.

## Reproduce

Build `make -C port/frontends/n64/adventure BUILD_DIR=build/duel-ui-smoke ROM=poketcg-duel-ui-smoke DIAGNOSTIC=9 bundle`, then run `capture.py` with the 13 slugs in `tcgn64.c`. It uses the previously verified isolated debug-entitled ares v148 copy and its original PNG encoder, never the installed app or global settings. Raw callback buffers/logs remain in ignored build storage. Production ROM provenance in the manifest identifies the 320×240 build before the subsequent Expansion Pak experiment.
