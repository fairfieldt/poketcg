# Teaser v6

The final movie is 1:12.58 (4,355 frames). Full audio/video decoding passes;
its measured true peak is −5.2 dBFS, and the legendary sound cue matches the
glow marker to within one output frame.

Fresh local N64 captures add Ronald's original challenge before his battle,
then a longer walk to the legendary card, a held golden glow, and the original
legendary-card sound synchronized to the glow. The glow is rendered by the
N64 as twenty translucent triangles; it is not a video overlay.

All four capture ROMs use the 480×360 Expansion Pak mode. The final file is
1440×1080 at 60 fps; native gameplay targets 30 fps. The editor follows fresh
native VI markers and holds or drops recorded frames without motion
interpolation. The previous v5 capture also used 480×360, rather than 320×240.

The window recorder excludes the cursor, desktop, microphone, and system
sound. Weave deinterlacing and interframe blending are disabled. Captures must
cover the native end marker; a frozen or truncated capture fails validation.
The Mac's locked session froze early probes, so final captures were made after
the user unlocked it. Failed probes are excluded from the edit.

The cast uses the [durable preview-shadow fix](../../actor-ground-shadow-regression/README.md).
The old shape-specific recognizer missed the new twelve-sided preview disks;
full and distant runtime exports now exclude authored shadow meshes by ownership,
and N64/web builds reject regressions. Both native resolutions were checked.

[Shot review](contact-sheet.jpg) · [Frame timing](performance.json) ·
[Validation](validation.json) · [Timeline](timeline.json) ·
[Audio recipe](audio-recipe.json) · [Capture provenance](provenance.json)

Reproduce from the repository root:

```sh
make -C port/frontends/n64/adventure expansion
python3 port/frontends/n64/adventure/tools/build_trailer.py --segment all --tag cast --base-ref HEAD
python3 port/frontends/n64/adventure/tools/trailer_capture_local.py all --version 6
python3 port/frontends/n64/adventure/tools/trailer_edit_v6.py prepare
python3 port/frontends/n64/adventure/tools/trailer_edit_v6.py render
python3 port/frontends/n64/adventure/tools/check_trailer_v6.py
```

The private capture ROMs use the unchanged legal Ronald and Amy checkpoints.
Their native logs, packed-asset verification, and capture coverage checks are
retained with this review. The final audio uses original game-driver PCM;
its recipe records source hashes, cue positions, and gains.
