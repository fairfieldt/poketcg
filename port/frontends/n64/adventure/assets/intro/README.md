# Original opening and title

These are the English CGB graphics from the disassembly, rendered from its
tiles, tilemaps, object layouts, and palettes. The opening presents Charizard,
Scyther, and Aerodactyl booster packs, then the title and original PRESS START
sprite. No new title art or text is substituted.

The shared orb PNGs stay at their original 16x16 size. The N64 copies them
into an 18x18 surface with a transparent sampling border, then offsets the
draw by one source pixel to preserve the original position. Transparent
pixels retain the outline's RGB and the orb draw uses alpha blending, so
bilinear filtering keeps the round edge without a clipped rim or dark halo.

`intro_assets.h` retains all 26 original animation tables (191 through 216),
the five sets of orb coordinates, and their animation IDs. `intro.c` applies
the source frame durations and translations, including 8-bit OAM wrapping,
the CGB idle-orb reveal, 16-tick white fades, 37/26-tick prompt blinking, and
the original intro sounds and title music. It presents two original animation
ticks per 30 Hz N64 frame, centered at 240x216 with the source aspect ratio.

A or START skips the opening to the title; another press opens the diary
selector after the source's one-second title input delay. An unattended title
returns to the opening when its music ends. Returning from the credits shows
the title directly.

Regenerate with the linked source ROM and symbols available:

```sh
python3 port/frontends/n64/adventure/tools/build_intro.py
```

Verify the checked-in assets without a ROM:

```sh
python3 port/frontends/n64/adventure/tools/build_intro.py --check
```

`adventure_intro_check()` verifies the native animation interpreter's wrap,
stop/hide, title-arrival, and fade boundaries. The native diagnostic can use
`adventure_intro_preview(scene, tick)` to capture completed RDP frames for all
four backgrounds. Runtime frames are RDP-only and finish rendering before
they are handed to the display subsystem.
