# Gameplay trailer

[Watch the trailer](poketcg64-trailer.mp4) · [Contact sheet](contact-sheet.jpg) · [Title poster](poster.png)

The final video is **1:57.3**, 1440×1080 at 60 recorded frames per second, H.264 with stereo 48 kHz AAC. The 480×360 Expansion Pak game image retains its 4:3 shape. Game cadence is preserved; there is no motion interpolation or speed change.

Mark runs to the laboratory, speaks with Mason and Sam, and reaches the moment before the practice duel. A dissolve leads to a real Ronald knockout and Prize. Lightning, Water and Fire walks lead into the Water Medal, Hall of Honor and the actual animated title screen. Both FPS overlays and emulator chrome are absent. No trailer captions or fabricated gameplay graphics were added.

This is actual Ares shader output from private capture ROMs based on production commit `2dda70b3`. It uses the installed **CRT Guest Advanced** preset with `interm = 0.0`, matching the Steam Machine. The preset SHA-256 before and after capture was `9f88fd3af570f50659b10199fc7b08e21c0575a5d8b07e285e827807b32bdfc1`. An `openat` trace confirms the actual override, shader passes and LUTs loaded by Ares; [capture.json](provenance/capture.json) includes their paths, runtime version, raw-master hashes and ROM hashes. Ares resolves its installed user-data override even when the capture supplies a private XDG directory. That override was read only and remained unchanged.

The footage is a montage. Ronald's untouched checkpoint was produced by legal engine play, then continued through real controller requests. Club visits use private room/progression fixtures. Amy's win is declared in private state to run her actual reward script, and Hall of Honor uses a progression warp. [Route provenance](provenance/README.md) and [all four native flow gates](../trailer-harness/) make those boundaries explicit. User diaries, saves and the active Steam session were not used.

All music and effects originate in the original game's sound driver. Intro and duel audio retain the actual captured mix and effects. One continuous original Club 1 theme crosses the three club cuts. The complete 5.396-second medal stem plays before Hall music enters. The title uses its complete closing phrase, 24.636719–34.05 seconds, which resolves to the original written tonic cadence. [timeline.json](timeline.json) records cuts, dissolves and fixed gains. No compressor, limiter or generated sound was used. The [original audio manifest](provenance/original-audio.json) records PCM verification and hashes.

Final decode and format checks pass: 7,038 video frames, matching 117.3-second audio/video durations, streaming metadata before media data, no decoding errors or nonfinite samples. Integrated loudness is **−19.5 LUFS**, true peak **−5.7 dBFS**, loudness range **3.4 LU**. Short periodic silent rests in the duel match the original theme; the final silence accompanies its resolved tail. See [validation.json](validation.json) and the [measurement log](validation-audio-video.log). The parent reviewed the actual draft at 12 anchors; the final 20-frame contact sheet was inspected. This model cannot receive audio, so these are analytical and original-score checks, not an ear audition.

Reproduction, from the repository root with the existing N64/audio toolchain and Steam Machine access:

```sh
python3 port/frontends/n64/adventure/tools/build_trailer.py --segment all
python3 port/frontends/n64/adventure/tools/stage_trailer_audio.py
python3 port/frontends/n64/adventure/tools/trailer_capture.py prepare --name intro --warmup 2 --seconds 88 --rom port/frontends/n64/adventure/build/trailer-intro/trailer-intro.z64
python3 port/frontends/n64/adventure/tools/trailer_capture.py proof --name intro
```

Repeat capture for `ronald` (32 seconds), `clubs` (34 seconds) and `ending` (43 seconds) with their corresponding private ROMs, then run `python3 port/frontends/n64/adventure/tools/trailer_edit.py`. Remote lossless masters remain in `/home/deck/.cache/poketcg-trailer-20261031/`; each profile has private settings and saves. The recorder routes only its verified Flatpak audio instance to a temporary sink and unloads that sink afterward. No capture process or temporary sink remains running.

Final MP4 SHA-256: `aeb648b7047bb78f5934d2e09ba80cabcb3102526a6d85ce5015ed41f08eeb0f`.
