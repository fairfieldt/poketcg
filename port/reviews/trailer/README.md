# Gameplay trailers

[Compare](compare.html) · [CRT](poketcg64-trailer.mp4) · [No CRT](poketcg64-trailer-no-crt.mp4)

This 1:04.57 edit trims 15 waiting frames before Mark's walk to the table. Ronald now shows only the attack and knockout, then cuts straight to Lightning. Lightning → Fire → Water and the final Hall shot begin with Mark already moving. Amy's insert is a 2.2-second hit/knockout directly into the medal. The full original medal fanfare and resolved title music remain.

Both 1440×1080, 60 fps versions use the same native-clock timeline and one encoded AAC mix. The CRT version uses the recorded Guest Advanced output with `interm = 0.0`; the other uses the separately recorded shader-disabled output. Only existing v2 footage and original game audio are used. No emulator, runtime, capture or Steam setting changed for this pass.

The existing recorded visuals are deliberately retained; the separate later runtime shadow correction is not included in these takes. [V2 capture provenance](v2/provenance/capture.json), legal duel checkpoint proofs, shader identity and source waveform verification remain applicable. [timeline.json](timeline.json) and [audio-recipe.json](audio-recipe.json) record this edit; [pair-validation.json](pair-validation.json) records final formats, frame counts and identical compressed/decoded audio.

The real captured duel effects remain in the mix. One continuous original Club 1 theme crosses the three clubs; fixed gains and short fades preserve the original dynamics. Final analytical checks show −19.7 LUFS, −6.5 dBTP and no decode errors. The model cannot receive audio; these are waveform, source-score and analytical checks rather than an ear audition. [validation.json](validation.json) records the result.

This montage uses private fixtures. Ronald and Amy continue untouched, legally generated duel checkpoints; Amy’s final knockout returns a natural player win and runs her actual medal reward script. Club entries and Hall access use progression/room warps. No user diary, card, HP or RNG was changed.

The full v2 pair is preserved in [versions/v2](versions/v2/README.md), with its original editor and metadata unchanged. V3 is reproduced with `python3 tools/trailer_edit_v3.py prepare`, then `video-crt`, `video-none`, `audio`, and `mux`, from the adventure frontend directory. The shared `check_trailer_pair.py` verifies the result. Source recordings are selected by [capture-sources.json](capture-sources.json).
