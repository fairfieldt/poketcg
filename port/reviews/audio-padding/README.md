# Intro audio correction

The installed `audioconv64` stereo padding path erased 16 stereo frames near
the midpoint of a clip whose length was 16 modulo 32. Its decoded title asset
contained a 0.667 ms dropout at 17.025 seconds. Re-encoding the original title
WAV reproduced the shipped WAV64 byte for byte, so this defect exists in the
asset independently of emulator timing.

The project now copies any loop alignment frames and zero-pads complete
32-frame transport blocks before conversion, then restores the exact original
playback/loop header lengths. All 127 original PCM hashes, source timings,
and WAV64 playback headers are unchanged; 54 encoded assets were corrected.

The title's affected 16-frame RMS was 9,166.84 in original PCM, 387.87 in the
old decode, and 9,087.23 after correction. [validation.json](validation.json)
records every old/new asset hash and the source/header preservation checks.

Validation passed with the existing cache and with an empty temporary source
cache. The regression checks cover stereo finite clips, loop realignment,
transport tail zeros, and the actual title decode. Run:

```sh
python3 port/audio/tools/build_audio.py --cached
python3 port/audio/tools/check_audio.py
```

Diagnostic 14 runs the production intro and the complete title sample while
measuring actual emulated AI queue starvation. Its runner uses private ares
settings without debugger/frame breakpoints:

```sh
make -C port/frontends/n64/adventure BUILD_DIR=build/audio-review \
  ROM=poketcg-audio-review HIGH_RES=1 DIAGNOSTIC=14 -j4 bundle
python3 port/audio/tools/review_intro.py
```

The asset fix establishes a concrete click source. Emulated AI queue state
does not measure host audio-device underruns or replace a listening test.

The 480×360 Expansion Pak diagnostic completed in 53.25 wall-clock seconds.
It observed zero AI starvation spells across 1,378 polls / 1,159 mixed output
buffers, with a maximum poll gap of 33.68 ms and maximum mixer time of 2.08 ms.
The unchanged eight-buffer output ring therefore covered the measured intro
without starvation. [runtime-validation.json](runtime-validation.json) and
[runtime.log](runtime.log) preserve the run; host audio output was not captured.
