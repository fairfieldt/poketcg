# Rounded head teaser

[Watch](poketcg64-trailer.mp4) · [Shot review](contact-sheet.jpg) · [Previous heads](../v4/poketcg64-trailer.mp4)

Fresh native N64 footage uses the rounded cast heads, Mark’s extra side/rear
depth, and Mason and the lab scientist’s smaller eyes. Distant-character models
retain the complete head geometry. The 1:04.57 edit retains the previous pacing and its original game audio.
The output is 1440×1080 at 60 fps, with 3,874 frames. The encoded AAC soundtrack
is byte-identical to v3. [Validation](validation.json) confirms frame count,
audio identity, fast-start playback, and error-free decoding.

The usual Steam Deck recording host was offline. The user selected local capture.
Four private Ares runs on macOS recorded the actual post-VI Metal output without
a CRT shader. Only each emulator window was recorded; window chrome was cropped.
No desktop, microphone, system audio, or personal diary was captured. The previous
CRT version remains unchanged in v3.

[Provenance](provenance.json) records fresh capture and ROM hashes, native event
markers, crop dimensions, and timing anchors. Ordered native events align the
new recordings to the established edit; frames are held or dropped without
motion interpolation. Initial presentation-phase calibration has approximately
one captured frame of uncertainty. The final fade holds the captured title tail
to preserve the exact frame count. [Build manifests and runtime logs](provenance/)
identify all four newly built capture ROMs. The capture builder always rebuilds
the filesystem archive and compares model and texture bytes extracted from the
packed ROM with the current inputs. The cursor is hidden and parked outside the
private window during recording, then restored. The club segment uses a
ScreenCaptureKit window filter with cursor capture disabled; the other three
segments retain the local CLI recordings. The current capture tool uses
ScreenCaptureKit for subsequent recordings. The private legal duel checkpoints,
actual Amy reward script, and progression warps are unchanged.

Reproduce from the repository root after building the Expansion Pak ROM:

```sh
for shot in intro ronald clubs amy; do
  python3 port/frontends/n64/adventure/tools/build_trailer.py --segment "$shot" --tag cast --base-ref HEAD
done
python3 port/frontends/n64/adventure/tools/trailer_capture_local.py all --version 5
python3 port/frontends/n64/adventure/tools/trailer_edit_v4.py prepare --version 5
python3 port/frontends/n64/adventure/tools/trailer_edit_v4.py render --version 5
python3 port/frontends/n64/adventure/tools/check_trailer_v4.py --version 5
```

Capture requires macOS screen-recording access and the existing isolated Ares
copy. Edit timing references the retained v3 timeline and the local v2 native
marker records. Raw window recordings remain under the ignored build directory;
final video, visual review, validation, and provenance are committed.
