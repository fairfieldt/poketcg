# Audio asset and buffering follow-up

Read-only source/SDK investigation, 2026-10-31. No audio source, generated asset, SDK, ROM, or emulator setting was changed. Temporary conversions below used the already installed `audioconv64` and wrote only OS scratch directories.

## Confirmed stereo padding defect

The installed libdragon converter has a wrong pointer in `tools/audioconv64/conv_wav64.cpp:255–263` when padding VADPCM input to 32 sample frames:

```cpp
memset(wav->samples + wav->cnt, 0,
       (newcnt - wav->cnt) * wav->channels * sizeof(int16_t));
```

`wav->samples` points to interleaved `int16_t` samples. For stereo, the destination needs `wav->cnt * wav->channels`. When the input frame count is 16 modulo 32, the installed converter zeroes **16 stereo frames around the middle of the clip**, while the appended padding is left uninitialized. This is an asset defect independent of game/emulator frame timing.

Two synthetic nonzero stereo signals, 3,200 versus 3,216 frames, were converted with `/opt/libdragon/bin/audioconv64 --debug --wav-compress 1 --wav-loop false -o SCRATCH INPUT.wav`. The 3,216-frame decode falls almost silent at frames 1,608–1,623, recovering at 1,624. The 3,200-frame version does not. Mean absolute magnitude in the affected window was 261.8 versus 7,162.6 in the control signal.

More decisively, converting the existing `port/audio/build/music_01.wav` with those same options produces a WAV64 **identical in SHA-256 to the shipped `port/audio/generated/music_01.wav64`**. The converter's decoded debug WAV shows:

| Title samples / seconds | Source PCM RMS | Decoded WAV64 RMS |
|---|---:|---:|
| 408,584–408,599 / 17.0243 s | 3,269.40 | 3,130.65 |
| **408,600–408,615 / 17.0250 s** | **9,166.84** | **387.87** |
| 408,616–408,631 / 17.0257 s | 4,170.69 | 4,086.05 |

The title has 817,200 frames (16 modulo 32). Finite intro orb and orb-title SFX also have 16-modulo-32 lengths; the swoop is a multiple of 32. The defect affects 63 of 127 exported clip headers, including six looping songs after converter loop adjustment. A 16-frame dropout lasts about 0.667 ms at 24 kHz and can produce a click. This proves a concrete artifact in the music, although it does not establish that every user-reported skip has this cause.

The generated PCM source hashes do not catch this: `check_audio.py` currently verifies original driver PCM, not decoded compressed WAV64. A converter fix or project-owned preprocessing must be followed by compressed-output comparison. Prepadding finite source PCM to 32 frames avoids the defect. For looping clips, consider the converter's additional loop adjustment below; length padding alone can be insufficient.

## Loop alignment and validation

Inspected all 127 headers and VADPCM extension skip tables, without running the production validator (it rebuilds/renders inputs): all are version 4, stereo 16-bit, 24 kHz, VADPCM with Huffman. All lengths are multiples of 16. **All 16 looping clips have actual header loop starts aligned to 32 frames and matching saved decoder skip-state entries.** No current bad loop-start alignment was found.

`build_audio.py` requests loop offsets at 16-frame boundaries. The converter correctly rounds those forward to 32, copies skipped loop samples to the end, and records decoder state. That can add 16 to the input length and trigger the separate padding defect. Preserve this seam handling when fixing the converter/preprocessor.

A header length that is 16 modulo 32 is not automatically invalid: the converter writes the logical header before padding encoded payload to 32. Do not add a blanket `length % 32 == 0` assertion. Add explicit version/format, loop-start alignment and skip-state checks, plus decoded-output verification, rather than relying only on approximate metadata offsets.

## Playback hazards separate from the intro defect

The output ring has eight ~40 ms stereo buffers (~320 ms only when filled). The mixer samplebuffer is much larger than one output block by default: maximum 32 kHz × 16 bits × stereo / 8 expected polls/s = 16,000 bytes, or 4,000 stereo source frames. A ~40 ms output block uses ~960 source frames at 24 kHz. No undersized default samplebuffer or missing high-priority mixer queue was identified. VADPCM reads already round decoded requests and preserve alignment; mixer work uses the RSP high-priority queue.

There is a separate arbitrary-seek hazard in `audio.c` pause/resume: save a current sample position, stop, reopen/replay the VADPCM music, then call `mixer_ch_set_pos` at that arbitrary position. A fresh VADPCM decode accepts position zero or an exact saved skip point; most paused positions have neither. Pause/PC/Card Pop flows use these events. This is source-confirmed misuse, not a reproduced intro cause. Resume by retaining valid decoder/buffer state or decoding forward from a known checkpoint, rather than assuming compressed samples support arbitrary seek.

Continue checking startup queue prefill, long waits/loads, output queue low-water and ares audio pacing. The intro's simple 2D renderer does not justify attributing its startup artifacts to the world geometry budget.
