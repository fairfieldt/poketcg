# Web title parity

The adventure title uses the shared N64 `assets/title64/logo-480.png` unchanged, placed at logical 20,12 with size 280×140 on the same 320×240 composition. The original intro scenes and Energy orbs retain their 160×144 coordinates inside the N64 frame at 40,12 with scale 1.5 and the original bounds. The copyright still comes from the original title image’s exact 0,110,160×34 crop. Press Start keeps its original 35,94 source position and blink timing. Opening durations, orb paths, audio events, two-step Start flow and button CSS are unchanged.

`pack_adventure.py` copies the shared logo to `dist/intro/title64-logo.png`. The source PNG SHA-256 is 90c6aec27e526e70e1afdb57825de73dff118f0d2cd1c721e7eb451f712a2dba. The web source modules were copied to dist; no WASM rebuild was needed.

Validation: `node port/frontends/web/test/title64.mjs` passes. It verifies the exact shared PNG hash, independently composed pixels for all four backdrops at 320×240 and 480×360, the original sprite clip, stable Start-button centering while pressed/skipping, first press skipping to the title, second press opening the diary menu, and original title-music ID 1. Actual app screenshots cover 1280×720 and 320×480. These are browser screenshots, not native VI captures.
