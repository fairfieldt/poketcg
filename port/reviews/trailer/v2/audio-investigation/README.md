# Intro audio load gaps

The v1 music recording lost phase during the lab approach and later resumed about4.74 seconds behind the original music09 PCM. Sample correlation and native queue instrumentation distinguish that discontinuity from simple loudness or clipping.

A private baseline reproduced a361,391µs maximum audio-poll gap and one AI underrun. Eight1,272-frame buffers at31,995Hz hold about318ms. The largest gap was enough to exhaust them. Pumping only between complete scenes fixed the lab load but exposed a separate346ms first-island scene load.

The production fix polls after completed room/character loads and uses an optional scene load callback after each scan, texture and mesh batch. Calls happen after block recording ends and before a display frame is attached. The callback defaults to null for independent scene consumers. Codec, compressed assets, mixer channels and audio ring size stay unchanged.

The final private gates follow the actual opening and first island/lab/Lightning/Water/Fire loads at320×240/4MiB and480×360/8MiB. Both report zero underruns; their largest poll gaps are163,993 and164,543µs. In the8MiB gate, the first-island load gap is57.6ms and the lab load gap is34.4ms. [results.json](results.json) and the recorded runtime/VI markers contain the measurements. These are Ares CP0/AI results, not physical-console measurements.

The recorder also uses two low-priority encoding threads and independent input queues. The paired v2 edit uses the same clean original score track and sound-effect timings in both variants; no new music or artificial sound effects are added. This is an analytical timing/continuity check, not an auditory review by the model.
