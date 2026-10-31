# Original audio for the N64 port

`generated/` contains all **31 music IDs and 96 SFX IDs**, including stop and
unused entries, rendered from the original disassembled sound driver. Files
are stereo, 24 kHz, VADPCM WAV64 streams (about 30 MiB total).

The N64 links libdragon's WAV64/mixer code. Game Boy emulation is used only by
the offline asset build; none is included in the console runtime.

## Rebuild

Requirements: RGBDS, Python 3, libgme, and libdragon's `audioconv64`.
On this machine libgme was installed from Homebrew's `game-music-emu` formula.

```sh
python3 port/audio/tools/build_audio.py
python3 port/audio/tools/check_audio.py
```

The builder includes the original `src/audio/music1.asm`, `music2.asm`, and
`sfx.asm` unchanged, assembles their three banks at 1/2/3, and includes the
original bank callback and WRAM definitions. Only the harness, ROM bank numbers,
and callback section location differ from the game. `BANK()` expressions
resolve to the relocated banks, so original command streams, instruments,
channel behavior, and song tables run through the original driver.

The generated GBS timer period is **69,632 original CPU cycles** per sound
driver update, matching the game's 16,384 Hz timer divided by 68 and then 4
(60.235294 Hz). `libgme` renders its Game Boy APU with automatic silence
skipping disabled. Original driver bytecode is separately parsed to determine
finite track durations and the least common multiple of all looping channel
periods. Every music clip includes its full intro and a complete original loop;
credits and jingles remain finite. Finite clips include seven driver ticks
to finish the last register update and leave a short silence tail. SFX
frequency commands and explicit waits retain their original tick durations.

`audioconv64` encodes these waveforms to VADPCM, aligning loop points to its
16-sample blocks. This is a compressed, sampled rendering of the original
sound, rather than new synthesized approximations of its melodies. Playback
of a recorded loop does not keep the Game Boy oscillator/noise phase running
indefinitely across repetitions, and VADPCM is lossy.

## Runtime contract

Include `generated/audio_assets.h` for `tcg_music_paths[31]`,
`tcg_sfx_paths[96]`, and `tcg_sfx_priorities[96]`. Paths are numeric native IDs
under `rom:/audio/`. Loop flags and sample offsets are embedded in WAV64.

A numerically smaller nonzero SFX priority may interrupt a larger priority;
equal priorities replace each other. ID 0 stops playback. The N64 adapter
should retain original pause/resume behavior and service `mixer_poll` through
the audio output path while both the world and duel screens run.

`generated/audio.json` records original names/IDs, channel timing, loop
positions, raw PCM SHA-256 hashes, sample counts, and the assembled GBS hash.
`tools/check_audio.py` verifies every file's header and loop range, reparses all
original sequences, and rerenders representative music/SFX to verify identical
PCM hashes. `build/` holds temporary WAV files, assembly objects, and GBS data.

Primary API reference: [Game Music Emu](https://github.com/libgme/game-music-emu).
